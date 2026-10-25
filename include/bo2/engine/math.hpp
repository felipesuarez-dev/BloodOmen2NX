#pragma once

#include <algorithm>
#include <cmath>

namespace bo2::engine {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(const Vec3& a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(const Vec3& a, float scale) { return {a.x * scale, a.y * scale, a.z * scale}; }
inline Vec3 operator/(const Vec3& a, float scale) { return {a.x / scale, a.y / scale, a.z / scale}; }

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& value) { return std::sqrt(dot(value, value)); }

inline Vec3 normalize(const Vec3& value) {
    const float magnitude = length(value);
    if (magnitude == 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return value / magnitude;
}

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct Mat4 {
    float m[16] = {};

    static Mat4 identity() {
        Mat4 result;
        result.m[0] = 1.0f;
        result.m[5] = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;
        return result;
    }

    static Mat4 perspective(float fov_y_radians, float aspect, float near_plane, float far_plane) {
        Mat4 result;
        const float focal = 1.0f / std::tan(fov_y_radians * 0.5f);
        result.m[0] = focal / aspect;
        result.m[5] = focal;
        result.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
        result.m[11] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
        result.m[14] = -1.0f;
        return result;
    }

    static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 forward = normalize(center - eye);
        const Vec3 side = normalize(cross(forward, up));
        const Vec3 corrected_up = cross(side, forward);
        Mat4 result = identity();
        result.m[0] = side.x;
        result.m[1] = side.y;
        result.m[2] = side.z;
        result.m[3] = -dot(side, eye);
        result.m[4] = corrected_up.x;
        result.m[5] = corrected_up.y;
        result.m[6] = corrected_up.z;
        result.m[7] = -dot(corrected_up, eye);
        result.m[8] = -forward.x;
        result.m[9] = -forward.y;
        result.m[10] = -forward.z;
        result.m[11] = dot(forward, eye);
        return result;
    }

    Vec4 operator*(const Vec4& value) const {
        return {
            m[0] * value.x + m[1] * value.y + m[2] * value.z + m[3] * value.w,
            m[4] * value.x + m[5] * value.y + m[6] * value.z + m[7] * value.w,
            m[8] * value.x + m[9] * value.y + m[10] * value.z + m[11] * value.w,
            m[12] * value.x + m[13] * value.y + m[14] * value.z + m[15] * value.w,
        };
    }

    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                float value = 0.0f;
                for (int index = 0; index < 4; ++index) {
                    value += m[row * 4 + index] * other.m[index * 4 + column];
                }
                result.m[row * 4 + column] = value;
            }
        }
        return result;
    }
};

}  // namespace bo2::engine
