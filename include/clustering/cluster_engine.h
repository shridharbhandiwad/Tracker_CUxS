#pragma once

#include "common/types.h"
#include "common/config.h"
#include <vector>
#include <memory>

namespace cuas {

class IClusterer {
public:
    virtual ~IClusterer() = default;
    virtual std::vector<Cluster> cluster(const std::vector<Detection>& dets) = 0;
    virtual std::string name() const = 0;
};

class ClusterEngine {
public:
    explicit ClusterEngine(const ClusterConfig& cfg);

    std::vector<Cluster> process(const std::vector<Detection>& dets);
    std::string activeMethod() const;

private:
    std::unique_ptr<IClusterer> clusterer_;
    ClusterConfig config_;
    uint32_t nextClusterId_ = 1;
};

} // namespace cuas
