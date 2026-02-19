#include "receiver/detection_receiver.h"
#include "common/logger.h"
#include "common/constants.h"

namespace cuas {

DetectionReceiver::DetectionReceiver(const NetworkConfig& cfg)
    : config_(cfg) {}

DetectionReceiver::~DetectionReceiver() {
    stop();
}

bool DetectionReceiver::start(Callback cb) {
    if (running_.load()) return false;

    callback_ = std::move(cb);

    if (!socket_.bindSocket(config_.receiverIp, config_.receiverPort)) {
        LOG_ERROR("Receiver", "Failed to bind receiver socket");
        return false;
    }

    socket_.setReceiveTimeout(200);
    socket_.setBufferSize(config_.receiveBufferSize, config_.sendBufferSize);

    running_.store(true);
    thread_ = std::thread(&DetectionReceiver::receiveLoop, this);

    LOG_INFO("Receiver", "Detection receiver started on %s:%d",
             config_.receiverIp.c_str(), config_.receiverPort);
    return true;
}

void DetectionReceiver::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    socket_.closeSocket();
    LOG_INFO("Receiver", "Detection receiver stopped. Total msgs: %lu, dets: %lu",
             static_cast<unsigned long>(msgCount_.load()),
             static_cast<unsigned long>(detCount_.load()));
}

void DetectionReceiver::receiveLoop() {
    std::vector<uint8_t> buffer(config_.receiveBufferSize);

    while (running_.load()) {
        std::string senderIp;
        int senderPort = 0;
        int n = socket_.receive(buffer.data(), static_cast<int>(buffer.size()),
                                senderIp, senderPort);

        if (n <= 0) continue;

        SPDetectionMessage msg;
        if (!MessageSerializer::deserialize(buffer.data(), n, msg)) {
            LOG_WARN("Receiver", "Failed to deserialize message (%d bytes)", n);
            continue;
        }

        if (msg.messageId != MSG_ID_SP_DETECTION) {
            LOG_WARN("Receiver", "Unknown message ID: 0x%04X", msg.messageId);
            continue;
        }

        msgCount_.fetch_add(1);
        detCount_.fetch_add(msg.numDetections);

        LOG_DEBUG("Receiver", "Dwell %u: %u detections from %s:%d",
                  msg.dwellCount, msg.numDetections,
                  senderIp.c_str(), senderPort);

        if (callback_) {
            callback_(msg);
        }
    }
}

} // namespace cuas
