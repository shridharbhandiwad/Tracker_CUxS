#include "prediction/imm_filter.h"
#include "prediction/cv_model.h"
#include "prediction/ca_model.h"
#include "prediction/ctr_model.h"
#include "common/matrix_ops.h"
#include "common/logger.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace cuas {

IMMFilter::IMMFilter(const PredictionConfig& cfg) : config_(cfg) {
    models_[0] = std::make_unique<CVModel>(cfg.cv);
    models_[1] = std::make_unique<CAModel>(cfg.ca1, "CA1");
    models_[2] = std::make_unique<CAModel>(cfg.ca2, "CA2");
    models_[3] = std::make_unique<CTRModel>(cfg.ctr1, "CTR1");
    models_[4] = std::make_unique<CTRModel>(cfg.ctr2, "CTR2");

    for (int i = 0; i < IMM_NUM_MODELS; ++i)
        for (int j = 0; j < IMM_NUM_MODELS; ++j)
            transMatrix_[i][j] = cfg.imm.transitionMatrix[i][j];

    LOG_INFO("IMMFilter", "Initialized with %d models: CV, CA1, CA2, CTR1, CTR2",
             IMM_NUM_MODELS);
}

void IMMFilter::init(const StateVector& x0, const StateMatrix& P0) {
    // Unused here; Track construction handles init
    (void)x0; (void)P0;
}

MeasStateMatrix IMMFilter::getMeasurementMatrix() const {
    // Measurement is Cartesian position [x, y, z]
    // H maps state [x, vx, ax, y, vy, ay, z, vz, az] -> [x, y, z]
    MeasStateMatrix H{};
    for (int i = 0; i < MEAS_DIM; ++i)
        for (int j = 0; j < STATE_DIM; ++j)
            H[i][j] = 0.0;
    H[0][0] = 1.0; // x
    H[1][3] = 1.0; // y
    H[2][6] = 1.0; // z
    return H;
}

void IMMFilter::interaction(IMMState& state) const {
    // Compute mixing probabilities
    std::array<double, IMM_NUM_MODELS> cBar;
    cBar.fill(0.0);
    for (int j = 0; j < IMM_NUM_MODELS; ++j)
        for (int i = 0; i < IMM_NUM_MODELS; ++i)
            cBar[j] += transMatrix_[i][j] * state.modeProbabilities[i];

    // Mixing probabilities mu_{i|j}
    std::array<std::array<double, IMM_NUM_MODELS>, IMM_NUM_MODELS> mixProb;
    for (int i = 0; i < IMM_NUM_MODELS; ++i)
        for (int j = 0; j < IMM_NUM_MODELS; ++j) {
            if (cBar[j] > 1e-15)
                mixProb[i][j] = transMatrix_[i][j] * state.modeProbabilities[i] / cBar[j];
            else
                mixProb[i][j] = (i == j) ? 1.0 : 0.0;
        }

    // Mixed initial conditions for each model
    std::array<StateVector, IMM_NUM_MODELS> x0j;
    std::array<StateMatrix, IMM_NUM_MODELS> P0j;

    for (int j = 0; j < IMM_NUM_MODELS; ++j) {
        x0j[j] = stateZero();
        for (int i = 0; i < IMM_NUM_MODELS; ++i) {
            x0j[j] = mat::add(x0j[j], mat::scale(state.modelStates[i], mixProb[i][j]));
        }
    }

    for (int j = 0; j < IMM_NUM_MODELS; ++j) {
        P0j[j] = matZero();
        for (int i = 0; i < IMM_NUM_MODELS; ++i) {
            StateVector diff = mat::sub(state.modelStates[i], x0j[j]);
            StateMatrix spread = mat::outerProduct(diff, diff);
            StateMatrix weighted = mat::addMat(state.modelCovariances[i], spread);
            P0j[j] = mat::addMat(P0j[j], mat::scaleMat(weighted, mixProb[i][j]));
        }
    }

    state.modelStates = x0j;
    state.modelCovariances = P0j;
}

void IMMFilter::modelPredictions(double dt, IMMState& state) const {
    for (int m = 0; m < IMM_NUM_MODELS; ++m) {
        StateVector xPred;
        StateMatrix PPred;
        models_[m]->predict(state.modelStates[m], state.modelCovariances[m],
                            dt, xPred, PPred);
        state.modelStates[m] = xPred;
        state.modelCovariances[m] = PPred;
    }
}

void IMMFilter::predict(double dt, IMMState& state) const {
    interaction(state);
    modelPredictions(dt, state);
    mergeEstimates(state);

    LOG_TRACE("IMMFilter", "Predict dt=%.4f, probs=[%.3f,%.3f,%.3f,%.3f,%.3f]",
              dt,
              state.modeProbabilities[0], state.modeProbabilities[1],
              state.modeProbabilities[2], state.modeProbabilities[3],
              state.modeProbabilities[4]);
}

