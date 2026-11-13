#pragma once

#include <cstdint>

#include "bo2/engine/anim_graph.hpp"
#include "bo2/engine/anim_machine.hpp"
#include "bo2/game/kain_tuning.hpp"
#include "bo2/game/player.hpp"

namespace bo2::game {

// The states of Kain's `animatio` graph that his movement states start. Each
// state stores its index at KainState+0x458 and passes it to the animation
// instance (`StartState`); the Wine oracle samples that field as `anim`.
namespace kain_anim {
constexpr std::int32_t kReady = 0;           // st_ready, stored at 0x445e8f
constexpr std::int32_t kRun = 1;             // st_run, FUN_004458e0
constexpr std::int32_t kBack = 59;           // st_back, stored at 0x446240
constexpr std::int32_t kStartJump = 76;      // st_startjump, FUN_004436e0
constexpr std::int32_t kFallingUp = 78;      // st_fallingup, FUN_00442800
constexpr std::int32_t kFallingDown = 79;    // st_fallingdn, FUN_00442800 and FUN_00442ae0
constexpr std::int32_t kNormalLanding = 86;  // st_normal_landing
constexpr std::int32_t kRunLanding = 87;     // st_run_landing
}  // namespace kain_anim

// Chooses the animation state from the player and runs the state machine
// (docs/GAMEPLAY.md, "Animation state"). Read from the original:
//   - on the ground forward starts st_run, back st_back and nothing st_ready;
//   - the jump state starts st_startjump with a 0.2 s timer (FUN_004436e0);
//   - in the air the state is st_fallingup while the vertical velocity is not
//     negative and st_fallingdn after that (FUN_00442800, FUN_00442ae0);
//   - the run state writes the run clip rate every frame (FUN_00445ba0).
// Read from the oracle, not from the code: a landing is st_run_landing when
// forward is held and st_normal_landing otherwise, and it lasts until its
// sequence ends.
// Not modelled: combat mode (Shift gives st_ready_cm and st_walk_cm in the
// oracle), the turns (they keep st_ready), attacks, and the shorter
// st_startjump of a standing jump.
class KainAnimation {
public:
    // `graph` is Kain's `animatio` graph and must outlive this object.
    KainAnimation(const bo2::engine::AnimGraph& graph, const KainTuning& tuning);

    // One tick: the input the controller just used and its state after it.
    void step(const PlayerInput& input, const PlayerState& state, double seconds);

    // KainState+0x458: the state the player asked for last, -1 before the
    // first step. The machine may be in another one (st_rdy2run before st_run).
    std::int32_t state() const { return state_; }
    const bo2::engine::AnimMachine& machine() const { return machine_; }

private:
    bo2::engine::AnimMachine machine_;
    float deadzone_ = 0.0f;
    float run_rate_ = 1.0f;
    std::int32_t state_ = -1;
    bool was_grounded_ = true;
    bool landing_ = false;
    float takeoff_left_ = 0.0f;  // the FUN_004436e0 timer

    void request(std::int32_t state);
};

}  // namespace bo2::game
