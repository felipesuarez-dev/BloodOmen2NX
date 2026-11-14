#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bo2/engine/anim_reel.hpp"
#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/skeleton_pose.hpp"
#include "bo2/game/kain_actor.hpp"
#include "bo2/game/kain_tuning.hpp"
#include "bo2/game/kain_vitals.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

using bo2::engine::Mat4;
using bo2::engine::Vec3;

float distance(const Vec3& left, const Vec3& right) {
    return bo2::engine::length(left - right);
}

// The position of a bone's joint under a pose: its pivot through its matrix.
Vec3 joint(const bo2::engine::ModelSkeleton& skeleton, const std::vector<Mat4>& matrices,
           std::size_t index) {
    return bo2::engine::transform_point(matrices[index], skeleton.bones[index].pivot);
}

void check_kain_skinning(const bo2::engine::BigArchive& archive) {
    const bo2::engine::BigEntry* model = archive.find("model", "kain");
    const bo2::engine::BigEntry* run = archive.find("animreel", "kain/run");
    CHECK(model != nullptr && run != nullptr);
    if (model == nullptr || run == nullptr) {
        return;
    }
    const bo2::engine::LevelGeometry geometry = bo2::engine::parse_model_asset(
        archive.extract(*model), model->symbols, archive.names());
    const bo2::engine::ModelSkeleton* found = nullptr;
    for (const bo2::engine::ModelSkeleton& candidate : geometry.skeletons) {
        if (!candidate.bones.empty()) {
            found = &candidate;
        }
    }
    CHECK(found != nullptr);
    if (found == nullptr) {
        return;
    }
    const bo2::engine::ModelSkeleton& skeleton = *found;
    CHECK(skeleton.bones.size() == 104);
    CHECK(skeleton.weights.size() == 1988 && skeleton.joints.size() == 1988);
    CHECK(geometry.positions.size() == 1988);

    // Every vertex belongs to exactly one bone range, a bone's matrix index is
    // its position and a parent comes before its child.
    std::vector<int> owners(geometry.positions.size(), 0);
    bool ordered = true;
    std::size_t scale_keepers = 0;
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index) {
        const bo2::engine::SkeletonBone& bone = skeleton.bones[index];
        ordered = ordered && bone.current_matrix == index &&
                  (index == 0 ? bone.parent_matrix == 0xFFFFFFFFU : bone.parent_matrix < index);
        if ((bone.channel_mask & bo2::engine::kBoneKeepsParentScale) != 0) {
            ++scale_keepers;
        }
        for (std::uint32_t vertex = bone.first_vertex;
             vertex < bone.first_vertex + bone.vertex_count && vertex < owners.size(); ++vertex) {
            ++owners[vertex];
        }
    }
    CHECK(ordered);
    CHECK(scale_keepers == 103);  // every bone but the root
    std::size_t owned_once = 0;
    std::size_t leaning = 0;
    for (std::size_t vertex = 0; vertex < owners.size(); ++vertex) {
        owned_once += owners[vertex] == 1 ? 1 : 0;
        leaning += skeleton.weights[vertex] != 0.0f ? 1 : 0;
        CHECK(skeleton.weights[vertex] >= 0.0f && skeleton.weights[vertex] <= 0.5f);
        CHECK(skeleton.joints[vertex] < skeleton.bones.size());
    }
    CHECK(owned_once == 1988);
    CHECK(leaning == 870);  // the other 1,118 name their owner with weight 0

    // Without channels the pose is the bind pose: no inverse bind matrix exists.
    const bo2::engine::AnimReel rest;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    bo2::engine::skin_vertices(
        skeleton,
        bo2::engine::bone_matrices(
            skeleton, bo2::engine::sample_pose(
                          skeleton, rest, bo2::engine::bind_channels(skeleton, rest), 0.0f)),
        geometry.positions, geometry.normals, positions, normals);
    float rest_error = 0.0f;
    for (std::size_t vertex = 0; vertex < positions.size(); ++vertex) {
        rest_error = std::max(rest_error, distance(positions[vertex], geometry.positions[vertex]));
    }
    CHECK(rest_error < 1.0e-4f);

    const bo2::engine::AnimReel reel = bo2::engine::parse_anim_reel(archive.extract(*run));
    const std::vector<bo2::engine::BoneChannels> channels =
        bo2::engine::bind_channels(skeleton, reel);
    std::size_t bound_bones = 0;
    for (const bo2::engine::BoneChannels& bound : channels) {
        bool any = false;
        for (int component = 0; component < 3; ++component) {
            any = any || bound.rotation[component] >= 0 || bound.translation[component] >= 0 ||
                  bound.scale[component] >= 0;
        }
        bound_bones += any ? 1 : 0;
    }
    CHECK(bound_bones == 102);  // every plain node of the clip is a bone id
    const auto matrices_at = [&](float frame) {
        return bo2::engine::bone_matrices(
            skeleton, bo2::engine::sample_pose(skeleton, reel, channels, frame));
    };
    const std::vector<Mat4> first = matrices_at(0.0f);
    const std::vector<Mat4> middle = matrices_at(12.0f);
    const std::vector<Mat4> last = matrices_at(static_cast<float>(reel.frames - 1));

    // The run cycle closes: the last frame is the first one again, although
    // the root has moved 160 units (the root's channels are not applied).
    float loop_error = 0.0f;
    float stride = 0.0f;
    for (std::size_t index = 0; index < first.size(); ++index) {
        for (int cell = 0; cell < 12; ++cell) {
            loop_error = std::max(loop_error, std::fabs(first[index].m[cell] - last[index].m[cell]));
            stride = std::max(stride, std::fabs(first[index].m[cell] - middle[index].m[cell]));
        }
    }
    CHECK(loop_error < 2.0e-2f);
    CHECK(stride > 0.5f);

    // Bones 98..100 and 93..95 are the two legs (hip, knee, ankle), without
    // scale channels: the pose keeps their lengths.
    const std::size_t legs[2][3] = {{98, 99, 100}, {93, 94, 95}};
    for (const auto& leg : legs) {
        for (const std::vector<Mat4>* pose : {&first, &middle}) {
            for (int segment = 0; segment < 2; ++segment) {
                const float bind_length = distance(skeleton.bones[leg[segment]].pivot,
                                                   skeleton.bones[leg[segment + 1]].pivot);
                const float posed_length = distance(joint(skeleton, *pose, leg[segment]),
                                                    joint(skeleton, *pose, leg[segment + 1]));
                CHECK(std::fabs(bind_length - posed_length) < 1.0e-2f);
            }
        }
    }
    // Local x is forward. At frame 0 the first leg's thigh swings forward and
    // half a cycle later the other one does.
    CHECK(joint(skeleton, first, 99).x - joint(skeleton, first, 98).x > 10.0f);
    CHECK(joint(skeleton, middle, 94).x - joint(skeleton, middle, 93).x > 10.0f);
    CHECK(joint(skeleton, first, 94).x - joint(skeleton, first, 93).x < 5.0f);

    // The posed mesh stays a body: its bounds stay close to the bind bounds.
    bo2::engine::skin_vertices(skeleton, first, geometry.positions, geometry.normals, positions,
                               normals);
    Vec3 low = positions[0];
    Vec3 high = positions[0];
    for (const Vec3& position : positions) {
        low = {std::min(low.x, position.x), std::min(low.y, position.y),
               std::min(low.z, position.z)};
        high = {std::max(high.x, position.x), std::max(high.y, position.y),
                std::max(high.z, position.z)};
    }
    CHECK(low.z > -12.0f && high.z < 90.0f);
    CHECK(high.x - low.x < 90.0f && high.y - low.y < 90.0f);
}

