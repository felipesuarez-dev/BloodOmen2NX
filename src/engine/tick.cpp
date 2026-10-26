#include "bo2/engine/tick.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bo2::engine {

FixedStep::FixedStep(double step_seconds, double max_frame_seconds)
    : step_seconds_(step_seconds), max_frame_seconds_(max_frame_seconds) {
    if (!std::isfinite(step_seconds_) || step_seconds_ <= 0.0) {
        throw std::invalid_argument("fixed step must be positive");
    }
    if (!std::isfinite(max_frame_seconds_) || max_frame_seconds_ < step_seconds_) {
        throw std::invalid_argument("max frame must be at least one fixed step");
    }
}

std::uint32_t FixedStep::advance(double elapsed_seconds,
                                 const std::function<void(double)>& update) {
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
        return 0;
    }
    accumulator_ += std::min(elapsed_seconds, max_frame_seconds_);
    std::uint32_t steps = 0;
    while (accumulator_ >= step_seconds_) {
        if (update) {
            update(step_seconds_);
        }
        accumulator_ -= step_seconds_;
        ++ticks_;
        ++steps;
    }
    return steps;
}

std::uint64_t FixedStep::ticks() const {
    return ticks_;
}

double FixedStep::accumulator() const {
    return accumulator_;
}

double FixedStep::step_seconds() const {
    return step_seconds_;
}

void FixedStep::reset() {
    accumulator_ = 0.0;
    ticks_ = 0;
}

}  // namespace bo2::engine
