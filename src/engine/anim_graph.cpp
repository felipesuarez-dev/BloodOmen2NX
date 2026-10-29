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
void skip_action(Cursor& in) {
    const std::int32_t kind = in.i32();
    in.skip_ints(3);
    if (kind >= 1 && kind <= 6) {
        in.i32();
    }
}

// FUN_005eab00: each state is a vector of transitions (a variant, a condition
// id and a vector of actions) followed by six ints.
void skip_states(Cursor& in) {
    const std::size_t states = in.count(4);
    for (std::size_t state = 0; state < states; ++state) {
        const std::size_t transitions = in.count(12);
        for (std::size_t transition = 0; transition < transitions; ++transition) {
            in.skip_ints(2);
            const std::size_t actions = in.count(16);
            for (std::size_t action = 0; action < actions; ++action) {
                skip_action(in);
            }
        }
        in.skip_ints(6);
    }
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
    skip_states(in);  // +0x28
    const std::size_t bindings = in.count(8);  // +0x34, FUN_005eaea0
    in.skip_ints(bindings * 2);
    const std::size_t sequences = in.count(12);  // +0x40, FUN_005eb000
    in.skip_ints(sequences * 3);
    const std::size_t chain = in.count(8);  // +0x4c, FUN_005eb180
    in.skip_ints(chain * 2);

    AnimGraph graph;
    const std::size_t clips = in.count(36);  // +0x58, FUN_005eb2e0
    graph.clips.reserve(clips);
    for (std::size_t index = 0; index < clips; ++index) {
        AnimClipNode clip;
        clip.in_time = in.f32();
        clip.out_time = in.f32();
        clip.speed = in.f32();
        clip.reel_tag = static_cast<std::uint32_t>(in.i32());
        in.skip_ints(5);  // events, transform, blend, mask, name
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
    return graph;
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
