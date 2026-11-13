#pragma once

#include <cstdint>
#include <vector>

#include "bo2/engine/anim_reel.hpp"
#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/math.hpp"

namespace bo2::engine {

// Skeleton pose evaluation and skinning (docs/FORMATS/models.md, "Skeleton
// pose and skinning"). The original works on row vectors; `Mat4` here applies
// to column vectors, so every product below is written in the reverse order.

// `chanMask` bit of a bone record. FUN_005e27e0 copies the mask to the runtime
// record and FUN_00637710 tests this bit: a bone that carries it keeps its
// parent's scale; a bone without it undoes the parent's scale first.
constexpr std::uint16_t kBoneKeepsParentScale = 0x200;

// The nine channels the clip sampler binds to one bone (FUN_00638920), as
// indices into `AnimReel::channels`; -1 when the clip has no such channel.
struct BoneChannels {
    std::int32_t rotation[3] = {-1, -1, -1};     // descriptor type 0
    std::int32_t translation[3] = {-1, -1, -1};  // descriptor type 1
    std::int32_t scale[3] = {-1, -1, -1};        // descriptor type 2
};

// One bone's sampled values (FUN_00637900). The rotation is in radians.
struct BoneTransform {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

// Binds the clip to the skeleton, one entry per bone in file order. A channel
// belongs to the bone whose id (`bone_index`) is the channel's node; a node
// that no bone has is never looked up.
std::vector<BoneChannels> bind_channels(const ModelSkeleton& skeleton, const AnimReel& reel);

// The values of every bone at a frame (fractional frames interpolate). A
// missing channel takes its default: the bone pivot for a translation, 0 for a
// rotation and 1 for a scale, so a clip without channels gives the rest pose.
std::vector<BoneTransform> sample_pose(const ModelSkeleton& skeleton, const AnimReel& reel,
                                       const std::vector<BoneChannels>& channels, float frame);

// The inverse scale FUN_00637710 stores for a bone's children: 1 for a scale
// of exactly 1, 1 / s from 0.01 up and 100 below that.
Vec3 inverse_bone_scale(const Vec3& scale);

// The matrix of a bone in its parent's space (FUN_00637710). On row vectors
// the original builds T(-pivot) * S * Rx * Ry * Rz * S(parent inverse) * T(t);
// a rotation below 1e-4 rad is skipped. `parent_inverse_scale` is null for a
// bone that keeps its parent's scale or has no parent.
Mat4 local_matrix(const SkeletonBone& bone, const BoneTransform& transform,
                  const Vec3* parent_inverse_scale);

// The model-space matrix of every bone, in file order (FUN_005e43e0): the
// local matrix times the parent's. The bone of index 0 and any bone without a
// parent get the identity (the original gives them the entity matrix), so the
// root's channels never move the skeleton. A parent is a position in the bone
// array. Throws ParseError when the parent links form a cycle.
std::vector<Mat4> bone_matrices(const ModelSkeleton& skeleton,
                                const std::vector<BoneTransform>& pose);

// Skins the bind pose (FUN_0056cc20, FUN_0056ecf0). A vertex belongs to the
// bone whose `first_vertex`/`vertex_count` range holds it; `joints` names a
// second bone and `weights` its weight: v' = a + (b - a) * w with a and b the
// vertex under the owner's and the second bone's matrices. Vertices outside
// every range keep their bind position. The original emits no normal (it
// lights the vertex on the CPU); the port blends the normal with the same
// matrices and weight for its own shading. `normals` may be empty.
void skin_vertices(const ModelSkeleton& skeleton, const std::vector<Mat4>& matrices,
                   const std::vector<Vec3>& bind_positions,
                   const std::vector<Vec3>& bind_normals, std::vector<Vec3>& positions,
                   std::vector<Vec3>& normals);

// A skinned model and the clip that poses it, for the host: the bind pose in
// model space, the skeleton and the clip bound to it. Without a clip the
// actor stays in its bind pose.
struct SkinnedActor {
    ModelSkeleton skeleton;
    std::vector<Vec3> bind_positions;
    std::vector<Vec3> bind_normals;
    AnimReel reel;
    std::vector<BoneChannels> channels;

    void set_clip(AnimReel clip);
};

// Skins the actor at a frame and places it in the world: a yaw about z (local
// x is forward), then `position`. This stands in for the entity matrix that
// the original gives the parentless bone.
void pose_actor(const SkinnedActor& actor, float frame, const Vec3& position, float yaw,
                std::vector<Vec3>& positions, std::vector<Vec3>& normals);

// The frame of a looping clip after `seconds` at 30 frames per second (the
// 1/30 constant of the reel duration), wrapped at the last frame. The loop is
// the host's playback rule; the clip player's own loop and hold flags are not
// ported.
float loop_frame(const AnimReel& reel, double seconds);

}  // namespace bo2::engine
