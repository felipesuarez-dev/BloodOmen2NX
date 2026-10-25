#include "bo2/platform/headless/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace bo2::platform::headless {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Texel {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct ScreenVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float inverse_w = 0.0f;
    float u_over_w = 0.0f;
    float v_over_w = 0.0f;
    float shade = 1.0f;
};

float edge(float ax, float ay, float bx, float by, float cx, float cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

int wrap_index(int value, std::uint32_t size) {
    const int limit = static_cast<int>(size);
    int wrapped = value % limit;
    if (wrapped < 0) {
        wrapped += limit;
    }
    return wrapped;
}

Texel fetch(const engine::Image& image, int x, int y) {
    const std::size_t index = (static_cast<std::size_t>(y) * image.width +
                               static_cast<std::size_t>(x)) * 4;
    return {image.rgba[index], image.rgba[index + 1], image.rgba[index + 2],
            image.rgba[index + 3]};
}

Texel sample_bilinear(const engine::Image& image, float u, float v) {
    const float wrapped_u = u - std::floor(u);
    const float wrapped_v = v - std::floor(v);
    const float sx = wrapped_u * static_cast<float>(image.width) - 0.5f;
    const float sy = wrapped_v * static_cast<float>(image.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const float fx = sx - std::floor(sx);
    const float fy = sy - std::floor(sy);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const Texel c00 = fetch(image, wrap_index(x0, image.width), wrap_index(y0, image.height));
    const Texel c10 = fetch(image, wrap_index(x1, image.width), wrap_index(y0, image.height));
    const Texel c01 = fetch(image, wrap_index(x0, image.width), wrap_index(y1, image.height));
    const Texel c11 = fetch(image, wrap_index(x1, image.width), wrap_index(y1, image.height));

    auto blend = [fx, fy](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
        const float top = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * fx;
        const float bottom = static_cast<float>(c) + (static_cast<float>(d) - static_cast<float>(c)) * fx;
        const float value = top + (bottom - top) * fy;
        return static_cast<std::uint8_t>(std::clamp(value + 0.5f, 0.0f, 255.0f));
    };

    return {
        blend(c00.r, c10.r, c01.r, c11.r),
        blend(c00.g, c10.g, c01.g, c11.g),
        blend(c00.b, c10.b, c01.b, c11.b),
        blend(c00.a, c10.a, c01.a, c11.a),
    };
}

bool project(const engine::Mat4& view_projection, const engine::Vec3& position, float u, float v,
             float shade, std::uint32_t width, std::uint32_t height, ScreenVertex& out) {
    const engine::Vec4 clip = view_projection * engine::Vec4{position.x, position.y, position.z, 1.0f};
    if (clip.w <= 1.0e-6f) {
        return false;
    }
    const float inverse_w = 1.0f / clip.w;
    const float ndc_x = clip.x * inverse_w;
    const float ndc_y = clip.y * inverse_w;
    out.x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
    out.y = (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(height);
    out.z = clip.z * inverse_w;
    out.inverse_w = inverse_w;
    out.u_over_w = u * inverse_w;
    out.v_over_w = v * inverse_w;
    out.shade = shade;
    return true;
}

void rasterize(const ScreenVertex& v0, const ScreenVertex& v1, const ScreenVertex& v2,
               const engine::Image& texture, std::uint32_t width, std::uint32_t height,
               std::vector<std::uint8_t>& color, std::vector<float>& depth) {
    const float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
    if (std::fabs(area) < 1.0e-6f) {
        return;
    }

    const int min_x = std::max(0, static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
    const int max_x = std::min(static_cast<int>(width) - 1,
                               static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
    const int max_y = std::min(static_cast<int>(height) - 1,
                               static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));
    if (min_x > max_x || min_y > max_y) {
        return;
    }

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float w0 = edge(v1.x, v1.y, v2.x, v2.y, px, py) / area;
            const float w1 = edge(v2.x, v2.y, v0.x, v0.y, px, py) / area;
            const float w2 = edge(v0.x, v0.y, v1.x, v1.y, px, py) / area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                continue;
            }
            const float inverse_w =
                w0 * v0.inverse_w + w1 * v1.inverse_w + w2 * v2.inverse_w;
            if (inverse_w <= 0.0f) {
                continue;
            }
            const float z = w0 * v0.z + w1 * v1.z + w2 * v2.z;
            const std::size_t pixel = static_cast<std::size_t>(y) * width +
                                      static_cast<std::size_t>(x);
            if (z < -1.0f || z > 1.0f || z >= depth[pixel]) {
                continue;
            }
            const float u =
                (w0 * v0.u_over_w + w1 * v1.u_over_w + w2 * v2.u_over_w) / inverse_w;
            const float v =
                (w0 * v0.v_over_w + w1 * v1.v_over_w + w2 * v2.v_over_w) / inverse_w;
            const float shade = std::clamp(w0 * v0.shade + w1 * v1.shade + w2 * v2.shade, 0.0f, 1.0f);
            const Texel texel = sample_bilinear(texture, u, v);
            const float alpha = static_cast<float>(texel.a) / 255.0f;
            const std::size_t out = pixel * 4;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const float source = static_cast<float>(
                    channel == 0 ? texel.r : (channel == 1 ? texel.g : texel.b));
                const float destination = static_cast<float>(color[out + channel]);
                const float blended = source * shade * alpha + destination * (1.0f - alpha);
                color[out + channel] = static_cast<std::uint8_t>(std::clamp(blended + 0.5f, 0.0f, 255.0f));
            }
            depth[pixel] = z;
        }
    }
}

