#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/region.hpp"
#include "bo2/engine/script_creature_natives.hpp"
#include "bo2/engine/script_timer.hpp"
#include "bo2/engine/script_machine.hpp"
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

// The machine (BO2NX-152). An instruction word is (code << 24) | imm.
constexpr std::uint32_t op(std::uint32_t code, std::int32_t imm = 0) {
    return (code << 24) | (static_cast<std::uint32_t>(imm) & 0xFFFFFFU);
}

using bo2::engine::ScriptFrame;
using bo2::engine::ScriptMachine;
using bo2::engine::ScriptObjectId;
using bo2::engine::ScriptThread;

// One class, alpha, with 16 bytes of data and one body: 16 bytes of locals
// and 8 stack cells.
ScriptClass machine_class(std::vector<std::uint32_t> code,
                          std::vector<ScriptReference> references = {}) {
    ScriptClass cls = make_class("alpha", "", {"default"}, 1);
    cls.data_size = 16;
    cls.methods = {make_method("start", 0)};
    ScriptBody body = make_body(0, 0, std::move(code));
    body.locals_size = 16;
    body.stack_size = 8;
    cls.bodies = {body};
    cls.slots = {{0, 0, "alpha", 0}};
    cls.references = {std::move(references)};
    return cls;
}

// A machine with two objects of alpha (ids 1 and 2) and a thread on the body
// of the first.
struct MachineRun {
    Linked linked;
    std::unique_ptr<ScriptMachine> machine;
    ScriptThread thread;

    explicit MachineRun(ScriptClass cls, const std::vector<std::string>& native_types = {})
        : MachineRun(std::vector<ScriptClass>{std::move(cls)}, native_types) {}

    // The objects and the thread belong to the first class.
    explicit MachineRun(std::vector<ScriptClass> classes, const std::vector<std::string>& native_types = {})
        : linked(make_program(std::move(classes), native_types)) {
        machine = std::make_unique<ScriptMachine>(linked.program);
        const ScriptObjectId self = machine->create_object(0);
        machine->create_object(0);
        thread.owner = self;
        thread.frames.push_back(machine->make_frame(self, 0));
    }

    ScriptFrame& frame() { return thread.frames.back(); }
    std::uint32_t cell(std::int32_t index) {
        return machine->memory().u32(frame().stack + static_cast<std::uint32_t>(index) * 4U);
    }
    std::uint32_t handle_count(ScriptObjectId object) {
        return machine->memory().u32(machine->handle_of(object) + 4);
    }
};

void test_push() {
    MachineRun run(machine_class(
        {op(0x02), 0x12345678U, op(0x03, -5), op(0x03, 7), op(0x01), op(0x00, 3), op(0x5C)}));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 1);
    CHECK(run.cell(0) == 0x12345678U);
    CHECK(run.cell(1) == 0xFFFFFFFBU);
    CHECK(run.frame().ip == 7);
}

void test_locals() {
    MachineRun run(machine_class({op(0x05, 4), op(0x06, 4), op(0x07, 4), op(0x05, 8), op(0x5C)}));
    run.machine->memory().set_u32(run.frame().locals + 4, 0x01020384U);
    run.machine->memory().set_u8(run.frame().locals + 8, 0xFE);
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 3);
    CHECK(run.cell(0) == 0x84);  // the byte, without sign
    CHECK(run.cell(1) == 0x01020384U);
    CHECK(run.cell(2) == run.frame().locals + 4);
    CHECK(run.cell(3) == 0xFE);
}

void test_self_fields() {
    MachineRun run(machine_class({op(0x09, 4), op(0x0A, 4), op(0x0B, 8), op(0x5C)}));
    const bo2::engine::ScriptAddress data = run.machine->data_of(1);
    run.machine->memory().set_u32(data + 4, 0xA1B2C3D4U);
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.cell(0) == 0xD4);
    CHECK(run.cell(1) == 0xA1B2C3D4U);
    CHECK(run.cell(2) == data + 8);
}

void test_object_fields() {
    MachineRun run(machine_class(
        {op(0x02), 2, op(0x0E, 12), op(0x02), 2, op(0x0F, 12), op(0x03, 0), op(0x0E, 12),
         op(0x03, 0), op(0x0F, 12), op(0x5C)}));
    run.machine->memory().set_u32(run.machine->data_of(2) + 12, 77);
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 3);
    CHECK(run.cell(0) == 77);
    CHECK(run.cell(1) == run.machine->data_of(2) + 12);
    CHECK(run.cell(2) == 0);
    CHECK(run.cell(3) == 0);
}

void test_handles() {
    MachineRun run(machine_class({op(0x08, 0), op(0x08, 4), op(0x0C, 0), op(0x0C, 4), op(0x5C)}));
    bo2::engine::ScriptMemory& memory = run.machine->memory();
    memory.set_u32(run.frame().locals + 4, run.machine->handle_of(2));
    memory.set_u32(run.machine->data_of(1) + 4, run.machine->handle_of(1));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.cell(0) == 0);
    CHECK(run.cell(1) == 2);
    CHECK(run.cell(2) == 0);
    CHECK(run.cell(3) == 1);
}

void test_store() {
    MachineRun run(machine_class({
        op(0x07, 0), op(0x02), 0xAABBCCDDU, op(0x1C),   // byte
        op(0x07, 4), op(0x02), 0x11223344U, op(0x1D),   // word
        op(0x03, 0), op(0x03, 9), op(0x1D),             // address 0: nothing
        op(0x03, 0), op(0x03, 9), op(0x1C),
        op(0x0B, 0), op(0x02), 2, op(0x1E),             // handle of object 2 into the data
        op(0x07, 8), op(0x02), 2, op(0x1E),             // and into a local
        op(0x0B, 0), op(0x02), 1, op(0x1E),             // the data now holds object 1
        op(0x03, 0), op(0x02), 2, op(0x1E),             // address 0: nothing
        op(0x5C)}));
    bo2::engine::ScriptMemory& memory = run.machine->memory();
    // Each object holds its own handle once (FUN_0057f830).
    CHECK(run.handle_count(1) == 1);
    CHECK(run.handle_count(2) == 1);
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == -1);
    CHECK(memory.u32(run.frame().locals) == 0xDD);
    CHECK(memory.u32(run.frame().locals + 4) == 0x11223344U);
    CHECK(memory.u32(run.frame().locals + 8) == run.machine->handle_of(2));
    CHECK(memory.u32(run.machine->data_of(1)) == run.machine->handle_of(1));
    CHECK(run.handle_count(1) == 2);
    CHECK(run.handle_count(2) == 2);
}

void test_store_null_object() {
    MachineRun run(machine_class({op(0x07, 0), op(0x02), 2, op(0x1E), op(0x07, 0), op(0x03, 0),
                                  op(0x1E), op(0x5C)}));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.machine->memory().u32(run.frame().locals) == 0);
    CHECK(run.handle_count(2) == 1);
}

// 0x04 comes from a reference of kind 0; 0x1f takes the record of the name,
// counted (FUN_0068eaa0).
void test_strings() {
    MachineRun run(machine_class(
        {0, op(0x01), op(0x07, 0), 0, op(0x1F), op(0x07, 0), 0, op(0x1F), op(0x5C)},
        {make_reference(0, 0, "first"), make_reference(0, 3, "second"), make_reference(0, 6, "second")}));
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    CHECK(run.linked.program.names() == (std::vector<std::string>{"first", "second"}));
    CHECK(machine.string_at(machine.name_cell(0)) == "first");
    CHECK(machine.string_at(machine.name_cell(1)) == "second");
    const bo2::engine::ScriptAddress record = memory.u32(machine.name_cell(1));
    CHECK(memory.u32(record) == 1);
    // The local holds a string of its own, counted once.
    const bo2::engine::ScriptAddress own = machine.new_string_cell("old");
    const bo2::engine::ScriptAddress own_record = memory.u32(own);
    memory.set_u32(run.frame().locals, own_record);
    CHECK(machine.interpret(run.thread));
    CHECK(memory.u32(run.frame().locals) == record);
    CHECK(memory.u32(record) == 2);  // the second store found the same record
    CHECK(machine.string_at(run.frame().locals) == "second");
    // The old record was released when its count reached 0.
    CHECK(throws([&] { static_cast<void>(memory.u32(own_record)); }));
    CHECK(machine.string_at(0).empty());
    CHECK(throws([&] { static_cast<void>(machine.name_cell(2)); }));
}

void test_unported_code() {
    CHECK(throws([] {
        MachineRun run(machine_class({op(0x03, 1), op(0x10, 0), op(0x5C)}));
        run.machine->interpret(run.thread);
    }));
    CHECK(throws([] {
        MachineRun run(machine_class({op(0x61), op(0x5C)}));
        run.machine->interpret(run.thread);
    }));
    CHECK(throws([] {  // a call word that names no body
        MachineRun run(machine_class({op(0x48), 0, op(0x5C)}));
        run.machine->interpret(run.thread);
    }));
    CHECK(throws([] {
        MachineRun run(machine_class({op(0x07, 0), op(0x07, 4), op(0x21), 0, op(0x5C)}));
        run.machine->interpret(run.thread);
    }));
    CHECK(throws([] {  // no 0x5c: the code runs out
        MachineRun run(machine_class({op(0x00)}));
        run.machine->interpret(run.thread);
    }));
    CHECK(throws([] {  // a pop from an empty stack
        MachineRun run(machine_class({op(0x01), op(0x5C)}));
        run.machine->interpret(run.thread);
    }));
}

// Arithmetic, logic and flow (BO2NX-153).
std::uint32_t bits(float value) {
    std::uint32_t out = 0;
    std::memcpy(&out, &value, sizeof out);
    return out;
}

float as_float(std::uint32_t value) {
    float out = 0.0f;
    std::memcpy(&out, &value, sizeof out);
    return out;
}

void test_int_ops() {
    MachineRun run(machine_class({op(0x02), 0x7FFFFFFFU, op(0x03, 1), op(0x28),  // wraps
                                  op(0x03, 5), op(0x27),
                                  op(0x03, 3), op(0x03, 10), op(0x29),
                                  op(0x5C)}));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 2);
    CHECK(run.cell(0) == 0x80000000U);
    CHECK(run.cell(1) == static_cast<std::uint32_t>(-5));
    CHECK(run.cell(2) == static_cast<std::uint32_t>(-7));
}

