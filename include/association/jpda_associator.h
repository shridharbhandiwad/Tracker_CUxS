#pragma once

#include "association_engine.h"
#include <vector>

namespace cuas {

class JPDAAssociator : public IAssociator {
public:
    explicit JPDAAssociator(const JPDAConfig& cfg, double gatingThreshold);

    AssociationOutput associate(
        const std::vector<Track>& tracks,
        const std::vector<Cluster>& clusters,
        const IMMFilter& imm,
        const MeasMatrix& R) override;

    std::string name() const override { return "JPDA"; }

    struct JPDAWeights {
        int trackIndex;
        std::vector<std::pair<int, double>> clusterWeights; // (clusterIdx, beta)
        double betaZero; // probability of no detection
    };

    std::vector<JPDAWeights> computeWeights(
        const std::vector<Track>& tracks,
        const std::vector<Cluster>& clusters,
        const IMMFilter& imm,
        const MeasMatrix& R) const;

private:
    JPDAConfig config_;
    double gatingThreshold_;
};

} // namespace cuas
