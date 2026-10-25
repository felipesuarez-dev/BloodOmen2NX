#define GL_GLEXT_PROTOTYPES

#include "render_pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL_opengl.h>

namespace bo2::platform::sdl::detail {

namespace {

struct PipelineVertex {
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
    float lu;
    float lv;
};

void matrix_values(const engine::Mat4& matrix, GLfloat values[16]) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            values[column * 4 + row] = matrix.m[row * 4 + column];
        }
    }
}

GLuint compile_shader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    const GLchar* sources[1] = {source};
    glShaderSource(shader, 1, sources, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == 0) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("pipeline shader compile failed: " + log);
    }
    return shader;
}

GLuint build_program() {
    static const char* vertex_source = R"(
#version 120
attribute vec3 a_position;
attribute vec3 a_normal;
attribute vec2 a_uv;
attribute vec2 a_light_uv;
uniform mat4 u_mvp;
varying vec2 v_uv;
varying vec2 v_light_uv;
varying float v_shade;

void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_uv = a_uv;
    v_light_uv = a_light_uv;
    float normal_length = length(a_normal);
    v_shade = 0.85;
    if (normal_length >= 0.5) {
        vec3 light = normalize(vec3(-0.35, 0.55, 0.75));
        v_shade = 0.25 + 0.75 * abs(dot(a_normal / normal_length, light));
    }
}
)";
    static const char* fragment_source = R"(
#version 120
uniform sampler2D u_texture;
uniform sampler2D u_lightmap;
uniform int u_textured;
uniform int u_use_lightmap;
uniform vec3 u_tint;
varying vec2 v_uv;
varying vec2 v_light_uv;
varying float v_shade;

void main() {
    vec4 color;
    if (u_textured != 0) {
        color = texture2D(u_texture, v_uv);
        color.rgb *= u_tint;
        if (u_use_lightmap != 0) {
            color *= texture2D(u_lightmap, v_light_uv);
        } else {
            color.rgb *= v_shade;
        }
    } else {
        color = vec4(u_tint, 1.0);
    }
    gl_FragColor = color;
}
)";
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_normal");
    glBindAttribLocation(program, 2, "a_uv");
    glBindAttribLocation(program, 3, "a_light_uv");
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == 0) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("pipeline program link failed: " + log);
    }
    return program;
}

GLint uniform_location(GLuint program, const char* name) {
    const GLint location = glGetUniformLocation(program, name);
    if (location < 0) {
        throw std::runtime_error(std::string("pipeline uniform not found: ") + name);
    }
    return location;
}

}  // namespace

struct LevelPipeline::Impl {
    GLuint program = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLint u_mvp = -1;
    GLint u_texture = -1;
    GLint u_lightmap = -1;
    GLint u_textured = -1;
    GLint u_use_lightmap = -1;
    GLint u_tint = -1;

    Impl() {
        program = build_program();
        u_mvp = uniform_location(program, "u_mvp");
        u_texture = uniform_location(program, "u_texture");
        u_lightmap = uniform_location(program, "u_lightmap");
        u_textured = uniform_location(program, "u_textured");
        u_use_lightmap = uniform_location(program, "u_use_lightmap");
        u_tint = uniform_location(program, "u_tint");
        glUseProgram(program);
        glUniform1i(u_texture, 0);
        glUniform1i(u_lightmap, 1);
        glUseProgram(0);
    }

    ~Impl() {
        if (ibo != 0) {
            glDeleteBuffers(1, &ibo);
        }
        if (vbo != 0) {
            glDeleteBuffers(1, &vbo);
        }
        if (program != 0) {
            glDeleteProgram(program);
        }
    }
};

LevelPipeline::LevelPipeline() : impl_(std::make_unique<Impl>()) {}

LevelPipeline::~LevelPipeline() = default;