// A comparison writes only the low byte of the first operand's cell.
void test_int_compare() {
    MachineRun run(machine_class({op(0x02), 0x11223300U, op(0x02), 0x11223301U, op(0x2D),
                                  op(0x02), 0x11223300U, op(0x02), 0x11223300U, op(0x2E),
                                  op(0x02), 0x11223305U, op(0x02), 0x11223300U, op(0x2F),
                                  op(0x03, -1), op(0x03, 1), op(0x2D),
                                  op(0x03, 1), op(0x03, -1), op(0x2E),
                                  op(0x5C)}));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 4);
    CHECK(run.cell(0) == 0x11223301U);
    CHECK(run.cell(1) == 0x11223301U);
    CHECK(run.cell(2) == 0x11223300U);
    CHECK(run.cell(3) == 0xFFFFFF01U);  // signed: -1 < 1
    CHECK(run.cell(4) == 0x00000000U);  // 1 <= -1 is false
}

void test_float_ops() {
    MachineRun run(machine_class({
        op(0x02), bits(1.5f), op(0x30),
        op(0x02), bits(1.5f), op(0x02), bits(2.25f), op(0x31),
        op(0x02), bits(1.5f), op(0x02), bits(2.25f), op(0x32),
        op(0x02), bits(1.5f), op(0x02), bits(2.0f), op(0x33),
        op(0x02), bits(3.0f), op(0x02), bits(2.0f), op(0x34),
        op(0x02), bits(1.0f), op(0x02), bits(0.0f), op(0x34),
        op(0x02), bits(-1.0f), op(0x02), bits(0.0f), op(0x34),
        op(0x02), bits(0.0f), op(0x02), bits(-0.0f), op(0x34),
        op(0x5C)}));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 7);
    CHECK(as_float(run.cell(0)) == -1.5f);
    CHECK(as_float(run.cell(1)) == 3.75f);
    CHECK(as_float(run.cell(2)) == -0.75f);
    CHECK(as_float(run.cell(3)) == 3.0f);
    CHECK(as_float(run.cell(4)) == 1.5f);
    CHECK(run.cell(5) == 0x7F7FFFFFU);  // the largest float, 0x0086dee8
    CHECK(run.cell(6) == 0xFF7FFFFFU);
    CHECK(run.cell(7) == 0);
}

void test_float_compare() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    MachineRun run(machine_class({
        op(0x02), bits(1.0f), op(0x02), bits(2.0f), op(0x35),
        op(0x02), bits(2.0f), op(0x02), bits(1.0f), op(0x35),
        op(0x02), bits(nan), op(0x02), bits(1.0f), op(0x35),
        op(0x02), bits(1.0f), op(0x02), bits(1.0f), op(0x36),
        op(0x02), bits(nan), op(0x02), bits(1.0f), op(0x36),
        op(0x5C)}));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 4);
    CHECK((run.cell(0) & 0xFFU) == 1);
    CHECK((run.cell(1) & 0xFFU) == 0);
    CHECK((run.cell(2) & 0xFFU) == 1);  // with NaN, "less" is true
    CHECK((run.cell(3) & 0xFFU) == 1);
    CHECK((run.cell(4) & 0xFFU) == 0);  // with NaN, "less or equal" is false
    CHECK((run.cell(0) & 0xFFFFFF00U) == (bits(1.0f) & 0xFFFFFF00U));
}

// Codes only the packages of the regions use: 0x0d reads a byte of an
// object's data, 0x18 bounds an index and 0x17 moves an address by it, 0x37
// compares floats for equality.
void test_region_codes() {
    MachineRun run(machine_class({
        op(0x02), 2, op(0x0D, 12), op(0x03, 0), op(0x0D, 12),
        op(0x03, 1000), op(0x03, 2), op(0x18, 3), op(0x17, 12),
        op(0x03, 1000), op(0x03, 3), op(0x18, 3), op(0x17, 12),
        op(0x03, 1000), op(0x02), 0xFFFFFFFFU, op(0x18, 3), op(0x17, 12),
        op(0x5C)}));
    run.machine->memory().set_u32(run.machine->data_of(2) + 12, 0x1234ABCDU);
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 4);
    CHECK(run.cell(0) == 0xCD);
    CHECK(run.cell(1) == 0);     // object 0
    CHECK(run.cell(2) == 1024);  // 1000 + 2 * 12
    CHECK(run.cell(3) == 0);     // index 3 is not below 3
    CHECK(run.cell(4) == 0);     // index -1 is below 0

    const float nan = std::numeric_limits<float>::quiet_NaN();
    MachineRun equal(machine_class({
        op(0x02), bits(1.0f), op(0x02), bits(1.0f), op(0x37),
        op(0x02), bits(1.0f), op(0x02), bits(2.0f), op(0x37),
        op(0x02), bits(nan), op(0x02), bits(nan), op(0x37),
        op(0x5C)}));
    CHECK(equal.machine->interpret(equal.thread));
    CHECK(equal.frame().top == 2);
    CHECK((equal.cell(0) & 0xFFU) == 1);
    CHECK((equal.cell(1) & 0xFFU) == 0);
    CHECK((equal.cell(2) & 0xFFU) == 0);  // NaN is not equal to itself
    CHECK((equal.cell(0) & 0xFFFFFF00U) == (bits(1.0f) & 0xFFFFFF00U));  // only the low byte
}

// FUN_0068ec30: two strings are equal when their bytes are.
void test_string_compare() {
    MachineRun run(machine_class({op(0x06, 0), op(0x06, 4), op(0x38),
                                  op(0x06, 0), op(0x06, 8), op(0x38),
                                  op(0x03, 0), op(0x06, 0), op(0x38),
                                  op(0x06, 0), op(0x06, 12), op(0x38),
                                  op(0x5C)}));
    ScriptMachine& machine = *run.machine;
    const bo2::engine::ScriptAddress locals = run.frame().locals;
    machine.memory().set_u32(locals, machine.new_string_cell("abc"));
    machine.memory().set_u32(locals + 4, machine.new_string_cell("abc"));
    machine.memory().set_u32(locals + 8, machine.new_string_cell("abd"));
    machine.memory().set_u32(locals + 12, machine.new_string_cell("ABC"));
    CHECK(machine.interpret(run.thread));
    CHECK(run.frame().top == 3);
    CHECK((run.cell(0) & 0xFFU) == 1);
    CHECK((run.cell(1) & 0xFFU) == 0);
    CHECK((run.cell(2) & 0xFFU) == 0);  // a null string
    CHECK((run.cell(3) & 0xFFU) == 0);  // case counts
}

void test_logic() {
    MachineRun run(machine_class({op(0x03, 0), op(0x39), op(0x03, 5), op(0x39),
                                  op(0x02), 0xAABBCC00U, op(0x39),
                                  op(0x03, 1), op(0x03, 0), op(0x3A),
                                  op(0x03, 2), op(0x03, 3), op(0x3A),
                                  op(0x03, 0), op(0x03, 0), op(0x3B),
                                  op(0x03, 0), op(0x03, 4), op(0x3B),
                                  op(0x03, 0x100), op(0x39),  // only the low byte counts
                                  op(0x5C)}));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 7);
    CHECK(run.cell(0) == 1);
    CHECK(run.cell(1) == 0);
    CHECK(run.cell(2) == 0xAABBCC01U);
    CHECK(run.cell(3) == 0);
    CHECK(run.cell(4) == 1);
    CHECK(run.cell(5) == 0);
    CHECK(run.cell(6) == 1);
    CHECK(run.cell(7) == 0x101);
}

// 0x3c is __ftol (0x7a3af4): truncated to 64 bits, the low 32 bits kept.
void test_conversions() {
    MachineRun run(machine_class({op(0x02), bits(-2.75f), op(0x3C),
                                  op(0x02), bits(2.75f), op(0x3C),
                                  op(0x02), bits(5.0e9f), op(0x3C),
                                  op(0x02), bits(std::numeric_limits<float>::quiet_NaN()), op(0x3C),
                                  op(0x03, -7), op(0x3E),
                                  op(0x5C)}));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 4);
    CHECK(run.cell(0) == static_cast<std::uint32_t>(-2));
    CHECK(run.cell(1) == 2);
    CHECK(run.cell(2) == static_cast<std::uint32_t>(5000000000ULL & 0xFFFFFFFFULL));
    CHECK(run.cell(3) == 0);
    CHECK(as_float(run.cell(4)) == -7.0f);
}

// The sum of 1 to 10 with 0x46 and 0x60: i in the local at 0, the sum at 4.
void test_jumps() {
    MachineRun run(machine_class({
        op(0x07, 0), op(0x03, 1), op(0x1D),                                // 0: i = 1
        op(0x07, 4), op(0x06, 4), op(0x06, 0), op(0x28), op(0x1D),         // 3: sum += i
        op(0x07, 0), op(0x06, 0), op(0x03, 1), op(0x28), op(0x1D),         // 8: i += 1
        op(0x06, 0), op(0x03, 11), op(0x2D), op(0x46, 1),                  // 13: i < 11, else to 18
        op(0x60, -15),                                                     // 17: back to 3
        op(0x5C)}));                                                       // 18
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.machine->memory().u32(run.frame().locals + 4) == 55);
    CHECK(run.frame().top == -1);
    CHECK(run.machine->warnings().empty());

    MachineRun skip(machine_class({op(0x45, 1), op(0x03, 9), op(0x03, 4), op(0x5C)}));
    CHECK(skip.machine->interpret(skip.thread));
    CHECK(skip.frame().top == 0);
    CHECK(skip.cell(0) == 4);
}

// An endless loop ends its frame on the 10,001st 0x60, with the warning of
// the original; a new interpret starts the count again.
void test_loop_guard() {
    const std::vector<std::uint32_t> code = {
        op(0x07, 0), op(0x06, 0), op(0x03, 1), op(0x28), op(0x1D),  // count += 1
        op(0x60, -6),                                               // back to 0
        op(0x5C)};
    MachineRun run(machine_class(code));
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.machine->memory().u32(run.frame().locals) == 10001);
    CHECK(run.machine->warnings() ==
          (std::vector<std::string>{"WARNING: Scripted loop ran too long: alpha, start"}));
    run.thread.frames.back() = run.machine->make_frame(1, 0);
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.machine->memory().u32(run.frame().locals) == 10001);
    CHECK(run.machine->warnings().size() == 2);
}

void test_yield() {
    MachineRun run(machine_class({op(0x03, 1), op(0x5D), op(0x03, 2), op(0x5C)}));
    CHECK(!run.machine->interpret(run.thread));
    CHECK(run.frame().top == 0);
    CHECK(run.frame().ip == 2);
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 1);
    CHECK(run.cell(1) == 2);
}

