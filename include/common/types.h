#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <array>

namespace cuas {

using Timestamp = uint64_t; // microseconds since epoch

inline Timestamp nowMicros() {
    auto tp = std::chrono::high_resolution_clock::now();
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            tp.time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// Detection from DSP
// ---------------------------------------------------------------------------
struct Detection {
    double range      = 0.0;  // meters
    double azimuth    = 0.0;  // radians
    double elevation  = 0.0;  // radians
    double strength   = 0.0;  // dBm
    double noise      = 0.0;  // dBm
    double snr        = 0.0;  // dB
    double rcs        = 0.0;  // dBsm
    double microDoppler = 0.0; // Hz
};

struct SPDetectionMessage {
    uint32_t  messageId     = 0;
    uint32_t  dwellCount    = 0;
    Timestamp timestamp     = 0;
    uint32_t  numDetections = 0;
    std::vector<Detection> detections;
};

// ---------------------------------------------------------------------------
// Cartesian position / state
// ---------------------------------------------------------------------------
struct CartesianPos {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SphericalPos {
    double range     = 0.0;
    double azimuth   = 0.0;
    double elevation = 0.0;
};

inline CartesianPos sphericalToCartesian(double r, double az, double el) {
    CartesianPos c;
    c.x = r * std::cos(el) * std::cos(az);
    c.y = r * std::cos(el) * std::sin(az);
    c.z = r * std::sin(el);
    return c;
}

inline SphericalPos cartesianToSpherical(double x, double y, double z) {
    SphericalPos s;
    s.range     = std::sqrt(x * x + y * y + z * z);
    s.azimuth   = std::atan2(y, x);
    s.elevation = (s.range > 1e-9) ? std::asin(z / s.range) : 0.0;
    return s;
}

// ---------------------------------------------------------------------------
// Cluster: centroided group of detections
// ---------------------------------------------------------------------------
struct Cluster {
    uint32_t clusterId = 0;
    double range       = 0.0;
    double azimuth     = 0.0;
    double elevation   = 0.0;
    double strength    = 0.0;
    double snr         = 0.0;
    double rcs         = 0.0;
    double microDoppler = 0.0;
    uint32_t numDetections = 0;
    CartesianPos cartesian;
    std::vector<uint32_t> detectionIndices;
};

// ---------------------------------------------------------------------------
// IMM state: 9-dimensional [x, vx, ax, y, vy, ay, z, vz, az]
// ---------------------------------------------------------------------------
static constexpr int STATE_DIM = 9;
static constexpr int MEAS_DIM  = 3; // range, azimuth, elevation (converted to x,y,z)

using StateVector    = std::array<double, STATE_DIM>;
using StateMatrix    = std::array<std::array<double, STATE_DIM>, STATE_DIM>;
using MeasVector     = std::array<double, MEAS_DIM>;
using MeasMatrix     = std::array<std::array<double, MEAS_DIM>, MEAS_DIM>;
using MeasStateMatrix = std::array<std::array<double, STATE_DIM>, MEAS_DIM>;

inline StateVector stateZero() {
    StateVector v{};
    v.fill(0.0);
    return v;
}

inline StateMatrix matZero() {
    StateMatrix m{};
    for (auto& row : m) row.fill(0.0);
    return m;
}

inline StateMatrix matIdentity() {
    StateMatrix m = matZero();
    for (int i = 0; i < STATE_DIM; ++i) m[i][i] = 1.0;
    return m;
}

// ---------------------------------------------------------------------------
// Track status and classification
// ---------------------------------------------------------------------------
enum class TrackStatus : uint32_t {
    Tentative  = 0,
    Confirmed  = 1,
    Coasting   = 2,
    Deleted    = 3
};

enum class TrackClassification : uint32_t {
    Unknown        = 0,
    DroneRotary    = 1,
    DroneFixedWing = 2,
    Bird           = 3,
    Clutter        = 4
};

// ---------------------------------------------------------------------------
// Track update sent to display
// ---------------------------------------------------------------------------
struct TrackUpdateMessage {
    uint32_t            messageId      = 0x0002;
    uint32_t            trackId        = 0;
    Timestamp           timestamp      = 0;
    TrackStatus         status         = TrackStatus::Tentative;
    TrackClassification classification = TrackClassification::Unknown;
    double              range          = 0.0;
    double              azimuth        = 0.0;
    double              elevation      = 0.0;
    double              rangeRate      = 0.0;
    double              x = 0.0, y = 0.0, z = 0.0;
    double              vx = 0.0, vy = 0.0, vz = 0.0;
    double              trackQuality   = 0.0;
    uint32_t            hitCount       = 0;
    uint32_t            missCount      = 0;
    uint32_t            age            = 0;
};

// ---------------------------------------------------------------------------
// Clustering method enum
// ---------------------------------------------------------------------------
enum class ClusterMethod {
    DBSCAN,
    RangeBased,
    RangeStrengthBased
};

// ---------------------------------------------------------------------------
// Association method enum
// ---------------------------------------------------------------------------
enum class AssociationMethod {
    Mahalanobis,
    GNN,
    JPDA
};

// ---------------------------------------------------------------------------
// Log record type
// ---------------------------------------------------------------------------
enum class LogRecordType : uint32_t {
    RawDetection   = 0,
    Preprocessed   = 1,
    Clustered      = 2,
    Predicted      = 3,
    Associated     = 4,
    TrackInitiated = 5,
    TrackUpdated   = 6,
    TrackDeleted   = 7,
    TrackSent      = 8
};

static constexpr uint32_t LOG_MAGIC = 0xCAFEBABE;

#pragma pack(push, 1)
struct LogRecordHeader {
    uint32_t  magic       = LOG_MAGIC;
    uint32_t  recordType  = 0;
    Timestamp timestamp   = 0;
    uint32_t  payloadSize = 0;
};
#pragma pack(pop)

} // namespace cuas
