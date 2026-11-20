#include "bo2/engine/script_machine.hpp"
#include "bo2/engine/script_timer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bo2::engine {

namespace {

std::string hex_code(std::uint32_t code) {
    std::array<char, 8> text{};
    std::snprintf(text.data(), text.size(), "0x%02x", code);
    return text.data();
}

// The codes that no body of the game data uses, in the bpackage entries or
// in the packages of the regions (in italics in docs/SCRIPTS.md,
// "Instructions").
bool unused_by_the_game(std::uint32_t code) {
    switch (code) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x20:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x2A:
    case 0x2B:
    case 0x2C:
    case 0x3D:
    case 0x3F:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
        return true;
    default:
        return code > 0x60;
    }
}

float as_float(std::uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

std::int32_t as_int(std::uint32_t bits) {
    return static_cast<std::int32_t>(bits);
}

// __ftol (0x7a3af4): rounding set to chop, fistp to a 64-bit integer, the low
// 32 bits. A value the 64-bit integer cannot hold (or NaN) stores the integer
// indefinite 0x8000000000000000, whose low 32 bits are 0.
std::uint32_t ftol(float value) {
    const double wide = static_cast<double>(value);
    if (std::isnan(wide) || wide >= 9223372036854775808.0 || wide < -9223372036854775808.0) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(wide)));
}

constexpr std::uint32_t kLargestFloat = 0x7F7FFFFFU;  // 0x0086dee8
constexpr std::uint32_t kLoopLimit = 10000;           // FUN_0057de80, instruction 0x60

// One frame's view of the stack: 4-byte cells from `stack`, `top` the index
// of the last one.
class Stack {
public:
    Stack(ScriptMemory& memory, ScriptFrame& frame) : memory_(memory), frame_(frame) {}

    void push(std::uint32_t value) {
        ++frame_.top;
        memory_.set_u32(cell(), value);
    }

    std::uint32_t pop() {
        const std::uint32_t value = memory_.u32(cell());
        --frame_.top;
        return value;
    }

    std::uint32_t top() const { return memory_.u32(cell()); }
    void replace(std::uint32_t value) { memory_.set_u32(cell(), value); }
    // A boolean is the low byte of a cell; the other three bytes stay.
    std::uint8_t top_byte() const { return memory_.u8(cell()); }
    void replace_byte(bool value) { memory_.set_u8(cell(), value ? 1 : 0); }
    // The cell under the top.
    void replace_under(std::uint32_t value) {
        if (frame_.top < 1) {
            throw ScriptError("script stack has no cell under its top");
        }
        memory_.set_u32(frame_.stack + static_cast<std::uint32_t>(frame_.top - 1) * 4U, value);
    }

private:
    ScriptAddress cell() const {
        if (frame_.top < 0) {
            throw ScriptError("script stack is empty");
        }
        return frame_.stack + static_cast<std::uint32_t>(frame_.top) * 4U;
    }

    ScriptMemory& memory_;
    ScriptFrame& frame_;
};

}  // namespace

ScriptMachine::ScriptMachine(const ScriptProgram& program)
    : program_(program),
      natives_(program.bodies().size(), nullptr),
      task_natives_(program.bodies().size(), nullptr) {
    empty_record_ = new_record("");
    // NTposition (0x878d68), NTrotation (0x878d3c), NTcolor (0x878d10) and
    // NTgridposition (0x878bb0); docs/SCRIPTS.md, "Values".
    register_value_type("position", std::make_unique<ScriptBlockType>(12, 3));
    register_value_type("rotation", std::make_unique<ScriptBlockType>(12, 3));
    register_value_type("color", std::make_unique<ScriptBlockType>(16, 4));
    register_value_type("gridposition", std::make_unique<ScriptBlockType>(8, 0));
    register_value_type("waypoints", std::make_unique<ScriptWaypointsType>());
    // CreatureNativeType (0x878b58) and NTcreaturecontroller (0x878c60).
    register_value_type("creature", std::make_unique<ScriptWordType>());
    // TerrainNativeType (0x878b2c): the same as creature (FUN_0076b1c0,
    // FUN_0076b1d0, FUN_0076b1e0; no natives, FUN_0076b170).
    register_value_type("terrain", std::make_unique<ScriptWordType>());
    register_value_type("creaturecontroller", std::make_unique<ScriptCountedType>());
    // NTsound (0x878c08), NTeffect (0x878ce4), NTsoundgroup (0x878bdc).
    register_value_type("sound", std::make_unique<ScriptEngineObjectType>(ScriptEngineObjectType::Kind::sound));
    register_value_type("effect", std::make_unique<ScriptEngineObjectType>(ScriptEngineObjectType::Kind::effect));
    register_value_type("soundgroup",
                        std::make_unique<ScriptEngineObjectType>(ScriptEngineObjectType::Kind::soundgroup));
    // NTtimer (0x878c34).
    register_value_type("timer", std::make_unique<ScriptTimerType>());
    const std::vector<std::string>& names = program_.names();
    name_table_ = memory_.allocate(static_cast<std::uint32_t>(names.size() * 4));
    for (std::size_t index = 0; index < names.size(); ++index) {
        memory_.set_u32(name_table_ + static_cast<std::uint32_t>(index * 4), new_record(names[index]));
    }
}

ScriptObjectId ScriptMachine::create_object(std::uint32_t class_id) {
    if (class_id >= program_.classes().size()) {
        throw ScriptError("script class id is out of range");
    }
    Object object;
    object.class_id = class_id;
    object.data = memory_.allocate(program_.classes()[class_id].source->data_size);
    object.handle = memory_.allocate(12);
    const auto id = static_cast<ScriptObjectId>(objects_.size() + 1);
    memory_.set_u32(object.handle + 4, 1);  // FUN_0057f830: the object holds its handle
    memory_.set_u32(object.handle + 8, id);
    objects_.push_back(std::move(object));
    init_fields(id);
    return id;
}

const ScriptMachine::Object& ScriptMachine::object_at(ScriptObjectId object) const {
    if (object == 0 || object > objects_.size()) {
        throw ScriptError("script object does not exist");
    }
    return objects_[object - 1];
}

ScriptMachine::Object& ScriptMachine::object_at(ScriptObjectId object) {
    if (object == 0 || object > objects_.size()) {
        throw ScriptError("script object does not exist");
    }
    return objects_[object - 1];
}

// The record of a name a field defaults to; the machine holds it once, as the
// original's global name table does.
ScriptAddress ScriptMachine::name_record(const std::string& name) {
    const auto found = default_names_.find(name);
    if (found != default_names_.end()) {
        return found->second;
    }
    const ScriptAddress record = new_record(name);
    default_names_.emplace(name, record);
    return record;
}

// FUN_0057fa20: the fields of the class and then of each ancestor take their
// initial value at their offset; an offset already written by a class nearer
// the object is skipped, so a field a class declares again keeps that class's
// default (docs/SCRIPTS.md, "Objects, classes and states"). State 0.
void ScriptMachine::init_fields(ScriptObjectId object) {
    const ScriptAddress data = data_of(object);
    memory_.set_u8(data, 0);
    std::vector<std::uint32_t> written;
    for (std::uint32_t id = class_of(object) + 1; id != 0; id = program_.classes()[id - 1].parent) {
        for (const ScriptField& field : program_.classes()[id - 1].source->fields) {
            if (std::find(written.begin(), written.end(), field.offset) != written.end()) {
                continue;
            }
            written.push_back(field.offset);
            const ScriptAddress address = data + field.offset;
            switch (field.type.kind) {
            case 1:
            case 2:
            case 3:
            case 5:
            case 11:
                store_value(address, field.type, field.default_value);
                break;
            case 4:
                if (field.default_flag == 0) {
                    init_variable(address, field.type);
                } else {
                    memory_.set_u32(address, new_holder(name_record(field.default_name)));
                }
                break;
            case 6:
            case 7:
            case 10:
                init_variable(address, field.type);
                break;
            case 9:
                if (field.default_flag != 0) {
                    value_type(field.type.name).read_default(*this, address, field.default_object);
                } else {
                    value_type(field.type.name).init(*this, address, 0);
                }
                break;
            default:
                break;
            }
        }
    }
}

