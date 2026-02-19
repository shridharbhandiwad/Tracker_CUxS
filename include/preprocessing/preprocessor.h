#pragma once

#include "common/types.h"
#include "common/config.h"
#include <vector>

namespace cuas {

class Preprocessor {
public:
    explicit Preprocessor(const PreprocessConfig& cfg);

    std::vector<Detection> process(const std::vector<Detection>& raw) const;
    uint64_t totalRejected() const { return rejected_; }
    void resetStats() { rejected_ = 0; }

private:
    bool isValid(const Detection& d) const;

    PreprocessConfig config_;
    mutable uint64_t rejected_ = 0;
};

} // namespace cuas
