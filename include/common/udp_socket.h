#pragma once

/*
 * Raw UDP socket wrapper.
 *
 * DDS (via FastRTPS) manages the actual transport for all IDL-typed messages.
 * This class is retained for any non-DDS UDP usage (e.g., legacy tooling,
 * raw binary log file transfers) and as the underlying transport layer that
 * the DDS participant internally relies on.
 *
 * MessageSerializer has been removed: all serialization is now performed by
 * the IDL-generated CDR code (messages.cxx / messagesPubSubTypes.cxx) and
 * the DDS DataWriter/DataReader API in dds_participant.h.
 */

#include <string>
#include <cstdint>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using SocketHandle = SOCKET;
    static constexpr SocketHandle INVALID_SOCK = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    using SocketHandle = int;
    static constexpr SocketHandle INVALID_SOCK = -1;
#endif

namespace cuas {

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool bindSocket(const std::string& ip, int port);
    bool setDestination(const std::string& ip, int port);
    bool setReceiveTimeout(int timeoutMs);
    bool setBufferSize(int recvSize, int sendSize);

    int  receive(uint8_t* buffer, int maxLen);
    int  receive(uint8_t* buffer, int maxLen, std::string& senderIp, int& senderPort);
    bool send(const uint8_t* data, int len);
    bool send(const uint8_t* data, int len, const std::string& ip, int port);

    void closeSocket();
    bool isValid() const;

    static bool initNetwork();
    static void cleanupNetwork();

private:
    SocketHandle sock_;
    sockaddr_in  destAddr_;
    bool         destSet_ = false;
};

} // namespace cuas
