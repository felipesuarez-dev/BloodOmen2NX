#include "bo2/engine/anim_graph.hpp"

#include <cstring>

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

constexpr std::int32_t kFormatId = 0;
constexpr std::int32_t kVersionGraph = 0;  // FUN_005e8ee0
constexpr std::int32_t kVersionAlias = 1;  // list of references, no graph
// Port-only limit on an element count, so a corrupt count cannot allocate
// gigabytes. Not an original limit.
constexpr std::int64_t kMaxCount = std::int64_t{1} << 22;
constexpr std::uint32_t kSecondReelTable = 0x4000;  // FUN_005e1b60

// The stream primitives FUN_006a09a0, FUN_006a09e0, FUN_006a0920 and
// FUN_006a0960: ints and floats are 4-byte aligned, u16 values 2-byte aligned,
// bytes are not aligned.
class Cursor {
public:
    explicit Cursor(const std::vector<std::uint8_t>& data) : reader_(data) {}

    std::int32_t i32() {
        align(4);
        const std::uint32_t value = reader_.u32(pos_);
        pos_ += 4;
        return static_cast<std::int32_t>(value);
    }

    float f32() {
        align(4);
        const std::uint32_t bits = reader_.u32(pos_);
        pos_ += 4;
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }

    std::uint16_t u16() {
        align(2);
        const std::uint16_t value = reader_.u16(pos_);
        pos_ += 2;
        return value;
    }

    std::uint8_t u8() {
        const std::uint8_t value = reader_.u8(pos_);
        pos_ += 1;
        return value;
    }

    bool at_end() const { return pos_ == reader_.size(); }

    // Reads an element count and rejects one the remaining bytes cannot hold.
    std::size_t count(std::size_t min_element_size) {
        const std::int64_t n = i32();
        if (n < 0 || n > kMaxCount ||
            static_cast<std::size_t>(n) * min_element_size > reader_.size() - pos_) {
            throw common::ParseError("animatio element count does not fit in the stream");
        }
        return static_cast<std::size_t>(n);
    }

    void skip_ints(std::size_t n) {
        for (std::size_t index = 0; index < n; ++index) {
            i32();
        }
    }

private:
    void align(std::size_t to) {
        pos_ = (pos_ + to - 1) & ~(to - 1);
        // Reader::u32 and u16 check the bounds of the read itself.
    }

    common::Reader reader_;
    std::size_t pos_ = 0;
};

// FUN_005e8e00: four ints, plus a fifth when the kind is 1 to 6.
AnimAction read_action(Cursor& in) {
    AnimAction action;
    action.kind = in.i32();
    action.target = in.i32();
    action.blend_tag = static_cast<std::uint32_t>(in.i32());
    action.param_tag = static_cast<std::uint32_t>(in.i32());
    if (action.kind >= 1 && action.kind <= 6) {
        action.arg = in.i32();
    }
    return action;
}

// FUN_005eab00: each state is a vector of transitions (a variant, a condition
// id and a vector of actions) followed by six ints: variant, factory, child,
// blend, object and name. `name_tags` receives the name tag of each state.
std::vector<AnimState> read_states(Cursor& in, std::vector<std::uint32_t>& name_tags) {
    std::vector<AnimState> states(in.count(4));
    name_tags.reserve(states.size());
    for (AnimState& state : states) {
        state.transitions.resize(in.count(12));
        for (AnimTransition& transition : state.transitions) {
            transition.variant_tag = static_cast<std::uint32_t>(in.i32());
            transition.condition = in.i32();
            transition.actions.resize(in.count(16));
            for (AnimAction& action : transition.actions) {
                action = read_action(in);
            }
        }
        state.function_variant_tag = static_cast<std::uint32_t>(in.i32());
        state.function = in.i32();
        state.child = in.i32();
        state.blend_tag = static_cast<std::uint32_t>(in.i32());
        in.i32();  // object
        name_tags.push_back(static_cast<std::uint32_t>(in.i32()));
    }
    return states;
}

// A name tag is a byte offset into the name pool (FUN_00642ea0).
std::string pool_name(const std::vector<std::uint8_t>& pool, std::uint32_t tag) {
    std::string name;
    if (tag == 0) {
        return name;
    }
    for (std::size_t at = tag >> 1; at < pool.size() && pool[at] != 0; ++at) {
        name.push_back(static_cast<char>(pool[at]));
    }
    return name;
}

// FUN_005e9840: a reference is two u16.
AnimReelRef read_ref(Cursor& in) {
    AnimReelRef ref;
    ref.name_symbol = in.u16();
    ref.type_symbol = in.u16();
    return ref;
}

}  // namespace

