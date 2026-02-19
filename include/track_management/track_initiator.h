#pragma once

#include "common/types.h"
#include "common/config.h"
#include "track.h"
#include <vector>
#include <deque>
#include <memory>

namespace cuas {

struct TentativeDetection {
    Cluster cluster;
    Timestamp timestamp;
    uint32_t dwellCount;
};

struct InitiationCandidate {
    std::deque<TentativeDetection> history;
    uint32_t hits  = 0;
    uint32_t total = 0;
    bool promoted  = false;
};

class TrackInitiator {
public:
    TrackInitiator(const InitiationConfig& initCfg,
                   const InitialCovarianceConfig& covCfg,
                   const PredictionConfig& predCfg);

    std::vector<std::unique_ptr<Track>> processCandidates(
        const std::vector<Cluster>& unmatched,
        Timestamp ts, uint32_t dwellCount);

    void purgeStaleCandidates(uint32_t currentDwell);
    size_t numCandidates() const { return candidates_.size(); }

private:
    StateVector initState(const Cluster& c) const;
    StateVector initStateWithVelocity(const Cluster& c0, const Cluster& c1, double dt) const;
    StateMatrix initCovariance() const;
    uint32_t nextTrackId();

    InitiationConfig       initCfg_;
    InitialCovarianceConfig covCfg_;
    PredictionConfig       predCfg_;

    std::vector<InitiationCandidate> candidates_;
    uint32_t nextId_ = 1;
};

} // namespace cuas
