#include "track_management/track.h"
#include "common/constants.h"

namespace cuas {

Track::Track(uint32_t id, const StateVector& x0, const StateMatrix& P0,
             const PredictionConfig& predCfg, Timestamp initTime)
    : id_(id), initiationTime_(initTime), lastUpdateTime_(initTime) {

    // Initialize all models with the same state
    for (int m = 0; m < IMM_NUM_MODELS; ++m) {
        immState_.modelStates[m] = x0;
        immState_.modelCovariances[m] = P0;
    }
    immState_.modeProbabilities = predCfg.imm.initialModeProbabilities;
    immState_.mergedState = x0;
    immState_.mergedCovariance = P0;

    hitCount_ = 1;
}

CartesianPos Track::position() const {
    return {immState_.mergedState[0],
            immState_.mergedState[3],
            immState_.mergedState[6]};
}

CartesianPos Track::velocity() const {
    return {immState_.mergedState[1],
            immState_.mergedState[4],
            immState_.mergedState[7]};
}

SphericalPos Track::sphericalPosition() const {
    auto p = position();
    return cartesianToSpherical(p.x, p.y, p.z);
}

double Track::rangeRate() const {
    auto p = position();
    auto v = velocity();
    double r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (r < 1e-9) return 0.0;
    return (p.x * v.x + p.y * v.y + p.z * v.z) / r;
}

void Track::recordHit() {
    ++hitCount_;
    consecutiveMisses_ = 0;
    lastUpdateTime_ = nowMicros();
}

void Track::recordMiss() {
    ++missCount_;
    ++consecutiveMisses_;
}

void Track::incrementAge() {
    ++age_;
}

TrackUpdateMessage Track::toUpdateMessage() const {
    TrackUpdateMessage msg;
    msg.messageId      = MSG_ID_TRACK_UPDATE;
    msg.trackId        = id_;
    msg.timestamp      = lastUpdateTime_;
    msg.status         = status_;
    msg.classification = classification_;

    auto sph = sphericalPosition();
    msg.range     = sph.range;
    msg.azimuth   = sph.azimuth;
    msg.elevation = sph.elevation;
    msg.rangeRate = rangeRate();

    auto pos = position();
    msg.x = pos.x;
    msg.y = pos.y;
    msg.z = pos.z;

    auto vel = velocity();
    msg.vx = vel.x;
    msg.vy = vel.y;
    msg.vz = vel.z;

    msg.trackQuality = quality_;
    msg.hitCount     = hitCount_;
    msg.missCount    = missCount_;
    msg.age          = age_;
    return msg;
}

} // namespace cuas
