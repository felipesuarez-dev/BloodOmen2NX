#include "bo2/game/kain_animation.hpp"

namespace bo2::game {

namespace {

constexpr float kTakeoffTime = 0.2f;  // 0x3e4ccccd, the timer FUN_004436e0 sets

}  // namespace

KainAnimation::KainAnimation(const bo2::engine::AnimGraph& graph, const KainTuning& tuning)
    : machine_(graph), deadzone_(tuning.stick_deadzone), run_rate_(tuning.run_clip_rate) {}

void KainAnimation::request(std::int32_t state) {
    // Every state enter stores the index and starts the state on layer 0.
    state_ = state;
    machine_.start_state(state);
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
        } else {
            if (takeoff_left_ > 0.0f) {
                takeoff_left_ -= dt;
            }
            if (takeoff_left_ <= 0.0f || state.vertical_velocity < 0.0f) {
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
        machine_.set_rate(run_rate_);
    }
    const bool sequence_ended = machine_.advance(seconds);
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