// 0x5f keeps an object whose class or an ancestor has the name; the word
// comes from a reference of kind 1.
void test_cast() {
    ScriptClass child = machine_class(
        {op(0x02), 1, 0, op(0x02), 1, 0, op(0x03, 0), 0, op(0x02), 1, 0, op(0x5C)},
        {make_reference(1, 2, "base"), make_reference(1, 5, "other"), make_reference(1, 7, "base"),
         make_reference(1, 10, "alpha")});
    child.parent = "base";
    ScriptClass base = make_class("base", "", {"default"}, 1);
    ScriptClass other = make_class("other", "", {"default"}, 1);
    MachineRun run(std::vector<ScriptClass>{child, base, other});
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.frame().top == 3);
    CHECK(run.cell(0) == 1);  // alpha derives from base
    CHECK(run.cell(1) == 0);  // not an other
    CHECK(run.cell(2) == 0);  // null stays null
    CHECK(run.cell(3) == 1);  // its own class
}

// Calls (BO2NX-154).
using bo2::engine::ScriptAddress;
using bo2::engine::ScriptType;
using bo2::engine::ScriptVariable;

ScriptVariable variable(std::uint16_t kind, std::uint32_t offset) {
    ScriptVariable out;
    out.type.kind = kind;
    out.offset = offset;
    return out;
}

ScriptMethod method_with(const std::string& name, std::uint16_t slot, std::vector<ScriptVariable> params,
                         std::uint16_t result_kind) {
    ScriptMethod method = make_method(name, slot);
    method.params = std::move(params);
    method.result.kind = result_kind;
    return method;
}

ScriptBody native_body(std::uint16_t method) {
    ScriptBody body = make_body(method, 0, {});
    body.flags = 1;
    return body;
}

ScriptBody script_body(std::uint16_t method, std::uint16_t state, std::vector<std::uint32_t> code,
                       std::uint16_t locals_size = 16) {
    ScriptBody body = make_body(method, state, std::move(code));
    body.locals_size = locals_size;
    body.stack_size = 8;
    return body;
}

// data + offset += 1, then the frame ends.
std::vector<std::uint32_t> count_into(std::int32_t offset) {
    return {op(0x0B, offset), op(0x0A, offset), op(0x03, 1), op(0x28), op(0x1D), op(0x5C)};
}

struct NativeSeen {
    ScriptObjectId self = 0;
    std::uint32_t argc = 0;
    std::uint32_t first = 0;
    std::uint32_t second = 0;
} native_seen;

void native_sum(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc, ScriptAddress args,
                ScriptAddress result) {
    native_seen = {self, argc, machine.memory().u32(args), machine.memory().u32(args + 4)};
    machine.memory().set_u32(result, native_seen.first + native_seen.second);
}

void test_native_call() {
    ScriptClass cls = machine_class(
        {op(0x03, 0x55), op(0x03, 5), op(0x03, 7), op(0x07, 0), op(0x02), 2, op(0x47), 0, op(0x5C)},
        {make_reference(4, 7, "alpha", 1)});
    cls.methods.push_back(method_with("sum", bo2::engine::kScriptNoSlot,
                                      {variable(2, 4), variable(2, 8)}, 2));
    cls.bodies.push_back(native_body(1));
    MachineRun run(cls);
    CHECK(throws([&] { run.machine->bind_native("alpha", 0, native_sum); }));  // a script body
    CHECK(throws([&] { run.machine->bind_native("beta", 1, native_sum); }));
    CHECK(throws([&] { run.machine->bind_native("alpha", 2, native_sum); }));
    run.machine->bind_native("alpha", 1, native_sum);
    CHECK(run.machine->interpret(run.thread));
    CHECK(native_seen.self == 2);
    CHECK(native_seen.argc == 2);
    CHECK(native_seen.first == 5);
    CHECK(native_seen.second == 7);
    CHECK(run.machine->memory().u32(run.frame().locals) == 12);
    CHECK(run.frame().top == 0);
    CHECK(run.cell(0) == 0x55);
}

// The callee writes its argument times two into the result; the caller then
// yields so that its frame can be read.
void test_script_call() {
    ScriptClass cls = machine_class({op(0x03, 21), op(0x07, 8), op(0x48), 0, op(0x5D), op(0x5C)},
                                    {make_reference(4, 3, "alpha", 1)});
    cls.methods.push_back(method_with("twice", bo2::engine::kScriptNoSlot, {variable(2, 4)}, 2));
    cls.bodies.push_back(script_body(1, 0, {op(0x06, 0), op(0x06, 4), op(0x06, 4), op(0x28), op(0x1D), op(0x5C)}, 8));
    MachineRun run(cls);
    const std::size_t blocks = run.machine->memory().live_blocks();
    CHECK(run.machine->run_thread(run.thread));
    CHECK(run.thread.frames.size() == 1);
    CHECK(run.machine->memory().u32(run.frame().locals + 8) == 42);
    CHECK(run.frame().top == -1);
    CHECK(run.machine->memory().live_blocks() == blocks);  // the callee's stack and locals are freed
    CHECK(!run.machine->run_thread(run.thread));
    CHECK(run.thread.frames.empty());
}

// 0x4a picks the body in the slot of the method in the table of the current
// state, or of state 0 with bit 0 of imm.
void test_virtual_call() {
    ScriptClass cls = machine_class(
        {op(0x4A), 0, op(0x0B, 0), op(0x03, 1), op(0x1C), op(0x4A), 0, op(0x4A, 1), 0, op(0x5D), op(0x5C)},
        {make_reference(3, 1, "alpha", 1), make_reference(3, 6, "alpha", 1), make_reference(3, 8, "alpha", 1)});
    cls.states = {"default", "busy"};
    cls.state_owners = {"", ""};
    cls.slot_count = 2;
    cls.methods.push_back(make_method("act", 1));
    cls.bodies.push_back(script_body(1, 0, count_into(4)));
    cls.bodies.push_back(script_body(1, 1, count_into(8)));
    cls.slots = {{0, 0, "alpha", 0}, {0, 1, "alpha", 1}, {1, 1, "alpha", 2}};
    MachineRun run(cls);
    CHECK(run.machine->run_thread(run.thread));
    const ScriptAddress data = run.machine->data_of(1);
    CHECK(run.machine->memory().u32(data + 4) == 2);  // state 0, then state 0 by bit 0
    CHECK(run.machine->memory().u32(data + 8) == 1);  // state 1
}

// 0x49: the tables of another class (base) and one of its methods.
void test_table_call() {
    ScriptClass cls = machine_class(
        {op(0x49), 0, 0,                                // state 0: body 1 of base
         op(0x0B, 0), op(0x03, 1), op(0x1C), op(0x49), 0, 0,  // state 1: body 2
         op(0x0B, 0), op(0x03, 5), op(0x1C), op(0x49), 0, 0,  // state 5: base has 2, so state 0
         op(0x49), 0, 0,                                // slot 0 with state 5: no body
         op(0x5D), op(0x5C)},
        {make_reference(5, 1, "base", 1, "base"), make_reference(5, 7, "base", 1, "base"),
         make_reference(5, 13, "base", 1, "base"), make_reference(5, 16, "base", 0, "base")});
    ScriptClass base = make_class("base", "", {"default", "busy"}, 2);
    base.methods = {make_method("start", 0), make_method("act", 1)};
    base.bodies = {script_body(0, 0, {op(0x5C)}), script_body(1, 0, count_into(4)), script_body(1, 1, count_into(8))};
    base.slots = {{0, 0, "base", 0}, {0, 1, "base", 1}, {1, 1, "base", 2}};
    MachineRun run(std::vector<ScriptClass>{cls, base});
    CHECK(run.machine->run_thread(run.thread));
    const ScriptAddress data = run.machine->data_of(1);
    CHECK(run.machine->memory().u32(data + 4) == 2);
    CHECK(run.machine->memory().u32(data + 8) == 1);
    CHECK(run.frame().top == -1);
}

// A call on object 0 only removes its cells.
void test_call_on_null() {
    ScriptClass cls = machine_class(
        {op(0x03, 0x55), op(0x03, 9), op(0x03, 0), op(0x4B), 0,
         op(0x03, 9), op(0x03, 0), op(0x4C), 0, op(0x5C)},
        {make_reference(4, 4, "alpha", 1), make_reference(3, 8, "alpha", 1)});
    cls.methods.push_back(method_with("take", bo2::engine::kScriptNoSlot, {variable(2, 4)}, 0));
    cls.bodies.push_back(script_body(1, 0, {op(0x5C)}));
    MachineRun run(cls);
    CHECK(run.machine->interpret(run.thread));
    CHECK(run.thread.frames.size() == 1);
    CHECK(run.frame().top == 0);
    CHECK(run.cell(0) == 0x55);
}

// A string parameter shares the record of its argument, counted; a string
// local starts as the shared empty string.
void test_string_param() {
    ScriptClass cls = machine_class({0, op(0x48), 0, op(0x5C)},
                                    {make_reference(0, 0, "hello"), make_reference(4, 2, "alpha", 1)});
    cls.methods.push_back(method_with("hear", bo2::engine::kScriptNoSlot, {variable(4, 4)}, 0));
    ScriptBody hear = script_body(1, 0, {op(0x5D), op(0x5C)}, 12);
    hear.locals = {variable(4, 8)};
    cls.bodies.push_back(hear);
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    const ScriptAddress record = machine.memory().u32(machine.name_cell(0));
    CHECK(machine.memory().u32(record) == 1);
    CHECK(machine.run_thread(run.thread));
    CHECK(run.thread.frames.size() == 2);
    const ScriptAddress locals = run.frame().locals;
    CHECK(machine.memory().u32(locals + 4) == record);
    CHECK(machine.memory().u32(record) == 2);
    CHECK(machine.string_at(locals + 4) == "hello");
    CHECK(machine.string_at(locals + 8).empty());
    CHECK(machine.memory().u32(locals + 8) != 0);
}

ScriptVariable array_variable(std::uint16_t element_kind, std::uint32_t count, std::uint32_t offset) {
    ScriptVariable out = variable(6, offset);
    ScriptType element;
    element.kind = element_kind;
    out.type.inner = {element};
    out.type.count = count;
    return out;
}