void LevelPipeline::upload(const engine::LevelGeometry& geometry) {
    if (impl_->vbo != 0 || geometry.positions.empty()) {
        return;
    }
    std::vector<PipelineVertex> vertices;
    vertices.reserve(geometry.positions.size());
    for (std::size_t index = 0; index < geometry.positions.size(); ++index) {
        const engine::Vec3& position = geometry.positions[index];
        const engine::Vec3 normal =
            index < geometry.normals.size() ? geometry.normals[index] : engine::Vec3{};
        const engine::Vec2 uv =
            index < geometry.texcoords.size() ? geometry.texcoords[index] : engine::Vec2{};
        const engine::Vec2 light_uv = index < geometry.lightmap_texcoords.size()
                                          ? geometry.lightmap_texcoords[index]
                                          : uv;
        vertices.push_back({position.x, position.y, position.z, normal.x, normal.y, normal.z,
                            uv.x, uv.y, light_uv.x, light_uv.y});
    }
    glGenBuffers(1, &impl_->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(PipelineVertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenBuffers(1, &impl_->ibo);
}

void LevelPipeline::draw(const engine::LevelGeometry& geometry,
                         const std::vector<std::uint32_t>& textures,
                         const std::vector<std::uint32_t>& lightmaps,
                         std::size_t model_lightmap_offset, const engine::Mat4& view_projection,
                         engine::MaterialMode material_mode, bool use_lightmap) {
    if (impl_->vbo == 0 || impl_->ibo == 0 || geometry.triangles.empty()) {
        return;
    }
    struct Run {
        int texture = -1;
        int lightmap = -1;
        std::uint32_t material = 0;
        bool textured = false;
        std::size_t first_index = 0;
        std::size_t index_count = 0;

        bool same_key(const Run& other) const {
            return texture == other.texture && lightmap == other.lightmap &&
                   material == other.material && textured == other.textured;
        }
    };
    std::vector<std::uint32_t> indices;
    std::vector<Run> runs;
    for (const engine::LevelTriangle& triangle : geometry.triangles) {
        if (triangle.a >= geometry.positions.size() || triangle.b >= geometry.positions.size() ||
            triangle.c >= geometry.positions.size()) {
            continue;
        }
        const bool textured =
            triangle.texture >= 0 && static_cast<std::size_t>(triangle.texture) < textures.size() &&
            textures[static_cast<std::size_t>(triangle.texture)] != 0;
        int lightmap = -1;
        if (use_lightmap && textured) {
            const std::size_t model_index =
                triangle.lightmap >= 0
                    ? model_lightmap_offset + static_cast<std::size_t>(triangle.lightmap)
                    : lightmaps.size();
            if (model_index < lightmaps.size() && lightmaps[model_index] != 0) {
                lightmap = static_cast<int>(model_index);
            } else if (static_cast<std::size_t>(triangle.texture) < model_lightmap_offset &&
                       lightmaps[static_cast<std::size_t>(triangle.texture)] != 0) {
                lightmap = triangle.texture;
            }
        }
        const Run key{textured ? triangle.texture : -1, lightmap, triangle.material, textured,
                      indices.size(), 0};
        if (runs.empty() || !runs.back().same_key(key)) {
            runs.push_back(key);
        }
        indices.push_back(triangle.a);
        indices.push_back(triangle.b);
        indices.push_back(triangle.c);
        runs.back().index_count += 3;
    }
    if (indices.empty()) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(),
                 GL_STREAM_DRAW);
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(PipelineVertex));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(PipelineVertex, px)));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(PipelineVertex, nx)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(PipelineVertex, u)));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(PipelineVertex, lu)));

    GLfloat matrix[16];
    matrix_values(view_projection, matrix);
    glUseProgram(impl_->program);
    glUniformMatrix4fv(impl_->u_mvp, 1, GL_FALSE, matrix);
    for (const Run& run : runs) {
        engine::LevelTriangle probe;
        probe.material = run.material;
        const engine::Vec3 tint =
            run.textured ? engine::material_tint(geometry, probe, material_mode)
                         : engine::material_color(geometry, probe);
        glUniform1i(impl_->u_textured, run.textured ? 1 : 0);
        glUniform1i(impl_->u_use_lightmap, run.lightmap >= 0 ? 1 : 0);
        glUniform3f(impl_->u_tint, tint.x, tint.y, tint.z);
        if (run.textured) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textures[static_cast<std::size_t>(run.texture)]);
        }
        if (run.lightmap >= 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, lightmaps[static_cast<std::size_t>(run.lightmap)]);
        }
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(run.index_count), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(run.first_index * sizeof(std::uint32_t)));
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

}  // namespace bo2::platform::sdl::detail
