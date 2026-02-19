#include "clustering/range_clusterer.h"
#include "common/logger.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace cuas {

RangeClusterer::RangeClusterer(const RangeBasedConfig& cfg) : config_(cfg) {}

bool RangeClusterer::inGate(const Detection& a, const Detection& b) const {
    return std::abs(a.range - b.range) <= config_.rangeGateSize &&
           std::abs(a.azimuth - b.azimuth) <= config_.azimuthGateSize &&
           std::abs(a.elevation - b.elevation) <= config_.elevationGateSize;
}

Cluster RangeClusterer::buildCluster(const std::vector<Detection>& dets,
                                      const std::vector<int>& indices, uint32_t id) const {
    Cluster c;
    c.clusterId = id;
    c.numDetections = static_cast<uint32_t>(indices.size());

    double linStrengthSum = 0.0;
    for (int idx : indices) {
        double linStr = std::pow(10.0, dets[idx].strength / 10.0);
        linStrengthSum += linStr;
        c.detectionIndices.push_back(static_cast<uint32_t>(idx));
    }

    double totalStrength = 0.0;
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

std::vector<Cluster> RangeClusterer::cluster(const std::vector<Detection>& dets) {
    int n = static_cast<int>(dets.size());
    if (n == 0) return {};

    // Sort by range
    std::vector<int> sortedIdx(n);
    for (int i = 0; i < n; ++i) sortedIdx[i] = i;
    std::sort(sortedIdx.begin(), sortedIdx.end(), [&](int a, int b) {
        return dets[a].range < dets[b].range;
    });

    // Greedy gating
    std::vector<bool> assigned(n, false);
    std::vector<Cluster> result;
    uint32_t cid = 0;

    for (int si = 0; si < n; ++si) {
        int i = sortedIdx[si];
        if (assigned[i]) continue;

        std::vector<int> group = {i};
        assigned[i] = true;

        for (int sj = si + 1; sj < n; ++sj) {
            int j = sortedIdx[sj];
            if (assigned[j]) continue;
            if (dets[j].range - dets[i].range > config_.rangeGateSize) break;
            if (inGate(dets[i], dets[j])) {
                group.push_back(j);
                assigned[j] = true;
            }
        }

        result.push_back(buildCluster(dets, group, cid++));
    }

    LOG_TRACE("RangeClusterer", "Formed %zu clusters from %d detections",
              result.size(), n);
    return result;
}

} // namespace cuas
