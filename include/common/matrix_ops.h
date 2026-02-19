#pragma once

#include "types.h"
#include <cmath>
#include <algorithm>

namespace cuas { namespace mat {

// ---------------------------------------------------------------------------
// State vector operations
// ---------------------------------------------------------------------------
inline StateVector add(const StateVector& a, const StateVector& b) {
    StateVector r;
    for (int i = 0; i < STATE_DIM; ++i) r[i] = a[i] + b[i];
    return r;
}

inline StateVector sub(const StateVector& a, const StateVector& b) {
    StateVector r;
    for (int i = 0; i < STATE_DIM; ++i) r[i] = a[i] - b[i];
    return r;
}

inline StateVector scale(const StateVector& a, double s) {
    StateVector r;
    for (int i = 0; i < STATE_DIM; ++i) r[i] = a[i] * s;
    return r;
}

// ---------------------------------------------------------------------------
// State matrix operations
// ---------------------------------------------------------------------------
inline StateMatrix addMat(const StateMatrix& A, const StateMatrix& B) {
    StateMatrix R;
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            R[i][j] = A[i][j] + B[i][j];
    return R;
}

inline StateMatrix subMat(const StateMatrix& A, const StateMatrix& B) {
    StateMatrix R;
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            R[i][j] = A[i][j] - B[i][j];
    return R;
}

inline StateMatrix scaleMat(const StateMatrix& A, double s) {
    StateMatrix R;
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            R[i][j] = A[i][j] * s;
    return R;
}

inline StateMatrix multiply(const StateMatrix& A, const StateMatrix& B) {
    StateMatrix R = matZero();
    for (int i = 0; i < STATE_DIM; ++i)
        for (int k = 0; k < STATE_DIM; ++k)
            if (std::abs(A[i][k]) > 1e-15)
                for (int j = 0; j < STATE_DIM; ++j)
                    R[i][j] += A[i][k] * B[k][j];
    return R;
}

inline StateVector multiplyMV(const StateMatrix& A, const StateVector& v) {
    StateVector r;
    r.fill(0.0);
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            r[i] += A[i][j] * v[j];
    return r;
}

inline StateMatrix transpose(const StateMatrix& A) {
    StateMatrix R;
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            R[i][j] = A[j][i];
    return R;
}

inline StateMatrix outerProduct(const StateVector& a, const StateVector& b) {
    StateMatrix R;
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            R[i][j] = a[i] * b[j];
    return R;
}

// ---------------------------------------------------------------------------
// Generic NxN matrix inversion (Gauss-Jordan) for small dimensions
// ---------------------------------------------------------------------------
template<int N>
bool invertMatrix(const std::array<std::array<double, N>, N>& input,
                  std::array<std::array<double, N>, N>& output) {
    std::array<std::array<double, 2 * N>, N> aug{};
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) aug[i][j] = input[i][j];
        for (int j = 0; j < N; ++j) aug[i][N + j] = (i == j) ? 1.0 : 0.0;
    }

    for (int col = 0; col < N; ++col) {
        int pivotRow = col;
        double pivotVal = std::abs(aug[col][col]);
        for (int row = col + 1; row < N; ++row) {
            if (std::abs(aug[row][col]) > pivotVal) {
                pivotVal = std::abs(aug[row][col]);
                pivotRow = row;
            }
        }
        if (pivotVal < 1e-14) return false;
        if (pivotRow != col) std::swap(aug[col], aug[pivotRow]);

        double div = aug[col][col];
        for (int j = 0; j < 2 * N; ++j) aug[col][j] /= div;

        for (int row = 0; row < N; ++row) {
            if (row == col) continue;
            double factor = aug[row][col];
            for (int j = 0; j < 2 * N; ++j)
                aug[row][j] -= factor * aug[col][j];
        }
    }

    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            output[i][j] = aug[i][N + j];
    return true;
}

inline bool invertState(const StateMatrix& in, StateMatrix& out) {
    return invertMatrix<STATE_DIM>(in, out);
}

inline bool invertMeas(const MeasMatrix& in, MeasMatrix& out) {
    return invertMatrix<MEAS_DIM>(in, out);
}

// ---------------------------------------------------------------------------
// Measurement-space helpers (3x9, 9x3, 3x3)
// ---------------------------------------------------------------------------

