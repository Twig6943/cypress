#include "pch.h"
#include "Client.h"
#include <cstdlib>
#include <fb/Engine/Message.h>
#include <fb/Engine/TypeInfo.h>
#include <HWID.h>
#include <Core/Logging.h>
#include <FreeCam.h>

#ifdef CYPRESS_BFN
#define OFFSET_FB_CLIENTPLAYERSELECTENTITY_ADDPERMANENTUSER 0x1417A3910
#endif

namespace
{
	Kyber::SocketSpawnInfo CreateSocketSpawnInfo()
	{
		const char* proxyAddress = std::getenv("CYPRESS_PROXY_ADDRESS");
		const char* proxyKey = std::getenv("CYPRESS_PROXY_KEY");
		const bool isProxied = proxyAddress != nullptr && proxyAddress[0] != '\0';
		return Kyber::SocketSpawnInfo(isProxied, isProxied ? proxyAddress : "", isProxied && proxyKey != nullptr ? proxyKey : "");
	}
}

namespace Cypress
{
	Client::Client()
		: m_socketManager(new Kyber::SocketManager(Kyber::ProtocolDirection::Serverbound, CreateSocketSpawnInfo()))
		, m_playerName(nullptr)
#ifdef CYPRESS_BFN
		, m_primaryUser(nullptr)
#endif
		, m_fbClientInstance(nullptr)
		, m_clientState(fb::ClientState::ClientState_None)
		, m_joiningServer(false)
#ifdef CYPRESS_BFN
		, m_addedPrimaryUser(false)
#endif
	{
	}

	Client::~Client()
	{
		StopClientListener();
		DisconnectSideChannel();
	}

	void Client::onMessage(fb::Message& inMessage)
	{

	}

	void Client::ConnectSideChannel(const std::string& serverIP, int port)
	{
		if (m_sideChannel.IsConnected()) return;

#ifdef CYPRESS_GW2
		// Register freecam handler before connecting
		m_sideChannel.SetHandler("freecam", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			bool nowActive = ToggleFreeCam();
			CYPRESS_LOGMESSAGE(LogLevel::Info, "Freecam {}", nowActive ? "activated" : "deactivated");
		});
#endif

		if (m_sideChannel.Connect(serverIP, port))
		{
			// if we're going through a relay, send handshake so the relay knows which server to proxy to
			const char* proxyKey = std::getenv("CYPRESS_PROXY_KEY");
			if (proxyKey && proxyKey[0] != '\0')
			{
				m_sideChannel.Send({
					{"type", "relay"},
					{"key", std::string(proxyKey)}
				});
			}

			nlohmann::json authMsg = {
				{"type", "auth"},
				{"name", m_playerName ? std::string(m_playerName) : ""},
				{"hwid", m_hwid},
				{"components", m_fingerprint.toJson()}
			};

			// mod token is pushed via stdin after launch, triggers modTokenUpdate flow

			m_sideChannel.SendAuth(authMsg);
		}
	}

	void Client::DisconnectSideChannel()
	{
		m_sideChannel.Disconnect();
	}

	void Client::StartClientListener()
	{
		int port = GetSideChannelPort();
		m_clientListener.SetClient(&m_sideChannel);
		if (m_clientListener.Start(port))
		{
			// Write discovery file so launcher can find us
			WriteDiscoveryFile(m_clientListener.GetPort(), CYPRESS_GAME_NAME, false);
		}
	}

	void Client::StopClientListener()
	{
		m_clientListener.Stop();
		DeleteDiscoveryFile();
	}

#ifdef CYPRESS_BFN
	void Client::AddPrimaryUser()
	{
		if (m_primaryUser == nullptr) return;

		using tAddPrimaryUser = void(*)(void*, void**, unsigned int);
		auto addPrimaryUser = reinterpret_cast<tAddPrimaryUser>(OFFSET_FB_CLIENTPLAYERSELECTENTITY_ADDPERMANENTUSER);

		addPrimaryUser(nullptr, &m_primaryUser, 0);

		m_addedPrimaryUser = true;
	}
#endif
}
