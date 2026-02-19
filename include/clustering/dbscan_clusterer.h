#pragma once

#include "cluster_engine.h"
#include <vector>

namespace cuas {

class DBScanClusterer : public IClusterer {
public:
    explicit DBScanClusterer(const DBScanConfig& cfg);

    std::vector<Cluster> cluster(const std::vector<Detection>& dets) override;
    std::string name() const override { return "DBSCAN"; }

private:
    void rangeQuery(const std::vector<Detection>& dets, int idx,
                    std::vector<int>& neighbors) const;
    double distance(const Detection& a, const Detection& b) const;
    Cluster buildCluster(const std::vector<Detection>& dets,
                         const std::vector<int>& indices, uint32_t id) const;

    DBScanConfig config_;
};

} // namespace cuas
