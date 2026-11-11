#include <cmath>
#include <iostream>
#include <stdexcept>

#include "bo2/game/replay.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

constexpr const char* kHeader =
    "frame,dt,phase,move_x,move_y,run,jump,attack,start_x,start_y,start_z,start_yaw_deg\n";

void test_reads_the_first_tick_and_the_spawn() {
    const std::string text = std::string(kHeader) +
                             "0,0,spawn_idle,0,0,0,0,0,-8.8,57.1,208,0\n"
                             "7,0.02,after_w,0,1,0,0,0,-8.8,57.1,208,0\n"
                             "8,0.02,after_w,0,1,1,0,1,0,0,0,0\n";
    const bo2::game::ReplayRoute route = bo2::game::parse_replay_csv(text);
    CHECK(std::fabs(route.start.x + 8.8f) < 1.0e-6f);
    CHECK(std::fabs(route.start.y - 57.1f) < 1.0e-3f);
    CHECK(std::fabs(route.start.z - 208.0f) < 1.0e-3f);
    CHECK(route.yaw_degrees == 0.0f);
    CHECK(route.ticks.size() == 3);
    CHECK(route.ticks[0].frame == 0);
    CHECK(route.ticks[1].frame == 7);
    CHECK(route.ticks[1].seconds == 0.02);
    CHECK(route.ticks[1].phase == "after_w");
    CHECK(route.ticks[1].move_y == 1.0f);
    CHECK(route.ticks[2].run);
    CHECK(route.ticks[2].attack);
}

void test_rejects_a_missing_column() {
    const std::string text = "frame,dt,phase,move_x,move_y,run,jump,attack\n0,0,spawn,0,0,0,0,0\n";
    bool threw = false;
    try {
        bo2::game::parse_replay_csv(text);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

void test_rejects_a_bad_value() {
    const std::string text = std::string(kHeader) +
                             "0,0,spawn,0,0,0,0,0,-8.8,57.1,208,0\n"
                             "1,oops,spawn,0,0,0,0,0,0,0,0,0\n";
    bool threw = false;
    try {
        bo2::game::parse_replay_csv(text);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    test_reads_the_first_tick_and_the_spawn();
    test_rejects_a_missing_column();
    test_rejects_a_bad_value();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "replay tests: ok\n";
    return 0;
}
