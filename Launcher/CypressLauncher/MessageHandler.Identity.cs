#nullable enable
using System;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;
using NSec.Cryptography;

#if WINDOWS
using Microsoft.Win32;
#endif

namespace CypressLauncher;

public partial class MessageHandler
{
	// identity state
	private string? m_identityJwt;
	private string? m_identityAccountId;
	private string? m_identityUsername;
	private string? m_identityNickname;
	private double m_identityExpiresAt;
	private Key? m_identityKey;

	private static readonly string s_identityKeyFilename = "identity_key.bin";
	private static readonly string s_identitySavedataKey = "Identity";

	private string GetIdentityKeyPath() => Path.Combine(GetAppdataDir(), s_identityKeyFilename);

	// load or generate ed25519 keypair
	private Key LoadOrCreateIdentityKey()
	{
		string path = GetIdentityKeyPath();
		if (File.Exists(path))
		{
			try
			{
				byte[] raw = File.ReadAllBytes(path);
				if (raw.Length == Ed25519.Ed25519.PrivateKeySize)
					return Key.Import(SignatureAlgorithm.Ed25519, raw, KeyBlobFormat.RawPrivateKey, new KeyCreationParameters { ExportPolicy = KeyExportPolicies.AllowPlaintextExport });
			}
			catch { }
		}

		var key = Key.Create(SignatureAlgorithm.Ed25519, new KeyCreationParameters { ExportPolicy = KeyExportPolicies.AllowPlaintextExport });
		byte[] exported = key.Export(KeyBlobFormat.RawPrivateKey);
		File.WriteAllBytes(path, exported);
		return key;
	}

	private string GetPublicKeyHex()
	{
		if (m_identityKey == null)
			m_identityKey = LoadOrCreateIdentityKey();

		byte[] pub = m_identityKey.PublicKey.Export(KeyBlobFormat.RawPublicKey);
		return Convert.ToHexString(pub).ToLowerInvariant();
	}

	private string SignChallenge(byte[] challenge)
	{
		if (m_identityKey == null)
			m_identityKey = LoadOrCreateIdentityKey();

		byte[] sig = SignatureAlgorithm.Ed25519.Sign(m_identityKey, challenge);
		return Convert.ToHexString(sig).ToLowerInvariant();
	}

