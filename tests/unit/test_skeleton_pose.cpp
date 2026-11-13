#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/engine/skeleton_pose.hpp"

namespace {

using bo2::engine::AnimChannel;
using bo2::engine::AnimReel;
using bo2::engine::BoneChannels;
using bo2::engine::BoneTransform;
using bo2::engine::Mat4;
using bo2::engine::ModelSkeleton;
using bo2::engine::SkeletonBone;
using bo2::engine::Vec3;

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

constexpr float kHalfPi = 1.57079632679489661923f;
constexpr std::uint32_t kNoParent = 0xFFFFFFFFU;

bool near(const Vec3& value, float x, float y, float z, float tolerance = 1.0e-4f) {
    return std::fabs(value.x - x) < tolerance && std::fabs(value.y - y) < tolerance &&
           std::fabs(value.z - z) < tolerance;
}

SkeletonBone bone(std::uint32_t id, std::uint32_t index, std::uint32_t parent, const Vec3& pivot,
                  std::uint32_t first_vertex, std::uint32_t vertex_count,
                  std::uint16_t channel_mask = 0x207) {
    SkeletonBone result;
    result.bone_index = id;
    result.current_matrix = index;
    result.parent_matrix = parent;
    result.pivot = pivot;
    result.first_vertex = first_vertex;
    result.vertex_count = vertex_count;
    result.channel_mask = channel_mask;
    return result;
}

// A root and a two-bone chain along z. The ids differ from the positions, as
// they do in the game's models. Vertices 0..1 belong to the lower bone and
// 2..3 to the upper one; nobody blends by default.
ModelSkeleton chain() {
    ModelSkeleton skeleton;
    skeleton.bones = {bone(0, 0, kNoParent, {0.0f, 0.0f, 0.0f}, 0, 0, 0x3f),
                      bone(10, 1, 0, {0.0f, 0.0f, 10.0f}, 0, 2),
                      bone(20, 2, 1, {0.0f, 0.0f, 20.0f}, 2, 2)};
    skeleton.weights = {0.0f, 0.0f, 0.0f, 0.0f};
    skeleton.joints = {1, 1, 2, 2};
    return skeleton;
}

const std::vector<Vec3> kBind = {
    {0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 15.0f}, {0.0f, 0.0f, 20.0f}, {0.0f, 0.0f, 25.0f}};

AnimChannel constant(std::uint16_t node, std::uint8_t type, std::uint8_t component, float value) {
    AnimChannel channel;
    channel.node = node;
    channel.type = type;
    channel.component = component;
    channel.constant = true;
    channel.value = value;
    return channel;
}

AnimReel reel_of(std::vector<AnimChannel> channels, std::uint32_t frames = 1) {
    AnimReel reel;
    reel.fps = 30.0f;
    reel.frames = frames;
    reel.channels = std::move(channels);
    return reel;
}

std::vector<Vec3> skin(const ModelSkeleton& skeleton, const AnimReel& reel, float frame = 0.0f) {
    const std::vector<BoneChannels> channels = bo2::engine::bind_channels(skeleton, reel);
    const std::vector<Mat4> matrices = bo2::engine::bone_matrices(
        skeleton, bo2::engine::sample_pose(skeleton, reel, channels, frame));
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    bo2::engine::skin_vertices(skeleton, matrices, kBind, {}, positions, normals);
    return positions;
}

void test_bind_uses_the_bone_id() {
    const ModelSkeleton skeleton = chain();
    // Node 1 is a position, not an id: nothing may bind to it. Node 95 names a
    // bone the model does not have.
    const AnimReel reel = reel_of({constant(1, 0, 0, 9.0f), constant(10, 0, 1, 1.0f),
                                   constant(10, 1, 2, 2.0f), constant(20, 2, 0, 3.0f),
                                   constant(95, 0, 0, 4.0f)});
    const std::vector<BoneChannels> channels = bo2::engine::bind_channels(skeleton, reel);
    CHECK(channels.size() == 3);
    CHECK(channels[1].rotation[0] == -1 && channels[1].rotation[1] == 1);
    CHECK(channels[1].translation[2] == 2 && channels[1].translation[0] == -1);
    CHECK(channels[2].scale[0] == 3 && channels[2].scale[1] == -1);
    CHECK(channels[0].rotation[0] == -1 && channels[0].translation[0] == -1);
}

void test_missing_channels_give_the_rest_pose() {
    const ModelSkeleton skeleton = chain();
    const AnimReel reel = reel_of({});
    const std::vector<BoneTransform> pose =
        bo2::engine::sample_pose(skeleton, reel, bo2::engine::bind_channels(skeleton, reel), 0.0f);
    // FUN_00637900: translation falls back to the pivot, rotation to 0, scale to 1.
    CHECK(near(pose[2].translation, 0.0f, 0.0f, 20.0f));
    CHECK(near(pose[2].rotation, 0.0f, 0.0f, 0.0f));
    CHECK(near(pose[2].scale, 1.0f, 1.0f, 1.0f));
    const std::vector<Vec3> positions = skin(skeleton, reel);
    for (std::size_t index = 0; index < kBind.size(); ++index) {
        CHECK(near(positions[index], kBind[index].x, kBind[index].y, kBind[index].z, 1.0e-6f));
    }
}

void test_rotation_turns_about_the_pivot_and_carries_the_child() {
    const ModelSkeleton skeleton = chain();
    const std::vector<Vec3> positions = skin(skeleton, reel_of({constant(10, 0, 1, kHalfPi)}));
    // FUN_00692cb0: (x, z) -> (x c + z s, z c - x s) about the pivot (0, 0, 10).
    CHECK(near(positions[0], 0.0f, 0.0f, 10.0f));
    CHECK(near(positions[1], 5.0f, 0.0f, 10.0f));
    CHECK(near(positions[2], 10.0f, 0.0f, 10.0f));
    CHECK(near(positions[3], 15.0f, 0.0f, 10.0f));
}

void test_rotation_order_is_x_then_y_then_z() {
    SkeletonBone joint = bone(10, 1, 0, {0.0f, 0.0f, 0.0f}, 0, 1);
    BoneTransform transform;
    transform.rotation = {kHalfPi, kHalfPi, 0.0f};
    // x first: (0, 1, 0) -> (0, 0, 1); then y: -> (1, 0, 0). The other order
    // would leave (0, 0, 1).
    Vec3 turned = bo2::engine::transform_point(
        bo2::engine::local_matrix(joint, transform, nullptr), {0.0f, 1.0f, 0.0f});
    CHECK(near(turned, 1.0f, 0.0f, 0.0f));
    // FUN_00692eb0: (x, y) -> (x c - y s, y c + x s), after x and y.
    transform.rotation = {0.0f, kHalfPi, kHalfPi};
    turned = bo2::engine::transform_point(bo2::engine::local_matrix(joint, transform, nullptr),
                                          {0.0f, 0.0f, 1.0f});
    CHECK(near(turned, 0.0f, 1.0f, 0.0f));
    // FUN_00692aa0: (y, z) -> (y c - z s, y s + z c).
    transform.rotation = {kHalfPi, 0.0f, 0.0f};
    turned = bo2::engine::transform_point(bo2::engine::local_matrix(joint, transform, nullptr),
                                          {0.0f, 0.0f, 1.0f});
    CHECK(near(turned, 0.0f, -1.0f, 0.0f));
}

void test_tiny_rotations_are_skipped() {
    const SkeletonBone joint = bone(10, 1, 0, {1.0f, 2.0f, 3.0f}, 0, 1);
    BoneTransform transform;
    transform.translation = joint.pivot;
    transform.rotation = {5.0e-5f, -5.0e-5f, 5.0e-5f};  // below the 1e-4 threshold
    const Mat4 skipped = bo2::engine::local_matrix(joint, transform, nullptr);
    const Mat4 identity = Mat4::identity();
    for (int index = 0; index < 16; ++index) {
        CHECK(skipped.m[index] == identity.m[index]);
    }
    transform.rotation = {2.0e-4f, 0.0f, 0.0f};
    CHECK(bo2::engine::local_matrix(joint, transform, nullptr).m[6] != 0.0f);
}

void test_translation_channel_moves_the_joint() {
    const ModelSkeleton skeleton = chain();
    // Only z has a channel; x and y keep the pivot.
    const std::vector<Vec3> positions = skin(skeleton, reel_of({constant(10, 1, 2, 12.0f)}));
    CHECK(near(positions[0], 0.0f, 0.0f, 12.0f));
    CHECK(near(positions[1], 0.0f, 0.0f, 17.0f));
    CHECK(near(positions[3], 0.0f, 0.0f, 27.0f));
}

void test_root_channels_do_not_move_the_skeleton() {
    const ModelSkeleton skeleton = chain();
    // The bone of index 0 gets an identity matrix (0x639160): root motion and
    // a root rotation leave the mesh where it is.
    const std::vector<Vec3> positions =
        skin(skeleton, reel_of({constant(0, 0, 2, kHalfPi), constant(0, 1, 0, 160.0f),
                                constant(0, 2, 0, 3.0f)}));
    for (std::size_t index = 0; index < kBind.size(); ++index) {
        CHECK(near(positions[index], kBind[index].x, kBind[index].y, kBind[index].z, 1.0e-6f));
    }
}

void test_scale_and_the_parent_scale_bit() {
    const AnimReel doubled = reel_of({constant(10, 2, 0, 2.0f), constant(10, 2, 1, 2.0f),
                                      constant(10, 2, 2, 2.0f)});
    // The child carries 0x200: it keeps the parent's scale.
    ModelSkeleton skeleton = chain();
    std::vector<Vec3> positions = skin(skeleton, doubled);
    CHECK(near(positions[1], 0.0f, 0.0f, 20.0f));
    CHECK(near(positions[2], 0.0f, 0.0f, 30.0f));
    CHECK(near(positions[3], 0.0f, 0.0f, 40.0f));
    // Without the bit the child undoes it: its joint moves, its length does not.
    skeleton.bones[2].channel_mask = 0x007;
    positions = skin(skeleton, doubled);
    CHECK(near(positions[2], 0.0f, 0.0f, 30.0f));
    CHECK(near(positions[3], 0.0f, 0.0f, 35.0f));
    // FUN_00637710 stores 1 / s from 0.01 up and 100 below.
    CHECK(near(bo2::engine::inverse_bone_scale({1.0f, 4.0f, 0.01f}), 1.0f, 0.25f, 100.0f, 1.0e-3f));
    CHECK(near(bo2::engine::inverse_bone_scale({0.001f, -2.0f, 0.5f}), 100.0f, 100.0f, 2.0f));
    const AnimReel tiny = reel_of({constant(10, 2, 2, 0.001f)});
    positions = skin(skeleton, tiny);
    // (25 - 20) * 100 + 20 = 520 in the parent, (520 - 10) * 0.001 + 10 there.
    CHECK(near(positions[3], 0.0f, 0.0f, 10.51f));
}

void test_second_joint_blend() {
    ModelSkeleton skeleton = chain();
    // Vertex 1 belongs to the lower bone and leans half on the upper one.
    skeleton.joints[1] = 2;
    skeleton.weights[1] = 0.5f;
    const std::vector<Vec3> positions = skin(skeleton, reel_of({constant(20, 1, 2, 24.0f)}));
    // FUN_0056ecf0: a = (0, 0, 15), b = (0, 0, 19), a + (b - a) * 0.5.
    CHECK(near(positions[1], 0.0f, 0.0f, 17.0f));
    CHECK(near(positions[0], 0.0f, 0.0f, 10.0f));
    CHECK(near(positions[3], 0.0f, 0.0f, 29.0f));
}

void test_fractional_frame_and_normals() {
    const ModelSkeleton skeleton = chain();
    AnimChannel turn;
    turn.node = 10;
    turn.type = 0;
    turn.component = 1;
    turn.samples = {0.0f, kHalfPi};
    const AnimReel reel = reel_of({turn}, 2);
    const std::vector<BoneChannels> channels = bo2::engine::bind_channels(skeleton, reel);
    const std::vector<BoneTransform> pose = bo2::engine::sample_pose(skeleton, reel, channels, 0.5f);
    CHECK(std::fabs(pose[1].rotation.y - kHalfPi * 0.5f) < 1.0e-6f);
    const std::vector<Mat4> matrices = bo2::engine::bone_matrices(
        skeleton, bo2::engine::sample_pose(skeleton, reel, channels, 1.0f));
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    const std::vector<Vec3> bind_normals(kBind.size(), Vec3{0.0f, 0.0f, 1.0f});
    bo2::engine::skin_vertices(skeleton, matrices, kBind, bind_normals, positions, normals);
    // The port turns the normal with the bone (the original emits none).
    CHECK(normals.size() == kBind.size());
    CHECK(near(normals[1], 1.0f, 0.0f, 0.0f));
    CHECK(near(normals[3], 1.0f, 0.0f, 0.0f));
}

void test_malformed_skeletons() {
    // Ranges and joints outside the arrays are clamped or ignored.
    ModelSkeleton skeleton = chain();
    skeleton.bones[2].vertex_count = 99;
    skeleton.bones[1].first_vertex = 500;
    skeleton.joints[3] = 77;
    skeleton.weights[3] = 0.5f;
    const std::vector<Vec3> positions = skin(skeleton, reel_of({constant(20, 1, 2, 24.0f)}));
    CHECK(positions.size() == kBind.size());
    CHECK(near(positions[3], 0.0f, 0.0f, 29.0f));
    CHECK(near(positions[1], 0.0f, 0.0f, 15.0f));  // outside every range: bind position
    // A parent cycle is rejected; a missing parent is a root.
    ModelSkeleton cycle = chain();
    cycle.bones[1].parent_matrix = 2;
    bool thrown = false;
    try {
        bo2::engine::bone_matrices(cycle, std::vector<BoneTransform>(3));
    } catch (const bo2::common::ParseError&) {
        thrown = true;
    }
    CHECK(thrown);
    ModelSkeleton orphan = chain();
    orphan.bones[1].parent_matrix = 40;
    const std::vector<Mat4> matrices =
        bo2::engine::bone_matrices(orphan, std::vector<BoneTransform>(3));
    CHECK(matrices[1].m[0] == 1.0f && matrices[1].m[11] == 0.0f);
    // A child listed before its parent still gets the parent's matrix.
    ModelSkeleton reversed;
    reversed.bones = {bone(0, 0, kNoParent, {0.0f, 0.0f, 0.0f}, 0, 0, 0x3f),
                      bone(20, 1, 2, {0.0f, 0.0f, 20.0f}, 0, 1),
                      bone(10, 2, 0, {0.0f, 0.0f, 10.0f}, 1, 1)};
    std::vector<BoneTransform> pose(3);
    pose[1].translation = {0.0f, 0.0f, 20.0f};
    pose[2].translation = {0.0f, 0.0f, 13.0f};
    const std::vector<Mat4> ordered = bo2::engine::bone_matrices(reversed, pose);
    CHECK(near(bo2::engine::transform_point(ordered[1], {0.0f, 0.0f, 25.0f}), 0.0f, 0.0f, 28.0f));
}

void test_actor_is_posed_and_placed() {
    bo2::engine::SkinnedActor actor;
    actor.skeleton = chain();
    actor.bind_positions = kBind;
    actor.bind_normals.assign(kBind.size(), Vec3{1.0f, 0.0f, 0.0f});
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    // No clip: the bind pose, moved to the position.
    bo2::engine::pose_actor(actor, 0.0f, {100.0f, 200.0f, 300.0f}, 0.0f, positions, normals);
    CHECK(near(positions[3], 100.0f, 200.0f, 325.0f));
    CHECK(near(normals[3], 1.0f, 0.0f, 0.0f));
    // A quarter turn of yaw takes local x (forward) to y; the clip bends the
    // chain forward first.
    actor.set_clip(reel_of({constant(10, 0, 1, kHalfPi)}));
    CHECK(actor.channels.size() == 3 && actor.channels[1].rotation[1] == 0);
    bo2::engine::pose_actor(actor, 0.0f, {100.0f, 200.0f, 300.0f}, kHalfPi, positions, normals);
    CHECK(near(positions[3], 100.0f, 215.0f, 310.0f));
    CHECK(near(positions[0], 100.0f, 200.0f, 310.0f));
}

void test_blend_poses() {
    const SkeletonBone joint = bone(10, 1, 0, {0.0f, 0.0f, 0.0f}, 0, 1);
    std::vector<BoneTransform> from(1);
    std::vector<BoneTransform> to(1);
    from[0].translation = {0.0f, 0.0f, 0.0f};
    to[0].translation = {4.0f, 0.0f, 8.0f};
    to[0].scale = {3.0f, 1.0f, 1.0f};
    to[0].rotation = {0.0f, 0.0f, kHalfPi};
    // The ends are the two poses; the translation and the scale are linear.
    std::vector<BoneTransform> pose = bo2::engine::blend_poses(from, to, 0.0f);
    CHECK(near(pose[0].translation, 0.0f, 0.0f, 0.0f) && near(pose[0].scale, 1.0f, 1.0f, 1.0f));
    CHECK(near(bo2::engine::transform_point(bo2::engine::local_matrix(joint, pose[0], nullptr),
                                            {1.0f, 0.0f, 0.0f}),
               1.0f, 0.0f, 0.0f));
    pose = bo2::engine::blend_poses(from, to, 1.0f);
    CHECK(near(pose[0].translation, 4.0f, 0.0f, 8.0f) && near(pose[0].scale, 3.0f, 1.0f, 1.0f));
    pose = bo2::engine::blend_poses(from, to, 0.5f);
    CHECK(near(pose[0].translation, 2.0f, 0.0f, 4.0f) && near(pose[0].scale, 2.0f, 1.0f, 1.0f));
    CHECK(pose[0].has_orientation);
    // Half of a quarter turn about z: (1, 0, 0) scaled by 2, turned 45 degrees.
    const float half = std::sqrt(0.5f);
    CHECK(near(bo2::engine::transform_point(bo2::engine::local_matrix(joint, pose[0], nullptr),
                                            {1.0f, 0.0f, 0.0f}),
               2.0f * half + 2.0f, 2.0f * half, 4.0f));
    // A quaternion pose gives the matrix the three angles give.
    BoneTransform angles;
    angles.rotation = {0.3f, -0.5f, 1.1f};
    const std::vector<BoneTransform> same =
        bo2::engine::blend_poses({angles}, {angles}, 0.37f);
    const Vec3 by_angles = bo2::engine::transform_point(
        bo2::engine::local_matrix(joint, angles, nullptr), {1.0f, 2.0f, 3.0f});
    const Vec3 by_quaternion = bo2::engine::transform_point(
        bo2::engine::local_matrix(joint, same[0], nullptr), {1.0f, 2.0f, 3.0f});
    CHECK(near(by_quaternion, by_angles.x, by_angles.y, by_angles.z, 1.0e-4f));
    // The shorter arc (FUN_0069b5b0): from 170 to -170 degrees about z the
    // middle is 180 degrees, not 0.
    BoneTransform left;
    BoneTransform right;
    left.rotation = {0.0f, 0.0f, 170.0f * kHalfPi / 90.0f};
    right.rotation = {0.0f, 0.0f, -170.0f * kHalfPi / 90.0f};
    const std::vector<BoneTransform> middle = bo2::engine::blend_poses({left}, {right}, 0.5f);
    CHECK(near(bo2::engine::transform_point(bo2::engine::local_matrix(joint, middle[0], nullptr),
                                            {1.0f, 0.0f, 0.0f}),
               -1.0f, 0.0f, 0.0f, 1.0e-3f));
    // A blended pose can be the start of the next blend, and a bone the first
    // pose lacks takes the second.
    const std::vector<BoneTransform> again = bo2::engine::blend_poses(pose, to, 1.0f);
    CHECK(near(again[0].translation, 4.0f, 0.0f, 8.0f));
    const std::vector<BoneTransform> longer(2, to[0]);
    CHECK(bo2::engine::blend_poses(from, longer, 0.5f).size() == 2);
    CHECK(near(bo2::engine::blend_poses(from, longer, 0.5f)[1].translation, 4.0f, 0.0f, 8.0f));
}

void test_loop_frame() {
    AnimReel reel = reel_of({}, 25);  // 24 frame steps: 0.8 s
    CHECK(bo2::engine::loop_frame(reel, 0.0) == 0.0f);
    CHECK(std::fabs(bo2::engine::loop_frame(reel, 0.1) - 3.0f) < 1.0e-4f);
    CHECK(std::fabs(bo2::engine::loop_frame(reel, 0.9) - 3.0f) < 1.0e-4f);  // wrapped
    CHECK(std::fabs(bo2::engine::loop_frame(reel, -0.1) - 21.0f) < 1.0e-4f);
    reel.frames = 1;
    CHECK(bo2::engine::loop_frame(reel, 5.0) == 0.0f);
}

}  // namespace

int main() {
    test_actor_is_posed_and_placed();
    test_blend_poses();
    test_loop_frame();
    test_bind_uses_the_bone_id();
    test_missing_channels_give_the_rest_pose();
    test_rotation_turns_about_the_pivot_and_carries_the_child();
    test_rotation_order_is_x_then_y_then_z();
    test_tiny_rotations_are_skipped();
    test_translation_channel_moves_the_joint();
    test_root_channels_do_not_move_the_skeleton();
    test_scale_and_the_parent_scale_bit();
    test_second_joint_blend();
    test_fractional_frame_and_normals();
    test_malformed_skeletons();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "skeleton_pose tests: ok\n";
    return 0;
}
