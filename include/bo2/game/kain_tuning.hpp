#pragma once

#include <cstdint>
#include <vector>

namespace bo2::game {

// Movement fields of the `kain` tunedata entry (resources/levels/kain/kain.big).
// Offsets are into the data blob B; the file offset is 4 + the B offset.
// Layout and sources: docs/FORMATS/tunedata.md. The original copies some
// fields into a runtime tune object A (FUN_00428fe0) and its consumers read A;
// the turn rates and the run clip rate below are such fields.

// The `normalJump` block, 9 little-endian floats at B+0x9FC. FUN_00451d00
// builds the jump model from it.
struct JumpTuning {
    float gravity_up = 0.0f;      // B+0x9FC, used while vz >= 0 (FUN_00442ae0)
    float gravity_down = 0.0f;    // B+0xA00, used once vz < 0
    float gravity_glide = 0.0f;   // B+0xA04, parsed only; glide is not implemented
    float terminal_fall = 0.0f;   // B+0xA08, fall speed cap (FUN_00441f20)
    float terminal_glide = 0.0f;  // B+0xA0C, parsed only
    float height_tap = 0.0f;      // B+0xA10, apex of a tapped jump
    float height_hold = 0.0f;     // B+0xA14, apex target of a held jump
    float boost_start = 0.0f;     // B+0xA18, hold boost window start after takeoff (s)
    float boost_end = 0.0f;       // B+0xA1C, hold boost window end after takeoff (s)
};

struct KainTuning {
    float stick_deadzone = 0.0f;       // B+0x00C (FUN_00425520, FUN_0041ecf0)
    JumpTuning jump;                   // B+0x9FC
    float turn_rate_ready = 0.0f;      // B+0xAC8 -> A+0x18, rad/s (FUN_0041ecf0 gets it as an argument)
    float turn_rate_run = 0.0f;        // B+0xACC -> A+0x1C
    float turn_rate_walk_back = 0.0f;  // B+0xAD0 -> A+0x20
    float run_clip_rate = 0.0f;        // B+0xB14 -> A+0x40 (read by FUN_00445ba0); parsed only
};

// Values FUN_00451d00 derives from the jump block.
struct JumpDerived {
    float launch_speed = 0.0f;  // takeoff vz, sqrt(2 * h1 * |g_up|)
    float boost_accel = 0.0f;   // extra acceleration while the jump button is held
    float boost_start = 0.0f;   // boost window start (s after takeoff), clamped to the apex time
    float boost_end = 0.0f;     // boost window end (s after takeoff), solved by the iteration
    float min_airtime = 0.0f;   // tap jump: time up plus time down
};

// `bytes` is the extracted tunedata entry: a u32 size field followed by the
// blob. Throws common::ParseError when the entry is truncated or the size
// field does not cover the movement fields.
KainTuning parse_kain_tuning(const std::vector<std::uint8_t>& bytes);

// Throws std::invalid_argument when the gravities are not negative or the tap
// height is not positive.
JumpDerived derive_jump(const KainTuning& tuning);

}  // namespace bo2::game