engine::Mat4 make_view_projection(const Camera& camera, float aspect, float near_plane,
                                  float far_plane) {
    if (camera.fov_degrees <= 1.0f || camera.fov_degrees >= 179.0f) {
        throw std::runtime_error("unsupported field of view");
    }
    const engine::Vec3 forward = camera.target - camera.eye;
    if (engine::length(forward) < 1.0e-6f) {
        throw std::runtime_error("camera eye and target are the same");
    }
    engine::Vec3 up = camera.up;
    if (engine::length(up) < 1.0e-6f) {
        up = {0.0f, 1.0f, 0.0f};
    }
    if (engine::length(engine::cross(engine::normalize(forward), engine::normalize(up))) < 1.0e-6f) {
        up = {0.0f, 0.0f, 1.0f};
    }
    const engine::Mat4 view = engine::Mat4::look_at(camera.eye, camera.target, up);
    const engine::Mat4 projection =
        engine::Mat4::perspective(camera.fov_degrees * kPi / 180.0f, aspect, near_plane, far_plane);
    return projection * view;
}

struct Face {
    engine::Vec3 corners[4];
    float shade;
};

const std::array<Face, 6>& cube_faces() {
    static const std::array<Face, 6> faces = {{
        Face{{engine::Vec3{-0.5f, -0.5f, 0.5f}, engine::Vec3{0.5f, -0.5f, 0.5f},
              engine::Vec3{0.5f, 0.5f, 0.5f}, engine::Vec3{-0.5f, 0.5f, 0.5f}},
             0.95f},
        Face{{engine::Vec3{-0.5f, -0.5f, -0.5f}, engine::Vec3{-0.5f, -0.5f, 0.5f},
              engine::Vec3{-0.5f, 0.5f, 0.5f}, engine::Vec3{-0.5f, 0.5f, -0.5f}},
             0.75f},
        Face{{engine::Vec3{0.5f, -0.5f, -0.5f}, engine::Vec3{0.5f, -0.5f, 0.5f},
              engine::Vec3{0.5f, 0.5f, 0.5f}, engine::Vec3{0.5f, 0.5f, -0.5f}},
             0.85f},
        Face{{engine::Vec3{-0.5f, -0.5f, -0.5f}, engine::Vec3{0.5f, -0.5f, -0.5f},
              engine::Vec3{0.5f, 0.5f, -0.5f}, engine::Vec3{-0.5f, 0.5f, -0.5f}},
             0.70f},
        Face{{engine::Vec3{-0.5f, 0.5f, 0.5f}, engine::Vec3{0.5f, 0.5f, 0.5f},
              engine::Vec3{0.5f, 0.5f, -0.5f}, engine::Vec3{-0.5f, 0.5f, -0.5f}},
             1.00f},
        Face{{engine::Vec3{-0.5f, -0.5f, -0.5f}, engine::Vec3{0.5f, -0.5f, -0.5f},
              engine::Vec3{0.5f, -0.5f, 0.5f}, engine::Vec3{-0.5f, -0.5f, 0.5f}},
             0.55f},
    }};
    return faces;
}

}  // namespace

