#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "bo2/engine/anim_graph.hpp"
#include "bo2/engine/anim_machine.hpp"
#include "bo2/engine/big_archive.hpp"

namespace {

using bo2::engine::AnimAction;
using bo2::engine::AnimChainLink;
using bo2::engine::AnimClipNode;
using bo2::engine::AnimGraph;
using bo2::engine::AnimMachine;
using bo2::engine::AnimSequence;
using bo2::engine::AnimState;
using bo2::engine::AnimTransition;

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

bool near(float left, float right, float tolerance = 1.0e-4f) {
    return std::fabs(left - right) < tolerance;
}

std::uint32_t tag(std::size_t index) {
    return static_cast<std::uint32_t>((index << 1) | 1);
}

AnimClipNode clip(float in, float out, float speed, std::uint32_t blend_tag = 0) {
    AnimClipNode node;
    node.in_time = in;
    node.out_time = out;
    node.speed = speed;
    node.blend_tag = blend_tag;
    return node;
}

AnimTransition when(std::int32_t condition, std::uint32_t variant_tag,
                    std::vector<AnimAction> actions) {
    AnimTransition transition;
    transition.condition = condition;
    transition.variant_tag = variant_tag;
    transition.actions = std::move(actions);
    return transition;
}

AnimAction play(std::int32_t sequence, std::uint32_t blend_tag = 0) {
    AnimAction action;
    action.kind = 1;
    action.target = sequence;
    action.blend_tag = blend_tag;
    return action;
}

AnimAction go(std::int32_t state) {
    AnimAction action;
    action.kind = 2;
    action.target = state;
    return action;
}

// The shape of Kain's ground states, with made-up numbers:
//   state 0 "idle": plays sequence 0 (clip 0, 0..1 s).
//   state 1 "run": from idle it continues with state 2; otherwise it plays
//     sequence 1 (clip 1, 0..0.8 s at speed 2).
//   state 2 "ready to run": plays sequence 2 (clip 2, a held frame with a
//     0.2 s blend) unless it is the state being left, then it continues with 1.
//   state 3 "combo": plays sequence 3, two clips in a row.
AnimGraph ground_graph() {
    AnimGraph graph;
    graph.blends = {0.2f, 0.5f};
    graph.clips = {clip(0.0f, 1.0f, 1.0f), clip(0.0f, 0.8f, 2.0f), clip(0.0f, 0.0f, 1.0f, tag(0)),
                   clip(0.5f, 1.0f, 1.0f), clip(0.0f, 0.25f, 1.0f)};
    // Chain: [clip 0, end] [clip 1, end] [clip 2, end] [clip 3, clip 4 with a 0.5 s blend, end].
    graph.chain = {AnimChainLink{0, 0}, AnimChainLink{-1, 0}, AnimChainLink{1, 0},
                   AnimChainLink{-1, 0}, AnimChainLink{2, 0}, AnimChainLink{-1, 0},
                   AnimChainLink{3, 0}, AnimChainLink{4, tag(1)}, AnimChainLink{-1, 0}};
    graph.sequences = {AnimSequence{tag(0), "sq_idle"}, AnimSequence{tag(2), "sq_run"},
                       AnimSequence{tag(4), "sq_rdy2run"}, AnimSequence{tag(6), "sq_combo"}};
    // Variants: 0 is unused, 1 = state 0, 2 = state 2, 3 = an unknown state,
    // 4 and 5 = the two bindings of an AND, 6 = the binding of a NOT.
    graph.variants = {{0, 0}, {2, 0}, {2, 2}, {2, 77}, {8, static_cast<std::int32_t>(tag(0))},
                      {8, static_cast<std::int32_t>(tag(1))},
                      {8, static_cast<std::int32_t>(tag(0))}};
    // Bindings: 0 = "the state being left is state 0", 1 = "always".
    graph.bindings = {{tag(1), 1}, {0, 0}};
    AnimState idle;
    idle.name = "st_idle";
    idle.transitions = {when(0, 0, {play(0)})};
    AnimState run;
    run.name = "st_run";
    run.transitions = {when(1, tag(1), {go(2)}), when(0, 0, {play(1)})};
    AnimState ready;
    ready.name = "st_rdy2run";
    ready.transitions = {when(1, tag(2), {go(1)}), when(0, 0, {play(2)})};
    AnimState combo;
    combo.name = "st_combo";
    combo.transitions = {when(0, 0, {play(3)})};
    graph.states = {idle, run, ready, combo};
    return graph;
}

void test_start_plays_the_first_true_transition() {
    const AnimGraph graph = ground_graph();
    AnimMachine machine(graph);
    CHECK(machine.state() == -1 && machine.clip() == -1);
    machine.start_state(0);
    CHECK(machine.state() == 0 && machine.requested() == 0);
    CHECK(machine.sequence() == 0 && machine.clip() == 0);
    CHECK(machine.time() == 0.0f && !machine.blending());
    // Out of range stops the machine (FUN_00642c00 with a null state).
    machine.start_state(40);
    CHECK(machine.state() == -1 && machine.clip() == -1);
    machine.advance(1.0);  // nothing to move
    CHECK(machine.state() == -1);
}

void test_goto_sees_the_state_being_left() {
    const AnimGraph graph = ground_graph();
    AnimMachine machine(graph);
    machine.start_state(0);
    // Run from idle goes through "ready to run": the condition of that state
    // is checked against idle, not against run.
    machine.start_state(1);
    CHECK(machine.state() == 2 && machine.requested() == 1);
    CHECK(machine.clip() == 2);
    // The held frame blends in for 0.2 s: the clock starts before `in`.
    CHECK(near(machine.time(), -0.2f) && machine.blending());
    CHECK(machine.reel_time() == 0.0f);
    machine.advance(0.1);
    CHECK(machine.state() == 2 && near(machine.time(), -0.1f));
    // The blend ends, the sequence is over and the requested state restarts:
    // now the state being left is "ready to run", so run plays its clip.
    machine.advance(0.15);
    CHECK(machine.state() == 1 && machine.clip() == 1);
    // 0.05 s were left: at speed 2 the run clip is at 0.1.
    CHECK(near(machine.time(), 0.1f));
    CHECK(!machine.blending());
}

void test_clip_runs_at_its_rate_and_loops() {
    const AnimGraph graph = ground_graph();
    AnimMachine machine(graph);
    machine.start_state(0);
    machine.start_state(1);
    machine.advance(0.2);  // the blend
    machine.advance(0.1);
    CHECK(machine.state() == 1 && near(machine.time(), 0.2f));
    const std::uint32_t starts = machine.starts();
    // 0.8 s of clip at speed 2 last 0.4 s; after 0.45 s the clip has wrapped.
    machine.advance(0.35);
    CHECK(machine.state() == 1 && near(machine.time(), 0.1f));
    CHECK(machine.starts() == starts + 1);
    // FUN_00723620: the run state replaces the rate of the clip in play.
    machine.set_rate(1.0f);
    machine.advance(0.1);
    CHECK(near(machine.time(), 0.2f) && machine.rate() == 1.0f);
    // The next clip starts at its node speed again.
    machine.advance(0.7);
    CHECK(machine.rate() == 2.0f);
}

void test_sequence_chain_and_finish() {
    const AnimGraph graph = ground_graph();
    AnimMachine machine(graph);
    machine.start_state(3);
    CHECK(machine.clip() == 3 && machine.time() == 0.5f);
    CHECK(!machine.finished());
    // The first clip ends after 0.5 s; the second starts with the link's blend.
    CHECK(!machine.advance(0.6));  // the chain goes on
    CHECK(machine.clip() == 4 && machine.blending());
    CHECK(near(machine.time(), -0.4f));
    machine.advance(0.4);
    CHECK(near(machine.time(), 0.0f) && !machine.blending());
    CHECK(machine.advance(0.25));
    CHECK(machine.finished() && machine.clip() == 4 && near(machine.time(), 0.25f));
    // The next advance restarts the requested state from its first clip.
    machine.advance(0.1);
    CHECK(machine.clip() == 3 && near(machine.time(), 0.6f) && !machine.finished());
}

void test_conditions() {
    AnimGraph graph = ground_graph();
    AnimState probe;
    probe.name = "st_probe";
    // AND(previous is state 0, always), then NOT(previous is state 0), then an
    // undecoded game condition, which is false, then the fallback.
    probe.transitions = {when(0x402, tag(4), {play(0)}), when(0x400, tag(6), {play(1)}),
                         when(0x9, 0, {play(2)}), when(0, 0, {play(3)})};
    graph.states.push_back(probe);
    AnimMachine machine(graph);
    machine.start_state(0);
    machine.start_state(4);
    CHECK(machine.sequence() == 0);  // the AND holds after state 0
    machine.start_state(4);
    CHECK(machine.sequence() == 1);  // the state being left is the probe: NOT holds
    // An OR of the same two operands holds as well; a "previous state" operand
    // outside the table means "no state" and fails while a state is set.
    graph.states[4].transitions = {when(1, tag(3), {play(2)}), when(0x401, tag(4), {play(3)})};
    AnimMachine second(graph);
    second.start_state(0);
    second.start_state(4);
    CHECK(second.sequence() == 3);
    // Condition 7: the clip is at its end.
    graph.states[4].transitions = {when(7, 0, {play(1)}), when(0, 0, {play(0)})};
    AnimMachine third(graph);
    third.start_state(4);
    CHECK(third.sequence() == 0);
    third.advance(1.0);
    CHECK(third.finished());
    third.start_state(4);
    CHECK(third.sequence() == 1);
    // A state whose "continue" names itself stops there.
    graph.states[4].transitions = {when(0, 0, {go(4), play(0)})};
    AnimMachine fourth(graph);
    fourth.start_state(4);
    CHECK(fourth.state() == 4 && fourth.clip() == -1);
}

// Asset-gated: Kain's graph. The numbers are the ones the Wine oracle shows.
void test_kain_states() {
    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "kain machine check skipped (BO2NX_ASSETS_DIR is unset)\n";
        return;
    }
    const std::filesystem::path path =
        std::filesystem::path(root) / "media/gogames/bo2/resources/levels/kain/kain.big";
    if (!std::filesystem::exists(path)) {
        std::cout << "kain machine check skipped (kain.big not found)\n";
        return;
    }
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path.string());
    const bo2::engine::BigEntry* entry = archive.find("animatio", "kain");
    CHECK(entry != nullptr);
    if (entry == nullptr) {
        return;
    }
    const AnimGraph graph = bo2::engine::parse_anim_graph(archive.extract(*entry));
    CHECK(graph.states.size() == 188);
    CHECK(graph.sequences.size() == 208);
    // The indexes the game passes to StartState.
    CHECK(graph.find_state("st_ready") == 0 && graph.find_state("st_run") == 1);
    CHECK(graph.find_state("st_back") == 59 && graph.find_state("st_startjump") == 76);
    CHECK(graph.find_state("st_fallingup") == 78 && graph.find_state("st_fallingdn") == 79);
    CHECK(graph.find_state("st_normal_landing") == 86 && graph.find_state("st_run_landing") == 87);
    const auto reel = [&](const AnimMachine& machine) {
        return bo2::engine::anim_clip_reel_name(
            graph, graph.clips[static_cast<std::size_t>(machine.clip())], archive, *entry);
    };

    AnimMachine machine(graph);
    machine.start_state(0);
    CHECK(machine.state() == 0 && reel(machine) == "kain/normal_stance");
    // W: st_run from st_ready holds frame 0 of the run reel for a 0.2 s blend,
    // then the run clip loops.
    machine.start_state(1);
    CHECK(graph.states[static_cast<std::size_t>(machine.state())].name == "st_rdy2run");
    CHECK(reel(machine) == "kain/run" && near(machine.time(), -0.2f));
    machine.advance(0.18);
    CHECK(machine.state() == 4);
    machine.advance(0.04);
    CHECK(machine.state() == 1 && reel(machine) == "kain/run" && near(machine.rate(), 1.5f));
    // FUN_00445ba0 plays it at the run rate; one cycle is 0.8 / 1.15 s.
    machine.set_rate(1.15f);
    const float phase = machine.time();
    machine.advance(0.4);
    CHECK(near(machine.time(), phase + 0.46f, 1.0e-3f));
    // Releasing W: st_ready from st_run goes through st_run2rdy.
    machine.start_state(0);
    CHECK(graph.states[static_cast<std::size_t>(machine.state())].name == "st_run2rdy");
    CHECK(reel(machine) == "kain/normal_stance");
    // S.
    machine.start_state(59);
    CHECK(machine.state() == 59 && reel(machine) == "kain/walk_backwards");
    // A jump: three held frames, then the landing clip. The oracle holds
    // st_normal_landing for 25 to 27 samples of 20 ms; the clip lasts 0.5333 s.
    machine.start_state(76);
    CHECK(reel(machine) == "kain/takeoff_p");
    machine.start_state(78);
    CHECK(reel(machine) == "kain/jump_up_p");
    machine.start_state(79);
    CHECK(reel(machine) == "kain/fallingdown_p");
    machine.start_state(86);
    CHECK(reel(machine) == "kain/normal_landing" && !machine.finished());
    CHECK(!machine.advance(0.52));
    CHECK(machine.advance(0.02));  // the sequence ended inside this step
    machine.start_state(87);
    CHECK(reel(machine) == "kain/run_landing");
    CHECK(!machine.advance(0.30));
    CHECK(machine.advance(0.02));
}

}  // namespace

int main() {
    try {
        test_start_plays_the_first_true_transition();
        test_goto_sees_the_state_being_left();
        test_clip_runs_at_its_rate_and_loops();
        test_sequence_chain_and_finish();
        test_conditions();
        test_kain_states();
    } catch (const std::exception& error) {
        std::cerr << "anim machine test: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "anim machine tests: ok\n";
    return 0;
}
