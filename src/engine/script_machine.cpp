#include "bo2/engine/script_machine.hpp"

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

// The codes that no body of the game data uses (in italics in
// docs/SCRIPTS.md, "Instructions").
bool unused_by_the_game(std::uint32_t code) {
    switch (code) {
    case 0x0D:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
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
    case 0x37:
    case 0x3D:
    case 0x3F:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x53:
    case 0x54:
    case 0x57:
    case 0x58:
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
    : program_(program), natives_(program.bodies().size(), nullptr) {
    empty_record_ = new_record("");
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
    objects_.push_back(object);
    return id;
}

const ScriptMachine::Object& ScriptMachine::object_at(ScriptObjectId object) const {
    if (object == 0 || object > objects_.size()) {
        throw ScriptError("script object does not exist");
    }
    return objects_[object - 1];
}

ScriptAddress ScriptMachine::data_of(ScriptObjectId object) const { return object_at(object).data; }
ScriptAddress ScriptMachine::handle_of(ScriptObjectId object) const { return object_at(object).handle; }
std::uint32_t ScriptMachine::class_of(ScriptObjectId object) const { return object_at(object).class_id; }

void ScriptMachine::bind_native(const std::string& class_name, std::uint16_t body_index,
                                ScriptNative native) {
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
    natives_[body] = native;
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
        memory_.set_u32(address, empty_record_);
        memory_.set_u32(empty_record_, memory_.u32(empty_record_) + 1);
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
        throw ScriptError("script variable of kind 9 is not ported");
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
    case 4: {
        // The cell's string, counted; no cell gives the shared empty string.
        const ScriptAddress record = value != 0 ? memory_.u32(value) : empty_record_;
        memory_.set_u32(address, record);
        if (record != 0) {
            memory_.set_u32(record, memory_.u32(record) + 1);
        }
        break;
    }
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
    case 9:
        throw ScriptError("script value of kind 9 is not ported");
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
        // The original also frees the 4-byte holder of the string; the
        // port's variable is the holder (docs/ARCHITECTURE.md).
        const ScriptAddress record = memory_.u32(address);
        if (record != 0) {
            release_record(record);
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
        throw ScriptError("script value of kind 9 is not ported");
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

// FUN_0057d690: the parameters of the method and then the locals of the body
// give back what they hold, and the stack and the locals are freed.
void ScriptMachine::end_frame(ScriptFrame& frame) {
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
        return false;
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
    thread.frames.push_back(callee);
    return true;
}

bool ScriptMachine::run_thread(ScriptThread& thread) {
    while (!thread.frames.empty()) {
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
// high byte. The instructions are those of docs/SCRIPTS.md, "Values and
// variables", "Arithmetic and logic" and "Flow"; calls and threads are not
// ported yet.
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
        case 0x00:  // mark: the original calls the object's debugger
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
        case 0x21:
            next_word();
            throw ScriptError("script instruction 0x21: value types are not ported");
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
            const ScriptProgramClass& owner = program_.classes()[tables];
            const std::uint16_t slot = program_.methods().at(method).source->slot;
            std::uint32_t body = 0;
            if (frame.object != 0 && !owner.states.empty() && slot < owner.states[0].size()) {
                const std::uint32_t state = memory_.u8(data_of(frame.object));
                if (state < owner.states.size()) {
                    body = owner.states[state][slot];
                } else if (slot != 0) {
                    body = owner.states[0][slot];
                }
            }
            if (body == 0) {
                call(thread, op, 0, 0, method);
                break;
            }
            if (call(thread, 0x48, frame.object, body - 1, method)) {
                return true;
            }
            break;
        }
        case 0x5C:
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
