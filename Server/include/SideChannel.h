#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

namespace Cypress
{
	static constexpr int SIDE_CHANNEL_DEFAULT_PORT = 14638;

	int GetSideChannelPort();

	// discovery file at %TEMP%\cypress_<PID>.port
	void WriteDiscoveryFile(int port, const char* game, bool isServer);
	void DeleteDiscoveryFile();

	struct SideChannelPeer
	{
		SOCKET sock = INVALID_SOCKET;
		std::string name;
		std::string hwid;
		Cypress::HardwareFingerprint fingerprint;
		bool authenticated = false;
		bool isModerator = false;
		bool subscribed = false;
		std::string recvBuf;
		std::string challengeNonce; // server sends this on connect, client must prove hwid ownership
		std::string modChallengeNonce; // server sends this when client claims mod, client must HMAC with token
		std::chrono::steady_clock::time_point modChallengeTime{}; // rate limit mod challenges
	};

	using SideChannelHandler = std::function<void(const nlohmann::json& msg, SideChannelPeer& peer)>;
	using SideChannelAuthCallback = std::function<void(SideChannelPeer& peer)>;
	using PlayerNamesCallback = std::function<std::vector<std::string>()>;

	struct ServerInfo
	{
		std::string motd;
		std::string icon; // base64 JPEG
		bool modded = false;
		std::string modpackUrl;
	};

	class SideChannelServer
	{
	public:
		SideChannelServer();
		~SideChannelServer();

		bool Start(int port = 0);
		void Stop();
		bool IsRunning() const { return m_running; }
		int GetPort() const { return m_port; }

		void Broadcast(const nlohmann::json& msg);
		void BroadcastEvent(const nlohmann::json& msg); // subscribed peers only
		void SendTo(const std::string& playerName, const nlohmann::json& msg);
		void SendToPeer(SideChannelPeer& peer, const nlohmann::json& msg);

		void SetHandler(const std::string& type, SideChannelHandler handler);
		void SetOnModeratorAuth(SideChannelAuthCallback cb) { m_onModeratorAuth = cb; }
		void SetOnAuth(SideChannelAuthCallback cb) { m_onAuth = cb; }
		void SetPlayerNamesCallback(PlayerNamesCallback cb) { m_playerNamesCb = cb; }

		void AddModerator(const std::string& hwid);
		void RemoveModerator(const std::string& hwid);
		bool IsModerator(const std::string& hwid) const;
		const std::vector<std::string>& GetModeratorHWIDs() const { return m_moderatorHWIDs; }
		bool LoadModerators(const std::string& path);
		bool SaveModerators(const std::string& path) const;

		std::vector<std::pair<std::string, std::string>> GetConnectedPeers() const; // {name, hwid}
		SideChannelPeer* FindPeerByName(const std::string& name);

		static bool IsNameValid(const std::string& name, std::string& reason);

		// server info for unauthenticated queries
		void SetServerInfo(const ServerInfo& info) { std::lock_guard<std::recursive_mutex> lock(m_peersMutex); m_serverInfo = info; }
		ServerInfo GetServerInfo() const { std::lock_guard<std::recursive_mutex> lock(m_peersMutex); return m_serverInfo; }

	private:
		void AcceptLoop();
		void ClientLoop(SOCKET clientSock);
		void ProcessLine(SideChannelPeer& peer, const std::string& line);
		void RemovePeer(SOCKET sock);

		SOCKET m_listenSock = INVALID_SOCKET;
		bool m_running = false;
		int m_port = 0;
		std::thread m_acceptThread;

		mutable std::recursive_mutex m_peersMutex;
		std::unordered_map<SOCKET, SideChannelPeer> m_peers;
		std::vector<std::thread> m_clientThreads;

		std::mutex m_handlersMutex;
		std::unordered_map<std::string, SideChannelHandler> m_handlers;

		std::vector<std::string> m_moderatorHWIDs;
		SideChannelAuthCallback m_onModeratorAuth;
		SideChannelAuthCallback m_onAuth;
		PlayerNamesCallback m_playerNamesCb;
		ServerInfo m_serverInfo;
	};

	class SideChannelClient
	{
	public:
		SideChannelClient();
		~SideChannelClient();

		bool Connect(const std::string& serverIP, int port = 0);
		void Disconnect();
		bool IsConnected() const { return m_connected; }

		void Send(const nlohmann::json& msg);
		// queue auth message to be sent after server challenge is received
		void SendAuth(const nlohmann::json& msg) { m_pendingAuth = msg; }
		void SetHandler(const std::string& type, SideChannelHandler handler);

		bool IsModerator() const { return m_isModerator; }
		void SetModToken(const std::string& token) { m_modToken = token; }

	private:
		void RecvLoop();
		void ProcessLine(const std::string& line);

		SOCKET m_sock = INVALID_SOCKET;
		bool m_connected = false;
		std::thread m_recvThread;
		std::mutex m_sendMutex;
		SideChannelPeer m_self; // dummy peer for handler callbacks

		std::mutex m_handlersMutex;
		std::unordered_map<std::string, SideChannelHandler> m_handlers;

		bool m_isModerator = false;
		std::string m_modToken; // stored locally, never sent to server
		nlohmann::json m_pendingAuth; // held until challenge received
	};

	class SideChannelClientListener
	{
	public:
		SideChannelClientListener();
		~SideChannelClientListener();

		bool Start(int port = 0);
		void Stop();
		bool IsRunning() const { return m_running; }
		int GetPort() const { return m_port; }

		void BroadcastEvent(const nlohmann::json& msg);

		void SetClient(SideChannelClient* client) { m_client = client; }

	private:
		void AcceptLoop();
		void ClientLoop(SOCKET clientSock);
		void ProcessLine(SideChannelPeer& peer, const std::string& line);
		void RemovePeer(SOCKET sock);

		SOCKET m_listenSock = INVALID_SOCKET;
		bool m_running = false;
		int m_port = 0;
		std::thread m_acceptThread;

		mutable std::recursive_mutex m_peersMutex;
		std::unordered_map<SOCKET, SideChannelPeer> m_peers;
		std::vector<std::thread> m_clientThreads;

		SideChannelClient* m_client = nullptr;
	};

	// tunnels side-channel through the relay for NAT-ed servers
	// connects outbound to relay, demuxes framed client connections to localhost
	class SideChannelTunnel
	{
	public:
		SideChannelTunnel();
		~SideChannelTunnel();

		bool Start(const std::string& relayHost, int relayPort, const std::string& proxyKey, int localPort);
		void Stop();
		bool IsRunning() const { return m_running; }

	private:
		void TunnelLoop();
		void ClientReadLoop(uint32_t clientId, SOCKET localSock);
		void SendFrame(uint8_t cmd, uint32_t clientId, const char* data = nullptr, uint32_t dataLen = 0);
		bool RecvExact(char* buf, int len);

		SOCKET m_tunnelSock = INVALID_SOCKET;
		int m_localPort = 0;
		bool m_running = false;
		std::thread m_thread;
		std::mutex m_writeMutex;
		std::mutex m_clientsMutex;
		std::unordered_map<uint32_t, SOCKET> m_localClients;
	};
}
