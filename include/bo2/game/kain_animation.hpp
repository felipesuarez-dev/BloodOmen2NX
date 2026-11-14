#pragma once

#include <cstdint>
#include <vector>

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
constexpr std::int32_t kReadyCombat = 13;    // st_ready_cm
constexpr std::int32_t kWalkCombat = 14;     // st_walk_cm
constexpr std::int32_t kBackCombat = 15;     // st_back_cm
constexpr std::int32_t kBack = 59;           // st_back, stored at 0x446240
constexpr std::int32_t kStrafeRightCombat = 71;  // st_straferight_cm
constexpr std::int32_t kStrafeLeftCombat = 72;   // st_strafeleft_cm
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
// Combat mode (the combat control, Shift in the captures):
//   - FUN_0042dca0 enters it while the control is held and leaves it once the
//     control has been up for the hold exit delay of the tuning (0.15 s);
//   - with the control held and the mode on, the reaction table of the game
//     (`data/kain.react`, `t_directlytocombat`) starts the combat movement,
//     which lasts until the mode goes off (`t_notcombat`);
//   - the combat movement starts the state of the stick direction from the
//     table its enter fills (0x0044a1c0): st_ready_cm, st_walk_cm, st_back_cm,
//     st_strafeleft_cm, st_straferight_cm. The direction is FUN_00425580 and
//     the state changes when the direction does (FUN_0044ab20). Its update
//     (FUN_0044a380) moves Kain by the root motion and does not turn him.
// Not modelled: the turn toward a target and its states, the stealth and
// silent tables of the combat movement, the combat jump, the turns in place
// (they keep st_ready) and attacks.
class KainAnimation {
public:
    // `graph` is Kain's `animatio` graph and must outlive this object.
    KainAnimation(const bo2::engine::AnimGraph& graph, const KainTuning& tuning);

    // The first half of a tick, before the player moves: starts the ground
    // state the input asks for, advances the layers and works out how far the
    // animation moves Kain forward (`ground_advance`). `state` is the player
    // at the start of the tick.
    void begin(const PlayerInput& input, const PlayerState& state, double seconds);
    // The second half, after the player moved: the takeoff, the fall and the
    // landing, from the player at the end of the tick.
    void end(const PlayerInput& input, const PlayerState& state, double seconds);
    // Both halves with one player state, for a caller that only draws.
    void step(const PlayerInput& input, const PlayerState& state, double seconds);

    // What the root motion of the last `begin` moves Kain by. The updates of
    // his ground states (FUN_00445930 for the run, FUN_00446270 for the walk
    // back, FUN_0044a380 for the combat movement) add the root translation of
    // the animation to his position (FUN_0041f460, FUN_0041f7d0): the
    // transform of the blend while a clip blends in, then the transform of the
    // clip node or the root of the reel (docs/FORMATS/animatio.md, Root
    // motion). The root of a reel is taken along the forward axis only. In
    // the run with a partial stick it is the sum of layers 0 and 1 by the
    // weights of the cross blend, as the pose is; the root output of the
    // cross blend entity is not decompiled.
    const GroundMotion& ground_motion() const { return ground_; }
    // KainState in combat mode (its vtable slots +0x14 and +0x18).
    bool combat_mode() const { return combat_mode_; }

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
    std::vector<float> clip_forward_speed_;  // KainTuning::clip_forward_speed
    GroundMotion ground_;
    // Combat mode, FUN_0042dca0: the mode, the exit timer (behaviour +0x6c4)
    // and whether it has been started (+0x6cc).
    bool combat_mode_ = false;
    bool combat_exit_armed_ = false;
    float combat_exit_left_ = 0.0f;
    float combat_exit_delay_ = 0.0f;  // A+0x104
    bool combat_move_ = false;        // the combat movement is the behaviour in charge
    std::int32_t combat_direction_ = -1;  // KainState+0x540 of the last pick
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
    void request_ground_state(const PlayerInput& input);
    void update_combat_mode(const PlayerInput& input, float seconds);
    std::int32_t combat_direction(const PlayerInput& input) const;
    GroundMotion layer_motion(const bo2::engine::AnimMachine& machine) const;
};

}  // namespace bo2::game
