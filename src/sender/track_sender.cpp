#include "sender/track_sender.h"
#include "common/logger.h"
#include "common/constants.h"

namespace cuas {

TrackSender::TrackSender(const NetworkConfig& netCfg, const DisplayConfig& dispCfg)
    : netConfig_(netCfg), dispConfig_(dispCfg) {}

TrackSender::~TrackSender() {
    close();
}

bool TrackSender::init() {
    if (!socket_.setDestination(netConfig_.senderIp, netConfig_.senderPort)) {
        LOG_ERROR("TrackSender", "Failed to set destination %s:%d",
                  netConfig_.senderIp.c_str(), netConfig_.senderPort);
        return false;
    }

    socket_.setBufferSize(netConfig_.receiveBufferSize, netConfig_.sendBufferSize);

    LOG_INFO("TrackSender", "Initialized, sending to %s:%d",
             netConfig_.senderIp.c_str(), netConfig_.senderPort);
    return true;
}

void TrackSender::sendTrackUpdates(const std::vector<TrackUpdateMessage>& updates,
                                    Timestamp ts) {
    if (updates.empty()) return;

    // Filter out deleted tracks if configured
    std::vector<TrackUpdateMessage> toSend;
    for (const auto& u : updates) {
        if (!dispConfig_.sendDeletedTracks && u.status == TrackStatus::Deleted) continue;
        toSend.push_back(u);
    }

    if (toSend.empty()) return;

    auto data = MessageSerializer::serializeTrackTable(toSend, ts);

    if (socket_.send(data.data(), static_cast<int>(data.size()))) {
        msgCount_.fetch_add(1);
        LOG_DEBUG("TrackSender", "Sent %zu track updates (%zu bytes) TrackId %zu",
                  toSend.size(), data.size(), toSend[0].trackId);
    } else {
        LOG_WARN("TrackSender", "Failed to send track updates");
    }
}

void TrackSender::sendRawDetections(const SPDetectionMessage& msg) {
    auto data = MessageSerializer::serialize(msg);
    if (!socket_.send(data.data(), static_cast<int>(data.size()))) {
        LOG_WARN("TrackSender", "Failed to forward raw detection message (dwell %u)",
                 msg.dwellCount);
    } else {
        LOG_DEBUG("TrackSender", "Forwarded dwell %u: %u detections (%zu bytes)",
                  msg.dwellCount, msg.numDetections, data.size());
    }
}

void TrackSender::sendClusterTable(const std::vector<ClusterWire>& clusters,
                                    Timestamp ts, uint32_t dwellCount) {
    if (clusters.empty()) return;
    auto data = MessageSerializer::serializeClusterTable(clusters, ts, dwellCount);
    if (!socket_.send(data.data(), static_cast<int>(data.size()))) {
        LOG_WARN("TrackSender", "Failed to send cluster table (dwell %u)", dwellCount);
    } else {
        LOG_DEBUG("TrackSender", "Sent %zu clusters (%zu bytes)", clusters.size(), data.size());
    }
}

void TrackSender::sendAssocTable(const std::vector<AssocEntryWire>& entries, Timestamp ts) {
    if (entries.empty()) return;
    auto data = MessageSerializer::serializeAssocTable(entries, ts);
    if (!socket_.send(data.data(), static_cast<int>(data.size()))) {
        LOG_WARN("TrackSender", "Failed to send assoc table");
    } else {
        LOG_DEBUG("TrackSender", "Sent %zu assoc entries (%zu bytes)", entries.size(), data.size());
    }
}

void TrackSender::sendPredictedTable(const std::vector<PredictedEntryWire>& entries, Timestamp ts) {
    if (entries.empty()) return;
    auto data = MessageSerializer::serializePredictedTable(entries, ts);
    if (!socket_.send(data.data(), static_cast<int>(data.size()))) {
        LOG_WARN("TrackSender", "Failed to send predicted table");
    } else {
        LOG_DEBUG("TrackSender", "Sent %zu predicted entries (%zu bytes)", entries.size(), data.size());
    }
}

void TrackSender::close() {
    socket_.closeSocket();
    LOG_INFO("TrackSender", "Closed. Total messages sent: %lu",
             static_cast<unsigned long>(msgCount_.load()));
}

} // namespace cuas
