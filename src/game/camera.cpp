#include "bo2/game/camera.hpp"

#include <algorithm>
#include <cmath>

namespace bo2::game {

namespace {

constexpr float kPi = 3.14159265358979f;
// FUN_00713450 and FUN_00713810 treat a duration under 0.01 s as no motion at all.
constexpr float kMinDuration = 0.01f;

// The smallest duration of the cubic Hermite blend from (0, v0) to (delta, v1)
// whose acceleration at both ends stays within +-accel. Port of FUN_00713450:
// the end accelerations are 2 a2 and 2 a2 + 6 a3 T, and setting each to +-accel
// gives four quadratics in T. The duration is the largest of their chosen roots
// (the smallest non-negative one of each pair).
float hermite_duration(float delta, float v0, float v1, float accel) {
    const float start = v0 + v0 + v1;
    const float end = -(v1 + v1 + v0);
    float duration = 0.0f;
    const auto consider = [&](float b, float sign, float limit) {
        const float discriminant = b * b + sign * delta * limit * 6.0f;  // DAT_00841cbc = 6
        if (!(discriminant >= 0.0f)) {
            return;
        }
        const float root = std::sqrt(discriminant);
        const float inverse = 1.0f / limit;
        const float first = (root - b) * inverse;
        const float second = (-b - root) * inverse;
        if (!(first >= 0.0f || second >= 0.0f)) {
            return;
        }
        const float chosen =
            (first < 0.0f || second < 0.0f) ? std::max(first, second) : std::min(first, second);
        duration = std::max(duration, chosen);
    };
    consider(start, 1.0f, accel);
    consider(start, 1.0f, -accel);
    consider(end, -1.0f, accel);
    consider(end, -1.0f, -accel);
    return duration;
}

// The cubic Hermite blend of FUN_00713360 / FUN_007133a0 with the evaluators
// FUN_00713810 (position) and FUN_00713870 (velocity).
struct Hermite {
    float duration;
    float p0;
    float p1;
    float v0;
    float v1;
    float a2 = 0.0f;
    float a3 = 0.0f;

    Hermite(float duration_, float p0_, float v0_, float p1_, float v1_)
        : duration(duration_), p0(p0_), p1(p1_), v0(v0_), v1(v1_) {
        if (std::fabs(duration) >= kMinDuration) {
            const float inverse = 1.0f / duration;
            const float slope = inverse * (p1 - p0);
            a3 = ((v1 + v0) - (slope + slope)) * inverse * inverse;
            a2 = (slope * 3.0f - (v0 + v0 + v1)) * inverse;
        }
    }

    float position(float t) const {
        if (std::fabs(duration) < kMinDuration) {
            return t < 0.0f ? p0 : p1;
        }
        return ((a3 * t + a2) * t + v0) * t + p0;
    }

    float velocity(float t) const {
        if (std::fabs(duration) < kMinDuration) {
            return t < 0.0f ? v0 : v1;
        }
        return (a2 + a2 + t * a3 * 3.0f) * t + v0;
    }
};

// FUN_00694400: the camera yaw written as the angle closest to the target.
float closest_angle(float angle, float target) {
    return target + std::remainder(angle - target, 2.0f * kPi);
}

}  // namespace

void FollowCamera::update(const PlayerState& player, const bo2::engine::CollisionWorld& collision,
                          double seconds) {
    const bool step = placed_ && std::isfinite(seconds) && seconds > 0.0;
    const float dt = step ? static_cast<float>(std::min(seconds, 0.1)) : 0.0f;
    const float target_yaw = player.yaw;
    const float target_pivot = player.position.z + kFollowPivotHeight;

    if (!step) {
        // First frame or a reload: the camera starts where it belongs.
        yaw_ = target_yaw;
        yaw_velocity_ = 0.0f;
        pivot_z_ = target_pivot;
        lag_waiting_ = false;
        lag_running_ = false;
        lag_velocity_ = 0.0f;
        placed_ = true;
    } else {
        // Yaw, FUN_00407ec0: a Hermite blend from the camera's yaw and yaw
        // speed to Kain's yaw at rest. Each frame advances it by min(dt, duration).
        const float from = closest_angle(yaw_, target_yaw);
        const float duration = hermite_duration(target_yaw - from, yaw_velocity_, 0.0f, kFollowYawAccel);
        const Hermite blend(duration, from, yaw_velocity_, target_yaw, 0.0f);
        const float advance = std::min(dt, duration);
        yaw_ = blend.position(advance);
        yaw_velocity_ = blend.velocity(advance);

        // Height, FUN_00407c70. A jump gives the camera a setup with a delay and
        // a blend time (FUN_004074c0). The pivot holds its height for about the
        // delay, then blends toward Kain; the progress is the time since takeoff
        // over the blend time, as measured in the live follow object, so the
        // blend ends 0.6 s after takeoff (see kJumpLagDelay).
        if (was_grounded_ && !player.grounded && player.vertical_velocity > 0.0f) {
            lag_waiting_ = true;
            lag_running_ = false;
            lag_time_ = 0.0f;
            lag_velocity_ = 0.0f;
        }
        if (lag_waiting_ || lag_running_) {
            lag_time_ += dt;
        }
        if (lag_waiting_ && kJumpLagDelay < lag_time_) {
            lag_waiting_ = false;
            lag_running_ = true;
            lag_start_ = pivot_z_;
        }
        if (lag_running_) {
            const float progress = lag_time_ / kJumpLagTime;
            if (progress >= 1.0f) {
                lag_running_ = false;
            } else {
                const Hermite height(1.0f, 0.0f, lag_velocity_, 1.0f, 0.0f);
                const float blend_weight = height.position(progress);
                pivot_z_ = (1.0f - blend_weight) * lag_start_ + blend_weight * target_pivot;
                pivot_z_ = std::max(pivot_z_, player.position.z);  // never below Kain's feet
                lag_velocity_ = height.velocity(progress);
            }
        }
        if (!lag_waiting_ && !lag_running_) {
            pivot_z_ = target_pivot;
            lag_velocity_ = 0.0f;
        }
    }
    was_grounded_ = player.grounded;

    // Pose, FUN_00408b10: the eye is `kFollowRange` from the pivot, behind the
    // camera yaw and raised by the follow pitch, and looks back down at the pivot.
    const bo2::engine::Vec3 heading{std::cos(yaw_), std::sin(yaw_), 0.0f};
    const bo2::engine::Vec3 pivot{player.position.x, player.position.y, pivot_z_};
    const float elevation = kFollowPitch;
    const bo2::engine::Vec3 away{-std::cos(elevation) * heading.x, -std::cos(elevation) * heading.y,
                                 std::sin(elevation)};
    float reach = kFollowRange;
    const auto hit = collision.raycast(pivot, away, kFollowRange);
    if (hit.has_value()) {
        reach = std::max(0.05f, hit->distance - 0.2f);
    }
    eye_ = pivot + away * reach;

    const float pitch = -elevation + player.pitch;
    const bo2::engine::Vec3 view{std::cos(pitch) * heading.x, std::cos(pitch) * heading.y,
                                 std::sin(pitch)};
    target_ = eye_ + view * kFollowRange;
}

const bo2::engine::Vec3& FollowCamera::eye() const {
    return eye_;
}

const bo2::engine::Vec3& FollowCamera::target() const {
    return target_;
}

float FollowCamera::yaw() const {
    return yaw_;
}

float FollowCamera::pivot_z() const {
    return pivot_z_;
}

}  // namespace bo2::game
