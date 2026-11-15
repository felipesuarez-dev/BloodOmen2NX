#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/region.hpp"
#include "bo2/engine/script_memory.hpp"
#include "bo2/engine/script_program.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

bool throws(const std::function<void()>& action) {
    try {
        action();
    } catch (const bo2::engine::ScriptError&) {
        return true;
    }
    return false;
}

using bo2::engine::ScriptMemory;

// docs/ARCHITECTURE.md, "Script machine": nothing starts below 8, blocks are
// aligned to 4 and zeroed, and a reservation of 0 takes 4 bytes.
void test_memory_first_blocks() {
    ScriptMemory memory;
    CHECK(memory.allocate(10) == 8);
    CHECK(memory.allocate(1) == 20);
    CHECK(memory.allocate(0) == 24);
    CHECK(memory.live_blocks() == 3);
    CHECK(memory.block_size(8) == 10);
    CHECK(memory.block_size(20) == 1);
    CHECK(memory.block_size(24) == 4);
    for (std::uint32_t address = 8; address < 18; ++address) {
        CHECK(memory.u8(address) == 0);
    }
    CHECK(memory.u8(20) == 0);
    CHECK(memory.u32(24) == 0);
}

void test_memory_little_endian() {
    ScriptMemory memory;
    const std::uint32_t block = memory.allocate(8);
    memory.set_u32(block, 0x11223344U);
    CHECK(memory.u8(block) == 0x44);
    CHECK(memory.u8(block + 3) == 0x11);
    CHECK(memory.u32(block) == 0x11223344U);
    memory.set_u8(block + 4, 0x7F);
    CHECK(memory.u32(block + 4) == 0x7FU);
    memory.set_f32(block + 4, 1.5f);
    CHECK(memory.f32(block + 4) == 1.5f);
    CHECK(memory.u32(block + 4) == 0x3FC00000U);
}

void test_memory_bounds() {
    ScriptMemory memory;
    CHECK(memory.allocate(10) == 8);
    CHECK(memory.allocate(8) == 20);
    CHECK(throws([&] { static_cast<void>(memory.u32(4)); }));   // below the first block
    CHECK(throws([&] { static_cast<void>(memory.u32(16)); }));  // ends past the block of 10
    CHECK(throws([&] { static_cast<void>(memory.u32(18)); }));  // in the padding
    CHECK(throws([&] { static_cast<void>(memory.u8(0)); }));    // null
    CHECK(throws([&] { static_cast<void>(memory.u8(28)); }));   // past the arena
    CHECK(throws([&] { memory.set_u32(17, 1); }));
    CHECK(throws([&] { memory.copy(20, 14, 8); }));             // source crosses two blocks
    CHECK(throws([&] { memory.copy(14, 20, 8); }));             // target crosses two blocks
    CHECK(!throws([&] { static_cast<void>(memory.u32(14)); }));
    CHECK(throws([&] { static_cast<void>(memory.allocate(0xFFFFFFF0U)); }));
    CHECK(memory.live_blocks() == 2);
}

void test_memory_release() {
    ScriptMemory memory;
    const std::uint32_t first = memory.allocate(8);
    const std::uint32_t second = memory.allocate(8);
    memory.set_u32(first, 5);
    memory.release(first);
    CHECK(memory.live_blocks() == 1);
    CHECK(throws([&] { static_cast<void>(memory.u32(first)); }));
    CHECK(throws([&] { memory.release(first); }));       // released twice
    CHECK(throws([&] { memory.release(second + 1); }));  // not the start of a block
    CHECK(throws([&] { static_cast<void>(memory.block_size(first)); }));
    CHECK(memory.u32(second) == 0);
    // A reused block starts zeroed again.
    const std::uint32_t again = memory.allocate(4);
    CHECK(memory.u32(again) == 0);
}

void test_memory_copy() {
    ScriptMemory memory;
    const std::uint32_t from = memory.allocate(7);
    const std::uint32_t to = memory.allocate(9);
    for (std::uint32_t index = 0; index < 7; ++index) {
        memory.set_u8(from + index, static_cast<std::uint8_t>(index + 1));
    }
    memory.copy(to + 2, from, 7);
    CHECK(memory.u8(to) == 0);
    CHECK(memory.u8(to + 1) == 0);
    for (std::uint32_t index = 0; index < 7; ++index) {
        CHECK(memory.u8(to + 2 + index) == index + 1);
    }
    // Overlapping ranges inside one block.
    memory.copy(to, to + 2, 7);
    CHECK(memory.u8(to) == 1);
    CHECK(memory.u8(to + 6) == 7);
}

