#include "prediction/ctr_model.h"
#include "common/matrix_ops.h"
#include <cmath>

namespace cuas {

CTRModel::CTRModel(const CTRConfig& cfg, const std::string& label)
    : config_(cfg), label_(label) {}

double CTRModel::estimateTurnRate(const StateVector& x) const {
    // Estimate turn rate from velocity and acceleration in x-y plane
    double vx = x[1], vy = x[4];
    double ax = x[2], ay = x[5];
    double v2 = vx * vx + vy * vy;
    if (v2 < 1e-6) return 0.0;
    // omega = (vx*ay - vy*ax) / v^2
    return (vx * ay - vy * ax) / v2;
}

StateMatrix CTRModel::getTransitionMatrix(double dt, const StateVector& x) const {
    double omega = estimateTurnRate(x);
    StateMatrix F = matIdentity();

    if (std::abs(omega) < 1e-6) {
        // Near-zero turn rate: degenerate to CV-like
        F[0][1] = dt;
        F[3][4] = dt;
        F[6][7] = dt;
        F[2][2] = 0.0;
        F[5][5] = 0.0;
        F[8][8] = 0.0;
    } else {
        double sinOt = std::sin(omega * dt);
        double cosOt = std::cos(omega * dt);

        // x-y coordinated turn
        F[0][1] = sinOt / omega;
        F[0][4] = -(1.0 - cosOt) / omega;
        F[1][1] = cosOt;
        F[1][4] = -sinOt;
        F[3][1] = (1.0 - cosOt) / omega;
        F[3][4] = sinOt / omega;
        F[4][1] = sinOt;
        F[4][4] = cosOt;

        // z-axis: constant velocity (no turn in z)
        F[6][7] = dt;

        // Acceleration states decay
        F[2][2] = 0.5;
        F[5][5] = 0.5;
        F[8][8] = 0.0;
    }

    return F;
}

StateMatrix CTRModel::getProcessNoise(double dt) const {
    double q = config_.processNoiseStd * config_.processNoiseStd;
    double qOmega = config_.turnRateNoiseStd * config_.turnRateNoiseStd;
    double dt2 = dt * dt;
    double dt3 = dt2 * dt;

    StateMatrix Q = matZero();
    for (int axis = 0; axis < 3; ++axis) {
        int p = axis * 3;
        int v = axis * 3 + 1;
        int a = axis * 3 + 2;

        double qAxis = q;
        if (axis < 2) {
            qAxis += qOmega;
        }

        Q[p][p] = dt3 / 3.0 * qAxis;
        Q[p][v] = dt2 / 2.0 * qAxis;
        Q[v][p] = dt2 / 2.0 * qAxis;
        Q[v][v] = dt * qAxis;
        Q[a][a] = qAxis * 0.1;
    }
    return Q;
}

void CTRModel::predict(const StateVector& xIn, const StateMatrix& PIn,
                        double dt,
                        StateVector& xOut, StateMatrix& POut) const {
    StateMatrix F = getTransitionMatrix(dt, xIn);
    StateMatrix Q = getProcessNoise(dt);

    xOut = mat::multiplyMV(F, xIn);

    StateMatrix Ft = mat::transpose(F);
    POut = mat::addMat(mat::multiply(mat::multiply(F, PIn), Ft), Q);
}

} // namespace cuas
