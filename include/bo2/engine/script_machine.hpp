#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bo2/engine/script_memory.hpp"
#include "bo2/engine/script_program.hpp"

namespace bo2::engine {

// The script machine (docs/SCRIPTS.md, "Threads and frames", "Instructions";
// docs/ARCHITECTURE.md, "Script machine"). Everything a script can address
// lives in the machine's ScriptMemory.

using ScriptObjectId = std::uint32_t;  // index in the object table + 1; 0 is null

// A frame of a thread (FUN_0057d3e0): the object, the body (program id), the
// next code word, the top of its stack (-1 when empty), its stack of 4-byte
// cells and its locals.
struct ScriptFrame {
    ScriptObjectId object = 0;
    std::uint32_t body = 0;
    std::uint32_t ip = 0;
    std::int32_t top = -1;
    ScriptAddress stack = 0;
    ScriptAddress locals = 0;
    bool waiting = false;
};

// A thread: its last frame is the one that runs.
struct ScriptThread {
    std::vector<ScriptFrame> frames;
};

class ScriptMachine {
public:
    // The program must outlive the machine.
    explicit ScriptMachine(const ScriptProgram& program);

    ScriptMemory& memory() { return memory_; }
    const ScriptMemory& memory() const { return memory_; }
    const ScriptProgram& program() const { return program_; }

    // Creates an object of the class `class_id` (program id): a zeroed data
    // block of the size the class declares and its 12-byte handle (word 0
    // unused, the count at +4, the object at +8). The count starts at 0 and
    // counts the variables that hold the handle.
    ScriptObjectId create_object(std::uint32_t class_id);
    ScriptAddress data_of(ScriptObjectId object) const;
    ScriptAddress handle_of(ScriptObjectId object) const;
    std::uint32_t class_of(ScriptObjectId object) const;

    // A frame on the body `body` (program id) for `object`: a zeroed stack of
    // stack_size cells and zeroed locals of max(locals_size, 4) bytes, ip 0.
    ScriptFrame make_frame(ScriptObjectId object, std::uint32_t body);

    // Runs the last frame of the thread (FUN_0057de80). Returns true when the
    // frame ended and false when the thread must stop until its next run.
    bool interpret(ScriptThread& thread);

    // Strings: a record is a block {u32 count, the bytes, 0}; a string cell
    // is a 4-byte block that holds the address of a record, and a string on
    // the stack is the address of a cell. The global name table holds one
    // cell per name of the program, each with a record counted once.
    ScriptAddress name_cell(std::uint32_t index) const;
    std::string string_at(ScriptAddress cell) const;
    ScriptAddress new_string_cell(std::string_view text);

private:
    struct Object {
        std::uint32_t class_id = 0;
        ScriptAddress data = 0;
        ScriptAddress handle = 0;
    };

    const Object& object_at(ScriptObjectId object) const;
    ScriptAddress new_record(std::string_view text);
    void release_record(ScriptAddress record);

    const ScriptProgram& program_;
    ScriptMemory memory_;
    std::vector<Object> objects_;
    ScriptAddress name_table_ = 0;
};

}  // namespace bo2::engine
