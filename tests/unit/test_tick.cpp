#include <cmath>
#include <cstdint>
#include <iostream>

#include "bo2/engine/tick.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

void test_whole_steps() {
    bo2::engine::FixedStep loop(0.01);
    std::uint32_t updates = 0;
    const std::uint32_t steps = loop.advance(0.1, [&updates](double step) {
        CHECK(std::fabs(step - 0.01) < 1.0e-9);
        ++updates;
    });
    CHECK(steps == 10);
    CHECK(updates == 10);
    CHECK(loop.ticks() == 10);
}

void test_fractional_step() {
    bo2::engine::FixedStep loop(0.01);
    std::uint32_t updates = 0;
    CHECK(loop.advance(0.015, [&updates](double) { ++updates; }) == 1);
    CHECK(updates == 1);
    CHECK(std::fabs(loop.accumulator() - 0.005) < 1.0e-9);
}

void test_clamps_large_frames() {
    bo2::engine::FixedStep loop(0.01, 0.25);
    std::uint32_t updates = 0;
    const std::uint32_t steps = loop.advance(10.0, [&updates](double) { ++updates; });
    CHECK(steps >= 24);
    CHECK(steps <= 25);
    CHECK(updates == steps);
}

void test_reset_and_invalid() {
    bo2::engine::FixedStep loop(0.02);
    loop.advance(0.04, {});
    loop.reset();
    CHECK(loop.ticks() == 0);
    CHECK(loop.accumulator() == 0.0);
    bool threw = false;
    try {
        bo2::engine::FixedStep invalid(0.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    test_whole_steps();
    test_fractional_step();
    test_clamps_large_frames();
    test_reset_and_invalid();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "tick tests: ok\n";
    return 0;
}