std::vector<std::uint8_t> render_textured_cube(const engine::Image& texture,
                                               const FrameOptions& options) {
    if (texture.empty()) {
        throw std::runtime_error("cannot render an empty texture");
    }
    if (options.width == 0 || options.height == 0 || options.width > 8192 ||
        options.height > 8192) {
        throw std::runtime_error("unsupported frame size");
    }
    const float aspect = static_cast<float>(options.width) / static_cast<float>(options.height);
    const engine::Mat4 view_projection =
        make_view_projection(options.camera, aspect, 0.05f, 50.0f);

    const std::size_t pixel_count = static_cast<std::size_t>(options.width) * options.height;
    std::vector<std::uint8_t> color(pixel_count * 4);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        color[pixel * 4 + 0] = 26;
        color[pixel * 4 + 1] = 28;
        color[pixel * 4 + 2] = 36;
        color[pixel * 4 + 3] = 255;
    }
    std::vector<float> depth(pixel_count, std::numeric_limits<float>::infinity());

    static constexpr float u[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    static constexpr float v[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    for (const Face& face : cube_faces()) {
        ScreenVertex screen[4];
        bool visible = true;
        for (int corner = 0; corner < 4; ++corner) {
            if (!project(view_projection, face.corners[corner], u[corner], v[corner], face.shade,
                         options.width, options.height, screen[corner])) {
                visible = false;
                break;
            }
        }
        if (!visible) {
            continue;
        }
        rasterize(screen[0], screen[1], screen[2], texture, options.width, options.height, color,
                  depth);
        rasterize(screen[0], screen[2], screen[3], texture, options.width, options.height, color,
                  depth);
    }
    return color;
}

std::vector<std::uint8_t> render_level(const engine::LevelGeometry& geometry,
                                            const std::vector<engine::Image>& textures,
                                            const FrameOptions& options) {
    if (geometry.positions.empty() || geometry.triangles.empty()) {
        throw std::runtime_error("cannot render empty level geometry");
    }
    if (options.width == 0 || options.height == 0 || options.width > 8192 ||
        options.height > 8192) {
        throw std::runtime_error("unsupported frame size");
    }
    const float aspect = static_cast<float>(options.width) / static_cast<float>(options.height);
    engine::Vec3 minimum = geometry.minimum;
    engine::Vec3 maximum = geometry.maximum;
    if (!(minimum.x <= maximum.x)) {
        minimum = geometry.positions.front();
        maximum = geometry.positions.front();
        for (const engine::Vec3& position : geometry.positions) {
            minimum.x = std::min(minimum.x, position.x);
            minimum.y = std::min(minimum.y, position.y);
            minimum.z = std::min(minimum.z, position.z);
            maximum.x = std::max(maximum.x, position.x);
            maximum.y = std::max(maximum.y, position.y);
            maximum.z = std::max(maximum.z, position.z);
        }
    }
    const float distance = engine::length(options.camera.eye - options.camera.target);
    const float extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y,
                                   maximum.z - minimum.z});
    const float near_plane = std::max(0.05f, distance * 0.001f);
    const float far_plane = distance + extent * 2.0f + 10.0f;
    const engine::Mat4 view_projection =
        make_view_projection(options.camera, aspect, near_plane, far_plane);
    const std::size_t pixel_count = static_cast<std::size_t>(options.width) * options.height;
    std::vector<std::uint8_t> color(pixel_count * 4);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        color[pixel * 4 + 0] = 20;
        color[pixel * 4 + 1] = 22;
        color[pixel * 4 + 2] = 30;
        color[pixel * 4 + 3] = 255;
    }
    std::vector<float> depth(pixel_count, std::numeric_limits<float>::infinity());

    const std::array<engine::Vec3, 8> palette = {{
        {0.62f, 0.56f, 0.48f}, {0.50f, 0.52f, 0.56f}, {0.58f, 0.47f, 0.38f},
        {0.45f, 0.50f, 0.43f}, {0.54f, 0.44f, 0.42f}, {0.60f, 0.56f, 0.44f},
        {0.42f, 0.46f, 0.52f}, {0.52f, 0.48f, 0.58f},
    }};
    std::array<engine::Image, 8> materials;
    for (std::size_t index = 0; index < materials.size(); ++index) {
        engine::Image image;
        image.width = 1;
        image.height = 1;
        image.rgba = {
            static_cast<std::uint8_t>(std::clamp(palette[index].x * 255.0f, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(palette[index].y * 255.0f, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(palette[index].z * 255.0f, 0.0f, 255.0f)),
            255,
        };
        materials[index] = std::move(image);
    }

    const engine::Vec3 light = engine::normalize({-0.35f, 0.55f, 0.75f});
    auto shade_of = [&light](const engine::Vec3& normal) {
        const engine::Vec3 unit = engine::normalize(normal);
        if (engine::length(unit) < 0.5f) {
            return 0.85f;
        }
        return 0.25f + 0.75f * std::fabs(engine::dot(unit, light));
    };

    for (const engine::LevelTriangle& triangle : geometry.triangles) {
        if (triangle.a >= geometry.positions.size() || triangle.b >= geometry.positions.size() ||
            triangle.c >= geometry.positions.size()) {
            throw std::runtime_error("level triangle index is out of range");
        }
        const engine::Vec3 positions[3] = {geometry.positions[triangle.a],
                                           geometry.positions[triangle.b],
                                           geometry.positions[triangle.c]};
        const engine::Vec3 normals[3] = {geometry.normals[triangle.a], geometry.normals[triangle.b],
                                         geometry.normals[triangle.c]};
        const bool textured = triangle.texture >= 0 &&
                              static_cast<std::size_t>(triangle.texture) < textures.size() &&
                              !textures[static_cast<std::size_t>(triangle.texture)].empty();
        const engine::Vec2 uv[3] = {
            (textured && triangle.a < geometry.texcoords.size()) ? geometry.texcoords[triangle.a]
                                                                 : engine::Vec2{},
            (textured && triangle.b < geometry.texcoords.size()) ? geometry.texcoords[triangle.b]
                                                                 : engine::Vec2{},
            (textured && triangle.c < geometry.texcoords.size()) ? geometry.texcoords[triangle.c]
                                                                 : engine::Vec2{},
        };
        ScreenVertex screen[3];
        bool visible = true;
        for (int corner = 0; corner < 3; ++corner) {
            if (!project(view_projection, positions[corner], uv[corner].x, uv[corner].y,
                         shade_of(normals[corner]), options.width, options.height,
                         screen[corner])) {
                visible = false;
                break;
            }
        }
        if (!visible) {
            continue;
        }
        const engine::Image& image =
            textured ? textures[static_cast<std::size_t>(triangle.texture)]
                     : materials[static_cast<std::size_t>(triangle.material) % materials.size()];
        rasterize(screen[0], screen[1], screen[2], image, options.width, options.height, color,
                  depth);
    }
    return color;
}

std::vector<std::uint8_t> render_level(const engine::LevelGeometry& geometry,
                                     const FrameOptions& options) {
    return render_level(geometry, std::vector<engine::Image>{}, options);
}

void write_ppm(const std::string& path, std::uint32_t width, std::uint32_t height,
               const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() < static_cast<std::size_t>(width) * height * 4) {
        throw std::runtime_error("framebuffer is smaller than the frame");
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot write " + path);
    }
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
        stream.write(reinterpret_cast<const char*>(rgba.data() + pixel * 4), 3);
    }
}

}  // namespace bo2::platform::headless
