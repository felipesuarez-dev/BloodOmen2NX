#include "bo2/engine/script_timer.hpp"

#include <cstring>

namespace bo2::engine {

namespace {

constexpr std::uint32_t kTimerSize = 0x14;
constexpr std::uint32_t kTaskSize = 0xc;
constexpr std::uint32_t kCallbackSlot = 15;  // FUN_00766300

float float_at(const ScriptMemory& memory, ScriptAddress address) {
    const std::uint32_t bits = memory.u32(address);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void set_float(ScriptMemory& memory, ScriptAddress address, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    memory.set_u32(address, bits);
}

ScriptAddress new_block(ScriptMemory& memory, std::uint32_t size) {
    const ScriptAddress block = memory.allocate(size);
    for (std::uint32_t offset = 0; offset < size; offset += 4) {
        memory.set_u32(block + offset, 0);
    }
    return block;
}

// FUN_0079cae0 with FUN_0079cb10: the task parts from the timer and the
// object it calls back is released.
void release_timer(ScriptMachine& machine, ScriptAddress timer) {
    ScriptMemory& memory = machine.memory();
    const std::uint32_t count = memory.u32(timer + 4) - 1;
    memory.set_u32(timer + 4, count);
    if (count != 0) {
        return;
    }
    const ScriptAddress task = memory.u32(timer + 0xc);
    if (task != 0) {
        memory.set_u32(task + 4, 0);
    }
    machine.store_handle(timer + 0x10, 0);
    memory.release(timer);
}

// FUN_0079ca40: the task takes the timer and the restart time (0 when it
// does not repeat), and the timer the time.
void set_task(ScriptMemory& memory, ScriptAddress task, ScriptAddress timer, float time, bool repeat) {
    memory.set_u32(task + 4, timer);
    set_float(memory, task + 8, repeat ? time : 0.0f);
    if (timer != 0) {
        set_float(memory, timer + 8, time);
    }
}

// The arguments time and repeat of starttimer and play: a float, then a
// boolean in the low byte of the next cell.
float time_argument(const ScriptMemory& memory, ScriptAddress args) { return float_at(memory, args); }
bool repeat_argument(const ScriptMemory& memory, ScriptAddress args) { return memory.u8(args + 4) != 0; }

}  // namespace

// FUN_0079cc30.
void ScriptTimerType::init(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    static_cast<void>(source);
    ScriptMemory& memory = machine.memory();
    const ScriptAddress timer = new_block(memory, kTimerSize);
    memory.set_u32(timer + 4, 1);
    memory.set_u32(slot, timer);
}

// FUN_0079cc80.
void ScriptTimerType::destroy(ScriptMachine& machine, ScriptAddress slot) {
    const ScriptAddress timer = machine.memory().u32(slot);
    if (timer != 0) {
        release_timer(machine, timer);
    }
}

// FUN_0079cca0.
void ScriptTimerType::assign(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress old = memory.u32(slot);
    if (old == source) {
        return;
    }
    if (old != 0) {
        release_timer(machine, old);
    }
    memory.set_u32(slot, source);
    if (source != 0) {
        memory.set_u32(source + 4, memory.u32(source + 4) + 1);
    }
}

// FUN_0079cc10 reads no default: the field starts as a new timer.
void ScriptTimerType::read_default(ScriptMachine& machine, ScriptAddress slot, const std::vector<std::uint8_t>& chunk) {
    static_cast<void>(chunk);
    init(machine, slot, 0);
}

void tick_timer(ScriptMachine& machine, ScriptAddress timer, float step) {
    ScriptMemory& memory = machine.memory();
    const float left = float_at(memory, timer + 8) - step;
    set_float(memory, timer + 8, left);
    if (!(left < 0.0f)) {
        return;
    }
    memory.set_u32(timer + 8, 0);
    const ScriptAddress handle = memory.u32(timer + 0x10);
    const ScriptObjectId object = handle != 0 ? memory.u32(handle + 8) : 0;
    if (object == 0) {
        return;
    }
    const std::uint32_t body = machine.body_in_slot(object, kCallbackSlot);
    if (body == 0) {
        return;  // FUN_00580a40 does nothing for no body
    }
    const ScriptAddress argument = memory.allocate(4);
    memory.set_u32(argument, timer);
    machine.call_body(object, body - 1, 1, argument, 0);
    memory.release(argument);
}

void stop_timer(ScriptMachine& machine, ScriptAddress timer) {
    ScriptMemory& memory = machine.memory();
    memory.set_u32(timer + 8, 0);
    const ScriptAddress task = memory.u32(timer + 0xc);
    if (task != 0) {
        memory.set_u32(task + 4, 0);
        memory.set_u32(timer + 0xc, 0);
    }
}

// FUN_0079ca70: after the step, a timer with no time left starts again
// with the restart time, or stops.
void ScriptTimerTask::run(ScriptMachine& machine, std::uint32_t arg) {
    ScriptMemory& memory = machine.memory();
    if (memory.u32(block_ + 4) == 0) {
        return;
    }
    float step = 0.0f;
    std::memcpy(&step, &arg, sizeof step);
    tick_timer(machine, memory.u32(block_ + 4), step);
    const ScriptAddress timer = memory.u32(block_ + 4);
    if (timer == 0 || float_at(memory, timer + 8) != 0.0f) {
        return;
    }
    const float restart = float_at(memory, block_ + 8);
    if (0.0f < restart) {
        set_float(memory, timer + 8, restart);
        return;
    }
    stop_timer(machine, timer);
}

// FUN_0079c9e0.
bool ScriptTimerTask::running() const {
    const ScriptAddress timer = memory_.u32(block_ + 4);
    return timer != 0 && float_at(memory_, timer + 8) != 0.0f;
}

// FUN_0079ca00 with FUN_0079ca20.
void ScriptTimerTask::finish(ScriptMachine& machine) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress timer = memory.u32(block_ + 4);
    if (timer != 0) {
        memory.set_u32(timer + 0xc, 0);
    }
    memory.release(block_);
}

// FUN_0079ccd0: with a timer and an object, the timer keeps the object's
// handle, counted (FUN_0079ccf0).
void native_timer_set_callback(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                               ScriptAddress) {
    const ScriptAddress timer = machine.memory().u32(self);
    const ScriptObjectId object = machine.memory().u32(args);
    if (timer != 0 && object != 0) {
        machine.store_handle(timer + 0x10, machine.handle_of(object));
    }
}

// FUN_0079cd80: a timer with no task gets one (FUN_0079c970), which the
// script manager runs once per frame (FUN_00583200); a timer with a task
// sets it again.
void native_timer_start(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                        ScriptAddress) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress timer = memory.u32(self);
    if (timer == 0) {
        return;
    }
    const float time = time_argument(memory, args);
    const bool repeat = repeat_argument(memory, args);
    ScriptAddress task = memory.u32(timer + 0xc);
    if (task != 0) {
        set_task(memory, task, timer, time, repeat);
        return;
    }
    task = new_block(memory, kTaskSize);
    set_task(memory, task, timer, time, repeat);
    memory.set_u32(timer + 0xc, task);
    machine.add_update_task(task, std::make_unique<ScriptTimerTask>(memory, task));
}

