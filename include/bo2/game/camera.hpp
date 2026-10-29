#pragma once

#include "bo2/engine/collision.hpp"
#include "bo2/game/player.hpp"

namespace bo2::game {

// The follow camera (FollowingCreatureCameraImpl, FUN_00408b10 and the
// helpers it calls). The numbers are the fields of the live CameraSetup that
// the Wine oracle read from the game's memory at the sl01 spawn
// (external/captures/wine_camera_1, docs/GAMEPLAY.md "Camera"); the code that
// builds that setup was not traced.
constexpr float kFollowRange = 192.0f;        // setup+0x18: distance from the pivot to the eye
constexpr float kFollowPitch = 0.10472f;      // setup+0x1c: 6 degrees, the eye sits this far above the pivot
constexpr float kFollowPivotHeight = 76.0f;   // setup+0x0c: the pivot is this far above Kain
// setup+0x30 is 150 rad/s2, the limit of the yaw catch-up (FUN_00407ec0). The
// function multiplies it by 5 (DAT_0083f718) when the input device's type
// field is 2; that type is assumed to be the keyboard, because with 150 the
// camera lags Kain by up to 5 degrees on a turn and the oracle measured 0.2
// degrees, while 750 stays within 1.6.
constexpr float kFollowYawAccel = 150.0f * 5.0f;
// The setup the camera gets at takeoff (FUN_004074c0), read live: delay
// setup+0x10 = 0.1 s, blend setup+0x14 = 0.6 s. The live follow object shows
// the blend progress as the time since takeoff over 0.6 s (follow+0xD4 equals
// follow+0xCC), and the delay field reads 0 from about 0.08 s on, so the hold
// before the blend is approximated by kJumpLagDelay. FUN_00407c70 itself sets
// +0xD4 = +0xCC - delay, which the live data does not show.
constexpr float kJumpLagDelay = 0.1f;  // measured approximation of the hold
constexpr float kJumpLagTime = 0.6f;   // setup+0x14

class FollowCamera {
public:
    FollowCamera() = default;

    // `seconds` advances the yaw catch-up and the height lag; zero or less
    // places the camera at once, which is what the first frame and a reload need.
    // The eye is pulled in along the line back from the pivot when the level
    // collision mesh is in the way. The pose follows Kain's yaw; the view pitch
    // is the fixed follow pitch plus the debug look offset in `player.pitch`.
    void update(const PlayerState& player, const bo2::engine::CollisionWorld& collision,
                double seconds = 0.0);
    const bo2::engine::Vec3& eye() const;
    // A point one follow range ahead of the eye along the view direction.
    const bo2::engine::Vec3& target() const;
    // The camera's own yaw (radians, unwrapped): it follows Kain's yaw with a limited acceleration.
    float yaw() const;
    // Height of the pivot above the world floor, which lags Kain for 0.6 s after a jump.
    float pivot_z() const;

private:
    bool placed_ = false;
    float yaw_ = 0.0f;
    float yaw_velocity_ = 0.0f;
    float pivot_z_ = 0.0f;
    // Height lag after a jump (FUN_00407c70).
    bool was_grounded_ = true;
    bool lag_waiting_ = false;   // +0xc8: the delay is running
    bool lag_running_ = false;   // +0xd0: the blend is running
    float lag_time_ = 0.0f;      // +0xcc: seconds since takeoff
    float lag_start_ = 0.0f;     // +0xbc: pivot height when the blend began
    float lag_velocity_ = 0.0f;  // +0xc0: slope of the blend, carried between frames
    bo2::engine::Vec3 eye_{0.0f, 0.0f, 0.0f};
    bo2::engine::Vec3 target_{0.0f, 0.0f, 0.0f};
};

}  // namespace bo2::game
