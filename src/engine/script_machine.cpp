#include "bo2/engine/script_machine.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

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

ScriptMachine::ScriptMachine(const ScriptProgram& program) : program_(program) {
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

// FUN_0057de80: read the word at ip, move ip past it and dispatch on the
// high byte. The instructions are those of docs/SCRIPTS.md, "Values and
// variables"; calls, threads, jumps and arithmetic are not ported yet.
bool ScriptMachine::interpret(ScriptThread& thread) {
    if (thread.frames.empty()) {
        throw ScriptError("script thread has no frame");
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
                memory_.set_u32(old_handle + 4, memory_.u32(old_handle + 4) - 1);
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
        case 0x5C:
            return true;
        default:
            if (unused_by_the_game(op)) {
                throw ScriptError("script instruction " + hex_code(op) + " is not used by the game data");
            }
            throw ScriptError("script instruction " + hex_code(op) + " is not ported");
        }
    }
}

}  // namespace bo2::engine