std::uint32_t ScriptMachine::state_of(ScriptObjectId object) const { return memory_.u8(data_of(object)); }

std::uint32_t ScriptMachine::pending_state_of(ScriptObjectId object) const { return object_at(object).pending; }

const ScriptTask* ScriptMachine::main_thread(ScriptObjectId object) const { return object_at(object).main.get(); }

// ScriptObject::PushThread: the original keeps 20 entries and warns past them.
void ScriptMachine::push_running(ScriptObjectId object) {
    if (running_.size() > 0x13) {
        warnings_.push_back("ScriptObject::PushThread(): Stack size exceeded!  The game will likely crash now.");
    }
    running_.push_back(object);
}

bool ScriptMachine::is_running(ScriptObjectId object) const {
    return std::find(running_.begin(), running_.end(), object) != running_.end();
}

bool ScriptMachine::owner_pending(const ScriptThread& thread) const {
    return thread.owner != 0 && object_at(thread.owner).pending != 0;
}

// FUN_00580be0: the body in slot 0 of the table of the current state; a
// native body returns the task that is the main thread (the object is both
// self and owner), a script body runs as a thread with no arguments.
void ScriptMachine::make_main_thread(ScriptObjectId object) {
    const ScriptProgramClass& cls = program_.classes()[class_of(object)];
    const std::uint32_t state = state_of(object);
    if (state >= cls.states.size() || cls.states[state].empty()) {
        throw ScriptError("script object has no slot 0 in state " + std::to_string(state));
    }
    const std::uint32_t found = cls.states[state][0];
    if (found == 0) {
        return;
    }
    const std::uint32_t body = found - 1;
    const ScriptProgramBody& target = program_.bodies().at(body);
    if (target.source->native()) {
        const ScriptTaskNative native = task_natives_[body];
        if (native == nullptr) {
            throw ScriptError("task body " + std::to_string(target.index) + " of " +
                              program_.classes()[target.owner].name + " is not bound");
        }
        object_at(object).main = native(*this, object, object, 0, 0);
        return;
    }
    auto thread = std::make_unique<ScriptThread>();
    thread->owner = object;
    ScriptFrame first = make_frame(object, body);
    for (const ScriptVariable& local : target.source->locals) {
        init_variable(first.locals + local.offset, local.type);
    }
    thread->frames.push_back(std::move(first));
    object_at(object).main = std::move(thread);
}

void ScriptMachine::run_main(ScriptObjectId object, std::uint32_t arg) {
    push_running(object);
    ScriptTask& main = *object_at(object).main;
    main.run(*this, arg);
    if (!main.running()) {
        destroy_task(object_at(object).main);
    }
    running_.pop_back();
    if (object_at(object).pending != 0) {
        change_state(object);
    }
}

void ScriptMachine::start(ScriptObjectId object) {
    if (state_of(object) != 0) {
        warnings_.push_back("WARNING: Branch occurred before ScriptObject::StartObject() called!");
        return;
    }
    make_main_thread(object);
    if (object_at(object).main != nullptr) {
        run_main(object, 0);
    }
}

void ScriptMachine::run(ScriptObjectId object, std::uint32_t arg) {
    if (object_at(object).main != nullptr) {
        run_main(object, arg);
    }
}

void ScriptMachine::stop(ScriptObjectId object) {
    if (object_at(object).main != nullptr) {
        push_running(object);
        destroy_task(object_at(object).main);
        running_.pop_back();
    }
    memory_.set_u8(data_of(object), 0);
    object_at(object).pending = 0;
}

// FUN_0070e0c0 with the walk of FUN_00589030 (class then ancestors, one field
// per offset, FUN_0057c080): a found object's handle replaces the field's,
// counted; the old handle is released.
void ScriptMachine::link_object_fields(ScriptObjectId object, const ScriptFinder& find) {
    const ScriptAddress data = data_of(object);
    std::vector<std::uint32_t> seen;
    for (std::uint32_t id = class_of(object) + 1; id != 0; id = program_.classes()[id - 1].parent) {
        for (const ScriptField& field : program_.classes()[id - 1].source->fields) {
            if (std::find(seen.begin(), seen.end(), field.offset) != seen.end()) {
                continue;
            }
            seen.push_back(field.offset);
            if (field.type.kind != 10) {
                continue;
            }
            const ScriptObjectId target = find(field.type.name);
            if (target != 0) {
                store_handle(data + field.offset, handle_of(target));
            }
        }
    }
}

void ScriptMachine::activate(ScriptObjectId object, const ScriptFinder& find) {
    link_object_fields(object, find);
    start(object);
}

std::uint32_t ScriptMachine::body_in_slot(ScriptObjectId object, std::uint32_t slot) const {
    const ScriptProgramClass& cls = program_.classes()[class_of(object)];
    const std::uint32_t state = memory_.u8(data_of(object));
    if (state >= cls.states.size() || slot >= cls.states[state].size()) {
        return 0;
    }
    return cls.states[state][slot];
}

// FUN_00580a40 with FUN_0057d810 (the thread and its first frame),
// FUN_0057dcf0 (one run with 0) and FUN_0057dbe0 (the thread ends).
void ScriptMachine::call_body(ScriptObjectId object, std::uint32_t body, std::uint32_t argc, ScriptAddress args,
                              ScriptAddress result) {
    const ScriptProgramBody& target = program_.bodies().at(body);
    if (target.source->native()) {
        const ScriptNative native = natives_[body];
        if (native == nullptr) {
            throw ScriptError("native body " + std::to_string(target.index) + " of " +
                              program_.classes()[target.owner].name + " is not bound");
        }
        native(*this, object, argc, args, result);
        return;
    }
    const ScriptMethod& method = *program_.methods().at(method_of_body(body)).source;
    ScriptThread thread;
    thread.owner = running_.empty() ? 0 : running_.back();
    ScriptFrame first = make_frame(object, body);
    if (method.result.kind != 0) {
        memory_.set_u32(first.locals, result);
    }
    for (std::uint32_t index = 0; index < argc && index < method.params.size(); ++index) {
        const ScriptVariable& param = method.params[index];
        copy_argument(first.locals + param.offset, param.type, args + index * 4U);
    }
    for (const ScriptVariable& local : target.source->locals) {
        init_variable(first.locals + local.offset, local.type);
    }
    thread.frames.push_back(std::move(first));
    run_thread(thread, 0);
    end_thread(thread);
}

void ScriptMachine::add_update_task(ScriptAddress key, std::unique_ptr<ScriptTask> task) {
    update_tasks_.emplace(key, std::move(task));
}

// FUN_00583400: the virtual +8 of each task with the time, then its +4; a
// task that stops running is destroyed (its virtual 0; FUN_0079c9d0 for the
// task of a timer) and leaves the set.
void ScriptMachine::update_tasks(std::uint32_t arg) {
    for (auto entry = update_tasks_.begin(); entry != update_tasks_.end();) {
        entry->second->run(*this, arg);
        if (entry->second->running()) {
            ++entry;
            continue;
        }
        std::unique_ptr<ScriptTask> task = std::move(entry->second);
        entry = update_tasks_.erase(entry);
        task->finish(*this);
    }
}