using bo2::engine::ScriptBody;
using bo2::engine::ScriptClass;
using bo2::engine::ScriptMethod;
using bo2::engine::ScriptPackage;
using bo2::engine::ScriptProgram;
using bo2::engine::ScriptReference;
using bo2::engine::ScriptSlot;

ScriptMethod make_method(const std::string& name, std::uint16_t slot) {
    ScriptMethod method;
    method.name = name;
    method.slot = slot;
    return method;
}

ScriptBody make_body(std::uint16_t method, std::uint16_t state, std::vector<std::uint32_t> code) {
    ScriptBody body;
    body.method = method;
    body.state = state;
    body.code = std::move(code);
    return body;
}

ScriptClass make_class(const std::string& name, const std::string& parent,
                       const std::vector<std::string>& states, std::uint16_t slot_count) {
    ScriptClass cls;
    cls.name = name;
    cls.parent = parent;
    cls.states = states;
    cls.state_owners.assign(states.size(), std::string());
    cls.slot_count = slot_count;
    return cls;
}

ScriptReference make_reference(std::uint16_t kind, std::uint16_t word, const std::string& name,
                               std::uint16_t index = 0, const std::string& second = {}) {
    ScriptReference reference;
    reference.kind = kind;
    reference.word = word;
    reference.name = name;
    reference.index = index;
    reference.second = second;
    return reference;
}

// The program points into its package, so both travel together.
struct Linked {
    std::unique_ptr<ScriptPackage> package;
    ScriptProgram program;
};

Linked make_program(std::vector<ScriptClass> classes,
                    const std::vector<std::string>& native_types = {}) {
    auto package = std::make_unique<ScriptPackage>();
    package->version = 1;
    package->classes = std::move(classes);
    for (ScriptClass& cls : package->classes) {
        cls.references.resize(cls.bodies.size());
    }
    ScriptProgram program = ScriptProgram::link({package.get()}, native_types);
    return {std::move(package), std::move(program)};
}

bool link_throws(std::vector<ScriptClass> classes) {
    return throws([&] { static_cast<void>(make_program(std::move(classes))); });
}

// A child takes the table of state 0 of its parent and writes one slot of it;
// its table of state 1 is its own (docs/SCRIPTS.md, BO2NX-151).
void test_link_parent_and_tables() {
    ScriptClass base = make_class("base", "", {"default", "busy"}, 2);
    base.methods = {make_method("start", 0), make_method("run", 1)};
    base.bodies = {make_body(0, 0, {}), make_body(0, 1, {})};
    base.slots = {{0, 0, "base", 0}, {1, 0, "base", 1}};
    ScriptClass child = make_class("child", "base", {"default", "busy"}, 2);
    child.state_owners = {"base", ""};
    child.methods = {make_method("start", 0), make_method("extra", 1)};
    child.bodies = {make_body(1, 0, {}), make_body(0, 1, {})};
    child.slots = {{0, 1, "child", 0}, {1, 0, "child", 1}};
    const Linked linked = make_program({base, child});
    const auto& classes = linked.program.classes();
    CHECK(classes.size() == 2);
    CHECK(linked.program.bodies().size() == 4);
    CHECK(linked.program.methods().size() == 4);
    CHECK(classes[0].parent == 0);
    CHECK(classes[1].parent == 1);
    CHECK(classes[0].states == (std::vector<std::vector<std::uint32_t>>{{1, 0}, {2, 0}}));
    CHECK(classes[1].states == (std::vector<std::vector<std::uint32_t>>{{1, 3}, {4, 0}}));
    CHECK(classes[1].bodies == (std::vector<std::uint32_t>{2, 3}));
    CHECK(linked.program.bodies()[3].owner == 1);
    CHECK(linked.program.bodies()[3].index == 1);
    CHECK(linked.program.find_class("child") == 2);
    CHECK(linked.program.find_class("nobody") == 0);
}

// One reference of each kind, 0 to 5 (docs/SCRIPTS.md, "Loading and
// linking").
void test_link_code_words() {
    ScriptClass alpha = make_class("alpha", "", {"default"}, 1);
    alpha.methods = {make_method("start", 0), make_method("run", bo2::engine::kScriptNoSlot)};
    alpha.bodies = {make_body(0, 0, std::vector<std::uint32_t>(9, 0x01000000U))};
    alpha.slots = {{0, 0, "alpha", 0}};
    alpha.references = {{
        make_reference(0, 0, "speed"),
        make_reference(1, 1, "mode"),
        make_reference(0, 7, "speed"),
        make_reference(2, 2, "position"),
        make_reference(3, 3, "alpha", 1),
        make_reference(4, 4, "alpha", 0),
        make_reference(5, 5, "alpha", 0, "position"),
    }};
    ScriptClass position = make_class("position", "", {"default"}, 1);
    position.kind = 5;
    position.methods = {make_method("start", 0)};
    const Linked linked = make_program({alpha, position});
    const std::vector<std::uint32_t>& code = linked.program.bodies()[0].code;
    CHECK(code[0] == 0x04000000U);  // name 0, speed
    CHECK(code[1] == 0x5F000001U);  // name 1, mode
    CHECK(code[7] == 0x04000000U);  // speed again
    CHECK(code[2] == 1);            // value type 0, position
    CHECK(code[3] == 2);            // method 1
    CHECK(code[4] == 1);            // body 0
    CHECK(code[5] == 1);            // tables of alpha
    CHECK(code[6] == 3);            // method 0 of position, program id 2
    CHECK(code[8] == 0x01000000U);  // untouched
    CHECK(linked.program.names() == (std::vector<std::string>{"speed", "mode"}));
    CHECK(linked.program.value_types() == (std::vector<std::string>{"position"}));
    CHECK(linked.program.classes()[1].value_class);
}

