#include "prediction/ca_model.h"
#include "common/matrix_ops.h"

namespace cuas {

CAModel::CAModel(const CAConfig& cfg, const std::string& label)
    : config_(cfg), label_(label) {}

StateMatrix CAModel::getTransitionMatrix(double dt, const StateVector& /*x*/) const {
    // State: [x, vx, ax, y, vy, ay, z, vz, az]
    // CA: full constant-acceleration model
    StateMatrix F = matIdentity();
    double dt2 = 0.5 * dt * dt;
    double decay = config_.accelDecayRate;

    // x-axis
    F[0][1] = dt;
    F[0][2] = dt2;
    F[1][2] = dt;
    F[2][2] = decay;

    // y-axis
    F[3][4] = dt;
    F[3][5] = dt2;
    F[4][5] = dt;
    F[5][5] = decay;

    // z-axis
    F[6][7] = dt;
    F[6][8] = dt2;
    F[7][8] = dt;
    F[8][8] = decay;

    return F;
}

StateMatrix CAModel::getProcessNoise(double dt) const {
    double q = config_.processNoiseStd * config_.processNoiseStd;
    double dt2 = dt * dt;
    double dt3 = dt2 * dt;
    double dt4 = dt3 * dt;
    double dt5 = dt4 * dt;

    StateMatrix Q = matZero();
    for (int axis = 0; axis < 3; ++axis) {
        int p = axis * 3;
        int v = axis * 3 + 1;
        int a = axis * 3 + 2;
        Q[p][p] = dt5 / 20.0 * q;
        Q[p][v] = dt4 / 8.0 * q;
        Q[p][a] = dt3 / 6.0 * q;
        Q[v][p] = dt4 / 8.0 * q;
        Q[v][v] = dt3 / 3.0 * q;
        Q[v][a] = dt2 / 2.0 * q;
        Q[a][p] = dt3 / 6.0 * q;
        Q[a][v] = dt2 / 2.0 * q;
        Q[a][a] = dt * q;
    }
    return Q;
}

void CAModel::predict(const StateVector& xIn, const StateMatrix& PIn,
                       double dt,
                       StateVector& xOut, StateMatrix& POut) const {
    StateMatrix F = getTransitionMatrix(dt, xIn);
    StateMatrix Q = getProcessNoise(dt);

    xOut = mat::multiplyMV(F, xIn);

    StateMatrix Ft = mat::transpose(F);
    POut = mat::addMat(mat::multiply(mat::multiply(F, PIn), Ft), Q);
}

} // namespace cuas
