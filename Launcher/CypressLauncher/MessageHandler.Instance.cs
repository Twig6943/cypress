#nullable enable
using System;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

public partial class MessageHandler
{
	private void OnSendCommand(int pid, string cmd)
	{
		if (string.IsNullOrWhiteSpace(cmd)) return;
		lock (m_instanceLock)
		{
			if (m_instances.TryGetValue(pid, out var instance))
				instance.SendCommand(cmd);
			else if (m_externalInstances.TryGetValue(pid, out var ext))
				ext.SendCommand(cmd);
		}
	}

	private void OnGetLocalBans()
	{
		var path = GetUnifiedBanlistPath();
		var result = new JObject { ["type"] = "localBansResult" };
		try
		{
			if (File.Exists(path))
			{
				var raw = JArray.Parse(File.ReadAllText(path));
				result["bans"] = raw;
			}
			else
			{
				result["bans"] = new JArray();
			}
		}
		catch (Exception ex)
		{
			result["bans"] = new JArray();
			result["error"] = ex.Message;
		}
		Send(result);
	}

	private void OnKillInstance(int pid)
	{
		lock (m_instanceLock)
		{
			if (m_instances.TryGetValue(pid, out var instance))
				instance.Kill();
			else if (m_externalInstances.TryGetValue(pid, out var ext))
				ext.Kill();
		}
	}

	private void OnGetInstances()
	{
		var arr = new JArray();
		lock (m_instanceLock)
		{
			foreach (var kvp in m_instances)
			{
				var inst = kvp.Value;
				arr.Add(new JObject
				{
					["pid"] = inst.Pid,
					["game"] = inst.Game,
					["isServer"] = inst.IsServer,
					["startTime"] = inst.StartTime.ToString("o")
				});
			}
		}
		Send(new JObject { ["type"] = "instances", ["list"] = arr });
	}

	private void OnCheckServer(string address, string? relayAddress = null, string? relayKey = null)
	{
		if (string.IsNullOrWhiteSpace(address)) return;

		// for relay servers, connect to the relay's side-channel tunnel port
		bool isRelay = !string.IsNullOrEmpty(relayAddress) && !string.IsNullOrEmpty(relayKey);
		string host;
		int port;

		if (isRelay)
		{
			// parse relay address (host:port format, port is the UDP relay port, we want TCP 14638)
			string ra = relayAddress!;
			int ci = ra.LastIndexOf(':');
			host = ci > 0 ? ra.Substring(0, ci) : ra;
			port = 14638;
		}
		else
		{
			host = address;
			port = 14638;
			int colonIdx = address.LastIndexOf(':');
			if (colonIdx > 0 && int.TryParse(address.Substring(colonIdx + 1), out int parsedPort))
			{
				host = address.Substring(0, colonIdx);
				port = parsedPort;
			}
		}

		Task.Run(async () =>
		{
			try
			{
				using var client = new TcpClient();
				var sw = Stopwatch.StartNew();
				var connectTask = client.ConnectAsync(host, port);
				if (await Task.WhenAny(connectTask, Task.Delay(3000)) != connectTask)
					throw new TimeoutException();
				await connectTask;
				sw.Stop();
				long connectMs = sw.ElapsedMilliseconds;

				var stream = client.GetStream();
				stream.ReadTimeout = 3000;
				stream.WriteTimeout = 3000;

				// if relay, send tunnel handshake first so relay routes us to the real server
				if (isRelay)
				{
					var handshakeObj = new JObject { ["type"] = "relay", ["key"] = relayKey };
					string? ticket = FetchAuthTicketSync();
					if (ticket != null)
						handshakeObj["ticket"] = ticket;
					byte[] handshake = Encoding.UTF8.GetBytes(handshakeObj.ToString(Newtonsoft.Json.Formatting.None) + "\n");
					await stream.WriteAsync(handshake, 0, handshake.Length);
				}

				byte[] request = Encoding.UTF8.GetBytes("{\"type\":\"serverInfo\"}\n");
				await stream.WriteAsync(request, 0, request.Length);

				var buf = new byte[65536];
				var sb = new StringBuilder();
				JObject? info = null;
				while (info == null)
				{
					int bytesRead = await stream.ReadAsync(buf, 0, buf.Length);
					if (bytesRead <= 0) break;
					sb.Append(Encoding.UTF8.GetString(buf, 0, bytesRead));
					int newline;
					while ((newline = sb.ToString().IndexOf('\n')) >= 0)
					{
						string line = sb.ToString().Substring(0, newline).Trim();
						sb.Remove(0, newline + 1);
						if (string.IsNullOrEmpty(line)) continue;
						try
						{
							var j = JObject.Parse(line);
							if ((string?)j["type"] == "serverInfo")
							{
								info = j;
								break;
							}
							// skip challenge and other non-serverInfo messages
						}
						catch { }
					}
				}

				if (info != null)
				{
					info["type"] = "serverInfo";
					info["ok"] = true;
					info["address"] = address;
					info["ping"] = connectMs;
					Send(info);
				}
				else
				{
					Send(new JObject { ["type"] = "serverInfo", ["ok"] = false, ["address"] = address });
				}
			}
			catch
			{
				Send(new JObject { ["type"] = "serverInfo", ["ok"] = false, ["address"] = address });
			}
		});
	}

