#pragma once

#include "common/types.h"
#include "common/udp_socket.h"
#include "common/config.h"
#include <functional>
#include <atomic>
#include <thread>

namespace cuas {

class DetectionReceiver {
public:
    using Callback = std::function<void(const SPDetectionMessage&)>;

    explicit DetectionReceiver(const NetworkConfig& cfg);
    ~DetectionReceiver();

    bool start(Callback cb);
    void stop();
    bool isRunning() const { return running_.load(); }

    uint64_t totalMessagesReceived() const { return msgCount_.load(); }
    uint64_t totalDetectionsReceived() const { return detCount_.load(); }

private:
    void receiveLoop();

    NetworkConfig config_;
    UdpSocket     socket_;
    Callback      callback_;
    std::atomic<bool> running_{false};
    std::thread   thread_;
    std::atomic<uint64_t> msgCount_{0};
    std::atomic<uint64_t> detCount_{0};
};

} // namespace cuas
