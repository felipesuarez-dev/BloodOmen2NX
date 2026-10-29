#pragma once

#include "bo2/engine/collision.hpp"
#include "bo2/game/player.hpp"

namespace bo2::game {

// Follow camera pose relative to Kain, measured in the Wine oracle
// (external/captures/wine_20261029_0052) once the camera has caught up. The
// scripted CameraSetup range is 192 (follow+0x50, FUN_004074c0); the measured
// pose sits at 190.9. The original camera lags Kain's yaw (at the end of the
// turn-left phase it was 117 back and 88 high); this port camera is rigid and
// always sits at the caught-up offsets.
constexpr float kFollowDistance = 190.9f;  // behind Kain along his yaw
constexpr float kFollowHeight = 96.1f;     // above Kain's position
constexpr float kFollowPitch = -6.0f * 3.14159265358979f / 180.0f;  // looking slightly down

class FollowCamera {
public:
    explicit FollowCamera(float distance = kFollowDistance, float height = kFollowHeight);

    // The eye is pulled in along the line back from Kain when the level
    // collision mesh is in the way. The pose follows the yaw only; the pitch
    // is the fixed follow pitch plus the debug look offset in `player.pitch`.
    void update(const PlayerState& player, const bo2::engine::CollisionWorld& collision);
    const bo2::engine::Vec3& eye() const;
    // A point one follow distance ahead of the eye along the view direction.
    const bo2::engine::Vec3& target() const;

private:
    float distance_;
    float height_;
    bo2::engine::Vec3 eye_{0.0f, 0.0f, 0.0f};
    bo2::engine::Vec3 target_{0.0f, 0.0f, 0.0f};
};

}  // namespace bo2::game
