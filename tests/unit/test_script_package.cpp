#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/region.hpp"
#include "bo2/engine/script_package.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

bool throws(const std::function<void()>& body) {
    try {
        body();
    } catch (const bo2::common::ParseError&) {
        return true;
    }
    return false;
}

// Writes a package with the alignment of the writer of the executable: u16
// aligned to 2, u32 aligned to 4, chunks padded to 4.
class Writer {
public:
    std::vector<std::uint8_t> bytes;

    void align(std::size_t step) {
        while (bytes.size() % step != 0) {
            bytes.push_back(0);
        }
    }
    void u8(std::uint8_t value) { bytes.push_back(value); }
    void u16(std::uint16_t value) {
        align(2);
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }
    void u32(std::uint32_t value) {
        align(4);
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
        }
    }
    std::size_t begin_chunk() {
        u32(0);
        return bytes.size();
    }
    // `extra` adds bytes of padding inside the chunk, to test the limit.
    void end_chunk(std::size_t start, std::size_t extra = 0) {
        align(4);
        for (std::size_t index = 0; index < extra; ++index) {
            bytes.push_back(0);
        }
        const auto size = static_cast<std::uint32_t>(bytes.size() - start);
        for (int shift = 0; shift < 32; shift += 8) {
            bytes[start - 4 + static_cast<std::size_t>(shift / 8)] =
                static_cast<std::uint8_t>((size >> shift) & 0xFF);
        }
    }
};

// The names of the synthetic package; symbol i is name i.
const std::vector<std::string> kNames = {"", "native", "alpha", "default", "idle", "mode", "fast",
                                         "slow", "speed", "start", "run", "base"};
enum Name : std::uint16_t {
    kNative = 1, kAlpha, kDefault, kIdle, kMode, kFast, kSlow, kSpeed, kStart, kRun, kBase
};

std::vector<std::uint16_t> identity_symbols() {
    std::vector<std::uint16_t> symbols;
    for (std::size_t index = 0; index < kNames.size(); ++index) {
        symbols.push_back(static_cast<std::uint16_t>(index));
    }
    return symbols;
}

struct Variant {
    std::uint16_t version = 1;
    std::uint16_t class_name = kAlpha;
    std::uint16_t method_slot = 0;
    std::uint16_t body_method = 1;
    std::uint16_t slot_state = 0;
    std::uint16_t reference_body = 1;
    std::uint16_t reference_kind = 2;
    std::uint16_t reference_word = 1;
    std::uint32_t tail = 0;
    std::size_t declaration_padding = 0;
    bool trailing_byte = false;

};

