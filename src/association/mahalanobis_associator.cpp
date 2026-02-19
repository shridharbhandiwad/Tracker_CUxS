#include "association/mahalanobis_associator.h"
#include "track_management/track.h"
#include "common/matrix_ops.h"
#include "common/logger.h"
#include <algorithm>
#include <set>

namespace cuas {

MahalanobisAssociator::MahalanobisAssociator(const MahalanobisConfig& cfg,
                                               double gatingThreshold)
    : config_(cfg), gatingThreshold_(gatingThreshold) {}

AssociationOutput MahalanobisAssociator::associate(
    const std::vector<Track>& tracks,
    const std::vector<Cluster>& clusters,
    const IMMFilter& imm,
    const MeasMatrix& R) {

    int nTracks   = static_cast<int>(tracks.size());
    int nClusters = static_cast<int>(clusters.size());

    MeasStateMatrix H = imm.getMeasurementMatrix();
    AssociationOutput out;
    std::set<int> matchedTracks, matchedClusters;

    // Compute all distances
    struct Candidate {
        int trackIdx, clusterIdx;
        double distance;
    };
    std::vector<Candidate> candidates;

    for (int t = 0; t < nTracks; ++t) {
        const auto& state = tracks[t].immState();
        MeasMatrix S = mat::measAddMat(mat::hpht(H, state.mergedCovariance), R);
        MeasMatrix Sinv;
        if (!mat::invertMeas(S, Sinv)) continue;

        MeasVector zPred = mat::measFromState(H, state.mergedState);

        for (int c = 0; c < nClusters; ++c) {
            MeasVector z = {clusters[c].cartesian.x,
                           clusters[c].cartesian.y,
                           clusters[c].cartesian.z};
            MeasVector innov = mat::measSub(z, zPred);
            double d = mat::mahalanobisDistance(innov, Sinv);

            if (d <= gatingThreshold_) {
                candidates.push_back({t, c, d});
            }
        }
    }

    // Greedy nearest-neighbor: sort by distance and assign
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.distance < b.distance;
              });

    for (const auto& cand : candidates) {
        if (matchedTracks.count(cand.trackIdx) ||
            matchedClusters.count(cand.clusterIdx)) continue;

        if (cand.distance <= config_.distanceThreshold) {
            AssociationResult res;
            res.trackIndex   = cand.trackIdx;
            res.clusterIndex = cand.clusterIdx;
            res.distance     = cand.distance;
            out.matched.push_back(res);
            matchedTracks.insert(cand.trackIdx);
            matchedClusters.insert(cand.clusterIdx);
        }
    }

    for (int t = 0; t < nTracks; ++t)
        if (!matchedTracks.count(t)) out.unmatchedTracks.push_back(t);
    for (int c = 0; c < nClusters; ++c)
        if (!matchedClusters.count(c)) out.unmatchedClusters.push_back(c);

    LOG_DEBUG("Mahalanobis", "Matched: %zu, Unmatched tracks: %zu, Unmatched clusters: %zu",
              out.matched.size(), out.unmatchedTracks.size(), out.unmatchedClusters.size());

    return out;
}

} // namespace cuas
