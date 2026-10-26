#pragma once

#include "bo2/engine/collision.hpp"
#include "bo2/game/player.hpp"

namespace bo2::game {

class FollowCamera {
public:
    explicit FollowCamera(float distance = 4.0f, float height = 1.7f);

    void update(const PlayerState& player, const bo2::engine::CollisionWorld& collision);
    const bo2::engine::Vec3& eye() const;
    const bo2::engine::Vec3& target() const;

private:
    float distance_;
    float height_;
    bo2::engine::Vec3 eye_{0.0f, 0.0f, 0.0f};
    bo2::engine::Vec3 target_{0.0f, 0.0f, 0.0f};
};

}  // namespace bo2::game
