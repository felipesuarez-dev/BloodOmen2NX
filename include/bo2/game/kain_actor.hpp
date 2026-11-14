#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "bo2/engine/anim_graph.hpp"
#include "bo2/engine/anim_reel.hpp"
#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/skeleton_pose.hpp"
#include "bo2/game/kain_animation.hpp"
#include "bo2/game/kain_tuning.hpp"
#include "bo2/game/player.hpp"

namespace bo2::game {

// Kain as the host draws him: his skinned model posed by the clip his
// animation state plays. The state machine is `KainAnimation`; the clip is the
// reel of the clip node on layer 0, at the machine's reel time (30 frames per
// second). While a clip blends in, the pose goes from the one drawn last to
// the first frame of the new clip, as the blend sampler does (FUN_00639990).
// In the run the pose is the cross blend of the run and the walk clips by the
// stick magnitude (layers 0 to 2). The lean of layer 3 is not drawn
// (docs/GAMEPLAY.md, "Animation state").
class KainActor {
public:
    // `archive` is the Kain container: it holds the `kain` animation graph and
    // the reels. `model` carries the bind pose and the skeleton. Throws
    // std::runtime_error when the container has no `kain` graph.
    KainActor(bo2::engine::BigArchive archive, bo2::engine::SkinnedActor model,
              const KainTuning& tuning);
    KainActor(const KainActor&) = delete;
    KainActor& operator=(const KainActor&) = delete;

    // Plays one reel in a loop instead of following the animation state (a
    // diagnostic). Throws std::runtime_error when the container has no such reel.
    void set_fixed_clip(const std::string& reel_name);

    // One tick of the animation: the input the controller used and its state.
    void step(const PlayerInput& input, const PlayerState& state, double seconds);
    // The same tick in two halves, for a host that moves the player by the
    // root motion: `begin` before the player steps, with its state at the
    // start of the tick, returns what the animation moves him by
    // (KainAnimation::ground_motion); `end` after it.
    GroundMotion begin(const PlayerInput& input, const PlayerState& state, double seconds);
    void end(const PlayerInput& input, const PlayerState& state, double seconds);

    // Skins the model at the current clip time and places it.
    void pose(const bo2::engine::Vec3& position, float yaw,
              std::vector<bo2::engine::Vec3>& positions,
              std::vector<bo2::engine::Vec3>& normals);

    std::int32_t animation_state() const { return animation_.state(); }
    // The reel being drawn; empty for the bind pose.
    const std::string& clip_name() const { return clip_name_; }

private:
    struct Clip {
        bo2::engine::AnimReel reel;
        std::vector<bo2::engine::BoneChannels> channels;
    };

    bo2::engine::BigArchive archive_;
    bo2::engine::BigEntry graph_entry_;
    bo2::engine::AnimGraph graph_;
    bo2::engine::SkinnedActor model_;
    KainAnimation animation_;
    std::map<std::string, Clip> clips_;  // reels loaded so far, by name
    std::string fixed_clip_;
    double fixed_seconds_ = 0.0;
    std::string clip_name_;
    // The blend: the pose drawn last, the pose the clip in play started from
    // and the clip start it belongs to.
    std::vector<bo2::engine::BoneTransform> shown_pose_;
    std::vector<bo2::engine::BoneTransform> blend_from_;
    std::uint32_t shown_clip_start_ = 0;

    const Clip* load_clip(const std::string& reel_name);
};

}  // namespace bo2::game
