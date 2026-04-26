#include "pch.h"
#include <SideChannel.h>
#include <Core/Logging.h>
#include <HWID.h>
#include <fstream>
#include <random>

namespace Cypress
{
	static std::string HttpGet(const std::string& host, int port, const std::string& path, int timeoutMs = 5000)
	{
		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET) return "";

		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);

		if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
		{
			addrinfo hints = {}, *result = nullptr;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result)
			{
				closesocket(sock);
				return "";
			}
			addr.sin_addr = ((sockaddr_in*)result->ai_addr)->sin_addr;
			freeaddrinfo(result);
		}

		if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			closesocket(sock);
			return "";
		}

		std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
		send(sock, request.c_str(), (int)request.size(), 0);

		std::string response;
		char buf[4096];
		int n;
		while ((n = recv(sock, buf, sizeof(buf), 0)) > 0)
			response.append(buf, n);
		closesocket(sock);

		auto bodyStart = response.find("\r\n\r\n");
		if (bodyStart == std::string::npos) return "";
		return response.substr(bodyStart + 4);
	}

	// nonce+sig verification against master server, no raw token
	static bool ValidateModChallenge(const std::string& nonce, const std::string& sig)
	{
		char urlBuf[256] = {};
		if (GetEnvironmentVariableA("CYPRESS_MASTER_URL", urlBuf, sizeof(urlBuf)) == 0)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: CYPRESS_MASTER_URL not set, can't validate mod");
			return false;
		}

		std::string url(urlBuf);
		std::string host;
		int port = 80;

		size_t schemeEnd = url.find("://");
		std::string hostPort = (schemeEnd != std::string::npos) ? url.substr(schemeEnd + 3) : url;
		if (!hostPort.empty() && hostPort.back() == '/') hostPort.pop_back();

		auto colonPos = hostPort.rfind(':');
		if (colonPos != std::string::npos)
		{
			host = hostPort.substr(0, colonPos);
			port = atoi(hostPort.substr(colonPos + 1).c_str());
		}
		else
		{
			host = hostPort;
		}

		std::string path = "/mod/verify?nonce=" + nonce + "&sig=" + sig;
		std::string body = HttpGet(host, port, path);
		if (body.empty())
		{
			CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Mod verify HTTP request failed ({}:{})", host, port);
			return false;
		}

		try
		{
			auto j = nlohmann::json::parse(body);
			bool ok = j.value("ok", false);
			if (ok)
				CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannel: Master server confirmed mod (user={})", j.value("username", "?"));
			return ok;
		}
		catch (...)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Failed to parse mod verify response: {}", body.substr(0, 128));
			return false;
		}
	}

	static std::string GenerateNonce()
	{
		BYTE buf[32];
		BCryptGenRandom(nullptr, buf, sizeof(buf), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
		char hex[65];
		for (int i = 0; i < 32; ++i)
			snprintf(hex + i * 2, 3, "%02x", buf[i]);
		hex[64] = '\0';
		return std::string(hex);
	}

	static std::string HmacSha256(const std::string& key, const std::string& data)
	{
		BCRYPT_ALG_HANDLE hAlg = nullptr;
		BCRYPT_HASH_HANDLE hHash = nullptr;
		BYTE hash[32] = {};

		BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
		BCryptCreateHash(hAlg, &hHash, nullptr, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0);
		BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0);
		BCryptFinishHash(hHash, hash, sizeof(hash), 0);
		BCryptDestroyHash(hHash);
		BCryptCloseAlgorithmProvider(hAlg, 0);

		char hex[65];
		for (int i = 0; i < 32; ++i)
			snprintf(hex + i * 2, 3, "%02x", hash[i]);
		hex[64] = '\0';
		return std::string(hex);
	}

	static constexpr int MAX_NAME_LENGTH = 16;

	static const std::vector<std::string>& GetSlurList()
	{
		static const std::vector<std::string> slurs = {
			"nigger", "nigga", "faggot", "fag", "dyke", "kike", "tranny", "troon"
		};
		return slurs;
	}

	static std::string ToLowerStr(const std::string& s)
	{
		std::string out = s;
		for (auto& c : out) c = (char)tolower((unsigned char)c);
		return out;
	}

	bool SideChannelServer::IsNameValid(const std::string& name, std::string& reason)
	{
		if (name.empty())
		{
			reason = "name is empty";
			return false;
		}
		if (name.length() > MAX_NAME_LENGTH)
		{
			reason = "name too long (max 16)";
			return false;
		}

		for (char c : name)
		{
			if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				c == ' ' || c == '-' || c == '_' || c == '!' || c == '?'))
			{
				reason = "invalid character in name";
				return false;
			}
		}

		// block ID_ because of idiots using long strings to cover the screen 
		char blockIdBuf[8] = {};
		if (GetEnvironmentVariableA("CYPRESS_BLOCK_ID_NAMES", blockIdBuf, sizeof(blockIdBuf)) > 0
			&& strcmp(blockIdBuf, "1") == 0)
		{
			if (name.length() >= 3 && name[0] == 'I' && name[1] == 'D' && name[2] == '_')
			{
				reason = "names starting with ID_ are blocked";
				return false;
			}
		}

		std::string lower = ToLowerStr(name);
		for (const auto& slur : GetSlurList())
		{
			if (lower.find(slur) != std::string::npos)
			{
				reason = "inappropriate name";
				return false;
			}
		}

		return true;
	}

	// hash an hwid for storage - we never store raw hwids on disk
	static std::string HashHWID(const std::string& hwid)
	{
		return detail::sha256hex("cypress-mod:" + hwid);
	}

	int GetSideChannelPort()
	{
		char buf[32] = {};
		if (GetEnvironmentVariableA("CYPRESS_SIDE_CHANNEL_PORT", buf, sizeof(buf)) > 0)
		{
			int port = atoi(buf);
			if (port > 0 && port < 65536) return port;
		}
		return SIDE_CHANNEL_DEFAULT_PORT;
	}

	void WriteDiscoveryFile(int port, const char* game, bool isServer)
	{
		char tempDir[MAX_PATH];
		DWORD len = GetTempPathA(MAX_PATH, tempDir);
		if (len == 0) return;

		DWORD pid = GetCurrentProcessId();
		std::string path = std::string(tempDir) + "cypress_" + std::to_string(pid) + ".port";

		// store process creation time so stale files with reused pids can be detected
		FILETIME ct = {}, dummy = {};
		GetProcessTimes(GetCurrentProcess(), &ct, &dummy, &dummy, &dummy);
		char createTimeHex[32];
		ULONGLONG ctVal = ((ULONGLONG)ct.dwHighDateTime << 32) | ct.dwLowDateTime;
		snprintf(createTimeHex, sizeof(createTimeHex), "%016llx", ctVal);

		nlohmann::json j = {
			{"port", port},
			{"pid", (int)pid},
			{"game", game},
			{"isServer", isServer},
			{"createTime", std::string(createTimeHex)}
		};

		std::ofstream f(path);
		if (f.is_open())
		{
			f << j.dump();
			CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannel: Wrote discovery file {}", path);
		}
	}

	void DeleteDiscoveryFile()
	{
		char tempDir[MAX_PATH];
		DWORD len = GetTempPathA(MAX_PATH, tempDir);
		if (len == 0) return;

		DWORD pid = GetCurrentProcessId();
		std::string path = std::string(tempDir) + "cypress_" + std::to_string(pid) + ".port";
		DeleteFileA(path.c_str());
	}

	SideChannelServer::SideChannelServer() {}

	SideChannelServer::~SideChannelServer()
	{
		Stop();
		for (auto& t : m_clientThreads)
		{
			if (t.joinable()) t.detach();
		}
	}

	bool SideChannelServer::Start(int port)
	{
		if (m_running) return true;

		if (port <= 0) port = GetSideChannelPort();
		m_port = port;

		m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSock == INVALID_SOCKET)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannel: Failed to create listen socket ({})", WSAGetLastError());
			return false;
		}

		int optval = 1;
		setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(m_port);

		if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Port {} in use, trying fallback port", m_port);
			addr.sin_port = htons(0);
			if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
			{
				CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannel: Bind failed ({})", WSAGetLastError());
				closesocket(m_listenSock);
				m_listenSock = INVALID_SOCKET;
				return false;
			}
			sockaddr_in bound = {};
			int boundLen = sizeof(bound);
			getsockname(m_listenSock, (sockaddr*)&bound, &boundLen);
			m_port = ntohs(bound.sin_port);
		}

		if (listen(m_listenSock, SOMAXCONN) == SOCKET_ERROR)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannel: Listen failed ({})", WSAGetLastError());
			closesocket(m_listenSock);
			m_listenSock = INVALID_SOCKET;
			return false;
		}

		m_running = true;
		m_acceptThread = std::thread(&SideChannelServer::AcceptLoop, this);
		CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Server listening on port {}", m_port);
		return true;
	}

	void SideChannelServer::Stop()
	{
		if (!m_running) return;
		m_running = false;

		if (m_listenSock != INVALID_SOCKET)
		{
			closesocket(m_listenSock);
			m_listenSock = INVALID_SOCKET;
		}

		{
			std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
			for (auto& [sock, peer] : m_peers)
			{
				closesocket(sock);
			}
			m_peers.clear();
		}

		if (m_acceptThread.joinable())
			m_acceptThread.join();
	}

	void SideChannelServer::AcceptLoop()
	{
		while (m_running)
		{
			sockaddr_in clientAddr = {};
			int addrLen = sizeof(clientAddr);
			SOCKET clientSock = accept(m_listenSock, (sockaddr*)&clientAddr, &addrLen);

			if (clientSock == INVALID_SOCKET)
			{
				if (m_running)
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Accept failed ({})", WSAGetLastError());
				continue;
			}

			int nodelay = 1;
			setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

			{
				std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
				m_peers[clientSock] = SideChannelPeer{ clientSock };
			}

			char addrStr[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
			CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannel: New connection from {}", addrStr);

			m_clientThreads.emplace_back(&SideChannelServer::ClientLoop, this, clientSock);
		}
	}

	void SideChannelServer::ClientLoop(SOCKET clientSock)
	{
		static constexpr size_t MAX_RECV_BUF = 65536; // 64KB max pending data per peer
		static constexpr int RECV_TIMEOUT_MS = 30000;  // 30s idle timeout

		// set recv timeout so idle connections don't hold threads forever
		setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&RECV_TIMEOUT_MS, sizeof(RECV_TIMEOUT_MS));

		// send challenge nonce for auth
		std::string challengeNonce = GenerateNonce();
		{
			std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
			auto it = m_peers.find(clientSock);
			if (it != m_peers.end())
				it->second.challengeNonce = challengeNonce;
		}
		nlohmann::json challengeMsg = { {"type", "challenge"}, {"nonce", challengeNonce} };
		std::string challengeLine = challengeMsg.dump() + "\n";
		::send(clientSock, challengeLine.c_str(), (int)challengeLine.size(), 0);

		char buf[4096];
		while (m_running)
		{
			int bytesRead = recv(clientSock, buf, sizeof(buf) - 1, 0);
			if (bytesRead <= 0) break;

			buf[bytesRead] = '\0';

			std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
			auto it = m_peers.find(clientSock);
			if (it == m_peers.end()) break;

			SideChannelPeer& peer = it->second;
			peer.recvBuf += buf;

			// kill connection if buffer too large (no newline = likely malicious)
			if (peer.recvBuf.size() > MAX_RECV_BUF)
			{
				CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Dropping {} - recv buffer exceeded 64KB", peer.name.empty() ? "(unknown)" : peer.name);
				break;
			}

			size_t pos;
			while ((pos = peer.recvBuf.find('\n')) != std::string::npos)
			{
				std::string line = peer.recvBuf.substr(0, pos);
				peer.recvBuf.erase(0, pos + 1);
				if (!line.empty())
					ProcessLine(peer, line);
			}
		}

		RemovePeer(clientSock);
	}

	void SideChannelServer::ProcessLine(SideChannelPeer& peer, const std::string& line)
	{
		try
		{
			auto msg = nlohmann::json::parse(line);
			std::string type = msg.value("type", "");

			if (type == "serverInfo")
			{
				ServerInfo info = GetServerInfo();
				// get player names from game engine if available, fall back to side-channel peers
				nlohmann::json playerNames = nlohmann::json::array();
				int playerCount = 0;
				if (m_playerNamesCb) {
					auto names = m_playerNamesCb();
					playerCount = (int)names.size();
					for (const auto& n : names)
						playerNames.push_back(n);
				} else {
					auto peers = GetConnectedPeers();
					playerCount = (int)peers.size();
					for (const auto& [name, hwid] : peers)
						playerNames.push_back(name);
				}
				nlohmann::json response = {
					{"type", "serverInfo"},
#ifdef CYPRESS_GW1
					{"game", "GW1"},
#elif defined(CYPRESS_BFN)
					{"game", "BFN"},
#else
					{"game", "GW2"},
#endif
					{"players", playerCount},
					{"playerNames", playerNames},
					{"port", m_port}
				};
				// include game port so clients can connect to the right port when multiple servers are running
				char gamePortBuf[16] = {};
				if (GetEnvironmentVariableA("CYPRESS_SERVER_PORT", gamePortBuf, sizeof(gamePortBuf)) > 0 && gamePortBuf[0] != '\0')
					response["gamePort"] = atoi(gamePortBuf);
				else
					response["gamePort"] = 25200;
				if (!info.motd.empty()) response["motd"] = info.motd;
				if (!info.icon.empty()) response["icon"] = info.icon;
				response["modded"] = info.modded;
				if (!info.modpackUrl.empty()) response["modpackUrl"] = info.modpackUrl;
				SendToPeer(peer, response);
				return;
			}

			if (type == "auth")
			{
				peer.name = msg.value("name", "");
				peer.hwid = msg.value("hwid", "");
				std::string proof = msg.value("proof", "");
				if (msg.contains("components"))
					peer.fingerprint = Cypress::HardwareFingerprint::fromJson(msg["components"]);

				bool proofValid = false;
				if (!peer.challengeNonce.empty() && !peer.hwid.empty() && !proof.empty())
				{
					std::string expected = HmacSha256(peer.challengeNonce, peer.hwid);
					proofValid = (proof == expected);
				}

				if (!proofValid)
				{
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Auth from {} failed challenge-response", peer.name.empty() ? "(unknown)" : peer.name);
					SendToPeer(peer, { {"type", "authResult"}, {"ok", false}, {"msg", "invalid proof"} });
					return;
				}

				peer.challengeNonce.clear();

				// validate player name before accepting auth
				std::string nameRejectReason;
				if (!IsNameValid(peer.name, nameRejectReason))
				{
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Rejected name '{}': {}", peer.name, nameRejectReason);
					SendToPeer(peer, { {"type", "authResult"}, {"ok", false}, {"msg", nameRejectReason} });
					return;
				}

				// todo: replace with global auth (master server name ownership) so names are protected across servers
				auto* existing = FindPeerByName(peer.name);
				if (existing && existing->sock != peer.sock)
				{
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Rejected '{}': name already in use", peer.name);
					SendToPeer(peer, { {"type", "authResult"}, {"ok", false}, {"msg", "name already in use"} });
					return;
				}

				peer.authenticated = !peer.name.empty() && !peer.hwid.empty();
				peer.isModerator = IsModerator(peer.hwid);

				// never accept raw token, always challenge
				if (peer.authenticated && !peer.isModerator && msg.value("claimMod", false))
				{
					peer.modChallengeNonce = GenerateNonce();
					CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannel: Issuing mod challenge to {}", peer.name);
					SendToPeer(peer, { {"type", "modChallenge"}, {"nonce", peer.modChallengeNonce} });
				}

				// auto-mod first player if env var is set
				if (peer.authenticated && !peer.isModerator)
				{
					char autoModBuf[8] = {};
					if (GetEnvironmentVariableA("CYPRESS_AUTO_MOD_HOST", autoModBuf, sizeof(autoModBuf)) > 0
						&& strcmp(autoModBuf, "1") == 0)
					{
								// first player gets mod
						if (m_moderatorHWIDs.empty())
						{
							AddModerator(peer.hwid);
							SaveModerators("moderators.json");
							peer.isModerator = true;
							CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Auto-modded first player {} (HWID: {}...)",
								peer.name, peer.hwid.substr(0, 8));
							if (Cypress_IsEmbeddedMode())
								Cypress_EmitJsonPlayerEvent("modListChanged", -1, "", nullptr);
						}
					}
				}

				nlohmann::json response = {
					{"type", "authResult"},
					{"ok", peer.authenticated},
					{"moderator", peer.isModerator}
				};
				SendToPeer(peer, response);

				if (peer.authenticated)
				{
					if (m_onAuth) m_onAuth(peer);

					// authenticated clients stay connected indefinitely, clear the pre-auth timeout
					int noTimeout = 0;
					setsockopt(peer.sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&noTimeout, sizeof(noTimeout));

					CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: {} authenticated (HWID: {}...{})",
						peer.name, peer.hwid.substr(0, 8),
						peer.isModerator ? ", moderator" : "");

					// tell launcher (include components for global ban check)
					if (Cypress_IsEmbeddedMode())
					{
						nlohmann::json authEvent = {
							{"t", "sideChannelAuth"},
							{"id", -1},
							{"name", peer.name},
							{"extra", std::string(peer.isModerator ? "mod" : "player") + "|" + peer.hwid},
							{"components", peer.fingerprint.toJson()}
						};
						Cypress_WriteRawStdout(authEvent.dump() + "\n");
					}

					// send player list to new mod
					if (peer.isModerator && m_onModeratorAuth)
					{
						m_onModeratorAuth(peer);
					}
				}
				return;
			}

			if (type == "subscribe")
			{
				if (!peer.authenticated)
				{
					SendToPeer(peer, { {"type", "error"}, {"msg", "Must authenticate before subscribing"} });
					return;
				}
				peer.subscribed = true;
				SendToPeer(peer, { {"type", "subscribed"}, {"ok", true} });
				CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannel: {} subscribed to events", peer.name);
				return;
			}

			if (type == "modTokenUpdate")
			{
				if (!peer.authenticated)
				{
					SendToPeer(peer, { {"type", "error"}, {"msg", "Not authenticated"} });
					return;
				}
				if (peer.isModerator) return; // already a mod

				// rate limit: 30 seconds between challenges
				auto now = std::chrono::steady_clock::now();
				if (peer.modChallengeTime.time_since_epoch().count() > 0 &&
					std::chrono::duration_cast<std::chrono::seconds>(now - peer.modChallengeTime).count() < 30)
				{
					SendToPeer(peer, { {"type", "error"}, {"msg", "Too many mod challenge requests"} });
					return;
				}
				peer.modChallengeTime = now;

				// issue a fresh mod challenge
				peer.modChallengeNonce = GenerateNonce();
				CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannel: Issuing mod challenge to {} (token update)", peer.name);
				SendToPeer(peer, { {"type", "modChallenge"}, {"nonce", peer.modChallengeNonce} });
				return;
			}

			if (type == "modChallengeResponse")
			{
				if (!peer.authenticated || peer.isModerator) return;
				if (peer.modChallengeNonce.empty())
				{
					SendToPeer(peer, { {"type", "error"}, {"msg", "No pending mod challenge"} });
					return;
				}

				std::string sig = msg.value("sig", "");
				std::string nonce = peer.modChallengeNonce;
				peer.modChallengeNonce.clear(); // consume nonce, prevent replay

				if (sig.empty() || !ValidateModChallenge(nonce, sig))
				{
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: {} failed mod challenge", peer.name);
					SendToPeer(peer, { {"type", "error"}, {"msg", "Mod verification failed"} });
					return;
				}

				peer.isModerator = true;
				CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: {} verified as global moderator", peer.name);
				SendToPeer(peer, { {"type", "moderatorStatus"}, {"moderator", true} });
				if (m_onModeratorAuth)
					m_onModeratorAuth(peer);
				return;
			}

			// mod can request full player list at any time (belt-and-suspenders for relay timing)
			if (type == "requestPlayerList")
			{
				if (!peer.authenticated || !peer.isModerator)
				{
					SendToPeer(peer, { {"type", "error"}, {"msg", "Not a moderator"} });
					return;
				}
				if (m_onModeratorAuth)
					m_onModeratorAuth(peer);
				return;
			}

			if (!peer.authenticated)
			{
				nlohmann::json err = { {"type", "error"}, {"msg", "Not authenticated"} };
				SendToPeer(peer, err);
				return;
			}

			{
				std::lock_guard<std::mutex> hlock(m_handlersMutex);
				auto hit = m_handlers.find(type);
				if (hit != m_handlers.end())
				{
					hit->second(msg, peer);
					return;
				}
			}
		}
		catch (const std::exception& e)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Bad message from {}: {}", peer.name, e.what());
		}
	}

	void SideChannelServer::RemovePeer(SOCKET sock)
	{
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		auto it = m_peers.find(sock);
		if (it != m_peers.end())
		{
			if (it->second.authenticated)
			{
				CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: {} disconnected", it->second.name);
				if (Cypress_IsEmbeddedMode())
					Cypress_EmitJsonPlayerEvent("sideChannelDisconnect", -1, it->second.name.c_str());
			}
			closesocket(sock);
			m_peers.erase(it);
		}
	}

	void SideChannelServer::Broadcast(const nlohmann::json& msg)
	{
		std::string line = msg.dump() + "\n";
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		for (auto& [sock, peer] : m_peers)
		{
			if (peer.authenticated)
				send(sock, line.c_str(), (int)line.size(), 0);
		}
	}

	void SideChannelServer::BroadcastEvent(const nlohmann::json& msg)
	{
		std::string line = msg.dump() + "\n";
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		for (auto& [sock, peer] : m_peers)
		{
			if (peer.subscribed)
				send(sock, line.c_str(), (int)line.size(), 0);
		}
	}

	void SideChannelServer::SendTo(const std::string& playerName, const nlohmann::json& msg)
	{
		std::string line = msg.dump() + "\n";
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		for (auto& [sock, peer] : m_peers)
		{
			if (peer.authenticated && peer.name == playerName)
			{
				send(sock, line.c_str(), (int)line.size(), 0);
				return;
			}
		}
	}

	void SideChannelServer::SendToPeer(SideChannelPeer& peer, const nlohmann::json& msg)
	{
		std::string line = msg.dump() + "\n";
		send(peer.sock, line.c_str(), (int)line.size(), 0);
	}

	void SideChannelServer::SetHandler(const std::string& type, SideChannelHandler handler)
	{
		std::lock_guard<std::mutex> lock(m_handlersMutex);
		m_handlers[type] = handler;
	}

	void SideChannelServer::AddModerator(const std::string& hwid)
	{
		std::string hashed = HashHWID(hwid);
		for (const auto& h : m_moderatorHWIDs)
			if (h == hashed) return;
		m_moderatorHWIDs.push_back(hashed);

		// update connected peers
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		for (auto& [sock, peer] : m_peers)
		{
			if (peer.hwid == hwid)
			{
				peer.isModerator = true;
				SendToPeer(peer, { {"type", "moderatorStatus"}, {"moderator", true} });
			}
		}
	}

	void SideChannelServer::RemoveModerator(const std::string& hwid)
	{
		std::string hashed = HashHWID(hwid);
		m_moderatorHWIDs.erase(
			std::remove(m_moderatorHWIDs.begin(), m_moderatorHWIDs.end(), hashed),
			m_moderatorHWIDs.end());

		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		for (auto& [sock, peer] : m_peers)
		{
			if (peer.hwid == hwid)
			{
				peer.isModerator = false;
				SendToPeer(peer, { {"type", "moderatorStatus"}, {"moderator", false} });
			}
		}
	}

	bool SideChannelServer::IsModerator(const std::string& hwid) const
	{
		std::string hashed = HashHWID(hwid);
		for (const auto& h : m_moderatorHWIDs)
			if (h == hashed) return true;
		return false;
	}

	bool SideChannelServer::LoadModerators(const std::string& path)
	{
		std::ifstream file(path);
		if (!file.is_open()) return false;

		try
		{
			auto j = nlohmann::json::parse(file);
			m_moderatorHWIDs.clear();
			for (const auto& entry : j)
			{
				if (entry.is_string())
					m_moderatorHWIDs.push_back(entry.get<std::string>());
			}
			CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Loaded {} moderator(s)", m_moderatorHWIDs.size());
			return true;
		}
		catch (const std::exception& e)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannel: Failed to load moderators: {}", e.what());
			return false;
		}
	}

	bool SideChannelServer::SaveModerators(const std::string& path) const
	{
		std::ofstream file(path);
		if (!file.is_open()) return false;

		nlohmann::json j = m_moderatorHWIDs;
		file << j.dump(2);
		return file.good();
	}

	std::vector<std::pair<std::string, std::string>> SideChannelServer::GetConnectedPeers() const
	{
		std::vector<std::pair<std::string, std::string>> result;
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		for (const auto& [sock, peer] : m_peers)
		{
			if (peer.authenticated)
				result.emplace_back(peer.name, peer.hwid);
		}
		return result;
	}

	SideChannelPeer* SideChannelServer::FindPeerByName(const std::string& name)
	{
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		for (auto& [sock, peer] : m_peers)
		{
			if (peer.authenticated && peer.name == name)
				return &peer;
		}
		return nullptr;
	}

	SideChannelClient::SideChannelClient() {}

	SideChannelClient::~SideChannelClient()
	{
		Disconnect();
	}

	bool SideChannelClient::Connect(const std::string& serverIP, int port)
	{
		if (m_connected) return true;

		if (port <= 0) port = GetSideChannelPort();

		m_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_sock == INVALID_SOCKET)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannel: Failed to create socket ({})", WSAGetLastError());
			return false;
		}

		int nodelay = 1;
		setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

		// 5s connection timeout
		DWORD timeout = 5000;
		setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
		setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);

		if (inet_pton(AF_INET, serverIP.c_str(), &addr.sin_addr) != 1)
		{
			// try hostname resolution
			addrinfo hints = {};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			addrinfo* result = nullptr;
			if (getaddrinfo(serverIP.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || !result)
			{
				CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannel: Cannot resolve {}", serverIP);
				closesocket(m_sock);
				m_sock = INVALID_SOCKET;
				return false;
			}
			addr = *(sockaddr_in*)result->ai_addr;
			freeaddrinfo(result);
		}

		if (connect(m_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Connect to {}:{} failed ({})", serverIP, port, WSAGetLastError());
			closesocket(m_sock);
			m_sock = INVALID_SOCKET;
			return false;
		}

		// back to blocking for recv
		timeout = 0;
		setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		m_connected = true;
		m_recvThread = std::thread(&SideChannelClient::RecvLoop, this);

		CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Connected to {}:{}", serverIP, port);
		return true;
	}

	void SideChannelClient::Disconnect()
	{
		if (!m_connected) return;
		m_connected = false;
		m_isModerator = false;

		if (m_sock != INVALID_SOCKET)
		{
			closesocket(m_sock);
			m_sock = INVALID_SOCKET;
		}

		if (m_recvThread.joinable())
			m_recvThread.join();
	}

	void SideChannelClient::Send(const nlohmann::json& msg)
	{
		if (!m_connected || m_sock == INVALID_SOCKET) return;
		std::string line = msg.dump() + "\n";
		std::lock_guard<std::mutex> lock(m_sendMutex);
		::send(m_sock, line.c_str(), (int)line.size(), 0);
	}

	void SideChannelClient::SetHandler(const std::string& type, SideChannelHandler handler)
	{
		std::lock_guard<std::mutex> lock(m_handlersMutex);
		m_handlers[type] = handler;
	}

	void SideChannelClient::RecvLoop()
	{
		char buf[4096];
		std::string recvBuf;

		while (m_connected)
		{
			int bytesRead = recv(m_sock, buf, sizeof(buf) - 1, 0);
			if (bytesRead <= 0) break;

			buf[bytesRead] = '\0';
			recvBuf += buf;

			size_t pos;
			while ((pos = recvBuf.find('\n')) != std::string::npos)
			{
				std::string line = recvBuf.substr(0, pos);
				recvBuf.erase(0, pos + 1);
				if (!line.empty())
					ProcessLine(line);
			}
		}

		m_connected = false;
		CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Disconnected from server");
	}

	void SideChannelClient::ProcessLine(const std::string& line)
	{
		try
		{
			auto msg = nlohmann::json::parse(line);
			std::string type = msg.value("type", "");

			// handle challenge from server - compute proof and send deferred auth
			if (type == "challenge")
			{
				std::string nonce = msg.value("nonce", "");
				if (!nonce.empty() && m_pendingAuth.contains("type"))
				{
					std::string hwid = m_pendingAuth.value("hwid", "");
					std::string proof = HmacSha256(nonce, hwid);
					m_pendingAuth["proof"] = proof;
					Send(m_pendingAuth);
					m_pendingAuth = {}; // consumed
					CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannel: Responded to auth challenge");
				}
				return;
			}

			// mod challenge from server - HMAC our token with their nonce, never send the raw token
			if (type == "modChallenge")
			{
				std::string nonce = msg.value("nonce", "");
				if (nonce.empty() || m_modToken.empty())
				{
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Got mod challenge but no token available");
					return;
				}
				std::string sig = HmacSha256(m_modToken, nonce);
				Send({ {"type", "modChallengeResponse"}, {"sig", sig} });
				CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannel: Responded to mod challenge");
				return;
			}

			if (type == "authResult")
			{
				bool ok = msg.value("ok", false);
				m_isModerator = msg.value("moderator", false);
				if (ok)
					CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Authenticated{}", m_isModerator ? " (moderator)" : "");
				else
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Authentication failed");

				// tell launcher about mod status
				if (Cypress_IsEmbeddedMode())
					Cypress_EmitJsonPlayerEvent("modStatus", m_isModerator ? 1 : 0, "", nullptr);

				// if we're a mod, explicitly request the full player list
				// (the server also pushes it on auth, but this covers relay timing edge cases)
				if (ok && m_isModerator)
					Send({ {"type", "requestPlayerList"} });
			}
			else if (type == "moderatorStatus")
			{
				m_isModerator = msg.value("moderator", false);
				CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Moderator status changed: {}", m_isModerator);

				if (Cypress_IsEmbeddedMode())
					Cypress_EmitJsonPlayerEvent("modStatus", m_isModerator ? 1 : 0, "", nullptr);
			}
			// forward player events to launcher for mod ui
			else if (type == "scPlayerJoin" || type == "scPlayerLeave" || type == "scPlayerList" || type == "scModBans")
			{
				if (Cypress_IsEmbeddedMode())
				{
					// launcher expects "t" not "type"
					nlohmann::json reMsg = msg;
					reMsg["t"] = reMsg["type"];
					reMsg.erase("type");
					std::string reEmit = reMsg.dump() + "\n";
					Cypress_WriteRawStdout(reEmit);
				}
			}

			// dispatch to handlers
			{
				std::lock_guard<std::mutex> lock(m_handlersMutex);
				auto it = m_handlers.find(type);
				if (it != m_handlers.end())
				{
					it->second(msg, m_self);
				}
			}
		}
		catch (const std::exception& e)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: Bad message from server: {}", e.what());
		}
	}

	SideChannelClientListener::SideChannelClientListener() {}

	SideChannelClientListener::~SideChannelClientListener()
	{
		Stop();
		for (auto& t : m_clientThreads)
		{
			if (t.joinable()) t.detach();
		}
	}

	bool SideChannelClientListener::Start(int port)
	{
		if (m_running) return true;

		m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSock == INVALID_SOCKET) return false;

		// always use os-assigned port to avoid colliding with a server on the same machine
		// the discovery file tells the launcher what port we actually got
		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = htons(0);

		if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			closesocket(m_listenSock);
			m_listenSock = INVALID_SOCKET;
			return false;
		}

		sockaddr_in bound = {};
		int boundLen = sizeof(bound);
		getsockname(m_listenSock, (sockaddr*)&bound, &boundLen);
		m_port = ntohs(bound.sin_port);

		if (listen(m_listenSock, 4) == SOCKET_ERROR)
		{
			closesocket(m_listenSock);
			m_listenSock = INVALID_SOCKET;
			return false;
		}

		m_running = true;
		m_acceptThread = std::thread(&SideChannelClientListener::AcceptLoop, this);
		CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Client listener on port {}", m_port);
		return true;
	}

	void SideChannelClientListener::Stop()
	{
		if (!m_running) return;
		m_running = false;

		if (m_listenSock != INVALID_SOCKET)
		{
			closesocket(m_listenSock);
			m_listenSock = INVALID_SOCKET;
		}

		{
			std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
			for (auto& [sock, peer] : m_peers)
				closesocket(sock);
			m_peers.clear();
		}

		if (m_acceptThread.joinable())
			m_acceptThread.join();
	}

	void SideChannelClientListener::AcceptLoop()
	{
		while (m_running)
		{
			SOCKET clientSock = accept(m_listenSock, nullptr, nullptr);
			if (clientSock == INVALID_SOCKET) continue;

			int nodelay = 1;
			setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

			{
				std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
				m_peers[clientSock] = SideChannelPeer{ clientSock };
			}

			m_clientThreads.emplace_back(&SideChannelClientListener::ClientLoop, this, clientSock);
		}
	}

	void SideChannelClientListener::ClientLoop(SOCKET clientSock)
	{
		char buf[4096];
		while (m_running)
		{
			int bytesRead = recv(clientSock, buf, sizeof(buf) - 1, 0);
			if (bytesRead <= 0) break;

			buf[bytesRead] = '\0';

			std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
			auto it = m_peers.find(clientSock);
			if (it == m_peers.end()) break;

			SideChannelPeer& peer = it->second;
			peer.recvBuf += buf;

			size_t pos;
			while ((pos = peer.recvBuf.find('\n')) != std::string::npos)
			{
				std::string line = peer.recvBuf.substr(0, pos);
				peer.recvBuf.erase(0, pos + 1);
				if (!line.empty())
					ProcessLine(peer, line);
			}
		}

		RemovePeer(clientSock);
	}

	void SideChannelClientListener::ProcessLine(SideChannelPeer& peer, const std::string& line)
	{
		try
		{
			auto msg = nlohmann::json::parse(line);
			std::string type = msg.value("type", "");

			// serverInfo - no auth needed
			if (type == "serverInfo")
			{
				nlohmann::json response = {
					{"type", "serverInfo"},
#ifdef CYPRESS_GW1
					{"game", "GW1"},
#elif defined(CYPRESS_BFN)
					{"game", "BFN"},
#else
					{"game", "GW2"},
#endif
					{"isClient", true},
					{"port", m_port},
					{"isModerator", m_client ? m_client->IsModerator() : false}
				};
				std::string out = response.dump() + "\n";
				send(peer.sock, out.c_str(), (int)out.size(), 0);
				return;
			}

			// subscribe to events
			if (type == "subscribe")
			{
				peer.subscribed = true;
				std::string out = "{\"type\":\"subscribed\",\"ok\":true}\n";
				send(peer.sock, out.c_str(), (int)out.size(), 0);
				return;
			}

			// forward mod commands to server
			if (m_client && m_client->IsConnected() && m_client->IsModerator())
			{
				if (type == "modKick" || type == "modBan" || type == "modCommand" || type == "modFreecam"
					|| type == "addMod" || type == "removeMod")
				{
					m_client->Send(msg);
					return;
				}
			}
		}
		catch (const std::exception& e)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: ClientListener bad message: {}", e.what());
		}
	}

	void SideChannelClientListener::RemovePeer(SOCKET sock)
	{
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		auto it = m_peers.find(sock);
		if (it != m_peers.end())
		{
			closesocket(sock);
			m_peers.erase(it);
		}
	}

	void SideChannelClientListener::BroadcastEvent(const nlohmann::json& msg)
	{
		std::string line = msg.dump() + "\n";
		std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
		for (auto& [sock, peer] : m_peers)
		{
			if (peer.subscribed)
				send(sock, line.c_str(), (int)line.size(), 0);
		}
	}

	// -- SideChannelTunnel --
	// frame format: [1B cmd][4B client_id BE][4B data_len BE][data]
	// cmd: 1=OPEN, 2=DATA, 3=CLOSE

	static constexpr uint8_t TCMD_OPEN = 1;
	static constexpr uint8_t TCMD_DATA = 2;
	static constexpr uint8_t TCMD_CLOSE = 3;

	SideChannelTunnel::SideChannelTunnel() {}
	SideChannelTunnel::~SideChannelTunnel() { Stop(); }

	bool SideChannelTunnel::RecvExact(char* buf, int len)
	{
		int total = 0;
		while (total < len)
		{
			int n = recv(m_tunnelSock, buf + total, len - total, 0);
			if (n <= 0) return false;
			total += n;
		}
		return true;
	}

	void SideChannelTunnel::SendFrame(uint8_t cmd, uint32_t clientId, const char* data, uint32_t dataLen)
	{
		uint8_t header[9];
		header[0] = cmd;
		uint32_t cidBE = htonl(clientId);
		uint32_t lenBE = htonl(dataLen);
		memcpy(header + 1, &cidBE, 4);
		memcpy(header + 5, &lenBE, 4);

		std::lock_guard<std::mutex> lock(m_writeMutex);
		::send(m_tunnelSock, (const char*)header, 9, 0);
		if (data && dataLen > 0)
			::send(m_tunnelSock, data, dataLen, 0);
	}

	bool SideChannelTunnel::Start(const std::string& relayHost, int relayPort,
		const std::string& proxyKey, int localPort)
	{
		if (m_running) return true;
		m_localPort = localPort;

		m_tunnelSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_tunnelSock == INVALID_SOCKET)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannelTunnel: Failed to create socket ({})", WSAGetLastError());
			return false;
		}

		int nodelay = 1;
		setsockopt(m_tunnelSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

		DWORD timeout = 5000;
		setsockopt(m_tunnelSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
		setsockopt(m_tunnelSock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(relayPort);

		if (inet_pton(AF_INET, relayHost.c_str(), &addr.sin_addr) != 1)
		{
			addrinfo hints = {};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			addrinfo* result = nullptr;
			if (getaddrinfo(relayHost.c_str(), std::to_string(relayPort).c_str(), &hints, &result) != 0 || !result)
			{
				CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannelTunnel: Cannot resolve {}", relayHost);
				closesocket(m_tunnelSock);
				m_tunnelSock = INVALID_SOCKET;
				return false;
			}
			addr = *(sockaddr_in*)result->ai_addr;
			freeaddrinfo(result);
		}

		if (connect(m_tunnelSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "SideChannelTunnel: Connect to {}:{} failed ({})",
				relayHost, relayPort, WSAGetLastError());
			closesocket(m_tunnelSock);
			m_tunnelSock = INVALID_SOCKET;
			return false;
		}

		// back to blocking for recv
		timeout = 0;
		setsockopt(m_tunnelSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		// send register handshake (JSON line, then binary frames)
		nlohmann::json handshake = { {"type", "register"}, {"key", proxyKey} };
		std::string line = handshake.dump() + "\n";
		::send(m_tunnelSock, line.c_str(), (int)line.size(), 0);

		m_running = true;
		m_thread = std::thread(&SideChannelTunnel::TunnelLoop, this);

		CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannelTunnel: Connected to {}:{}", relayHost, relayPort);
		return true;
	}

	void SideChannelTunnel::Stop()
	{
		m_running = false;

		if (m_tunnelSock != INVALID_SOCKET)
		{
			closesocket(m_tunnelSock);
			m_tunnelSock = INVALID_SOCKET;
		}

		// close all local sockets so reader threads exit
		{
			std::lock_guard<std::mutex> lock(m_clientsMutex);
			for (auto& [id, sock] : m_localClients)
				closesocket(sock);
			m_localClients.clear();
		}

		if (m_thread.joinable()) m_thread.join();
	}

	void SideChannelTunnel::TunnelLoop()
	{
		while (m_running)
		{
			uint8_t header[9];
			if (!RecvExact((char*)header, 9))
				break;

			uint8_t cmd = header[0];
			uint32_t clientId, dataLen;
			memcpy(&clientId, header + 1, 4);
			memcpy(&dataLen, header + 5, 4);
			clientId = ntohl(clientId);
			dataLen = ntohl(dataLen);

			std::vector<char> data;
			if (dataLen > 0)
			{
				if (dataLen > 1024 * 1024) break; // sanity limit
				data.resize(dataLen);
				if (!RecvExact(data.data(), dataLen))
					break;
			}

			switch (cmd)
			{
			case TCMD_OPEN:
			{
				// create local tcp connection to side-channel server
				SOCKET localSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (localSock == INVALID_SOCKET)
				{
					SendFrame(TCMD_CLOSE, clientId);
					break;
				}

				int nd = 1;
				setsockopt(localSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));

				sockaddr_in localAddr = {};
				localAddr.sin_family = AF_INET;
				localAddr.sin_port = htons(m_localPort);
				localAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

				if (connect(localSock, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR)
				{
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannelTunnel: Local connect failed for cid={}", clientId);
					closesocket(localSock);
					SendFrame(TCMD_CLOSE, clientId);
					break;
				}

				{
					std::lock_guard<std::mutex> lock(m_clientsMutex);
					m_localClients[clientId] = localSock;
				}

				// spawn reader thread (detached, self-managing)
				std::thread(&SideChannelTunnel::ClientReadLoop, this, clientId, localSock).detach();
				CYPRESS_LOGMESSAGE(LogLevel::Debug, "SideChannelTunnel: Opened local connection for cid={}", clientId);
				break;
			}
			case TCMD_DATA:
			{
				std::lock_guard<std::mutex> lock(m_clientsMutex);
				auto it = m_localClients.find(clientId);
				if (it != m_localClients.end())
					::send(it->second, data.data(), (int)dataLen, 0);
				break;
			}
			case TCMD_CLOSE:
			{
				SOCKET sock = INVALID_SOCKET;
				{
					std::lock_guard<std::mutex> lock(m_clientsMutex);
					auto it = m_localClients.find(clientId);
					if (it != m_localClients.end())
					{
						sock = it->second;
						m_localClients.erase(it);
					}
				}
				if (sock != INVALID_SOCKET)
					closesocket(sock);
				break;
			}
			}
		}

		m_running = false;
		CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannelTunnel: Disconnected");
	}

	void SideChannelTunnel::ClientReadLoop(uint32_t clientId, SOCKET localSock)
	{
		char buf[4096];
		while (m_running)
		{
			int n = recv(localSock, buf, sizeof(buf), 0);
			if (n <= 0) break;
			SendFrame(TCMD_DATA, clientId, buf, (uint32_t)n);
		}

		// tell relay this client is done
		if (m_running)
			SendFrame(TCMD_CLOSE, clientId);

		// clean up
		{
			std::lock_guard<std::mutex> lock(m_clientsMutex);
			auto it = m_localClients.find(clientId);
			if (it != m_localClients.end())
			{
				closesocket(it->second);
				m_localClients.erase(it);
			}
		}
	}
}
