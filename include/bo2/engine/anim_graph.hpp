#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"

namespace bo2::engine {

// One clip node of an `animatio` graph (docs/FORMATS/animatio.md): a window
// [in_time, out_time] of a reel played at `speed`. FUN_005eb2e0 reads it and
// FUN_005e1c20 prints it.
struct AnimClipNode {
    float in_time = 0.0f;
    float out_time = 0.0f;
    float speed = 1.0f;
    // Tagged reel reference, (index << 1) | 1, or 0 for none (FUN_005e1b60).
    std::uint32_t reel_tag = 0;
    // Tagged index into AnimGraph::blends; the blend a clip starts with when
    // neither the action nor the chain link names one (FUN_005e1d80).
    std::uint32_t blend_tag = 0;
    std::string name;  // `cl_*`

    bool has_reel() const { return reel_tag != 0; }
    // Index into AnimGraph::reels. Values >= 0x4000 select the second reel
    // table (pairs of reels), which this reader does not decode.
    std::uint32_t reel_index() const { return reel_tag >> 1; }
};

// FUN_005e9840: two u16 indexes into the entry's BIG symbol list.
struct AnimReelRef {
    std::uint16_t name_symbol = 0;
    std::uint16_t type_symbol = 0;
};

// One action of a transition (FUN_005e8e00, run by FUN_00642c00). Kind 1
// plays the sequence `target`, kind 2 continues with the state `target`,
// kinds 3 and 4 start a state or a sequence on the layer `arg`, kind 5 creates
// the layer entity and kind 6 calls a function.
struct AnimAction {
    std::int32_t kind = 0;
    std::int32_t target = 0;
    std::uint32_t blend_tag = 0;  // tagged index into AnimGraph::blends
    std::uint32_t param_tag = 0;  // tagged index into AnimGraph::variants
    std::int32_t arg = 0;
};

// A condition id with its operand and the actions it guards (FUN_005ea770).
struct AnimTransition {
    std::uint32_t variant_tag = 0;  // tagged index into AnimGraph::variants
    std::int32_t condition = 0;
    std::vector<AnimAction> actions;
};

struct AnimState {
    std::vector<AnimTransition> transitions;
    std::uint32_t blend_tag = 0;  // state +0x18
    std::string name;             // `st_*`
};

// The operand of a combinator condition: another condition and its operand.
struct AnimBinding {
    std::uint32_t value_tag = 0;  // tagged index into AnimGraph::variants
    std::int32_t condition = 0;
};

// A sequence is the run of chain links from `head` up to the first link whose
// clip is -1 (FUN_00643d60).
struct AnimSequence {
    std::uint32_t head_tag = 0;  // tagged index into AnimGraph::chain
    std::string name;            // `sq_*`
};

struct AnimChainLink {
    std::int32_t clip = -1;       // clip node index, -1 ends the sequence
    std::uint32_t blend_tag = 0;  // tagged index into AnimGraph::blends
};

// A typed value (FUN_005ebed0). Floats (types 1, 4, 5) keep their bit pattern
// in `value`; symbols (types 9, 10) are widened.
struct AnimVariant {
    std::int32_t type = 0;
    std::int32_t value = 0;
};

struct AnimGraph {
    std::vector<AnimState> states;
    std::vector<AnimBinding> bindings;
    std::vector<AnimSequence> sequences;
    std::vector<AnimChainLink> chain;
    std::vector<AnimClipNode> clips;
    std::vector<AnimReelRef> reels;
    std::vector<float> blends;  // the blend time in seconds of each 16-byte record
    std::vector<AnimVariant> variants;

    // The blend time a tag names; 0 for no tag or one out of range.
    float blend_seconds(std::uint32_t tag) const;
    // The index of the state with this name, or -1.
    std::int32_t find_state(const std::string& name) const;
};

// `bytes` is the extracted `animatio` entry. Reads the whole version 0 stream
// (FUN_005e9ae0, FUN_005e8ee0) and keeps the tables the state machine needs;
// masks, layer types, the symbol map, objects, transforms and event lists are
// read and dropped. Throws common::ParseError on a bad format id, on version 1
// (an alias list, which has no graph), on a truncated stream, on bytes after
// the graph, on an unknown object class and on an element count that cannot
// fit in the remaining bytes (a port-only limit, counts above 4M are rejected
// too).
AnimGraph parse_anim_graph(const std::vector<std::uint8_t>& bytes);

// Name of the reel a clip node plays: the entry's symbol at the reel's name
// symbol, looked up in the archive name table. Empty when the node has no
// reel, when it uses the second reel table or when an index is out of range.
std::string anim_clip_reel_name(const AnimGraph& graph, const AnimClipNode& clip,
                                const BigArchive& archive, const BigEntry& entry);

}  // namespace bo2::engine