double IMMFilter::modelLikelihood(int modelIdx, const IMMState& state,
                                   const MeasVector& z, const MeasMatrix& R) const {
    MeasStateMatrix H = getMeasurementMatrix();
    MeasVector zPred = mat::measFromState(H, state.modelStates[modelIdx]);
    MeasVector innov = mat::measSub(z, zPred);
    MeasMatrix S = mat::measAddMat(mat::hpht(H, state.modelCovariances[modelIdx]), R);

    double detS = mat::det3x3(S);
    if (detS < 1e-30) return 1e-30;

    MeasMatrix Sinv;
    if (!mat::invertMeas(S, Sinv)) return 1e-30;

    double d = mat::mahalanobisDistance(innov, Sinv);
    double logLik = -0.5 * (MEAS_DIM * std::log(2.0 * 3.14159265358979) +
                            std::log(detS) + d);
    return std::exp(logLik);
}

void IMMFilter::updateModeProbabilities(IMMState& state, const MeasVector& z,
                                         const MeasMatrix& R) const {
    std::array<double, IMM_NUM_MODELS> likelihoods;
    for (int m = 0; m < IMM_NUM_MODELS; ++m) {
        likelihoods[m] = modelLikelihood(m, state, z, R);
    }

    // Predicted mode probabilities
    std::array<double, IMM_NUM_MODELS> cBar;
    cBar.fill(0.0);
    for (int j = 0; j < IMM_NUM_MODELS; ++j)
        for (int i = 0; i < IMM_NUM_MODELS; ++i)
            cBar[j] += transMatrix_[i][j] * state.modeProbabilities[i];

    double totalLik = 0.0;
    for (int j = 0; j < IMM_NUM_MODELS; ++j) {
        state.modeProbabilities[j] = likelihoods[j] * cBar[j];
        totalLik += state.modeProbabilities[j];
    }

    if (totalLik > 1e-30) {
        for (int j = 0; j < IMM_NUM_MODELS; ++j)
            state.modeProbabilities[j] /= totalLik;
    } else {
        for (int j = 0; j < IMM_NUM_MODELS; ++j)
            state.modeProbabilities[j] = 1.0 / IMM_NUM_MODELS;
    }
}

void IMMFilter::update(IMMState& state, const MeasVector& z, const MeasMatrix& R) const {
    MeasStateMatrix H = getMeasurementMatrix();

    // Update each model with standard Kalman update
    for (int m = 0; m < IMM_NUM_MODELS; ++m) {
        MeasVector zPred = mat::measFromState(H, state.modelStates[m]);
        MeasVector innov = mat::measSub(z, zPred);

        MeasMatrix S = mat::measAddMat(mat::hpht(H, state.modelCovariances[m]), R);
        MeasMatrix Sinv;
        if (!mat::invertMeas(S, Sinv)) continue;

        auto PHt = mat::pht(state.modelCovariances[m], H);
        auto K = mat::kalmanGain(PHt, Sinv);

        StateVector correction = mat::kalmanCorrection(K, innov);
        state.modelStates[m] = mat::add(state.modelStates[m], correction);

        StateMatrix KH = mat::kh(K, H);
        StateMatrix IminKH = mat::subMat(matIdentity(), KH);
        state.modelCovariances[m] = mat::multiply(IminKH, state.modelCovariances[m]);

        // Joseph form stabilization would be:
        // P = (I-KH)P(I-KH)' + KRK'
        // Using the simplified form above for performance; numerically
        // stable enough for typical radar tracking covariance magnitudes.
    }

    updateModeProbabilities(state, z, R);
    mergeEstimates(state);

    LOG_TRACE("IMMFilter", "Update probs=[%.3f,%.3f,%.3f,%.3f,%.3f]",
              state.modeProbabilities[0], state.modeProbabilities[1],
              state.modeProbabilities[2], state.modeProbabilities[3],
              state.modeProbabilities[4]);
}

MeasMatrix IMMFilter::getInnovationCovariance(const IMMState& state,
                                               const MeasMatrix& R) const {
    MeasStateMatrix H = getMeasurementMatrix();
    MeasMatrix S = mat::measAddMat(mat::hpht(H, state.mergedCovariance), R);
    return S;
}

MeasVector IMMFilter::getInnovation(const IMMState& state, const MeasVector& z) const {
    MeasStateMatrix H = getMeasurementMatrix();
    MeasVector zPred = mat::measFromState(H, state.mergedState);
    return mat::measSub(z, zPred);
}

void IMMFilter::mergeEstimates(IMMState& state) {
    state.mergedState = stateZero();
    for (int m = 0; m < IMM_NUM_MODELS; ++m) {
        state.mergedState = mat::add(state.mergedState,
                                     mat::scale(state.modelStates[m],
                                                state.modeProbabilities[m]));
    }

    state.mergedCovariance = matZero();
    for (int m = 0; m < IMM_NUM_MODELS; ++m) {
        StateVector diff = mat::sub(state.modelStates[m], state.mergedState);
        StateMatrix spread = mat::outerProduct(diff, diff);
        StateMatrix weighted = mat::addMat(state.modelCovariances[m], spread);
        state.mergedCovariance = mat::addMat(state.mergedCovariance,
                                              mat::scaleMat(weighted,
                                                            state.modeProbabilities[m]));
    }
}

} // namespace cuas
