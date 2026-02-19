#pragma once

#include <cstdint>

namespace cuas {

static constexpr double PI = 3.14159265358979323846;
static constexpr double DEG2RAD = PI / 180.0;
static constexpr double RAD2DEG = 180.0 / PI;

static constexpr uint32_t MSG_ID_SP_DETECTION = 0x0001;
static constexpr uint32_t MSG_ID_TRACK_UPDATE = 0x0002;
static constexpr uint32_t MSG_ID_TRACK_TABLE  = 0x0003;

static constexpr int MAX_DETECTIONS_PER_DWELL = 256;
static constexpr int MAX_TRACKS = 200;
static constexpr int IMM_NUM_MODELS = 5;

} // namespace cuas