AnimGraph parse_anim_graph(const std::vector<std::uint8_t>& bytes) {
    Cursor in(bytes);
    if (in.i32() != kFormatId) {
        throw common::ParseError("animatio: unsupported format id");
    }
    const std::int32_t version = in.i32();
    if (version == kVersionAlias) {
        throw common::ParseError("animatio: version 1 entries are alias lists, not graphs");
    }
    if (version != kVersionGraph) {
        throw common::ParseError("animatio: unsupported version");
    }
    in.i32();         // flags
    in.skip_ints(9);  // nine opaque 32-bit values
    AnimGraph graph;
    std::vector<std::uint32_t> state_names;
    graph.states = read_states(in, state_names);  // +0x28
    graph.bindings.resize(in.count(8));           // +0x34, FUN_005eaea0
    for (AnimBinding& binding : graph.bindings) {
        binding.value_tag = static_cast<std::uint32_t>(in.i32());
        binding.condition = in.i32();
    }
    std::vector<std::uint32_t> sequence_names;
    graph.sequences.resize(in.count(12));  // +0x40, FUN_005eb000
    for (AnimSequence& sequence : graph.sequences) {
        sequence.head_tag = static_cast<std::uint32_t>(in.i32());
        in.i32();  // events
        sequence_names.push_back(static_cast<std::uint32_t>(in.i32()));
    }
    graph.chain.resize(in.count(8));  // +0x4c, FUN_005eb180
    for (AnimChainLink& link : graph.chain) {
        link.clip = in.i32();
        link.blend_tag = static_cast<std::uint32_t>(in.i32());
    }

    std::vector<std::uint32_t> clip_names;
    const std::size_t clips = in.count(36);  // +0x58, FUN_005eb2e0
    graph.clips.reserve(clips);
    for (std::size_t index = 0; index < clips; ++index) {
        AnimClipNode clip;
        clip.in_time = in.f32();
        clip.out_time = in.f32();
        clip.speed = in.f32();
        clip.reel_tag = static_cast<std::uint32_t>(in.i32());
        in.skip_ints(2);  // events, transform
        clip.blend_tag = static_cast<std::uint32_t>(in.i32());
        in.i32();  // mask
        clip_names.push_back(static_cast<std::uint32_t>(in.i32()));
        graph.clips.push_back(clip);
    }

    // Second reel table (+0xc0, FUN_005e9180): a zero tag is followed by two
    // references.
    const std::size_t second = in.count(4);
    for (std::size_t index = 0; index < second; ++index) {
        if (in.i32() == 0) {
            read_ref(in);
            read_ref(in);
        }
    }

    const std::size_t reels = in.count(4);  // +0x64, FUN_005eb5d0
    graph.reels.reserve(reels);
    for (std::size_t index = 0; index < reels; ++index) {
        graph.reels.push_back(read_ref(in));
    }

    const std::size_t masks = in.count(8);  // +0x74, FUN_005eb700
    for (std::size_t index = 0; index < masks; ++index) {
        in.u8();
        in.u8();
        in.u16();
        in.i32();
    }
    const std::size_t table_bytes = in.count(1);  // +0x80
    for (std::size_t index = 0; index < table_bytes; ++index) {
        in.u8();
    }
    in.skip_ints(in.count(8) * 2);  // +0x8c layer types, FUN_005eb900
    const std::size_t symbols = in.count(6);  // +0x98, FUN_005e9460
    for (std::size_t index = 0; index < symbols; ++index) {
        in.u16();
        in.i32();
    }
    in.skip_ints(in.count(4));  // +0xa8
    std::vector<std::uint8_t> pool(in.count(1));  // +0xb4 name pool
    for (std::uint8_t& value : pool) {
        value = in.u8();
    }
    const std::size_t objects = in.count(4);  // +0xcc, FUN_0060f780
    for (std::size_t index = 0; index < objects; ++index) {
        const std::int32_t class_id = in.i32();
        if (class_id == 1) {
            in.skip_ints(2);  // ClipAndBoneID
        } else if (class_id == 2) {
            in.skip_ints(3);  // ClipAndBoneID2
        } else if (class_id >= 0) {
            throw common::ParseError("animatio: unknown object class");
        }
    }
    graph.blends.resize(in.count(16));  // +0xd8, FUN_005eba40
    for (float& seconds : graph.blends) {
        seconds = in.f32();
        in.skip_ints(3);
    }
    in.skip_ints(in.count(36) * 9);  // +0xe4 transforms, FUN_005e8ab0
    const std::size_t event_lists = in.count(4);  // +0xf0, FUN_005ebbf0
    for (std::size_t index = 0; index < event_lists; ++index) {
        in.skip_ints(in.count(16) * 4);
    }
    graph.variants.resize(in.count(8));  // +0xfc, FUN_005ebed0
    for (AnimVariant& variant : graph.variants) {
        variant.type = in.i32();
        if (variant.type == 9 || variant.type == 10) {
            variant.value = in.u16();
        } else {
            variant.value = in.i32();  // types 1, 4 and 5 are floats
        }
    }
    in.i32();  // +0x70
    if (!in.at_end()) {
        throw common::ParseError("animatio: bytes after the graph");
    }

    for (std::size_t index = 0; index < graph.states.size(); ++index) {
        graph.states[index].name = pool_name(pool, state_names[index]);
    }
    for (std::size_t index = 0; index < graph.sequences.size(); ++index) {
        graph.sequences[index].name = pool_name(pool, sequence_names[index]);
    }
    for (std::size_t index = 0; index < graph.clips.size(); ++index) {
        graph.clips[index].name = pool_name(pool, clip_names[index]);
    }
    return graph;
}

float AnimGraph::blend_seconds(std::uint32_t tag) const {
    const std::size_t index = tag >> 1;
    return tag != 0 && index < blends.size() ? blends[index] : 0.0f;
}

std::int32_t AnimGraph::find_state(const std::string& name) const {
    for (std::size_t index = 0; index < states.size(); ++index) {
        if (states[index].name == name) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

std::string anim_clip_reel_name(const AnimGraph& graph, const AnimClipNode& clip,
                                const BigArchive& archive, const BigEntry& entry) {
    if (!clip.has_reel() || clip.reel_index() >= kSecondReelTable ||
        clip.reel_index() >= graph.reels.size()) {
        return {};
    }
    const std::uint16_t symbol = graph.reels[clip.reel_index()].name_symbol;
    if (symbol >= entry.symbols.size()) {
        return {};
    }
    const std::uint16_t name = entry.symbols[symbol];
    if (name >= archive.names().size()) {
        return {};
    }
    return archive.names()[name];
}

}  // namespace bo2::engine
