#nullable enable
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

public partial class MessageHandler
{
	private static readonly string s_updateSavedataKey = "Updates";

	private sealed class UpdateChannel
	{
		public string Id;
		public string DisplayName;
		public string RepoOwner;
		public string RepoName;
		public string LocalVersion;
		public string? AssetPattern;

		public string? LatestTag;
		public string? LatestBody;
		public string? AssetUrl;
		public long AssetSize;

		public UpdateChannel(string id, string displayName, string repoOwner, string repoName, string localVersion, string? assetPattern = null)
		{
			Id = id;
			DisplayName = displayName;
			RepoOwner = repoOwner;
			RepoName = repoName;
			LocalVersion = localVersion;
			AssetPattern = assetPattern;
		}
	}

	private UpdateChannel[] GetUpdateChannels()
	{
		string launcherVersion = Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "0.0.0";
		string serverVersion = GetSavedServerDllVersion();

		return new[]
		{
			new UpdateChannel("launcher", "Cypress Launcher", "PvZ-Cypress", "Cypress", launcherVersion),
			new UpdateChannel("server", "Cypress Server", "PvZ-Cypress", "Cypress", serverVersion)
		};
	}

	private string GetSavedServerDllVersion()
	{
		try
		{
			string filePath = Path.Combine(GetAppdataDir(), s_launcherSavedataFilename);
			if (!File.Exists(filePath)) return "0.0.0";
			var root = JObject.Parse(File.ReadAllText(filePath));
			var updates = root[s_updateSavedataKey] as JObject;
			return (string?)updates?["server_dll_version"] ?? "0.0.0";
		}
		catch { return "0.0.0"; }
	}

	private void SaveServerDllVersion(string version)
	{
		try
		{
			string filePath = Path.Combine(GetAppdataDir(), s_launcherSavedataFilename);
			JObject root = new JObject();
			if (File.Exists(filePath))
				root = JObject.Parse(File.ReadAllText(filePath));
			var updates = root[s_updateSavedataKey] as JObject ?? new JObject();
			updates["server_dll_version"] = version;
			root[s_updateSavedataKey] = updates;
			File.WriteAllText(filePath, root.ToString());
		}
		catch { }
	}

	private static bool IsNewerVersion(string local, string remote)
	{
		// strip leading v
		local = local.TrimStart('v', 'V');
		remote = remote.TrimStart('v', 'V');

		if (Version.TryParse(local, out var localVer) && Version.TryParse(remote, out var remoteVer))
			return remoteVer > localVer;

		return false;
	}

	private void OnCheckUpdates()
	{
		Task.Run(async () =>
		{
			var channels = GetUpdateChannels();
			var updates = new JArray();

			foreach (var ch in channels)
			{
				try
				{
					var request = new HttpRequestMessage(HttpMethod.Get,
						$"https://api.github.com/repos/{ch.RepoOwner}/{ch.RepoName}/releases/latest");
					request.Headers.Add("User-Agent", "CypressLauncher");
					request.Headers.Add("Accept", "application/vnd.github+json");

					var resp = await s_httpClient.SendAsync(request);
					if (!resp.IsSuccessStatusCode) continue;

					var json = JObject.Parse(await resp.Content.ReadAsStringAsync());
					string? tag = (string?)json["tag_name"];
					if (tag == null) continue;

					if (!IsNewerVersion(ch.LocalVersion, tag)) continue;

					ch.LatestTag = tag;
					ch.LatestBody = (string?)json["body"] ?? "";

					// find zip asset
					var assets = json["assets"] as JArray;
					if (assets != null)
					{
						foreach (var asset in assets)
						{
							string? name = (string?)asset["name"];
							if (name == null) continue;
							if (!name.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) continue;

							// if pattern set, match it; otherwise just take first zip
							if (ch.AssetPattern != null && !name.Contains(ch.AssetPattern, StringComparison.OrdinalIgnoreCase))
								continue;

							ch.AssetUrl = (string?)asset["browser_download_url"];
							ch.AssetSize = (long)(asset["size"] ?? 0);
							break;
						}
					}

					if (ch.AssetUrl == null) continue;

					updates.Add(new JObject
					{
						["channel"] = ch.Id,
						["name"] = ch.DisplayName,
						["currentVersion"] = ch.LocalVersion,
						["latestVersion"] = ch.LatestTag,
						["releaseNotes"] = ch.LatestBody,
						["assetUrl"] = ch.AssetUrl,
						["assetSize"] = ch.AssetSize
					});
				}
				catch { }
			}

			Send(new JObject { ["type"] = "updateCheckResult", ["updates"] = updates });
		});
	}

