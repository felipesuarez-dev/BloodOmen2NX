#include "bo2/engine/anim_machine.hpp"

#include <cstddef>

namespace bo2::engine {

namespace {

constexpr std::int32_t kAlways = 0;          // FUN_006135d0
constexpr std::int32_t kPreviousState = 1;   // FUN_006135e0
constexpr std::int32_t kClipAtEnd = 7;       // FUN_00613770
constexpr std::int32_t kNot = 0x400;         // FUN_006134e0
constexpr std::int32_t kOr = 0x401;          // FUN_00613510
constexpr std::int32_t kAnd = 0x402;         // FUN_00613570

constexpr std::int32_t kPlaySequence = 1;
constexpr std::int32_t kContinueWithState = 2;

// Port-only bounds: the original loops without one, on data that always ends.
constexpr int kMaxStateChain = 64;
constexpr int kMaxConditionDepth = 16;
constexpr int kMaxAdvanceSteps = 64;
// FUN_005e1f80 drops a remainder below the float at 0x8612b0.
constexpr float kTimeEpsilon = 1.0e-6f;

}  // namespace

AnimMachine::AnimMachine(const AnimGraph& graph) : graph_(graph) {}

bool AnimMachine::at_end() const {
    // FUN_00613770 and FUN_00643030: a clip played backwards ends at `in`.
    return clip_ >= 0 && time_ == (rate_ <= 0.0f ? in_ : out_);
}

bool AnimMachine::finished() const {
    const bool terminator = link_ >= 0 && static_cast<std::size_t>(link_) < graph_.chain.size() &&
                            graph_.chain[static_cast<std::size_t>(link_)].clip < 0;
    return at_end() && terminator;
}

float AnimMachine::reel_time() const {
    // While a clip blends in, the clock is outside [in, out]: draw its first frame.
    if (rate_ < 0.0f) {
        return time_ > out_ ? out_ : time_;
    }
    return time_ < in_ ? in_ : time_;
}

void AnimMachine::set_rate(float rate) {
    rate_ = rate;
}

float AnimMachine::remaining_blend() const {
    // The clock is `blend` away from `in` when the clip starts (from `out`
    // for a negative rate).
    return rate_ >= 0.0f ? in_ - time_ : time_ - out_;
}

float AnimMachine::blend_factor() const {
    if (clip_ < 0 || !(blend_ > 0.0f)) {
        return 1.0f;
    }
    const float factor = 1.0f - remaining_blend() / blend_;
    return factor < 0.0f ? 0.0f : (factor > 1.0f ? 1.0f : factor);
}

bool AnimMachine::condition(std::int32_t id, std::uint32_t variant_tag, int depth) const {
    if (id == kAlways) {
        return true;
    }
    if (id == kClipAtEnd) {
        return at_end();
    }
    const std::size_t index = variant_tag >> 1;
    if (variant_tag == 0 || index >= graph_.variants.size() || depth > kMaxConditionDepth) {
        return false;
    }
    if (id == kPreviousState) {
        // The operand is a state index; an index outside the table is "none".
        const std::int32_t wanted = graph_.variants[index].value;
        const bool valid = wanted >= 0 && static_cast<std::size_t>(wanted) < graph_.states.size();
        return (valid ? wanted : -1) == state_;
    }
    // A combinator's operands are bindings: the value of its variant and, for
    // the two-operand ones, of the variant after it.
    const auto operand = [&](std::size_t variant) {
        if (variant >= graph_.variants.size()) {
            return false;
        }
        const std::uint32_t tag = static_cast<std::uint32_t>(graph_.variants[variant].value);
        const std::size_t binding = tag >> 1;
        if (tag == 0 || binding >= graph_.bindings.size()) {
            return false;
        }
        return condition(graph_.bindings[binding].condition, graph_.bindings[binding].value_tag,
                         depth + 1);
    };
    if (id == kNot) {
        return !operand(index);
    }
    if (id == kOr) {
        return operand(index) || operand(index + 1);
    }
    if (id == kAnd) {
        return operand(index) && operand(index + 1);
    }
    return false;  // a game condition that is not decoded
}

void AnimMachine::start_state(std::int32_t state) {
    ++starts_;
    requested_ = state;
    for (int hop = 0; hop < kMaxStateChain; ++hop) {
        if (state < 0 || static_cast<std::size_t>(state) >= graph_.states.size()) {
            state_ = -1;
            sequence_ = -1;
            link_ = -1;
            clip_ = -1;
            return;
        }
        const AnimState& entry = graph_.states[static_cast<std::size_t>(state)];
        const AnimTransition* chosen = nullptr;
        for (const AnimTransition& transition : entry.transitions) {
            if (condition(transition.condition, transition.variant_tag, 0)) {
                chosen = &transition;
                break;
            }
        }
        bool moved = false;
        if (chosen != nullptr) {
            for (const AnimAction& action : chosen->actions) {
                if (action.kind == kPlaySequence) {
                    // The action's blend, else the blend of the state being left.
                    std::uint32_t blend = state_ >= 0
                                              ? graph_.states[static_cast<std::size_t>(state_)].blend_tag
                                              : 0;
                    if (action.blend_tag != 0) {
                        blend = action.blend_tag;
                    }
                    play_sequence(action.target, blend);
                } else if (action.kind == kContinueWithState) {
                    if (action.target == state) {
                        state_ = state;
                        return;
                    }
                    moved = true;
                    state = action.target;
                }
            }
        }
        if (!moved) {
            break;
        }
    }
    // Only now: the conditions above saw the state that was being left.
    state_ = state;
}

void AnimMachine::play_sequence(std::int32_t sequence, std::uint32_t blend_tag) {
    // FUN_00643f10: the first link starts with the given blend, else its own.
    if (sequence < 0 || static_cast<std::size_t>(sequence) >= graph_.sequences.size()) {
        return;
    }
    const std::uint32_t head = graph_.sequences[static_cast<std::size_t>(sequence)].head_tag;
    const std::size_t first = head >> 1;
    if (head == 0 || first >= graph_.chain.size()) {
        return;
    }
    sequence_ = sequence;
    link_ = static_cast<std::int32_t>(first);
    const AnimChainLink& link = graph_.chain[first];
    start_clip(link.clip, blend_tag != 0 ? blend_tag : link.blend_tag);
}

void AnimMachine::start_clip(std::int32_t clip, std::uint32_t blend_tag) {
    // FUN_005e1d80: the clock starts the blend time before the clip.
    if (clip < 0 || static_cast<std::size_t>(clip) >= graph_.clips.size()) {
        clip_ = -1;
        return;
    }
    const AnimClipNode& node = graph_.clips[static_cast<std::size_t>(clip)];
    float blend = graph_.blend_seconds(blend_tag != 0 ? blend_tag : node.blend_tag);
    if (!(blend > 0.0f)) {
        blend = 0.0f;
    }
    clip_ = clip;
    ++clip_starts_;
    blend_ = blend;
    in_ = node.in_time;
    out_ = node.out_time;
    rate_ = node.speed;
    boundary_ = rate_ < 0.0f ? out_ : in_;
    time_ = rate_ < 0.0f ? boundary_ + blend : boundary_ - blend;
}

// FUN_005e1f80. Returns true when the clip reached its end; `seconds` keeps
// what the clip did not use.
bool AnimMachine::advance_clip(float& seconds) {
    const float before = time_;
    bool blended = false;
    bool ended = false;
    if (rate_ == 0.0f) {
        if (boundary_ <= time_) {
            seconds = 0.0f;
            return false;
        }
        blended = true;
        time_ = time_ + seconds;
        if (time_ >= boundary_) {
            time_ = boundary_;
            boundary_ = in_;
        }
    } else if (rate_ < 0.0f) {
        if (time_ <= boundary_) {
            time_ += rate_ * seconds;
        } else {
            blended = true;
            const float next = time_ - seconds;
            if (next <= boundary_) {
                time_ = (boundary_ - next) * rate_ + boundary_;
                boundary_ = out_;
            } else {
                time_ = next;
            }
        }
        if (time_ <= in_) {
            time_ = in_;
            ended = true;
        }
    } else {
        if (boundary_ <= time_) {
            time_ += rate_ * seconds;
        } else {
            blended = true;
            const float next = time_ + seconds;
            if (next >= boundary_) {
                time_ = (next - boundary_) * rate_ + boundary_;
                boundary_ = in_;
            } else {
                time_ = next;
            }
        }
        if (out_ <= time_) {
            time_ = out_;
            ended = true;
        }
    }
    if (rate_ != 0.0f) {
        // The time the clock used: a blend runs in real time, a clip at its rate.
        float used = time_ - before;
        if (!blended) {
            used /= rate_;
        } else if (rate_ < 0.0f) {
            used = -used;
        }
        seconds -= used;
        if (seconds < kTimeEpsilon) {
            seconds = 0.0f;
        }
    } else {
        seconds = 0.0f;
    }
    return ended;
}

bool AnimMachine::advance(double seconds) {
    float left = static_cast<float>(seconds);
    bool sequence_ended = false;
    for (int step = 0; step < kMaxAdvanceSteps && left > 0.0f; ++step) {
        if (state_ < 0 || clip_ < 0) {
            break;
        }
        if (finished()) {
            start_state(requested_);
            if (state_ < 0 || clip_ < 0) {
                break;
            }
        }
        const float before = left;
        if (advance_clip(left) && link_ >= 0 &&
            static_cast<std::size_t>(link_) < graph_.chain.size() &&
            graph_.chain[static_cast<std::size_t>(link_)].clip >= 0) {
            // FUN_00643f80: the next link, started with its own blend, or the
            // terminator, which FUN_00643030 finds on its next call.
            const std::size_t next = static_cast<std::size_t>(link_) + 1;
            if (next >= graph_.chain.size()) {
                break;
            }
            link_ = static_cast<std::int32_t>(next);
            if (graph_.chain[next].clip >= 0) {
                start_clip(graph_.chain[next].clip, graph_.chain[next].blend_tag);
            } else {
                sequence_ended = true;
            }
        }
        if (left == before) {
            break;  // a clip of no length used no time: wait for the next call
        }
    }
    return sequence_ended;
}

}  // namespace bo2::engine
