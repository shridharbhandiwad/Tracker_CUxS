#pragma once

#include "common/types.h"
#include "prediction/imm_filter.h"
#include <cstdint>

namespace cuas {

class Track {
public:
    Track(uint32_t id, const StateVector& x0, const StateMatrix& P0,
          const PredictionConfig& predCfg, Timestamp initTime);

    uint32_t id() const { return id_; }
    TrackStatus status() const { return status_; }
    TrackClassification classification() const { return classification_; }

    const IMMState& immState() const { return immState_; }
    IMMState& immState() { return immState_; }

    const StateVector& state() const { return immState_.mergedState; }
    const StateMatrix& covariance() const { return immState_.mergedCovariance; }

    CartesianPos position() const;
    CartesianPos velocity() const;
    SphericalPos sphericalPosition() const;
    double rangeRate() const;

    uint32_t hitCount() const { return hitCount_; }
    uint32_t missCount() const { return missCount_; }
    uint32_t consecutiveMisses() const { return consecutiveMisses_; }
    uint32_t age() const { return age_; }
    double quality() const { return quality_; }
    Timestamp lastUpdateTime() const { return lastUpdateTime_; }
    Timestamp initiationTime() const { return initiationTime_; }

    void setStatus(TrackStatus s) { status_ = s; }
    void setClassification(TrackClassification c) { classification_ = c; }
    void setQuality(double q) { quality_ = q; }

    void recordHit();
    void recordMiss();
    void incrementAge();

    TrackUpdateMessage toUpdateMessage() const;

private:
    uint32_t            id_;
    TrackStatus         status_         = TrackStatus::Tentative;
    TrackClassification classification_ = TrackClassification::Unknown;
    IMMState            immState_;

    uint32_t hitCount_          = 0;
    uint32_t missCount_         = 0;
    uint32_t consecutiveMisses_ = 0;
    uint32_t age_               = 0;
    double   quality_           = 0.5;
    Timestamp initiationTime_   = 0;
    Timestamp lastUpdateTime_   = 0;
};

} // namespace cuas
