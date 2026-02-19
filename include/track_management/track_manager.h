#pragma once

#include "track.h"
#include "track_initiator.h"
#include "common/config.h"
#include "common/logger.h"
#include "prediction/imm_filter.h"
#include "association/association_engine.h"
#include "clustering/cluster_engine.h"
#include "preprocessing/preprocessor.h"
#include <vector>
#include <memory>

namespace cuas {

class TrackManager {
public:
    explicit TrackManager(const TrackerConfig& cfg);

    void processDwell(const SPDetectionMessage& msg);

    const std::vector<std::unique_ptr<Track>>& tracks() const { return tracks_; }
    std::vector<TrackUpdateMessage> getTrackUpdates() const;

    uint32_t numActiveTracks() const;
    uint32_t numConfirmedTracks() const;

    BinaryLogger& logger() { return logger_; }

private:
    void predict(double dt);
    void associate(const std::vector<Cluster>& clusters);
    void updateMatchedTracks(const AssociationOutput& assoc,
                             const std::vector<Cluster>& clusters);
    void handleUnmatchedTracks(const std::vector<int>& unmatchedTracks);
    void initiateNewTracks(const std::vector<Cluster>& clusters,
                           const std::vector<int>& unmatchedClusters,
                           Timestamp ts, uint32_t dwellCount);
    void maintainTracks();
    void deleteTracks();
    void classifyTracks();

    TrackerConfig config_;
    std::unique_ptr<Preprocessor> preprocessor_;
    std::unique_ptr<ClusterEngine> clusterEngine_;
    std::unique_ptr<IMMFilter> immFilter_;
    std::unique_ptr<AssociationEngine> associationEngine_;
    std::unique_ptr<TrackInitiator> trackInitiator_;

    std::vector<std::unique_ptr<Track>> tracks_;
    BinaryLogger logger_;
    MeasMatrix measurementNoise_;

    Timestamp lastDwellTime_ = 0;
    uint32_t  dwellCount_    = 0;
};

} // namespace cuas
