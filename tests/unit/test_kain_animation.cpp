#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "bo2/engine/anim_graph.hpp"
#include "bo2/game/kain_animation.hpp"

namespace {

using bo2::game::KainAnimation;
using bo2::game::PlayerInput;
using bo2::game::PlayerState;
namespace anim = bo2::game::kain_anim;

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

// A graph with the state indexes Kain's movement uses: every state plays its
// own one-clip sequence. The run clip is 0.8 s at speed 1.5 and the two
// landing clips are 0.5 s and 0.3 s.
bo2::engine::AnimGraph movement_graph() {
    bo2::engine::AnimGraph graph;
    const std::size_t states = 88;
    for (std::size_t index = 0; index < states; ++index) {
        bo2::engine::AnimClipNode clip;
        clip.in_time = 0.0f;
        clip.out_time = index == 1 ? 0.8f : (index == 86 ? 0.5f : (index == 87 ? 0.3f : 1.0f));
        clip.speed = index == 1 ? 1.5f : 1.0f;
        graph.clips.push_back(clip);
        graph.chain.push_back({static_cast<std::int32_t>(index), 0});
        graph.chain.push_back({-1, 0});
        graph.sequences.push_back({static_cast<std::uint32_t>(((index * 2) << 1) | 1), "sq"});
        bo2::engine::AnimAction play;
        play.kind = 1;
        play.target = static_cast<std::int32_t>(index);
        bo2::engine::AnimTransition always;
        always.actions = {play};
        bo2::engine::AnimState state;
        state.transitions = {always};
        graph.states.push_back(state);
    }
    return graph;
}

bo2::game::KainTuning tuning() {
    bo2::game::KainTuning value;
    value.stick_deadzone = 0.1f;
    value.run_clip_rate = 1.15f;
    return value;
}

PlayerInput stick(float move_y, float move_x = 0.0f) {
    PlayerInput input;
    input.move_y = move_y;
    input.move_x = move_x;
    return input;
}

PlayerState on_ground() {
    PlayerState state;
    state.grounded = true;
    return state;
}

PlayerState in_air(float vertical_velocity) {
    PlayerState state;
    state.grounded = false;
    state.vertical_velocity = vertical_velocity;
    return state;
}

constexpr double kTick = 0.02;

void test_ground_states() {
    const bo2::engine::AnimGraph graph = movement_graph();
    KainAnimation animation(graph, tuning());
    CHECK(animation.state() == -1);
    animation.step(stick(0.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kReady);
    // The deadzone: a stick inside it is no movement.
    animation.step(stick(0.05f), on_ground(), kTick);
    CHECK(animation.state() == anim::kReady);
    animation.step(stick(1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kRun);
    animation.step(stick(-1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kBack);
    // A turn alone keeps st_ready, as the oracle shows.
    animation.step(stick(0.0f, 1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kReady);
}

void test_run_plays_at_the_run_rate() {
    const bo2::engine::AnimGraph graph = movement_graph();
    KainAnimation animation(graph, tuning());
    animation.step(stick(1.0f), on_ground(), kTick);
    CHECK(animation.machine().state() == anim::kRun);
    for (int tick = 0; tick < 10; ++tick) {
        animation.step(stick(1.0f), on_ground(), kTick);
    }
    // FUN_00445ba0 replaces the node speed 1.5 by 1.15. The state starts and
    // moves in the same tick: eleven ticks, 0.22 s of game time.
    CHECK(std::fabs(animation.machine().rate() - 1.15f) < 1.0e-6f);
    CHECK(std::fabs(animation.machine().time() - 0.22f * 1.15f) < 1.0e-3f);
    CHECK(animation.run_weight() == 1.0f);
    // Half stick: the weight of the run is the stick magnitude and the run
    // layer plays at (0.5 s + 0.5) of the run rate, the walk layer at (s + 1).
    animation.step(stick(0.5f), on_ground(), kTick);
    CHECK(std::fabs(animation.run_weight() - 0.5f) < 1.0e-6f);
    CHECK(std::fabs(animation.machine().rate() - 0.75f * 1.15f) < 1.0e-6f);
    CHECK(std::fabs(animation.layers().layer(1).rate() - 1.5f * 1.15f) < 1.0e-6f);
    // A diagonal stick is clamped to 1. This graph has no layer states, so the
    // cross blend is off.
    animation.step(stick(1.0f, 1.0f), on_ground(), kTick);
    CHECK(animation.run_weight() == 1.0f && !animation.cross_blend());
}

void test_ground_advance() {
    // The run clip moves the root 200 u per second of reel time and the walk
    // back clip -62.5. The walk back starts with a 0.1 s blend whose transform
    // is 5 u backwards.
    bo2::engine::AnimGraph graph = movement_graph();
    graph.blends = {0.1f};
    graph.blend_transforms = {1};
    graph.transforms = {bo2::engine::AnimTransform{
        {-5.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};
    graph.states[59].transitions[0].actions[0].blend_tag = 1;
    bo2::game::KainTuning value = tuning();
    value.clip_forward_speed.assign(graph.clips.size(), 0.0f);
    value.clip_forward_speed[1] = 200.0f;
    value.clip_forward_speed[59] = -62.5f;
    KainAnimation animation(graph, value);

    // W: the run starts and moves in the same tick, at the run rate.
    animation.begin(stick(1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kRun);
    CHECK(std::fabs(animation.ground_motion().forward - 200.0f * 1.15f * 0.02f) < 1.0e-4f);
    animation.end(stick(1.0f), on_ground(), kTick);
    // Nothing held: the idle clip does not move him.
    animation.begin(stick(0.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kReady && animation.ground_motion().forward == 0.0f);
    animation.end(stick(0.0f), on_ground(), kTick);
    // S: the blend gives its transform over its 0.1 s (FUN_005ceef0)...
    animation.begin(stick(-1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kBack);
    CHECK(std::fabs(animation.ground_motion().forward + 1.0f) < 1.0e-4f);
    animation.end(stick(-1.0f), on_ground(), kTick);
    // ...and then the clip moves him by the root of its reel.
    for (int tick = 0; tick < 7; ++tick) {
        animation.begin(stick(-1.0f), on_ground(), kTick);
        animation.end(stick(-1.0f), on_ground(), kTick);
    }
    CHECK(std::fabs(animation.ground_motion().forward + 62.5f * 0.02f) < 1.0e-4f);
    // In the air `begin` asks for no ground state: `end` owns the jump.
    animation.end(stick(-1.0f), in_air(200.0f), kTick);
    CHECK(animation.state() == anim::kStartJump);
    animation.begin(stick(1.0f), in_air(200.0f), kTick);
    CHECK(animation.state() == anim::kStartJump);
}

void test_combat_mode() {
    // The combat states play 1 s clips; the walk moves the root 200 u per
    // second and the left strafe clip names a transform of 96 u to the left.
    bo2::engine::AnimGraph graph = movement_graph();
    graph.transforms = {bo2::engine::AnimTransform{
        {0.0f, 96.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};
    graph.clips[72].transform_tag = 1;
    bo2::game::KainTuning value = tuning();
    value.combat_hold_exit_delay = 0.15f;
    value.clip_forward_speed.assign(graph.clips.size(), 0.0f);
    value.clip_forward_speed[14] = 200.0f;
    KainAnimation animation(graph, value);
    const auto combat = [](float move_y, float move_x = 0.0f) {
        PlayerInput input = stick(move_y, move_x);
        input.combat = true;
        return input;
    };
    animation.step(stick(0.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kReady && !animation.combat_mode());

    // The control held: combat mode, and the combat movement takes over.
    animation.step(combat(0.0f), on_ground(), kTick);
    CHECK(animation.combat_mode() && animation.state() == anim::kReadyCombat);
    CHECK(!animation.ground_motion().turn);
    // FUN_00425580: forward, back, and a side when |x| is over twice |y|.
    animation.step(combat(1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kWalkCombat);
    CHECK(std::fabs(animation.ground_motion().forward - 200.0f * 0.02f) < 1.0e-4f);
    animation.step(combat(-1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kBackCombat);
    animation.step(combat(0.4f, -1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kStrafeLeftCombat);
    // The strafe clip gives the part of its transform that matches its time.
    CHECK(std::fabs(animation.ground_motion().lateral - 96.0f * 0.02f) < 1.0e-4f);
    CHECK(animation.ground_motion().forward == 0.0f);
    animation.step(combat(0.4f, 1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kStrafeRightCombat);
    animation.step(combat(0.6f, 1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kWalkCombat);  // |x| is not over twice |y|
    animation.step(combat(0.05f, 0.05f), on_ground(), kTick);
    CHECK(animation.state() == anim::kReadyCombat);  // inside the deadzone

    // FUN_0042dca0: the mode outlasts the control by the hold exit delay. The
    // first tick without it arms the timer, then 0.15 s run down.
    animation.step(stick(1.0f), on_ground(), kTick);
    CHECK(animation.combat_mode() && animation.state() == anim::kWalkCombat);
    int ticks = 1;
    while (animation.combat_mode() && ticks < 40) {
        animation.step(stick(1.0f), on_ground(), kTick);
        ++ticks;
    }
    CHECK(ticks == 9);  // the arming tick and eight of 20 ms
    // Out of combat mode the ready behaviour is back: W runs and turns again.
    CHECK(animation.state() == anim::kRun);
    CHECK(animation.ground_motion().turn);
    // Holding the control again before the exit keeps the mode.
    animation.step(combat(0.0f), on_ground(), kTick);
    animation.step(stick(0.0f), on_ground(), kTick);
    animation.step(combat(0.0f), on_ground(), kTick);
    for (int tick = 0; tick < 6; ++tick) {
        animation.step(stick(0.0f), on_ground(), kTick);
    }
    CHECK(animation.combat_mode());
}

void test_jump_and_landing() {
    const bo2::engine::AnimGraph graph = movement_graph();
    KainAnimation animation(graph, tuning());
    animation.step(stick(0.0f), on_ground(), kTick);
    // Takeoff: st_startjump for the 0.2 s timer of FUN_004436e0.
    animation.step(stick(0.0f), in_air(500.0f), kTick);
    CHECK(animation.state() == anim::kStartJump);
    for (int tick = 0; tick < 9; ++tick) {
        animation.step(stick(0.0f), in_air(400.0f), kTick);
    }
    CHECK(animation.state() == anim::kStartJump);
    // The timer is a float counted down by the tick: allow it one more tick.
    animation.step(stick(0.0f), in_air(300.0f), kTick);
    animation.step(stick(0.0f), in_air(300.0f), kTick);
    CHECK(animation.state() == anim::kFallingUp);
    // The sign of the vertical velocity picks the state (FUN_00442ae0).
    animation.step(stick(0.0f), in_air(0.0f), kTick);
    CHECK(animation.state() == anim::kFallingUp);
    animation.step(stick(0.0f), in_air(-10.0f), kTick);
    CHECK(animation.state() == anim::kFallingDown);
    // Touchdown without forward: the normal landing, until its 0.5 s clip ends.
    animation.step(stick(0.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kNormalLanding);
    for (int tick = 0; tick < 23; ++tick) {
        animation.step(stick(1.0f), on_ground(), kTick);
    }
    CHECK(animation.state() == anim::kNormalLanding);  // input waits for the clip
    for (int tick = 0; tick < 3; ++tick) {
        animation.step(stick(1.0f), on_ground(), kTick);
    }
    CHECK(animation.state() == anim::kRun);
}

void test_takeoff_ends_with_the_boost_window() {
    const bo2::engine::AnimGraph graph = movement_graph();
    bo2::game::KainTuning jumping = tuning();
    jumping.jump.gravity_up = -2500.0f;
    jumping.jump.gravity_down = -700.0f;
    jumping.jump.height_tap = 64.0f;
    jumping.jump.height_hold = 128.0f;
    jumping.jump.boost_start = 0.08f;
    jumping.jump.boost_end = 0.4f;
    const bo2::game::JumpDerived window = bo2::game::derive_jump(jumping.jump);
    CHECK(window.boost_start > 0.02f && window.boost_start < 0.2f);
    PlayerInput held = stick(0.0f);
    held.jump = true;
    const auto takeoff_ticks = [&](bool release) {
        KainAnimation animation(graph, jumping);
        animation.step(stick(0.0f), on_ground(), kTick);
        animation.step(held, in_air(500.0f), kTick);
        int ticks = 1;
        while (animation.state() == anim::kStartJump && ticks < 40) {
            animation.step(release ? stick(0.0f) : held, in_air(400.0f), kTick);
            ++ticks;
        }
        CHECK(animation.state() == anim::kFallingUp);
        return ticks;
    };
    // A tap: the button is up when the boost would start, so the state ends
    // there (FUN_00443a50). The oracle holds it for 3 to 5 samples.
    const int tap = takeoff_ticks(true);
    CHECK(std::fabs(static_cast<float>(tap) * 0.02f - window.boost_start) < 0.05f);
    // A held jump: the boost lasts past the 0.2 s timer, which ends the state.
    const int hold = takeoff_ticks(false);
    CHECK(hold >= 10 && hold <= 12);
    CHECK(tap < hold);
}

void test_run_landing_and_fall() {
    const bo2::engine::AnimGraph graph = movement_graph();
    KainAnimation animation(graph, tuning());
    animation.step(stick(1.0f), on_ground(), kTick);
    // Walking off a ledge is a fall: no takeoff state.
    animation.step(stick(1.0f), in_air(0.0f), kTick);
    CHECK(animation.state() == anim::kFallingUp);
    animation.step(stick(1.0f), in_air(-50.0f), kTick);
    CHECK(animation.state() == anim::kFallingDown);
    // Forward held at touchdown: the run landing (0.3 s), then the run again.
    animation.step(stick(1.0f), on_ground(), kTick);
    CHECK(animation.state() == anim::kRunLanding);
    for (int tick = 0; tick < 13; ++tick) {
        animation.step(stick(1.0f), on_ground(), kTick);
    }
    CHECK(animation.state() == anim::kRunLanding);
    for (int tick = 0; tick < 3; ++tick) {
        animation.step(stick(1.0f), on_ground(), kTick);
    }
    CHECK(animation.state() == anim::kRun);
    // A jump that turns down before the takeoff timer ends leaves it at once.
    animation.step(stick(0.0f), on_ground(), kTick);
    animation.step(stick(0.0f), in_air(50.0f), kTick);
    CHECK(animation.state() == anim::kStartJump);
    animation.step(stick(0.0f), in_air(-5.0f), kTick);
    CHECK(animation.state() == anim::kFallingDown);
}

}  // namespace

int main() {
    test_ground_states();
    test_run_plays_at_the_run_rate();
    test_ground_advance();
    test_combat_mode();
    test_jump_and_landing();
    test_takeoff_ends_with_the_boost_window();
    test_run_landing_and_fall();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "kain_animation tests: ok\n";
    return 0;
}