// The actor the host draws: the animation state picks the reel.
void check_kain_actor(const std::string& path, const bo2::game::KainTuning& tuning) {
    bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path);
    const bo2::engine::BigEntry* model = archive.find("model", "kain");
    CHECK(model != nullptr);
    if (model == nullptr) {
        return;
    }
    const bo2::engine::LevelGeometry geometry = bo2::engine::parse_model_asset(
        archive.extract(*model), model->symbols, archive.names());
    bo2::engine::SkinnedActor skinned;
    for (const bo2::engine::ModelSkeleton& candidate : geometry.skeletons) {
        if (!candidate.bones.empty()) {
            skinned.skeleton = candidate;
        }
    }
    skinned.bind_positions = geometry.positions;
    skinned.bind_normals = geometry.normals;
    bo2::game::KainActor actor(std::move(archive), skinned, tuning);

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    // Before the first step there is no state: the bind pose, placed.
    actor.pose({10.0f, 20.0f, 30.0f}, 0.0f, positions, normals);
    CHECK(actor.clip_name().empty());
    CHECK(positions.size() == 1988);
    CHECK(distance(positions[0], geometry.positions[0] + Vec3{10.0f, 20.0f, 30.0f}) < 1.0e-4f);

    bo2::game::PlayerInput idle;
    bo2::game::PlayerInput forward;
    forward.move_y = 1.0f;
    bo2::game::PlayerState ground;
    ground.grounded = true;
    bo2::game::PlayerState rising;
    rising.vertical_velocity = 300.0f;
    bo2::game::PlayerState falling;
    falling.vertical_velocity = -100.0f;

    actor.step(idle, ground, 0.02);
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(actor.animation_state() == bo2::game::kain_anim::kReady);
    CHECK(actor.clip_name() == "kain/normal_stance");
    const std::vector<Vec3> stance = positions;
    // W: the run reel, held on its first frame while st_rdy2run blends in,
    // then moving.
    // W: st_rdy2run blends from the stance into frame 0 of the run reel over
    // 0.2 s, so the first drawn pose has barely left the stance.
    actor.step(forward, ground, 0.02);
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(actor.animation_state() == bo2::game::kain_anim::kRun);
    CHECK(actor.clip_name() == "kain/run");
    const auto farthest = [&](const std::vector<Vec3>& other) {
        float value = 0.0f;
        for (std::size_t vertex = 0; vertex < positions.size(); ++vertex) {
            value = std::max(value, distance(positions[vertex], other[vertex]));
        }
        return value;
    };
    const float blend_start = farthest(stance);
    for (int tick = 0; tick < 5; ++tick) {
        actor.step(forward, ground, 0.02);
        actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    }
    const float blend_middle = farthest(stance);
    CHECK(blend_start < blend_middle);
    for (int tick = 0; tick < 6; ++tick) {
        actor.step(forward, ground, 0.02);
        actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    }
    // The blend is over and the run clip moves.
    CHECK(farthest(stance) > blend_middle && farthest(stance) > 5.0f);
    const std::vector<Vec3> run_start = positions;
    for (int tick = 0; tick < 10; ++tick) {
        actor.step(forward, ground, 0.02);
        actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    }
    CHECK(farthest(run_start) > 5.0f);
    // Half stick: the run and the walk clips are summed by the stick, so the
    // pose is neither the full run nor what it was a moment ago.
    bo2::game::PlayerInput half;
    half.move_y = 0.5f;
    const std::vector<Vec3> full_run = positions;
    actor.step(half, ground, 0.0);
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(actor.clip_name() == "kain/run");
    CHECK(farthest(full_run) > 1.0f);
    actor.step(forward, ground, 0.0);
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(farthest(full_run) < 1.0e-3f);  // full stick again: the run alone
    // A jump: the takeoff pose, the rising pose after the timer, the falling pose.
    actor.step(forward, rising, 0.02);
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(actor.clip_name() == "kain/takeoff_p");
    for (int tick = 0; tick < 12; ++tick) {
        actor.step(forward, rising, 0.02);
    }
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(actor.clip_name() == "kain/jump_up_p");
    actor.step(forward, falling, 0.02);
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(actor.clip_name() == "kain/fallingdown_p");
    actor.step(forward, ground, 0.02);
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(actor.animation_state() == bo2::game::kain_anim::kRunLanding);
    CHECK(actor.clip_name() == "kain/run_landing");
    // One reel in a loop, whatever the state.
    actor.set_fixed_clip("kain/walk");
    actor.pose({0.0f, 0.0f, 0.0f}, 0.0f, positions, normals);
    CHECK(actor.clip_name() == "kain/walk");
    bool thrown = false;
    try {
        actor.set_fixed_clip("kain/no_such_clip");
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    CHECK(thrown);
}

}  // namespace

