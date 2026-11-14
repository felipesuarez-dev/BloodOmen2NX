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
//   - the jump state starts st_startjump (FUN_004436e0, FUN_00443d60) and its
//     update (FUN_00443a50) leaves it when its 0.2 s timer runs out, when the
//     vertical velocity is no longer positive, or when the hold boost is over:
//     the boost start has passed and the button is up or the boost end has
//     passed too;
//   - in the air the state is then st_fallingup while the vertical velocity is
//     not negative and st_fallingdn after that (FUN_00442800, FUN_00442ae0);
//   - the run state, every frame (FUN_00445ba0), takes the stick magnitude s
//     (at most 1) and writes the weights s and 1 - s of the cross blend of
//     layers 0 (run) and 1 (walk), and their rates (0.5 s + 0.5) and (s + 1)
//     times the run clip rate.
// Read from the oracle, not from the code: a landing is st_run_landing when
// forward is held and st_normal_landing otherwise, and it lasts until its
// sequence ends.
// Not modelled: combat mode (Shift gives st_ready_cm and st_walk_cm in the
// oracle), the turns (they keep st_ready) and attacks.
class KainAnimation {
public:
    // `graph` is Kain's `animatio` graph and must outlive this object.
    KainAnimation(const bo2::engine::AnimGraph& graph, const KainTuning& tuning);

    // One tick: the input the controller just used and its state after it.
    void step(const PlayerInput& input, const PlayerState& state, double seconds);

    // KainState+0x458: the state the player asked for last, -1 before the
    // first step. The machine may be in another one (st_rdy2run before st_run).
    std::int32_t state() const { return state_; }
    // Layer 0, the one the movement states drive.
    const bo2::engine::AnimMachine& machine() const { return layers_.layer(0); }
    const bo2::engine::AnimLayers& layers() const { return layers_; }
    // The cross blend of the run: on while layer 2 holds a state (the graph
    // starts st_moveblend there, which creates the cross blend entity), with
    // the weight of layer 0; layer 1 has the rest.
    bool cross_blend() const { return layers_.state_of(2) >= 0 && layers_.layer(1).clip() >= 0; }
    float run_weight() const { return run_weight_; }

private:
    bo2::engine::AnimLayers layers_;
    float run_weight_ = 1.0f;
    float deadzone_ = 0.0f;
    float run_rate_ = 1.0f;
    std::int32_t state_ = -1;
    bool was_grounded_ = true;
    bool landing_ = false;
    float takeoff_left_ = 0.0f;  // the 0.2 s timer of the jump state
    // The hold boost window of the jump model (FUN_00451d00), in seconds after
    // takeoff; unset when the tuning carries no jump.
    bool has_boost_ = false;
    float boost_start_ = 0.0f;
    float boost_end_ = 0.0f;
    float air_time_ = 0.0f;
    bool jump_released_ = false;

    void request(std::int32_t state);
};

}  // namespace bo2::game
