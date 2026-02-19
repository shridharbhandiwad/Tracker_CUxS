#include "prediction/cv_model.h"
#include "common/matrix_ops.h"

namespace cuas {

CVModel::CVModel(const CVConfig& cfg) : config_(cfg) {}

StateMatrix CVModel::getTransitionMatrix(double dt, const StateVector& /*x*/) const {
    // State: [x, vx, ax, y, vy, ay, z, vz, az]
    // CV: position updates with velocity, velocity constant, acceleration forced to zero
    StateMatrix F = matIdentity();
    // x += vx*dt
    F[0][1] = dt;
    // y += vy*dt
    F[3][4] = dt;
    // z += vz*dt
    F[6][7] = dt;
    // Force acceleration to zero in CV
    F[2][2] = 0.0;
    F[5][5] = 0.0;
    F[8][8] = 0.0;
    return F;
}

StateMatrix CVModel::getProcessNoise(double dt) const {
    double q = config_.processNoiseStd * config_.processNoiseStd;
    double dt2 = dt * dt;
    double dt3 = dt2 * dt / 2.0;
    double dt4 = dt2 * dt2 / 4.0;

    StateMatrix Q = matZero();
    // For each axis (x, y, z): indices {0,1,2}, {3,4,5}, {6,7,8}
    for (int axis = 0; axis < 3; ++axis) {
        int p = axis * 3;     // position index
        int v = axis * 3 + 1; // velocity index
        Q[p][p] = dt4 * q;
        Q[p][v] = dt3 * q;
        Q[v][p] = dt3 * q;
        Q[v][v] = dt2 * q;
        // Acceleration terms minimal for CV
        Q[p + 2][p + 2] = q * 0.01;
    }
    return Q;
}

void CVModel::predict(const StateVector& xIn, const StateMatrix& PIn,
                       double dt,
                       StateVector& xOut, StateMatrix& POut) const {
    StateMatrix F = getTransitionMatrix(dt, xIn);
    StateMatrix Q = getProcessNoise(dt);

    xOut = mat::multiplyMV(F, xIn);

    // Force acceleration to zero in CV model
    xOut[2] = 0.0;
    xOut[5] = 0.0;
    xOut[8] = 0.0;

    StateMatrix Ft = mat::transpose(F);
    POut = mat::addMat(mat::multiply(mat::multiply(F, PIn), Ft), Q);
}

} // namespace cuas
