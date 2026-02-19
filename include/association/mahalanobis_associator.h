#pragma once

#include "association_engine.h"

namespace cuas {

class MahalanobisAssociator : public IAssociator {
public:
    explicit MahalanobisAssociator(const MahalanobisConfig& cfg, double gatingThreshold);

    AssociationOutput associate(
        const std::vector<Track>& tracks,
        const std::vector<Cluster>& clusters,
        const IMMFilter& imm,
        const MeasMatrix& R) override;

    std::string name() const override { return "Mahalanobis"; }

private:
    MahalanobisConfig config_;
    double gatingThreshold_;
};

} // namespace cuas
