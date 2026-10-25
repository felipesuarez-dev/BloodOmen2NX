#include "bo2/engine/cell_group.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

struct Chunk {
    std::string name;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;
    std::size_t end = 0;
};

std::string chunk_name(const common::Reader& reader, std::size_t offset) {
    std::string name;
    for (std::size_t index = 0; index < 8; ++index) {
        const std::uint8_t value = reader.u8(offset + index);
        if (value == 0) {
            break;
        }
        name.push_back(static_cast<char>(value));
    }
    return name;
}

std::vector<Chunk> read_chunks(const common::Reader& reader, std::size_t start, std::size_t end) {
    std::vector<Chunk> chunks;
    std::size_t position = start;
    while (position + 16 <= end) {
        const std::string name = chunk_name(reader, position);
        const std::uint32_t total = reader.u32(position + 8);
        if (total < 16 || position + total > end) {
            throw common::ParseError("bad chunk size");
        }
        chunks.push_back({name, position + 16, total - 16, position + total});
        position += total;
    }
    if (position != end) {
        throw common::ParseError("chunk area has a trailer");
    }
    return chunks;
}

const Chunk* find_chunk(const std::vector<Chunk>& chunks, const std::string& name) {
    for (const Chunk& chunk : chunks) {
        if (chunk.name == name) {
            return &chunk;
        }
    }
    return nullptr;
}

float read_float(const common::Reader& reader, std::size_t offset) {
    const std::uint32_t bits = reader.u32(offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::vector<Vec3> read_vectors(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty vector chunk");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    if (4 + static_cast<std::size_t>(count) * 12 > chunk.data_size) {
        throw common::ParseError("vector chunk is short");
    }
    std::vector<Vec3> values;
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t offset = chunk.data_offset + 4 + static_cast<std::size_t>(index) * 12;
        values.push_back({read_float(reader, offset), read_float(reader, offset + 4),
                          read_float(reader, offset + 8)});
    }
    return values;
}

void parse_prims(const common::Reader& reader, const Chunk& chunk, std::uint32_t vertex_base,
                 std::uint32_t vertex_count, std::uint32_t model, LevelGeometry& geometry) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty prim chunk");
    }
    const std::size_t end = chunk.data_offset + chunk.data_size;
    const std::uint32_t groups = reader.u32(chunk.data_offset);
    std::size_t position = chunk.data_offset + 4;
    for (std::uint32_t group = 0; group < groups; ++group) {
        if (position + 12 > end) {
            throw common::ParseError("truncated prim group");
        }
        const std::uint32_t material = reader.u32(position);
        const std::uint32_t triangles = reader.u32(position + 4);
        const std::uint32_t indices = reader.u32(position + 8);
        position += 12;
        if (triangles * 3 != indices) {
            throw common::ParseError("prim index count does not match the triangle count");
        }
        if (position + static_cast<std::size_t>(indices) * 2 > end) {
            throw common::ParseError("prim indices are truncated");
        }
        for (std::uint32_t index = 0; index + 2 < indices; index += 3) {
            const std::uint16_t a = reader.u16(position + static_cast<std::size_t>(index) * 2);
            const std::uint16_t b = reader.u16(position + static_cast<std::size_t>(index + 1) * 2);
            const std::uint16_t c = reader.u16(position + static_cast<std::size_t>(index + 2) * 2);
            if (a >= vertex_count || b >= vertex_count || c >= vertex_count) {
                throw common::ParseError("prim index is out of range");
            }
            geometry.triangles.push_back(
                {vertex_base + a, vertex_base + b, vertex_base + c, material, model});
        }
        position += static_cast<std::size_t>(indices) * 2;
        if (position + 2 > end) {
            throw common::ParseError("prim trailer is truncated");
        }
        const std::uint16_t flag = reader.u16(position);
        position += 2;
        position += (flag == 0xFFFFU) ? 40 : 38;
        if (position > end) {
            throw common::ParseError("prim extra data is truncated");
        }
    }
    if (position != end) {
        throw common::ParseError("prim chunk has a trailer");
    }
}

