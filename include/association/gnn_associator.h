#pragma once

#include "association_engine.h"

namespace cuas {

class GNNAssociator : public IAssociator {
public:
    explicit GNNAssociator(const GNNConfig& cfg, double gatingThreshold);

    AssociationOutput associate(
        const std::vector<Track>& tracks,
        const std::vector<Cluster>& clusters,
        const IMMFilter& imm,
        const MeasMatrix& R) override;

    std::string name() const override { return "GNN"; }

private:
    // Auction-based or Hungarian assignment
    std::vector<int> hungarianAssignment(
        const std::vector<std::vector<double>>& costMatrix,
        int numTracks, int numClusters) const;

    GNNConfig config_;
    double gatingThreshold_;
};

} // namespace cuas
