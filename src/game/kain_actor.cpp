#include "bo2/game/kain_actor.hpp"

#include <stdexcept>
#include <utility>

namespace bo2::game {

namespace {

constexpr float kReelFramesPerSecond = 30.0f;  // the 1/30 of the reel duration (0x872498)

const bo2::engine::BigEntry& graph_entry(const bo2::engine::BigArchive& archive) {
    const bo2::engine::BigEntry* entry = archive.find("animatio", "kain");
    if (entry == nullptr) {
        throw std::runtime_error("the Kain container has no animatio entry named kain");
    }
    return *entry;
}

}  // namespace

KainActor::KainActor(bo2::engine::BigArchive archive, bo2::engine::SkinnedActor model,
                     const KainTuning& tuning)
    : archive_(std::move(archive)),
      graph_entry_(graph_entry(archive_)),
      graph_(bo2::engine::parse_anim_graph(archive_.extract(graph_entry_))),
      model_(std::move(model)),
      animation_(graph_, tuning) {}

const KainActor::Clip* KainActor::load_clip(const std::string& reel_name) {
    const auto found = clips_.find(reel_name);
    if (found != clips_.end()) {
        return &found->second;
    }
    const bo2::engine::BigEntry* entry = archive_.find("animreel", reel_name);
    if (entry == nullptr) {
        return nullptr;
    }
    Clip clip;
    clip.reel = bo2::engine::parse_anim_reel(archive_.extract(*entry));
    clip.channels = bo2::engine::bind_channels(model_.skeleton, clip.reel);
    return &clips_.emplace(reel_name, std::move(clip)).first->second;
}

void KainActor::set_fixed_clip(const std::string& reel_name) {
    if (load_clip(reel_name) == nullptr) {
        throw std::runtime_error("no animreel named " + reel_name + " in the Kain container");
    }
    fixed_clip_ = reel_name;
    fixed_seconds_ = 0.0;
}

void KainActor::step(const PlayerInput& input, const PlayerState& state, double seconds) {
    animation_.step(input, state, seconds);
    fixed_seconds_ += seconds;
}

float KainActor::begin(const PlayerInput& input, const PlayerState& state, double seconds) {
    animation_.begin(input, state, seconds);
    fixed_seconds_ += seconds;
    return animation_.ground_advance();
}

void KainActor::end(const PlayerInput& input, const PlayerState& state, double seconds) {
    animation_.end(input, state, seconds);
}

void KainActor::pose(const bo2::engine::Vec3& position, float yaw,
                     std::vector<bo2::engine::Vec3>& positions,
                     std::vector<bo2::engine::Vec3>& normals) {
    clip_name_.clear();
    if (!fixed_clip_.empty()) {
        const Clip* clip = load_clip(fixed_clip_);
        if (clip != nullptr) {
            clip_name_ = fixed_clip_;
            shown_pose_ = bo2::engine::sample_pose(
                model_.skeleton, clip->reel, clip->channels,
                bo2::engine::loop_frame(clip->reel, fixed_seconds_));
            bo2::engine::place_pose(model_, shown_pose_, position, yaw, positions, normals);
            return;
        }
    }
    const bo2::engine::AnimMachine& machine = animation_.machine();
    const Clip* clip = nullptr;
    if (fixed_clip_.empty() && machine.clip() >= 0) {
        const std::string name = bo2::engine::anim_clip_reel_name(
            graph_, graph_.clips[static_cast<std::size_t>(machine.clip())], archive_,
            graph_entry_);
        clip = name.empty() ? nullptr : load_clip(name);
        if (clip != nullptr) {
            clip_name_ = name;
        }
    }
    if (clip == nullptr) {
        // No clip to draw: the bind pose.
        const bo2::engine::AnimReel rest;
        shown_pose_.clear();
        bo2::engine::pose_actor(model_, rest, {}, 0.0f, position, yaw, positions, normals);
        return;
    }
    std::vector<bo2::engine::BoneTransform> pose = bo2::engine::sample_pose(
        model_.skeleton, clip->reel, clip->channels, machine.reel_time() * kReelFramesPerSecond);
    // The run: the cross blend layer sums the run (layer 0) and the walk
    // (layer 1) by the stick (FUN_005d4300 with the weights of FUN_00445ba0).
    // The original blends each input in its own sampler first; here the blend
    // into a new clip is applied after the sum.
    const float walk_weight = 1.0f - animation_.run_weight();
    if (animation_.state() == kain_anim::kRun && animation_.cross_blend() &&
        walk_weight >= 1.0e-4f) {
        const bo2::engine::AnimMachine& walk = animation_.layers().layer(1);
        const std::string walk_name = bo2::engine::anim_clip_reel_name(
            graph_, graph_.clips[static_cast<std::size_t>(walk.clip())], archive_, graph_entry_);
        const Clip* walk_clip = walk_name.empty() ? nullptr : load_clip(walk_name);
        if (walk_clip != nullptr) {
            pose = bo2::engine::cross_blend_poses(
                pose, animation_.run_weight(),
                bo2::engine::sample_pose(model_.skeleton, walk_clip->reel, walk_clip->channels,
                                         walk.reel_time() * kReelFramesPerSecond),
                walk_weight);
        }
    }
    if (machine.clip_starts() != shown_clip_start_) {
        // A clip started since the last drawn pose: that pose is where its
        // blend begins (the snapshot the blend sampler takes).
        shown_clip_start_ = machine.clip_starts();
        blend_from_ = shown_pose_;
    }
    if (machine.blending() && !blend_from_.empty()) {
        pose = bo2::engine::blend_poses(blend_from_, pose, machine.blend_factor());
    }
    shown_pose_ = pose;
    bo2::engine::place_pose(model_, pose, position, yaw, positions, normals);
}

}  // namespace bo2::game
