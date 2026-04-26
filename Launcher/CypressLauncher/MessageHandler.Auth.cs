#nullable enable
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web;
using Newtonsoft.Json.Linq;
#if WINDOWS
using Microsoft.Win32;
#endif

namespace CypressLauncher;

public partial class MessageHandler
{
	private const string EA_CLIENT_ID = "JUNO_PC_CLIENT";
	private const string EA_CLIENT_SECRET = "4mRLtYMb6vq9qglomWEaT4ChxsXWcyqbQpuBNfMPOYOiDmYYQmjuaBsF2Zp0RyVeWkfqhE9TuGgAw7te";
	private const string EA_AUTH_URL = "https://accounts.ea.com/connect/auth";
	private const string EA_TOKEN_URL = "https://accounts.ea.com/connect/token";
	private const string EA_REDIRECT_URI = "qrc:///html/login_successful.html";

	// persisted auth state
	private string? m_authToken;
	private string? m_authPid;
	private string? m_authUid;
	private string? m_authDisplayName;
	private double m_authExpiresAt;

	// pkce state for in-flight login
	private string? m_codeVerifier;

	private void OnCheckAuth()
	{
		LoadAuthFromDisk();

		if (m_authToken != null && m_authExpiresAt > DateTimeOffset.UtcNow.ToUnixTimeSeconds())
		{
			// validate session is still good with master
			Task.Run(async () =>
			{
				try
				{
					var req = new HttpRequestMessage(HttpMethod.Get, MASTER_SERVER_URL + "/auth/me");
					req.Headers.Add("Authorization", "Bearer " + m_authToken);
					var resp = await s_httpClient.SendAsync(req);
					if (resp.IsSuccessStatusCode)
					{
						var body = JObject.Parse(await resp.Content.ReadAsStringAsync());
						m_authDisplayName = (string?)body["displayName"] ?? m_authDisplayName;
						m_authUid = (string?)body["uid"]?.ToString() ?? m_authUid;
						Send(new JObject { ["type"] = "authStatus", ["loggedIn"] = true, ["displayName"] = m_authDisplayName, ["pid"] = m_authPid, ["uid"] = m_authUid });
						await AutoRegisterIdentityAsync();
						return;
					}
				}
				catch { }

				// session expired or invalid
				ClearAuth();
				Send(new JObject { ["type"] = "authStatus", ["loggedIn"] = false });
			});
		}
		else
		{
			ClearAuth();
			Send(new JObject { ["type"] = "authStatus", ["loggedIn"] = false });
		}
	}

	private CancellationTokenSource? m_oauthPollCts;

	private void OnEaLogin()
	{
		// generate pkce challenge
		var verifierBytes = new byte[32];
		RandomNumberGenerator.Fill(verifierBytes);
		m_codeVerifier = Base64UrlEncode(verifierBytes);

		byte[] challengeHash = SHA256.HashData(Encoding.ASCII.GetBytes(m_codeVerifier));
		string codeChallenge = Base64UrlEncode(challengeHash);

		string pcSign = GeneratePcSign();

		string authUrl = $"{EA_AUTH_URL}?client_id={EA_CLIENT_ID}"
			+ $"&response_type=code"
			+ $"&redirect_uri={Uri.EscapeDataString(EA_REDIRECT_URI)}"
			+ $"&code_challenge={codeChallenge}"
			+ $"&code_challenge_method=S256"
			+ $"&pc_sign={Uri.EscapeDataString(pcSign)}";

		// register qrc:// protocol handler so the browser can redirect back to us
		string callbackFile = Path.Combine(GetAppdataDir(), "ea_oauth_callback.tmp");
		try { File.Delete(callbackFile); } catch { }
		RegisterQrcProtocol(callbackFile);

		// open system browser for EA login
		try
		{
			Process.Start(new ProcessStartInfo(authUrl) { UseShellExecute = true });
		}
		catch (Exception ex)
		{
			Send(new JObject { ["type"] = "authLoginResult", ["ok"] = false, ["error"] = "Failed to open browser: " + ex.Message });
			return;
		}

		// poll for the callback file
		m_oauthPollCts?.Cancel();
		m_oauthPollCts = new CancellationTokenSource();
		var ct = m_oauthPollCts.Token;

		Task.Run(async () =>
		{
			try
			{
				// poll for up to 5 minutes
				for (int i = 0; i < 600 && !ct.IsCancellationRequested; i++)
				{
					await Task.Delay(500, ct);

					if (!File.Exists(callbackFile))
						continue;

					string callbackUrl = File.ReadAllText(callbackFile).Trim().Trim('"');
					try { File.Delete(callbackFile); } catch { }

					// extract query string directly, Uri doesn't like qrc:// scheme
					int qIdx = callbackUrl.IndexOf('?');
					var query = qIdx >= 0 ? HttpUtility.ParseQueryString(callbackUrl.Substring(qIdx)) : HttpUtility.ParseQueryString("");
					string? code = query["code"];
					string? error = query["error"];

					if (!string.IsNullOrEmpty(error) || string.IsNullOrEmpty(code))
					{
						Send(new JObject { ["type"] = "authLoginResult", ["ok"] = false, ["error"] = error ?? "No authorization code received" });
						return;
					}

					await ExchangeCodeForToken(code!, EA_REDIRECT_URI);
					return;
				}

				if (!ct.IsCancellationRequested)
					Send(new JObject { ["type"] = "authLoginResult", ["ok"] = false, ["error"] = "Login timed out" });
			}
			catch (OperationCanceledException) { }
			catch (Exception ex)
			{
				Send(new JObject { ["type"] = "authLoginResult", ["ok"] = false, ["error"] = ex.Message });
			}
		}, ct);
	}

