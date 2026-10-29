#include "bo2/game/camera.hpp"

#include <algorithm>
#include <cmath>

namespace bo2::game {

FollowCamera::FollowCamera(float distance, float height)
    : distance_(std::max(0.1f, distance)), height_(height) {}

void FollowCamera::update(const PlayerState& player,
                          const bo2::engine::CollisionWorld& collision) {
    const bo2::engine::Vec3 heading{std::cos(player.yaw), std::sin(player.yaw), 0.0f};
    const bo2::engine::Vec3 back = heading * -1.0f;
    // Eye = position - heading * distance + up * height.
    const bo2::engine::Vec3 pivot = player.position + bo2::engine::Vec3{0.0f, 0.0f, height_};
    float reach = distance_;
    const auto hit = collision.raycast(pivot, back, distance_);
    if (hit.has_value()) {
        reach = std::max(0.05f, hit->distance - 0.2f);
    }
    eye_ = pivot + back * reach;

    const float pitch = kFollowPitch + player.pitch;
    const bo2::engine::Vec3 view{std::cos(pitch) * heading.x, std::cos(pitch) * heading.y,
                                 std::sin(pitch)};
    target_ = eye_ + view * distance_;
}

const bo2::engine::Vec3& FollowCamera::eye() const {
    return eye_;
}

const bo2::engine::Vec3& FollowCamera::target() const {
    return target_;
}

}  // namespace bo2::game