void ScriptMachine::set_creature(ScriptObjectId object, std::uint32_t creature) {
    object_at(object).creature = creature;
}

std::uint32_t ScriptMachine::creature_of(ScriptObjectId object) const {
    return object_at(object).creature;
}

// FUN_004cd760 (and FUN_004cdf20, FUN_0070e0c0): the new handle is counted
// before the old one is released, so the slot never holds a freed handle.
void ScriptMachine::store_handle(ScriptAddress slot, ScriptAddress handle) {
    const ScriptAddress old = memory_.u32(slot);
    if (old == handle) {
        return;
    }
    memory_.set_u32(slot, 0);
    if (old != 0) {
        release_handle(old);
    }
    memory_.set_u32(slot, handle);
    if (handle != 0) {
        memory_.set_u32(handle + 4, memory_.u32(handle + 4) + 1);
    }
}

void ScriptMachine::request_state(ScriptObjectId object, std::int32_t state) {
    const ScriptProgramClass& cls = program_.classes()[class_of(object)];
    if (state <= 0 || static_cast<std::uint32_t>(state) >= cls.states.size()) {
        return;
    }
    object_at(object).pending = static_cast<std::uint32_t>(state);
    if (!is_running(object)) {
        change_state(object);
    }
}

// FUN_00580890: the main thread is destroyed, the pending state becomes the
// state and the main thread is made on its start, which runs at the next run
// of the object (the virtual +0x18 called last, 0x580880, does nothing in a
// script object).
void ScriptMachine::change_state(ScriptObjectId object) {
    push_running(object);
    destroy_task(object_at(object).main);
    memory_.set_u8(data_of(object), static_cast<std::uint8_t>(object_at(object).pending & 0xFFU));
    object_at(object).pending = 0;
    make_main_thread(object);
    running_.pop_back();
}

ScriptAddress ScriptMachine::data_of(ScriptObjectId object) const { return object_at(object).data; }
ScriptAddress ScriptMachine::handle_of(ScriptObjectId object) const { return object_at(object).handle; }
std::uint32_t ScriptMachine::class_of(ScriptObjectId object) const { return object_at(object).class_id; }

std::uint32_t ScriptMachine::native_body(const std::string& class_name, std::uint16_t body_index) const {
    const std::uint32_t id = program_.find_class(class_name);
    if (id == 0) {
        throw ScriptError("script class " + class_name + " does not exist");
    }
    const ScriptProgramClass& cls = program_.classes()[id - 1];
    if (body_index >= cls.bodies.size()) {
        throw ScriptError("script class " + class_name + " has no body " + std::to_string(body_index));
    }
    const std::uint32_t body = cls.bodies[body_index];
    if (!program_.bodies()[body].source->native()) {
        throw ScriptError("script body " + std::to_string(body_index) + " of " + class_name + " is not native");
    }
    return body;
}

void ScriptMachine::bind_native(const std::string& class_name, std::uint16_t body_index,
                                ScriptNative native) {
    natives_[native_body(class_name, body_index)] = native;
}

// FUN_00587b00 writes the same function slot as FUN_00587ac0.
void ScriptMachine::bind_task_native(const std::string& class_name, std::uint16_t body_index,
                                     ScriptTaskNative native) {
    task_natives_[native_body(class_name, body_index)] = native;
}

std::uint32_t ScriptMachine::method_of_body(std::uint32_t body) const {
    const ScriptProgramBody& record = program_.bodies().at(body);
    return program_.classes()[record.owner].methods[record.source->method];
}

// The body in the slot of `method`, in the table of the current state of the
// object (its first data byte) or of state 0; id + 1, 0 for none.
std::uint32_t ScriptMachine::body_in_state(ScriptObjectId object, std::uint32_t method,
                                           bool state_zero) const {
    const ScriptProgramClass& cls = program_.classes()[class_of(object)];
    const std::uint32_t state = state_zero ? 0 : memory_.u8(data_of(object));
    const std::uint16_t slot = program_.methods().at(method).source->slot;
    if (state >= cls.states.size() || slot >= cls.states[state].size()) {
        throw ScriptError("script call finds no slot " + std::to_string(slot) + " in state " +
                          std::to_string(state) + " of " + cls.name);
    }
    return cls.states[state][slot];
}

namespace {

// The size of a value of the type (FUN_00587d30): one byte for a boolean and
// a byte, the elements of an array, four bytes for the others.
std::uint32_t type_size(const ScriptType& type) {
    switch (type.kind) {
    case 1:
    case 5:
        return 1;
    case 2:
    case 3:
    case 4:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
        return 4;
    case 6:
        return type.inner.empty() ? 0 : type_size(type.inner[0]) * type.count;
    default:
        return 0;
    }
}

const ScriptType& element_of(const ScriptType& array) {
    if (array.inner.empty()) {
        throw ScriptError("script array type has no element type");
    }
    return array.inner[0];
}

// Kind 7 (a collection) appears in no package of the game data
// (docs/SCRIPTS.md, "Values").
[[noreturn]] void collection_kind() {
    throw ScriptError("script variable of kind 7 is not used by the game data");
}

}  // namespace

void ScriptMachine::register_value_type(const std::string& name, std::unique_ptr<ScriptValueType> type) {
    value_types_[name] = std::move(type);
}

ScriptValueType& ScriptMachine::value_type(const std::string& name) {
    const auto found = value_types_.find(name);
    if (found == value_types_.end() || found->second == nullptr) {
        throw ScriptError("script value type " + name + " is not ported");
    }
    return *found->second;
}

// position FUN_0079e3c0, rotation FUN_0079e0a0, color FUN_0079ddd0,
// gridposition FUN_0079b080. Without a source the original zeroes the block,
// except gridposition, which leaves it as its allocator gives it; the arena
// gives zeroed blocks.
void ScriptBlockType::init(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress block = memory.allocate(size_);
    if (source != 0) {
        memory.copy(block, source, size_);
    }
    memory.set_u32(slot, block);
}

// FUN_0079e430, FUN_0079e110, FUN_0079de50, FUN_0079b0d0.
void ScriptBlockType::destroy(ScriptMachine& machine, ScriptAddress slot) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress block = memory.u32(slot);
    if (block != 0) {
        memory.release(block);
    }
    memory.set_u32(slot, 0);
}

// FUN_0079e450, FUN_0079e130, FUN_0079de70, FUN_0079b0f0: nothing when the
// slot already holds the source block.
void ScriptBlockType::assign(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress block = memory.u32(slot);
    if (block == source) {
        return;
    }
    if (block == 0) {
        init(machine, slot, source);
        return;
    }
    if (source == 0) {
        for (std::uint32_t offset = 0; offset < size_; offset += 4) {
            memory.set_u32(block + offset, 0);
        }
        return;
    }
    memory.copy(block, source, size_);
}

// FUN_0079e350, FUN_0079e020, FUN_0079dd50: the floats of the chunk;
// FUN_0079b060 reads nothing and returns no block.
void ScriptBlockType::read_default(ScriptMachine& machine, ScriptAddress slot,
                                   const std::vector<std::uint8_t>& chunk) {
    if (floats_ == 0) {
        init(machine, slot, 0);
        return;
    }
    if (chunk.size() < floats_ * 4U) {
        throw ScriptError("script value default holds " + std::to_string(chunk.size()) + " bytes, not " +
                          std::to_string(floats_ * 4U));
    }
    init(machine, slot, 0);
    ScriptMemory& memory = machine.memory();
    const ScriptAddress block = memory.u32(slot);
    for (std::uint32_t index = 0; index < floats_ * 4U; ++index) {
        memory.set_u8(block + index, chunk[index]);
    }
}

void ScriptWaypointsType::init(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    machine.memory().set_u32(slot, source);
}