// The end of a frame (FUN_0057d690, FUN_00588570): the parameters and the
// locals give back the strings and the handles they hold, and the stack and
// the locals are freed.
void test_frame_end() {
    ScriptClass cls = machine_class({0, op(0x02), 2, op(0x48), 0, op(0x5D), op(0x5C)},
                                    {make_reference(0, 0, "hello"), make_reference(4, 4, "alpha", 1)});
    cls.methods.push_back(method_with("keep", bo2::engine::kScriptNoSlot, {variable(4, 4), variable(10, 8)}, 0));
    ScriptBody keep = script_body(1, 0, {op(0x5D), op(0x5C)}, 20);
    keep.locals = {variable(4, 12), variable(10, 16)};
    cls.bodies.push_back(keep);
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress record = memory.u32(machine.name_cell(0));
    const std::size_t blocks = memory.live_blocks();
    CHECK(machine.run_thread(run.thread));  // the callee yields
    CHECK(run.thread.frames.size() == 2);
    const ScriptAddress locals = run.frame().locals;
    const ScriptAddress empty = memory.u32(locals + 12);
    const std::uint32_t empty_count = memory.u32(empty);
    CHECK(memory.u32(record) == 2);
    CHECK(run.handle_count(2) == 2);
    CHECK(memory.u32(locals + 16) == 0);
    CHECK(machine.run_thread(run.thread));  // the callee ends, the caller yields
    CHECK(run.thread.frames.size() == 1);
    CHECK(memory.u32(record) == 1);
    CHECK(run.handle_count(2) == 1);
    CHECK(memory.u32(empty) == empty_count - 1);
    CHECK(memory.live_blocks() == blocks);
    CHECK(!machine.run_thread(run.thread));
}

// Arrays (kind 6): an argument is the address of the source elements, and 0
// gives each element its initial value; a local array starts element by
// element and gives each string back at the end.
void test_array_variables() {
    ScriptClass cls = machine_class({op(0x0B, 4), op(0x03, 0), op(0x48), 0, op(0x48), 0, op(0x5D), op(0x5C)},
                                    {make_reference(4, 3, "alpha", 1), make_reference(4, 5, "alpha", 2)});
    cls.methods.push_back(method_with("take", bo2::engine::kScriptNoSlot,
                                      {array_variable(2, 3, 4), array_variable(2, 2, 16)}, 0));
    cls.methods.push_back(make_method("names", bo2::engine::kScriptNoSlot));
    ScriptBody take = script_body(1, 0, {op(0x06, 4), op(0x06, 8), op(0x28), op(0x06, 12), op(0x28),
                                         op(0x06, 16), op(0x28), op(0x06, 20), op(0x28), op(0x5D), op(0x5C)}, 24);
    ScriptBody names = script_body(2, 0, {op(0x5D), op(0x5C)}, 12);
    names.locals = {array_variable(4, 2, 4)};
    cls.bodies.push_back(take);
    cls.bodies.push_back(names);
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress data = machine.data_of(1);
    memory.set_u32(data + 4, 10);
    memory.set_u32(data + 8, 20);
    memory.set_u32(data + 12, 12);
    const std::size_t blocks = memory.live_blocks();
    CHECK(machine.run_thread(run.thread));
    CHECK(run.thread.frames.size() == 2);
    CHECK(run.frame().top == 0);
    CHECK(run.cell(0) == 42);  // 10 + 20 + 12, and the second array is zero
    CHECK(machine.run_thread(run.thread));
    CHECK(run.thread.frames.size() == 2);  // now in names
    const ScriptAddress locals = run.frame().locals;
    const ScriptAddress empty = memory.u32(locals + 4);
    CHECK(empty != 0);
    CHECK(memory.u32(locals + 8) == empty);
    const std::uint32_t empty_count = memory.u32(empty);
    CHECK(machine.run_thread(run.thread));
    CHECK(memory.u32(empty) == empty_count - 2);
    CHECK(memory.live_blocks() == blocks);
}

// Child threads and tasks (BO2NX-155). data + 4 = 7, the mark of the parent.
std::vector<std::uint32_t> mark() { return {op(0x0B, 4), op(0x03, 7), op(0x1D)}; }

// A body that adds its argument to data + 8, yields, adds it again and ends.
ScriptBody worker(std::uint16_t method) {
    const std::vector<std::uint32_t> add = {op(0x0B, 8), op(0x0A, 8), op(0x06, 4), op(0x28), op(0x1D)};
    std::vector<std::uint32_t> code = add;
    code.push_back(op(0x5D));
    code.insert(code.end(), add.begin(), add.end());
    code.push_back(op(0x5C));
    return script_body(method, 0, code, 8);
}