// H * x  (3x9 * 9x1 -> 3x1)
inline MeasVector measFromState(const MeasStateMatrix& H, const StateVector& x) {
    MeasVector z;
    z.fill(0.0);
    for (int i = 0; i < MEAS_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            z[i] += H[i][j] * x[j];
    return z;
}

// H * P * H^T  (3x9 * 9x9 * 9x3 -> 3x3)
inline MeasMatrix hpht(const MeasStateMatrix& H, const StateMatrix& P) {
    MeasMatrix R;
    // temp = H * P (3x9)
    MeasStateMatrix temp{};
    for (int i = 0; i < MEAS_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j) {
            temp[i][j] = 0.0;
            for (int k = 0; k < STATE_DIM; ++k)
                temp[i][j] += H[i][k] * P[k][j];
        }
    // R = temp * H^T (3x3)
    for (int i = 0; i < MEAS_DIM; ++i)
        for (int j = 0; j < MEAS_DIM; ++j) {
            R[i][j] = 0.0;
            for (int k = 0; k < STATE_DIM; ++k)
                R[i][j] += temp[i][k] * H[j][k];
        }
    return R;
}

// P * H^T (9x9 * 9x3 -> 9x3)
inline std::array<std::array<double, MEAS_DIM>, STATE_DIM>
pht(const StateMatrix& P, const MeasStateMatrix& H) {
    std::array<std::array<double, MEAS_DIM>, STATE_DIM> R{};
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < MEAS_DIM; ++j) {
            R[i][j] = 0.0;
            for (int k = 0; k < STATE_DIM; ++k)
                R[i][j] += P[i][k] * H[j][k];
        }
    return R;
}

// K = PHt * S^{-1}  (9x3 * 3x3 -> 9x3)
inline std::array<std::array<double, MEAS_DIM>, STATE_DIM>
kalmanGain(const std::array<std::array<double, MEAS_DIM>, STATE_DIM>& PHt,
           const MeasMatrix& Sinv) {
    std::array<std::array<double, MEAS_DIM>, STATE_DIM> K{};
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < MEAS_DIM; ++j) {
            K[i][j] = 0.0;
            for (int k = 0; k < MEAS_DIM; ++k)
                K[i][j] += PHt[i][k] * Sinv[k][j];
        }
    return K;
}

// K * innovation (9x3 * 3x1 -> 9x1)
inline StateVector kalmanCorrection(
    const std::array<std::array<double, MEAS_DIM>, STATE_DIM>& K,
    const MeasVector& innov) {
    StateVector r;
    r.fill(0.0);
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < MEAS_DIM; ++j)
            r[i] += K[i][j] * innov[j];
    return r;
}

// K * H (9x3 * 3x9 -> 9x9)
inline StateMatrix kh(const std::array<std::array<double, MEAS_DIM>, STATE_DIM>& K,
                      const MeasStateMatrix& H) {
    StateMatrix R = matZero();
    for (int i = 0; i < STATE_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            for (int k = 0; k < MEAS_DIM; ++k)
                R[i][j] += K[i][k] * H[k][j];
    return R;
}

// Mahalanobis distance: innov^T * Sinv * innov
inline double mahalanobisDistance(const MeasVector& innov, const MeasMatrix& Sinv) {
    double d = 0.0;
    for (int i = 0; i < MEAS_DIM; ++i)
        for (int j = 0; j < MEAS_DIM; ++j)
            d += innov[i] * Sinv[i][j] * innov[j];
    return d;
}

// Determinant of 3x3 matrix
inline double det3x3(const MeasMatrix& M) {
    return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
         - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
         + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
}

// Measurement addition/subtraction
inline MeasVector measSub(const MeasVector& a, const MeasVector& b) {
    MeasVector r;
    for (int i = 0; i < MEAS_DIM; ++i) r[i] = a[i] - b[i];
    return r;
}

inline MeasMatrix measAddMat(const MeasMatrix& A, const MeasMatrix& B) {
    MeasMatrix R;
    for (int i = 0; i < MEAS_DIM; ++i)
        for (int j = 0; j < MEAS_DIM; ++j)
            R[i][j] = A[i][j] + B[i][j];
    return R;
}

}} // namespace cuas::mat
