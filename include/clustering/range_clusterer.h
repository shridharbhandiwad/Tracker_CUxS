#pragma once

#include "cluster_engine.h"

namespace cuas {

class RangeClusterer : public IClusterer {
public:
    explicit RangeClusterer(const RangeBasedConfig& cfg);

    std::vector<Cluster> cluster(const std::vector<Detection>& dets) override;
    std::string name() const override { return "RangeBased"; }

private:
    bool inGate(const Detection& a, const Detection& b) const;
    Cluster buildCluster(const std::vector<Detection>& dets,
                         const std::vector<int>& indices, uint32_t id) const;

    RangeBasedConfig config_;
};

} // namespace cuas
