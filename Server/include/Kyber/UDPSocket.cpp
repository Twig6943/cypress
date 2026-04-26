#include "pch.h"
#include "SocketManager.h"

#include "UDPSocket.h"
#include <cstdlib>

#define _WINSOCKAPI_
#include <Windows.h>
#include <ws2tcpip.h>

namespace Kyber
{
    std::string DirectionToString(ProtocolDirection direction)
    {
        switch (direction)
        {
        case ProtocolDirection::Serverbound:
            return "Client";
        case ProtocolDirection::Clientbound:
            return "Server";
        }

        return "";
    }

    UDPSocket::UDPSocket(SocketCreator* creator, ProtocolDirection direction, SocketSpawnInfo info)
        : m_creator(creator)
        , m_socketHandle(INVALID_SOCKET)
        , m_port(0)
        , m_isBroadcasting(false)
        , m_blockingMode(false)
        , m_peerAddressIsValid(false)
        , m_direction(direction)
        , m_info(info)
        , m_keepaliveRunning(false)
    {}

    UDPSocket::~UDPSocket()
    {
        Close();
    }

    void UDPSocket::Close()
    {
        CYPRESS_DEBUG_LOGMESSAGE(LogLevel::Debug, "Closing socket");
        m_keepaliveRunning = false;
        if (m_keepaliveThread.joinable())
            m_keepaliveThread.join();
        if (m_socketHandle != INVALID_SOCKET)
        {
            //closesocket(m_socketHandle);
            if (closesocket(m_socketHandle) == SOCKET_ERROR) {
                CYPRESS_LOGMESSAGE(LogLevel::Error, "Failed to close socket ({})", WSAGetLastError());
            }
        }

        m_socketHandle = INVALID_SOCKET;
    }

    uint16_t GetProxyPort()
    {
        const char* proxyPort = std::getenv("CYPRESS_PROXY_PORT");
        if (proxyPort != nullptr)
        {
            int parsedPort = atoi(proxyPort);
            if (parsedPort > 0 && parsedPort <= 65535)
            {
                return static_cast<uint16_t>(parsedPort);
            }
        }

        return 25200;
    }

