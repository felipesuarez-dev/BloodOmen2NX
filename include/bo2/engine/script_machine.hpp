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

class ScriptMachine;

// A native function (docs/SCRIPTS.md, "Native functions"): the machine, the
// object, the argument count, the address of the cell of the first argument
// and the address of the result (0 when the method has none).
using ScriptNative = void (*)(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                              ScriptAddress args, ScriptAddress result);

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
    // frame ended or when a call pushed a new frame, and false when the thread
    // must stop until its next run.
    bool interpret(ScriptThread& thread);

    // Runs a thread (FUN_0057dcf0, without child threads): interpret until it
    // stops; a frame that ended is removed and its caller continues. Returns
    // true while the thread has frames.
    bool run_thread(ScriptThread& thread);

    // Binds the native function of the body `body_index` of the class named
    // `class_name` (FUN_00587ac0). Throws when the class or the body does not
    // exist or the body is not native.
    void bind_native(const std::string& class_name, std::uint16_t body_index, ScriptNative native);

    // Strings: a record is a block {u32 count, the bytes, 0}; a string cell
    // is a 4-byte block that holds the address of a record, and a string on
    // the stack is the address of a cell. The global name table holds one
    // cell per name of the program, each with a record counted once.
    ScriptAddress name_cell(std::uint32_t index) const;
    std::string string_at(ScriptAddress cell) const;
    ScriptAddress new_string_cell(std::string_view text);

    // The warnings the original prints to its log, for the host to show.
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    struct Object {
        std::uint32_t class_id = 0;
        ScriptAddress data = 0;
        ScriptAddress handle = 0;
    };

    const Object& object_at(ScriptObjectId object) const;
    ScriptAddress new_record(std::string_view text);
    void release_record(ScriptAddress record);

    bool is_a(std::uint32_t class_id, const std::string& name) const;
    std::uint32_t method_of_body(std::uint32_t body) const;
    std::uint32_t body_in_state(ScriptObjectId object, std::uint32_t method, bool state_zero) const;
    void init_variable(ScriptAddress address, const ScriptType& type);
    void copy_argument(ScriptAddress address, const ScriptType& type, ScriptAddress cell);
    // Pops the cells of a call and makes it; returns true when it pushed a
    // frame.
    bool call(ScriptThread& thread, std::uint32_t op, ScriptObjectId object, std::uint32_t body,
              std::uint32_t method);

    const ScriptProgram& program_;
    ScriptMemory memory_;
    std::vector<Object> objects_;
    ScriptAddress name_table_ = 0;
    std::vector<ScriptNative> natives_;  // one per body of the program
    ScriptAddress empty_record_ = 0;      // the shared empty string
    std::vector<std::string> warnings_;
    std::uint32_t loop_count_ = 0;  // DAT_00a6a57c
    int depth_ = 0;                 // DAT_00a6a578: interpreters running
};

}  // namespace bo2::engine
