#pragma once

#include "types.h"
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

// Serialization helpers for network messages
class MessageSerializer {
public:
    static std::vector<uint8_t> serialize(const SPDetectionMessage& msg);
    static bool deserialize(const uint8_t* data, int len, SPDetectionMessage& msg);

    static std::vector<uint8_t> serialize(const TrackUpdateMessage& msg);
    static bool deserialize(const uint8_t* data, int len, TrackUpdateMessage& msg);

    static std::vector<uint8_t> serializeTrackTable(
        const std::vector<TrackUpdateMessage>& tracks, uint64_t timestamp);
    static bool deserializeTrackTable(
        const uint8_t* data, int len,
        std::vector<TrackUpdateMessage>& tracks, uint64_t& timestamp);
};

} // namespace cuas
