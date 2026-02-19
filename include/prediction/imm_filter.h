#pragma once

#include "motion_model.h"
#include "common/types.h"
#include "common/config.h"
#include <vector>
#include <memory>
#include <array>

namespace cuas {

struct IMMState {
    std::array<StateVector, IMM_NUM_MODELS>  modelStates;
    std::array<StateMatrix, IMM_NUM_MODELS>  modelCovariances;
    std::array<double, IMM_NUM_MODELS>       modeProbabilities;

    StateVector mergedState;
    StateMatrix mergedCovariance;
};

class IMMFilter {
public:
    explicit IMMFilter(const PredictionConfig& cfg);

    void init(const StateVector& x0, const StateMatrix& P0);
    void predict(double dt, IMMState& state) const;
    void update(IMMState& state, const MeasVector& z, const MeasMatrix& R) const;

    MeasStateMatrix getMeasurementMatrix() const;
    MeasMatrix getInnovationCovariance(const IMMState& state, const MeasMatrix& R) const;
    MeasVector getInnovation(const IMMState& state, const MeasVector& z) const;

    static void mergeEstimates(IMMState& state);

    const PredictionConfig& config() const { return config_; }

private:
    void interaction(IMMState& state) const;
    void modelPredictions(double dt, IMMState& state) const;
    void updateModeProbabilities(IMMState& state, const MeasVector& z,
                                 const MeasMatrix& R) const;
    double modelLikelihood(int modelIdx, const IMMState& state,
                           const MeasVector& z, const MeasMatrix& R) const;

    PredictionConfig config_;
    std::array<std::unique_ptr<IMotionModel>, IMM_NUM_MODELS> models_;
    std::array<std::array<double, IMM_NUM_MODELS>, IMM_NUM_MODELS> transMatrix_;
};

} // namespace cuas
