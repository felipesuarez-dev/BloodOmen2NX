#pragma once

#include <cstdint>
#include <memory>
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

class ScriptMachine;

// What a frame can wait for: a child thread, or the task a native function
// returns. The machine runs it once per run of the frame's thread and
// destroys it when it stops running.
class ScriptTask {
public:
    virtual ~ScriptTask() = default;
    // Runs once; `arg` is what the runner of the thread passes (0 when the
    // task has just started).
    virtual void run(ScriptMachine& machine, std::uint32_t arg) = 0;
    virtual bool running() const = 0;
    // Gives back what the task holds in the machine; called before it is
    // deleted.
    virtual void finish(ScriptMachine& machine) { static_cast<void>(machine); }
};

// A frame of a thread (FUN_0057d3e0): the object, the body (program id), the
// next code word, the top of its stack (-1 when empty), its stack of 4-byte
// cells and its locals, whether it waits for its children, and its child
// threads and tasks. A copy (FUN_0057d5c0) is `borrowed`: it shares the stack
// and the locals of the frame it copies and gives back neither.
struct ScriptFrame {
    ScriptObjectId object = 0;
    std::uint32_t body = 0;
    std::uint32_t ip = 0;
    std::int32_t top = -1;
    ScriptAddress stack = 0;
    ScriptAddress locals = 0;
    bool waiting = false;
    bool borrowed = false;
    std::vector<std::unique_ptr<ScriptTask>> children;
};

// A thread (vtable 0x86def0): its last frame is the one that runs; `owner`
// is the object it belongs to, which its child threads inherit.
class ScriptThread final : public ScriptTask {
public:
    ScriptObjectId owner = 0;
    std::vector<ScriptFrame> frames;

    void run(ScriptMachine& machine, std::uint32_t arg) override;  // FUN_0057dcf0
    bool running() const override { return !frames.empty(); }      // FUN_0057da20
    void finish(ScriptMachine& machine) override;                   // FUN_0057dc80
};

// A native function (docs/SCRIPTS.md, "Native functions"): the machine, the
// object, the argument count, the address of the cell of the first argument
// and the address of the result (0 when the method has none).
using ScriptNative = void (*)(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                              ScriptAddress args, ScriptAddress result);

// A native function that returns a task (FUN_0057f6d0, instruction 0x4d):
// the machine, the object, the owner of the calling thread, the argument
// count and the address of the cell of the first argument. Null: no task.
using ScriptTaskNative = std::unique_ptr<ScriptTask> (*)(ScriptMachine& machine, ScriptObjectId self,
                                                         ScriptObjectId owner, std::uint32_t argc,
                                                         ScriptAddress args);

class ScriptMachine {
public:
    // The program must outlive the machine.
    explicit ScriptMachine(const ScriptProgram& program);

    ScriptMemory& memory() { return memory_; }
    const ScriptMemory& memory() const { return memory_; }
    const ScriptProgram& program() const { return program_; }

    // Creates an object of the class `class_id` (program id): a zeroed data
    // block of the size the class declares and its 12-byte handle (word 0
    // unused, the count at +4, the object at +8). The count starts at 1, the
    // object's own (FUN_0057f830), and counts the variables that hold the
    // handle.
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

    // Runs a thread once (FUN_0057dcf0): first the children of its last
    // frame, each once with `arg`, destroying those that stop; a waiting
    // frame with children left stops there. Then interpret until it stops;
    // a frame that ended is removed and its caller continues. Returns true
    // while the thread has frames.
    bool run_thread(ScriptThread& thread, std::uint32_t arg = 0);

    // Ends every frame of a thread, the last one first (FUN_0057dc80).
    void end_thread(ScriptThread& thread);

    // Binds the native function of the body `body_index` of the class named
    // `class_name` (FUN_00587ac0). Throws when the class or the body does not
    // exist or the body is not native.
    void bind_native(const std::string& class_name, std::uint16_t body_index, ScriptNative native);
    // The same for a native function that returns a task (FUN_00587b00).
    void bind_task_native(const std::string& class_name, std::uint16_t body_index, ScriptTaskNative native);

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
    // Stores `value` into a variable (FUN_00587fc0): a string is the address
    // of a cell, an object is a handle, an array is the address of its first
    // element (0: initial values).
    void store_value(ScriptAddress address, const ScriptType& type, std::uint32_t value);
    // A parameter of a new frame from its argument cell (FUN_0057d3e0): an
    // object argument is stored as the object's handle.
    void copy_argument(ScriptAddress address, const ScriptType& type, ScriptAddress cell);
    void destroy_variable(ScriptAddress address, const ScriptType& type);
    void release_handle(ScriptAddress handle);
    // The end of a frame (FUN_0057d690): its variables, stack and locals, then
    // its children.
    void end_frame(ScriptFrame& frame);
    void destroy_task(std::unique_ptr<ScriptTask>& task);
    std::uint32_t native_body(const std::string& class_name, std::uint16_t body_index) const;
    // Removes the cells of a call or a thread start that finds no object or
    // no body: the arguments and, when the method has one, the result.
    void drop_call(ScriptFrame& frame, std::uint32_t method);
    // The body (id + 1, 0 for none) of `method` in the state tables of the
    // class `tables`, for the state of `object` (instructions 0x49, 0x4f).
    std::uint32_t body_in_tables(ScriptObjectId object, std::uint32_t tables, std::uint32_t method) const;
    // Starts `body` on `object` as a child of the last frame and runs it once
    // (FUN_0057f6d0).
    void spawn(ScriptThread& thread, ScriptObjectId object, std::uint32_t body);
    // A child thread on a copy of the last frame, run once (0x59, 0x5a).
    void fork(ScriptThread& thread);
    // Pops the cells of a call and makes it; returns true when it pushed a
    // frame.
    bool call(ScriptThread& thread, std::uint32_t op, ScriptObjectId object, std::uint32_t body,
              std::uint32_t method);

    const ScriptProgram& program_;
    ScriptMemory memory_;
    std::vector<Object> objects_;
    ScriptAddress name_table_ = 0;
    std::vector<ScriptNative> natives_;           // one per body of the program
    std::vector<ScriptTaskNative> task_natives_;  // one per body of the program
    ScriptAddress empty_record_ = 0;      // the shared empty string
    std::vector<std::string> warnings_;
    std::uint32_t loop_count_ = 0;  // DAT_00a6a57c
    int depth_ = 0;                 // DAT_00a6a578: interpreters running
};

}  // namespace bo2::engine