	private void OnStartUpdate(JObject msg)
	{
		string channel = (string?)msg["channel"] ?? "";
		string assetUrl = (string?)msg["assetUrl"] ?? "";
		string latestVersion = (string?)msg["latestVersion"] ?? "";

		if (string.IsNullOrEmpty(channel) || string.IsNullOrEmpty(assetUrl))
		{
			Send(new JObject { ["type"] = "updateError", ["channel"] = channel, ["error"] = "Missing update info" });
			return;
		}

		Task.Run(async () =>
		{
			try
			{
				string tempDir = Path.Combine(Path.GetTempPath(), "cypress-update", channel);
				if (Directory.Exists(tempDir)) Directory.Delete(tempDir, true);
				Directory.CreateDirectory(tempDir);

				string zipPath = Path.Combine(tempDir, "update.zip");

				// download with progress
				var request = new HttpRequestMessage(HttpMethod.Get, assetUrl);
				request.Headers.Add("User-Agent", "CypressLauncher");
				var resp = await s_httpClient.SendAsync(request, HttpCompletionOption.ResponseHeadersRead);
				resp.EnsureSuccessStatusCode();

				long totalBytes = resp.Content.Headers.ContentLength ?? -1;
				long downloaded = 0;
				int lastPercent = -1;

				using (var contentStream = await resp.Content.ReadAsStreamAsync())
				using (var fileStream = new FileStream(zipPath, FileMode.Create, FileAccess.Write, FileShare.None, 8192, true))
				{
					var buffer = new byte[65536];
					int bytesRead;
					while ((bytesRead = await contentStream.ReadAsync(buffer, 0, buffer.Length)) > 0)
					{
						await fileStream.WriteAsync(buffer, 0, bytesRead);
						downloaded += bytesRead;

						if (totalBytes > 0)
						{
							int percent = (int)(downloaded * 100 / totalBytes);
							if (percent != lastPercent)
							{
								lastPercent = percent;
								Send(new JObject { ["type"] = "updateProgress", ["channel"] = channel, ["percent"] = percent });
							}
						}
					}
				}

				// extract
				string extractDir = Path.Combine(tempDir, "extracted");
				ZipFile.ExtractToDirectory(zipPath, extractDir, true);

				// if the zip contains a single top-level folder, use its contents
				var topDirs = Directory.GetDirectories(extractDir);
				var topFiles = Directory.GetFiles(extractDir);
				if (topDirs.Length == 1 && topFiles.Length == 0)
					extractDir = topDirs[0];

				if (channel == "server")
				{
					ApplyServerUpdate(extractDir, latestVersion);
				}
				else if (channel == "launcher")
				{
					ApplyLauncherUpdate(extractDir);
				}

				Send(new JObject { ["type"] = "updateComplete", ["channel"] = channel, ["version"] = latestVersion });
			}
			catch (Exception ex)
			{
				Send(new JObject { ["type"] = "updateError", ["channel"] = channel, ["error"] = ex.Message });
			}
		});
	}

	private void ApplyServerUpdate(string extractDir, string version)
	{
		// copy DLLs to the launcher directory (next to the exe)
		// they get copied to the game directory as dinput8.dll at launch time
		string installDir = AppContext.BaseDirectory;
		string[] games = { "GW1", "GW2", "BFN" };
		foreach (string game in games)
		{
			string dllName = $"cypress_{game}.dll";
			string srcPath = Path.Combine(extractDir, dllName);
			if (!File.Exists(srcPath))
			{
				var found = Directory.GetFiles(extractDir, dllName, SearchOption.AllDirectories).FirstOrDefault();
				if (found != null) srcPath = found;
				else continue;
			}

			string destPath = Path.Combine(installDir, dllName);
			File.Copy(srcPath, destPath, true);
		}

		SaveServerDllVersion(version.TrimStart('v', 'V'));
		SendStatus("Server DLLs updated to " + version, "info");
	}

	private void ApplyLauncherUpdate(string extractDir)
	{
		string installDir = AppContext.BaseDirectory;
		string tempDir = Path.Combine(Path.GetTempPath(), "cypress-update", "launcher");

		// write a powershell script that waits for us to exit, copies files, restarts
		string scriptPath = Path.Combine(tempDir, "apply.ps1");
		int pid = Environment.ProcessId;

		string script = $@"
$ErrorActionPreference = 'Stop'
try {{
    $proc = Get-Process -Id {pid} -ErrorAction SilentlyContinue
    if ($proc) {{ $proc.WaitForExit(30000) | Out-Null }}
}} catch {{}}
Start-Sleep -Milliseconds 500
Copy-Item -Path '{extractDir}\*' -Destination '{installDir}' -Recurse -Force
Start-Process '{Path.Combine(installDir, "CypressLauncher.exe")}'
";
		File.WriteAllText(scriptPath, script, Encoding.UTF8);

		var psi = new ProcessStartInfo
		{
			FileName = "powershell.exe",
			Arguments = $"-ExecutionPolicy Bypass -WindowStyle Hidden -File \"{scriptPath}\"",
			UseShellExecute = true,
			CreateNoWindow = true
		};
		Process.Start(psi);

		// exit the launcher so the script can overwrite files
		SendStatus("Restarting to apply update...", "info");
		Environment.Exit(0);
	}
}
