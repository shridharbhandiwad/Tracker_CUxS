#pragma once

#include "motion_model.h"
#include "common/config.h"

namespace cuas {

class CTRModel : public IMotionModel {
public:
    CTRModel(const CTRConfig& cfg, const std::string& label);

    void predict(const StateVector& xIn, const StateMatrix& PIn,
                 double dt,
                 StateVector& xOut, StateMatrix& POut) const override;

    StateMatrix getProcessNoise(double dt) const override;
    StateMatrix getTransitionMatrix(double dt, const StateVector& x) const override;
    std::string name() const override { return label_; }

private:
    double estimateTurnRate(const StateVector& x) const;

    CTRConfig config_;
    std::string label_;
};

} // namespace cuas
