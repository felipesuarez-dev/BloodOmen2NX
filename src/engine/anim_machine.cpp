#include "bo2/engine/anim_machine.hpp"

#include <cmath>
#include <cstddef>

namespace bo2::engine {

namespace {

constexpr std::int32_t kAlways = 0;          // FUN_006135d0
constexpr std::int32_t kPreviousState = 1;   // FUN_006135e0
constexpr std::int32_t kLayerInState = 5;    // FUN_006136b0
constexpr std::int32_t kClipAtEnd = 7;       // FUN_00613770
constexpr std::int32_t kNot = 0x400;         // FUN_006134e0
constexpr std::int32_t kOr = 0x401;          // FUN_00613510
constexpr std::int32_t kAnd = 0x402;         // FUN_00613570

constexpr std::int32_t kPlaySequence = 1;
constexpr std::int32_t kContinueWithState = 2;
constexpr std::int32_t kStartOnLayer = 3;  // FUN_005e3b80
constexpr std::int32_t kPlayOnLayer = 4;   // FUN_005e3e50

// Port-only bounds: the original loops without one, on data that always ends.
constexpr int kMaxStateChain = 64;
constexpr int kMaxConditionDepth = 16;
// The float at 0x8612b0: FUN_005e1f80 adds it to the blend time played when
// the blend ends inside the call.
constexpr float kBlendEndSlack = 0.001f;

}  // namespace

AnimMachine::AnimMachine(const AnimGraph& graph, AnimLayers* layers)
    : graph_(graph), layers_(layers) {}

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
    if (id == kLayerInState) {
        // The operands are a layer and, in the next variant, a state. A layer
        // without a machine fails.
        if (layers_ == nullptr || index + 1 >= graph_.variants.size()) {
            return false;
        }
        const std::int32_t layer = graph_.variants[index].value;
        if (layer < 0 || static_cast<std::size_t>(layer) >= layers_->size()) {
            return false;
        }
        const std::int32_t wanted = graph_.variants[index + 1].value;
        const bool valid = wanted >= 0 && static_cast<std::size_t>(wanted) < graph_.states.size();
        return (valid ? wanted : -1) == layers_->state_of(layer);
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
                } else if (action.kind == kStartOnLayer && layers_ != nullptr) {
                    layers_->start_state(action.arg, action.target);
                } else if (action.kind == kPlayOnLayer && layers_ != nullptr) {
                    layers_->play_sequence(action.arg, action.target, action.blend_tag);
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
    blend_tag_ = blend_tag != 0 ? blend_tag : node.blend_tag;
    float blend = graph_.blend_seconds(blend_tag_);
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

// FUN_005e1f80: moves the clock by `seconds` and adds the root motion of that
// move. Returns true when the clip reached its end.
bool AnimMachine::advance_clip(float seconds) {
    const float before = time_;
    const float rate_before = rate_;
    bool blended = false;
    bool blend_ended = false;
    float blend_stop = time_;  // the clock where the blend time played stops
    bool ended = false;
    if (rate_ == 0.0f) {
        if (boundary_ <= time_) {
            // FUN_005e1f80: past the blend the clip stays and takes all the time.
            used_ = static_cast<double>(seconds);
            return false;
        }
        blended = true;
        time_ = time_ + seconds;
        if (time_ >= boundary_) {
            time_ = boundary_;
            boundary_ = in_;
            blend_ended = true;
        }
        blend_stop = time_;
    } else if (rate_ < 0.0f) {
        if (time_ <= boundary_) {
            time_ += rate_ * seconds;
        } else {
            blended = true;
            const float next = time_ - seconds;
            if (next <= boundary_) {
                blend_stop = boundary_;
                blend_ended = true;
                time_ = (boundary_ - next) * rate_ + boundary_;
                boundary_ = out_;
            } else {
                time_ = next;
                blend_stop = next;
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
                blend_stop = boundary_;
                blend_ended = true;
                time_ = (next - boundary_) * rate_ + boundary_;
                boundary_ = in_;
            } else {
                time_ = next;
                blend_stop = next;
            }
        }
        if (out_ <= time_) {
            time_ = out_;
            ended = true;
        }
    }
    if (blended) {
        // FUN_005e1f80: the clock moved, and a negative rate counts it the
        // other way; the sums go in double, as the original's x87.
        const double moved = static_cast<double>(time_) - static_cast<double>(before);
        used_ = rate_before < 0.0f ? -moved : moved;
    } else {
        used_ = (static_cast<double>(time_) - static_cast<double>(before)) /
                static_cast<double>(rate_before);
    }
    if (blended) {
        // FUN_005ceef0 over the blend interval, whose length is the blend time.
        const AnimTransform* motion = graph_.blend_transform(blend_tag_);
        if (motion != nullptr && blend_ != 0.0f) {
            const float played = (blend_ended ? blend_stop + kBlendEndSlack : blend_stop) - before;
            const float part = played / blend_;
            root_.translation = root_.translation + motion->translation * part;
            root_.rotation = root_.rotation + motion->rotation * part;
        }
        return ended;
    }
    const AnimClipNode& node = graph_.clips[static_cast<std::size_t>(clip_)];
    const AnimTransform* motion = graph_.transform(node.transform_tag);
    if (motion == nullptr) {
        if (time_ != before) {
            root_.spans.push_back({clip_, before, time_});
        }
    } else if (out_ - in_ != 0.0f) {
        const float part = std::fabs(time_ - before) / (out_ - in_);
        root_.translation = root_.translation + motion->translation * part;
        root_.rotation = root_.rotation + motion->rotation * part;
    }
    return ended;
}

// FUN_00643030 with FUN_00643f80 and FUN_005e1f80: the advance of FUN_00643030
// with the flags of the original and, in `time`, what the clip did not use.
std::uint32_t AnimMachine::advance_flags(float& time) {
    root_ = AnimRootMotion{};
    if (state_ < 0) {
        return 4;  // no state: nothing moves and the time is kept
    }
    if (clip_ >= 0 && finished()) {
        // FUN_00643030: the sequence is over, the requested state starts again.
        start_state(requested_);
    }
    std::uint32_t flags = 0;
    const float seconds = time;
    bool moved = false;
    if (clip_ >= 0 && seconds > 0.0f) {
        moved = true;
        const bool ended = advance_clip(seconds);
        if (ended) {
            // FUN_00643f80: the chain moves on, and the next clip waits for
            // the next call.
            const bool terminator = link_ >= 0 &&
                                    static_cast<std::size_t>(link_) < graph_.chain.size() &&
                                    graph_.chain[static_cast<std::size_t>(link_)].clip < 0;
            if (terminator) {
                flags = 3;
            } else if (link_ >= 0) {
                const std::size_t next = static_cast<std::size_t>(link_) + 1;
                if (next < graph_.chain.size()) {
                    link_ = static_cast<std::int32_t>(next);
                    if (graph_.chain[next].clip >= 0) {
                        start_clip(graph_.chain[next].clip, graph_.chain[next].blend_tag);
                        flags = 1;
                    } else {
                        flags = 3;
                    }
                }
            }
        }
    }
    if (moved) {
        // FUN_005e1f80: time less used, 0 below 0.001 s, in double.
        const double left = static_cast<double>(seconds) - used_;
        time = left < 0.001f ? 0.0f : static_cast<float>(left);
    }
    if (requested_ >= 0 && static_cast<std::size_t>(requested_) < graph_.states.size()) {
        const AnimState& wanted = graph_.states[static_cast<std::size_t>(requested_)];
        if (wanted.function >= 0 && wanted.function_variant_tag != 0 &&
            !condition(wanted.function, wanted.function_variant_tag, 0)) {
            start_state(wanted.child);
            return flags;
        }
    }
    const bool terminator = link_ >= 0 && static_cast<std::size_t>(link_) < graph_.chain.size() &&
                            graph_.chain[static_cast<std::size_t>(link_)].clip < 0;
    if (at_end() && terminator && state_ == requested_) {
        flags |= 4;
    }
    return flags;
}

bool AnimMachine::advance(double seconds) {
    root_ = AnimRootMotion{};
    bool sequence_ended = false;
    if (state_ >= 0 && clip_ >= 0 && finished()) {
        // FUN_00643030: the sequence is over, the requested state starts again.
        start_state(requested_);
    }
    if (state_ >= 0 && clip_ >= 0 && seconds > 0.0 &&
        advance_clip(static_cast<float>(seconds)) && link_ >= 0 &&
        static_cast<std::size_t>(link_) < graph_.chain.size() &&
        graph_.chain[static_cast<std::size_t>(link_)].clip >= 0) {
        // FUN_00643f80: the next link starts with its own blend and waits for
        // the next call; the terminator ends the sequence.
        const std::size_t next = static_cast<std::size_t>(link_) + 1;
        if (next < graph_.chain.size()) {
            link_ = static_cast<std::int32_t>(next);
            if (graph_.chain[next].clip >= 0) {
                start_clip(graph_.chain[next].clip, graph_.chain[next].blend_tag);
            } else {
                sequence_ended = true;
            }
        }
    }
    // FUN_00643030: the function of the requested state; when it fails the
    // machine goes to the state's child (none stops the layer).
    if (requested_ >= 0 && static_cast<std::size_t>(requested_) < graph_.states.size()) {
        const AnimState& wanted = graph_.states[static_cast<std::size_t>(requested_)];
        if (wanted.function >= 0 && wanted.function_variant_tag != 0 &&
            !condition(wanted.function, wanted.function_variant_tag, 0)) {
            start_state(wanted.child);
        }
    }
    return sequence_ended;
}

AnimLayers::AnimLayers(const AnimGraph& graph, std::size_t count) {
    machines_.reserve(count);
    made_.assign(count, 0);
    for (std::size_t index = 0; index < count; ++index) {
        machines_.emplace_back(graph, this);
    }
}

std::int32_t AnimLayers::state_of(std::int32_t layer) const {
    if (layer < 0 || static_cast<std::size_t>(layer) >= machines_.size()) {
        return -1;
    }
    return machines_[static_cast<std::size_t>(layer)].state();
}

void AnimLayers::start_state(std::int32_t layer, std::int32_t state) {
    if (layer >= 0 && static_cast<std::size_t>(layer) < machines_.size()) {
        made_[static_cast<std::size_t>(layer)] = 1;
        machines_[static_cast<std::size_t>(layer)].start_state(state);
    }
}

void AnimLayers::play_sequence(std::int32_t layer, std::int32_t sequence,
                               std::uint32_t blend_tag) {
    if (layer >= 0 && static_cast<std::size_t>(layer) < machines_.size()) {
        made_[static_cast<std::size_t>(layer)] = 1;
        machines_[static_cast<std::size_t>(layer)].play_sequence(sequence, blend_tag);
    }
}

bool AnimLayers::advance(double seconds) {
    bool ended = false;
    for (std::size_t index = 0; index < machines_.size(); ++index) {
        const bool result = machines_[index].advance(seconds);
        if (index == 0) {
            ended = result;
        }
    }
    return ended;
}

// FUN_005e3ea0 with FUN_005e3aa0: the layers that have a machine made move,
// three flag bits each from bit 0, and the smallest time left goes out.
std::uint32_t AnimLayers::advance_flags(float& time) {
    std::uint32_t flags = 0;
    float left = time;
    for (std::size_t index = 0; index < machines_.size(); ++index) {
        if (made_[index] == 0) {
            continue;
        }
        float layer_time = time;
        const std::uint32_t layer_flags = machines_[index].advance_flags(layer_time);
        const std::size_t shift = index * 3;
        if (shift < 32) {  // the original's word holds three bits per layer
            flags |= layer_flags << shift;
        }
        if (layer_time < left) {
            left = layer_time;
        }
    }
    time = left;
    return flags;
}

}  // namespace bo2::engine