	private static string GetMachineHWID()
	{
#if WINDOWS
		try
		{
			using var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Cryptography");
			string? guid = key?.GetValue("MachineGuid") as string;
			if (!string.IsNullOrEmpty(guid))
			{
				byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes("cypress_hwid:" + guid));
				return Convert.ToHexString(hash).ToLowerInvariant();
			}
		}
		catch { }
#endif
		// fallback
		byte[] fallback = SHA256.HashData(Encoding.UTF8.GetBytes("cypress_hwid:" + Environment.MachineName));
		return Convert.ToHexString(fallback).ToLowerInvariant();
	}

	private void OnCheckIdentity()
	{
		LoadIdentityFromDisk();

		if (m_identityJwt != null && m_identityExpiresAt > DateTimeOffset.UtcNow.ToUnixTimeSeconds())
		{
			Send(new JObject
			{
				["type"] = "identityStatus",
				["registered"] = true,
				["username"] = m_identityUsername,
				["nickname"] = m_identityNickname,
				["accountId"] = m_identityAccountId
			});

			// auto-refresh if <2 days to expiry
			double secsLeft = m_identityExpiresAt - DateTimeOffset.UtcNow.ToUnixTimeSeconds();
			if (secsLeft < 2 * 24 * 3600)
				Task.Run(RefreshIdentityAsync);
		}
		else if (m_identityJwt != null)
		{
			// expired, try refresh
			Task.Run(async () =>
			{
				bool ok = await RefreshIdentityAsync();
				if (ok)
				{
					Send(new JObject
					{
						["type"] = "identityStatus",
						["registered"] = true,
						["username"] = m_identityUsername,
						["nickname"] = m_identityNickname,
						["accountId"] = m_identityAccountId
					});
				}
				else
				{
					Send(new JObject { ["type"] = "identityStatus", ["registered"] = false });
				}
			});
		}
		else
		{
			Send(new JObject { ["type"] = "identityStatus", ["registered"] = false });
		}
	}

	private void OnRegisterIdentity(JObject msg)
	{
		string username = ((string?)msg["username"] ?? "").Trim();
		if (string.IsNullOrEmpty(username) || username.Length < 3 || username.Length > 16)
		{
			Send(new JObject { ["type"] = "registerResult", ["ok"] = false, ["error"] = "Username must be 3-16 characters" });
			return;
		}

		Task.Run(async () =>
		{
			try
			{
				if (string.IsNullOrEmpty(m_authToken))
				{
					Send(new JObject { ["type"] = "registerResult", ["ok"] = false, ["error"] = "EA login required" });
					return;
				}

				m_identityKey = LoadOrCreateIdentityKey();
				string pubKeyHex = GetPublicKeyHex();
				string hwid = GetMachineHWID();

				var body = new JObject
				{
					["username"] = username,
					["public_key"] = pubKeyHex,
					["hwid"] = hwid
				};

				var request = new HttpRequestMessage(HttpMethod.Post, MASTER_SERVER_URL + "/auth/register")
				{
					Content = new StringContent(body.ToString(), Encoding.UTF8, "application/json")
				};
				request.Headers.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", m_authToken);

				var resp = await s_httpClient.SendAsync(request);
				var respBody = JObject.Parse(await resp.Content.ReadAsStringAsync());

				if (!resp.IsSuccessStatusCode)
				{
					Send(new JObject { ["type"] = "registerResult", ["ok"] = false, ["error"] = (string?)respBody["error"] ?? "Registration failed" });
					return;
				}

				m_identityJwt = (string?)respBody["jwt"];
				m_identityAccountId = (string?)respBody["account_id"];
				m_identityUsername = (string?)respBody["username"];

				m_identityExpiresAt = ParseJwtExpiry(m_identityJwt);
				SaveIdentityToDisk();

				var backupCodes = respBody["backup_codes"] as JArray;

				Send(new JObject
				{
					["type"] = "registerResult",
					["ok"] = true,
					["username"] = m_identityUsername,
					["accountId"] = m_identityAccountId,
					["backupCodes"] = backupCodes
				});
			}
			catch (Exception ex)
			{
				Send(new JObject { ["type"] = "registerResult", ["ok"] = false, ["error"] = ex.Message });
			}
		});
	}

	private void OnRebindIdentity(JObject msg)
	{
		string username = ((string?)msg["username"] ?? "").Trim();
		string backupCode = ((string?)msg["backupCode"] ?? "").Trim();

		if (string.IsNullOrEmpty(username) || string.IsNullOrEmpty(backupCode))
		{
			Send(new JObject { ["type"] = "rebindResult", ["ok"] = false, ["error"] = "Username and backup code required" });
			return;
		}

		Task.Run(async () =>
		{
			try
			{
				m_identityKey = LoadOrCreateIdentityKey();
				string pubKeyHex = GetPublicKeyHex();
				string hwid = GetMachineHWID();

				var body = new JObject
				{
					["username"] = username,
					["backup_code"] = backupCode,
					["new_public_key"] = pubKeyHex,
					["new_hwid"] = hwid
				};

				var resp = await s_httpClient.PostAsync(
					MASTER_SERVER_URL + "/auth/rebind",
					new StringContent(body.ToString(), Encoding.UTF8, "application/json"));

				var respBody = JObject.Parse(await resp.Content.ReadAsStringAsync());

				if (!resp.IsSuccessStatusCode)
				{
					Send(new JObject { ["type"] = "rebindResult", ["ok"] = false, ["error"] = (string?)respBody["error"] ?? "Rebind failed" });
					return;
				}

				m_identityJwt = (string?)respBody["jwt"];
				m_identityExpiresAt = ParseJwtExpiry(m_identityJwt);

				// parse account info from jwt
				ParseJwtIdentity(m_identityJwt);
				SaveIdentityToDisk();

				Send(new JObject
				{
					["type"] = "rebindResult",
					["ok"] = true,
					["username"] = m_identityUsername,
					["accountId"] = m_identityAccountId
				});
			}
			catch (Exception ex)
			{
				Send(new JObject { ["type"] = "rebindResult", ["ok"] = false, ["error"] = ex.Message });
			}
		});
	}

	private async Task<bool> RefreshIdentityAsync()
	{
		if (m_identityJwt == null) return false;

		try
		{
			m_identityKey ??= LoadOrCreateIdentityKey();

			// challenge = sha256(jwt)
			byte[] challenge = SHA256.HashData(Encoding.UTF8.GetBytes(m_identityJwt));
			string challengeSig = SignChallenge(challenge);

			var body = new JObject
			{
				["jwt"] = m_identityJwt,
				["challenge_sig"] = challengeSig
			};

			var resp = await s_httpClient.PostAsync(
				MASTER_SERVER_URL + "/auth/refresh-identity",
				new StringContent(body.ToString(), Encoding.UTF8, "application/json"));

			if (!resp.IsSuccessStatusCode) return false;

			var respBody = JObject.Parse(await resp.Content.ReadAsStringAsync());
			m_identityJwt = (string?)respBody["jwt"];
			m_identityExpiresAt = ParseJwtExpiry(m_identityJwt);
			ParseJwtIdentity(m_identityJwt);
			SaveIdentityToDisk();
			return true;
		}
		catch
		{
			return false;
		}
	}

	// get the jwt for passing to game servers via env var
	public string? GetIdentityJwt() => m_identityJwt;

	// check identity after ea login, prompt registration if needed
	private async Task AutoRegisterIdentityAsync()
	{
		try
		{
			LoadIdentityFromDisk();

			// if already registered and not expired, just refresh if needed
			if (m_identityJwt != null && m_identityExpiresAt > DateTimeOffset.UtcNow.ToUnixTimeSeconds())
			{
				double secsLeft = m_identityExpiresAt - DateTimeOffset.UtcNow.ToUnixTimeSeconds();
				if (secsLeft < 2 * 24 * 3600)
					await RefreshIdentityAsync();

				Send(new JObject
				{
					["type"] = "identityStatus",
					["registered"] = true,
					["username"] = m_identityUsername,
					["nickname"] = m_identityNickname,
					["accountId"] = m_identityAccountId
				});
				return;
			}

			// try refresh if expired but within grace
			if (m_identityJwt != null)
			{
				if (await RefreshIdentityAsync())
				{
					Send(new JObject
					{
						["type"] = "identityStatus",
						["registered"] = true,
						["username"] = m_identityUsername,
						["nickname"] = m_identityNickname,
						["accountId"] = m_identityAccountId
					});
					return;
				}
			}

			// no local identity, check if this ea account already has one on the server
			if (!string.IsNullOrEmpty(m_authToken))
			{
				m_identityKey = LoadOrCreateIdentityKey();
				string pubKeyHex = GetPublicKeyHex();
				string hwid = GetMachineHWID();

				var checkBody = new JObject { ["public_key"] = pubKeyHex, ["hwid"] = hwid };
				var checkReq = new HttpRequestMessage(HttpMethod.Post, MASTER_SERVER_URL + "/auth/check-identity")
				{
					Content = new StringContent(checkBody.ToString(), Encoding.UTF8, "application/json")
				};
				checkReq.Headers.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", m_authToken);

				var resp = await s_httpClient.SendAsync(checkReq);
				if (resp.IsSuccessStatusCode)
				{
					var respBody = JObject.Parse(await resp.Content.ReadAsStringAsync());
					if ((bool)(respBody["registered"] ?? false))
					{
						// ea account already has an identity, update local key + get jwt
						m_identityKey = LoadOrCreateIdentityKey();
						m_identityJwt = (string?)respBody["jwt"];
						m_identityAccountId = (string?)respBody["account_id"];
						m_identityUsername = (string?)respBody["username"];
						m_identityExpiresAt = ParseJwtExpiry(m_identityJwt);
						ParseJwtIdentity(m_identityJwt);
						SaveIdentityToDisk();

						Send(new JObject
						{
							["type"] = "identityStatus",
							["registered"] = true,
							["username"] = m_identityUsername,
							["nickname"] = m_identityNickname,
							["accountId"] = m_identityAccountId
						});
						return;
					}
				}
			}

			// no identity exists, tell frontend to show registration
			Send(new JObject { ["type"] = "identityStatus", ["registered"] = false });
		}
		catch
		{
			Send(new JObject { ["type"] = "identityStatus", ["registered"] = false });
		}
	}

	private string? GetIdentityPrivateKeyHex()
	{
		if (m_identityKey == null) return null;
		byte[] raw = m_identityKey.Export(KeyBlobFormat.RawPrivateKey);
		return Convert.ToHexString(raw).ToLowerInvariant();
	}

	private void LoadIdentityFromDisk()
	{
		try
		{
			string filePath = Path.Combine(GetAppdataDir(), s_launcherSavedataFilename);
			if (!File.Exists(filePath)) return;
			var root = JObject.Parse(File.ReadAllText(filePath));
			var id = root[s_identitySavedataKey] as JObject;
			if (id == null) return;
			m_identityJwt = (string?)id["JWT"];
			m_identityAccountId = (string?)id["AccountId"];
			m_identityUsername = (string?)id["Username"];
			m_identityExpiresAt = (double)(id["ExpiresAt"] ?? 0);
			ParseJwtIdentity(m_identityJwt);
		}
		catch { }
	}

	private void SaveIdentityToDisk()
	{
		try
		{
			string filePath = Path.Combine(GetAppdataDir(), s_launcherSavedataFilename);
			JObject root = new JObject();
			if (File.Exists(filePath))
				root = JObject.Parse(File.ReadAllText(filePath));

			if (m_identityJwt != null)
			{
				root[s_identitySavedataKey] = new JObject
				{
					["JWT"] = m_identityJwt,
					["AccountId"] = m_identityAccountId,
					["Username"] = m_identityUsername,
					["ExpiresAt"] = m_identityExpiresAt
				};
			}
			else
			{
				root.Remove(s_identitySavedataKey);
			}

			File.WriteAllText(filePath, root.ToString());
		}
		catch { }
	}

	private void OnClearIdentity()
	{
		m_identityJwt = null;
		m_identityAccountId = null;
		m_identityUsername = null;
		m_identityNickname = null;
		m_identityExpiresAt = 0;
		SaveIdentityToDisk();

		// silently re-register (register endpoint handles existing accounts, updates pubkey)
		Task.Run(async () =>
		{
			try
			{
				if (string.IsNullOrEmpty(m_authToken))
				{
					Send(new JObject { ["type"] = "identityStatus", ["registered"] = false });
					return;
				}

				m_identityKey = LoadOrCreateIdentityKey();
				string pubKeyHex = GetPublicKeyHex();
				string hwid = GetMachineHWID();

				// use a dummy username, register endpoint ignores it for existing accounts
				var body = new JObject
				{
					["username"] = "relogin",
					["public_key"] = pubKeyHex,
					["hwid"] = hwid
				};

				var request = new HttpRequestMessage(HttpMethod.Post, MASTER_SERVER_URL + "/auth/register")
				{
					Content = new StringContent(body.ToString(), Encoding.UTF8, "application/json")
				};
				request.Headers.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", m_authToken);

				var resp = await s_httpClient.SendAsync(request);
				var respBody = JObject.Parse(await resp.Content.ReadAsStringAsync());

				if (resp.IsSuccessStatusCode && (bool)(respBody["ok"] ?? false))
				{
					m_identityJwt = (string?)respBody["jwt"];
					m_identityAccountId = (string?)respBody["account_id"];
					m_identityUsername = (string?)respBody["username"];
					m_identityExpiresAt = ParseJwtExpiry(m_identityJwt);
					ParseJwtIdentity(m_identityJwt);
					SaveIdentityToDisk();

					Send(new JObject
					{
						["type"] = "identityStatus",
						["registered"] = true,
						["username"] = m_identityUsername,
						["nickname"] = m_identityNickname,
						["accountId"] = m_identityAccountId
					});
				}
				else
				{
					Send(new JObject { ["type"] = "identityStatus", ["registered"] = false });
				}
			}
			catch
			{
				Send(new JObject { ["type"] = "identityStatus", ["registered"] = false });
			}
		});
	}

	// parse jwt payload without verifying (launcher trusts master's response)
	private static double ParseJwtExpiry(string? jwt)
	{
		if (string.IsNullOrEmpty(jwt)) return 0;
		var parts = jwt.Split('.');
		if (parts.Length < 2) return 0;
		try
		{
			string padded = parts[1].Replace('-', '+').Replace('_', '/');
			switch (padded.Length % 4) { case 2: padded += "=="; break; case 3: padded += "="; break; }
			var json = JObject.Parse(Encoding.UTF8.GetString(Convert.FromBase64String(padded)));
			return (double)(json["exp"] ?? 0);
		}
		catch { return 0; }
	}

	private void ParseJwtIdentity(string? jwt)
	{
		if (string.IsNullOrEmpty(jwt)) return;
		var parts = jwt.Split('.');
		if (parts.Length < 2) return;
		try
		{
			string padded = parts[1].Replace('-', '+').Replace('_', '/');
			switch (padded.Length % 4) { case 2: padded += "=="; break; case 3: padded += "="; break; }
			var json = JObject.Parse(Encoding.UTF8.GetString(Convert.FromBase64String(padded)));
			m_identityAccountId = (string?)json["sub"];
			m_identityUsername = (string?)json["username"];
			m_identityNickname = (string?)json["nickname"];
		}
		catch { }
	}

	private void OnSetNickname(JObject msg)
	{
		string nickname = ((string?)msg["nickname"] ?? "").Trim();

		if (m_identityJwt == null)
		{
			Send(new JObject { ["type"] = "nicknameResult", ["ok"] = false, ["error"] = "Not logged in" });
			return;
		}

		Task.Run(async () =>
		{
			try
			{
				m_identityKey ??= LoadOrCreateIdentityKey();
				byte[] challenge = SHA256.HashData(Encoding.UTF8.GetBytes(m_identityJwt));
				string challengeSig = SignChallenge(challenge);

				var body = new JObject
				{
					["jwt"] = m_identityJwt,
					["challenge_sig"] = challengeSig,
					["nickname"] = nickname
				};

				var resp = await s_httpClient.PostAsync(
					MASTER_SERVER_URL + "/auth/set-nickname",
					new StringContent(body.ToString(), Encoding.UTF8, "application/json"));

				var respBody = JObject.Parse(await resp.Content.ReadAsStringAsync());
				if ((bool)(respBody["ok"] ?? false))
				{
					m_identityJwt = (string?)respBody["jwt"] ?? m_identityJwt;
					m_identityExpiresAt = ParseJwtExpiry(m_identityJwt);
					ParseJwtIdentity(m_identityJwt);
					SaveIdentityToDisk();
					Send(new JObject { ["type"] = "nicknameResult", ["ok"] = true, ["nickname"] = nickname });
				}
				else
				{
					string error = (string?)respBody["error"] ?? "Unknown error";
					Send(new JObject { ["type"] = "nicknameResult", ["ok"] = false, ["error"] = error });
				}
			}
			catch (Exception ex)
			{
				Send(new JObject { ["type"] = "nicknameResult", ["ok"] = false, ["error"] = ex.Message });
			}
		});
	}
}