// A reference of kind 2 to a name that is no value type stays 0; the native
// types follow the value classes.
void test_link_missing_value_type() {
    ScriptClass alpha = make_class("alpha", "", {"default"}, 1);
    alpha.methods = {make_method("start", 0)};
    alpha.bodies = {make_body(0, 0, {7, 7, 7})};
    alpha.references = {{make_reference(2, 0, "creature"), make_reference(2, 1, "nothing"),
                         make_reference(2, 2, "activation_info")}};
    const Linked bare = make_program({alpha});
    CHECK(bare.program.bodies()[0].code == (std::vector<std::uint32_t>{0, 0, 0}));
    const Linked native = make_program({alpha}, {"activation_info", "creature"});
    CHECK(native.program.bodies()[0].code == (std::vector<std::uint32_t>{2, 0, 1}));
}

void test_link_errors() {
    ScriptClass base = make_class("base", "", {"default"}, 1);
    base.methods = {make_method("start", 0)};
    base.bodies = {make_body(0, 0, {0})};
    CHECK(!link_throws({base}));

    ScriptClass orphan = make_class("orphan", "missing", {"default"}, 1);
    CHECK(link_throws({orphan}));            // the parent does not exist
    CHECK(link_throws({base, base}));        // two classes named base

    ScriptClass slot = make_class("slot", "", {"default"}, 1);
    slot.slots = {{0, 0, "missing", 0}};
    CHECK(link_throws({slot}));              // the class of a slot entry does not exist
    slot.slots = {{0, 0, "base", 1}};
    CHECK(link_throws({base, slot}));        // body 1 of base does not exist

    ScriptClass early = make_class("early", "", {"default"}, 1);
    early.state_owners = {"base"};
    CHECK(link_throws({early, base}));       // the owner is linked after the class
    CHECK(!link_throws({base, early}));

    ScriptClass caller = base;
    caller.name = "caller";
    caller.references = {{make_reference(3, 0, "base", 1)}};
    CHECK(link_throws({base, caller}));      // method 1 of base does not exist
    caller.references = {{make_reference(4, 0, "missing", 0)}};
    CHECK(link_throws({base, caller}));      // the class does not exist
}

// The game data. Containers are read with one sized read: the stream
// iterator of BigArchive::load takes over a minute for the whole game in a
// debug build.
bo2::engine::BigArchive load_container(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::streamsize size = stream.tellg();
    stream.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(data.data()), size);
    return bo2::engine::BigArchive::parse(std::move(data));
}

const std::vector<std::string> kNativeTypes = {"activation_info", "creature"};

struct RegionPrograms {
    std::vector<std::string> names;                        // of the bpackage entries
    std::vector<std::unique_ptr<ScriptPackage>> packages;  // the bpackage entries
    std::vector<std::pair<std::string, std::unique_ptr<ScriptPackage>>> regions;
};

// The bpackage entries of a container in entry order, and the package of each
// region of it.
RegionPrograms read_container(const bo2::engine::BigArchive& archive) {
    RegionPrograms out;
    for (const bo2::engine::BigEntry& entry : archive.entries()) {
        if (entry.type == "bpackage") {
            out.names.push_back(entry.name);
            out.packages.push_back(std::make_unique<ScriptPackage>(
                bo2::engine::parse_script_package(archive.extract(entry), entry.symbols, archive.names())));
        } else if (entry.type == "region") {
            out.regions.emplace_back(entry.name, std::make_unique<ScriptPackage>(
                bo2::engine::parse_region_script_package(archive.extract(entry), entry.symbols,
                                                         archive.names())));
        }
    }
    return out;
}

ScriptProgram link_region(const RegionPrograms& container, const ScriptPackage& region,
                          const std::vector<std::string>& native_types) {
    std::vector<const ScriptPackage*> packages;
    for (const auto& package : container.packages) {
        packages.push_back(package.get());
    }
    packages.push_back(&region);
    return ScriptProgram::link(packages, native_types);
}

