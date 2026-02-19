#include "clustering/cluster_engine.h"
#include "clustering/dbscan_clusterer.h"
#include "clustering/range_clusterer.h"
#include "clustering/range_strength_clusterer.h"
#include "common/logger.h"

namespace cuas {

ClusterEngine::ClusterEngine(const ClusterConfig& cfg) : config_(cfg) {
    switch (cfg.method) {
        case ClusterMethod::DBSCAN:
            clusterer_ = std::make_unique<DBScanClusterer>(cfg.dbscan);
            break;
        case ClusterMethod::RangeBased:
            clusterer_ = std::make_unique<RangeClusterer>(cfg.rangeBased);
            break;
        case ClusterMethod::RangeStrengthBased:
            clusterer_ = std::make_unique<RangeStrengthClusterer>(cfg.rangeStrength);
            break;
    }
    LOG_INFO("ClusterEngine", "Initialized with method: %s", clusterer_->name().c_str());
}

std::vector<Cluster> ClusterEngine::process(const std::vector<Detection>& dets) {
    if (dets.empty()) return {};

    auto clusters = clusterer_->cluster(dets);

    for (auto& c : clusters) {
        c.clusterId = nextClusterId_++;
        c.cartesian = sphericalToCartesian(c.range, c.azimuth, c.elevation);
    }

    LOG_DEBUG("ClusterEngine", "Input dets: %zu, Output clusters: %zu",
              dets.size(), clusters.size());

    return clusters;
}

std::string ClusterEngine::activeMethod() const {
    return clusterer_ ? clusterer_->name() : "None";
}

} // namespace cuas
