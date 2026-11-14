#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bo2/engine/anim_graph.hpp"

namespace bo2::engine {

class AnimLayers;

// One layer of the animation state machine of an `animatio` graph
// (docs/FORMATS/animatio.md, "State machine runtime"): the state it is in,
// the sequence it plays and the clip time.
//
// A machine that belongs to an `AnimLayers` also runs the actions that start
// a state or a sequence on another layer (kinds 3 and 4), the condition
// "layer A is in state B" (id 5) and the state function. Not ported: the
// layer entity (kind 5), the callbacks (kind 6) and the clip events. Condition
// ids other than 0, 1, 5, 7 and the three combinators are game functions that
// are not decoded; they evaluate to false.
// A stretch of reel time a clip played: [from, to] of the clip node `clip`.
struct AnimRootSpan {
    std::int32_t clip = -1;
    float from = 0.0f;
    float to = 0.0f;
};

// What the root moved by in one advance of a layer: the nine floats that
// FUN_005e1f80 writes for the layer (translation, rotation and scale; the
// scale is not kept).
//   - While a clip blends in, the root gets the part of the blend's transform
//     that matches the blend time played (FUN_005ceef0). When the blend ends
//     inside the call, the original counts 0.001 s more of it, and the clip
//     time played after it in the same call moves the root by nothing.
//   - A clip whose node names a transform gives the part of it that matches
//     the clip time played; a clip of no length gives none.
//   - A clip without a transform moves the root by the difference of the
//     root channels of its reel between the two clock values (FUN_0063d080).
//     The machine holds no reels: it reports the stretch in `spans`.
struct AnimRootMotion {
    Vec3 translation;
    Vec3 rotation;
    std::vector<AnimRootSpan> spans;
};

class AnimMachine {
public:
    // `graph` must outlive the machine; so must `layers`, when given.
    explicit AnimMachine(const AnimGraph& graph, AnimLayers* layers = nullptr);

    // FUN_00642c00: runs the actions of the first transition of `state` whose
    // condition holds, following "continue with state" actions. A state out of
    // range stops the machine.
    void start_state(std::int32_t state);

    // FUN_00643030 with FUN_00643f80 and FUN_005e1f80: restarts the requested
    // state when its sequence has finished, then moves the clock of the clip
    // in play once. When the clip reaches its end the next clip of the chain
    // starts, but it does not move in this call: the time the finished clip
    // left over is dropped, as the callers of the original drop it. Returns
    // true when the sequence played its last clip to the end during the call.
    bool advance(double seconds);
    // The root motion of the last advance.
    const AnimRootMotion& root_motion() const { return root_; }

    // FUN_00723620: replaces the rate of the clip in play (the run state
    // writes its own every frame). The next clip starts at its node speed.
    void set_rate(float rate);

    // FUN_00643f10: starts a sequence with the given blend, or the blend of
    // its first link. Action kind 4 calls it on another layer.
    void play_sequence(std::int32_t sequence, std::uint32_t blend_tag);

    std::int32_t state() const { return state_; }          // machine+0x9c, -1 for none
    std::int32_t requested() const { return requested_; }  // machine+0xa0
    std::int32_t sequence() const { return sequence_; }    // -1 for none
    std::int32_t clip() const { return clip_; }            // clip node in play, -1 for none
    // The clip clock in seconds. It runs below `in` while the clip blends in.
    float time() const { return time_; }
    // The reel time to draw: the clock held at `in` during the blend.
    float reel_time() const;
    float rate() const { return rate_; }
    bool blending() const { return clip_ >= 0 && blend_ > 0.0f && remaining_blend() > 0.0f; }
    // FUN_00639de0: how far the blend into the clip has gone, 0 at its start
    // and 1 at its end (1 without a blend). The transition filter a blend
    // record may name is not decoded: the factor is linear.
    float blend_factor() const;
    // How many clips have started; it changes whenever a new pose begins.
    std::uint32_t clip_starts() const { return clip_starts_; }
    // The sequence has played its last clip to the end; the next advance
    // restarts the requested state.
    bool finished() const;
    // How many times a state has been started, restarts included.
    std::uint32_t starts() const { return starts_; }

private:
    const AnimGraph& graph_;
    AnimLayers* layers_ = nullptr;
    std::int32_t state_ = -1;
    std::int32_t requested_ = -1;
    std::int32_t sequence_ = -1;
    std::int32_t link_ = -1;  // chain index of the clip in play, or of the terminator
    std::int32_t clip_ = -1;
    float time_ = 0.0f;      // machine+0x00
    float boundary_ = 0.0f;  // machine+0x04: the clock runs in real time below it
    float in_ = 0.0f;        // machine+0x08
    float out_ = 0.0f;       // machine+0x0c
    float rate_ = 1.0f;      // machine+0x10
    float blend_ = 0.0f;     // the blend time of the clip in play
    std::uint32_t blend_tag_ = 0;  // the blend record of the clip in play
    AnimRootMotion root_;
    std::uint32_t starts_ = 0;
    std::uint32_t clip_starts_ = 0;

    float remaining_blend() const;

    bool condition(std::int32_t id, std::uint32_t variant_tag, int depth) const;
    void start_clip(std::int32_t clip, std::uint32_t blend_tag);
    bool advance_clip(float seconds);
    bool at_end() const;
};

// The layers of an animation instance: one machine each (the original keeps
// 16 slots at AnimInst+0x20). A state of one layer can start states on the
// others (FUN_005e3b80) and ask what state they are in (FUN_006136b0).
class AnimLayers {
public:
    explicit AnimLayers(const AnimGraph& graph, std::size_t count = 16);
    AnimLayers(const AnimLayers&) = delete;
    AnimLayers& operator=(const AnimLayers&) = delete;

    std::size_t size() const { return machines_.size(); }
    AnimMachine& layer(std::size_t index) { return machines_[index]; }
    const AnimMachine& layer(std::size_t index) const { return machines_[index]; }
    // The state of a layer, -1 when it has none or the layer does not exist.
    std::int32_t state_of(std::int32_t layer) const;
    // FUN_005e3b80: starts a state on a layer; a layer out of range is ignored.
    void start_state(std::int32_t layer, std::int32_t state);
    // FUN_005e3e50: plays a sequence on a layer.
    void play_sequence(std::int32_t layer, std::int32_t sequence, std::uint32_t blend_tag);
    // FUN_005e3ea0: advances every layer in order. Returns what layer 0 returns.
    bool advance(double seconds);

private:
    std::vector<AnimMachine> machines_;
};

}  // namespace bo2::engine