void ScriptWaypointsType::destroy(ScriptMachine& machine, ScriptAddress slot) {
    static_cast<void>(machine);
    static_cast<void>(slot);
}

void ScriptWaypointsType::assign(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    machine.memory().set_u32(slot, source);
}

// FUN_0079e650: a u32 count, then count points of three floats; the list is
// made once for the chunk and every slot shares it.
void ScriptWaypointsType::read_default(ScriptMachine& machine, ScriptAddress slot,
                                       const std::vector<std::uint8_t>& chunk) {
    const auto found = defaults_.find(chunk.data());
    if (found != defaults_.end()) {
        init(machine, slot, found->second);
        return;
    }
    if (chunk.size() < 4) {
        throw ScriptError("script waypoints default has no count");
    }
    const std::uint32_t count = static_cast<std::uint32_t>(chunk[0]) | static_cast<std::uint32_t>(chunk[1]) << 8U |
                                static_cast<std::uint32_t>(chunk[2]) << 16U |
                                static_cast<std::uint32_t>(chunk[3]) << 24U;
    if (count > 0x10000U || chunk.size() != 4U + count * 12U) {
        throw ScriptError("script waypoints default holds " + std::to_string(chunk.size()) + " bytes for " +
                          std::to_string(count) + " points");
    }
    ScriptMemory& memory = machine.memory();
    const ScriptAddress list = memory.allocate(8U + count * 12U);
    memory.set_u32(list, count);
    memory.set_u32(list + 4, list + 8);
    for (std::uint32_t index = 0; index < count * 12U; ++index) {
        memory.set_u8(list + 8 + index, chunk[4 + index]);
    }
    defaults_.emplace(chunk.data(), list);
    init(machine, slot, list);
}

// CreatureNativeType: FUN_0076b2f0, FUN_0076b300, FUN_0076b310; no default (FUN_004cb480).
void ScriptWordType::init(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    machine.memory().set_u32(slot, source);
}

void ScriptWordType::destroy(ScriptMachine& machine, ScriptAddress slot) { machine.memory().set_u32(slot, 0); }

void ScriptWordType::assign(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    machine.memory().set_u32(slot, source);
}

void ScriptWordType::read_default(ScriptMachine& machine, ScriptAddress slot, const std::vector<std::uint8_t>& chunk) {
    static_cast<void>(chunk);
    init(machine, slot, 0);
}

namespace {

void release_counted(ScriptMemory& memory, ScriptAddress object) {
    const std::uint32_t count = memory.u32(object + 4) - 1;
    memory.set_u32(object + 4, count);
    if (count == 0) {
        memory.release(object);  // the object's virtual +4 in the original
    }
}

}  // namespace

// NTcreaturecontroller: FUN_0079d4b0, FUN_0079d4c0, FUN_0079d4e0; no default (FUN_0079d490).
void ScriptCountedType::init(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    static_cast<void>(source);
    machine.memory().set_u32(slot, 0);
}

void ScriptCountedType::destroy(ScriptMachine& machine, ScriptAddress slot) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress object = memory.u32(slot);
    if (object != 0) {
        release_counted(memory, object);
        memory.set_u32(slot, 0);
    }
}

void ScriptCountedType::assign(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress old = memory.u32(slot);
    if (old == source) {
        return;
    }
    if (source != 0) {
        memory.set_u32(source + 4, memory.u32(source + 4) + 1);
    }
    memory.set_u32(slot, source);
    if (old != 0) {
        release_counted(memory, old);
    }
}

void ScriptCountedType::read_default(ScriptMachine& machine, ScriptAddress slot,
                                     const std::vector<std::uint8_t>& chunk) {
    static_cast<void>(chunk);
    init(machine, slot, 0);
}

// No engine system finds a name yet: the slot starts at 0 as for a name the
// original does not find (FUN_0079c0b0, FUN_00515140, FUN_0079b410). Destroy:
// FUN_0079c1b0, FUN_005151d0, FUN_0079b440. Assign: FUN_0079c1d0, FUN_005151f0,
// FUN_0079b450.
void ScriptEngineObjectType::init(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    static_cast<void>(source);
    machine.memory().set_u32(slot, 0);
}

void ScriptEngineObjectType::destroy(ScriptMachine& machine, ScriptAddress slot) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress object = memory.u32(slot);
    if (kind_ == Kind::soundgroup || object == 0) {
        return;  // 0x79b440 does nothing
    }
    release_counted(memory, object);
    if (kind_ == Kind::effect) {
        memory.set_u32(slot, 0);  // 0x5151d0; the sound keeps its slot (FUN_0079c1b0)
    }
}

void ScriptEngineObjectType::assign(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    ScriptMemory& memory = machine.memory();
    if (kind_ == Kind::soundgroup) {
        memory.set_u32(slot, source);  // 0x79b450
        return;
    }
    // FUN_0079c1d0, 0x5151f0: share the counted object.
    const ScriptAddress old = memory.u32(slot);
    if (old == source) {
        return;
    }
    if (old != 0) {
        release_counted(memory, old);
    }
    memory.set_u32(slot, source);
    if (source != 0) {
        memory.set_u32(source + 4, memory.u32(source + 4) + 1);
    }
}

// The default is a name the engine would look up (FUN_0079c010, FUN_005150a0,
// FUN_0079b320); with no engine system the result is the slot of a name not
// found.
void ScriptEngineObjectType::read_default(ScriptMachine& machine, ScriptAddress slot,
                                          const std::vector<std::uint8_t>& chunk) {
    static_cast<void>(chunk);
    init(machine, slot, 0);
}

// The initial value of a variable (FUN_00588390) by the kind of its type.
void ScriptMachine::init_variable(ScriptAddress address, const ScriptType& type) {
    switch (type.kind) {
    case 1:
    case 5:
        memory_.set_u8(address, 0);
        break;
    case 2:
    case 3:
    case 8:
    case 10:
    case 11:
        memory_.set_u32(address, 0);
        break;
    case 4:
        // FUN_00588390: a new 4-byte holder of the shared empty string.
        memory_.set_u32(address, new_holder(empty_record_));
        break;
    case 6: {
        const ScriptType& element = element_of(type);
        const std::uint32_t size = type_size(element);
        for (std::uint32_t index = 0; index < type.count; ++index) {
            init_variable(address + index * size, element);
        }
        break;
    }
    case 7:
        collection_kind();
    case 9:
        value_type(type.name).init(*this, address, 0);
        break;
    default:
        break;  // the original leaves a variable of any other kind as it is
    }
}

void ScriptMachine::store_value(ScriptAddress address, const ScriptType& type, std::uint32_t value) {
    switch (type.kind) {
    case 1:
    case 5:
        memory_.set_u8(address, static_cast<std::uint8_t>(value & 0xFFU));
        break;
    case 2:
    case 3:
    case 11:
        memory_.set_u32(address, value);
        break;
    case 8:
        memory_.set_u32(address, 0);
        break;
    case 4:
        // FUN_00587fc0: a new holder of the string of the holder `value`,
        // counted; no holder gives the shared empty string.
        memory_.set_u32(address, new_holder(value != 0 ? memory_.u32(value) : empty_record_));
        break;
    case 10:
        memory_.set_u32(address, value);
        if (value != 0) {
            memory_.set_u32(value + 4, memory_.u32(value + 4) + 1);
        }
        break;
    case 6: {
        // Arrays and values of a class take the address of the source
        // element; the other elements take its value, and with no source
        // their initial value.
        const ScriptType& element = element_of(type);
        const std::uint32_t size = type_size(element);
        ScriptAddress source = value;
        for (std::uint32_t index = 0; index < type.count; ++index) {
            const ScriptAddress target = address + index * size;
            if (element.kind == 6 || element.kind == 9) {
                store_value(target, element, source);
            } else if (source == 0) {
                init_variable(target, element);
            } else {
                store_value(target, element, size == 1 ? memory_.u8(source) : memory_.u32(source));
            }
            if (source != 0) {
                source += size;
            }
        }
        break;
    }
    case 7:
        collection_kind();
    case 9: {
        // A new block, then the value of the block the slot at `value` holds.
        ScriptValueType& kind = value_type(type.name);
        kind.init(*this, address, 0);
        if (value != 0) {
            kind.assign(*this, address, memory_.u32(value));
        }
        break;
    }
    default:
        break;
    }
}

