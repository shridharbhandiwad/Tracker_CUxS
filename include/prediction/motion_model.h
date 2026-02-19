#pragma once

#include "common/types.h"
#include <string>

namespace cuas {

class IMotionModel {
public:
    virtual ~IMotionModel() = default;

    virtual void predict(const StateVector& xIn, const StateMatrix& PIn,
                         double dt,
                         StateVector& xOut, StateMatrix& POut) const = 0;

    virtual StateMatrix getProcessNoise(double dt) const = 0;
    virtual StateMatrix getTransitionMatrix(double dt, const StateVector& x) const = 0;
    virtual std::string name() const = 0;
};

} // namespace cuas