	private void OnSaveServerList(JArray? servers)
	{
		try
		{
			string filePath = Path.Combine(GetAppdataDir(), s_launcherSavedataFilename);
			JObject root = new JObject();
			if (File.Exists(filePath))
				root = JObject.Parse(File.ReadAllText(filePath));
			root["ServerList"] = servers ?? new JArray();
			File.WriteAllText(filePath, root.ToString());
		}
		catch { }
	}

	private void OnAttachInstance(string address, int port, int pid)
	{
		Task.Run(async () =>
		{
			try
			{
				using var probeClient = new TcpClient();
				var connectTask = probeClient.ConnectAsync(address, port);
				if (await Task.WhenAny(connectTask, Task.Delay(3000)) != connectTask)
					throw new TimeoutException();
				await connectTask;

				var stream = probeClient.GetStream();
				stream.ReadTimeout = 3000;
				stream.WriteTimeout = 3000;

				byte[] request = Encoding.UTF8.GetBytes("{\"type\":\"serverInfo\"}\n");
				await stream.WriteAsync(request, 0, request.Length);

				var buf = new byte[65536];
				var sb = new StringBuilder();
				JObject? info = null;
				while (info == null)
				{
					int bytesRead = await stream.ReadAsync(buf, 0, buf.Length);
					if (bytesRead <= 0) break;
					sb.Append(Encoding.UTF8.GetString(buf, 0, bytesRead));
					int newline;
					while ((newline = sb.ToString().IndexOf('\n')) >= 0)
					{
						string ln = sb.ToString().Substring(0, newline).Trim();
						sb.Remove(0, newline + 1);
						if (string.IsNullOrEmpty(ln)) continue;
						try
						{
							var j = JObject.Parse(ln);
							if ((string?)j["type"] == "serverInfo") { info = j; break; }
						}
						catch { }
					}
				}

				if (info == null)
				{
					SendStatus("Could not get info from instance", "error");
					return;
				}

				string game = (string?)info["game"] ?? "GW2";
				bool isServer = !(bool)(info["isClient"] ?? false);
				int actualPort = (int)(info["port"] ?? port);
				int instancePid = pid;

				lock (m_instanceLock)
				{
					if (m_instances.ContainsKey(instancePid) || m_externalInstances.ContainsKey(instancePid))
					{
						SendStatus("Instance already tracked", "warning");
						return;
					}
				}

				var ext = new ExternalInstance(
					instancePid, game, isServer, address, actualPort,
					onOutput: (p, outputLine) =>
					{
						try { Send(new JObject { ["type"] = "instanceOutput", ["pid"] = p, ["line"] = outputLine }); }
						catch { }
					},
					onExit: (p) =>
					{
						lock (m_instanceLock)
						{
							if (m_externalInstances.Remove(p, out var inst))
								inst.Dispose();
						}
						try { Send(new JObject { ["type"] = "instanceExited", ["pid"] = p, ["exitCode"] = 0 }); }
						catch { }
					}
				);

				if (!ext.Connect())
				{
					ext.Dispose();
					SendStatus("Failed to connect to instance", "error");
					return;
				}

				lock (m_instanceLock)
				{
					m_externalInstances[instancePid] = ext;
				}

				Send(new JObject
				{
					["type"] = "instanceStarted",
					["pid"] = instancePid,
					["game"] = game,
					["isServer"] = isServer,
					["isExternal"] = true,
					["startTime"] = ext.StartTime.ToString("o"),
					["motd"] = (string?)info["motd"] ?? "",
					["modded"] = (bool)(info["modded"] ?? false)
				});

				SendStatus($"Attached to external {(isServer ? "server" : "client")} (PID {instancePid})", "success");
			}
			catch (Exception ex)
			{
				SendStatus("Failed to attach: " + ex.Message, "error");
			}
		});
	}

