#include "association/association_engine.h"
#include "association/mahalanobis_associator.h"
#include "association/gnn_associator.h"
#include "association/jpda_associator.h"
#include "common/logger.h"

namespace cuas {

AssociationEngine::AssociationEngine(const AssociationConfig& cfg) : config_(cfg) {
    switch (cfg.method) {
        case AssociationMethod::Mahalanobis:
            associator_ = std::make_unique<MahalanobisAssociator>(
                cfg.mahalanobis, cfg.gatingThreshold);
            break;
        case AssociationMethod::GNN:
            associator_ = std::make_unique<GNNAssociator>(cfg.gnn, cfg.gatingThreshold);
            break;
        case AssociationMethod::JPDA:
            associator_ = std::make_unique<JPDAAssociator>(cfg.jpda, cfg.gatingThreshold);
            break;
    }
    LOG_INFO("Association", "Initialized with method: %s", associator_->name().c_str());
}

AssociationOutput AssociationEngine::process(
    const std::vector<Track>& tracks,
    const std::vector<Cluster>& clusters,
    const IMMFilter& imm,
    const MeasMatrix& R) {

    if (tracks.empty() || clusters.empty()) {
        AssociationOutput out;
        for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
            out.unmatchedTracks.push_back(i);
        for (int i = 0; i < static_cast<int>(clusters.size()); ++i)
            out.unmatchedClusters.push_back(i);
        return out;
    }

    return associator_->associate(tracks, clusters, imm, R);
}

std::string AssociationEngine::activeMethod() const {
    return associator_ ? associator_->name() : "None";
}

} // namespace cuas