	private void RegisterQrcProtocol(string callbackFile)
	{
#if WINDOWS
		try
		{
			// write a small batch script that dumps the url arg to a file
			string handlerScript = Path.Combine(GetAppdataDir(), "qrc_handler.cmd");
			File.WriteAllText(handlerScript,
				$"@echo off\r\necho %1 > \"{callbackFile}\"\r\n");

			using var key = Registry.CurrentUser.CreateSubKey(@"Software\Classes\qrc");
			key.SetValue("", "URL:Cypress EA Login");
			key.SetValue("URL Protocol", "");

			using var cmdKey = key.CreateSubKey(@"shell\open\command");
			cmdKey.SetValue("", $"\"{handlerScript}\" \"%1\"");
		}
		catch { }
#endif
	}

	private async Task ExchangeCodeForToken(string code, string redirectUri)
	{
		try
		{
			// exchange auth code for EA JWT
			var tokenBody = new FormUrlEncodedContent(new[]
			{
				new KeyValuePair<string, string>("grant_type", "authorization_code"),
				new KeyValuePair<string, string>("code", code),
				new KeyValuePair<string, string>("client_id", EA_CLIENT_ID),
				new KeyValuePair<string, string>("client_secret", EA_CLIENT_SECRET),
				new KeyValuePair<string, string>("code_verifier", m_codeVerifier!),
				new KeyValuePair<string, string>("redirect_uri", redirectUri),
				new KeyValuePair<string, string>("token_format", "JWS"),
			});

			var tokenResp = await s_httpClient.PostAsync(EA_TOKEN_URL, tokenBody);
			string tokenRaw = await tokenResp.Content.ReadAsStringAsync();
			JObject? tokenJson = null;
			try { tokenJson = JObject.Parse(tokenRaw); } catch { }

			if (!tokenResp.IsSuccessStatusCode)
			{
				string err = (string?)tokenJson?["error_description"] ?? (tokenRaw.Length > 200 ? tokenRaw.Substring(0, 200) : tokenRaw);
				Send(new JObject { ["type"] = "authLoginResult", ["ok"] = false, ["error"] = $"EA token exchange failed: {err}" });
				return;
			}

			string? accessToken = (string?)tokenJson?["access_token"];
			if (string.IsNullOrEmpty(accessToken))
			{
				Send(new JObject { ["type"] = "authLoginResult", ["ok"] = false, ["error"] = "No access token in EA response" });
				return;
			}

			// send EA JWT to cypress master for cypress session
			var body = new JObject { ["token"] = accessToken };
			var content = new StringContent(body.ToString(), Encoding.UTF8, "application/json");
			var resp = await s_httpClient.PostAsync(MASTER_SERVER_URL + "/auth/login", content);
			string respRaw = await resp.Content.ReadAsStringAsync();
			JObject? respBody = null;
			try { respBody = JObject.Parse(respRaw); } catch { }

			if (!resp.IsSuccessStatusCode)
			{
				string err = (string?)respBody?["error"] ?? (respRaw.Length > 200 ? respRaw.Substring(0, 200) : respRaw);
				Send(new JObject { ["type"] = "authLoginResult", ["ok"] = false, ["error"] = $"Login failed: {err}" });
				return;
			}

			m_authToken = (string?)respBody?["token"];
			m_authPid = (string?)respBody?["pid"];
			m_authUid = (string?)respBody?["uid"]?.ToString();
			m_authDisplayName = (string?)respBody?["displayName"];
			m_authExpiresAt = (double)(respBody?["expiresAt"] ?? 0);
			SaveAuthToDisk();

			Send(new JObject { ["type"] = "authLoginResult", ["ok"] = true, ["displayName"] = m_authDisplayName, ["pid"] = m_authPid, ["uid"] = m_authUid });

			// auto-register identity if not already done
			await AutoRegisterIdentityAsync();
		}
		catch (Exception ex)
		{
			Send(new JObject { ["type"] = "authLoginResult", ["ok"] = false, ["error"] = ex.Message });
		}
	}

	private void OnEaLogout()
	{
		if (m_authToken != null)
		{
			string token = m_authToken;
			Task.Run(async () =>
			{
				try
				{
					var req = new HttpRequestMessage(HttpMethod.Post, MASTER_SERVER_URL + "/auth/logout");
					req.Headers.Add("Authorization", "Bearer " + token);
					await s_httpClient.SendAsync(req);
				}
				catch { }
			});
		}

		ClearAuth();
		Send(new JObject { ["type"] = "authLogoutResult", ["ok"] = true });
	}

