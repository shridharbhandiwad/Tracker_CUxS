#include "preprocessing/preprocessor.h"
#include "common/logger.h"

namespace cuas {

Preprocessor::Preprocessor(const PreprocessConfig& cfg)
    : config_(cfg) {}

bool Preprocessor::isValid(const Detection& d) const {
    if (d.range < config_.minRange || d.range > config_.maxRange) return false;
    if (d.azimuth < config_.minAzimuth || d.azimuth > config_.maxAzimuth) return false;
    if (d.elevation < config_.minElevation || d.elevation > config_.maxElevation) return false;
    if (d.snr < config_.minSNR || d.snr > config_.maxSNR) return false;
    if (d.rcs < config_.minRCS || d.rcs > config_.maxRCS) return false;
    if (d.strength < config_.minStrength || d.strength > config_.maxStrength) return false;
    return true;
}

std::vector<Detection> Preprocessor::process(const std::vector<Detection>& raw) const {
    std::vector<Detection> filtered;
    filtered.reserve(raw.size());

    uint64_t rejectedInThisBatch = 0;

    for (const auto& d : raw) {
        if (isValid(d)) {
            filtered.push_back(d);
        } else {
            ++rejectedInThisBatch;
        }
    }

    rejected_ += rejectedInThisBatch;

    LOG_DEBUG("Preprocessor", "Input: %zu, Passed: %zu, Rejected: %lu",
              raw.size(), filtered.size(),
              static_cast<unsigned long>(rejectedInThisBatch));

    return filtered;
}

} // namespace cuas
