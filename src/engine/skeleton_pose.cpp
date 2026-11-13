#include "bo2/engine/skeleton_pose.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

constexpr float kMinimumRotation = 1.0e-4f;  // float at 0x87623c (FUN_00692aa0)
constexpr float kMinimumScale = 0.01f;       // float at 0x841c08 (FUN_00637710)
constexpr float kTinyScaleInverse = 100.0f;  // FUN_00637710
constexpr std::uint32_t kNoParent = 0xFFFFFFFFU;

Mat4 translation(const Vec3& offset) {
    Mat4 result = Mat4::identity();
    result.m[3] = offset.x;
    result.m[7] = offset.y;
    result.m[11] = offset.z;
    return result;
}

Mat4 scaling(const Vec3& scale) {
    Mat4 result = Mat4::identity();
    result.m[0] = scale.x;
    result.m[5] = scale.y;
    result.m[10] = scale.z;
    return result;
}

// FUN_00692aa0, FUN_00692cb0 and FUN_00692eb0 leave the matrix alone below
// this angle.
bool rotates(float angle) {
    return angle >= kMinimumRotation || angle <= -kMinimumRotation;
}

// The three rotations as they act on a column vector. FUN_00692aa0 turns
// (y, z) into (y c - z s, y s + z c), FUN_00692cb0 turns (x, z) into
// (x c + z s, z c - x s) and FUN_00692eb0 turns (x, y) into
// (x c - y s, y c + x s).
Mat4 rotation_x(float angle) {
    Mat4 result = Mat4::identity();
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    result.m[5] = cosine;
    result.m[6] = -sine;
    result.m[9] = sine;
    result.m[10] = cosine;
    return result;
}

Mat4 rotation_y(float angle) {
    Mat4 result = Mat4::identity();
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    result.m[0] = cosine;
    result.m[2] = sine;
    result.m[8] = -sine;
    result.m[10] = cosine;
    return result;
}

Mat4 rotation_z(float angle) {
    Mat4 result = Mat4::identity();
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    result.m[0] = cosine;
    result.m[1] = -sine;
    result.m[4] = sine;
    result.m[5] = cosine;
    return result;
}

bool is_unit(const Vec3& value) {
    return value.x == 1.0f && value.y == 1.0f && value.z == 1.0f;
}

float inverse_component(float scale) {
    if (scale == 1.0f) {
        return 1.0f;
    }
    return scale >= kMinimumScale ? 1.0f / scale : kTinyScaleInverse;
}

float channel_value(const AnimReel& reel, std::int32_t channel, float frame, float fallback) {
    if (channel < 0 || static_cast<std::size_t>(channel) >= reel.channels.size()) {
        return fallback;
    }
    return reel.channels[static_cast<std::size_t>(channel)].sample(frame);
}

bool has_parent(const ModelSkeleton& skeleton, std::size_t index) {
    const std::uint32_t parent = skeleton.bones[index].parent_matrix;
    return index != 0 && parent != kNoParent && parent < skeleton.bones.size();
}

}  // namespace

std::vector<BoneChannels> bind_channels(const ModelSkeleton& skeleton, const AnimReel& reel) {
    std::vector<BoneChannels> result(skeleton.bones.size());
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index) {
        // The runtime key keeps the bone id in its low word (FUN_005e27e0).
        const std::uint16_t node = static_cast<std::uint16_t>(skeleton.bones[index].bone_index);
        BoneChannels& bound = result[index];
        for (std::uint8_t component = 0; component < 3; ++component) {
            bound.rotation[component] = find_channel(reel, node, 0, component);
            bound.translation[component] = find_channel(reel, node, 1, component);
            bound.scale[component] = find_channel(reel, node, 2, component);
        }
    }
    return result;
}

std::vector<BoneTransform> sample_pose(const ModelSkeleton& skeleton, const AnimReel& reel,
                                       const std::vector<BoneChannels>& channels, float frame) {
    std::vector<BoneTransform> pose(skeleton.bones.size());
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index) {
        const SkeletonBone& bone = skeleton.bones[index];
        const BoneChannels bound = index < channels.size() ? channels[index] : BoneChannels{};
        BoneTransform& transform = pose[index];
        transform.translation = {
            channel_value(reel, bound.translation[0], frame, bone.pivot.x),
            channel_value(reel, bound.translation[1], frame, bone.pivot.y),
            channel_value(reel, bound.translation[2], frame, bone.pivot.z)};
        transform.rotation = {channel_value(reel, bound.rotation[0], frame, 0.0f),
                              channel_value(reel, bound.rotation[1], frame, 0.0f),
                              channel_value(reel, bound.rotation[2], frame, 0.0f)};
        transform.scale = {channel_value(reel, bound.scale[0], frame, 1.0f),
                           channel_value(reel, bound.scale[1], frame, 1.0f),
                           channel_value(reel, bound.scale[2], frame, 1.0f)};
    }
    return pose;
}