// One class `alpha` (parent `base`), two states, an enumeration with two
// values, a float field with a default, two methods (one with a parameter),
// a native body and a script body of three words with a reference of each
// kind 0, 2, 4 and 5.
std::vector<std::uint8_t> build_package(const Variant& v = Variant{}) {
    Writer out;
    out.u16(v.version);
    out.u16(1);
    out.u16(kNative);
    out.u16(1);  // class count

    const std::size_t declaration = out.begin_chunk();
    out.u16(v.class_name);
    out.u16(0);   // kind
    out.u16(8);   // data size
    out.u16(2);   // slot count
    out.u16(2);   // states
    out.u16(1);   // enums
    out.u16(1);   // fields
    out.u16(2);   // methods
    out.u16(2);   // bodies
    out.u16(kDefault);
    out.u16(kIdle);
    out.u16(kMode);  // enum
    out.u16(0);
    out.u16(2);
    out.u16(kFast);
    out.u16(kSlow);
    out.u32(1);
    out.u32(2);
    out.u16(kSpeed);  // field
    out.u16(12);
    out.u32(4);
    out.u16(3);           // type: float
    out.u16(1);           // default flag
    out.u32(0x3FC00000);  // 1.5f
    out.u16(kStart);      // method 0
    out.u8(0);
    out.u8(2);
    out.u16(v.method_slot);
    out.u16(0);  // result: no value
    out.u16(0);  // parameters
    out.u16(kRun);  // method 1
    out.u8(5);
    out.u8(0);
    out.u16(bo2::engine::kScriptNoSlot);
    out.u16(2);  // result: integer
    out.u16(1);  // one parameter
    out.u16(10);
    out.u16(kAlpha);
    out.u32(4);
    out.u16(v.body_method);  // body 0, native
    out.u16(1);
    out.u16(0);
    out.u16(8);
    out.u16(0);
    out.u16(0);  // locals
    out.u16(0);  // body 1, script
    out.u16(0);
    out.u16(1);
    out.u16(4);
    out.u16(4);
    out.u16(1);  // one local
    out.u16(2);
    out.u32(0);
    out.u32(3);  // three code words
    out.u32(0x04000000);
    out.u32(0x48000000);
    out.u32(0x00000000);
    out.end_chunk(declaration, v.declaration_padding);

    const std::size_t link = out.begin_chunk();
    out.u16(1);
    out.u16(kBase);   // parent
    out.u16(0);       // state 0: own table
    out.u16(1);
    out.u16(kBase);   // state 1: shares the table of `base`
    out.u16(v.slot_state);
    out.u16(0);
    out.u16(kAlpha);
    out.u16(1);
    out.u16(0xFFFF);  // end of slots
    out.u16(v.reference_body);
    out.u16(0);  // kind 0 at word 0
    out.u16(0);
    out.u16(kSpeed);
    out.u16(v.reference_kind);  // kind 2 at word 1
    out.u16(v.reference_word);
    out.u16(kAlpha);
    out.u16(4);  // kind 4 at word 2: body 0 of alpha
    out.u16(2);
    out.u16(kAlpha);
    out.u16(0);
    out.u16(5);  // kind 5 at word 1: tables of base, method 1 of alpha
    out.u16(1);
    out.u16(kBase);
    out.u16(kAlpha);
    out.u16(1);
    out.u16(0xFFFF);  // end of references
    out.u16(0xFFFF);  // end of bodies
    out.end_chunk(link);

    out.u32(v.tail);
    if (v.trailing_byte) {
        out.bytes.push_back(0);
    }
    return out.bytes;
}

bo2::engine::ScriptPackage parse(const std::vector<std::uint8_t>& data) {
    return bo2::engine::parse_script_package(data, identity_symbols(), kNames);
}

void test_parse_minimal_package() {
    const bo2::engine::ScriptPackage package = parse(build_package());
    CHECK(package.version == 1);
    CHECK(package.imports == std::vector<std::string>{"native"});
    CHECK(package.classes.size() == 1);
    const bo2::engine::ScriptClass& cls = package.classes[0];
    CHECK(cls.name == "alpha");
    CHECK(cls.kind == 0 && cls.data_size == 8 && cls.slot_count == 2);
    CHECK((cls.states == std::vector<std::string>{"default", "idle"}));
    CHECK(cls.enums.size() == 1 && cls.enums[0].name == "mode" && cls.enums[0].values.size() == 2);
    CHECK(cls.fields.size() == 1);
    CHECK(cls.fields[0].name == "speed" && cls.fields[0].flags == 12 && cls.fields[0].offset == 4);
    CHECK(cls.fields[0].type.kind == 3 && cls.fields[0].default_flag == 1);
    CHECK(cls.fields[0].default_value == 0x3FC00000U);
    CHECK(cls.methods.size() == 2);
    CHECK(cls.methods[0].name == "start" && cls.methods[0].byte5 == 2 && cls.methods[0].slot == 0);
    CHECK(cls.methods[1].slot == bo2::engine::kScriptNoSlot && cls.methods[1].result.kind == 2);
    CHECK(cls.methods[1].params.size() == 1 && cls.methods[1].params[0].type.kind == 10);
    CHECK(cls.methods[1].params[0].type.name == "alpha" && cls.methods[1].params[0].offset == 4);
    CHECK(cls.bodies.size() == 2);
    CHECK(cls.bodies[0].native() && cls.bodies[0].code.empty() && cls.bodies[0].method == 1);
    CHECK(!cls.bodies[1].native() && cls.bodies[1].state == 1 && cls.bodies[1].stack_size == 4);
    CHECK(cls.bodies[1].locals.size() == 1 && cls.bodies[1].locals[0].type.kind == 2);
    CHECK((cls.bodies[1].code == std::vector<std::uint32_t>{0x04000000U, 0x48000000U, 0U}));
    CHECK(cls.parent == "base");
    CHECK((cls.state_owners == std::vector<std::string>{"", "base"}));
    CHECK(cls.slots.size() == 1 && cls.slots[0].class_name == "alpha" && cls.slots[0].body == 1);
    CHECK(cls.references.size() == 2 && cls.references[0].empty());
    const std::vector<bo2::engine::ScriptReference>& refs = cls.references[1];
    CHECK(refs.size() == 4);
    if (refs.size() == 4) {
        CHECK(refs[0].kind == 0 && refs[0].word == 0 && refs[0].name == "speed");
        CHECK(refs[1].kind == 2 && refs[1].word == 1 && refs[1].name == "alpha");
        CHECK(refs[2].kind == 4 && refs[2].word == 2 && refs[2].index == 0);
        CHECK(refs[3].kind == 5 && refs[3].name == "base" && refs[3].second == "alpha");
        CHECK(refs[3].index == 1);
    }
}

