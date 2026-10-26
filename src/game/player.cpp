#include "bo2/game/player.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bo2::game {

namespace {

constexpr float kWalkSpeed = 3.0f;
constexpr float kRunSpeed = 6.0f;
constexpr float kGravity = 20.0f;
constexpr float kJumpSpeed = 6.0f;
constexpr float kEyeHeight = 1.7f;
constexpr float kBodyRadius = 0.4f;
constexpr float kBodyClearance = 0.05f;
constexpr float kLookSpeed = 2.0f;

}  // namespace

PlayerController::PlayerController(bo2::engine::CollisionWorld& collision,
                                   const bo2::engine::Vec3& start)
    : collision_(collision) {
    state_.position = start;
}

void PlayerController::step(const PlayerInput& input, double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return;
    }
    const float dt = static_cast<float>(seconds);
    state_.yaw -= input.look_x * kLookSpeed * dt;
    state_.pitch += input.look_y * kLookSpeed * dt;
    state_.pitch = std::clamp(state_.pitch, -1.2f, 1.2f);

    const float cosine = std::cos(state_.yaw);
    const float sine = std::sin(state_.yaw);
    const bo2::engine::Vec3 forward{cosine, sine, 0.0f};
    const bo2::engine::Vec3 right{-sine, cosine, 0.0f};
    bo2::engine::Vec3 movement =
        forward * input.move_y + right * input.move_x;
    if (bo2::engine::length(movement) > 1.0f) {
        movement = bo2::engine::normalize(movement);
    }
    const float speed = input.run ? kRunSpeed : kWalkSpeed;
    const bo2::engine::Vec3 next = state_.position + movement * (speed * dt);
    const bo2::engine::Vec3 body_center =
        next + bo2::engine::Vec3{0.0f, 0.0f, kBodyRadius + kBodyClearance};
    if (!collision_.intersects_sphere(body_center, kBodyRadius)) {
        state_.position = next;
    }

    const bo2::engine::Vec3 ray_origin =
        state_.position + bo2::engine::Vec3{0.0f, 0.0f, kEyeHeight};
    const auto ground = collision_.raycast(ray_origin, {0.0f, 0.0f, -1.0f}, kEyeHeight + 1.0f);
    if (input.jump && state_.grounded) {
        state_.vertical_velocity = kJumpSpeed;
        state_.grounded = false;
    }
    state_.vertical_velocity -= kGravity * dt;
    state_.position.z += state_.vertical_velocity * dt;
    if (ground.has_value() && state_.position.z <= ground->point.z) {
        state_.position.z = ground->point.z;
        state_.vertical_velocity = 0.0f;
        state_.grounded = true;
    } else {
        state_.grounded = false;
    }
}

void PlayerController::look(float yaw_delta, float pitch_delta) {
    state_.yaw += yaw_delta;
    state_.pitch = std::clamp(state_.pitch + pitch_delta, -1.2f, 1.2f);
}

const PlayerState& PlayerController::state() const {
    return state_;
}

bo2::engine::Vec3 PlayerController::camera_eye() const {
    return state_.position + bo2::engine::Vec3{0.0f, 0.0f, kEyeHeight};
}

bo2::engine::Vec3 PlayerController::camera_target() const {
    const float cosine_pitch = std::cos(state_.pitch);
    const bo2::engine::Vec3 direction{cosine_pitch * std::cos(state_.yaw),
                                      cosine_pitch * std::sin(state_.yaw),
                                      std::sin(state_.pitch)};
    return camera_eye() + bo2::engine::normalize(direction) * 10.0f;
}

}  // namespace bo2::game
