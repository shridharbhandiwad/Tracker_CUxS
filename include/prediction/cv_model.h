#pragma once

#include "motion_model.h"
#include "common/config.h"

namespace cuas {

class CVModel : public IMotionModel {
public:
    explicit CVModel(const CVConfig& cfg);

    void predict(const StateVector& xIn, const StateMatrix& PIn,
                 double dt,
                 StateVector& xOut, StateMatrix& POut) const override;

    StateMatrix getProcessNoise(double dt) const override;
    StateMatrix getTransitionMatrix(double dt, const StateVector& x) const override;
    std::string name() const override { return "CV"; }

private:
    CVConfig config_;
};

} // namespace cuas
