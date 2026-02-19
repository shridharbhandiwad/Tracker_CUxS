#pragma once

#include "cluster_engine.h"

namespace cuas {

class RangeStrengthClusterer : public IClusterer {
public:
    explicit RangeStrengthClusterer(const RangeStrengthConfig& cfg);

    std::vector<Cluster> cluster(const std::vector<Detection>& dets) override;
    std::string name() const override { return "RangeStrength"; }

private:
    bool inGate(const Detection& a, const Detection& b) const;
    Cluster buildCluster(const std::vector<Detection>& dets,
                         const std::vector<int>& indices, uint32_t id) const;

    RangeStrengthConfig config_;
};

} // namespace cuas
