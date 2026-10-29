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

struct AnimGraph {
    std::vector<AnimClipNode> clips;
    std::vector<AnimReelRef> reels;
};

// `bytes` is the extracted `animatio` entry. Reads the version 0 stream up to
// the end of the reel table (FUN_005e9ae0, FUN_005e8ee0): the states, the
// sequences and the clip nodes are walked to reach it, and what follows (name
// pool, blends, events) is not read. Throws common::ParseError on a bad
// format id, on version 1 (an alias list, which has no graph), on a truncated
// stream and on an element count that cannot fit in the remaining bytes (a
// port-only limit, counts above 4M are rejected too).
AnimGraph parse_anim_graph(const std::vector<std::uint8_t>& bytes);

// Name of the reel a clip node plays: the entry's symbol at the reel's name
// symbol, looked up in the archive name table. Empty when the node has no
// reel, when it uses the second reel table or when an index is out of range.
std::string anim_clip_reel_name(const AnimGraph& graph, const AnimClipNode& clip,
                                const BigArchive& archive, const BigEntry& entry);

}  // namespace bo2::engine
