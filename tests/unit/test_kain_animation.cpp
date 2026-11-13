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
    // FUN_00445ba0 replaces the node speed 1.5 by 1.15: 0.2 s of game time.
    CHECK(std::fabs(animation.machine().rate() - 1.15f) < 1.0e-6f);
    CHECK(std::fabs(animation.machine().time() - 0.2f * 1.15f) < 1.0e-3f);
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
    test_jump_and_landing();
    test_run_landing_and_fall();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "kain_animation tests: ok\n";
    return 0;
}
