#include "common/udp_socket.h"
#include "common/types.h"
#include "common/logger.h"
#include "common/constants.h"
#include <cstring>
#include <vector>

#ifdef _WIN32
    static bool wsaInitialized = false;
#endif

namespace cuas {

bool UdpSocket::initNetwork() {
#ifdef _WIN32
    if (!wsaInitialized) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            LOG_ERROR("UdpSocket", "WSAStartup failed");
            return false;
        }
        wsaInitialized = true;
    }
#endif
    return true;
}

void UdpSocket::cleanupNetwork() {
#ifdef _WIN32
    if (wsaInitialized) {
        WSACleanup();
        wsaInitialized = false;
    }
#endif
}

UdpSocket::UdpSocket() : sock_(INVALID_SOCK) {
    std::memset(&destAddr_, 0, sizeof(destAddr_));
    initNetwork();
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCK) {
        LOG_ERROR("UdpSocket", "Failed to create socket");
    }
}

UdpSocket::~UdpSocket() {
    closeSocket();
}

bool UdpSocket::bindSocket(const std::string& ip, int port) {
    if (sock_ == INVALID_SOCK) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("UdpSocket", "Bind failed on %s:%d", ip.c_str(), port);
        return false;
    }
    LOG_INFO("UdpSocket", "Bound to %s:%d", ip.c_str(), port);
    return true;
}

bool UdpSocket::setDestination(const std::string& ip, int port) {
    destAddr_.sin_family = AF_INET;
    destAddr_.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &destAddr_.sin_addr);
    destSet_ = true;
    return true;
}

bool UdpSocket::setReceiveTimeout(int timeoutMs) {
    if (sock_ == INVALID_SOCK) return false;
#ifdef _WIN32
    DWORD tv = timeoutMs;
    return setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#else
    struct timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#endif
}

bool UdpSocket::setBufferSize(int recvSize, int sendSize) {
    if (sock_ == INVALID_SOCK) return false;
    setsockopt(sock_, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&recvSize), sizeof(recvSize));
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&sendSize), sizeof(sendSize));
    return true;
}

int UdpSocket::receive(uint8_t* buffer, int maxLen) {
    if (sock_ == INVALID_SOCK) return -1;
    int n = ::recvfrom(sock_, reinterpret_cast<char*>(buffer), maxLen, 0, nullptr, nullptr);
    return n;
}

int UdpSocket::receive(uint8_t* buffer, int maxLen, std::string& senderIp, int& senderPort) {
    if (sock_ == INVALID_SOCK) return -1;
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = ::recvfrom(sock_, reinterpret_cast<char*>(buffer), maxLen, 0,
                       reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n > 0) {
        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ipBuf, sizeof(ipBuf));
        senderIp = ipBuf;
        senderPort = ntohs(from.sin_port);
    }
    return n;
}

bool UdpSocket::send(const uint8_t* data, int len) {
    if (sock_ == INVALID_SOCK || !destSet_) return false;
    int sent = ::sendto(sock_, reinterpret_cast<const char*>(data), len, 0,
                        reinterpret_cast<sockaddr*>(&destAddr_), sizeof(destAddr_));
    return sent == len;
}