void test_alignment() {
    // The enumeration has two values: after five u16 the u32 values need two
    // bytes of padding, and they still read 1 and 2.
    const bo2::engine::ScriptPackage package = parse(build_package());
    CHECK(package.classes[0].enums[0].values[0].second == 1);
    CHECK(package.classes[0].enums[0].values[1].second == 2);
    CHECK(package.classes[0].enums[0].values[1].first == "slow");
}

void test_chunk_padding() {
    Variant three_short;
    three_short.declaration_padding = 2;
    CHECK(!throws([&] { parse(build_package(three_short)); }));
    Variant four_more;
    four_more.declaration_padding = 4;
    CHECK(throws([&] { parse(build_package(four_more)); }));
}

void test_validation_rules() {
    Variant version;
    version.version = 2;
    CHECK(throws([&] { parse(build_package(version)); }));
    Variant name;
    name.class_name = 40;
    CHECK(throws([&] { parse(build_package(name)); }));
    std::vector<std::uint8_t> short_chunk = build_package();
    short_chunk.resize(short_chunk.size() / 2);
    CHECK(throws([&] { parse(short_chunk); }));
    Variant kind;
    kind.reference_kind = 6;
    CHECK(throws([&] { parse(build_package(kind)); }));
    Variant method;
    method.body_method = 2;
    CHECK(throws([&] { parse(build_package(method)); }));
    Variant slot;
    slot.method_slot = 2;
    CHECK(throws([&] { parse(build_package(slot)); }));
    Variant slot_state;
    slot_state.slot_state = 2;
    CHECK(throws([&] { parse(build_package(slot_state)); }));
    Variant body;
    body.reference_body = 2;
    CHECK(throws([&] { parse(build_package(body)); }));
    Variant word;
    word.reference_word = 3;
    CHECK(throws([&] { parse(build_package(word)); }));
    Variant tail;
    tail.tail = 1;
    CHECK(throws([&] { parse(build_package(tail)); }));
    Variant trailing;
    trailing.trailing_byte = true;
    CHECK(throws([&] { parse(build_package(trailing)); }));
    // A symbol that points outside the name table.
    std::vector<std::uint16_t> symbols = identity_symbols();
    symbols[kAlpha] = 500;
    CHECK(throws([&] { bo2::engine::parse_script_package(build_package(), symbols, kNames); }));
}

void test_instruction_words() {
    for (std::uint32_t code = 0; code <= 0x60; ++code) {
        std::size_t expected = 1;
        if (code == 0x49 || code == 0x4F || code == 0x55) {
            expected = 3;
        } else if (code == 0x02 || code == 0x21 || code == 0x26 || code == 0x47 || code == 0x48 ||
                   (code >= 0x4A && code <= 0x4E) || (code >= 0x50 && code <= 0x54) ||
                   (code >= 0x56 && code <= 0x58)) {
            expected = 2;
        }
        CHECK(bo2::engine::script_instruction_words((code << 24) | 0x123456U) == expected);
    }
}

struct Totals {
    std::size_t packages = 0;
    std::map<std::string, std::size_t> by_name;
    std::size_t classes = 0;
    std::size_t methods = 0;
    std::size_t bodies = 0;
    std::size_t native_bodies = 0;
    std::size_t fields = 0;
    std::size_t enums = 0;
    std::size_t code_words = 0;
    std::size_t references[6] = {};
    bool start_first = true;
};

