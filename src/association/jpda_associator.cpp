#include "association/jpda_associator.h"
#include "track_management/track.h"
#include "common/matrix_ops.h"
#include "common/logger.h"
#include <cmath>
#include <algorithm>
#include <set>

namespace cuas {

JPDAAssociator::JPDAAssociator(const JPDAConfig& cfg, double gatingThreshold)
    : config_(cfg), gatingThreshold_(gatingThreshold) {}

std::vector<JPDAAssociator::JPDAWeights> JPDAAssociator::computeWeights(
    const std::vector<Track>& tracks,
    const std::vector<Cluster>& clusters,
    const IMMFilter& imm,
    const MeasMatrix& R) const {

    int nTracks   = static_cast<int>(tracks.size());
    int nClusters = static_cast<int>(clusters.size());

    MeasStateMatrix H = imm.getMeasurementMatrix();
    std::vector<JPDAWeights> allWeights;

    for (int t = 0; t < nTracks; ++t) {
        const auto& state = tracks[t].immState();
        MeasMatrix S = mat::measAddMat(mat::hpht(H, state.mergedCovariance), R);
        MeasMatrix Sinv;
        if (!mat::invertMeas(S, Sinv)) {
            JPDAWeights w;
            w.trackIndex = t;
            w.betaZero = 1.0;
            allWeights.push_back(w);
            continue;
        }

        double detS = mat::det3x3(S);
        MeasVector zPred = mat::measFromState(H, state.mergedState);

        JPDAWeights w;
        w.trackIndex = t;

        double pd = config_.detectionProbability;
        double lambda = config_.clutterDensity;

        // Compute likelihoods for each gated measurement
        std::vector<std::pair<int, double>> gatedMeas;
        for (int c = 0; c < nClusters; ++c) {
            MeasVector z = {clusters[c].cartesian.x,
                           clusters[c].cartesian.y,
                           clusters[c].cartesian.z};
            MeasVector innov = mat::measSub(z, zPred);
            double d = mat::mahalanobisDistance(innov, Sinv);

            if (d <= config_.gateSize) {
                double lik = std::exp(-0.5 * d) /
                             std::sqrt(std::pow(2.0 * 3.14159265, MEAS_DIM) * std::abs(detS));
                gatedMeas.push_back({c, lik});
            }
        }

        if (gatedMeas.empty()) {
            w.betaZero = 1.0;
            allWeights.push_back(w);
            continue;
        }

        // Beta_0 (probability of no valid detection)
        double sumLik = 0.0;
        for (auto& [idx, lik] : gatedMeas) {
            sumLik += pd * lik;
        }
        double denominator = (1.0 - pd) * lambda + sumLik;

        if (denominator < 1e-30) {
            w.betaZero = 1.0;
        } else {
            w.betaZero = (1.0 - pd) * lambda / denominator;
            for (auto& [idx, lik] : gatedMeas) {
                double beta = pd * lik / denominator;
                w.clusterWeights.push_back({idx, beta});
            }
        }

        allWeights.push_back(w);
    }

    return allWeights;
}

AssociationOutput JPDAAssociator::associate(
    const std::vector<Track>& tracks,
    const std::vector<Cluster>& clusters,
    const IMMFilter& imm,
    const MeasMatrix& R) {

    auto weights = computeWeights(tracks, clusters, imm, R);

    AssociationOutput out;
    std::set<int> matchedClusters;

    // For JPDA, each track gets its "best" measurement, but the real power
    // is in the weighted update. We report the strongest association for
    // the pipeline's matched/unmatched classification.
    for (const auto& w : weights) {
        if (w.clusterWeights.empty() || w.betaZero > 0.5) {
            out.unmatchedTracks.push_back(w.trackIndex);
            continue;
        }

        // Select measurement with highest beta
        int bestCluster = -1;
        double bestBeta = 0.0;
        for (const auto& [cIdx, beta] : w.clusterWeights) {
            if (beta > bestBeta) {
                bestBeta = beta;
                bestCluster = cIdx;
            }
        }

        if (bestCluster >= 0) {
            AssociationResult res;
            res.trackIndex   = w.trackIndex;
            res.clusterIndex = bestCluster;
            res.distance     = 1.0 - bestBeta; // Use complement as pseudo-distance
            out.matched.push_back(res);
            matchedClusters.insert(bestCluster);
        } else {
            out.unmatchedTracks.push_back(w.trackIndex);
        }
    }

    int nClusters = static_cast<int>(clusters.size());
    for (int c = 0; c < nClusters; ++c)
        if (!matchedClusters.count(c)) out.unmatchedClusters.push_back(c);

    LOG_DEBUG("JPDA", "Matched: %zu, Unmatched tracks: %zu, Unmatched clusters: %zu",
              out.matched.size(), out.unmatchedTracks.size(), out.unmatchedClusters.size());

    return out;
}

} // namespace cuas