void ScriptMachine::copy_argument(ScriptAddress address, const ScriptType& type, ScriptAddress cell) {
    std::uint32_t value = memory_.u32(cell);
    if (type.kind == 10 && value != 0) {
        value = handle_of(value);
    }
    store_value(address, type, value);
}

// FUN_00588570: a variable gives back what it holds.
void ScriptMachine::destroy_variable(ScriptAddress address, const ScriptType& type) {
    switch (type.kind) {
    case 4: {
        // The string of the holder is released, then the holder is freed.
        const ScriptAddress holder = memory_.u32(address);
        if (holder != 0) {
            const ScriptAddress record = memory_.u32(holder);
            if (record != 0) {
                release_record(record);
            }
            memory_.release(holder);
        }
        break;
    }
    case 6: {
        const ScriptType& element = element_of(type);
        const std::uint32_t size = type_size(element);
        for (std::uint32_t index = 0; index < type.count; ++index) {
            destroy_variable(address + index * size, element);
        }
        break;
    }
    case 7:
        collection_kind();
    case 9:
        value_type(type.name).destroy(*this, address);
        break;
    case 10: {
        const ScriptAddress handle = memory_.u32(address);
        if (handle != 0) {
            release_handle(handle);
        }
        break;
    }
    default:
        break;  // the value holds nothing to give back
    }
}

// The count of a handle goes down by 1; at 0 the handle frees itself (its
// virtual +4, vtable 0x86df04). While its object lives the count is at least
// the object's own 1 (FUN_0057f830).
void ScriptMachine::release_handle(ScriptAddress handle) {
    const std::uint32_t count = memory_.u32(handle + 4) - 1;
    memory_.set_u32(handle + 4, count);
    if (count == 0) {
        memory_.release(handle);
    }
}

// FUN_0057d690: unless the frame is a copy, the parameters of the method and
// then the locals of the body give back what they hold, and the stack and the
// locals are freed; then the children are destroyed.
void ScriptMachine::end_frame(ScriptFrame& frame) {
    if (!frame.borrowed) {
        const ScriptProgramBody& body = program_.bodies().at(frame.body);
        const ScriptMethod& method = *program_.methods().at(method_of_body(frame.body)).source;
        for (const ScriptVariable& param : method.params) {
            destroy_variable(frame.locals + param.offset, param.type);
        }
        for (const ScriptVariable& local : body.source->locals) {
            destroy_variable(frame.locals + local.offset, local.type);
        }
        memory_.release(frame.stack);
        memory_.release(frame.locals);
    }
    for (std::unique_ptr<ScriptTask>& child : frame.children) {
        destroy_task(child);
    }
    frame.children.clear();
}

// The virtual +0 of a task: it gives back what it holds and is deleted. For a
// thread that is FUN_0057dc70, which calls the deleting destructor
// FUN_0057da50; its destructor FUN_0057dbe0 ends the frames (FUN_0057dc80).
void ScriptMachine::destroy_task(std::unique_ptr<ScriptTask>& task) {
    if (task != nullptr) {
        task->finish(*this);
        task.reset();
    }
}

void ScriptMachine::end_thread(ScriptThread& thread) {
    while (!thread.frames.empty()) {
        end_frame(thread.frames.back());
        thread.frames.pop_back();
    }
}

// The virtual +8 (FUN_0057dcf0) and +4 (FUN_0057da20) of a thread.
void ScriptThread::run(ScriptMachine& machine, std::uint32_t arg) { machine.run_thread(*this, arg); }

void ScriptThread::finish(ScriptMachine& machine) { machine.end_thread(*this); }

void ScriptMachine::drop_call(ScriptFrame& frame, std::uint32_t method) {
    const ScriptMethod& source = *program_.methods().at(method).source;
    frame.top -= static_cast<std::int32_t>(source.params.size()) + (source.result.kind != 0 ? 1 : 0);
    if (frame.top < -1) {
        throw ScriptError("script call pops more cells than its stack holds");
    }
}

// The tables of `tables` at the state of `object` (its first data byte); a
// state past the tables takes the table of state 0 except for slot 0.
std::uint32_t ScriptMachine::body_in_tables(ScriptObjectId object, std::uint32_t tables,
                                            std::uint32_t method) const {
    const ScriptProgramClass& owner = program_.classes()[tables];
    const std::uint16_t slot = program_.methods().at(method).source->slot;
    if (object == 0 || owner.states.empty() || slot >= owner.states[0].size()) {
        return 0;
    }
    const std::uint32_t state = memory_.u8(data_of(object));
    if (state < owner.states.size()) {
        return owner.states[state][slot];
    }
    return slot != 0 ? owner.states[0][slot] : 0;
}

// FUN_0057f6d0: the arguments are popped (the result cell is not); a script
// body runs as a new thread of the same owner (FUN_0057d810), a native body
// returns a task. The new child is added to the last frame and runs once.
void ScriptMachine::spawn(ScriptThread& thread, ScriptObjectId object, std::uint32_t body) {
    ScriptFrame& frame = thread.frames.back();
    const ScriptMethod& method = *program_.methods().at(method_of_body(body)).source;
    const auto argc = static_cast<std::uint32_t>(method.params.size());
    frame.top -= static_cast<std::int32_t>(argc);
    if (frame.top < -1) {
        throw ScriptError("script thread start pops more cells than its stack holds");
    }
    const ScriptAddress args = frame.stack + static_cast<std::uint32_t>(frame.top + 1) * 4U;
    std::unique_ptr<ScriptTask> child;
    const ScriptProgramBody& target = program_.bodies().at(body);
    if (target.source->native()) {
        const ScriptTaskNative native = task_natives_[body];
        if (native == nullptr) {
            throw ScriptError("task body " + std::to_string(target.index) + " of " +
                              program_.classes()[target.owner].name + " is not bound");
        }
        child = native(*this, object, thread.owner, argc, args);
        if (child == nullptr) {
            return;
        }
    } else {
        auto started = std::make_unique<ScriptThread>();
        started->owner = thread.owner;
        ScriptFrame first = make_frame(object, body);
        for (std::uint32_t index = 0; index < argc; ++index) {
            const ScriptVariable& param = method.params[index];
            copy_argument(first.locals + param.offset, param.type, args + index * 4U);
        }
        for (const ScriptVariable& local : target.source->locals) {
            init_variable(first.locals + local.offset, local.type);
        }
        started->frames.push_back(std::move(first));
        child = std::move(started);
    }
    frame.children.push_back(std::move(child));
    frame.children.back()->run(*this, 0);
}

// FUN_0057daf0, FUN_0057d5c0: the copy has the object, the body and the next
// instruction of the frame and the same stack and locals, with its stack top
// at -1.
void ScriptMachine::fork(ScriptThread& thread) {
    ScriptFrame& frame = thread.frames.back();
    ScriptFrame copy;
    copy.object = frame.object;
    copy.body = frame.body;
    copy.ip = frame.ip;
    copy.stack = frame.stack;
    copy.locals = frame.locals;
    copy.borrowed = true;
    auto child = std::make_unique<ScriptThread>();
    child->owner = thread.owner;
    child->frames.push_back(std::move(copy));
    frame.children.push_back(std::move(child));
    frame.children.back()->run(*this, 0);
}