// The control values of sl01 (BO2NX-151): native, creatures and sounds, then
// the region package.
void test_link_sl01(const std::filesystem::path& root) {
    const bo2::engine::BigArchive archive =
        load_container(root / "media/gogames/bo2/resources/levels/slums/sl01.big");
    const RegionPrograms container = read_container(archive);
    CHECK(container.packages.size() == 3);
    CHECK(container.regions.size() == 1);
    if (container.packages.size() != 3 || container.regions.size() != 1) {
        return;
    }
    CHECK(container.names == (std::vector<std::string>{"native", "creatures", "sounds"}));
    const ScriptPackage& region = *container.regions[0].second;
    const ScriptProgram program = link_region(container, region, kNativeTypes);
    std::size_t native_bodies = 0;
    std::size_t words = 0;
    for (const auto& body : program.bodies()) {
        native_bodies += body.source->native() ? 1 : 0;
        words += body.code.size();
    }
    std::size_t slots = 0;
    std::vector<std::size_t> kinds(6, 0);
    std::size_t activation = 0;
    std::size_t creature = 0;
    std::size_t zero_kind2 = 0;
    for (const auto& cls : program.classes()) {
        slots += cls.source->slots.size();
        for (std::size_t body = 0; body < cls.source->references.size(); ++body) {
            for (const auto& reference : cls.source->references[body]) {
                ++kinds[reference.kind];
                if (reference.kind == 2) {
                    activation += reference.name == "activation_info" ? 1 : 0;
                    creature += reference.name == "creature" ? 1 : 0;
                    zero_kind2 += program.bodies()[cls.bodies[body]].code[reference.word] == 0 ? 1 : 0;
                }
            }
        }
    }
    CHECK(program.classes().size() == 497);
    CHECK(program.bodies().size() == 1320);
    CHECK(native_bodies == 505);
    CHECK(words == 24640);
    CHECK(slots == 13361);
    CHECK(kinds == (std::vector<std::size_t>{395, 278, 59, 450, 2660, 149}));
    CHECK(activation == 28);
    CHECK(creature == 8);
    CHECK(zero_kind2 == 0);

    // Without the two native types the 36 references that name them stay 0.
    const ScriptProgram bare = link_region(container, region, {});
    std::size_t bare_zero = 0;
    for (const auto& cls : bare.classes()) {
        for (std::size_t body = 0; body < cls.source->references.size(); ++body) {
            for (const auto& reference : cls.source->references[body]) {
                if (reference.kind == 2) {
                    bare_zero += bare.bodies()[cls.bodies[body]].code[reference.word] == 0 ? 1 : 0;
                }
            }
        }
    }
    CHECK(bare_zero == 36);
}

// Every region of the game links (BO2NX-151): 165 programs, 81,752 classes.
// The packages of a container start with native, and every state table, own
// or copied from its owner, has the slot count of its class.
void test_link_all_regions(const std::filesystem::path& root) {
    std::size_t programs = 0;
    std::size_t classes = 0;
    std::size_t other_first = 0;
    std::size_t other_length = 0;
    for (const auto& item : std::filesystem::recursive_directory_iterator(root / "media/gogames/bo2")) {
        if (item.path().extension() != ".big") {
            continue;
        }
        const bo2::engine::BigArchive archive = load_container(item.path());
        const RegionPrograms container = read_container(archive);
        for (const auto& [name, region] : container.regions) {
            other_first += container.names.empty() || container.names.front() != "native" ? 1 : 0;
            const ScriptProgram program = link_region(container, *region, kNativeTypes);
            ++programs;
            classes += program.classes().size();
            for (const auto& cls : program.classes()) {
                for (const auto& table : cls.states) {
                    other_length += table.size() != cls.source->slot_count ? 1 : 0;
                }
            }
        }
    }
    CHECK(programs == 165);
    CHECK(classes == 81752);
    CHECK(other_first == 0);
    CHECK(other_length == 0);
}

}  // namespace

int main() {
    test_memory_first_blocks();
    test_memory_little_endian();
    test_memory_bounds();
    test_memory_release();
    test_memory_copy();
    test_link_parent_and_tables();
    test_link_code_words();
    test_link_missing_value_type();
    test_link_errors();

    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "script machine asset tests skipped (BO2NX_ASSETS_DIR is unset)\n";
    } else {
        try {
            test_link_sl01(root);
            test_link_all_regions(root);
        } catch (const std::exception& error) {
            std::cerr << "script machine asset tests: " << error.what() << '\n';
            return 1;
        }
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "script machine tests passed\n";
    return 0;
}
