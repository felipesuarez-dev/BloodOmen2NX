#pragma once

#include <cstdint>
#include <functional>

namespace bo2::engine {

class FixedStep {
public:
    explicit FixedStep(double step_seconds, double max_frame_seconds = 0.25);

    std::uint32_t advance(double elapsed_seconds, const std::function<void(double)>& update);
    std::uint64_t ticks() const;
    double accumulator() const;
    double step_seconds() const;
    void reset();

private:
    double step_seconds_;
    double max_frame_seconds_;
    double accumulator_ = 0.0;
    std::uint64_t ticks_ = 0;
};

}  // namespace bo2::engine