void parse_model(const common::Reader& reader, const Chunk& rmd, std::uint32_t model,
                 LevelGeometry& geometry) {
    const std::size_t data_end = rmd.data_offset + rmd.data_size;
    if (rmd.data_size < 4) {
        throw common::ParseError("empty rmd chunk");
    }
    const std::size_t record = rmd.data_offset + 4;
    if (record + 16 > data_end) {
        throw common::ParseError("truncated rmd record");
    }
    const std::uint32_t total = reader.u32(record + 8);
    if (total < 16 || record + total > data_end) {
        throw common::ParseError("bad rmd record size");
    }
    const std::vector<Chunk> chunks = read_chunks(reader, record + 16, record + total);
    const Chunk* positions = find_chunk(chunks, "position");
    const Chunk* normals = find_chunk(chunks, "normals");
    const Chunk* prims = find_chunk(chunks, "prims");
    if (positions == nullptr || prims == nullptr) {
        throw common::ParseError("model has no position or prim chunk");
    }
    std::vector<Vec3> vertices = read_vectors(reader, *positions);
    std::vector<Vec3> vertex_normals;
    if (normals != nullptr) {
        vertex_normals = read_vectors(reader, *normals);
    }
    vertex_normals.resize(vertices.size(), Vec3{0.0f, 0.0f, 0.0f});

    const std::uint32_t base = static_cast<std::uint32_t>(geometry.positions.size());
    geometry.positions.insert(geometry.positions.end(), vertices.begin(), vertices.end());
    geometry.normals.insert(geometry.normals.end(), vertex_normals.begin(), vertex_normals.end());
    parse_prims(reader, *prims, base, static_cast<std::uint32_t>(vertices.size()), model, geometry);
}

void compute_bounds(LevelGeometry& geometry) {
    const float infinity = std::numeric_limits<float>::infinity();
    Vec3 minimum{infinity, infinity, infinity};
    Vec3 maximum{-infinity, -infinity, -infinity};
    for (const Vec3& position : geometry.positions) {
        minimum.x = std::min(minimum.x, position.x);
        minimum.y = std::min(minimum.y, position.y);
        minimum.z = std::min(minimum.z, position.z);
        maximum.x = std::max(maximum.x, position.x);
        maximum.y = std::max(maximum.y, position.y);
        maximum.z = std::max(maximum.z, position.z);
    }
    geometry.minimum = minimum;
    geometry.maximum = maximum;
}

}  // namespace

LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data) {
    const common::Reader reader(data);
    if (data.size() < 28) {
        throw common::ParseError("cell group is shorter than its header");
    }
    if (chunk_name(reader, 8) != "cellmdls") {
        throw common::ParseError("cell group has no cellmdls section");
    }
    const std::uint32_t model_count = reader.u32(24);
    std::size_t position = 28;
    LevelGeometry geometry;
    for (std::uint32_t model = 0; model < model_count; ++model) {
        if (position + 20 > data.size()) {
            throw common::ParseError("truncated model block");
        }
        const std::string name = reader.cstr(position + 4, data.size());
        const std::size_t name_end = position + 4 + name.size();
        const std::uint32_t model_size = reader.u32(name_end + 1);
        const std::size_t chunks_start = name_end + 9;
        const std::size_t model_end = position + 4 + model_size;
        if (model_size < 16 || chunks_start > model_end || model_end > data.size()) {
            throw common::ParseError("bad model block size");
        }
        const std::vector<Chunk> chunks = read_chunks(reader, chunks_start, model_end);
        const Chunk* rmd = find_chunk(chunks, "rmd");
        if (rmd == nullptr) {
            ++geometry.skipped_models;
        } else {
            parse_model(reader, *rmd, model, geometry);
            ++geometry.model_count;
        }
        position = model_end;
    }
    if (geometry.positions.empty() || geometry.triangles.empty()) {
        throw common::ParseError("cell group has no renderable geometry");
    }
    compute_bounds(geometry);
    return geometry;
}

}  // namespace bo2::engine
