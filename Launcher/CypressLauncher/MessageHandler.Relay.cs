#nullable enable
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

public partial class MessageHandler
{
	private static bool TryParseProxyEndpoint(string rawValue, out ProxyEndpoint? endpoint)
	{
		endpoint = null;
		if (string.IsNullOrWhiteSpace(rawValue))
			return false;

		string candidate = rawValue.Trim();
		if (!candidate.Contains("://", StringComparison.Ordinal))
			candidate = "udp://" + candidate;

		if (!Uri.TryCreate(candidate, UriKind.Absolute, out Uri? uri) || string.IsNullOrWhiteSpace(uri.Host))
			return false;

		int port = uri.IsDefaultPort || uri.Port <= 0 ? s_defaultProxyPort : uri.Port;
		endpoint = new ProxyEndpoint(uri.Host, port);
		return true;
	}

	private void ClearProxyEnvironment()
	{
		Environment.SetEnvironmentVariable(s_proxyAddressEnv, null);
		Environment.SetEnvironmentVariable(s_proxyPortEnv, null);
		Environment.SetEnvironmentVariable(s_proxyKeyEnv, null);
		Environment.SetEnvironmentVariable("CYPRESS_AUTH_TICKET", null);
	}

	private static bool IsPrivateIPv4(IPAddress address)
	{
		byte[] bytes = address.GetAddressBytes();
		return bytes[0] == 10
			|| (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31)
			|| (bytes[0] == 192 && bytes[1] == 168);
	}

	private string? FetchAuthTicketSync()
	{
		if (m_authToken == null) { SendStatus("No auth token for ticket request", "warning"); return null; }
		try
		{
			var req = new HttpRequestMessage(HttpMethod.Post, MASTER_SERVER_URL + "/auth/ticket");
			req.Headers.Add("Authorization", "Bearer " + m_authToken);
			var resp = s_httpClient.SendAsync(req).GetAwaiter().GetResult();
			var body = JObject.Parse(resp.Content.ReadAsStringAsync().GetAwaiter().GetResult());
			if (resp.IsSuccessStatusCode)
				return (string?)body["ticket"];
			SendStatus($"Ticket request failed: {resp.StatusCode} - {body}", "warning");
		}
		catch (Exception ex) { SendStatus($"Ticket request exception: {ex.Message}", "warning"); }
		return null;
	}

	private static string TryGetPreferredDeviceIp()
	{
		try
		{
			List<IPAddress> privateCandidates = new();
			List<IPAddress> fallbackCandidates = new();

			foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
			{
				if (nic.OperationalStatus != OperationalStatus.Up)
					continue;
				if (nic.NetworkInterfaceType is NetworkInterfaceType.Loopback or NetworkInterfaceType.Tunnel)
					continue;

				foreach (UnicastIPAddressInformation addr in nic.GetIPProperties().UnicastAddresses)
				{
					if (addr.Address.AddressFamily != AddressFamily.InterNetwork || IPAddress.IsLoopback(addr.Address))
						continue;

					if (IsPrivateIPv4(addr.Address))
						privateCandidates.Add(addr.Address);
					else
						fallbackCandidates.Add(addr.Address);
				}
			}

			return privateCandidates.FirstOrDefault()?.ToString()
				?? fallbackCandidates.FirstOrDefault()?.ToString()
				?? string.Empty;
		}
		catch
		{
			return string.Empty;
		}
	}