	private void OnGetAuthTicket()
	{
		if (m_authToken == null)
		{
			Send(new JObject { ["type"] = "authTicket", ["ok"] = false, ["error"] = "Not logged in" });
			return;
		}

		Task.Run(async () =>
		{
			try
			{
				var req = new HttpRequestMessage(HttpMethod.Post, MASTER_SERVER_URL + "/auth/ticket");
				req.Headers.Add("Authorization", "Bearer " + m_authToken);
				var resp = await s_httpClient.SendAsync(req);
				var body = JObject.Parse(await resp.Content.ReadAsStringAsync());

				if (resp.IsSuccessStatusCode)
				{
					Send(new JObject { ["type"] = "authTicket", ["ok"] = true, ["ticket"] = (string?)body["ticket"] });
				}
				else
				{
					Send(new JObject { ["type"] = "authTicket", ["ok"] = false, ["error"] = (string?)body["error"] ?? "Failed to get ticket" });
				}
			}
			catch (Exception ex)
			{
				Send(new JObject { ["type"] = "authTicket", ["ok"] = false, ["error"] = ex.Message });
			}
		});
	}

	private void ClearAuth()
	{
		m_authToken = null;
		m_authPid = null;
		m_authDisplayName = null;
		m_authExpiresAt = 0;
		SaveAuthToDisk();
	}

	private void LoadAuthFromDisk()
	{
		try
		{
			string filePath = Path.Combine(GetAppdataDir(), s_launcherSavedataFilename);
			if (!File.Exists(filePath)) return;
			var root = JObject.Parse(File.ReadAllText(filePath));
			var auth = root["Auth"] as JObject;
			if (auth == null) return;
			m_authToken = (string?)auth["Token"];
			m_authPid = (string?)auth["PID"];
			m_authUid = (string?)auth["UID"];
			m_authDisplayName = (string?)auth["DisplayName"];
			m_authExpiresAt = (double)(auth["ExpiresAt"] ?? 0);
		}
		catch { }
	}

	private void SaveAuthToDisk()
	{
		try
		{
			string filePath = Path.Combine(GetAppdataDir(), s_launcherSavedataFilename);
			JObject root = new JObject();
			if (File.Exists(filePath))
				root = JObject.Parse(File.ReadAllText(filePath));

			if (m_authToken != null)
			{
				root["Auth"] = new JObject
				{
					["Token"] = m_authToken,
					["PID"] = m_authPid,
					["UID"] = m_authUid,
					["DisplayName"] = m_authDisplayName,
					["ExpiresAt"] = m_authExpiresAt
				};
			}
			else
			{
				root.Remove("Auth");
			}

			File.WriteAllText(filePath, root.ToString());
		}
		catch { }
	}

	private static readonly byte[] s_pcSignKeyV1 = Encoding.ASCII.GetBytes("ISa3dpGOc8wW7Adn4auACSQmaccrOyR2");
	private static readonly byte[] s_pcSignKeyV2 = Encoding.ASCII.GetBytes("nt5FfJbdPzNcl2pkC3zgjO43Knvscxft");

	private static string GeneratePcSign()
	{
		// pick a random signing key version
		bool useV2 = RandomNumberGenerator.GetInt32(2) == 1;
		byte[] signKey = useV2 ? s_pcSignKeyV2 : s_pcSignKeyV1;
		string sv = useV2 ? "v2" : "v1";

		// gather basic machine identifiers
		string machineName = Environment.MachineName;
		string ts = DateTime.UtcNow.ToString("yyyy-MM-dd HH:mm:ss:fff");

		// fnv1a of machine name as mid
		ulong mid = Fnv1a(Encoding.UTF8.GetBytes(machineName));

		var payload = new JObject
		{
			["av"] = "v1",
			["bsn"] = machineName,
			["gid"] = 0,
			["hsn"] = "None",
			["mid"] = mid.ToString(),
			["msn"] = machineName,
			["sv"] = sv,
			["ts"] = ts
		};

		string payloadJson = payload.ToString(Newtonsoft.Json.Formatting.None);
		string payloadB64 = Base64UrlEncode(Encoding.UTF8.GetBytes(payloadJson));

		using var hmac = new HMACSHA256(signKey);
		byte[] sig = hmac.ComputeHash(Encoding.UTF8.GetBytes(payloadB64));
		string sigB64 = Base64UrlEncode(sig);

		return payloadB64 + "." + sigB64;
	}

	private static ulong Fnv1a(byte[] data)
	{
		ulong hash = 14695981039346656037;
		foreach (byte b in data)
		{
			hash ^= b;
			hash *= 1099511628211;
		}
		return hash;
	}

	private static string Base64UrlEncode(byte[] data)
	{
		return Convert.ToBase64String(data)
			.TrimEnd('=')
			.Replace('+', '-')
			.Replace('/', '_');
	}
}
