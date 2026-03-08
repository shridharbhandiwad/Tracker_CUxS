#pragma once

/*
 * Internal algorithmic types for the Counter-UAS radar tracker pipeline.
 *
 * WIRE TYPES ARE NOT DEFINED HERE.  All message types that cross a process
 * boundary (DDS topics) are defined exclusively in idl/messages.idl and
 * generated into <build>/generated/messages.h by fastddsgen at build time.
 *
 * This header:
 *   - Includes the IDL-generated header so downstream code only needs one
 *     include.
 *   - Re-exports commonly-used IDL types into the cuas namespace as aliases
 *     so existing pipeline code compiles unchanged.
 *   - Provides static_assert guards that will break the build if any
 *     hand-maintained enum value drifts away from the IDL definition.
 *   - Defines internal types (CartesianPos, Cluster, StateVector, …) that
 *     are never serialised over a DDS topic and therefore do not belong in
 *     the IDL.
 */

#include "messages.h"   // IDL-generated: CounterUAS namespace

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
// Log-framing constants — values are authoritative in the IDL.
// The static_asserts below will fail to compile if anyone edits the IDL and
// forgets to update something (or vice-versa).
// ---------------------------------------------------------------------------
static constexpr uint32_t LOG_MAGIC       = CounterUAS::LOG_MAGIC;
static constexpr uint32_t LOG_EOM         = CounterUAS::LOG_EOM;
static constexpr uint32_t LOG_MAX_PAYLOAD = 64u * 1024u * 1024u;  // 64 MiB guard

// ---------------------------------------------------------------------------
// Binary log record wire layout (little-endian, #pragma pack enforced)
//
//  ┌─────────────────────────────────────────────────────┐
//  │  SOM   magic       (4 B)  = LOG_MAGIC  0xCAFEBABE  │
//  │        recordType  (4 B)  LogRecordType enum value  │
//  │        timestamp   (8 B)  microseconds since epoch  │
//  │        payloadSize (4 B)  bytes that follow          │
//  ├─────────────────────────────────────────────────────┤
//  │  PAYLOAD           (payloadSize bytes)               │
//  ├─────────────────────────────────────────────────────┤
//  │  EOM   eom         (4 B)  = LOG_EOM    0xDEADBEEF  │
//  └─────────────────────────────────────────────────────┘
//
// The struct must stay exactly 20 bytes.  The IDL defines the same layout;
// the packed struct here is needed because file-I/O uses memcpy/fwrite, not
// CDR serialization.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct LogRecordHeader {
    uint32_t  magic       = LOG_MAGIC;
    uint32_t  recordType  = 0;
    Timestamp timestamp   = 0;
    uint32_t  payloadSize = 0;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// LogRecordType — scoped enum class with readable names; underlying values
// are pinned to the IDL-generated plain-enum values via static_asserts.
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
    TrackSent      = 8,
    RunInfo        = 9
};

static_assert(static_cast<uint32_t>(LogRecordType::RawDetection)   == CounterUAS::LOG_RAW_DETECTION,   "LogRecordType::RawDetection out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::Preprocessed)   == CounterUAS::LOG_PREPROCESSED,    "LogRecordType::Preprocessed out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::Clustered)      == CounterUAS::LOG_CLUSTERED,       "LogRecordType::Clustered out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::Predicted)      == CounterUAS::LOG_PREDICTED,       "LogRecordType::Predicted out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::Associated)     == CounterUAS::LOG_ASSOCIATED,      "LogRecordType::Associated out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::TrackInitiated) == CounterUAS::LOG_TRACK_INITIATED, "LogRecordType::TrackInitiated out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::TrackUpdated)   == CounterUAS::LOG_TRACK_UPDATED,   "LogRecordType::TrackUpdated out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::TrackDeleted)   == CounterUAS::LOG_TRACK_DELETED,   "LogRecordType::TrackDeleted out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::TrackSent)      == CounterUAS::LOG_TRACK_SENT,      "LogRecordType::TrackSent out of sync with IDL");
static_assert(static_cast<uint32_t>(LogRecordType::RunInfo)        == CounterUAS::LOG_RUN_INFO,        "LogRecordType::RunInfo out of sync with IDL");

