#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/engine/anim_graph.hpp"
#include "bo2/engine/big_archive.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

using Bytes = std::vector<std::uint8_t>;

// The stream primitives of the loader: ints and floats 4-byte aligned, u16
// 2-byte aligned, bytes unaligned (docs/FORMATS/animatio.md).
struct Writer {
    Bytes out;

    void align(std::size_t to) {
        while (out.size() % to != 0) {
            out.push_back(0);
        }
    }
    void i32(std::int32_t value) {
        align(4);
        const auto bits = static_cast<std::uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFFU));
        }
    }
    void f32(float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        i32(static_cast<std::int32_t>(bits));
    }
    void u16(std::uint16_t value) {
        align(2);
        out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
    }
    void action(std::int32_t kind, std::int32_t target, std::int32_t arg) {
        i32(kind);
        i32(target);
        i32(0);
        i32(0);
        if (kind >= 1 && kind <= 6) {
            i32(arg);
        }
    }
};

std::int32_t tag(std::int32_t index) {
    return (index << 1) | 1;
}

// A graph with two states (the first has a transition with a five-int and a
// four-int action), one binding, one sequence, a two-link chain, two clip
// nodes, a second reel table and two reels. `tail` is appended after the reels
// to show that the reader stops there.
Bytes make_graph(std::int32_t version = 0, std::size_t tail = 8) {
    Writer w;
    w.i32(0);
    w.i32(version);
    w.i32(0);
    for (int index = 0; index < 9; ++index) {
        w.i32(0x4F000000 + index);
    }
    w.i32(2);  // states
    w.i32(1);
    w.i32(tag(0));
    w.i32(0x402);
    w.i32(2);
    w.action(1, 0, 0x5C0073);  // five ints
    w.action(7, 3, 99);        // four ints, the next int is not an argument
    for (std::int32_t value : {0, -1, -1, 0, 0, tag(0)}) {
        w.i32(value);
    }
    w.i32(0);  // second state: no transitions
    for (std::int32_t value : {0, -1, -1, 0, 0, tag(5)}) {
        w.i32(value);
    }
    w.i32(1);  // bindings
    w.i32(5);
    w.i32(8);
    w.i32(1);  // sequences
    w.i32(tag(0));
    w.i32(0);
    w.i32(tag(10));
    w.i32(2);  // chain
    w.i32(1);
    w.i32(0);
    w.i32(-1);
    w.i32(0);
    w.i32(2);  // clip nodes
    w.f32(0.0f);
    w.f32(0.8f);
    w.f32(1.5f);
    for (std::int32_t value : {tag(1), 0, 0, 0, 0, tag(0)}) {
        w.i32(value);
    }
    w.f32(0.25f);
    w.f32(1.0f);
    w.f32(1.7f);
    for (std::int32_t value : {0, 0, 0, 0, 0, tag(5)}) {
        w.i32(value);
    }
    w.i32(2);  // second reel table: a pair of references, then an empty slot
    w.i32(0);
    for (std::uint16_t value : std::vector<std::uint16_t>{7, 1, 8, 1}) {
        w.u16(value);
    }
    w.i32(1);
    w.i32(2);  // reels
    for (std::uint16_t value : std::vector<std::uint16_t>{0, 1, 2, 1}) {
        w.u16(value);
    }
    w.out.insert(w.out.end(), tail, 0xAB);
    return w.out;
}

bool throws_parse_error(const Bytes& bytes) {
    try {
        bo2::engine::parse_anim_graph(bytes);
    } catch (const bo2::common::ParseError&) {
        return true;
    }
    return false;
}

void test_clip_nodes_and_reels() {
    const bo2::engine::AnimGraph graph = bo2::engine::parse_anim_graph(make_graph());
    CHECK(graph.clips.size() == 2);
    CHECK(graph.clips[0].in_time == 0.0f);
    CHECK(graph.clips[0].out_time == 0.8f);
    CHECK(graph.clips[0].speed == 1.5f);
    CHECK(graph.clips[0].has_reel() && graph.clips[0].reel_index() == 1);
    CHECK(graph.clips[1].in_time == 0.25f);
    CHECK(graph.clips[1].speed == 1.7f);
    CHECK(!graph.clips[1].has_reel());
    CHECK(graph.reels.size() == 2);
    CHECK(graph.reels[0].name_symbol == 0 && graph.reels[0].type_symbol == 1);
    CHECK(graph.reels[1].name_symbol == 2 && graph.reels[1].type_symbol == 1);
}

void test_rejections() {
    CHECK(throws_parse_error(make_graph(1)));  // alias list
    CHECK(throws_parse_error(make_graph(2)));
    Bytes bad_format = make_graph();
    bad_format[0] = 1;
    CHECK(throws_parse_error(bad_format));
    const Bytes whole = make_graph(0, 0);
    for (std::size_t cut : {std::size_t{3}, std::size_t{20}, std::size_t{60}, whole.size() / 2,
                            whole.size() - 1}) {
        CHECK(throws_parse_error(Bytes(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(cut))));
    }
    // The state count claims more records than the remaining bytes can hold.
    Bytes huge = make_graph();
    huge[48] = 0xFF;
    huge[49] = 0xFF;
    huge[50] = 0xFF;
    huge[51] = 0x7F;
    CHECK(throws_parse_error(huge));
    huge[51] = 0xFF;  // negative
    CHECK(throws_parse_error(huge));
}

