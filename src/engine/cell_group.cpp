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

std::vector<std::uint16_t> read_u16s(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty u16 chunk");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    if (4 + static_cast<std::size_t>(count) * 2 > chunk.data_size) {
        throw common::ParseError("u16 chunk is short");
    }
    std::vector<std::uint16_t> values;
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        values.push_back(reader.u16(chunk.data_offset + 4 + static_cast<std::size_t>(index) * 2));
    }
    return values;
}

std::vector<Vec2> read_uvs(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 8) {
        throw common::ParseError("empty uv chunk");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset + 4);
    if (8 + static_cast<std::size_t>(count) * 8 > chunk.data_size) {
        throw common::ParseError("uv chunk is short");
    }
    std::vector<Vec2> values;
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t offset = chunk.data_offset + 8 + static_cast<std::size_t>(index) * 8;
        values.push_back({read_float(reader, offset), read_float(reader, offset + 4)});
    }
    return values;
}

std::string resolve_texture_name(std::uint32_t relative,
                                 const std::vector<std::uint16_t>& symbols,
                                 const std::vector<std::string>& names) {
    if (relative >= symbols.size()) {
        return {};
    }
    const std::uint32_t symbol = symbols[relative];
    if (symbol >= names.size()) {
        return {};
    }
    return names[symbol];
}

std::int32_t add_texture_name(LevelGeometry& geometry, const std::string& name) {
    if (name.empty()) {
        return -1;
    }
    for (std::size_t index = 0; index < geometry.texture_names.size(); ++index) {
        if (geometry.texture_names[index] == name) {
            return static_cast<std::int32_t>(index);
        }
    }
    geometry.texture_names.push_back(name);
    return static_cast<std::int32_t>(geometry.texture_names.size() - 1);
}

std::vector<std::int32_t> read_material_textures(
    const common::Reader& reader, const Chunk* chunk, const std::vector<std::uint16_t>& symbols,
    const std::vector<std::string>& names, LevelGeometry& geometry) {
    std::vector<std::int32_t> result;
    if (chunk == nullptr || chunk->data_size < 8) {
        return result;
    }
    const std::uint32_t start = reader.u32(chunk->data_offset);
    const std::uint32_t count = reader.u32(chunk->data_offset + 4);
    if (count == 0 || count > 4096 || start > 65536) {
        return result;
    }
    const std::size_t record_size = (chunk->data_size - 8) / count;
    if (record_size < 4) {
        return result;
    }
    result.assign(static_cast<std::size_t>(start) + count, -1);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t record = chunk->data_offset + 8 + static_cast<std::size_t>(index) * record_size;
        const std::uint32_t relative =
            (record_size >= 6) ? reader.u16(record + 4) : reader.u32(record);
        result[static_cast<std::size_t>(start) + index] =
            add_texture_name(geometry, resolve_texture_name(relative, symbols, names));
    }
    return result;
}

void parse_prims(const common::Reader& reader, const Chunk& chunk, std::uint32_t vertex_base,
                 std::uint32_t vertex_count, std::uint32_t model,
                 const std::vector<std::int32_t>& material_texture, LevelGeometry& geometry) {
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
            const std::int32_t texture =
                material < material_texture.size() ? material_texture[material] : -1;
            geometry.triangles.push_back(
                {vertex_base + a, vertex_base + b, vertex_base + c, material, model, texture});
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
                 const std::vector<std::uint16_t>& symbols,
                 const std::vector<std::string>& names, LevelGeometry& geometry) {
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
    const Chunk* uv_indices = find_chunk(chunks, "textures");
    const Chunk* uv_set = nullptr;
    for (const Chunk& chunk : chunks) {
        if (chunk.name != "uvs") {
            continue;
        }
        if (uv_set == nullptr || (chunk.data_size >= 4 && reader.u32(chunk.data_offset) == 0)) {
            uv_set = &chunk;
            if (reader.u32(chunk.data_offset) == 0) {
                break;
            }
        }
    }
    if (positions == nullptr || prims == nullptr) {
        throw common::ParseError("model has no position or prim chunk");
    }
    std::vector<Vec3> vertices = read_vectors(reader, *positions);
    std::vector<Vec3> vertex_normals;
    if (normals != nullptr) {
        vertex_normals = read_vectors(reader, *normals);
    }
    vertex_normals.resize(vertices.size(), Vec3{0.0f, 0.0f, 0.0f});

    const std::vector<std::uint16_t> uv_index_values =
        (uv_indices != nullptr) ? read_u16s(reader, *uv_indices) : std::vector<std::uint16_t>{};
    const std::vector<Vec2> uv_values =
        (uv_set != nullptr) ? read_uvs(reader, *uv_set) : std::vector<Vec2>{};

    const std::uint32_t base = static_cast<std::uint32_t>(geometry.positions.size());
    geometry.positions.insert(geometry.positions.end(), vertices.begin(), vertices.end());
    geometry.normals.insert(geometry.normals.end(), vertex_normals.begin(), vertex_normals.end());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const std::uint32_t uv_index =
            (index < uv_index_values.size()) ? uv_index_values[index]
                                             : static_cast<std::uint32_t>(index);
        geometry.texcoords.push_back(uv_index < uv_values.size() ? uv_values[uv_index] : Vec2{});
    }
    const std::vector<std::int32_t> material_texture =
        read_material_textures(reader, find_chunk(chunks, "texture"), symbols, names, geometry);
    parse_prims(reader, *prims, base, static_cast<std::uint32_t>(vertices.size()), model,
                material_texture, geometry);
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

LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data,
                               const std::vector<std::uint16_t>& symbols,
                               const std::vector<std::string>& names) {
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
            parse_model(reader, *rmd, model, symbols, names, geometry);
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

LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data) {
    static const std::vector<std::uint16_t> no_symbols;
    static const std::vector<std::string> no_names;
    return parse_cell_group(data, no_symbols, no_names);
}

}  // namespace bo2::engine