void add(Totals& totals, const std::string& name, const bo2::engine::ScriptPackage& package) {
    ++totals.packages;
    ++totals.by_name[name];
    totals.classes += package.classes.size();
    for (const bo2::engine::ScriptClass& cls : package.classes) {
        totals.methods += cls.methods.size();
        totals.bodies += cls.bodies.size();
        totals.fields += cls.fields.size();
        totals.enums += cls.enums.size();
        for (const bo2::engine::ScriptBody& body : cls.bodies) {
            totals.native_bodies += body.native() ? 1U : 0U;
            totals.code_words += body.code.size();
        }
        for (const auto& refs : cls.references) {
            for (const bo2::engine::ScriptReference& ref : refs) {
                ++totals.references[ref.kind];
            }
        }
        if (cls.states.empty() || cls.states[0] != "default" || cls.methods.empty() ||
            cls.methods[0].name != "start" || cls.methods[0].slot != 0) {
            totals.start_first = false;
        }
    }
}

void test_sl01_packages(const std::filesystem::path& root) {
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(
        (root / "media/gogames/bo2/resources/levels/slums/sl01.big").string());
    std::map<std::string, Totals> per;
    std::string first_creature;
    for (const bo2::engine::BigEntry& entry : archive.entries()) {
        if (entry.type != "bpackage") {
            continue;
        }
        const bo2::engine::ScriptPackage package =
            bo2::engine::parse_script_package(archive.extract(entry), entry.symbols, archive.names());
        add(per[entry.name], entry.name, package);
        if (entry.name == "creatures") {
            CHECK(package.imports == std::vector<std::string>{"native"});
            const bo2::engine::ScriptClass& critter = package.classes.front();
            CHECK(critter.name == "critter" && critter.parent == "monsterscript");
            CHECK(critter.data_size == 41 && critter.slot_count == 16);
            CHECK((critter.states == std::vector<std::string>{"default", "idlestate"}));
        }
        if (entry.name == "native") {
            CHECK(package.imports.empty());
        }
    }
    const Totals& native = per["native"];
    CHECK(native.classes == 58 && native.methods == 588 && native.bodies == 556);
    CHECK(native.native_bodies == 505 && native.code_words == 557);
    CHECK(native.fields == 55 && native.enums == 25);
    const Totals& creatures = per["creatures"];
    CHECK(creatures.classes == 340 && creatures.methods == 625 && creatures.bodies == 656);
    CHECK(creatures.native_bodies == 0 && creatures.code_words == 20195);
    CHECK(creatures.fields == 1085 && creatures.enums == 1);
    const Totals& sounds = per["sounds"];
    CHECK(sounds.classes == 20 && sounds.methods == 47 && sounds.bodies == 41);
    CHECK(sounds.code_words == 1598 && sounds.fields == 96 && sounds.enums == 0);
}

// The package of the sl01 region (docs/FORMATS/bpackage.md, "Region
// packages"): it imports creatures and its first class is resourcedb, with no
// parent and no states.
void test_sl01_region_package(const std::filesystem::path& root) {
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(
        (root / "media/gogames/bo2/resources/levels/slums/sl01.big").string());
    int regions = 0;
    for (const bo2::engine::BigEntry& entry : archive.entries()) {
        if (entry.type != "region") {
            continue;
        }
        ++regions;
        const bo2::engine::ScriptPackage package = bo2::engine::parse_region_script_package(
            archive.extract(entry), entry.symbols, archive.names());
        CHECK(entry.name == "slums/sl01");
        CHECK(package.imports == std::vector<std::string>{"creatures"});
        CHECK(package.classes.size() == 79);
        CHECK(!package.classes.empty() && package.classes.front().name == "resourcedb");
        CHECK(!package.classes.empty() && package.classes.front().parent.empty());
        CHECK(!package.classes.empty() && package.classes.front().states.empty());
    }
    CHECK(regions == 1);
}

}  // namespace

int main() {
    test_parse_minimal_package();
    test_alignment();
    test_chunk_padding();
    test_validation_rules();
    test_instruction_words();

    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "script package asset tests skipped (BO2NX_ASSETS_DIR is unset)\n";
    } else {
        try {
            // The 480 packages and the 165 region packages of the game are read
            // by the data checker (bo2nx_check_data, docs/DATA_COVERAGE.md):
            // loading the 179 containers takes over a minute in a debug build.
            test_sl01_packages(root);
            test_sl01_region_package(root);
        } catch (const std::exception& error) {
            std::cerr << "script package asset tests: " << error.what() << '\n';
            return 1;
        }
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "script package tests passed\n";
    return 0;
}