// A one-entry-plus-names container: the entry symbols index the names table.
Bytes build_big(const std::vector<std::string>& names, const Bytes& payload,
                const std::vector<std::uint16_t>& symbols) {
    auto put16 = [](Bytes& out, std::uint16_t value) {
        out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
    };
    auto put32 = [](Bytes& out, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    };
    auto patch32 = [](Bytes& out, std::size_t offset, std::uint32_t value) {
        for (std::size_t index = 0; index < 4; ++index) {
            out[offset + index] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFFU);
        }
    };
    auto text = [](Bytes& out, const std::string& value) {
        out.insert(out.end(), value.begin(), value.end());
    };

    Bytes directory;
    text(directory, "symlist");
    directory.push_back(0);
    put32(directory, 0);
    put32(directory, 0);
    put32(directory, static_cast<std::uint32_t>(names.size()));
    for (const std::string& name : names) {
        text(directory, name);
        directory.push_back(0);
    }
    if (directory.size() % 2 != 0) {
        directory.push_back(0);
    }
    patch32(directory, 8, static_cast<std::uint32_t>(directory.size()));

    Bytes out;
    text(out, "goefile");
    out.push_back(0);
    put32(out, 0);
    put32(out, 1);
    out.insert(out.end(), directory.begin(), directory.end());

    Bytes body;
    const std::size_t symbols_size = (20 + symbols.size() * 2 + 3) & ~std::size_t{3};
    text(body, "symlist");
    body.push_back(0);
    put32(body, static_cast<std::uint32_t>(symbols_size));
    put32(body, 0);
    put32(body, static_cast<std::uint32_t>(symbols.size()));
    for (std::uint16_t symbol : symbols) {
        put16(body, symbol);
    }
    while (body.size() < symbols_size) {
        body.push_back(0);
    }
    put32(body, static_cast<std::uint32_t>(payload.size()));
    body.insert(body.end(), payload.begin(), payload.end());
    std::size_t entry_length = 20 + body.size();
    while (entry_length % 4 != 0) {
        body.push_back(0);
        ++entry_length;
    }
    text(out, "animatio");
    put32(out, static_cast<std::uint32_t>(entry_length));
    put32(out, 0);
    put16(out, 0);  // the entry is named by names[0]
    put16(out, 0);
    out.insert(out.end(), body.begin(), body.end());
    patch32(out, 8, static_cast<std::uint32_t>(out.size()));
    return out;
}

void test_reel_names() {
    // names: [0] graph, [1] kain/run, [2] kain/walk. The entry symbols map the
    // reel name symbols 0 and 2 to names 2 and 1.
    const Bytes container = build_big({"g", "kain/run", "kain/walk"}, make_graph(), {2, 0, 1});
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(container);
    CHECK(archive.entries().size() == 1);
    const bo2::engine::BigEntry& entry = archive.entries()[0];
    const bo2::engine::AnimGraph graph = bo2::engine::parse_anim_graph(archive.extract(entry));
    CHECK(bo2::engine::anim_clip_reel_name(graph, graph.clips[0], archive, entry) == "kain/run");
    CHECK(bo2::engine::anim_clip_reel_name(graph, graph.clips[1], archive, entry).empty());
    bo2::engine::AnimClipNode far_reel = graph.clips[0];
    far_reel.reel_tag = static_cast<std::uint32_t>(tag(0x4000));
    CHECK(bo2::engine::anim_clip_reel_name(graph, far_reel, archive, entry).empty());
    far_reel.reel_tag = static_cast<std::uint32_t>(tag(9));
    CHECK(bo2::engine::anim_clip_reel_name(graph, far_reel, archive, entry).empty());
}

// Asset-gated: Kain's graph in the installed game.
void test_kain_graph() {
    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "kain animatio check skipped (BO2NX_ASSETS_DIR is unset)\n";
        return;
    }
    const std::filesystem::path path =
        std::filesystem::path(root) / "media/gogames/bo2/resources/levels/kain/kain.big";
    if (!std::filesystem::exists(path)) {
        std::cout << "kain animatio check skipped (kain.big not found)\n";
        return;
    }
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path.string());
    const bo2::engine::BigEntry* entry = archive.find("animatio", "kain");
    CHECK(entry != nullptr);
    if (entry == nullptr) {
        return;
    }
    const bo2::engine::AnimGraph graph = bo2::engine::parse_anim_graph(archive.extract(*entry));
    CHECK(graph.clips.size() == 237);
    CHECK(graph.reels.size() == 133);
    // Kain's graph also has test clips on the same reels, so look for the nodes
    // the run and walk states use by reel name and window.
    int walk_back = 0;
    bool run_node = false;
    bool walk_node = false;
    for (const bo2::engine::AnimClipNode& clip : graph.clips) {
        const std::string reel = bo2::engine::anim_clip_reel_name(graph, clip, archive, *entry);
        if (reel == "kain/walk_backwards") {
            ++walk_back;
            // The walk-back node speed the port used to hard-code.
            CHECK(std::fabs(clip.speed - 1.7f) < 1.0e-6f);
            CHECK(std::fabs(clip.out_time - 47.0f / 30.0f) < 1.0e-4f);
        } else if (reel == "kain/run" && std::fabs(clip.out_time - 0.8f) < 1.0e-6f &&
                   std::fabs(clip.speed - 1.5f) < 1.0e-6f) {
            run_node = true;
        } else if (reel == "kain/walk" && std::fabs(clip.out_time - 46.0f / 30.0f) < 1.0e-4f &&
                   std::fabs(clip.speed - 1.5f) < 1.0e-6f) {
            walk_node = true;
        }
    }
    CHECK(walk_back == 1);
    CHECK(run_node);
    CHECK(walk_node);
}

}  // namespace

int main() {
    try {
        test_clip_nodes_and_reels();
        test_rejections();
        test_reel_names();
        test_kain_graph();
    } catch (const std::exception& error) {
        std::cerr << "anim graph test: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "anim graph tests: ok\n";
    return 0;
}
