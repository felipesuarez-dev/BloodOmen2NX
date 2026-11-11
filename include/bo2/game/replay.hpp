#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/math.hpp"

namespace bo2::game {

struct ReplayTick {
    std::uint64_t frame = 0;
    double seconds = 0.0;
    std::string phase;
    float move_x = 0.0f;
    float move_y = 0.0f;
    bool run = false;
    bool jump = false;
    bool attack = false;
};

// One tick per row of the oracle state.csv. The first row also carries the
// spawn position and yaw sampled by the oracle, so the replay starts where
// the original was.
struct ReplayRoute {
    bo2::engine::Vec3 start{0.0f, 0.0f, 0.0f};
    float yaw_degrees = 0.0f;
    std::vector<ReplayTick> ticks;
};

// Reads the CSV produced by tools/oracle/prepare_replay.py:
// frame,dt,phase,move_x,move_y,run,jump,attack,start_x,start_y,start_z,start_yaw_deg
ReplayRoute parse_replay_csv(const std::string& text);

}  // namespace bo2::game