// The ground movement of the original through the real graph. The Wine oracle
// shows 100 u/s from rest until Kain is 20.1 u ahead and 230 u/s after it, and
// 0.1 s without moving before he walks back at 106.25 u/s
// (docs/FORMATS/animatio.md, Root motion).
void check_kain_ground_motion(const bo2::engine::BigArchive& archive,
                              const bo2::game::KainTuning& tuning) {
    const bo2::engine::BigEntry* entry = archive.find("animatio", "kain");
    CHECK(entry != nullptr);
    if (entry == nullptr) {
        return;
    }
    const bo2::engine::AnimGraph graph = bo2::engine::parse_anim_graph(archive.extract(*entry));
    bo2::game::KainAnimation animation(graph, tuning);
    bo2::game::PlayerState ground;
    ground.grounded = true;
    const bo2::game::PlayerInput idle;
    bo2::game::PlayerInput forward;
    forward.move_y = 1.0f;
    bo2::game::PlayerInput back;
    back.move_y = -1.0f;
    const double tick = 0.007;  // not a divisor of the blend times
    const auto step = [&](const bo2::game::PlayerInput& input) {
        animation.begin(input, ground, tick);
        animation.end(input, ground, tick);
        return animation.ground_motion().forward;
    };
    step(idle);

    // W: st_rdy2run blends in for 0.2 s and its blend carries 20 u.
    float travelled = 0.0f;
    int ticks = 0;
    for (; ticks < 40; ++ticks) {
        const float advance = step(forward);
        travelled += advance;
        if (std::fabs(advance - 100.0f * 0.007f) > 1.0e-3f) {
            break;  // the blend ended inside this tick
        }
    }
    CHECK(ticks == 28);
    CHECK(std::fabs(travelled - 20.1f) < 1.0e-3f);
    // The run clip starts at its node speed, 1.5: the run state writes the run
    // rate before the advance that restarts the clip (FUN_00445930 calls
    // FUN_00445ba0 and then FUN_0041f460), so the rate takes one more tick.
    CHECK(std::fabs(step(forward) - 300.0f * 0.007f) < 1.0e-3f);
    CHECK(std::fabs(step(forward) - 230.0f * 0.007f) < 1.0e-3f);

    // Back to rest, then S: 0.1 s of a blend without a transform, then the
    // walk back clip at its node speed.
    for (int index = 0; index < 150; ++index) {
        step(idle);
    }
    CHECK(animation.state() == bo2::game::kain_anim::kReady);
    CHECK(std::fabs(step(idle)) < 1.0e-4f);
    int pause = 0;
    float advance = 0.0f;
    for (; pause < 40; ++pause) {
        advance = step(back);
        if (advance != 0.0f) {
            break;
        }
    }
    CHECK(pause == 15);
    CHECK(std::fabs(advance + 106.25f * 0.007f) < 1.0e-3f);

    // Combat mode: with the control held the oracle shows st_ready_cm, and
    // with W st_walk_cm at exactly 200 u/s from the first sample: the blend
    // that starts it carries 20 u in 0.1 s and kain/advance moves 200 u/s.
    for (int index = 0; index < 150; ++index) {
        step(idle);
    }
    bo2::game::PlayerInput hold;
    hold.combat = true;
    step(hold);
    CHECK(animation.state() == bo2::game::kain_anim::kReadyCombat);
    CHECK(std::fabs(animation.ground_motion().forward) < 1.0e-4f);
    bo2::game::PlayerInput advance_input = hold;
    advance_input.move_y = 1.0f;
    CHECK(std::fabs(step(advance_input) - 200.0f * 0.007f) < 1.0e-3f);
    CHECK(animation.state() == bo2::game::kain_anim::kWalkCombat);
    CHECK(!animation.ground_motion().turn);
    float walked = 0.0f;
    for (int index = 0; index < 100; ++index) {
        walked += step(advance_input);
    }
    // 0.7 s at 200 u/s; the ticks where the blend and the clip end give less.
    CHECK(walked > 0.7f * 200.0f * 0.95f && walked < 0.7f * 200.0f + 0.3f);
    // The control released: the mode lasts 0.15 s more, then st_run is back.
    int held = 0;
    for (; held < 60 && animation.combat_mode(); ++held) {
        step(forward);
    }
    CHECK(held == 23);  // the arming tick and 22 of 7 ms
    CHECK(animation.state() == bo2::game::kain_anim::kRun);
}

