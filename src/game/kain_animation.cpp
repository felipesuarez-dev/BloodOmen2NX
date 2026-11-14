#include "bo2/game/kain_animation.hpp"

#include <cmath>

namespace bo2::game {

namespace {

constexpr float kTakeoffTime = 0.2f;  // 0x3e4ccccd, the timer FUN_004436e0 sets

}  // namespace

KainAnimation::KainAnimation(const bo2::engine::AnimGraph& graph, const KainTuning& tuning)
    : layers_(graph), deadzone_(tuning.stick_deadzone), run_rate_(tuning.run_clip_rate) {
    const JumpTuning& jump = tuning.jump;
    if (jump.gravity_up < 0.0f && jump.gravity_down < 0.0f && jump.height_tap > 0.0f) {
        const JumpDerived derived = derive_jump(jump);
        has_boost_ = true;
        boost_start_ = derived.boost_start;
        boost_end_ = derived.boost_end;
    }
}

void KainAnimation::request(std::int32_t state) {
    // Every state enter stores the index and starts the state on layer 0.
    state_ = state;
    layers_.start_state(0, state);
}

void KainAnimation::step(const PlayerInput& input, const PlayerState& state, double seconds) {
    const bool forward = input.move_y > deadzone_;
    const bool back = input.move_y < -deadzone_;
    const float dt = static_cast<float>(seconds);
    if (!state.grounded) {
        landing_ = false;
        if (was_grounded_ && state.vertical_velocity > 0.0f) {
            // Leaving the ground upwards is a jump; a fall has no takeoff.
            request(kain_anim::kStartJump);
            takeoff_left_ = kTakeoffTime;
            air_time_ = 0.0f;
            jump_released_ = !input.jump;
        } else {
            if (takeoff_left_ > 0.0f) {
                takeoff_left_ -= dt;
                air_time_ += dt;
                if (!input.jump) {
                    jump_released_ = true;
                }
            }
            // FUN_00443a50: the boost timer has run out and the boost is over
            // (cancelled by the released button, or past its end).
            const bool boost_over = has_boost_ && air_time_ >= boost_start_ &&
                                    (jump_released_ || air_time_ >= boost_end_);
            if (takeoff_left_ <= 0.0f || state.vertical_velocity <= 0.0f || boost_over) {
                takeoff_left_ = 0.0f;
                const std::int32_t wanted = state.vertical_velocity < 0.0f
                                                ? kain_anim::kFallingDown
                                                : kain_anim::kFallingUp;
                if (state_ != wanted) {
                    request(wanted);
                }
            }
        }
    } else {
        takeoff_left_ = 0.0f;
        if (!was_grounded_) {
            request(forward ? kain_anim::kRunLanding : kain_anim::kNormalLanding);
            landing_ = true;
        }
    }
    was_grounded_ = state.grounded;

    if (state_ == kain_anim::kRun) {
        // FUN_00445ba0: the stick magnitude drives the run/walk cross blend.
        const float magnitude = std::sqrt(input.move_x * input.move_x + input.move_y * input.move_y);
        run_weight_ = magnitude > 1.0f ? 1.0f : magnitude;
        layers_.layer(0).set_rate((run_weight_ * 0.5f + 0.5f) * run_rate_);
        layers_.layer(1).set_rate((run_weight_ + 1.0f) * run_rate_);
    }
    const bool sequence_ended = layers_.advance(seconds);
    if (landing_ && sequence_ended) {
        landing_ = false;
    }
    if (state.grounded && !landing_) {
        const std::int32_t wanted =
            forward ? kain_anim::kRun : (back ? kain_anim::kBack : kain_anim::kReady);
        if (state_ != wanted) {
            request(wanted);
        }
    }
}

}  // namespace bo2::game
