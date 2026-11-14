#include "bo2/game/kain_animation.hpp"

#include <cmath>

namespace bo2::game {

namespace {

constexpr float kTakeoffTime = 0.2f;  // 0x3e4ccccd, the timer FUN_004436e0 sets

}  // namespace

KainAnimation::KainAnimation(const bo2::engine::AnimGraph& graph, const KainTuning& tuning)
    : layers_(graph),
      deadzone_(tuning.stick_deadzone),
      run_rate_(tuning.run_clip_rate),
      clip_forward_speed_(tuning.clip_forward_speed),
      combat_exit_delay_(tuning.combat_hold_exit_delay) {
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

void KainAnimation::request_ground_state(const PlayerInput& input) {
    if (combat_move_ && !combat_mode_) {
        combat_move_ = false;  // t_notcombat: back to the ready behaviour
    } else if (!combat_move_ && input.combat && combat_mode_) {
        combat_move_ = true;  // t_directlytocombat
        combat_direction_ = -1;
    }
    if (combat_move_) {
        // FUN_0044ab20 with the table of the combat movement's enter: the
        // state of the direction, started again when the direction changes.
        static constexpr std::int32_t kStates[5] = {
            kain_anim::kReadyCombat, kain_anim::kWalkCombat, kain_anim::kBackCombat,
            kain_anim::kStrafeLeftCombat, kain_anim::kStrafeRightCombat};
        const std::int32_t direction = combat_direction(input);
        if (direction != combat_direction_) {
            combat_direction_ = direction;
            request(kStates[direction]);
        }
        return;
    }
    const bool forward = input.move_y > deadzone_;
    const bool back = input.move_y < -deadzone_;
    const std::int32_t wanted =
        forward ? kain_anim::kRun : (back ? kain_anim::kBack : kain_anim::kReady);
    if (state_ != wanted) {
        request(wanted);
    }
}

// FUN_0042dca0: combat mode follows its control, and outlasts it by the hold
// exit delay.
void KainAnimation::update_combat_mode(const PlayerInput& input, float seconds) {
    if (input.combat) {
        combat_mode_ = true;  // FUN_0042ddd0
        combat_exit_left_ = combat_exit_delay_;
        return;
    }
    if (!combat_mode_) {
        return;
    }
    if (!combat_exit_armed_) {
        combat_exit_left_ = combat_exit_delay_;
        combat_exit_armed_ = true;
        return;
    }
    combat_exit_left_ -= seconds;
    if (combat_exit_left_ <= 0.0f) {
        combat_mode_ = false;  // FUN_0042e010
        combat_exit_armed_ = false;
    }
}

// FUN_00425580: 0 none, 1 forward, 2 back, 3 left, 4 right. The original
// reads forward as a negative y; `move_y` is positive forward.
std::int32_t KainAnimation::combat_direction(const PlayerInput& input) const {
    const float x = input.move_x;
    const float y = -input.move_y;
    if (std::sqrt(x * x + y * y) < deadzone_) {
        return 0;
    }
    const float side = std::fabs(y) + std::fabs(y);
    if (x > side) {
        return 4;
    }
    if (x < -side) {
        return 3;
    }
    return y >= 0.0f ? 2 : 1;
}

GroundMotion KainAnimation::layer_motion(const bo2::engine::AnimMachine& machine) const {
    const bo2::engine::AnimRootMotion& root = machine.root_motion();
    GroundMotion motion;
    motion.forward = root.translation.x;
    motion.lateral = root.translation.y;
    for (const bo2::engine::AnimRootSpan& span : root.spans) {
        if (span.clip >= 0 && static_cast<std::size_t>(span.clip) < clip_forward_speed_.size()) {
            motion.forward +=
                (span.to - span.from) * clip_forward_speed_[static_cast<std::size_t>(span.clip)];
        }
    }
    return motion;
}

void KainAnimation::begin(const PlayerInput& input, const PlayerState& state, double seconds) {
    update_combat_mode(input, static_cast<float>(seconds));
    const bool on_ground = state.grounded && was_grounded_;
    if (on_ground && !landing_) {
        request_ground_state(input);
    }
    if (state_ == kain_anim::kRun) {
        // FUN_00445ba0: the stick magnitude drives the run/walk cross blend.
        const float magnitude = std::sqrt(input.move_x * input.move_x + input.move_y * input.move_y);
        run_weight_ = magnitude > 1.0f ? 1.0f : magnitude;
        layers_.layer(0).set_rate((run_weight_ * 0.5f + 0.5f) * run_rate_);
        layers_.layer(1).set_rate((run_weight_ + 1.0f) * run_rate_);
    }
    const bool sequence_ended = layers_.advance(seconds);
    ground_ = layer_motion(layers_.layer(0));
    if (state_ == kain_anim::kRun && cross_blend()) {
        const GroundMotion walk = layer_motion(layers_.layer(1));
        ground_.forward = run_weight_ * ground_.forward + (1.0f - run_weight_) * walk.forward;
        ground_.lateral = run_weight_ * ground_.lateral + (1.0f - run_weight_) * walk.lateral;
    }
    ground_.turn = !combat_move_;
    if (landing_ && sequence_ended) {
        landing_ = false;
    }
    if (on_ground && !landing_) {
        request_ground_state(input);
    }
}

void KainAnimation::end(const PlayerInput& input, const PlayerState& state, double seconds) {
    const bool forward = input.move_y > deadzone_;
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
}

void KainAnimation::step(const PlayerInput& input, const PlayerState& state, double seconds) {
    begin(input, state, seconds);
    end(input, state, seconds);
}

}  // namespace bo2::game