int main() {
    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "kain asset test skipped (BO2NX_ASSETS_DIR is unset)\n";
        return 0;
    }
    const std::filesystem::path path =
        std::filesystem::path(root) / "media/gogames/bo2/resources/levels/kain/kain.big";
    if (!std::filesystem::exists(path)) {
        std::cout << "kain asset test skipped (kain.big not found)\n";
        return 0;
    }
    try {
        const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path.string());
        const bo2::engine::BigEntry* entry = archive.find("tunedata", "kain");
        CHECK(entry != nullptr);
        if (entry != nullptr) {
            const std::vector<std::uint8_t> bytes = archive.extract(*entry);
            CHECK(bytes.size() > 4);
            CHECK(bytes[0] == 0xC5 && bytes[1] == 0x2E && bytes[2] == 0x00 && bytes[3] == 0x00);
            bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(bytes);
            CHECK(std::fabs(tuning.stick_deadzone - 0.1f) < 1.0e-6f);
            CHECK(tuning.turn_rate_ready == 2.5f);
            CHECK(tuning.turn_rate_run == 2.5f);
            CHECK(tuning.turn_rate_walk_back == 2.5f);
            CHECK(tuning.jump.gravity_up == -2500.0f);
            CHECK(tuning.jump.gravity_down == -700.0f);
            CHECK(tuning.jump.terminal_fall == 512.0f);
            CHECK(tuning.jump.height_tap == 64.0f);
            CHECK(tuning.jump.height_hold == 128.0f);
            CHECK(std::fabs(tuning.run_clip_rate - 1.15f) < 1.0e-6f);
            // healthParams: the tables behind the relocated pointers.
            CHECK(tuning.lore_levels.size() == 26);
            if (tuning.lore_levels.size() == 26) {
                CHECK(tuning.lore_levels[0].max_lore == 6.0f);
                CHECK(tuning.lore_levels[0].max_hit_points == 30.0f);
                CHECK(tuning.lore_levels[1].max_lore == 30.0f);
                CHECK(tuning.lore_levels[1].max_hit_points == 60.0f);
                CHECK(bo2::game::top_max_lore(tuning) == 950.0f);
                CHECK(bo2::game::top_max_health(tuning) == 500.0f);
            }
            CHECK(tuning.default_lore_level == 15);
            CHECK(tuning.start_lore_levels.size() == 17);
            CHECK(bo2::game::start_lore_level(tuning, "slums") == 0);
            CHECK(bo2::game::start_lore_level(tuning, "keep") == 7);
            CHECK(bo2::game::start_lore_level(tuning, "no such level") == 15);
            // Kain enters the slums with 30 hit points and room for 6 lore.
            bo2::game::KainVitals vitals;
            bo2::game::set_lore_level(vitals, tuning,
                                      bo2::game::start_lore_level(tuning, "slums"), 0.0f);
            CHECK(vitals.health == 30.0f && vitals.max_health == 30.0f);
            CHECK(vitals.lore == 0.0f && vitals.max_lore == 6.0f);
            bo2::game::apply_kain_clip_speeds(tuning, archive);
            check_kain_actor(path.string(), tuning);
            check_kain_ground_motion(archive, tuning);
            CHECK(std::fabs(tuning.run_root_speed - 200.0f) < 1.0e-3f);
            CHECK(std::fabs(tuning.walk_root_speed - 56.3485f) < 1.0e-3f);
            CHECK(std::fabs(tuning.walk_back_root_speed - 62.5f) < 1.0e-3f);
            // The walk_backwards clip node of the kain animatio graph.
            CHECK(std::fabs(tuning.walk_back_clip_speed - 1.7f) < 1.0e-6f);
            // FUN_00445ba0 at full stick, and the walk-back clip at its node speed.
            CHECK(std::fabs(tuning.run_root_speed * tuning.run_clip_rate - 230.0f) < 1.0e-2f);
            CHECK(std::fabs(tuning.walk_back_root_speed * tuning.walk_back_clip_speed - 106.25f) < 1.0e-2f);
            const bo2::game::JumpDerived derived = bo2::game::derive_jump(tuning);
            CHECK(std::fabs(derived.launch_speed - 565.685f) < 1.0e-2f);
            CHECK(std::fabs(derived.boost_accel - 1998.94f) < 1.0e-2f);
            CHECK(std::fabs(derived.min_airtime - 0.6539f) < 1.0e-2f);
            CHECK(std::fabs(derived.max_airtime - 1.0181f) < 1.0e-3f);
            // The air control fields (docs/FORMATS/tunedata.md) and what FUN_00452030 derives
            // from them. The Wine oracle measured 323.0, 205.2 and 203.0 u/s in the air.
            CHECK(tuning.jump_turn_rate == 45.0f);
            CHECK(tuning.side_jump.gravity_up == -3000.0f);
            CHECK(tuning.side_jump.height_tap == 64.0f);
            CHECK(tuning.air_forward.max_dist == 320.0f);
            CHECK(tuning.air_side.max_lateral_vel == 256.0f);
            CHECK(tuning.air_back.min_dist == 64.0f);
            const bo2::game::AirSteerDerived forward =
                bo2::game::derive_air_steer(tuning.air_forward, derived);
            const bo2::game::AirSteerDerived back =
                bo2::game::derive_air_steer(tuning.air_back, derived);
            const bo2::game::AirSteerDerived side =
                bo2::game::derive_air_steer(tuning.air_side, derived);
            CHECK(std::fabs(forward.launch_speed - 146.813f) < 1.0e-2f);
            CHECK(std::fabs(forward.peak_speed - 322.956f) < 1.0e-2f);
            CHECK(std::fabs(back.launch_speed - 97.8755f) < 1.0e-2f);
            CHECK(std::fabs(back.peak_speed - 205.246f) < 1.0e-2f);
            CHECK(std::fabs(side.launch_speed - 110.110f) < 1.0e-2f);
            CHECK(std::fabs(side.peak_speed - 202.998f) < 1.0e-2f);
        }
        const bo2::engine::BigEntry* walk = archive.find("animreel", "kain/walk");
        CHECK(walk != nullptr);
        if (walk != nullptr) {
            const bo2::engine::AnimReel reel =
                bo2::engine::parse_anim_reel(archive.extract(*walk));
            CHECK(reel.frames == 47);
            // Cross-check against tools/bo2fmt/animreel.py: displacement 86.4010.
            CHECK(std::fabs(bo2::engine::root_displacement(reel).x - 86.4010f) < 1.0e-3f);
            CHECK(std::fabs(bo2::engine::root_displacement(reel).x / reel.duration - 56.3485f) <
                  1.0e-3f);
        }
        // The channel lookup (find_channel) relies on the bone keys being
        // sorted and unique, as the original's bisection does (FUN_0063ce20).
        std::size_t reels = 0;
        std::size_t unsorted_reels = 0;
        std::size_t bone_streams = 0;
        for (const bo2::engine::BigEntry& candidate : archive.entries()) {
            if (candidate.type != "animreel") {
                continue;
            }
            ++reels;
            const bo2::engine::AnimReel reel =
                bo2::engine::parse_anim_reel(archive.extract(candidate));
            std::uint32_t previous = 0;
            bool first = true;
            bool sorted = true;
            for (const bo2::engine::AnimChannel& channel : reel.channels) {
                if (channel.node >= 0x1000) {
                    continue;
                }
                const std::uint32_t key = (static_cast<std::uint32_t>(channel.node) << 16) |
                                          (static_cast<std::uint32_t>(channel.type) << 8) |
                                          channel.component;
                if (!first && key <= previous) {
                    sorted = false;
                }
                first = false;
                previous = key;
                if (channel.opaque && channel.type <= 2) {
                    ++bone_streams;
                }
            }
            if (!sorted) {
                ++unsorted_reels;
            }
        }
        CHECK(reels == 134);
        CHECK(unsorted_reels == 0);
        CHECK(bone_streams == 0);
        check_kain_skinning(archive);
    } catch (const std::exception& error) {
        std::cerr << "kain asset test: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "kain asset tests: ok\n";
    return 0;
}