// FUN_0057f580 (and 0x47): pops the arguments, the result address and, for
// the codes that take it from the stack, the object; then calls the native
// function at once or pushes the frame of a script body (FUN_0057d3e0).
bool ScriptMachine::call(ScriptThread& thread, std::uint32_t op, ScriptObjectId object,
                         std::uint32_t body, std::uint32_t method) {
    ScriptFrame& frame = thread.frames.back();
    Stack stack(memory_, frame);
    const ScriptMethod& source = *program_.methods().at(method).source;
    const bool popped_object = op == 0x47 || op == 0x4B || op == 0x4C;
    const bool has_result = source.result.kind != 0;
    const auto argc = static_cast<std::uint32_t>(source.params.size());
    if (popped_object) {
        object = stack.pop();
    }
    const ScriptAddress result = has_result ? stack.pop() : 0;
    frame.top -= static_cast<std::int32_t>(argc);
    if (frame.top < -1) {
        throw ScriptError("script call pops more cells than its stack holds");
    }
    const ScriptAddress args = frame.stack + static_cast<std::uint32_t>(frame.top + 1) * 4U;
    if (object == 0) {
        return false;
    }
    if (op == 0x4A || op == 0x4C) {
        // The body comes from the slot of the method (state table).
        body = body_in_state(object, method, false);
        if (body == 0) {
            return false;
        }
        --body;
    }
    const ScriptProgramBody& target = program_.bodies().at(body);
    if (op == 0x47 || target.source->native()) {
        const ScriptNative native = natives_[body];
        if (native == nullptr) {
            throw ScriptError("native body " + std::to_string(target.index) + " of " +
                              program_.classes()[target.owner].name + " is not bound");
        }
        native(*this, object, argc, args, result);
        // FUN_0057f580: a native that leaves a state pending on the owner of
        // the thread ends the frame; 0x47 calls the native itself and goes on.
        return op != 0x47 && owner_pending(thread);
    }
    ScriptFrame callee = make_frame(object, body);
    if (has_result) {
        memory_.set_u32(callee.locals, result);
    }
    for (std::uint32_t index = 0; index < argc; ++index) {
        const ScriptVariable& param = source.params[index];
        copy_argument(callee.locals + param.offset, param.type, args + index * 4U);
    }
    for (const ScriptVariable& local : target.source->locals) {
        init_variable(callee.locals + local.offset, local.type);
    }
    thread.frames.push_back(std::move(callee));
    return true;
}

bool ScriptMachine::run_thread(ScriptThread& thread, std::uint32_t arg) {
    if (thread.frames.empty()) {
        return false;
    }
    // Running a child never changes the frames of this thread.
    std::vector<std::unique_ptr<ScriptTask>>& children = thread.frames.back().children;
    for (std::size_t index = 0; index < children.size();) {
        children[index]->run(*this, arg);
        if (children[index]->running()) {
            ++index;
        } else {
            destroy_task(children[index]);
            children.erase(children.begin() + static_cast<std::ptrdiff_t>(index));
        }
        if (owner_pending(thread)) {
            return true;  // the owner changes state at the end of its run
        }
    }
    if (thread.frames.back().waiting) {
        if (!children.empty()) {
            return true;
        }
        thread.frames.back().waiting = false;
    }
    while (!thread.frames.empty()) {
        if (owner_pending(thread)) {
            return true;
        }
        const std::size_t frames = thread.frames.size();
        if (!interpret(thread)) {
            return true;
        }
        if (thread.frames.size() == frames) {
            end_frame(thread.frames.back());
            thread.frames.pop_back();
        }
    }
    return false;
}

ScriptFrame ScriptMachine::make_frame(ScriptObjectId object, std::uint32_t body) {
    if (body >= program_.bodies().size()) {
        throw ScriptError("script body id is out of range");
    }
    const ScriptBody& source = *program_.bodies()[body].source;
    ScriptFrame frame;
    frame.object = object;
    frame.body = body;
    frame.stack = memory_.allocate(static_cast<std::uint32_t>(source.stack_size) * 4U);
    frame.locals = memory_.allocate(std::max<std::uint32_t>(source.locals_size, 4));
    return frame;
}

ScriptAddress ScriptMachine::name_cell(std::uint32_t index) const {
    if (index >= program_.names().size()) {
        throw ScriptError("script name index is out of range");
    }
    return name_table_ + index * 4U;
}

ScriptAddress ScriptMachine::new_record(std::string_view text) {
    const ScriptAddress record = memory_.allocate(static_cast<std::uint32_t>(text.size() + 5));
    memory_.set_u32(record, 1);
    for (std::size_t index = 0; index < text.size(); ++index) {
        memory_.set_u8(record + 4 + static_cast<std::uint32_t>(index),
                       static_cast<std::uint8_t>(text[index]));
    }
    return record;
}

void ScriptMachine::release_record(ScriptAddress record) {
    const std::uint32_t count = memory_.u32(record) - 1;
    memory_.set_u32(record, count);
    if (count == 0) {
        memory_.release(record);
    }
}

ScriptAddress ScriptMachine::new_string_cell(std::string_view text) {
    const ScriptAddress record = new_record(text);
    const ScriptAddress cell = memory_.allocate(4);
    memory_.set_u32(cell, record);
    return cell;
}

void ScriptMachine::release_string_cell(ScriptAddress cell) {
    if (cell == 0) {
        return;
    }
    const ScriptAddress record = memory_.u32(cell);
    if (record != 0) {
        release_record(record);
    }
    memory_.release(cell);
}

// FUN_0068eaa0 on the caller's cell: the temporary the native built is
// released after the store, so the cell ends with the only count of the new
// record.
void ScriptMachine::store_string_result(ScriptAddress result, std::string_view text) {
    if (result == 0) {
        return;
    }
    const ScriptAddress cell = memory_.u32(result);
    if (cell == 0) {
        return;
    }
    const ScriptAddress old = memory_.u32(cell);
    memory_.set_u32(cell, new_record(text));
    if (old != 0) {
        release_record(old);
    }
}

// A string variable holds the address of a 4-byte holder of a counted
// string record (FUN_00588390, FUN_00587fc0), the same thing a string cell
// on the stack is.
ScriptAddress ScriptMachine::new_holder(ScriptAddress record) {
    const ScriptAddress holder = memory_.allocate(4);
    memory_.set_u32(holder, record);
    if (record != 0) {
        memory_.set_u32(record, memory_.u32(record) + 1);
    }
    return holder;
}

std::string ScriptMachine::string_at(ScriptAddress cell) const {
    std::string text;
    if (cell == 0) {
        return text;
    }
    const ScriptAddress record = memory_.u32(cell);
    if (record == 0) {
        return text;
    }
    for (ScriptAddress address = record + 4;; ++address) {
        const std::uint8_t value = memory_.u8(address);
        if (value == 0) {
            return text;
        }
        text.push_back(static_cast<char>(value));
    }
}

bool ScriptMachine::is_a(std::uint32_t class_id, const std::string& name) const {
    for (std::uint32_t id = class_id + 1; id != 0; id = program_.classes()[id - 1].parent) {
        if (program_.classes()[id - 1].name == name) {
            return true;
        }
    }
    return false;
}

