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

std::size_t align_up(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

Vec3 read_vec3(const common::Reader& reader, std::size_t offset) {
    return {read_float(reader, offset), read_float(reader, offset + 4),
            read_float(reader, offset + 8)};
}

CellAttributeTable read_attribute_table(const common::Reader& reader, std::size_t start,
                                        std::size_t end) {
    if (start + 12 > end) {
        throw common::ParseError("attribute table is short");
    }
    CellAttributeTable table;
    table.name = chunk_name(reader, start);
    table.size = reader.u32(start + 8);
    if (table.size < 12 || static_cast<std::size_t>(table.size) > end - start) {
        throw common::ParseError("bad attribute table size");
    }
    if ((table.size - 12) % 4 != 0) {
        throw common::ParseError("attribute table payload is not u32 aligned");
    }
    const std::size_t count = (table.size - 12) / 4;
    table.values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        table.values.push_back(reader.u32(start + 12 + index * 4));
    }
    return table;
}

std::vector<CellAttributeTable> read_attribute_tables(const common::Reader& reader,
                                                      const Chunk& chunk) {
    std::vector<CellAttributeTable> tables;
    std::size_t position = chunk.data_offset;
    const std::size_t end = chunk.data_offset + chunk.data_size;
    while (position < end) {
        if (position + 12 > end) {
            throw common::ParseError("truncated attribute table");
        }
        const CellAttributeTable table = read_attribute_table(reader, position, end);
        if (table.size == 0 || static_cast<std::size_t>(table.size) > end - position) {
            throw common::ParseError("attribute table overruns its chunk");
        }
        position += table.size;
        tables.push_back(table);
    }
    if (position != end) {
        throw common::ParseError("attribute table has a trailer");
    }
    return tables;
}

CellPortal read_portal(const common::Reader& reader, std::size_t& position, std::size_t end) {
    if (position + 12 > end) {
        throw common::ParseError("truncated portal");
    }
    CellPortal portal;
    portal.version = reader.u32(position);
    portal.cell_a = reader.u16(position + 4);
    portal.cell_b = reader.u16(position + 6);
    const std::uint32_t count = reader.u32(position + 8);
    position += 12;
    if (portal.version != 1 && portal.version != 2) {
        throw common::ParseError("unsupported portal version");
    }
    if (count > 65536 || static_cast<std::size_t>(count) * 12 + position > end) {
        throw common::ParseError("portal vertices are truncated");
    }
    portal.vertices.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        portal.vertices.push_back(read_vec3(reader, position));
        position += 12;
    }
    if (position + 12 > end) {
        throw common::ParseError("truncated portal plane");
    }
    portal.plane = read_vec3(reader, position);
    position += 12;
    if (portal.version == 2) {
        if (position + 4 > end) {
            throw common::ParseError("truncated portal index");
        }
        portal.index = static_cast<std::int32_t>(reader.u32(position));
        position += 4;
    }
    return portal;
}

std::vector<CellPortal> read_portals(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty portal chunk");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    std::size_t position = chunk.data_offset + 4;
    const std::size_t end = chunk.data_offset + chunk.data_size;
    if (count > 65536) {
        throw common::ParseError("portal count is out of range");
    }
    std::vector<CellPortal> portals;
    portals.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        portals.push_back(read_portal(reader, position, end));
    }
    if (position != end) {
        throw common::ParseError("portal chunk has a trailer");
    }
    return portals;
}

std::vector<std::uint32_t> read_terrains(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty terrain chunk");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    if (4 + static_cast<std::size_t>(count) * 4 > chunk.data_size) {
        throw common::ParseError("terrain chunk is short");
    }
    std::vector<std::uint32_t> values;
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        values.push_back(reader.u32(chunk.data_offset + 4 + static_cast<std::size_t>(index) * 4));
    }
    return values;
}

LevelCell read_cell(const common::Reader& reader, std::size_t& position, std::size_t end) {
    if (position + 36 > end) {
        throw common::ParseError("truncated cell header");
    }
    LevelCell cell;
    cell.version = reader.u32(position);
    if (cell.version != 2) {
        throw common::ParseError("unsupported cell version");
    }
    cell.symbol = reader.u16(position + 4);
    cell.flags = reader.u32(position + 8);
    cell.minimum = read_vec3(reader, position + 12);
    cell.maximum = read_vec3(reader, position + 24);
    position += 36;

    if (position + 16 > end) {
        throw common::ParseError("truncated cell container");
    }
    const std::string name = chunk_name(reader, position);
    const std::uint32_t total = reader.u32(position + 8);
    if (name != "default") {
        throw common::ParseError("cell has no default container");
    }
    if (total < 16 || static_cast<std::size_t>(total) > end - position) {
        throw common::ParseError("bad cell container size");
    }
    const std::vector<Chunk> chunks = read_chunks(reader, position + 16, position + total);
    for (const Chunk& chunk : chunks) {
        if (chunk.name == "cellattr") {
            cell.attributes = read_attribute_tables(reader, chunk);
        } else if (chunk.name == "portal2") {
            cell.portals = read_portals(reader, chunk);
        } else if (chunk.name == "terrains") {
            cell.terrains = read_terrains(reader, chunk);
        } else if (chunk.name == "dbgmodel") {
            cell.dbgmodel_size = static_cast<std::uint32_t>(chunk.data_size);
        }
    }
    position += total;
    return cell;
}

