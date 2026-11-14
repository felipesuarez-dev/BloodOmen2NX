#include "bo2/engine/skeleton_pose.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

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

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

constexpr float kSlerpMinimum = 1.0e-5f;    // float at 0x84a92c (FUN_0069b5b0)
constexpr float kLinearBlendDot = 0.99999f;  // float at 0x87625c (FUN_0069b640)

// The quaternion of a rotation matrix (FUN_0069aa30: the trace, else the
// largest diagonal term).
Quat quat_from_matrix(const Mat4& matrix) {
    const float* m = matrix.m;
    Quat q;
    const float trace = m[0] + m[5] + m[10];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m[9] - m[6]) / s;
        q.y = (m[2] - m[8]) / s;
        q.z = (m[4] - m[1]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        q.w = (m[9] - m[6]) / s;
        q.x = 0.25f * s;
        q.y = (m[1] + m[4]) / s;
        q.z = (m[2] + m[8]) / s;
    } else if (m[5] > m[10]) {
        const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        q.w = (m[2] - m[8]) / s;
        q.x = (m[1] + m[4]) / s;
        q.y = 0.25f * s;
        q.z = (m[6] + m[9]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        q.w = (m[4] - m[1]) / s;
        q.x = (m[2] + m[8]) / s;
        q.y = (m[6] + m[9]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

// FUN_00690d20: the rotation matrix of a quaternion.
Mat4 matrix_from_quat(const Quat& q) {
    Mat4 result = Mat4::identity();
    result.m[0] = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    result.m[1] = 2.0f * (q.x * q.y - q.z * q.w);
    result.m[2] = 2.0f * (q.x * q.z + q.y * q.w);
    result.m[4] = 2.0f * (q.x * q.y + q.z * q.w);
    result.m[5] = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    result.m[6] = 2.0f * (q.y * q.z - q.x * q.w);
    result.m[8] = 2.0f * (q.x * q.z - q.y * q.w);
    result.m[9] = 2.0f * (q.y * q.z + q.x * q.w);
    result.m[10] = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
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

namespace {

// The rotation of a transform as a quaternion: the blend sampler turns the
// three angles into a matrix (x, then y, then z) and that into a quaternion.
Quat orientation_of(const BoneTransform& transform) {
    if (transform.has_orientation) {
        return {transform.orientation[0], transform.orientation[1], transform.orientation[2],
                transform.orientation[3]};
    }
    Mat4 turn = Mat4::identity();
    if (rotates(transform.rotation.x)) {
        turn = rotation_x(transform.rotation.x) * turn;
    }
    if (rotates(transform.rotation.y)) {
        turn = rotation_y(transform.rotation.y) * turn;
    }
    if (rotates(transform.rotation.z)) {
        turn = rotation_z(transform.rotation.z) * turn;
    }
    return quat_from_matrix(turn);
}

Vec3 mix(const Vec3& from, const Vec3& to, float factor) {
    return from + (to - from) * factor;
}

}  // namespace

std::vector<BoneTransform> blend_poses(const std::vector<BoneTransform>& from,
                                       const std::vector<BoneTransform>& to, float factor) {
    std::vector<BoneTransform> result(to.size());
    for (std::size_t index = 0; index < to.size(); ++index) {
        if (index >= from.size()) {
            result[index] = to[index];
            continue;
        }
        BoneTransform& blended = result[index];
        blended.translation = mix(from[index].translation, to[index].translation, factor);
        blended.scale = mix(from[index].scale, to[index].scale, factor);
        const Quat first = orientation_of(from[index]);
        Quat second = orientation_of(to[index]);
        // FUN_0069b5b0: take the shorter arc.
        float cosine =
            first.x * second.x + first.y * second.y + first.z * second.z + first.w * second.w;
        if (cosine < 0.0f) {
            second = {-second.x, -second.y, -second.z, -second.w};
            cosine = -cosine;
        }
        // FUN_0069b640: linear weights near a dot product of 1, spherical below.
        float first_weight = 1.0f - factor;
        float second_weight = factor;
        if (cosine < kLinearBlendDot && 1.0f - cosine > kSlerpMinimum) {
            const float angle = std::acos(cosine);
            const float inverse_sine = 1.0f / std::sin(angle);
            first_weight = std::sin((1.0f - factor) * angle) * inverse_sine;
            second_weight = std::sin(factor * angle) * inverse_sine;
        }
        Quat out{first_weight * first.x + second_weight * second.x,
                 first_weight * first.y + second_weight * second.y,
                 first_weight * first.z + second_weight * second.z,
                 first_weight * first.w + second_weight * second.w};
        const float norm = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w);
        if (norm > 0.0f) {
            out = {out.x / norm, out.y / norm, out.z / norm, out.w / norm};
        } else {
            out = second;
        }
        blended.has_orientation = true;
        blended.orientation[0] = out.x;
        blended.orientation[1] = out.y;
        blended.orientation[2] = out.z;
        blended.orientation[3] = out.w;
    }
    return result;
}

std::vector<BoneTransform> cross_blend_poses(const std::vector<BoneTransform>& first,
                                             float first_weight,
                                             const std::vector<BoneTransform>& second,
                                             float second_weight) {
    constexpr float kMinimumWeight = 1.0e-4f;  // float at 0x86664c
    std::vector<BoneTransform> result(first.size());
    for (BoneTransform& transform : result) {
        transform.scale = {0.0f, 0.0f, 0.0f};
    }
    const auto add = [&](const std::vector<BoneTransform>& pose, float weight) {
        if (weight < kMinimumWeight) {
            return;
        }
        for (std::size_t index = 0; index < result.size() && index < pose.size(); ++index) {
            result[index].translation = result[index].translation + pose[index].translation * weight;
            result[index].rotation = result[index].rotation + pose[index].rotation * weight;
            result[index].scale = result[index].scale + pose[index].scale * weight;
        }
    };
    add(first, first_weight);
    add(second, second_weight);
    return result;
}

Mat4 local_matrix(const SkeletonBone& bone, const BoneTransform& transform,
                  const Vec3* parent_inverse_scale) {
    // Row order of FUN_00637710, applied right to left on a column vector.
    Mat4 result = translation(-bone.pivot);
    if (!is_unit(transform.scale)) {
        result = scaling(transform.scale) * result;
    }
    if (transform.has_orientation) {
        // FUN_00638310: the same order with the blended quaternion as the rotation.
        result = matrix_from_quat({transform.orientation[0], transform.orientation[1],
                                   transform.orientation[2], transform.orientation[3]}) *
                 result;
    } else {
        if (rotates(transform.rotation.x)) {
            result = rotation_x(transform.rotation.x) * result;
        }
        if (rotates(transform.rotation.y)) {
            result = rotation_y(transform.rotation.y) * result;
        }
        if (rotates(transform.rotation.z)) {
            result = rotation_z(transform.rotation.z) * result;
        }
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

void SkinnedActor::set_clip(AnimReel clip) {
    reel = std::move(clip);
    channels = bind_channels(skeleton, reel);
}

void pose_actor(const SkinnedActor& actor, float frame, const Vec3& position, float yaw,
                std::vector<Vec3>& positions, std::vector<Vec3>& normals) {
    pose_actor(actor, actor.reel, actor.channels, frame, position, yaw, positions, normals);
}

void pose_actor(const SkinnedActor& actor, const AnimReel& reel,
                const std::vector<BoneChannels>& channels, float frame, const Vec3& position,
                float yaw, std::vector<Vec3>& positions, std::vector<Vec3>& normals) {
    place_pose(actor, sample_pose(actor.skeleton, reel, channels, frame), position, yaw,
               positions, normals);
}

void place_pose(const SkinnedActor& actor, const std::vector<BoneTransform>& pose,
                const Vec3& position, float yaw, std::vector<Vec3>& positions,
                std::vector<Vec3>& normals) {
    const std::vector<Mat4> matrices = bone_matrices(actor.skeleton, pose);
    skin_vertices(actor.skeleton, matrices, actor.bind_positions, actor.bind_normals, positions,
                  normals);
    const Mat4 turn = rotation_z(yaw);
    for (Vec3& value : positions) {
        value = transform_direction(turn, value) + position;
    }
    for (Vec3& value : normals) {
        value = transform_direction(turn, value);
    }
}

float loop_frame(const AnimReel& reel, double seconds) {
    if (reel.frames <= 1) {
        return 0.0f;
    }
    const double last = static_cast<double>(reel.frames - 1);
    double frame = std::fmod(seconds * 30.0, last);
    if (frame < 0.0) {
        frame += last;
    }
    return static_cast<float>(frame);
}

}  // namespace bo2::engine