// FUN_0057de80: read the word at ip, move ip past it and dispatch on the
// high byte. The instructions are those of docs/SCRIPTS.md, "Instructions";
// a value of a class needs the native type of the class.
bool ScriptMachine::interpret(ScriptThread& thread) {
    if (thread.frames.empty()) {
        throw ScriptError("script thread has no frame");
    }
    // The loop guard restarts when no other interpreter is running.
    struct Depth {
        explicit Depth(int& depth) : depth_(depth) { ++depth_; }
        ~Depth() { --depth_; }
        Depth(const Depth&) = delete;
        Depth& operator=(const Depth&) = delete;
        int& depth_;
    } depth(depth_);
    if (depth_ == 1) {
        loop_count_ = 0;
    }
    ScriptFrame& frame = thread.frames.back();
    const std::vector<std::uint32_t>& code = program_.bodies().at(frame.body).code;
    Stack stack(memory_, frame);
    const auto next_word = [&]() {
        if (frame.ip >= code.size()) {
            throw ScriptError("script frame ran past the end of its code");
        }
        return code[frame.ip++];
    };
    // A linked word that holds an id + 1 (docs/ARCHITECTURE.md).
    const auto next_id = [&](std::size_t count, const char* what) {
        const std::uint32_t value = next_word();
        if (value == 0 || value > count) {
            throw ScriptError(std::string("script call names no ") + what);
        }
        return value - 1;
    };
    for (;;) {
        const std::uint32_t word = next_word();
        const std::uint32_t op = word >> 24;
        const std::uint32_t imm = word & 0xFFFFFFU;
        const auto simm = static_cast<std::int32_t>(word << 8) >> 8;
        switch (op) {
        case 0x00:  // mark: the object's debugger hook, always null (DAT_00a6a698 is never written)
            break;
        case 0x01:
            stack.pop();
            break;
        case 0x02:
            stack.push(next_word());
            break;
        case 0x03:
            stack.push(static_cast<std::uint32_t>(simm));
            break;
        case 0x04:
            stack.push(name_cell(imm));
            break;
        case 0x05:
            stack.push(memory_.u8(frame.locals + imm));
            break;
        case 0x06:
            stack.push(memory_.u32(frame.locals + imm));
            break;
        case 0x07:
            stack.push(frame.locals + imm);
            break;
        case 0x08: {
            const ScriptAddress handle = memory_.u32(frame.locals + imm);
            stack.push(handle != 0 ? memory_.u32(handle + 8) : 0);
            break;
        }
        case 0x09:
            stack.push(memory_.u8(data_of(frame.object) + imm));
            break;
        case 0x0A:
            stack.push(memory_.u32(data_of(frame.object) + imm));
            break;
        case 0x0B:
            stack.push(data_of(frame.object) + imm);
            break;
        case 0x0C: {
            const ScriptAddress handle = memory_.u32(data_of(frame.object) + imm);
            stack.push(handle != 0 ? memory_.u32(handle + 8) : 0);
            break;
        }
        case 0x0D: {
            const ScriptObjectId object = stack.top();
            stack.replace(object != 0 ? memory_.u8(data_of(object) + imm) : 0);
            break;
        }
        case 0x0E: {
            const ScriptObjectId object = stack.top();
            stack.replace(object != 0 ? memory_.u32(data_of(object) + imm) : 0);
            break;
        }
        case 0x0F: {
            const ScriptObjectId object = stack.top();
            stack.replace(object != 0 ? data_of(object) + imm : 0);
            break;
        }
        case 0x17: {
            // An element of an array: the address under the index moves
            // `imm` bytes per index, unless it is 0.
            const std::uint32_t index = stack.pop();
            const ScriptAddress address = stack.top();
            if (address != 0) {
                stack.replace(imm * index + address);
            }
            break;
        }
        case 0x18:
            // The bound of an index: below 0 or not below `imm`, the cell
            // under it becomes 0; the index stays.
            if (static_cast<std::int32_t>(stack.top()) < 0 ||
                static_cast<std::int32_t>(imm) <= static_cast<std::int32_t>(stack.top())) {
                stack.replace_under(0);
            }
            break;
        case 0x1C: {
            const std::uint32_t value = stack.pop();
            const ScriptAddress address = stack.pop();
            if (address != 0) {
                memory_.set_u8(address, static_cast<std::uint8_t>(value & 0xFFU));
            }
            break;
        }
        case 0x1D: {
            const std::uint32_t value = stack.pop();
            const ScriptAddress address = stack.pop();
            if (address != 0) {
                memory_.set_u32(address, value);
            }
            break;
        }
        case 0x1E: {
            const ScriptObjectId object = stack.pop();
            const ScriptAddress address = stack.pop();
            if (address == 0) {
                break;
            }
            const ScriptAddress old_handle = memory_.u32(address);
            if (old_handle != 0) {
                release_handle(old_handle);
            }
            if (object == 0) {
                memory_.set_u32(address, 0);
            } else {
                const ScriptAddress handle = handle_of(object);
                memory_.set_u32(address, handle);
                memory_.set_u32(handle + 4, memory_.u32(handle + 4) + 1);
            }
            break;
        }
        case 0x1F: {
            // FUN_0068eaa0 on the string at `address`: take the record of the
            // string at `cell`, counted.
            const ScriptAddress cell = stack.pop();
            const ScriptAddress address = stack.pop();
            if (address == 0 || cell == 0) {
                break;
            }
            const ScriptAddress old_record = memory_.u32(address);
            const ScriptAddress record = memory_.u32(cell);
            if (old_record == record) {
                break;
            }
            if (old_record != 0) {
                release_record(old_record);
            }
            memory_.set_u32(address, record);
            if (record != 0) {
                memory_.set_u32(record, memory_.u32(record) + 1);
            }
            break;
        }
        case 0x21: {
            // A value of a class: pop the address of the source slot, then
            // the target slot; the target takes the source's block value.
            const std::uint32_t type = next_id(program_.value_types().size(), "value type");
            const ScriptAddress source = stack.pop();
            const ScriptAddress target = stack.pop();
            if (target != 0 && source != 0) {
                value_type(program_.value_types()[type]).assign(*this, target, memory_.u32(source));
            }
            break;
        }
        case 0x27:
            stack.replace(0U - stack.top());
            break;
        case 0x28: {
            const std::uint32_t right = stack.pop();
            stack.replace(stack.top() + right);
            break;
        }
        case 0x29: {
            const std::uint32_t right = stack.pop();
            stack.replace(stack.top() - right);
            break;
        }
        case 0x2D: {
            const std::int32_t right = as_int(stack.pop());
            stack.replace_byte(as_int(stack.top()) < right);
            break;
        }
        case 0x2E: {
            const std::int32_t right = as_int(stack.pop());
            stack.replace_byte(as_int(stack.top()) <= right);
            break;
        }
        case 0x2F: {
            const std::uint32_t right = stack.pop();
            stack.replace_byte(stack.top() == right);
            break;
        }
        case 0x30:
            stack.replace(float_bits(-as_float(stack.top())));
            break;
        case 0x31: {
            const float right = as_float(stack.pop());
            stack.replace(float_bits(as_float(stack.top()) + right));
            break;
        }
        case 0x32: {
            const float right = as_float(stack.pop());
            stack.replace(float_bits(as_float(stack.top()) - right));
            break;
        }
        case 0x33: {
            const float right = as_float(stack.pop());
            stack.replace(float_bits(as_float(stack.top()) * right));
            break;
        }
        case 0x34: {
            const float right = as_float(stack.pop());
            const float left = as_float(stack.top());
            if (right != 0.0f) {
                stack.replace(float_bits(left / right));
            } else if (0.0f <= left) {
                stack.replace(left <= 0.0f ? 0U : kLargestFloat);
            } else {
                stack.replace(float_bits(-as_float(kLargestFloat)));
            }
            break;
        }
        case 0x35: {
            const float right = as_float(stack.pop());
            stack.replace_byte(!(right <= as_float(stack.top())));
            break;
        }
        case 0x36: {
            const float right = as_float(stack.pop());
            const float left = as_float(stack.top());
            stack.replace_byte(left < right || left == right);
            break;
        }
        case 0x37: {
            const float right = as_float(stack.pop());
            stack.replace_byte(as_float(stack.top()) == right);
            break;
        }
        case 0x38: {
            // FUN_0068ec30: equal byte by byte.
            const ScriptAddress right = stack.pop();
            const ScriptAddress left = stack.top();
            stack.replace_byte(left != 0 && right != 0 && string_at(left) == string_at(right));
            break;
        }
        case 0x39:
            stack.replace_byte(stack.top_byte() == 0);
            break;
        case 0x3A: {
            const std::uint8_t right = static_cast<std::uint8_t>(stack.pop() & 0xFFU);
            stack.replace_byte(stack.top_byte() != 0 && right != 0);
            break;
        }
        case 0x3B: {
            const std::uint8_t right = static_cast<std::uint8_t>(stack.pop() & 0xFFU);
            stack.replace_byte(stack.top_byte() != 0 || right != 0);
            break;
        }
        case 0x3C:
            stack.replace(ftol(as_float(stack.top())));
            break;
        case 0x3E:
            stack.replace(float_bits(static_cast<float>(as_int(stack.top()))));
            break;
        case 0x45:
            frame.ip += static_cast<std::uint32_t>(simm);
            break;
        case 0x46:
            if ((stack.pop() & 0xFFU) == 0) {
                frame.ip += static_cast<std::uint32_t>(simm);
            }
            break;
        case 0x60:
            if (++loop_count_ > kLoopLimit) {
                const std::string class_name =
                    frame.object != 0 ? program_.classes()[class_of(frame.object)].name : std::string();
                const ScriptProgramBody& body = program_.bodies()[frame.body];
                const std::string& method =
                    program_.classes()[body.owner].source->methods[body.source->method].name;
                warnings_.push_back("WARNING: Scripted loop ran too long: " + class_name + ", " + method);
                return true;
            }
            frame.ip += static_cast<std::uint32_t>(simm);
            break;
        case 0x47:
        case 0x48:
        case 0x4B: {
            // The next word names a body (id + 1).
            const std::uint32_t body = next_id(program_.bodies().size(), "body");
            if (call(thread, op, op == 0x48 ? frame.object : 0, body, method_of_body(body))) {
                return true;
            }
            break;
        }
        case 0x4A:
        case 0x4C: {
            // The next word names a method (id + 1); the body is in its slot.
            // In 0x4a, bit 0 of imm picks the table of state 0.
            const std::uint32_t method = next_id(program_.methods().size(), "method");
            if (op == 0x4A && (imm & 1U) != 0) {
                const std::uint32_t body = frame.object != 0 ? body_in_state(frame.object, method, true) : 0;
                if (body == 0) {
                    call(thread, op, 0, 0, method);
                    break;
                }
                if (call(thread, 0x48, frame.object, body - 1, method)) {
                    return true;
                }
                break;
            }
            if (call(thread, op, op == 0x4A ? frame.object : 0, 0, method)) {
                return true;
            }
            break;
        }
        case 0x49: {
            // The tables of a class (id + 1) and a method (id + 1).
            const std::uint32_t tables = next_id(program_.classes().size(), "class");
            const std::uint32_t method = next_id(program_.methods().size(), "method");
            const std::uint32_t body = body_in_tables(frame.object, tables, method);
            if (body == 0) {
                call(thread, op, 0, 0, method);
                break;
            }
            if (call(thread, 0x48, frame.object, body - 1, method)) {
                return true;
            }
            break;
        }
        // Thread starts (FUN_0057f6d0): each finds its body as the call it
        // mirrors (0x4e as 0x48, 0x4f and 0x55 as 0x49, 0x50 and 0x56 as
        // 0x4a, 0x51 as 0x4b, 0x52 as 0x4c). 0x4e to 0x52 then wait for the
        // children of the frame; 0x55 and 0x56 go on.
        case 0x4D:
        case 0x53: {
            // A native body that returns a task, on an object from the stack;
            // 0x53 does not wait for it.
            const std::uint32_t body = next_id(program_.bodies().size(), "body");
            const ScriptObjectId object = stack.pop();
            if (object == 0) {
                drop_call(frame, method_of_body(body));
            } else {
                spawn(thread, object, body);
            }
            if (op == 0x53) {
                break;
            }
            frame.waiting = true;
            return false;
        }
        case 0x4E:
        case 0x51:
        case 0x54:
        case 0x57: {
            const std::uint32_t body = next_id(program_.bodies().size(), "body");
            const ScriptObjectId object = op == 0x4E || op == 0x54 ? frame.object : stack.pop();
            if (object == 0) {
                drop_call(frame, method_of_body(body));
            } else {
                spawn(thread, object, body);
            }
            if (op == 0x54 || op == 0x57) {
                break;
            }
            frame.waiting = true;
            return false;
        }
        case 0x4F:
        case 0x55: {
            const std::uint32_t tables = next_id(program_.classes().size(), "class");
            const std::uint32_t method = next_id(program_.methods().size(), "method");
            const std::uint32_t body = body_in_tables(frame.object, tables, method);
            if (body == 0) {
                drop_call(frame, method);
            } else {
                spawn(thread, frame.object, body - 1);
            }
            if (op == 0x4F) {
                frame.waiting = true;
                return false;
            }
            break;
        }
        case 0x50:
        case 0x56: {
            const std::uint32_t method = next_id(program_.methods().size(), "method");
            const std::uint32_t body = body_in_state(frame.object, method, (imm & 1U) != 0);
            if (body == 0) {
                drop_call(frame, method);
            } else {
                spawn(thread, frame.object, body - 1);
            }
            if (op == 0x50) {
                frame.waiting = true;
                return false;
            }
            break;
        }
        case 0x52:
        case 0x58: {
            const std::uint32_t method = next_id(program_.methods().size(), "method");
            const ScriptObjectId object = stack.pop();
            const std::uint32_t body = object != 0 ? body_in_state(object, method, false) : 0;
            if (body == 0) {
                drop_call(frame, method);
            } else {
                spawn(thread, object, body - 1);
            }
            if (op == 0x58) {
                break;
            }
            frame.waiting = true;
            return false;
        }
        case 0x59:
        case 0x5A:
            // A copy of the frame runs the next instruction on; the frame
            // skips imm words past it.
            fork(thread);
            frame.ip += imm;
            if (op == 0x59) {
                frame.waiting = true;
                return false;
            }
            if (owner_pending(thread)) {
                return false;
            }
            break;
        case 0x5B:
            // The frame ends once it has no children; until then it waits
            // here.
            if (frame.children.empty()) {
                return true;
            }
            --frame.ip;
            frame.waiting = true;
            return false;
        case 0x5C:
            return true;
        case 0x5E:
            // A state for the object of the frame (its virtual +0x14,
            // FUN_00580830); the frame ends.
            request_state(frame.object, static_cast<std::int32_t>(stack.pop()));
            return true;
        case 0x5D:
            return false;
        case 0x5F: {
            const ScriptObjectId object = stack.top();
            if (imm >= program_.names().size()) {
                throw ScriptError("script name index is out of range");
            }
            if (object != 0 && !is_a(class_of(object), program_.names()[imm])) {
                stack.replace(0);
            }
            break;
        }
        default:
            if (unused_by_the_game(op)) {
                throw ScriptError("script instruction " + hex_code(op) + " is not used by the game data");
            }
            throw ScriptError("script instruction " + hex_code(op) + " is not ported");
        }
    }
}

}  // namespace bo2::engine