    SocketAddr GetProxyAddress(const char* proxyAddress)
    {
        SocketAddr addr(proxyAddress, GetProxyPort());
        // Log the resolved address for diagnostics
        const sockaddr_in* resolved = reinterpret_cast<const sockaddr_in*>(addr.Data());
        char ipBuf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &resolved->sin_addr, ipBuf, sizeof(ipBuf));
        CYPRESS_LOGMESSAGE(LogLevel::Info, "Proxy address resolved: {} -> {}:{}",
            proxyAddress, ipBuf, ntohs(resolved->sin_port));
        return addr;
    }

    size_t GetProxyKeyLength(const char* proxyKey)
    {
        if (proxyKey == nullptr)
        {
            return 0;
        }

        size_t proxyKeyLength = strlen(proxyKey);
        return proxyKeyLength > 255 ? 255 : proxyKeyLength;
    }

    void SendProxyRegistration(SOCKET socketHandle, const char* proxyAddress, const char* proxyKey)
    {
        std::string registration = std::string("CYPRESS_PROXY_REGISTER|SERVER|") + std::string(proxyKey == nullptr ? "" : proxyKey);
        SocketAddr relayAddress = GetProxyAddress(proxyAddress);
        sendto(socketHandle, registration.c_str(), (int)registration.size(), 0, (sockaddr*)relayAddress.Data(), sizeof(sockaddr_in));
    }

    void PrependProxyKey(uint8_t*& buffer, int& bufferSize, const char* proxyKey)
    {
        const size_t proxyKeyLength = GetProxyKeyLength(proxyKey);
        bufferSize += static_cast<int>(proxyKeyLength) + 1;
        uint8_t* proxyBuffer = new uint8_t[bufferSize];
        proxyBuffer[0] = static_cast<uint8_t>(proxyKeyLength);
        if (proxyKeyLength > 0)
        {
            memcpy(proxyBuffer + 1, proxyKey, proxyKeyLength);
        }
        for (int i = 0; i < bufferSize - (int)proxyKeyLength - 1; i++)
        {
            proxyBuffer[i + (int)proxyKeyLength + 1] = buffer[i];
        }
        buffer = proxyBuffer;
    }

    void ProxifyBuffer(uint8_t*& buffer, int& bufferSize, sockaddr* addr)
    {
        bufferSize += 6;
        uint8_t* proxyBuffer = new uint8_t[bufferSize];
        auto* sourceAddr = (sockaddr_in*)addr;
        memcpy(proxyBuffer, &sourceAddr->sin_addr.s_addr, 4);
        proxyBuffer[4] = static_cast<uint8_t>((sourceAddr->sin_port >> 8) & 0xFF);
        proxyBuffer[5] = static_cast<uint8_t>(sourceAddr->sin_port & 0xFF);
        for (int i = 0; i < bufferSize - 6; i++)
        {
            proxyBuffer[i + 6] = buffer[i];
        }
        // We don't need to worry about freeing the original buffer here, Frostbite's networking engine does it for us.
        buffer = proxyBuffer;
    }

    bool UDPSocket::Send(uint8_t* buffer, int bufferSize, unsigned int flags)
    {
        sockaddr* addr = (sockaddr*)m_peerAddress.Data();;

        if (m_info.isProxied)
        {
            if (m_direction == ProtocolDirection::Clientbound)
            {
                ProxifyBuffer(buffer, bufferSize, addr);
            }
            else
            {
                PrependProxyKey(buffer, bufferSize, m_info.proxyKey);
            }
            addr = (sockaddr*)m_cachedProxyAddr.Data();

            static int proxySendCount = 0;
            if (++proxySendCount <= 5)
            {
                const sockaddr_in* dst = (sockaddr_in*)addr;
                char ipBuf[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &dst->sin_addr, ipBuf, sizeof(ipBuf));
                CYPRESS_LOGMESSAGE(LogLevel::Info, "Proxied Send #{}: dir={} size={} dst={}:{}",
                    proxySendCount,
                    m_direction == ProtocolDirection::Clientbound ? "Clientbound" : "Serverbound",
                    bufferSize, ipBuf, ntohs(dst->sin_port));
            }
        }

        if (sendto(m_socketHandle, reinterpret_cast<const char*>(buffer), bufferSize, 0, addr, sizeof(sockaddr_in)) < 0)
        {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK)
            {
                //TEMP_LOG("ERROR: Failed to send data: %d \n", error);
                return false;
            }
            return true;
        }

        //printf("DEBUG++: Sent %d bytes\n", bufferSize);

        return true;
    }

    int UDPSocket::ReceiveFrom(uint8_t* buffer, int bufferSize)
    {
        int addressSize = sizeof(sockaddr_in);
        sockaddr_in addr;

        int recvSize = recvfrom(m_socketHandle, (char*)buffer, bufferSize, 0, (sockaddr*)&addr, &addressSize);
        if (recvSize < 0)
        {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK)
            {
                //printf("WARNING: Error receiving data: %d \n", error);
            }
            return recvSize;
        }

        // Filter relay ACK packets - not game data
        static const char ackMsg[] = "CYPRESS_PROXY_ACK";
        constexpr int ackLen = sizeof(ackMsg) - 1;
        if (recvSize == ackLen && memcmp(buffer, ackMsg, ackLen) == 0)
        {
            CYPRESS_LOGMESSAGE(LogLevel::Info, "Relay ACK received on {} socket",
                m_direction == ProtocolDirection::Clientbound ? "Clientbound" : "Serverbound");
            WSASetLastError(WSAEWOULDBLOCK);
            return -1;
        }

        if (m_info.isProxied)
        {
            static int proxyRecvCount = 0;
            if (++proxyRecvCount <= 5)
            {
                char ipBuf[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf));
                CYPRESS_LOGMESSAGE(LogLevel::Info, "Proxied Recv #{}: dir={} size={} src={}:{}",
                    proxyRecvCount,
                    m_direction == ProtocolDirection::Clientbound ? "Clientbound" : "Serverbound",
                    recvSize, ipBuf, ntohs(addr.sin_port));
            }
        }

        if (m_info.isProxied && m_direction == ProtocolDirection::Clientbound)
        {
            memcpy(&addr.sin_addr.s_addr, buffer, 4);
            addr.sin_port = buffer[4] << 8 | buffer[5];
            for (int i = 0; i < recvSize - 6; i++)
            {
                buffer[i] = buffer[i + 6];
            }
            recvSize -= 6;
        }

        m_peerAddress.SetData(&addr, sizeof(sockaddr_in));
        m_peerAddressIsValid = true;

        //printf("DEBUG++: Received %d bytes \n", recvSize);

        return recvSize;
    }

#ifdef CYPRESS_BFN
    int ISocket::ReceiveFromWhen(uint8_t* buffer, int maxSize, unsigned int& receivedWhen)
    {
        receivedWhen = 0;
        return ReceiveFrom(buffer, maxSize);
    }
