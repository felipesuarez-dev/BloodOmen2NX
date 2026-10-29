#pragma once

#include <cstdint>
#include <vector>

#include "bo2/engine/big_archive.hpp"

namespace bo2::game {

// Movement fields of the `kain` tunedata entry (resources/levels/kain/kain.big).
// Offsets are into the data blob B; the file offset is 4 + the B offset.
// Layout and sources: docs/FORMATS/tunedata.md. The original copies some
// fields into a runtime tune object A (FUN_00428fe0) and its consumers read A;
// the turn rates and the run clip rate below are such fields.

// The `normalJump` block, 9 little-endian floats at B+0x9FC. FUN_00451d00
// builds the jump model from it. kaintune.h names the fields jAscentGravity,
// jDescentGravity, jFloatGravity, jNormalMaxV, jFloatMaxV, jMinHeight,
// jMaxHeight, jMinTime and jMaxTime (`jumpParams.normalJumpParams`).
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

// The tunedata blocks `jumpParams.forwardJumpParams` (B+0xA20),
// `sideJumpParams` (B+0xA38) and `backwardJumpParams` (B+0xA74), six floats
// each; the field names are those of kaintune.h in kain.big
// (`src/etemplates/kaintune.h`). FUN_00452030 turns one into the air steering
// of its jump direction.
struct AirSteerTuning {
    float min_dist = 0.0f;         // MinDist: horizontal distance of the shortest jump
    float max_dist = 0.0f;         // MaxDist: of the longest jump
    float min_time = 0.0f;         // MinTime: start of the extra-speed ramp, s after takeoff
    float max_time = 0.0f;         // MaxTime: end of the ramp
    float kd = 0.0f;               // Kd: percent of the velocity kept per tick with no stick
    float max_lateral_vel = 0.0f;  // MaxLateralVel: top speed on the other axes of this jump
};

struct KainTuning {
    float stick_deadzone = 0.0f;       // B+0x00C (FUN_00425520, FUN_0041ecf0)
    float jump_turn_rate = 0.0f;       // B+0x9DC jumpParams.jumpTurnRate, deg/s per unit stick in a forward or back jump (FUN_00441f20)
    JumpTuning jump;                   // B+0x9FC
    JumpTuning side_jump;              // B+0xA50 jumpParams.sideVertJumpParams, the vertical model of a sideways jump
    AirSteerTuning air_forward;        // B+0xA20
    AirSteerTuning air_side;           // B+0xA38
    AirSteerTuning air_back;           // B+0xA74
    float turn_rate_ready = 0.0f;      // B+0xAC8 -> A+0x18, rad/s (FUN_0041ecf0 gets it as an argument)
    float turn_rate_run = 0.0f;        // B+0xACC -> A+0x1C
    float turn_rate_walk_back = 0.0f;  // B+0xAD0 -> A+0x20
    float run_clip_rate = 0.0f;        // B+0xB14 -> A+0x40, run playback rate (FUN_00445ba0)
    // Root motion speeds in u/s, filled by apply_kain_clip_speeds from the
    // animreel entries of kain.big (docs/FORMATS/animreel.md). Both are
    // magnitudes of the clip's forward (local x) root speed.
    float run_root_speed = 0.0f;        // kain/run: 200
    float walk_back_root_speed = 0.0f;  // kain/walk_backwards: 62.5
};

// Values FUN_00451d00 derives from the jump block.
struct JumpDerived {
    float launch_speed = 0.0f;  // takeoff vz, sqrt(2 * h1 * |g_up|)
    float boost_accel = 0.0f;   // extra acceleration while the jump button is held
    float boost_start = 0.0f;   // boost window start (s after takeoff), clamped to the apex time
    float boost_end = 0.0f;     // boost window end (s after takeoff), solved by the iteration
    float min_airtime = 0.0f;   // tap jump: time up plus time down
    float max_airtime = 0.0f;   // held jump (apex h2): time up plus time down from h2
};

// What FUN_00452030 derives from an AirSteerTuning and the normalJump model.
struct AirSteerDerived {
    float launch_speed = 0.0f;  // speed at takeoff per unit stick: min_dist / min_airtime
    float accel = 0.0f;         // acceleration toward the stick target while airborne
    float peak_speed = 0.0f;    // top speed along this direction's own axis
    float damping = 0.0f;       // fraction of the velocity kept per tick with no stick
    float lateral_speed = 0.0f;  // top speed on the other axes (MaxLateralVel)
};

// `bytes` is the extracted tunedata entry: a u32 size field followed by the
// blob. Throws common::ParseError when the entry is truncated or the size
// field does not cover the movement fields.
KainTuning parse_kain_tuning(const std::vector<std::uint8_t>& bytes);

// Reads the root speeds from the `kain/run` and `kain/walk_backwards`
// animreel entries of `archive` (the same kain.big as the tunedata). Throws
// common::ParseError when an entry is missing or has no root motion.
void apply_kain_clip_speeds(KainTuning& tuning, const bo2::engine::BigArchive& archive);

// Throws std::invalid_argument when the gravities are not negative or the tap
// height is not positive.
JumpDerived derive_jump(const KainTuning& tuning);
// The same for any jump block, for example `side_jump`.
JumpDerived derive_jump(const JumpTuning& jump);

// Port of FUN_00452030. `normal` is the model of derive_jump(tuning); the
// original always passes the normalJump model, also for the side blocks.
AirSteerDerived derive_air_steer(const AirSteerTuning& block, const JumpDerived& normal);

}  // namespace bo2::game
