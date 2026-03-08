#pragma once

/*
 * TrackSender — DDS publishers for all Tracker-to-Display topics.
 *
 * Publishes five DDS topics using IDL-generated types:
 *   "SPDetection"    — forwards raw detections (re-published for display)
 *   "TrackTable"     — batch of confirmed/active track updates
 *   "ClusterTable"   — post-clustering debug output
 *   "AssocTable"     — association/gating debug output
 *   "PredictedTable" — post-prediction debug output
 *
 * Internal types are converted to IDL wire types at this boundary.
 * No hand-written serialization code is needed.
 */

#include "common/types.h"
#include "common/dds_participant.h"
#include "common/config.h"

#include <fastdds/dds/publisher/DataWriter.hpp>

#include <vector>
#include <atomic>

namespace cuas {

class TrackSender {
public:
    TrackSender(CuasDdsParticipant& participant, const DisplayConfig& dispCfg);
    ~TrackSender() = default;

    TrackSender(const TrackSender&)            = delete;
    TrackSender& operator=(const TrackSender&) = delete;

    // Publishes a TrackTableMessage on "TrackTable" topic.
    // Deleted tracks are filtered out unless dispCfg_.sendDeletedTracks.
    void sendTrackUpdates(
        const std::vector<CounterUAS::TrackUpdateMessage>& updates,
        Timestamp ts);

    // Re-publishes the raw detection dwell on "SPDetection" for display.
    void sendRawDetections(const SPDetectionMessage& msg);

    // Debug pipeline topics.
    void sendClusterTable(
        const std::vector<CounterUAS::ClusterData>& clusters,
        Timestamp ts, uint32_t dwellCount);

    void sendAssocTable(
        const std::vector<CounterUAS::AssocEntry>& entries,
        Timestamp ts);

    void sendPredictedTable(
        const std::vector<CounterUAS::PredictedEntry>& entries,
        Timestamp ts);

    uint64_t totalMessagesSent() const { return msgCount_.load(); }

private:
    DisplayConfig dispConfig_;

    eprosima::fastdds::dds::DataWriter* writerTrackTable_     = nullptr;
    eprosima::fastdds::dds::DataWriter* writerSPDetection_    = nullptr;
    eprosima::fastdds::dds::DataWriter* writerClusterTable_   = nullptr;
    eprosima::fastdds::dds::DataWriter* writerAssocTable_     = nullptr;
    eprosima::fastdds::dds::DataWriter* writerPredictedTable_ = nullptr;

    std::atomic<uint64_t> msgCount_{0};
};

} // namespace cuas