CellGrid read_cell_grid(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty cell grid");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    std::size_t position = chunk.data_offset + 4;
    const std::size_t end = chunk.data_offset + chunk.data_size;
    if (count > 65536) {
        throw common::ParseError("cell count is out of range");
    }
    CellGrid grid;
    grid.cells.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        grid.cells.push_back(read_cell(reader, position, end));
    }
    if (position != end) {
        throw common::ParseError("cell grid has a trailer");
    }
    return grid;
}

std::int32_t read_bsp_node(const common::Reader& reader, std::size_t& position, std::size_t end,
                           std::size_t base, std::uint32_t depth, std::vector<BspNode>& nodes) {
    if (depth < 1) {
        throw common::ParseError("BSP depth exceeded");
    }
    if (position >= end) {
        throw common::ParseError("truncated BSP node");
    }
    const std::uint8_t type = reader.u8(position);
    ++position;
    if (type == 0) {
        position = base + align_up(position - base, 2);
        if (position + 2 > end) {
            throw common::ParseError("truncated BSP leaf");
        }
        BspNode node;
        node.leaf = true;
        node.value = reader.u16(position);
        position += 2;
        nodes.push_back(node);
        return static_cast<std::int32_t>(nodes.size() - 1);
    }
    if (type != 1) {
        throw common::ParseError("invalid BSP node type");
    }
    position = base + align_up(position - base, 4);
    if (position + 16 > end) {
        throw common::ParseError("truncated BSP split");
    }
    BspNode node;
    node.plane = read_vec3(reader, position);
    node.distance = read_float(reader, position + 12);
    position += 16;
    const std::int32_t index = static_cast<std::int32_t>(nodes.size());
    nodes.push_back(node);
    const std::int32_t left = read_bsp_node(reader, position, end, base, depth - 1, nodes);
    const std::int32_t right = read_bsp_node(reader, position, end, base, depth - 1, nodes);
    nodes[static_cast<std::size_t>(index)].left = left;
    nodes[static_cast<std::size_t>(index)].right = right;
    return index;
}

CellBsp read_cell_bsp(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 12) {
        throw common::ParseError("truncated BSP header");
    }
    CellBsp bsp;
    bsp.version = reader.u32(chunk.data_offset);
    bsp.leaf_count = reader.u32(chunk.data_offset + 4);
    bsp.depth = reader.u32(chunk.data_offset + 8);
    if (bsp.version != 1) {
        throw common::ParseError("unsupported BSP version");
    }
    if (bsp.depth == 0 || bsp.depth > 64) {
        throw common::ParseError("BSP depth is out of range");
    }
    if (bsp.leaf_count > chunk.data_size / 3) {
        throw common::ParseError("BSP leaf count is out of range");
    }
    std::size_t position = chunk.data_offset + 12;
    const std::size_t end = chunk.data_offset + chunk.data_size;
    bsp.nodes.reserve(static_cast<std::size_t>(bsp.leaf_count) * 2);
    read_bsp_node(reader, position, end, chunk.data_offset, bsp.depth, bsp.nodes);
    if (position != end) {
        throw common::ParseError("BSP chunk has a trailer at " +
                                 std::to_string(position - chunk.data_offset) + " of " +
                                 std::to_string(chunk.data_size));
    }
    std::uint32_t leaves = 0;
    for (const BspNode& node : bsp.nodes) {
        if (node.leaf) {
            ++leaves;
        }
    }
    if (leaves != bsp.leaf_count) {
        throw common::ParseError("BSP leaf count does not match the header");
    }
    return bsp;
}

void read_cell_tail(const common::Reader& reader, std::size_t start, std::size_t end,
                    LevelGeometry& geometry) {
    if (start == end) {
        return;
    }
    const std::vector<Chunk> chunks = read_chunks(reader, start, end);
    for (const Chunk& chunk : chunks) {
        if (chunk.name == "cells") {
            geometry.cell_grid = read_cell_grid(reader, chunk);
            geometry.has_cell_grid = true;
        } else if (chunk.name == "attrs") {
            geometry.level_attributes = read_attribute_tables(reader, chunk);
            geometry.has_level_attributes = true;
        } else if (chunk.name == "cellbsp") {
            geometry.cell_bsp = read_cell_bsp(reader, chunk);
            geometry.has_cell_bsp = true;
        }
    }
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
    read_cell_tail(reader, position, data.size(), geometry);
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