#endif

    bool UDPSocket::Listen(const SocketAddr& address, bool blocking)
    {
        m_socketHandle = INVALID_SOCKET;

        sockaddr_in* addr = (sockaddr_in*)address.Data();
        if ((m_socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
        {
            CYPRESS_LOGMESSAGE(LogLevel::Error, "Failed to create socket ({})", WSAGetLastError());
            return false;
        }

        if (bind(m_socketHandle, (sockaddr*)addr, sizeof(sockaddr_in)) != 0)
        {
            Close();
            CYPRESS_LOGMESSAGE(LogLevel::Error, "Failed to bind socket ({})", WSAGetLastError());
            return false;
        }

        if (!SetBlockingMode(blocking))
        {
            Close();
            CYPRESS_LOGMESSAGE(LogLevel::Error, "Failed to set blocking mode of socket");
            return false;
        }

        m_port = ntohs(addr->sin_port);
        m_address = address;

        //puts("DEBUG++: Created listening socket");
        CYPRESS_DEBUG_LOGMESSAGE(LogLevel::Debug, "Created listening socket");

        if (m_info.isProxied)
        {
            m_cachedProxyAddr = GetProxyAddress(m_info.proxyAddress);
        }

        if (m_info.isProxied && m_direction == ProtocolDirection::Clientbound)
        {
            SendProxyRegistration(m_socketHandle, m_info.proxyAddress, m_info.proxyKey);
            CYPRESS_LOGMESSAGE(LogLevel::Info, "Sent proxy registration to {}", m_info.proxyAddress);

            // Keep the NAT hole open with periodic keepalives
            std::string keepaliveAddr(m_info.proxyAddress);
            std::string keepaliveKey(m_info.proxyKey ? m_info.proxyKey : "");
            m_keepaliveRunning = true;
            m_keepaliveThread = std::thread([this, keepaliveAddr, keepaliveKey]() {
                while (m_keepaliveRunning) {
                    for (int i = 0; i < 200 && m_keepaliveRunning; ++i)
                        Sleep(100);
                    if (m_keepaliveRunning && m_socketHandle != INVALID_SOCKET) {
                        SendProxyRegistration(m_socketHandle, keepaliveAddr.c_str(),
                            keepaliveKey.empty() ? nullptr : keepaliveKey.c_str());
                    }
                }
            });
        }

        return true;
    }

    bool UDPSocket::Connect(const SocketAddr& address, bool blocking)
    {
        return false;
    }

    bool UDPSocket::Create(bool blocking)
    {
        return false;
    }

    bool UDPSocket::SetBlockingMode(bool blocking)
    {
        if (m_socketHandle != INVALID_SOCKET)
        {
            u_long mode = blocking ? 0 : 1;
            if (ioctlsocket(m_socketHandle, FIONBIO, &mode) == SOCKET_ERROR)
            {
                return false;
            }
            int bufferSize = 600000;
            if (setsockopt(m_socketHandle, SOL_SOCKET, SO_SNDBUF, (char*)&bufferSize, sizeof(bufferSize)) == SOCKET_ERROR)
            {
                return false;
            }
            if (setsockopt(m_socketHandle, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(bufferSize)) == SOCKET_ERROR)
            {
                return false;
            }
        }

        m_blockingMode = blocking;
        return true;
    }

    //void UDPSocket::SendProxyHandshake()
    //{
    //    std::stringstream handshake;
    //    handshake << "PROXY_HANDSHAKE|";
    //    handshake << (m_direction == ProtocolDirection::Clientbound ? "SERVER" : "CLIENT") << "|";
    //    handshake << m_info.serverName << "|";
    //    handshake << std::getenv("EALaunchEAID") << "|";
    //    //handshake << PlatformUtils::GetFrostyMods();
    //    std::string str = handshake.str();
    //    printf("DEBUG: Sending handshake: %s \n", str);
    //    Send(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(str.c_str())), str.length());
    //}

    void UDPSocket::SetPeerAddress(const SocketAddr& address)
    {
        sockaddr_in* addr = (sockaddr_in*)address.Data();
        //printf("DEBUG++: Setting peer address (%hu) \n", ntohs(addr->sin_port));
        m_peerAddress = address;
        m_peerAddressIsValid = true;
    }

    SocketAddr UDPSocket::PeerAddress() const
    {
        return m_peerAddress;
    }

    bool UDPSocket::SetBroadcast(uint16_t port)
    {
        //TEMP_LOG("DEBUG++: Setting broadcast port to %hu \n", port);
        return false;
    }

    bool UDPSocket::BlockingMode() const
    {
        return false;
    }

    intptr_t UDPSocket::NativeSocket() const
    {
        return m_socketHandle;
    }

    int UDPSocket::Port() const
    {
        return m_port;
    }

    const SocketAddr* UDPSocket::Address() const
    {
        return &m_address;
    }

    bool UDPSocket::SetDefaultPacketInfo(PacketInfo* info)
    {
        if (!info)
        {
            //TEMP_LOG("ERROR: Invalid packet info");
            return false;
        }
        info->minSize = 1;
        info->maxSize = 1264;
        info->recommendedSize = info->maxSize;
        info->overheadWhenAligned = 28;
        info->alignment = 1;
        return true;
    }
} // namespace Kyber