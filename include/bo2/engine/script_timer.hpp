#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The timers of the scripts (docs/SCRIPTS.md, "Timers"). A timer is a
// counted block of 0x14 bytes in the machine's memory: +4 its count, +8 the
// time left (a float, 0 when stopped), +0xc its task (0 for none) and +0x10
// the handle of the object it calls back (0 for none). Its task is a block
// of 0xc bytes: +4 the timer (0 once detached) and +8 the time it restarts
// with (0: no restart).

// The value type of `timer` (NTtimer, vtable 0x878c34): init makes a new
// timer whatever the source (FUN_0079cc30), destroy releases it and keeps the
// slot (FUN_0079cc80), assign shares it, counted (FUN_0079cca0), and a field has
// no default (FUN_0079cc10).
class ScriptTimerType final : public ScriptValueType {
public:
    void init(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) override;
    void destroy(ScriptMachine& machine, ScriptAddress slot) override;
    void assign(ScriptMachine& machine, ScriptAddress slot, ScriptAddress source) override;
    void read_default(ScriptMachine& machine, ScriptAddress slot, const std::vector<std::uint8_t>& chunk) override;
};

// The task of a timer (vtable 0x87a00c): each run takes the time of the
// frame (a float in `arg`) from the timer (FUN_0079ca70); it runs while its
// timer has time left (FUN_0079c9e0) and detaches from it when it ends
// (FUN_0079ca20).
class ScriptTimerTask final : public ScriptTask {
public:
    ScriptTimerTask(const ScriptMemory& memory, ScriptAddress block) : memory_(memory), block_(block) {}
    void run(ScriptMachine& machine, std::uint32_t arg) override;
    bool running() const override;
    void finish(ScriptMachine& machine) override;
    ScriptAddress block() const { return block_; }

private:
    const ScriptMemory& memory_;
    ScriptAddress block_;
};

// FUN_0079cbb0: the time left goes down by `step`; below 0 it becomes 0 and
// the object of the timer, when it has one, gets the call of slot 15 of its
// current state with the timer (FUN_00766300; `ontimer` of creaturescript).
void tick_timer(ScriptMachine& machine, ScriptAddress timer, float step);
// FUN_0079cb90: no time left, and the timer and its task part.
void stop_timer(ScriptMachine& machine, ScriptAddress timer);

// The natives of `timer` (bound by FUN_0076c220). For each, `self` is the
// address of the slot that holds the timer.
// setcallback(basescript), body 0 (FUN_0076c2b0, FUN_0079ccd0, FUN_0079ccf0).
void native_timer_set_callback(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc, ScriptAddress args,
                               ScriptAddress result);
// starttimer(float, bool), body 1 (FUN_0076c2d0, FUN_0079cd80).
void native_timer_start(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc, ScriptAddress args,
                        ScriptAddress result);
// stop(), body 2 (FUN_0076c2f0, FUN_0079ce30).
void native_timer_stop(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc, ScriptAddress args,
                       ScriptAddress result);
// isrunning(), body 3 (FUN_0076c300, FUN_0079ce40).
void native_timer_is_running(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc, ScriptAddress args,
                             ScriptAddress result);
// getid(), body 4 (FUN_0076c320, FUN_0079cf30).
void native_timer_get_id(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc, ScriptAddress args,
                         ScriptAddress result);
// play(float, bool), body 5, a task (FUN_0076c340, FUN_0079ce70).
std::unique_ptr<ScriptTask> native_timer_play(ScriptMachine& machine, ScriptObjectId self, ScriptObjectId owner,
                                              std::uint32_t argc, ScriptAddress args);

void bind_timer_natives(ScriptMachine& machine);

}  // namespace bo2::engine
