#include "bo2/game/camera.hpp"

#include <algorithm>
#include <cmath>

namespace bo2::game {

FollowCamera::FollowCamera(float distance, float height)
    : distance_(std::max(0.1f, distance)), height_(height) {}

void FollowCamera::update(const PlayerState& player,
                          const bo2::engine::CollisionWorld& collision) {
    const float cosine_pitch = std::cos(player.pitch);
    const bo2::engine::Vec3 forward{cosine_pitch * std::cos(player.yaw),
                                    cosine_pitch * std::sin(player.yaw),
                                    std::sin(player.pitch)};
    target_ = player.position + bo2::engine::Vec3{0.0f, 0.0f, height_};
    eye_ = target_ - forward * distance_;
    const bo2::engine::Vec3 ray = eye_ - target_;
    if (bo2::engine::length(ray) < 1.0e-5f) {
        return;
    }
    const bo2::engine::Vec3 direction = bo2::engine::normalize(ray);
    const auto hit = collision.raycast(target_, direction, distance_);
    if (hit.has_value()) {
        eye_ = target_ + direction * std::max(0.05f, hit->distance - 0.2f);
    }
}

const bo2::engine::Vec3& FollowCamera::eye() const {
    return eye_;
}

const bo2::engine::Vec3& FollowCamera::target() const {
    return target_;
}

}  // namespace bo2::game