std::vector<std::uint32_t> join(std::initializer_list<std::vector<std::uint32_t>> parts) {
    std::vector<std::uint32_t> out;
    for (const std::vector<std::uint32_t>& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

// 0x4e starts the body as a child thread with its argument and waits: the
// parent goes on in the run in which the child ends.
void test_spawn_waits() {
    ScriptClass cls = machine_class(join({{op(0x03, 5), op(0x4E), 0}, mark(), {op(0x5D), op(0x5C)}}),
                                    {make_reference(4, 2, "alpha", 1)});
    cls.methods.push_back(method_with("work", bo2::engine::kScriptNoSlot, {variable(2, 4)}, 0));
    cls.bodies.push_back(worker(1));
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress data = machine.data_of(1);
    const std::size_t blocks = memory.live_blocks();
    CHECK(machine.run_thread(run.thread));
    CHECK(memory.u32(data + 8) == 5);  // the child ran once
    CHECK(memory.u32(data + 4) == 0);  // the parent waits
    CHECK(run.frame().top == -1);      // the argument was popped
    CHECK(run.frame().waiting);
    CHECK(run.frame().children.size() == 1);
    CHECK(machine.run_thread(run.thread));
    CHECK(memory.u32(data + 8) == 10);  // the child ended
    CHECK(memory.u32(data + 4) == 7);   // and the parent went on
    CHECK(run.frame().children.empty());
    CHECK(!run.frame().waiting);
    CHECK(memory.live_blocks() == blocks);
}

// 0x56 finds the body in the slot of the method and does not wait; the frame
// that ends destroys the child that still runs.
void test_spawn_no_wait() {
    ScriptClass cls = machine_class(join({{op(0x03, 2), op(0x56), 0}, mark(), {op(0x5C)}}),
                                    {make_reference(3, 2, "alpha", 1)});
    cls.slot_count = 2;
    cls.methods.push_back(method_with("work", 1, {variable(2, 4)}, 0));
    cls.bodies.push_back(worker(1));
    cls.slots = {{0, 0, "alpha", 0}, {0, 1, "alpha", 1}};
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress data = machine.data_of(1);
    const std::size_t blocks = memory.live_blocks();
    CHECK(!machine.run_thread(run.thread));
    CHECK(memory.u32(data + 8) == 2);  // the child ran once
    CHECK(memory.u32(data + 4) == 7);  // the parent did not wait
    CHECK(run.thread.frames.empty());
    // The child's frame went with the parent's stack and locals.
    CHECK(memory.live_blocks() == blocks - 2);
}

struct TaskSeen {
    ScriptObjectId self = 0;
    ScriptObjectId owner = 0;
    std::uint32_t argc = 0;
    std::uint32_t runs = 0;
    std::uint32_t last_arg = 0;
    std::uint32_t finished = 0;
} task_seen;

class CountTask final : public bo2::engine::ScriptTask {
public:
    explicit CountTask(std::uint32_t target) : target_(target) {}
    void run(ScriptMachine&, std::uint32_t arg) override {
        ++runs_;
        ++task_seen.runs;
        task_seen.last_arg = arg;
    }
    bool running() const override { return runs_ < target_; }
    void finish(ScriptMachine&) override { ++task_seen.finished; }

private:
    std::uint32_t target_;
    std::uint32_t runs_ = 0;
};

std::unique_ptr<bo2::engine::ScriptTask> count_task(ScriptMachine& machine, ScriptObjectId self,
                                                    ScriptObjectId owner, std::uint32_t argc,
                                                    ScriptAddress args) {
    task_seen.self = self;
    task_seen.owner = owner;
    task_seen.argc = argc;
    return std::make_unique<CountTask>(machine.memory().u32(args));
}

// 0x4d calls a native body that returns a task on an object from the stack:
// the task runs once at once, then once per run of the thread, and the frame
// waits for it; on object 0 only the cells go.
void test_task_native() {
    ScriptClass cls = machine_class(
        join({{op(0x03, 0x55), op(0x03, 9), op(0x03, 0), op(0x4D), 0,  // object 0: nothing
               op(0x03, 3), op(0x02), 2, op(0x4D), 0},
              mark(), {op(0x5D), op(0x5C)}}),
        {make_reference(4, 4, "alpha", 1), make_reference(4, 9, "alpha", 1)});
    cls.methods.push_back(method_with("wait", bo2::engine::kScriptNoSlot, {variable(2, 4)}, 0));
    cls.bodies.push_back(native_body(1));
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    CHECK(throws([&] {  // the second start finds the body not bound
        machine.run_thread(run.thread);
        machine.run_thread(run.thread);
    }));
    MachineRun bound(cls);
    ScriptMachine& second = *bound.machine;
    second.bind_task_native("alpha", 1, count_task);
    CHECK(throws([&] { second.bind_task_native("alpha", 0, count_task); }));  // a script body
    task_seen = {};
    const ScriptAddress data = second.data_of(1);
    CHECK(second.run_thread(bound.thread));  // object 0 waits once with no children
    CHECK(task_seen.runs == 0);
    CHECK(bound.frame().top == 0);
    CHECK(bound.cell(0) == 0x55);
    CHECK(second.run_thread(bound.thread));
    CHECK(task_seen.self == 2);
    CHECK(task_seen.owner == 1);
    CHECK(task_seen.argc == 1);
    CHECK(task_seen.runs == 1);
    CHECK(bound.frame().top == 0);
    CHECK(second.run_thread(bound.thread, 0x99));
    CHECK(task_seen.runs == 2);
    CHECK(task_seen.last_arg == 0x99);
    CHECK(second.memory().u32(data + 4) == 0);
    CHECK(second.run_thread(bound.thread));
    CHECK(task_seen.runs == 3);
    CHECK(task_seen.finished == 1);
    CHECK(second.memory().u32(data + 4) == 7);
}

// 0x54, 0x57 and 0x58 start a child like 0x4e, 0x51 and 0x52, and 0x53 a
// task like 0x4d, without waiting: the child runs once and the parent goes
// on in the same run.
void test_start_without_waiting() {
    for (const std::uint32_t code : {0x54U, 0x57U, 0x58U}) {
        const bool popped = code != 0x54U;
        std::vector<std::uint32_t> start = {op(0x03, 2)};
        if (popped) {
            start.push_back(op(0x03, 1));  // the object, the frame's own
        }
        start.push_back(op(code));
        start.push_back(0);
        const std::uint16_t word = popped ? 3 : 2;
        ScriptClass cls = machine_class(join({start, mark(), {op(0x5C)}}),
                                        {make_reference(code == 0x58U ? 3 : 4, word, "alpha", 1)});
        cls.slot_count = 2;
        cls.methods.push_back(method_with("work", 1, {variable(2, 4)}, 0));
        cls.bodies.push_back(worker(1));
        cls.slots = {{0, 0, "alpha", 0}, {0, 1, "alpha", 1}};
        MachineRun run(cls);
        ScriptMachine& machine = *run.machine;
        const ScriptAddress data = machine.data_of(1);
        CHECK(!machine.run_thread(run.thread));
        CHECK(machine.memory().u32(data + 8) == 2);  // the child ran once
        CHECK(machine.memory().u32(data + 4) == 7);  // the parent did not wait
    }

    ScriptClass cls = machine_class(join({{op(0x03, 3), op(0x02), 2, op(0x53), 0}, mark(), {op(0x5D), op(0x5C)}}),
                                    {make_reference(4, 4, "alpha", 1)});
    cls.methods.push_back(method_with("wait", bo2::engine::kScriptNoSlot, {variable(2, 4)}, 0));
    cls.bodies.push_back(native_body(1));
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    machine.bind_task_native("alpha", 1, count_task);
    task_seen = {};
    const ScriptAddress data = machine.data_of(1);
    CHECK(machine.run_thread(run.thread));
    CHECK(task_seen.self == 2);
    CHECK(task_seen.runs == 1);                  // at once
    CHECK(machine.memory().u32(data + 4) == 7);  // and the frame went on
    CHECK(!machine.run_thread(run.thread));
    CHECK(task_seen.runs == 2);  // first the children, then the frame ended
}

// 0x5a twice, then 0x5b: each copy of the frame runs its block with the same
// locals; the frame ends when both copies have ended.
void test_parallel_block() {
    ScriptClass cls = machine_class({op(0x07, 0), op(0x03, 3), op(0x1D),  // locals + 0 = 3
                                     op(0x5A, 3), op(0x5D), op(0x5D), op(0x5C),
                                     op(0x5A, 4), op(0x07, 4), op(0x06, 0), op(0x1D), op(0x5C),
                                     op(0x5B)});
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const std::size_t blocks = memory.live_blocks();
    CHECK(machine.run_thread(run.thread));
    // The second copy ended in its first run; it leaves the list in the next.
    CHECK(run.frame().children.size() == 2);
    CHECK(memory.u32(run.frame().locals + 4) == 3);  // it read the shared locals
    CHECK(run.frame().ip == 12);
    CHECK(run.frame().waiting);
    CHECK(machine.run_thread(run.thread));
    CHECK(run.frame().children.size() == 1);
    CHECK(memory.live_blocks() == blocks);  // the copies own no stack and no locals
    CHECK(!machine.run_thread(run.thread));
    CHECK(run.thread.frames.empty());
    CHECK(memory.live_blocks() == blocks - 2);
}

// Objects and states (BO2NX-156).
using bo2::engine::ScriptField;

ScriptField field(std::uint16_t kind, std::uint32_t offset, std::uint32_t value, const std::string& name = {}) {
    ScriptField out;
    out.offset = offset;
    out.type.kind = kind;
    out.default_flag = 1;
    out.default_value = value;
    out.default_name = name;
    return out;
}

// FUN_0057fa20: the class first, then its ancestors; an offset the class
// already wrote keeps the class's default.
void test_field_defaults() {
    ScriptClass base = make_class("base", "", {"default"}, 1);
    base.data_size = 36;
    base.fields = {field(2, 4, 11), field(4, 8, 0, "basename"), field(1, 12, 1)};
    ScriptClass child = make_class("child", "base", {"default"}, 1);
    child.data_size = 36;
    float one_and_half = 1.5f;
    std::uint32_t half_bits = 0;
    std::memcpy(&half_bits, &one_and_half, sizeof half_bits);
    child.fields = {field(2, 4, 22), field(3, 16, half_bits), field(5, 20, 0x1FF), field(4, 24, 0, "hello"),
                    field(10, 28, 0), field(11, 32, 3)};
    const Linked linked = make_program({child, base});
    ScriptMachine machine(linked.program);
    const ScriptObjectId object = machine.create_object(0);
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress data = machine.data_of(object);
    CHECK(machine.state_of(object) == 0);
    CHECK(memory.u32(data + 4) == 22);  // the child's default wins
    CHECK(machine.string_at(data + 8) == "basename");
    CHECK(memory.u8(data + 12) == 1);
    CHECK(memory.f32(data + 16) == 1.5f);
    CHECK(memory.u8(data + 20) == 0xFF);
    CHECK(memory.u8(data + 21) == 0);
    CHECK(machine.string_at(data + 24) == "hello");
    CHECK(memory.u32(data + 28) == 0);
    CHECK(memory.u32(data + 32) == 3);
    const ScriptAddress hello = memory.u32(data + 24);
    CHECK(memory.u32(hello) == 2);  // the machine's own and the field's
    const ScriptObjectId second = machine.create_object(0);
    CHECK(memory.u32(machine.data_of(second) + 24) == hello);
    CHECK(memory.u32(hello) == 3);
}

// Two states whose start adds 1 to data + 4 (default) or data + 8 (busy).
ScriptClass two_state_class(std::vector<std::uint32_t> default_start) {
    ScriptClass cls = machine_class(std::move(default_start));
    cls.states = {"default", "busy"};
    cls.state_owners = {"", ""};
    cls.bodies.push_back(script_body(0, 1, count_into(8)));
    cls.slots = {{0, 0, "alpha", 0}, {1, 0, "alpha", 1}};
    return cls;
}

void test_start_runs_state_start() {
    MachineRun run(two_state_class(count_into(4)));
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress data = machine.data_of(2);
    machine.start(2);
    CHECK(memory.u32(data + 4) == 1);
    CHECK(machine.main_thread(2) == nullptr);  // the start ended
    machine.request_state(2, 1);
    CHECK(machine.state_of(2) == 1);
    CHECK(machine.main_thread(2) != nullptr);  // made, not run
    CHECK(memory.u32(data + 8) == 0);
    machine.run(2);
    CHECK(memory.u32(data + 8) == 1);
    CHECK(machine.main_thread(2) == nullptr);
    machine.start(2);  // not in state 0
    CHECK(memory.u32(data + 4) == 1);
    CHECK(!machine.warnings().empty());
    CHECK(machine.warnings().back() == "WARNING: Branch occurred before ScriptObject::StartObject() called!");
}

// 0x5e while the object runs: the state waits, the frame ends, and the state
// is taken when the run ends; the new start runs at the next run.
void test_request_state() {
    MachineRun run(two_state_class(
        {op(0x0B, 4), op(0x03, 1), op(0x1D), op(0x03, 1), op(0x5E), op(0x0B, 12), op(0x03, 1), op(0x1D),
         op(0x5C)}));
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress data = machine.data_of(2);
    machine.start(2);
    CHECK(memory.u32(data + 4) == 1);
    CHECK(memory.u32(data + 12) == 0);  // the frame ended at 0x5e
    CHECK(machine.state_of(2) == 1);
    CHECK(machine.pending_state_of(2) == 0);
    CHECK(memory.u32(data + 8) == 0);
    machine.run(2);
    CHECK(memory.u32(data + 8) == 1);
}

void native_go_busy(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress, ScriptAddress) {
    machine.request_state(self, 1);
}

// A native that leaves a state pending on the owner ends the frame
// (FUN_0057f580).
void test_native_state_ends_frame() {
    ScriptClass cls = two_state_class({op(0x48), 0, op(0x0B, 12), op(0x03, 1), op(0x1D), op(0x5C)});
    cls.methods.push_back(make_method("go", bo2::engine::kScriptNoSlot));
    cls.bodies.push_back(native_body(1));
    cls.references = {{make_reference(4, 1, "alpha", 2)}};
    MachineRun run(cls);
    ScriptMachine& machine = *run.machine;
    machine.bind_native("alpha", 2, native_go_busy);
    machine.start(2);
    CHECK(machine.memory().u32(machine.data_of(2) + 12) == 0);
    CHECK(machine.state_of(2) == 1);
}

void test_request_invalid_state() {
    MachineRun run(two_state_class(count_into(4)));
    ScriptMachine& machine = *run.machine;
    machine.request_state(2, 0);
    machine.request_state(2, 2);
    machine.request_state(2, -1);
    CHECK(machine.state_of(2) == 0);
    CHECK(machine.pending_state_of(2) == 0);
    CHECK(machine.main_thread(2) == nullptr);
}

void test_stop() {
    MachineRun run(two_state_class({op(0x5D), op(0x5C)}));
    ScriptMachine& machine = *run.machine;
    const std::size_t blocks = machine.memory().live_blocks();
    machine.start(2);
    CHECK(machine.main_thread(2) != nullptr);  // it yielded
    CHECK(machine.memory().live_blocks() == blocks + 2);
    machine.stop(2);
    CHECK(machine.main_thread(2) == nullptr);
    CHECK(machine.state_of(2) == 0);
    CHECK(machine.memory().live_blocks() == blocks);
}

// Value types (BO2NX-157).
using bo2::engine::ScriptValueType;

void set_floats(bo2::engine::ScriptMemory& memory, ScriptAddress block, std::vector<float> values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        memory.set_f32(block + static_cast<std::uint32_t>(index * 4), values[index]);
    }
}

bool floats_are(const bo2::engine::ScriptMemory& memory, ScriptAddress block, std::vector<float> values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (memory.f32(block + static_cast<std::uint32_t>(index * 4)) != values[index]) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> float_chunk(std::vector<float> values) {
    std::vector<std::uint8_t> out(values.size() * 4);
    std::memcpy(out.data(), values.data(), out.size());
    return out;
}

// init, assign and destroy of position (FUN_0079e3c0, FUN_0079e450,
// FUN_0079e430).
void test_position_type() {
    MachineRun run(machine_class({op(0x5C)}));
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    ScriptValueType& position = machine.value_type("position");
    const ScriptAddress first = memory.allocate(4);
    const ScriptAddress second = memory.allocate(4);
    const std::size_t blocks = memory.live_blocks();
    position.init(machine, first, 0);
    const ScriptAddress block = memory.u32(first);
    CHECK(memory.block_size(block) == 12);
    CHECK(floats_are(memory, block, {0, 0, 0}));
    set_floats(memory, block, {1, 2, 3});
    position.init(machine, second, block);  // a copy
    const ScriptAddress copy = memory.u32(second);
    CHECK(copy != block);
    CHECK(floats_are(memory, copy, {1, 2, 3}));
    position.assign(machine, second, 0);
    CHECK(memory.u32(second) == copy);
    CHECK(floats_are(memory, copy, {0, 0, 0}));
    position.assign(machine, second, block);
    CHECK(floats_are(memory, copy, {1, 2, 3}));
    position.assign(machine, first, block);  // its own block: nothing
    CHECK(memory.u32(first) == block);
    position.destroy(machine, second);
    CHECK(memory.u32(second) == 0);
    CHECK(memory.live_blocks() == blocks + 1);
    position.assign(machine, second, block);  // no block yet: a new one
    CHECK(memory.u32(second) != 0);
    CHECK(floats_are(memory, memory.u32(second), {1, 2, 3}));
}

void test_color_and_grid_types() {
    MachineRun run(machine_class({op(0x5C)}));
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress slot = memory.allocate(4);
    machine.value_type("color").read_default(machine, slot, float_chunk({0.25f, 0.5f, 0.75f, 1.0f}));
    CHECK(memory.block_size(memory.u32(slot)) == 16);
    CHECK(floats_are(memory, memory.u32(slot), {0.25f, 0.5f, 0.75f, 1.0f}));
    const ScriptAddress grid = memory.allocate(4);
    machine.value_type("gridposition").read_default(machine, grid, float_chunk({5.0f, 6.0f}));
    CHECK(memory.block_size(memory.u32(grid)) == 8);
    CHECK(memory.u32(memory.u32(grid)) == 0);  // it reads nothing
    CHECK(memory.u32(memory.u32(grid) + 4) == 0);
    machine.value_type("rotation").read_default(machine, slot, float_chunk({1.0f, 2.0f, 3.0f}));
    CHECK(throws([&] { machine.value_type("color").read_default(machine, slot, float_chunk({1.0f})); }));
}

// 0x21: the target slot takes the value of the source slot's block.
void test_store_value() {
    MachineRun run(machine_class({op(0x07, 4), op(0x07, 8), op(0x21), 0,       // locals + 4 = locals + 8
                                  op(0x03, 0), op(0x07, 8), op(0x21), 0,       // no target: nothing
                                  op(0x5D), op(0x5C)},
                                 {make_reference(2, 3, "position"), make_reference(2, 7, "position")}),
                   {"position"});
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    ScriptValueType& position = machine.value_type("position");
    const ScriptAddress locals = run.frame().locals;
    position.init(machine, locals + 4, 0);
    position.init(machine, locals + 8, 0);
    set_floats(memory, memory.u32(locals + 8), {4, 5, 6});
    const ScriptAddress target = memory.u32(locals + 4);
    CHECK(machine.run_thread(run.thread));
    CHECK(memory.u32(locals + 4) == target);
    CHECK(floats_are(memory, target, {4, 5, 6}));
    CHECK(run.frame().top == -1);
}

// A parameter of kind 9 gets a block of its own with the argument's value; a
// local starts with an empty block; both go when the frame ends.
void test_value_param() {
    ScriptClass cls = machine_class({op(0x07, 4), op(0x48), 0, op(0x5D), op(0x5C)},
                                    {make_reference(4, 2, "alpha", 1)});
    ScriptVariable param = variable(9, 4);
    param.type.name = "position";
    ScriptVariable local = variable(9, 8);
    local.type.name = "position";
    cls.methods.push_back(method_with("place", bo2::engine::kScriptNoSlot, {param}, 0));
    ScriptBody place = script_body(1, 0, {op(0x5D), op(0x5C)}, 12);
    place.locals = {local};
    cls.bodies.push_back(place);
    MachineRun run(cls, {"position"});
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress caller = run.frame().locals;
    machine.value_type("position").init(machine, caller + 4, 0);
    set_floats(memory, memory.u32(caller + 4), {7, 8, 9});
    const std::size_t blocks = memory.live_blocks();
    CHECK(machine.run_thread(run.thread));
    CHECK(run.thread.frames.size() == 2);
    const ScriptAddress callee = run.frame().locals;
    CHECK(memory.u32(callee + 4) != memory.u32(caller + 4));
    CHECK(floats_are(memory, memory.u32(callee + 4), {7, 8, 9}));
    CHECK(floats_are(memory, memory.u32(callee + 8), {0, 0, 0}));
    CHECK(machine.run_thread(run.thread));
    CHECK(run.thread.frames.size() == 1);
    CHECK(memory.live_blocks() == blocks);
}

// A field of kind 9 reads its default from the chunk of the package; without
// a default it gets the empty value.
void test_value_field_default() {
    ScriptClass cls = make_class("holder", "", {"default"}, 1);
    cls.data_size = 12;
    ScriptField placed = field(9, 4, 0);
    placed.type.name = "position";
    placed.default_object = float_chunk({1.0f, 2.0f, 3.0f});
    ScriptField empty = field(9, 8, 0);
    empty.type.name = "position";
    empty.default_flag = 0;
    cls.fields = {placed, empty};
    const Linked linked = make_program({cls}, {"position"});
    ScriptMachine machine(linked.program);
    const ScriptObjectId object = machine.create_object(0);
    const ScriptAddress data = machine.data_of(object);
    CHECK(floats_are(machine.memory(), machine.memory().u32(data + 4), {1, 2, 3}));
    CHECK(floats_are(machine.memory(), machine.memory().u32(data + 8), {0, 0, 0}));
}

void test_unregistered_value_type() {
    ScriptClass cls = make_class("holder", "", {"default"}, 1);
    cls.data_size = 8;
    ScriptField lamp = field(9, 4, 0);
    lamp.type.name = "lamp";  // a value class with no type
    lamp.default_flag = 0;
    cls.fields = {lamp};
    const Linked linked = make_program({cls}, {"lamp"});
    ScriptMachine machine(linked.program);
    CHECK(throws([&] { machine.create_object(0); }));
    CHECK(throws([&] { static_cast<void>(machine.value_type("lamp")); }));
    machine.register_value_type("lamp", std::make_unique<bo2::engine::ScriptBlockType>(4, 1));
    const ScriptObjectId object = machine.create_object(0);
    CHECK(machine.memory().u32(machine.data_of(object) + 4) != 0);
}

// waypoints (FUN_0079e650 to FUN_0079e700): the default list is made once and
// every object shares it; a field without a default holds 0.
void test_waypoints_type() {
    ScriptClass cls = make_class("path", "", {"default"}, 1);
    cls.data_size = 12;
    ScriptField route = field(9, 4, 0);
    route.type.name = "waypoints";
    route.default_object = {2, 0, 0, 0};
    for (float value : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}) {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
        route.default_object.insert(route.default_object.end(), bytes, bytes + 4);
    }
    ScriptField none = field(9, 8, 0);
    none.type.name = "waypoints";
    none.default_flag = 0;
    cls.fields = {route, none};
    const Linked linked = make_program({cls}, {"waypoints"});
    ScriptMachine machine(linked.program);
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptObjectId first = machine.create_object(0);
    const std::size_t blocks = memory.live_blocks();
    const ScriptObjectId second = machine.create_object(0);
    const ScriptAddress list = memory.u32(machine.data_of(first) + 4);
    CHECK(list != 0);
    CHECK(memory.u32(machine.data_of(second) + 4) == list);  // shared
    CHECK(memory.live_blocks() == blocks + 2);                // the second object's data and handle only
    CHECK(memory.u32(list) == 2);
    CHECK(memory.u32(list + 4) == list + 8);
    CHECK(floats_are(memory, list + 8, {1, 2, 3, 4, 5, 6}));
    CHECK(memory.u32(machine.data_of(first) + 8) == 0);
    ScriptValueType& type = machine.value_type("waypoints");
    const ScriptAddress slot = memory.allocate(4);
    type.assign(machine, slot, list);
    CHECK(memory.u32(slot) == list);
    type.destroy(machine, slot);
    CHECK(memory.u32(slot) == list);  // the original leaves the slot alone
    CHECK(throws([&] { type.read_default(machine, slot, std::vector<std::uint8_t>{3, 0, 0, 0, 1, 2}); }));
}

// creature (CreatureNativeType) copies a word; creaturecontroller
// (NTcreaturecontroller) shares a counted object and frees it at 0.
void test_creature_value_types() {
    MachineRun run(machine_class({op(0x5C)}));
    ScriptMachine& machine = *run.machine;
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress slot = memory.allocate(4);
    ScriptValueType& creature = machine.value_type("creature");
    creature.init(machine, slot, 0x1234);
    CHECK(memory.u32(slot) == 0x1234);
    creature.assign(machine, slot, 0x55);
    CHECK(memory.u32(slot) == 0x55);
    creature.destroy(machine, slot);
    CHECK(memory.u32(slot) == 0);
    creature.read_default(machine, slot, {});
    CHECK(memory.u32(slot) == 0);

    ScriptValueType& controller = machine.value_type("creaturecontroller");
    const ScriptAddress object = memory.allocate(8);
    memory.set_u32(object + 4, 1);  // its owner's count
    const std::size_t blocks = memory.live_blocks();
    controller.init(machine, slot, object);
    CHECK(memory.u32(slot) == 0);  // init ignores the source
    controller.assign(machine, slot, object);
    CHECK(memory.u32(slot) == object);
    CHECK(memory.u32(object + 4) == 2);
    controller.assign(machine, slot, object);  // the same object: nothing
    CHECK(memory.u32(object + 4) == 2);
    controller.destroy(machine, slot);
    CHECK(memory.u32(slot) == 0);
    CHECK(memory.u32(object + 4) == 1);
    controller.assign(machine, slot, object);
    memory.set_u32(object + 4, 1);  // its owner let it go
    controller.assign(machine, slot, 0);
    CHECK(memory.u32(slot) == 0);
    CHECK(memory.live_blocks() == blocks - 1);  // freed at count 0
}

// Activating the object of a creature (FUN_0070e0c0): each field of kind 10
// takes the object of the creature named like the field's type class, then
// the object starts.
void test_activate_links_fields() {
    ScriptClass base = make_class("basecreaturescript", "", {"default"}, 1);
    base.data_size = 12;
    ScriptField guard = field(10, 8, 0);
    guard.type.name = "guard01";
    guard.default_flag = 0;
    base.fields = {guard};
    ScriptClass lever = machine_class({op(0x0B, 12), op(0x03, 7), op(0x1D), op(0x5C)});
    lever.name = "lever01";
    lever.parent = "basecreaturescript";
    lever.slots = {{0, 0, "lever01", 0}};
    lever.data_size = 20;
    ScriptField door = field(10, 4, 0);
    door.type.name = "door01";
    door.default_flag = 0;
    ScriptField camera = field(10, 16, 0);
    camera.type.name = "exportedcamera";
    camera.default_flag = 0;
    lever.fields = {door, camera};
    ScriptClass door_class = make_class("door01", "basecreaturescript", {"default"}, 1);
    door_class.data_size = 12;
    ScriptClass guard_class = make_class("guard01", "basecreaturescript", {"default"}, 1);
    guard_class.data_size = 12;
    const Linked linked = make_program({lever, base, door_class, guard_class});
    ScriptMachine machine(linked.program);
    const ScriptObjectId lever_object = machine.create_object(0);
    const ScriptObjectId door_object = machine.create_object(2);
    const ScriptObjectId guard_object = machine.create_object(3);
    const std::map<std::string, ScriptObjectId> creatures = {{"door01", door_object}, {"guard01", guard_object}};
    std::vector<std::string> asked;
    machine.activate(lever_object, [&](const std::string& name) {
        asked.push_back(name);
        const auto found = creatures.find(name);
        return found == creatures.end() ? ScriptObjectId{0} : found->second;
    });
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptAddress data = machine.data_of(lever_object);
    CHECK(asked == (std::vector<std::string>{"door01", "exportedcamera", "guard01"}));  // the class, then its parent
    CHECK(memory.u32(data + 4) == machine.handle_of(door_object));
    CHECK(memory.u32(data + 8) == machine.handle_of(guard_object));
    CHECK(memory.u32(data + 16) == 0);  // no creature has that name
    CHECK(memory.u32(machine.handle_of(door_object) + 4) == 2);
    CHECK(memory.u32(data + 12) == 7);  // then the object started
    machine.link_object_fields(lever_object, [&](const std::string&) { return guard_object; });
    CHECK(memory.u32(data + 4) == machine.handle_of(guard_object));  // relinked
    CHECK(memory.u32(machine.handle_of(door_object) + 4) == 1);      // the old one released
}

// The creature of an object (+0x2c) and a handle stored in an object slot
// (FUN_004cd760): counted first, the old one released when it differs.
void test_creature_and_store_handle() {
    ScriptClass base = make_class("basecreaturescript", "", {"default"}, 1);
    ScriptClass creature = make_class("creaturescript", "basecreaturescript", {"default"}, 1);
    ScriptClass guard = make_class("guard01", "creaturescript", {"default"}, 1);
    base.data_size = creature.data_size = guard.data_size = 4;
    const Linked linked = make_program({base, creature, guard});
    ScriptMachine machine(linked.program);
    const ScriptObjectId guard_object = machine.create_object(2);
    const ScriptObjectId base_object = machine.create_object(0);
    CHECK(machine.creature_of(guard_object) == 0);
    machine.set_creature(guard_object, 0x100);
    CHECK(machine.creature_of(guard_object) == 0x100);
    CHECK(machine.creature_of(base_object) == 0);
    CHECK(machine.creature_host() == nullptr);
    CHECK(machine.is_a(2, "creaturescript"));
    CHECK(!machine.is_a(0, "creaturescript"));

    bo2::engine::ScriptMemory& memory = machine.memory();
    const auto count = [&](ScriptObjectId object) { return memory.u32(machine.handle_of(object) + 4); };
    const ScriptAddress slot = memory.allocate(4);
    machine.store_handle(slot, machine.handle_of(guard_object));
    CHECK(memory.u32(slot) == machine.handle_of(guard_object));
    CHECK(count(guard_object) == 2);
    machine.store_handle(slot, machine.handle_of(guard_object));  // the same handle: nothing
    CHECK(count(guard_object) == 2);
    machine.store_handle(slot, machine.handle_of(base_object));
    CHECK(count(guard_object) == 1);
    CHECK(count(base_object) == 2);
    machine.store_handle(slot, 0);
    CHECK(memory.u32(slot) == 0);
    CHECK(count(base_object) == 1);
    machine.store_handle(slot, 0);
    CHECK(memory.u32(slot) == 0);
}

// The creatures of a level as the creature natives see them: a word per
// creature, its name and its object.
struct FakeCreatures final : bo2::engine::ScriptCreatureHost {
    std::map<std::string, std::uint32_t> by_name;
    std::map<std::uint32_t, ScriptObjectId> objects;
    std::vector<std::pair<std::uint32_t, std::string>> asked;
    std::uint32_t kain_creature = 0;
    ScriptObjectId hud = 0;

    std::uint32_t find_creature(std::uint32_t owner, const std::string& name) override {
        asked.emplace_back(owner, name);
        const auto found = by_name.find(name);
        return found == by_name.end() ? 0 : found->second;
    }
    std::string creature_name(std::uint32_t creature) override {
        for (const auto& [name, word] : by_name) {
            if (word == creature) {
                return name;
            }
        }
        return {};
    }
    ScriptObjectId creature_object(std::uint32_t creature) override {
        const auto found = objects.find(creature);
        return found == objects.end() ? 0 : found->second;
    }
    std::uint32_t kain() override { return kain_creature; }
    ScriptObjectId hud_object() override { return hud; }

    std::array<float, 3> creature_position(std::uint32_t) override { return {}; }
    std::array<float, 3> creature_rotation(std::uint32_t) override { return {}; }
    bool creature_has_package(std::uint32_t) override { return true; }
    void hibernate_creature(std::uint32_t) override {}
    void unhibernate_creature(std::uint32_t) override {}
    void remove_creature(std::uint32_t) override {}
    std::uint32_t creature_flags(std::uint32_t) override { return 0; }
    void set_creature_flags(std::uint32_t, std::uint32_t) override {}
    std::uint32_t drawn_frame(std::uint32_t) override { return 0; }
    std::uint32_t frame() override { return 0; }
    void leave_group(std::uint32_t, Group) override {}
    void set_model_lod(bool) override {}
};

// A class with `count` native bodies, one method each.
ScriptClass native_class(const std::string& name, const std::string& parent, std::uint16_t count) {
    ScriptClass cls = make_class(name, parent, {"default"}, 1);
    cls.data_size = 4;
    for (std::uint16_t index = 0; index < count; ++index) {
        cls.methods.push_back(make_method("m" + std::to_string(index), bo2::engine::kScriptNoSlot));
        cls.bodies.push_back(native_body(index));
    }
    return cls;
}

// The natives that find creatures (BO2NX-177; docs/SCRIPTS.md).
void test_creature_natives() {
    const Linked linked = make_program({native_class("basecreaturescript", "", 0),
                                        native_class("creaturescript", "basecreaturescript", 8),
                                        native_class("bo2creaturescript", "creaturescript", 12),
                                        native_class("guard01", "bo2creaturescript", 0),
                                        native_class("lever01", "basecreaturescript", 0)});
    ScriptMachine machine(linked.program);
    bo2::engine::bind_creature_natives(machine);
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptObjectId guard = machine.create_object(linked.program.find_class("guard01") - 1);
    const ScriptObjectId lever = machine.create_object(linked.program.find_class("lever01") - 1);
    machine.set_creature(guard, 0x100);
    machine.set_creature(lever, 0x200);
    const ScriptAddress arg = memory.allocate(4);
    const ScriptAddress result = memory.allocate(4);
    const auto call = [&](bo2::engine::ScriptNative native, const char* name) {
        memory.set_u32(arg, name == nullptr ? 0 : machine.new_string_cell(name));
        native(machine, guard, name == nullptr ? 0 : 1, arg, result);
        return memory.u32(result);
    };

    CHECK(call(bo2::engine::native_creature_self, nullptr) == 0x100);
    CHECK(throws([&] { call(bo2::engine::native_get_kain, nullptr); }));  // no host yet
    FakeCreatures creatures;
    creatures.by_name = {{"guard01", 0x100}, {"lever01", 0x200}, {"ghost", 0x300}};
    creatures.objects = {{0x100, guard}, {0x200, lever}};
    creatures.kain_creature = 0x400;
    machine.set_creature_host(&creatures);
    CHECK(call(bo2::engine::native_get_kain, nullptr) == 0x400);
    CHECK(!throws([&] { bo2::engine::native_get_kain(machine, guard, 0, 0, 0); }));  // no result

    // creaturebyname: the creature word; an empty or missing name asks no one.
    CHECK(call(bo2::engine::native_creature_by_name, "lever01") == 0x200);
    CHECK(creatures.asked.back() == std::make_pair(std::uint32_t{0x100}, std::string("lever01")));
    const std::size_t warned = machine.warnings().size();
    CHECK(call(bo2::engine::native_creature_by_name, "nobody") == 0);
    CHECK(machine.warnings().size() == warned + 1);
    CHECK(machine.warnings().back() == "guard01 : GetCreatureWithName('nobody') failed");
    const std::size_t asked = creatures.asked.size();
    memory.set_u32(result, 7);
    CHECK(call(bo2::engine::native_creature_by_name, nullptr) == 0);
    memory.set_u32(result, 7);
    CHECK(call(bo2::engine::native_creature_by_name, "") == 0);
    CHECK(creatures.asked.size() == asked);
    CHECK(machine.warnings().size() == warned + 1);

    // creaturescriptbyname: the handle of a creaturescript object, counted.
    const auto count = [&](ScriptObjectId object) { return memory.u32(machine.handle_of(object) + 4); };
    memory.set_u32(result, 0);
    CHECK(call(bo2::engine::native_creature_script_by_name, "guard01") == machine.handle_of(guard));
    CHECK(count(guard) == 2);
    CHECK(call(bo2::engine::native_creature_script_by_name, "guard01") == machine.handle_of(guard));
    CHECK(count(guard) == 2);  // the same handle: nothing
    CHECK(call(bo2::engine::native_creature_script_by_name, "lever01") == 0);  // not a creaturescript
    CHECK(count(guard) == 1);
    CHECK(count(lever) == 1);
    CHECK(call(bo2::engine::native_creature_script_by_name, "ghost") == 0);  // no object
    CHECK(call(bo2::engine::native_creature_script_by_name, "nobody") == 0);
    CHECK(machine.warnings().size() == warned + 1);  // this one does not log

    // gethud: the HUD's object, whatever its class.
    creatures.hud = lever;
    CHECK(call(bo2::engine::native_get_hud, nullptr) == machine.handle_of(lever));
    CHECK(count(lever) == 2);
    creatures.hud = 0;
    CHECK(call(bo2::engine::native_get_hud, nullptr) == 0);
    CHECK(count(lever) == 1);
}

// Timers (docs/SCRIPTS.md, "Timers"): the value type, the natives, the task
// the script manager runs once per frame, the call of slot 15 when the time
// runs out, and play as a task of the frame.
void test_timers() {
    ScriptClass timer_class = native_class("timer", "", 6);
    ScriptClass watcher = make_class("watcher", "", {"default"}, 16);
    watcher.data_size = 8;
    ScriptVariable timer_param = variable(9, 4);
    timer_param.type.name = "timer";
    watcher.methods = {method_with("ontimer", 15, {timer_param}, 0)};
    watcher.bodies = {script_body(0, 0, count_into(4), 8)};
    watcher.slots = {{0, 15, "watcher", 0}};
    ScriptClass mute = make_class("mute", "", {"default"}, 1);
    mute.data_size = 4;
    const Linked linked = make_program({timer_class, watcher, mute}, {"timer"});
    ScriptMachine machine(linked.program);
    bo2::engine::bind_timer_natives(machine);
    bo2::engine::ScriptMemory& memory = machine.memory();
    const ScriptObjectId object = machine.create_object(1);
    const ScriptObjectId silent = machine.create_object(2);
    const std::size_t blocks = memory.live_blocks();
    ScriptValueType& type = machine.value_type("timer");
    const ScriptAddress slot = memory.allocate(4);
    type.init(machine, slot, 0x1234);  // the source does not count
    const ScriptAddress timer = memory.u32(slot);
    CHECK(timer != 0 && timer != 0x1234);
    CHECK(memory.u32(timer + 4) == 1);
    const ScriptAddress args = memory.allocate(8);
    const ScriptAddress result = memory.allocate(4);
    const auto native = [&](bo2::engine::ScriptNative call) {
        memory.set_u32(result, 0xFFFFFF00U);
        call(machine, slot, 2, args, result);
        return memory.u32(result);
    };
    const auto start_args = [&](float time, bool repeat) {
        memory.set_u32(args, bits(time));
        memory.set_u32(args + 4, repeat ? 1 : 0);
    };
    const auto calls = [&] { return memory.u32(machine.data_of(object) + 4); };
    const auto step = [&](float time) { machine.update_tasks(bits(time)); };

    CHECK(native(bo2::engine::native_timer_is_running) == 0xFFFFFF00U);  // the low byte only
    CHECK(native(bo2::engine::native_timer_get_id) == timer);
    memory.set_u32(args, object);
    native(bo2::engine::native_timer_set_callback);
    CHECK(memory.u32(timer + 0x10) == machine.handle_of(object));
    CHECK(memory.u32(machine.handle_of(object) + 4) == 2);

    // starttimer: the script manager runs the task; at 0 the callback.
    start_args(1.0f, false);
    native(bo2::engine::native_timer_start);
    CHECK(machine.update_task_count() == 1);
    CHECK(native(bo2::engine::native_timer_is_running) == 0xFFFFFF01U);
    step(0.4f);
    step(0.4f);
    CHECK(calls() == 0);
    step(0.4f);
    CHECK(calls() == 1);
    CHECK(machine.update_task_count() == 0);  // it stopped, so it went
    CHECK(memory.u32(timer + 0xc) == 0);

    // A repeating timer starts again with its time until stopped.
    start_args(0.5f, true);
    native(bo2::engine::native_timer_start);
    step(0.6f);
    CHECK(calls() == 2);
    CHECK(memory.u32(timer + 8) == bits(0.5f));
    step(0.2f);
    CHECK(memory.u32(timer + 8) == bits(0.3f));
    native(bo2::engine::native_timer_stop);
    CHECK(memory.u32(timer + 8) == 0);
    step(0.2f);
    CHECK(machine.update_task_count() == 0);
    CHECK(calls() == 2);

    // play: a task the caller runs; it is not in the script manager.
    start_args(0.3f, false);
    std::unique_ptr<bo2::engine::ScriptTask> task = bo2::engine::native_timer_play(machine, slot, 0, 2, args);
    CHECK(task != nullptr && task->running());
    CHECK(machine.update_task_count() == 0);
    task->run(machine, bits(0.2f));
    CHECK(task->running());
    task->run(machine, bits(0.2f));
    CHECK(calls() == 3);
    CHECK(!task->running());
    task->finish(machine);
    CHECK(memory.u32(timer + 0xc) == 0);

    // A timer that has a task: play is not ported.
    start_args(1.0f, false);
    native(bo2::engine::native_timer_start);
    CHECK(throws([&] { static_cast<void>(bo2::engine::native_timer_play(machine, slot, 0, 2, args)); }));

    // An object with no body in slot 15 gets no call.
    memory.set_u32(args, silent);
    native(bo2::engine::native_timer_set_callback);
    CHECK(memory.u32(machine.handle_of(object) + 4) == 1);  // the old one released
    step(2.0f);
    CHECK(calls() == 3);

    // Shared, then destroyed: the last release frees the timer, its task
    // parts, and the callback goes.
    const ScriptAddress copy = memory.allocate(4);
    memory.set_u32(copy, 0);
    type.assign(machine, copy, timer);
    CHECK(memory.u32(timer + 4) == 2);
    type.destroy(machine, copy);
    CHECK(memory.u32(copy) == timer);  // destroy keeps the slot
    start_args(1.0f, false);
    native(bo2::engine::native_timer_start);
    CHECK(machine.update_task_count() == 1);
    type.destroy(machine, slot);
    CHECK(memory.u32(machine.handle_of(silent) + 4) == 1);
    step(0.1f);
    CHECK(machine.update_task_count() == 0);
    memory.release(copy);
    memory.release(slot);
    memory.release(args);
    memory.release(result);
    CHECK(memory.live_blocks() == blocks);
}

// 0x59: the copy runs, the frame skips the block and waits for it.
void test_fork_waits() {
    MachineRun run(machine_class(join({{op(0x59, 2), op(0x5D), op(0x5C)}, mark(), {op(0x5D), op(0x5C)}})));
    ScriptMachine& machine = *run.machine;
    const ScriptAddress data = machine.data_of(1);
    CHECK(machine.run_thread(run.thread));
    CHECK(machine.memory().u32(data + 4) == 0);
    CHECK(run.frame().ip == 3);
    CHECK(machine.run_thread(run.thread));
    CHECK(machine.memory().u32(data + 4) == 7);
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

// Every class of the region package of sl01 gets an object with the initial
// values of its fields: the value types of the level scripts are all there
// (the engine objects start at 0, as a name the engine does not find).
void test_create_sl01_objects(const std::filesystem::path& root) {
    const bo2::engine::BigArchive archive =
        load_container(root / "media/gogames/bo2/resources/levels/slums/sl01.big");
    const RegionPrograms container = read_container(archive);
    if (container.regions.size() != 1) {
        CHECK(container.regions.size() == 1);
        return;
    }
    const ScriptPackage& region = *container.regions[0].second;
    const ScriptProgram program = link_region(container, region, kNativeTypes);
    ScriptMachine machine(program);
    std::size_t created = 0;
    std::size_t creatures = 0;
    std::size_t creature_scripts = 0;
    std::vector<std::string> refused;
    for (std::uint32_t id = 0; id < program.classes().size(); ++id) {
        const auto& cls = program.classes()[id];
        if (cls.source < region.classes.data() || cls.source >= region.classes.data() + region.classes.size()) {
            continue;  // not a class of the region package
        }
        try {
            machine.create_object(id);
            ++created;
        } catch (const bo2::engine::ScriptError& error) {
            refused.push_back(cls.name + ": " + error.what());
        }
        for (std::uint32_t up = id + 1; up != 0; up = program.classes()[up - 1].parent) {
            if (program.classes()[up - 1].name == "basecreaturescript") {
                ++creatures;
                break;
            }
        }
        creature_scripts += machine.is_a(id, "creaturescript") ? 1 : 0;
    }
    for (const std::string& failure : refused) {
        std::cerr << "sl01 object: " << failure << '\n';
    }
    CHECK(refused.empty());
    CHECK(created == 79);
    CHECK(creatures == 78);  // every class but resourcedb derives from basecreaturescript
    CHECK(creature_scripts == 61);  // not the monsters (monsterscript) nor the conversations

    // The bodies the creature natives bind to (BO2NX-177).
    bo2::engine::bind_creature_natives(machine);
    const auto method_of = [&](const std::string& name, std::uint16_t index) -> const ScriptMethod& {
        const auto& cls = program.classes()[program.find_class(name) - 1];
        const auto& body = program.bodies()[cls.bodies[index]];
        return program.classes()[body.owner].source->methods[body.source->method];
    };
    CHECK(method_of("creaturescript", 6).name == "creaturebyname");
    CHECK(method_of("creaturescript", 6).result.kind == 9);
    CHECK(method_of("creaturescript", 7).name == "creatureself");
    CHECK(method_of("bo2creaturescript", 0).name == "getkain");
    CHECK(method_of("bo2creaturescript", 0).result.kind == 9);
    CHECK(method_of("bo2creaturescript", 1).name == "creaturescriptbyname");
    CHECK(method_of("bo2creaturescript", 1).result.name == "creaturescript");
    CHECK(method_of("bo2creaturescript", 11).name == "gethud");
    CHECK(method_of("bo2creaturescript", 11).result.name == "hud");
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
    test_push();
    test_locals();
    test_self_fields();
    test_object_fields();
    test_handles();
    test_store();
    test_store_null_object();
    test_strings();
    test_unported_code();
    test_int_ops();
    test_int_compare();
    test_float_ops();
    test_float_compare();
    test_region_codes();
    test_string_compare();
    test_logic();
    test_conversions();
    test_jumps();
    test_loop_guard();
    test_yield();
    test_cast();
    test_native_call();
    test_script_call();
    test_virtual_call();
    test_table_call();
    test_call_on_null();
    test_string_param();
    test_frame_end();
    test_array_variables();
    test_spawn_waits();
    test_spawn_no_wait();
    test_task_native();
    test_start_without_waiting();
    test_parallel_block();
    test_fork_waits();
    test_field_defaults();
    test_start_runs_state_start();
    test_request_state();
    test_native_state_ends_frame();
    test_request_invalid_state();
    test_stop();
    test_position_type();
    test_color_and_grid_types();
    test_store_value();
    test_value_param();
    test_value_field_default();
    test_unregistered_value_type();
    test_waypoints_type();
    test_creature_value_types();
    test_activate_links_fields();
    test_creature_and_store_handle();
    test_creature_natives();
    test_timers();

    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "script machine asset tests skipped (BO2NX_ASSETS_DIR is unset)\n";
    } else {
        try {
            test_link_sl01(root);
            test_create_sl01_objects(root);
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
