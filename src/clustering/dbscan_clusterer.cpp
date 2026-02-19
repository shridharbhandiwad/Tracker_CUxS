#include "clustering/dbscan_clusterer.h"
#include "common/logger.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <map>

namespace cuas {

DBScanClusterer::DBScanClusterer(const DBScanConfig& cfg) : config_(cfg) {}

double DBScanClusterer::distance(const Detection& a, const Detection& b) const {
    double dr = (a.range - b.range) / config_.epsilonRange;
    double da = (a.azimuth - b.azimuth) / config_.epsilonAzimuth;
    double de = (a.elevation - b.elevation) / config_.epsilonElevation;
    return std::sqrt(dr * dr + da * da + de * de);
}

void DBScanClusterer::rangeQuery(const std::vector<Detection>& dets, int idx,
                                  std::vector<int>& neighbors) const {
    neighbors.clear();
    for (int i = 0; i < static_cast<int>(dets.size()); ++i) {
        if (distance(dets[idx], dets[i]) <= 1.0) {
            neighbors.push_back(i);
        }
    }
}

Cluster DBScanClusterer::buildCluster(const std::vector<Detection>& dets,
                                       const std::vector<int>& indices, uint32_t id) const {
    Cluster c;
    c.clusterId = id;
    c.numDetections = static_cast<uint32_t>(indices.size());

    double totalStrength = 0.0;
    double linStrengthSum = 0.0;

    for (int idx : indices) {
        double linStr = std::pow(10.0, dets[idx].strength / 10.0);
        linStrengthSum += linStr;
        c.detectionIndices.push_back(static_cast<uint32_t>(idx));
    }

    // Strength-weighted centroid
    for (int idx : indices) {
        double linStr = std::pow(10.0, dets[idx].strength / 10.0);
        double w = linStr / linStrengthSum;
        c.range     += w * dets[idx].range;
        c.azimuth   += w * dets[idx].azimuth;
        c.elevation += w * dets[idx].elevation;
        c.snr       += w * dets[idx].snr;
        c.rcs       += w * dets[idx].rcs;
        c.microDoppler += w * dets[idx].microDoppler;
        totalStrength += dets[idx].strength;
    }

    c.strength = totalStrength / indices.size();
    return c;
}

std::vector<Cluster> DBScanClusterer::cluster(const std::vector<Detection>& dets) {
    int n = static_cast<int>(dets.size());
    if (n == 0) return {};

    std::vector<int> labels(n, -1);       // -1 = undefined
    static constexpr int NOISE = -2;
    int clusterLabel = 0;

    for (int i = 0; i < n; ++i) {
        if (labels[i] != -1) continue;

        std::vector<int> neighbors;
        rangeQuery(dets, i, neighbors);

        if (static_cast<int>(neighbors.size()) < config_.minPoints) {
            labels[i] = NOISE;
            continue;
        }

        int currentLabel = clusterLabel++;
        labels[i] = currentLabel;

        std::vector<int> seedSet(neighbors.begin(), neighbors.end());
        for (size_t si = 0; si < seedSet.size(); ++si) {
            int q = seedSet[si];
            if (labels[q] == NOISE) {
                labels[q] = currentLabel;
            }
            if (labels[q] != -1) continue;

            labels[q] = currentLabel;

            std::vector<int> qNeighbors;
            rangeQuery(dets, q, qNeighbors);
            if (static_cast<int>(qNeighbors.size()) >= config_.minPoints) {
                for (int nn : qNeighbors) {
                    if (labels[nn] == -1 || labels[nn] == NOISE) {
                        seedSet.push_back(nn);
                    }
                }
            }
        }
    }

    // Build clusters from labels
    std::map<int, std::vector<int>> clusterMap;
    for (int i = 0; i < n; ++i) {
        if (labels[i] >= 0) {
            clusterMap[labels[i]].push_back(i);
        }
    }

    // Noise points become single-detection clusters
    for (int i = 0; i < n; ++i) {
        if (labels[i] == NOISE) {
            clusterMap[clusterLabel++] = {i};
        }
    }

    std::vector<Cluster> result;
    result.reserve(clusterMap.size());
    for (auto& [label, indices] : clusterMap) {
        result.push_back(buildCluster(dets, indices, static_cast<uint32_t>(label)));
    }

    LOG_TRACE("DBScan", "Formed %zu clusters from %d detections",
              result.size(), n);
    return result;
}

} // namespace cuas
