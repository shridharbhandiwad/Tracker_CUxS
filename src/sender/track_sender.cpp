#include "sender/track_sender.h"
#include "common/constants.h"
#include "common/logger.h"

namespace cuas {

TrackSender::TrackSender(CuasDdsParticipant& participant,
                         const DisplayConfig& dispCfg)
    : dispConfig_(dispCfg) {
    writerTrackTable_     = participant.makeWriter<CounterUAS::TrackTableMessage>(
                                TOPIC_TRACK_TABLE);
    writerSPDetection_    = participant.makeWriter<CounterUAS::SPDetectionMessage>(
                                TOPIC_SP_DETECTION);
    writerClusterTable_   = participant.makeWriter<CounterUAS::ClusterTableMessage>(
                                TOPIC_CLUSTER_TABLE);
    writerAssocTable_     = participant.makeWriter<CounterUAS::AssocTableMessage>(
                                TOPIC_ASSOC_TABLE);
    writerPredictedTable_ = participant.makeWriter<CounterUAS::PredictedTableMessage>(
                                TOPIC_PREDICTED_TABLE);

    LOG_INFO("TrackSender", "DDS publishers created on topics: %s, %s, %s, %s, %s",
             TOPIC_TRACK_TABLE, TOPIC_SP_DETECTION,
             TOPIC_CLUSTER_TABLE, TOPIC_ASSOC_TABLE, TOPIC_PREDICTED_TABLE);
}

void TrackSender::sendTrackUpdates(
    const std::vector<CounterUAS::TrackUpdateMessage>& updates,
    Timestamp ts) {

    if (updates.empty()) return;

    // Build the TrackTableMessage from the vector, filtering deleted if needed.
    CounterUAS::TrackTableMessage tableMsg;
    tableMsg.messageId(MSG_ID_TRACK_TABLE);
    tableMsg.timestamp(ts);

    std::vector<CounterUAS::TrackUpdateMessage> toSend;
    for (const auto& u : updates) {
        if (!dispConfig_.sendDeletedTracks &&
            u.status() == CounterUAS::TRACK_DELETED) continue;
        toSend.push_back(u);
    }
    if (toSend.empty()) return;

    tableMsg.numTracks(static_cast<uint32_t>(toSend.size()));
    tableMsg.tracks(toSend);

    writerTrackTable_->write(&tableMsg);
    msgCount_.fetch_add(1);

    LOG_DEBUG("TrackSender", "Published %zu track updates on '%s'",
              toSend.size(), TOPIC_TRACK_TABLE);
}

void TrackSender::sendRawDetections(const SPDetectionMessage& msg) {
    // Convert internal type to IDL wire type and re-publish.
    CounterUAS::SPDetectionMessage idlMsg;
    idlMsg.messageId(msg.messageId);
    idlMsg.dwellCount(msg.dwellCount);
    idlMsg.timestamp(msg.timestamp);
    idlMsg.numDetections(msg.numDetections);

    std::vector<CounterUAS::DetectionData> dets;
    dets.reserve(msg.detections.size());
    for (const auto& d : msg.detections)
        dets.push_back(toIDL(d));
    idlMsg.detections(dets);

    writerSPDetection_->write(&idlMsg);

    LOG_DEBUG("TrackSender", "Re-published dwell %u (%u detections) on '%s'",
              msg.dwellCount, msg.numDetections, TOPIC_SP_DETECTION);
}

void TrackSender::sendClusterTable(
    const std::vector<CounterUAS::ClusterData>& clusters,
    Timestamp ts, uint32_t dwellCount) {

    if (clusters.empty()) return;

    CounterUAS::ClusterTableMessage msg;
    msg.messageId(MSG_ID_CLUSTER_TABLE);
    msg.timestamp(ts);
    msg.dwellCount(dwellCount);
    msg.numClusters(static_cast<uint32_t>(clusters.size()));
    msg.clusters(clusters);

    writerClusterTable_->write(&msg);
    LOG_DEBUG("TrackSender", "Published %zu clusters on '%s'",
              clusters.size(), TOPIC_CLUSTER_TABLE);
}

void TrackSender::sendAssocTable(
    const std::vector<CounterUAS::AssocEntry>& entries,
    Timestamp ts) {

    if (entries.empty()) return;

    CounterUAS::AssocTableMessage msg;
    msg.messageId(MSG_ID_ASSOC_TABLE);
    msg.timestamp(ts);
    msg.numEntries(static_cast<uint32_t>(entries.size()));
    msg.entries(entries);

    writerAssocTable_->write(&msg);
    LOG_DEBUG("TrackSender", "Published %zu assoc entries on '%s'",
              entries.size(), TOPIC_ASSOC_TABLE);
}

void TrackSender::sendPredictedTable(
    const std::vector<CounterUAS::PredictedEntry>& entries,
    Timestamp ts) {

    if (entries.empty()) return;

    CounterUAS::PredictedTableMessage msg;
    msg.messageId(MSG_ID_PREDICTED_TABLE);
    msg.timestamp(ts);
    msg.numEntries(static_cast<uint32_t>(entries.size()));
    msg.entries(entries);

    writerPredictedTable_->write(&msg);
    LOG_DEBUG("TrackSender", "Published %zu predicted entries on '%s'",
              entries.size(), TOPIC_PREDICTED_TABLE);
}

} // namespace cuas