Vec3 inverse_bone_scale(const Vec3& scale) {
    return {inverse_component(scale.x), inverse_component(scale.y), inverse_component(scale.z)};
}

Mat4 local_matrix(const SkeletonBone& bone, const BoneTransform& transform,
                  const Vec3* parent_inverse_scale) {
    // Row order of FUN_00637710, applied right to left on a column vector.
    Mat4 result = translation(-bone.pivot);
    if (!is_unit(transform.scale)) {
        result = scaling(transform.scale) * result;
    }
    if (rotates(transform.rotation.x)) {
        result = rotation_x(transform.rotation.x) * result;
    }
    if (rotates(transform.rotation.y)) {
        result = rotation_y(transform.rotation.y) * result;
    }
    if (rotates(transform.rotation.z)) {
        result = rotation_z(transform.rotation.z) * result;
    }
    if (parent_inverse_scale != nullptr && !is_unit(*parent_inverse_scale)) {
        result = scaling(*parent_inverse_scale) * result;
    }
    return translation(transform.translation) * result;
}

std::vector<Mat4> bone_matrices(const ModelSkeleton& skeleton,
                                const std::vector<BoneTransform>& pose) {
    const std::size_t count = skeleton.bones.size();
    std::vector<Mat4> matrices(count, Mat4::identity());
    std::vector<Vec3> inverse_scales(count, Vec3{1.0f, 1.0f, 1.0f});
    enum : std::uint8_t { kPending, kOpen, kDone };
    std::vector<std::uint8_t> state(count, kPending);
    std::vector<std::size_t> chain;
    for (std::size_t start = 0; start < count; ++start) {
        // The original recurses to the parent first (FUN_005e43e0); walk up
        // to the first finished bone, then come back down.
        chain.clear();
        for (std::size_t index = start; state[index] != kDone;) {
            if (state[index] == kOpen) {
                throw common::ParseError("skeleton parent links form a cycle");
            }
            state[index] = kOpen;
            chain.push_back(index);
            if (!has_parent(skeleton, index)) {
                break;
            }
            index = skeleton.bones[index].parent_matrix;
        }
        for (std::size_t step = chain.size(); step-- > 0;) {
            const std::size_t index = chain[step];
            state[index] = kDone;
            if (!has_parent(skeleton, index) || index >= pose.size()) {
                continue;  // identity, inverse scale 1
            }
            const SkeletonBone& bone = skeleton.bones[index];
            const std::size_t parent = bone.parent_matrix;
            const bool keeps_scale = (bone.channel_mask & kBoneKeepsParentScale) != 0;
            inverse_scales[index] = inverse_bone_scale(pose[index].scale);
            const Mat4 local =
                local_matrix(bone, pose[index], keeps_scale ? nullptr : &inverse_scales[parent]);
            matrices[index] = matrices[parent] * local;
        }
    }
    return matrices;
}

void skin_vertices(const ModelSkeleton& skeleton, const std::vector<Mat4>& matrices,
                   const std::vector<Vec3>& bind_positions,
                   const std::vector<Vec3>& bind_normals, std::vector<Vec3>& positions,
                   std::vector<Vec3>& normals) {
    positions = bind_positions;
    normals = bind_normals;
    const std::size_t vertices = bind_positions.size();
    for (std::size_t index = 0; index < skeleton.bones.size() && index < matrices.size();
         ++index) {
        const SkeletonBone& bone = skeleton.bones[index];
        const Mat4& owner = matrices[index];
        const std::size_t first = bone.first_vertex;
        if (first >= vertices) {
            continue;
        }
        const std::size_t last =
            first + std::min<std::size_t>(bone.vertex_count, vertices - first);
        for (std::size_t vertex = first; vertex < last; ++vertex) {
            const float weight = vertex < skeleton.weights.size() ? skeleton.weights[vertex] : 0.0f;
            const std::size_t joint =
                vertex < skeleton.joints.size() ? skeleton.joints[vertex] : index;
            const bool blend = weight != 0.0f && joint < matrices.size();
            const Vec3 position = transform_point(owner, bind_positions[vertex]);
            positions[vertex] =
                blend ? position +
                            (transform_point(matrices[joint], bind_positions[vertex]) - position) *
                                weight
                      : position;
            if (vertex < bind_normals.size()) {
                const Vec3 normal = transform_direction(owner, bind_normals[vertex]);
                normals[vertex] = normalize(
                    blend ? normal + (transform_direction(matrices[joint], bind_normals[vertex]) -
                                      normal) *
                                         weight
                          : normal);
            }
        }
    }
}

}  // namespace bo2::engine