// ---------------------------------------------------------------------------
// TrackStatus and TrackClassification — re-export IDL-generated plain enums
// as type aliases into the cuas namespace for pipeline code.
// The IDL enum values (TRACK_TENTATIVE etc.) are used directly; scoped-enum
// wrappers with friendly names are provided as convenience aliases.
// ---------------------------------------------------------------------------
using TrackStatus         = CounterUAS::TrackStatus;
using TrackClassification = CounterUAS::TrackClassification;

// Convenience aliases that mirror the old scoped-enum style:
//   cuas::TrackStatus::Tentative  →  CounterUAS::TRACK_TENTATIVE
namespace TrackStatusVal {
    static constexpr TrackStatus Tentative  = CounterUAS::TRACK_TENTATIVE;
    static constexpr TrackStatus Confirmed  = CounterUAS::TRACK_CONFIRMED;
    static constexpr TrackStatus Coasting   = CounterUAS::TRACK_COASTING;
    static constexpr TrackStatus Deleted    = CounterUAS::TRACK_DELETED;
}
namespace TrackClassVal {
    static constexpr TrackClassification Unknown       = CounterUAS::CLASS_UNKNOWN;
    static constexpr TrackClassification DroneRotary   = CounterUAS::CLASS_DRONE_ROTARY;
    static constexpr TrackClassification DroneFixedWing= CounterUAS::CLASS_DRONE_FIXED_WING;
    static constexpr TrackClassification Bird          = CounterUAS::CLASS_BIRD;
    static constexpr TrackClassification Clutter       = CounterUAS::CLASS_CLUTTER;
}

// ---------------------------------------------------------------------------
// Detection — internal representation used throughout the pipeline.
// Fields match IDL's DetectionData 1-for-1; the static_asserts below ensure
// any field reordering in the IDL is caught at compile time.
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

// Compile-time size guard: IDL DetectionData must stay 8 doubles.
static_assert(sizeof(Detection) == 8 * sizeof(double),
              "cuas::Detection size mismatch — check IDL DetectionData");

// Conversion helpers between DDS wire type and internal type.
inline Detection toInternal(const CounterUAS::DetectionData& d) {
    return {d.range(), d.azimuth(), d.elevation(), d.strength(),
            d.noise(), d.snr(), d.rcs(), d.microDoppler()};
}
inline CounterUAS::DetectionData toIDL(const Detection& d) {
    CounterUAS::DetectionData r;
    r.range(d.range);  r.azimuth(d.azimuth);  r.elevation(d.elevation);
    r.strength(d.strength); r.noise(d.noise); r.snr(d.snr);
    r.rcs(d.rcs); r.microDoppler(d.microDoppler);
    return r;
}

// SPDetectionMessage — internal envelope (mirrors IDL SPDetectionMessage).
struct SPDetectionMessage {
    uint32_t  messageId     = 0;
    uint32_t  dwellCount    = 0;
    Timestamp timestamp     = 0;
    uint32_t  numDetections = 0;
    std::vector<Detection> detections;
};

// Conversion from IDL wire type to internal.
inline SPDetectionMessage toInternal(const CounterUAS::SPDetectionMessage& m) {
    SPDetectionMessage r;
    r.messageId     = m.messageId();
    r.dwellCount    = m.dwellCount();
    r.timestamp     = m.timestamp();
    r.numDetections = m.numDetections();
    r.detections.reserve(m.detections().size());
    for (const auto& d : m.detections())
        r.detections.push_back(toInternal(d));
    return r;
}

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
// Cluster: centroided group of detections (internal pipeline type)
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
static constexpr int MEAS_DIM  = 3;

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
// Clustering and association method enums (config-only, not wire types)
// ---------------------------------------------------------------------------
enum class ClusterMethod {
    DBSCAN,
    RangeBased,
    RangeStrengthBased
};

enum class AssociationMethod {
    Mahalanobis,
    GNN,
    JPDA
};

} // namespace cuas
