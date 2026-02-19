#include "track_management/track_initiator.h"
#include "common/logger.h"
#include <cmath>
#include <algorithm>

namespace cuas {

TrackInitiator::TrackInitiator(const InitiationConfig& initCfg,
                                const InitialCovarianceConfig& covCfg,
                                const PredictionConfig& predCfg)
    : initCfg_(initCfg), covCfg_(covCfg), predCfg_(predCfg) {}

uint32_t TrackInitiator::nextTrackId() {
    return nextId_++;
}

StateVector TrackInitiator::initState(const Cluster& c) const {
    StateVector x = stateZero();
    x[0] = c.cartesian.x;
    x[3] = c.cartesian.y;
    x[6] = c.cartesian.z;
    return x;
}

StateVector TrackInitiator::initStateWithVelocity(const Cluster& c0,
                                                    const Cluster& c1,
                                                    double dt) const {
    StateVector x = stateZero();
    x[0] = c1.cartesian.x;
    x[3] = c1.cartesian.y;
    x[6] = c1.cartesian.z;
    if (dt > 1e-6) {
        x[1] = (c1.cartesian.x - c0.cartesian.x) / dt;
        x[4] = (c1.cartesian.y - c0.cartesian.y) / dt;
        x[7] = (c1.cartesian.z - c0.cartesian.z) / dt;
    }
    return x;
}

StateMatrix TrackInitiator::initCovariance() const {
    StateMatrix P = matZero();
    double sp2 = covCfg_.positionStd * covCfg_.positionStd;
    double sv2 = covCfg_.velocityStd * covCfg_.velocityStd;
    double sa2 = covCfg_.accelerationStd * covCfg_.accelerationStd;

    for (int axis = 0; axis < 3; ++axis) {
        P[axis * 3][axis * 3]         = sp2;
        P[axis * 3 + 1][axis * 3 + 1] = sv2;
        P[axis * 3 + 2][axis * 3 + 2] = sa2;
    }
    return P;
}

std::vector<std::unique_ptr<Track>> TrackInitiator::processCandidates(
    const std::vector<Cluster>& unmatched,
    Timestamp ts, uint32_t dwellCount) {

    std::vector<std::unique_ptr<Track>> newTracks;

    for (const auto& cluster : unmatched) {
        if (cluster.range > initCfg_.maxInitiationRange) continue;

        // Try to match with existing candidates
        bool matched = false;
        for (auto& cand : candidates_) {
            if (cand.promoted) continue;
            if (cand.history.empty()) continue;

            const auto& last = cand.history.back();
            double dr = std::abs(cluster.range - last.cluster.range);
            double da = std::abs(cluster.azimuth - last.cluster.azimuth);
            double de = std::abs(cluster.elevation - last.cluster.elevation);

            double dt = (ts - last.timestamp) * 1e-6; // seconds
            double maxRange = initCfg_.velocityGate * dt + 100.0;

            if (dr < maxRange && da < 0.1 && de < 0.1) {
                TentativeDetection td{cluster, ts, dwellCount};
                cand.history.push_back(td);
                cand.hits++;
                cand.total++;

                // M-of-N check
                if (cand.hits >= static_cast<uint32_t>(initCfg_.m) &&
                    cand.total <= static_cast<uint32_t>(initCfg_.n)) {
                    cand.promoted = true;

                    StateVector x;
                    if (cand.history.size() >= 2) {
                        auto& h0 = cand.history[cand.history.size() - 2];
                        auto& h1 = cand.history[cand.history.size() - 1];
                        double dtInit = (h1.timestamp - h0.timestamp) * 1e-6;
                        x = initStateWithVelocity(h0.cluster, h1.cluster, dtInit);
                    } else {
                        x = initState(cluster);
                    }

                    StateMatrix P = initCovariance();
                    uint32_t tid = nextTrackId();
                    auto track = std::make_unique<Track>(tid, x, P, predCfg_, ts);

                    LOG_INFO("Initiator", "New track %u at R=%.1f Az=%.3f El=%.3f",
                             tid, cluster.range, cluster.azimuth, cluster.elevation);

                    newTracks.push_back(std::move(track));
                }
                matched = true;
                break;
            }
        }

        if (!matched) {
            InitiationCandidate cand;
            TentativeDetection td{cluster, ts, dwellCount};
            cand.history.push_back(td);
            cand.hits = 1;
            cand.total = 1;
            candidates_.push_back(std::move(cand));
        }
    }

    return newTracks;
}

void TrackInitiator::purgeStaleCandidates(uint32_t currentDwell) {
    candidates_.erase(
        std::remove_if(candidates_.begin(), candidates_.end(),
                       [&](const InitiationCandidate& c) {
                           if (c.promoted) return true;
                           if (c.history.empty()) return true;
                           if (c.total >= static_cast<uint32_t>(initCfg_.n) &&
                               c.hits < static_cast<uint32_t>(initCfg_.m)) return true;
                           uint32_t age = currentDwell - c.history.front().dwellCount;
                           return age > static_cast<uint32_t>(initCfg_.n + 5);
                       }),
        candidates_.end());
}

} // namespace cuas