void native_timer_stop(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress, ScriptAddress) {
    const ScriptAddress timer = machine.memory().u32(self);
    if (timer != 0) {
        stop_timer(machine, timer);
    }
}

// FUN_0079ce40 writes the low byte of the result.
void native_timer_is_running(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                             ScriptAddress result) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress timer = memory.u32(self);
    memory.set_u8(result, timer != 0 && float_at(memory, timer + 8) != 0.0f ? 1 : 0);
}

// FUN_0079cf30: the address of the timer; nothing without one.
void native_timer_get_id(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                         ScriptAddress result) {
    const ScriptAddress timer = machine.memory().u32(self);
    if (timer != 0) {
        machine.memory().set_u32(result, timer);
    }
}

// FUN_0079ce70: the task is not added to the script manager; the frame
// that called play runs it as a child. A timer that has a task already
// gives that task, which then has two owners in the original.
std::unique_ptr<ScriptTask> native_timer_play(ScriptMachine& machine, ScriptObjectId self, ScriptObjectId,
                                              std::uint32_t, ScriptAddress args) {
    ScriptMemory& memory = machine.memory();
    const ScriptAddress timer = memory.u32(self);
    if (timer == 0) {
        return nullptr;
    }
    if (memory.u32(timer + 0xc) != 0) {
        throw ScriptError("timer play on a timer that has a task is not ported");
    }
    const ScriptAddress task = new_block(memory, kTaskSize);
    set_task(memory, task, timer, time_argument(memory, args), repeat_argument(memory, args));
    memory.set_u32(timer + 0xc, task);
    return std::make_unique<ScriptTimerTask>(memory, task);
}

void bind_timer_natives(ScriptMachine& machine) {
    machine.bind_native("timer", 0, native_timer_set_callback);
    machine.bind_native("timer", 1, native_timer_start);
    machine.bind_native("timer", 2, native_timer_stop);
    machine.bind_native("timer", 3, native_timer_is_running);
    machine.bind_native("timer", 4, native_timer_get_id);
    machine.bind_task_native("timer", 5, native_timer_play);
}

}  // namespace bo2::engine