bool UdpSocket::send(const uint8_t* data, int len, const std::string& ip, int port) {
    if (sock_ == INVALID_SOCK) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    int sent = ::sendto(sock_, reinterpret_cast<const char*>(data), len, 0,
                        reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == len;
}

void UdpSocket::closeSocket() {
    if (sock_ != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(sock_);
#else
        ::close(sock_);
#endif
        sock_ = INVALID_SOCK;
    }
}

bool UdpSocket::isValid() const {
    return sock_ != INVALID_SOCK;
}

// ---------------------------------------------------------------------------
// MessageSerializer
// ---------------------------------------------------------------------------

std::vector<uint8_t> MessageSerializer::serialize(const SPDetectionMessage& msg) {
    size_t sz = sizeof(uint32_t) * 3 + sizeof(uint64_t) +
                msg.numDetections * sizeof(Detection);
    std::vector<uint8_t> buf(sz);
    uint8_t* p = buf.data();

    std::memcpy(p, &msg.messageId, 4); p += 4;
    std::memcpy(p, &msg.dwellCount, 4); p += 4;
    std::memcpy(p, &msg.timestamp, 8); p += 8;
    std::memcpy(p, &msg.numDetections, 4); p += 4;
    for (uint32_t i = 0; i < msg.numDetections; ++i) {
        std::memcpy(p, &msg.detections[i], sizeof(Detection));
        p += sizeof(Detection);
    }
    return buf;
}

bool MessageSerializer::deserialize(const uint8_t* data, int len, SPDetectionMessage& msg) {
    if (len < 20) return false; // minimum header size

    const uint8_t* p = data;
    std::memcpy(&msg.messageId, p, 4); p += 4;
    std::memcpy(&msg.dwellCount, p, 4); p += 4;
    std::memcpy(&msg.timestamp, p, 8); p += 8;
    std::memcpy(&msg.numDetections, p, 4); p += 4;

    int remaining = len - 20;
    int expected = static_cast<int>(msg.numDetections * sizeof(Detection));
    if (remaining < expected) return false;

    msg.detections.resize(msg.numDetections);
    for (uint32_t i = 0; i < msg.numDetections; ++i) {
        std::memcpy(&msg.detections[i], p, sizeof(Detection));
        p += sizeof(Detection);
    }
    return true;
}

std::vector<uint8_t> MessageSerializer::serialize(const TrackUpdateMessage& msg) {
    std::vector<uint8_t> buf(sizeof(TrackUpdateMessage));
    std::memcpy(buf.data(), &msg, sizeof(TrackUpdateMessage));
    return buf;
}

bool MessageSerializer::deserialize(const uint8_t* data, int len, TrackUpdateMessage& msg) {
    if (len < static_cast<int>(sizeof(TrackUpdateMessage))) return false;
    std::memcpy(&msg, data, sizeof(TrackUpdateMessage));
    return true;
}

std::vector<uint8_t> MessageSerializer::serializeTrackTable(
    const std::vector<TrackUpdateMessage>& tracks, uint64_t timestamp) {
    uint32_t msgId = MSG_ID_TRACK_TABLE;
    uint32_t numTracks = static_cast<uint32_t>(tracks.size());
    size_t sz = sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t) +
                numTracks * sizeof(TrackUpdateMessage);
    std::vector<uint8_t> buf(sz);
    uint8_t* p = buf.data();

    std::memcpy(p, &msgId, 4); p += 4;
    std::memcpy(p, &timestamp, 8); p += 8;
    std::memcpy(p, &numTracks, 4); p += 4;
    for (auto& t : tracks) {
        std::memcpy(p, &t, sizeof(TrackUpdateMessage));
        p += sizeof(TrackUpdateMessage);
    }
    return buf;
}

bool MessageSerializer::deserializeTrackTable(
    const uint8_t* data, int len,
    std::vector<TrackUpdateMessage>& tracks, uint64_t& timestamp) {
    if (len < 16) return false;

    const uint8_t* p = data;
    uint32_t msgId;
    std::memcpy(&msgId, p, 4); p += 4;
    if (msgId != MSG_ID_TRACK_TABLE) return false;

    std::memcpy(&timestamp, p, 8); p += 8;
    uint32_t numTracks;
    std::memcpy(&numTracks, p, 4); p += 4;

    int remaining = len - 16;
    int expected = static_cast<int>(numTracks * sizeof(TrackUpdateMessage));
    if (remaining < expected) return false;

    tracks.resize(numTracks);
    for (uint32_t i = 0; i < numTracks; ++i) {
        std::memcpy(&tracks[i], p, sizeof(TrackUpdateMessage));
        p += sizeof(TrackUpdateMessage);
    }
    return true;
}

} // namespace cuas
