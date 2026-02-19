#pragma once

#include "common/types.h"
#include "common/config.h"
#include "prediction/imm_filter.h"
#include <vector>
#include <memory>

namespace cuas {

struct AssociationResult {
    int trackIndex   = -1;
    int clusterIndex = -1;
    double distance  = 1e30;
};

struct AssociationOutput {
    std::vector<AssociationResult> matched;
    std::vector<int> unmatchedTracks;
    std::vector<int> unmatchedClusters;
};

// Forward declaration
class Track;

class IAssociator {
public:
    virtual ~IAssociator() = default;
    virtual AssociationOutput associate(
        const std::vector<Track>& tracks,
        const std::vector<Cluster>& clusters,
        const IMMFilter& imm,
        const MeasMatrix& R) = 0;
    virtual std::string name() const = 0;
};

class AssociationEngine {
public:
    explicit AssociationEngine(const AssociationConfig& cfg);

    AssociationOutput process(
        const std::vector<Track>& tracks,
        const std::vector<Cluster>& clusters,
        const IMMFilter& imm,
        const MeasMatrix& R);

    std::string activeMethod() const;

private:
    std::unique_ptr<IAssociator> associator_;
    AssociationConfig config_;
};

} // namespace cuas
