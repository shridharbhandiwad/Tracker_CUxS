#include "association/gnn_associator.h"
#include "track_management/track.h"
#include "common/matrix_ops.h"
#include "common/logger.h"
#include <algorithm>
#include <set>
#include <limits>
#include <numeric>

namespace cuas {

GNNAssociator::GNNAssociator(const GNNConfig& cfg, double gatingThreshold)
    : config_(cfg), gatingThreshold_(gatingThreshold) {}

std::vector<int> GNNAssociator::hungarianAssignment(
    const std::vector<std::vector<double>>& costMatrix,
    int numTracks, int numClusters) const {

    // Simplified auction/greedy assignment for rectangular cost matrix
    // Full Hungarian is O(n^3); this greedy approach is acceptable for typical track counts
    int n = std::max(numTracks, numClusters);
    const double INF = 1e30;

    // Pad cost matrix to square
    std::vector<std::vector<double>> C(n, std::vector<double>(n, INF));
    for (int i = 0; i < numTracks; ++i)
        for (int j = 0; j < numClusters; ++j)
            C[i][j] = costMatrix[i][j];

    // Kuhn-Munkres (simplified row/column reduction + assignment)
    // Step 1: Row reduction
    for (int i = 0; i < n; ++i) {
        double minVal = *std::min_element(C[i].begin(), C[i].end());
        if (minVal < INF)
            for (int j = 0; j < n; ++j) C[i][j] -= minVal;
    }

    // Step 2: Column reduction
    for (int j = 0; j < n; ++j) {
        double minVal = INF;
        for (int i = 0; i < n; ++i) minVal = std::min(minVal, C[i][j]);
        if (minVal < INF)
            for (int i = 0; i < n; ++i) C[i][j] -= minVal;
    }

    // Step 3: Greedy assignment on reduced cost
    std::vector<int> assignment(numTracks, -1);
    std::vector<bool> colUsed(n, false);

    // Multiple passes for better assignment
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < numTracks; ++i) {
            if (assignment[i] != -1) continue;
            double bestCost = INF;
            int bestJ = -1;
            for (int j = 0; j < numClusters; ++j) {
                if (colUsed[j]) continue;
                if (C[i][j] < bestCost) {
                    bestCost = C[i][j];
                    bestJ = j;
                }
            }
            if (bestJ >= 0 && costMatrix[i][bestJ] < config_.costThreshold) {
                assignment[i] = bestJ;
                colUsed[bestJ] = true;
            }
        }
    }

    return assignment;
}

AssociationOutput GNNAssociator::associate(
    const std::vector<Track>& tracks,
    const std::vector<Cluster>& clusters,
    const IMMFilter& imm,
    const MeasMatrix& R) {

    int nTracks   = static_cast<int>(tracks.size());
    int nClusters = static_cast<int>(clusters.size());
    const double INF = 1e30;

    MeasStateMatrix H = imm.getMeasurementMatrix();

    // Build cost matrix based on Mahalanobis distance
    std::vector<std::vector<double>> costMatrix(nTracks, std::vector<double>(nClusters, INF));

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
                costMatrix[t][c] = d;
            }
        }
    }

    // Run assignment
    std::vector<int> assignment = hungarianAssignment(costMatrix, nTracks, nClusters);

    AssociationOutput out;
    std::set<int> matchedClusters;

    for (int t = 0; t < nTracks; ++t) {
        if (assignment[t] >= 0 && assignment[t] < nClusters) {
            AssociationResult res;
            res.trackIndex   = t;
            res.clusterIndex = assignment[t];
            res.distance     = costMatrix[t][assignment[t]];
            out.matched.push_back(res);
            matchedClusters.insert(assignment[t]);
        } else {
            out.unmatchedTracks.push_back(t);
        }
    }

    for (int c = 0; c < nClusters; ++c)
        if (!matchedClusters.count(c)) out.unmatchedClusters.push_back(c);

    LOG_DEBUG("GNN", "Matched: %zu, Unmatched tracks: %zu, Unmatched clusters: %zu",
              out.matched.size(), out.unmatchedTracks.size(), out.unmatchedClusters.size());

    return out;
}

} // namespace cuas