	private void OnDetachInstance(int pid)
	{
		lock (m_instanceLock)
		{
			if (m_externalInstances.Remove(pid, out var ext))
			{
				ext.Dispose();
				Send(new JObject { ["type"] = "instanceExited", ["pid"] = pid, ["exitCode"] = 0 });
				SendStatus($"Detached from instance (PID {pid})", "info");
			}
		}
	}

	private static async Task<int> ReadDiscoveryPortAsync(int pid, int timeoutMs = 10000)
	{
		string filePath = Path.Combine(Path.GetTempPath(), $"cypress_{pid}.port");
		int waited = 0;
		const int pollMs = 250;

		while (waited < timeoutMs)
		{
			if (File.Exists(filePath))
			{
				try
				{
					string content = await File.ReadAllTextAsync(filePath);
					var info = JObject.Parse(content.Trim());
					int port = (int)(info["port"] ?? 0);
					if (port > 0) return port;
				}
				catch { }
			}
			await Task.Delay(pollMs);
			waited += pollMs;
		}

		return 14638;
	}

	private void OnDetectInstances()
	{
		Task.Run(() =>
		{
			try
			{
				string tempDir = Path.GetTempPath();
				var discovered = new JArray();

				foreach (string file in Directory.GetFiles(tempDir, "cypress_*.port"))
				{
					try
					{
						string content = File.ReadAllText(file).Trim();
						var info = JObject.Parse(content);
						int pid = (int)(info["pid"] ?? 0);
						int port = (int)(info["port"] ?? 0);
						string game = (string?)info["game"] ?? "";
						bool isServer = (bool)(info["isServer"] ?? false);

						if (pid <= 0 || port <= 0) continue;

						try
						{
							var proc = Process.GetProcessById(pid);
							if (proc.HasExited) continue;
						}
						catch (ArgumentException)
						{
							try { File.Delete(file); } catch { }
							continue;
						}

						bool alreadyTracked;
						lock (m_instanceLock)
						{
							alreadyTracked = m_instances.ContainsKey(pid) || m_externalInstances.ContainsKey(pid);
						}
						if (alreadyTracked) continue;

						discovered.Add(new JObject
						{
							["pid"] = pid,
							["port"] = port,
							["game"] = game,
							["isServer"] = isServer
						});
					}
					catch { }
				}

				Send(new JObject { ["type"] = "detectedInstances", ["instances"] = discovered });
			}
			catch (Exception ex)
			{
				Send(new JObject { ["type"] = "detectedInstances", ["instances"] = new JArray(), ["error"] = ex.Message });
			}
		});
	}
}
