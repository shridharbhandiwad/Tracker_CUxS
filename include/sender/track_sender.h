#pragma once

#include "common/types.h"
#include "common/udp_socket.h"
#include "common/config.h"
#include <vector>
#include <atomic>

namespace cuas {

class TrackSender {
public:
    explicit TrackSender(const NetworkConfig& netCfg, const DisplayConfig& dispCfg);
    ~TrackSender();

    bool init();
    void sendTrackUpdates(const std::vector<TrackUpdateMessage>& updates, Timestamp ts);
    void close();

    uint64_t totalMessagesSent() const { return msgCount_.load(); }

private:
    NetworkConfig netConfig_;
    DisplayConfig dispConfig_;
    UdpSocket     socket_;
    std::atomic<uint64_t> msgCount_{0};
};

} // namespace cuas
