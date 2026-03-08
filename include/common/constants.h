#pragma once

/*
 * System-wide constants for the Counter-UAS radar tracker.
 *
 * Message-ID constants are derived from the IDL-generated CounterUAS
 * namespace.  Static asserts below will break the build if the IDL values
 * are changed without updating this header (or vice-versa).
 */

#include "messages.h"   // IDL-generated — CounterUAS::MSG_ID_* constants
#include <cstdint>

namespace cuas {

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------
static constexpr double PI      = 3.14159265358979323846;
static constexpr double DEG2RAD = PI / 180.0;
static constexpr double RAD2DEG = 180.0 / PI;

// ---------------------------------------------------------------------------
// Wire message IDs — values are authoritative in idl/messages.idl.
// These constants are kept here for backward compatibility in code that
// already uses the cuas:: namespace; they are derived directly from the
// IDL-generated values so they cannot drift.
// ---------------------------------------------------------------------------
static constexpr uint32_t MSG_ID_SP_DETECTION    = CounterUAS::MSG_ID_SP_DETECTION;
static constexpr uint32_t MSG_ID_TRACK_UPDATE    = CounterUAS::MSG_ID_TRACK_UPDATE;
static constexpr uint32_t MSG_ID_TRACK_TABLE     = CounterUAS::MSG_ID_TRACK_TABLE;
static constexpr uint32_t MSG_ID_CLUSTER_TABLE   = CounterUAS::MSG_ID_CLUSTER_TABLE;
static constexpr uint32_t MSG_ID_ASSOC_TABLE     = CounterUAS::MSG_ID_ASSOC_TABLE;
static constexpr uint32_t MSG_ID_PREDICTED_TABLE = CounterUAS::MSG_ID_PREDICTED_TABLE;

// Static asserts: if IDL values ever change these will catch it at build time.
static_assert(MSG_ID_SP_DETECTION    == 0x0001u, "IDL MSG_ID_SP_DETECTION mismatch");
static_assert(MSG_ID_TRACK_UPDATE    == 0x0002u, "IDL MSG_ID_TRACK_UPDATE mismatch");
static_assert(MSG_ID_TRACK_TABLE     == 0x0003u, "IDL MSG_ID_TRACK_TABLE mismatch");
static_assert(MSG_ID_CLUSTER_TABLE   == 0x0010u, "IDL MSG_ID_CLUSTER_TABLE mismatch");
static_assert(MSG_ID_ASSOC_TABLE     == 0x0011u, "IDL MSG_ID_ASSOC_TABLE mismatch");
static_assert(MSG_ID_PREDICTED_TABLE == 0x0012u, "IDL MSG_ID_PREDICTED_TABLE mismatch");

// ---------------------------------------------------------------------------
// Capacity limits (not wire-protocol, no IDL counterpart)
// ---------------------------------------------------------------------------
static constexpr int MAX_DETECTIONS_PER_DWELL = 256;
static constexpr int MAX_TRACKS               = 200;
static constexpr int IMM_NUM_MODELS           = 5;

// DDS topic names — must match the topic names used in publisher and subscriber
// create calls (dds_participant.h).
static constexpr const char* TOPIC_SP_DETECTION    = "SPDetection";
static constexpr const char* TOPIC_TRACK_TABLE     = "TrackTable";
static constexpr const char* TOPIC_CLUSTER_TABLE   = "ClusterTable";
static constexpr const char* TOPIC_ASSOC_TABLE     = "AssocTable";
static constexpr const char* TOPIC_PREDICTED_TABLE = "PredictedTable";

} // namespace cuas