	private static bool TryBuildRelayApiUri(string rawValue, out Uri? apiUri)
	{
		apiUri = null;
		if (string.IsNullOrWhiteSpace(rawValue))
			return false;

		string candidate = rawValue.Trim();
		if (candidate.StartsWith("http://", StringComparison.OrdinalIgnoreCase)
			|| candidate.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
		{
			if (!Uri.TryCreate(candidate, UriKind.Absolute, out Uri? directUri) || string.IsNullOrWhiteSpace(directUri.Host))
				return false;

			var builder = new UriBuilder(directUri)
			{
				Port = directUri.IsDefaultPort ? s_defaultProxyApiPort : directUri.Port,
				Path = string.IsNullOrWhiteSpace(directUri.AbsolutePath) || directUri.AbsolutePath == "/"
					? "/api/relays"
					: directUri.AbsolutePath.TrimEnd('/')
			};
			apiUri = builder.Uri;
			return true;
		}

		if (!TryParseProxyEndpoint(rawValue, out ProxyEndpoint? endpoint) || endpoint is null)
			return false;

		apiUri = new UriBuilder(Uri.UriSchemeHttp, endpoint.Host, s_defaultProxyApiPort, "/api/relays").Uri;
		return true;
	}

	private static string BuildRelayJoinLink(string displayHost, string relayAddress, string relayKey)
	{
		string host = string.IsNullOrWhiteSpace(displayHost) ? relayAddress : displayHost;
		string encodedRelay = Uri.EscapeDataString(relayAddress);
		string encodedKey = Uri.EscapeDataString(relayKey);
		return $"cypress://{host}?relay={encodedRelay}&key={encodedKey}";
	}

	private bool ConfigureProxyEnvironment(bool useRelay, string relayAddress, string relayKey, out string relayHost)
	{
		relayHost = string.Empty;
		if (!useRelay)
		{
			ClearProxyEnvironment();
			return true;
		}

		if (!TryParseProxyEndpoint(relayAddress, out ProxyEndpoint? endpoint) || endpoint is null)
		{
			ClearProxyEnvironment();
			SendStatus("Relay address must be a valid host or host:port.", "error");
			return false;
		}

		if (string.IsNullOrWhiteSpace(relayKey))
		{
			ClearProxyEnvironment();
			SendStatus("Relay key is required for multi-server relay mode.", "error");
			return false;
		}

		relayHost = endpoint.Host;
		Environment.SetEnvironmentVariable(s_proxyAddressEnv, endpoint.Host);
		Environment.SetEnvironmentVariable(s_proxyPortEnv, endpoint.Port.ToString());
		Environment.SetEnvironmentVariable(s_proxyKeyEnv, relayKey.Trim());

		// fetch auth ticket for relay validation
		string? ticket = FetchAuthTicketSync();
		Environment.SetEnvironmentVariable("CYPRESS_AUTH_TICKET", ticket);

		return true;
	}

	private void OnGetRelayLease(JObject msg)
	{
		string relayAddress = ((string?)msg["relayAddress"]) ?? string.Empty;
		string relayServerName = ((string?)msg["relayServerName"]) ?? string.Empty;
		string fallbackName = string.IsNullOrWhiteSpace(relayServerName)
			? s_gameToGameName[m_selectedGame] + " Server"
			: relayServerName.Trim();

		if (!TryBuildRelayApiUri(relayAddress, out Uri? apiUri) || apiUri is null)
		{
			SendStatus("Relay address must be set before requesting a relay lease.", "error");
			return;
		}

		var requestBody = new JObject
		{
			["serverName"] = fallbackName,
			["game"] = m_selectedGame.ToString()
		};

		try
		{
			using var request = new HttpRequestMessage(HttpMethod.Post, apiUri)
			{
				Content = new StringContent(requestBody.ToString(Newtonsoft.Json.Formatting.None), Encoding.UTF8, "application/json")
			};
			using HttpResponseMessage response = s_httpClient.Send(request);
			string responseText = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
			if (!response.IsSuccessStatusCode)
			{
				SendStatus("Relay API request failed: " + response.StatusCode, "error");
				return;
			}

			JObject payload = JObject.Parse(responseText);
			string resolvedRelayAddress = ((string?)payload["relayAddress"]) ?? relayAddress.Trim();
			string relayKey = ((string?)payload["relayKey"]) ?? string.Empty;
			string relayCode = ((string?)payload["code"]) ?? string.Empty;
			string displayHost = ((string?)payload["displayHost"]) ?? string.Empty;
			string resolvedName = ((string?)payload["serverName"]) ?? fallbackName;
			string joinLink = ((string?)payload["joinLink"]) ?? BuildRelayJoinLink(displayHost, resolvedRelayAddress, relayKey);

			Send(new JObject
			{
				["type"] = "relayLease",
				["relayAddress"] = resolvedRelayAddress,
				["hostRelayKey"] = relayKey,
				["hostRelayCode"] = relayCode,
				["hostRelayJoinLink"] = joinLink,
				["relayServerName"] = resolvedName
			});
			SendStatus("Relay lease ready.", "success");
		}
		catch (Exception ex)
		{
			SendStatus("Failed to request relay lease: " + ex.Message, "error");
		}
	}

	private void OnResolveRelayCode(JObject msg)
	{
		string relayAddress = ((string?)msg["relayAddress"]) ?? string.Empty;
		string code = ((string?)msg["code"]) ?? string.Empty;

		if (string.IsNullOrWhiteSpace(code))
		{
			Send(new JObject { ["type"] = "relayResolved", ["error"] = "No relay code provided." });
			return;
		}

		if (!TryBuildRelayApiUri(relayAddress, out Uri? baseUri) || baseUri is null)
		{
			Send(new JObject { ["type"] = "relayResolved", ["error"] = "Invalid relay address." });
			return;
		}

		try
		{
			var resolveUri = new Uri(baseUri, code.Trim().ToUpperInvariant());
			using var request = new HttpRequestMessage(HttpMethod.Get, resolveUri);
			using HttpResponseMessage response = s_httpClient.Send(request);
			string responseText = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();

			if (!response.IsSuccessStatusCode)
			{
				Send(new JObject { ["type"] = "relayResolved", ["error"] = "Unknown relay code." });
				return;
			}

			JObject payload = JObject.Parse(responseText);
			Send(new JObject
			{
				["type"] = "relayResolved",
				["relayAddress"] = ((string?)payload["relayAddress"]) ?? relayAddress,
				["relayKey"] = ((string?)payload["relayKey"]) ?? string.Empty,
				["serverName"] = ((string?)payload["serverName"]) ?? string.Empty,
				["game"] = ((string?)payload["game"]) ?? string.Empty
			});
		}
		catch (Exception ex)
		{
			Send(new JObject { ["type"] = "relayResolved", ["error"] = "Failed to resolve: " + ex.Message });
		}
	}
}
