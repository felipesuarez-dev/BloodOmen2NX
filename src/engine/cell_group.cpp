#include "bo2/engine/cell_group.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
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

std::string resolve_symbol_name(std::uint32_t relative,
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

LevelCell read_cell(const common::Reader& reader, std::size_t& position, std::size_t end,
                    const std::vector<std::uint16_t>& symbols,
                    const std::vector<std::string>& names) {
    if (position + 36 > end) {
        throw common::ParseError("truncated cell header");
    }
    LevelCell cell;
    cell.version = reader.u32(position);
    if (cell.version != 2) {
        throw common::ParseError("unsupported cell version");
    }
    cell.symbol = reader.u16(position + 4);
    if (cell.symbol < symbols.size() && symbols[cell.symbol] < names.size()) {
        cell.name = names[symbols[cell.symbol]];
    }
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

CellGrid read_cell_grid(const common::Reader& reader, const Chunk& chunk,
                        const std::vector<std::uint16_t>& symbols,
                        const std::vector<std::string>& names) {
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
        grid.cells.push_back(read_cell(reader, position, end, symbols, names));
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

std::vector<LevelLight> read_lights(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty light chunk");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    std::size_t position = chunk.data_offset + 4;
    const std::size_t end = chunk.data_offset + chunk.data_size;
    if (count > 65536) {
        throw common::ParseError("light count is out of range");
    }
    const auto need = [&position, end](std::size_t size) {
        if (position > end || size > end - position) {
            throw common::ParseError("truncated light record");
        }
    };
    std::vector<LevelLight> lights;
    lights.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        need(4);
        LevelLight light;
        light.version = reader.u32(position);
        position += 4;
        if (light.version != 3) {
            throw common::ParseError("unsupported light version");
        }
        need(4);
        light.handle = reader.u32(position);
        position += 4;
        need(12);
        light.position = read_vec3(reader, position);
        position += 12;
        need(16);
        for (std::size_t channel = 0; channel < 4; ++channel) {
            light.color[channel] = read_float(reader, position + channel * 4);
        }
        position += 16;
        need(20);
        for (std::size_t value = 0; value < 5; ++value) {
            light.params[value] = read_float(reader, position + value * 4);
        }
        position += 20;
        need(4);
        position += 3;
        light.type = reader.u8(position);
        position += 1;
        if (light.type == 1) {
            need(20);
            position += 20;
        } else if (light.type == 2) {
            need(12);
            position += 12;
        } else if (light.type != 0) {
            throw common::ParseError("unsupported light type");
        }
        need(4);
        light.flags = reader.u32(position);
        position += 4;
        need(4);
        const std::uint32_t entries = reader.u32(position);
        position += 4;
        if (entries > 65536) {
            throw common::ParseError("light index list is out of range");
        }
        need(static_cast<std::size_t>(entries) * 4);
        light.indices.reserve(entries);
        for (std::uint32_t entry = 0; entry < entries; ++entry) {
            light.indices.push_back(reader.u32(position));
            position += 4;
        }
        lights.push_back(light);
    }
    if (position != end) {
        throw common::ParseError("light chunk has a trailer");
    }
    return lights;
}

std::vector<LevelMarker> read_markers(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty marker chunk");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    std::size_t position = chunk.data_offset + 4;
    const std::size_t end = chunk.data_offset + chunk.data_size;
    if (count > 65536) {
        throw common::ParseError("marker count is out of range");
    }
    std::vector<LevelMarker> markers;
    markers.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (position + 56 > end) {
            throw common::ParseError("truncated marker record");
        }
        LevelMarker marker;
        marker.version = reader.u32(position);
        if (marker.version != 4) {
            throw common::ParseError("unsupported marker version");
        }
        marker.id_a = reader.u32(position + 4);
        marker.id_b = reader.u32(position + 8);
        marker.symbol_a = reader.u32(position + 12);
        marker.symbol_b = reader.u32(position + 16);
        marker.position = read_vec3(reader, position + 20);
        marker.normal = read_vec3(reader, position + 32);
        marker.flag = reader.u32(position + 44);
        marker.value = reader.u32(position + 48);
        marker.number = read_float(reader, position + 52);
        markers.push_back(marker);
        position += 56;
    }
    if (position != end) {
        throw common::ParseError("marker chunk has a trailer");
    }
    return markers;
}

std::vector<LevelInstance> read_instances(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("empty instance chunk");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    std::size_t position = chunk.data_offset + 4;
    const std::size_t end = chunk.data_offset + chunk.data_size;
    if (count > 65536) {
        throw common::ParseError("instance count is out of range");
    }
    std::vector<LevelInstance> instances;
    instances.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (position + 4 > end) {
            throw common::ParseError("truncated instance header");
        }
        const std::uint32_t version = reader.u32(position);
        std::size_t record_size = 0;
        if (version == 3) {
            record_size = 40;
        } else if (version == 4) {
            record_size = 52;
        } else {
            throw common::ParseError("unsupported instance version");
        }
        if (position + record_size > end) {
            throw common::ParseError("truncated instance record");
        }
        LevelInstance instance;
        instance.version = version;
        instance.name_symbol = reader.u16(position + 4);
        instance.external_symbol = reader.u16(position + 6);
        instance.model = reader.u32(position + 8);
        instance.position = read_vec3(reader, position + 12);
        if (version == 4) {
            instance.scale = read_vec3(reader, position + 24);
            instance.rotation = read_vec3(reader, position + 36);
            instance.flags = reader.u32(position + 48);
        } else {
            instance.rotation = read_vec3(reader, position + 24);
            instance.flags = reader.u32(position + 36);
            instance.scale = {1.0f, 1.0f, 1.0f};
        }
        instances.push_back(instance);
        position += record_size;
    }
    if (position != end) {
        throw common::ParseError("instance chunk has a trailer");
    }
    return instances;
}

Vec3 rotate_x(const Vec3& value, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {value.x, value.y * cosine - value.z * sine, value.y * sine + value.z * cosine};
}

Vec3 rotate_y(const Vec3& value, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {value.x * cosine + value.z * sine, value.y, -value.x * sine + value.z * cosine};
}

Vec3 rotate_z(const Vec3& value, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {value.x * cosine - value.y * sine, value.x * sine + value.y * cosine, value.z};
}

Vec3 rotate_euler_impl(const Vec3& value, const Vec3& angles) {
    return rotate_z(rotate_y(rotate_x(value, angles.x), angles.y), angles.z);
}

// `cellanim`: a count and that many clips, each in the layout of an `animreel`
// chunk from its inner version on. The cell group loader (FUN_00665200) reads
// the count, and FUN_00665b50 reads one clip per item with FUN_006689a0.
std::vector<AnimReel> read_cell_animations(const common::Reader& reader, const Chunk& chunk) {
    if (chunk.data_size < 4) {
        throw common::ParseError("cellanim has no count");
    }
    const std::uint32_t count = reader.u32(chunk.data_offset);
    std::size_t cursor = chunk.data_offset + 4;
    // The smallest clip is 28 bytes: three header words and the clip header.
    if (count > (chunk.end - cursor) / 28) {
        throw common::ParseError("cellanim count does not fit in the section");
    }
    std::vector<AnimReel> reels;
    reels.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::size_t used = 0;
        reels.push_back(parse_anim_reel_body(reader.data() + cursor, chunk.end - cursor, used));
        cursor += used;
    }
    if (cursor != chunk.end) {
        throw common::ParseError("cellanim has trailing bytes after its clips");
    }
    return reels;
}

void read_cell_tail(const common::Reader& reader, std::size_t start, std::size_t end,
                    const std::vector<std::uint16_t>& symbols,
                    const std::vector<std::string>& names, LevelGeometry& geometry) {
    if (start == end) {
        return;
    }
    const std::vector<Chunk> chunks = read_chunks(reader, start, end);
    for (const Chunk& chunk : chunks) {
        if (chunk.name == "cells") {
            geometry.cell_grid = read_cell_grid(reader, chunk, symbols, names);
            geometry.has_cell_grid = true;
        } else if (chunk.name == "attrs") {
            geometry.level_attributes = read_attribute_tables(reader, chunk);
            geometry.has_level_attributes = true;
        } else if (chunk.name == "cellinst") {
            geometry.instances = read_instances(reader, chunk);
            for (LevelInstance& instance : geometry.instances) {
                instance.name = resolve_symbol_name(instance.name_symbol, symbols, names);
                instance.external = resolve_symbol_name(instance.external_symbol, symbols, names);
            }
            geometry.has_instances = true;
        } else if (chunk.name == "cellanim") {
            geometry.cell_animations = read_cell_animations(reader, chunk);
        } else if (chunk.name == "cellight") {
            geometry.lights = read_lights(reader, chunk);
            geometry.has_lights = true;
        } else if (chunk.name == "cellmark") {
            geometry.markers = read_markers(reader, chunk);
            geometry.has_markers = true;
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
            add_texture_name(geometry, resolve_symbol_name(relative, symbols, names));
    }
    return result;
}

std::vector<MaterialLayer> read_material_layers(const common::Reader& reader,
                                                const std::vector<Chunk>& chunks) {
    std::vector<MaterialLayer> layers;
    const auto grow = [&layers](std::uint32_t start, std::uint32_t count) {
        const std::size_t required = static_cast<std::size_t>(start) + count;
        if (required > layers.size()) {
            layers.resize(required);
        }
    };

    const Chunk* colors = find_chunk(chunks, "mlyrcolr");
    if (colors != nullptr && colors->data_size >= 8) {
        const std::uint32_t start = reader.u32(colors->data_offset);
        const std::uint32_t count = reader.u32(colors->data_offset + 4);
        if (start > 65536 || count > 4096) {
            throw common::ParseError("material color range is out of bounds");
        }
        if (count != 0) {
            const std::size_t record_size = (colors->data_size - 8) / count;
            if (record_size < 48) {
                throw common::ParseError("material color record is short");
            }
            grow(start, count);
            for (std::uint32_t index = 0; index < count; ++index) {
                MaterialLayer& layer = layers[static_cast<std::size_t>(start) + index];
                for (std::size_t color = 0; color < 3; ++color) {
                    const std::size_t offset = colors->data_offset + 8 +
                                               static_cast<std::size_t>(index) * record_size +
                                               color * 16;
                    layer.colors[color] = read_vec3(reader, offset);
                    layer.alphas[color] = read_float(reader, offset + 12);
                }
                layer.has_colors = true;
            }
        }
    }

    const Chunk* controls = find_chunk(chunks, "mlyrctrl");
    if (controls != nullptr && controls->data_size >= 8) {
        const std::uint32_t start = reader.u32(controls->data_offset);
        const std::uint32_t count = reader.u32(controls->data_offset + 4);
        if (start > 65536 || count > 4096) {
            throw common::ParseError("material control range is out of bounds");
        }
        if (count != 0) {
            const std::size_t record_size = (controls->data_size - 8) / count;
            if (record_size < 12) {
                throw common::ParseError("material control record is short");
            }
            grow(start, count);
            for (std::uint32_t index = 0; index < count; ++index) {
                MaterialLayer& layer = layers[static_cast<std::size_t>(start) + index];
                for (std::size_t control = 0; control < 3; ++control) {
                    const std::size_t offset = controls->data_offset + 8 +
                                               static_cast<std::size_t>(index) * record_size +
                                               control * 12;
                    layer.controls[control] = {reader.u32(offset), read_float(reader, offset + 4),
                                               reader.u16(offset + 8), reader.u16(offset + 10)};
                }
                layer.has_controls = true;
            }
        }
    }
    return layers;
}

void parse_prims(const common::Reader& reader, const Chunk& chunk, std::uint32_t vertex_base,
                 std::uint32_t vertex_count, std::uint32_t model, std::uint32_t material_base,
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
            const std::uint32_t global_material = material_base + material;
            if (global_material >= geometry.materials.size()) {
                geometry.materials.resize(static_cast<std::size_t>(global_material) + 1);
            }
            geometry.triangles.push_back({vertex_base + a, vertex_base + b, vertex_base + c,
                                          global_material, model, 0xFFFFFFFFU, texture, -1});
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

ModelSkeleton& skeleton_for(LevelGeometry& geometry, std::uint32_t model) {
    for (ModelSkeleton& skeleton : geometry.skeletons) {
        if (skeleton.model == model) {
            return skeleton;
        }
    }
    geometry.skeletons.push_back(ModelSkeleton{});
    geometry.skeletons.back().model = model;
    return geometry.skeletons.back();
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
    const Chunk* lightmap_uv_set = nullptr;
    for (const Chunk& chunk : chunks) {
        if (chunk.name != "uvs" || chunk.data_size < 4) {
            continue;
        }
        const std::uint32_t set = reader.u32(chunk.data_offset);
        if (set == 0 && uv_set == nullptr) {
            uv_set = &chunk;
        } else if (set == 1 && lightmap_uv_set == nullptr) {
            lightmap_uv_set = &chunk;
        }
        if (uv_set != nullptr && lightmap_uv_set != nullptr) {
            break;
        }
    }
    if (uv_set == nullptr) {
        uv_set = lightmap_uv_set;
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
    const std::vector<Vec2> lightmap_uv_values =
        (lightmap_uv_set != nullptr) ? read_uvs(reader, *lightmap_uv_set) : uv_values;

    const std::uint32_t base = static_cast<std::uint32_t>(geometry.positions.size());
    geometry.positions.insert(geometry.positions.end(), vertices.begin(), vertices.end());
    geometry.normals.insert(geometry.normals.end(), vertex_normals.begin(), vertex_normals.end());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const std::uint32_t uv_index =
            (index < uv_index_values.size()) ? uv_index_values[index]
                                             : static_cast<std::uint32_t>(index);
        const Vec2 uv = uv_index < uv_values.size() ? uv_values[uv_index] : Vec2{};
        geometry.texcoords.push_back(uv);
        geometry.lightmap_texcoords.push_back(
            uv_index < lightmap_uv_values.size() ? lightmap_uv_values[uv_index] : uv);
    }
    const std::uint32_t material_base = static_cast<std::uint32_t>(geometry.materials.size());
    const std::vector<MaterialLayer> material_layers = read_material_layers(reader, chunks);
    geometry.materials.insert(geometry.materials.end(), material_layers.begin(),
                              material_layers.end());
    const std::vector<std::int32_t> material_texture =
        read_material_textures(reader, find_chunk(chunks, "texture"), symbols, names, geometry);
    parse_prims(reader, *prims, base, static_cast<std::uint32_t>(vertices.size()), model,
                material_base, material_texture, geometry);
    // The skin weights and joint indices live inside the rmd on character
    // models (the cell group also carries a top-level fallback).
    const Chunk* weights = find_chunk(chunks, "vtxweigh");
    const Chunk* joints = find_chunk(chunks, "jointidx");
    if (weights != nullptr || joints != nullptr) {
        ModelSkeleton& skeleton = skeleton_for(geometry, model);
        if (weights != nullptr && weights->data_size >= 4) {
            const std::uint32_t count = reader.u32(weights->data_offset);
            if (4 + static_cast<std::size_t>(count) * 4 <= weights->data_size) {
                skeleton.weights.reserve(count);
                for (std::uint32_t index = 0; index < count; ++index) {
                    skeleton.weights.push_back(
                        read_float(reader, weights->data_offset + 4 + index * 4));
                }
            }
        }
        if (joints != nullptr && joints->data_size >= 4) {
            const std::uint32_t count = reader.u32(joints->data_offset);
            if (4 + static_cast<std::size_t>(count) * 2 <= joints->data_size) {
                skeleton.joints.reserve(count);
                for (std::uint32_t index = 0; index < count; ++index) {
                    skeleton.joints.push_back(reader.u16(joints->data_offset + 4 + index * 2));
                }
            }
        }
    }
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

void place_instances(LevelGeometry& geometry) {
    geometry.placed_instances = 0;
    geometry.skipped_instances = 0;
    if (geometry.instances.empty() || geometry.model_ranges.empty()) {
        return;
    }
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texcoords;
    std::vector<Vec2> lightmap_texcoords;
    std::vector<LevelTriangle> triangles;
    geometry.instance_vertex_base.assign(geometry.instances.size(),
                                         std::numeric_limits<std::uint32_t>::max());
    for (std::size_t instance_index = 0; instance_index < geometry.instances.size();
         ++instance_index) {
        const LevelInstance& instance = geometry.instances[instance_index];
        if (instance.model >= geometry.model_ranges.size()) {
            ++geometry.skipped_instances;
            continue;
        }
        const ModelRange& range = geometry.model_ranges[instance.model];
        if (!range.present || range.vertex_count == 0 || range.triangle_count == 0) {
            ++geometry.skipped_instances;
            continue;
        }
        const std::uint32_t vertex_base = static_cast<std::uint32_t>(positions.size());
        geometry.instance_vertex_base[instance_index] = vertex_base;
        for (std::uint32_t index = 0; index < range.vertex_count; ++index) {
            const std::size_t source = static_cast<std::size_t>(range.first_vertex) + index;
            const Vec3 scaled{geometry.positions[source].x * instance.scale.x,
                              geometry.positions[source].y * instance.scale.y,
                              geometry.positions[source].z * instance.scale.z};
            positions.push_back(instance.position + rotate_euler_impl(scaled, instance.rotation));
            if (source < geometry.normals.size()) {
                normals.push_back(normalize(rotate_euler_impl(geometry.normals[source], instance.rotation)));
            } else {
                normals.push_back({0.0f, 0.0f, 0.0f});
            }
            texcoords.push_back(source < geometry.texcoords.size() ? geometry.texcoords[source]
                                                                  : Vec2{});
            if (!geometry.lightmap_texcoords.empty()) {
                lightmap_texcoords.push_back(source < geometry.lightmap_texcoords.size()
                                                 ? geometry.lightmap_texcoords[source]
                                                 : Vec2{});
            }
        }
        for (std::uint32_t index = 0; index < range.triangle_count; ++index) {
            const std::size_t source = static_cast<std::size_t>(range.first_triangle) + index;
            LevelTriangle triangle = geometry.triangles[source];
            const std::uint32_t first = range.first_vertex;
            const std::uint32_t last = range.first_vertex + range.vertex_count;
            if (triangle.a < first || triangle.a >= last || triangle.b < first ||
                triangle.b >= last || triangle.c < first || triangle.c >= last) {
                continue;
            }
            triangle.a = vertex_base + (triangle.a - first);
            triangle.b = vertex_base + (triangle.b - first);
            triangle.c = vertex_base + (triangle.c - first);
            triangle.instance = static_cast<std::uint32_t>(instance_index);
            triangles.push_back(triangle);
        }
        ++geometry.placed_instances;
    }
    if (geometry.placed_instances == 0) {
        return;
    }
    geometry.positions = std::move(positions);
    geometry.normals = std::move(normals);
    geometry.texcoords = std::move(texcoords);
    // The light-map UVs follow the placed vertices too; the renderer indexes
    // them with the placed triangle indices.
    if (!geometry.lightmap_texcoords.empty()) {
        geometry.lightmap_texcoords = std::move(lightmap_texcoords);
    }
    geometry.triangles = std::move(triangles);
    compute_bounds(geometry);
}

}  // namespace

Vec3 rotate_euler(const Vec3& value, const Vec3& rotation) {
    return rotate_euler_impl(value, rotation);
}

namespace {

void compute_collision_bounds(CollisionModel& model) {
    if (model.vertices.empty()) {
        return;
    }
    model.bounds.minimum = model.vertices.front();
    model.bounds.maximum = model.vertices.front();
    for (const Vec3& vertex : model.vertices) {
        model.bounds.minimum = {std::min(model.bounds.minimum.x, vertex.x),
                                std::min(model.bounds.minimum.y, vertex.y),
                                std::min(model.bounds.minimum.z, vertex.z)};
        model.bounds.maximum = {std::max(model.bounds.maximum.x, vertex.x),
                                std::max(model.bounds.maximum.y, vertex.y),
                                std::max(model.bounds.maximum.z, vertex.z)};
    }
}

// Decodes one collision record anchored at its 0xFFFFFF01 flag. `complete`
// requires the whole section chain to tile the payload down to its 28-byte
// trailer; the short form stops after the vertex block.
bool parse_collmodc_at(const common::Reader& reader, std::size_t flag_offset, bool complete,
                       CollisionModel& model) {
    model = CollisionModel{};
    const std::size_t size = reader.size();
    if (flag_offset + 8 > size || reader.u32(flag_offset) != 0xFFFFFF01U) {
        return false;
    }
    const std::uint32_t blob_size = reader.u32(flag_offset + 4);
    std::size_t position = flag_offset + 8;
    if (blob_size % 6 != 0 || blob_size > size - position) {
        return false;
    }
    const std::size_t point_count = blob_size / 6;
    if (point_count == 0) {
        return false;
    }
    model.vertices.reserve(point_count);
    for (std::size_t index = 0; index < point_count; ++index) {
        const std::size_t offset = position + index * 6;
        model.vertices.push_back(
            {static_cast<float>(static_cast<std::int16_t>(reader.u16(offset))),
             static_cast<float>(static_cast<std::int16_t>(reader.u16(offset + 2))),
             static_cast<float>(static_cast<std::int16_t>(reader.u16(offset + 4)))});
    }
    compute_collision_bounds(model);
    position += blob_size;
    // The vertex block is 4-byte aligned, so an odd point count leaves a
    // two-byte pad before the triangle list.
    position = (position + 3) & ~static_cast<std::size_t>(3);
    if (position + 8 > size) {
        model.valid = !complete;
        return !complete;
    }
    // Triangle list: the records the pair test walks (FUN_0062dfc0). A record
    // is one dword with three 9-bit indices, or two dwords with 16-bit indices
    // when its bit 31 is set. `triangle_at` maps the byte offset of a record
    // to its index, for the face slices below.
    const std::uint32_t index_bytes = reader.u32(position + 4);
    position += 8;
    if (index_bytes % 4 != 0 || index_bytes > size - position) {
        model = CollisionModel{};
        return false;
    }
    std::vector<std::int32_t> triangle_at(index_bytes / 4, -1);
    model.triangles.reserve(index_bytes / 4);
    for (std::size_t offset = 0; offset < index_bytes;) {
        const std::uint32_t record = reader.u32(position + offset);
        CollisionTriangle triangle;
        triangle.attribute = static_cast<std::uint8_t>(record >> 24);
        triangle.category = static_cast<std::uint8_t>((record >> 29) & 3U);
        triangle_at[offset / 4] = static_cast<std::int32_t>(model.triangles.size());
        if ((record & 0x80000000U) == 0) {
            triangle.a = (record >> 18) & 0x1FFU;
            triangle.b = (record >> 9) & 0x1FFU;
            triangle.c = record & 0x1FFU;
            offset += 4;
        } else {
            if (index_bytes - offset < 8) {
                model = CollisionModel{};
                return false;
            }
            triangle.a = record & 0xFFFFU;
            triangle.b = reader.u16(position + offset + 4);
            triangle.c = reader.u16(position + offset + 6);
            offset += 8;
        }
        model.triangles.push_back(triangle);
    }
    position += index_bytes;
    if (position + 4 > size) {
        model = CollisionModel{};
        return false;
    }
    // Solid spheres: 24-byte records of centre, radius, bone and flags.
    const std::uint32_t sphere_count = reader.u32(position);
    position += 4;
    if (sphere_count > (size - position) / 24) {
        model = CollisionModel{};
        return false;
    }
    model.spheres.reserve(sphere_count);
    for (std::uint32_t index = 0; index < sphere_count; ++index) {
        const std::size_t offset = position + static_cast<std::size_t>(index) * 24;
        CollisionSphere sphere;
        sphere.center = {read_float(reader, offset), read_float(reader, offset + 4),
                         read_float(reader, offset + 8)};
        sphere.radius = read_float(reader, offset + 12);
        sphere.bone = reader.u32(offset + 16);
        sphere.flags = reader.u32(offset + 20);
        if (std::isfinite(sphere.radius) && sphere.radius >= 0.0f) {
            model.spheres.push_back(sphere);
        }
    }
    position += static_cast<std::size_t>(sphere_count) * 24;
    if (position + 4 > size) {
        model = CollisionModel{};
        return false;
    }
    // Faces: anchor AABB corners, two neighbour faces and a triangle slice.
    const std::uint32_t face_count = reader.u32(position);
    position += 4;
    if (face_count > (size - position) / 20) {
        model = CollisionModel{};
        return false;
    }
    model.faces.reserve(face_count);
    for (std::uint32_t index = 0; index < face_count; ++index) {
        const std::size_t offset = position + static_cast<std::size_t>(index) * 20;
        const std::uint32_t anchor_a = reader.u16(offset);
        const std::uint32_t anchor_b = reader.u16(offset + 2);
        if (anchor_a >= model.vertices.size() || anchor_b >= model.vertices.size()) {
            continue;
        }
        const std::uint32_t triangle_offset = reader.u32(offset + 16);
        const std::uint16_t face_triangles = reader.u16(offset + 14);
        // The slice starts at the byte offset of a record and counts records.
        if (triangle_offset % 4 != 0) {
            continue;
        }
        std::size_t first_triangle = model.triangles.size();
        if (triangle_offset / 4 < triangle_at.size()) {
            if (triangle_at[triangle_offset / 4] < 0) {
                continue;  // the second dword of a wide record
            }
            first_triangle = static_cast<std::size_t>(triangle_at[triangle_offset / 4]);
        } else if (face_triangles != 0 || triangle_offset / 4 != triangle_at.size()) {
            continue;
        }
        if (first_triangle + face_triangles > model.triangles.size()) {
            continue;
        }
        CollisionFace face;
        const Vec3& first = model.vertices[anchor_a];
        const Vec3& second = model.vertices[anchor_b];
        face.bounds.minimum = {std::min(first.x, second.x), std::min(first.y, second.y),
                               std::min(first.z, second.z)};
        face.bounds.maximum = {std::max(first.x, second.x), std::max(first.y, second.y),
                               std::max(first.z, second.z)};
        face.adjacent[0] = reader.u32(offset + 4);
        face.adjacent[1] = reader.u32(offset + 8);
        face.first_triangle = static_cast<std::uint32_t>(first_triangle);
        face.triangle_count = face_triangles;
        face.has_triangles = face_triangles != 0;
        model.faces.push_back(face);
    }
    position += static_cast<std::size_t>(face_count) * 20;
    if (complete && size - position != 28) {
        model = CollisionModel{};
        return false;
    }
    model.valid = true;
    return true;
}

}  // namespace

CollisionModel parse_collmodc(const std::vector<std::uint8_t>& data) {
    CollisionModel model;
    const common::Reader reader(data);
    if (data.size() < 16) {
        return model;
    }
    std::size_t name_end = 0;
    while (name_end < data.size() && data[name_end] != 0) {
        ++name_end;
    }
    if (name_end + 5 > data.size()) {
        return model;
    }
    const std::uint32_t header_size = reader.u32(name_end + 1);
    const std::size_t header_end = name_end + 5;
    if (header_size <= data.size() - header_end) {
        const std::size_t flag_offset = header_end + header_size;
        if (parse_collmodc_at(reader, flag_offset, true, model) ||
            parse_collmodc_at(reader, flag_offset, false, model)) {
            return model;
        }
    }
    // Some payloads wrap the record in a second named header: the flag is not
    // where the outer header ends. Locate it by the section chain instead.
    for (std::size_t offset = 0; offset + 4 <= data.size(); ++offset) {
        if (reader.u32(offset) == 0xFFFFFF01U && parse_collmodc_at(reader, offset, true, model)) {
            return model;
        }
    }
    model = CollisionModel{};
    return model;
}

std::vector<SkeletonBone> parse_skeleton(const std::vector<std::uint8_t>& data) {
    const common::Reader reader(data);
    if (data.size() < 4) {
        throw common::ParseError("skeleton chunk is shorter than its count");
    }
    const std::uint32_t count = reader.u32(0);
    constexpr std::size_t kRecordBytes = 104;
    if (count > (data.size() - 4) / kRecordBytes) {
        throw common::ParseError("skeleton bone array overruns the chunk");
    }
    std::vector<SkeletonBone> bones;
    bones.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t offset = 4 + static_cast<std::size_t>(index) * kRecordBytes;
        SkeletonBone bone;
        bone.name_symbol = reader.u32(offset);
        bone.state_mask = reader.u32(offset + 0x04);
        bone.state_value = reader.u32(offset + 0x08);
        bone.bone_index = reader.u32(offset + 0x0c);
        bone.current_matrix = reader.u32(offset + 0x10);
        bone.parent_matrix = reader.u32(offset + 0x14);
        bone.first_collision_prim = reader.u32(offset + 0x18);
        bone.collision_prim_count = reader.u32(offset + 0x1c);
        bone.first_vertex = reader.u32(offset + 0x20);
        bone.vertex_count = reader.u32(offset + 0x24);
        bone.channel_mask = reader.u16(offset + 0x28);
        bone.rotation_order = reader.u16(offset + 0x2a);
        bone.pivot = {read_float(reader, offset + 0x2c), read_float(reader, offset + 0x30),
                      read_float(reader, offset + 0x34)};
        bone.rest_orientation = {read_float(reader, offset + 0x38),
                                 read_float(reader, offset + 0x3c),
                                 read_float(reader, offset + 0x40)};
        bone.joint_orientation = {read_float(reader, offset + 0x44),
                                  read_float(reader, offset + 0x48),
                                  read_float(reader, offset + 0x4c)};
        bone.rotation = {read_float(reader, offset + 0x50), read_float(reader, offset + 0x54),
                         read_float(reader, offset + 0x58)};
        bone.scale = {read_float(reader, offset + 0x5c), read_float(reader, offset + 0x60),
                      read_float(reader, offset + 0x64)};
        bones.push_back(bone);
    }
    return bones;
}

std::vector<LevelCollisionBox> parse_collmodc_boxes(const std::vector<std::uint8_t>& data) {
    const CollisionModel model = parse_collmodc(data);
    std::vector<LevelCollisionBox> boxes;
    if (!model.valid) {
        return boxes;
    }
    const auto same = [](const LevelCollisionBox& left, const LevelCollisionBox& right) {
        return left.minimum.x == right.minimum.x && left.minimum.y == right.minimum.y &&
               left.minimum.z == right.minimum.z && left.maximum.x == right.maximum.x &&
               left.maximum.y == right.maximum.y && left.maximum.z == right.maximum.z;
    };
    boxes.push_back(model.bounds);
    for (const CollisionFace& face : model.faces) {
        bool duplicate = false;
        for (const LevelCollisionBox& box : boxes) {
            if (same(box, face.bounds)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            boxes.push_back(face.bounds);
        }
    }
    return boxes;
}

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
    geometry.model_ranges.assign(model_count, ModelRange{});
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
        const Chunk* collision = find_chunk(chunks, "collmodc");
        if (collision != nullptr) {
            LevelCollision entry;
            entry.model = model;
            entry.name = name;
            entry.data.assign(data.begin() + static_cast<std::ptrdiff_t>(collision->data_offset),
                              data.begin() + static_cast<std::ptrdiff_t>(collision->data_offset +
                                                                         collision->data_size));
            geometry.collisions.push_back(std::move(entry));
        }
        const Chunk* skeleton = find_chunk(chunks, "skel");
        if (skeleton == nullptr) {
            skeleton = find_chunk(chunks, "modlskel");
        }
        const Chunk* weights = find_chunk(chunks, "vtxweigh");
        const Chunk* joints = find_chunk(chunks, "jointidx");
        if (skeleton != nullptr || weights != nullptr || joints != nullptr) {
            ModelSkeleton& entry = skeleton_for(geometry, model);
            if (skeleton != nullptr && skeleton->data_size >= 4) {
                const std::uint32_t count = reader.u32(skeleton->data_offset);
                if (count <= (skeleton->data_size - 4) / 104) {
                    const auto begin = data.begin() + static_cast<std::ptrdiff_t>(skeleton->data_offset);
                    entry.bones = parse_skeleton(
                        std::vector<std::uint8_t>(begin, begin + skeleton->data_size));
                }
            }
            // The rmd parser already filled the weights and joints when they
            // are subchunks; these top-level copies are the cell group variant.
            if (weights != nullptr && weights->data_size >= 4 && entry.weights.empty()) {
                const std::uint32_t count = reader.u32(weights->data_offset);
                if (4 + static_cast<std::size_t>(count) * 4 <= weights->data_size) {
                    entry.weights.reserve(count);
                    for (std::uint32_t index = 0; index < count; ++index) {
                        entry.weights.push_back(
                            read_float(reader, weights->data_offset + 4 + index * 4));
                    }
                }
            }
            if (joints != nullptr && joints->data_size >= 4 && entry.joints.empty()) {
                const std::uint32_t count = reader.u32(joints->data_offset);
                if (4 + static_cast<std::size_t>(count) * 2 <= joints->data_size) {
                    entry.joints.reserve(count);
                    for (std::uint32_t index = 0; index < count; ++index) {
                        entry.joints.push_back(
                            reader.u16(joints->data_offset + 4 + index * 2));
                    }
                }
            }
        }
        const std::size_t first_vertex = geometry.positions.size();
        const std::size_t first_triangle = geometry.triangles.size();
        if (rmd == nullptr) {
            ++geometry.skipped_models;
        } else {
            parse_model(reader, *rmd, model, symbols, names, geometry);
            ++geometry.model_count;
            ModelRange& range = geometry.model_ranges[model];
            range.first_vertex = static_cast<std::uint32_t>(first_vertex);
            range.vertex_count = static_cast<std::uint32_t>(geometry.positions.size() - first_vertex);
            range.first_triangle = static_cast<std::uint32_t>(first_triangle);
            range.triangle_count =
                static_cast<std::uint32_t>(geometry.triangles.size() - first_triangle);
            range.present = true;
        }
        position = model_end;
    }
    read_cell_tail(reader, position, data.size(), symbols, names, geometry);
    // Cell membership: each cell's `terrains` list references the `cellmdls`
    // models it draws, so it is a hint, not a unique owner (model 649 is
    // listed by cell 5 but its instance sits inside cell 0). Prefer the cell
    // whose bounds contain the instance and that lists its model, then the
    // bounds alone, and only then the list.
    std::vector<std::int32_t> model_cells(geometry.model_ranges.size(), -1);
    for (std::size_t cell_index = 0; cell_index < geometry.cell_grid.cells.size(); ++cell_index) {
        for (const std::uint32_t model : geometry.cell_grid.cells[cell_index].terrains) {
            if (model < model_cells.size() && model_cells[model] < 0) {
                model_cells[model] = static_cast<std::int32_t>(cell_index);
            }
        }
    }
    geometry.instance_cells.assign(geometry.instances.size(), -1);
    for (std::size_t index = 0; index < geometry.instances.size(); ++index) {
        const LevelInstance& instance = geometry.instances[index];
        std::int32_t spatial = -1;
        std::int32_t listed = -1;
        for (std::size_t cell_index = 0; cell_index < geometry.cell_grid.cells.size(); ++cell_index) {
            const LevelCell& cell = geometry.cell_grid.cells[cell_index];
            if (instance.position.x < cell.minimum.x || instance.position.x > cell.maximum.x ||
                instance.position.y < cell.minimum.y || instance.position.y > cell.maximum.y ||
                instance.position.z < cell.minimum.z || instance.position.z > cell.maximum.z) {
                continue;
            }
            if (spatial < 0) {
                spatial = static_cast<std::int32_t>(cell_index);
            }
            if (std::find(cell.terrains.begin(), cell.terrains.end(), instance.model) !=
                cell.terrains.end()) {
                listed = static_cast<std::int32_t>(cell_index);
                break;
            }
        }
        if (listed >= 0) {
            geometry.instance_cells[index] = listed;
        } else if (spatial >= 0) {
            geometry.instance_cells[index] = spatial;
        } else if (instance.model < model_cells.size()) {
            geometry.instance_cells[index] = model_cells[instance.model];
        }
    }
    place_instances(geometry);
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

LevelGeometry parse_model_asset(const std::vector<std::uint8_t>& data,
                                const std::vector<std::uint16_t>& symbols,
                                const std::vector<std::string>& names) {
    if (data.size() < 20) {
        throw common::ParseError("model asset is shorter than its header");
    }
    const common::Reader reader(data);
    if (reader.u32(0) != 105) {
        throw common::ParseError("unsupported model asset version");
    }
    const std::string name = reader.cstr(4, data.size());
    const std::size_t name_end = 4 + name.size();
    const std::uint32_t size = reader.u32(name_end + 1);
    const std::size_t chunks_start = name_end + 9;
    const std::size_t end = 4 + size;
    if (size < 16 || chunks_start > end || end > data.size()) {
        throw common::ParseError("bad model asset size");
    }
    const std::vector<Chunk> chunks = read_chunks(reader, chunks_start, end);
    const Chunk* rmd = find_chunk(chunks, "rmd");
    if (rmd == nullptr) {
        throw common::ParseError("model asset has no rmd chunk");
    }
    LevelGeometry geometry;
    geometry.model_ranges.assign(1, ModelRange{});
    parse_model(reader, *rmd, 0, symbols, names, geometry);
    // The bone records are a top-level chunk on the character model asset
    // (the `rmd` parser only fills the weights and joints).
    const Chunk* skeleton = find_chunk(chunks, "modlskel");
    if (skeleton == nullptr) {
        skeleton = find_chunk(chunks, "skel");
    }
    if (skeleton != nullptr && skeleton->data_size >= 4) {
        const std::uint32_t count = reader.u32(skeleton->data_offset);
        if (count <= (skeleton->data_size - 4) / 104) {
            const auto begin = data.begin() + static_cast<std::ptrdiff_t>(skeleton->data_offset);
            skeleton_for(geometry, 0).bones = parse_skeleton(
                std::vector<std::uint8_t>(begin, begin + skeleton->data_size));
        }
    }
    ModelRange& range = geometry.model_ranges[0];
    range.first_vertex = 0;
    range.vertex_count = static_cast<std::uint32_t>(geometry.positions.size());
    range.first_triangle = 0;
    range.triangle_count = static_cast<std::uint32_t>(geometry.triangles.size());
    range.present = !geometry.triangles.empty();
    geometry.model_count = range.present ? 1 : 0;
    compute_bounds(geometry);
    return geometry;
}

std::vector<LevelLightMap> parse_lightmaps(const std::vector<std::uint8_t>& data) {
    std::vector<LevelLightMap> maps;
    if (data.size() < 12) {
        return maps;
    }
    const common::Reader reader(data);
    if (reader.u32(0) != 1) {
        throw common::ParseError("unsupported light map version");
    }
    const std::uint32_t group_length = reader.u32(8);
    if (group_length == 0 || group_length > 512 ||
        12 + group_length + 12 > data.size()) {
        return maps;
    }
    std::set<std::string> seen;
    std::map<std::string, std::size_t> group_ends;
    std::size_t position = 12 + group_length + 12;
    while (position + 4 <= data.size()) {
        const std::uint32_t name_length = reader.u32(position);
        bool valid = name_length >= 4 && name_length <= 4096 &&
                     position + 4 + name_length + 12 <= data.size();
        bool has_dash = false;
        if (valid) {
            for (std::uint32_t index = 0; index < name_length; ++index) {
                const std::uint8_t value = data[position + 4 + index];
                if (value < 32 || value > 126) {
                    valid = false;
                    break;
                }
                if (value == '-') {
                    has_dash = true;
                }
            }
        }
        if (valid && has_dash) {
            const std::size_t record = position + 4 + name_length;
            const std::uint16_t version = reader.u16(record);
            const std::uint8_t format = reader.u8(record + 2);
            const std::uint16_t width = reader.u16(record + 8);
            const std::uint16_t height = reader.u16(record + 10);
            const std::size_t palette_offset = record + 12;
            const std::size_t indices_offset = palette_offset + 64;
            const std::size_t index_bytes =
                (static_cast<std::size_t>(width) * height + 1) / 2;
            if (version == 1 && format == 4 && width > 0 && height > 0 && width <= 2048 &&
                height <= 2048 && indices_offset + index_bytes <= data.size()) {
                LevelLightMap map;
                map.name.assign(reinterpret_cast<const char*>(data.data() + position + 4),
                                name_length);
                const std::size_t separator = map.name.find('-');
                map.group = separator == std::string::npos ? map.name : map.name.substr(0, separator);
                std::array<std::uint8_t, 16> luminance{};
                for (std::size_t index = 0; index < luminance.size(); ++index) {
                    luminance[index] = static_cast<std::uint8_t>(
                        (reader.u32(palette_offset + index * 4) >> 24) & 0xFF);
                }
                map.image.width = width;
                map.image.height = height;
                map.image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
                for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height;
                     ++index) {
                    const std::uint8_t byte = data[indices_offset + index / 2];
                    const std::uint8_t nibble =
                        (index % 2 == 0) ? static_cast<std::uint8_t>(byte >> 4)
                                         : static_cast<std::uint8_t>(byte & 0x0F);
                    const std::uint8_t value = luminance[nibble];
                    map.image.rgba[index * 4 + 0] = value;
                    map.image.rgba[index * 4 + 1] = value;
                    map.image.rgba[index * 4 + 2] = value;
                    map.image.rgba[index * 4 + 3] = 255;
                }
                group_ends[map.group] = indices_offset + index_bytes;
                if (seen.insert(map.name).second) {
                    maps.push_back(std::move(map));
                }
                position = indices_offset + index_bytes;
                continue;
            }
        }
        ++position;
    }
    // After a group's last entry: u32 vertex count, then one RGBA colour of
    // four f32 per vertex. FUN_006368c0 rejects a count above 0x8000.
    constexpr std::uint32_t kMaxGroupVertices = 0x8000;
    std::map<std::string, std::shared_ptr<const std::vector<Vec3>>> group_colors;
    for (LevelLightMap& map : maps) {
        const auto found = group_ends.find(map.group);
        if (found == group_ends.end() || found->second + 4 > data.size()) {
            continue;
        }
        const std::uint32_t count = reader.u32(found->second);
        if (count <= 1000000) {
            map.group_vertex_count = count;
        }
        const auto cached = group_colors.find(map.group);
        if (cached != group_colors.end()) {
            map.group_colors = cached->second;
            continue;
        }
        std::shared_ptr<std::vector<Vec3>> colors;
        const std::size_t first = found->second + 4;
        if (count > 0 && count <= kMaxGroupVertices &&
            first + static_cast<std::size_t>(count) * 16 <= data.size()) {
            colors = std::make_shared<std::vector<Vec3>>();
            colors->reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                const std::size_t at = first + static_cast<std::size_t>(index) * 16;
                colors->push_back({read_float(reader, at), read_float(reader, at + 4), read_float(reader, at + 8)});
            }
        }
        group_colors.emplace(map.group, colors);
        map.group_colors = colors;
    }
    return maps;
}

void assign_model_lightmaps(LevelGeometry& geometry,
                            const std::vector<LevelLightMap>& lightmaps) {
    if (lightmaps.empty() || geometry.model_ranges.empty()) {
        return;
    }
    const auto normalized = [](const std::string& name) {
        std::string result;
        result.reserve(name.size());
        for (char character : name) {
            if (character == '/') {
                result.push_back('-');
            } else {
                result.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }
        }
        return result;
    };
    struct Group {
        std::string name;
        std::uint32_t vertex_count = 0;
        std::vector<const LevelLightMap*> entries;
    };
    std::vector<Group> groups;
    std::map<std::string, std::size_t> group_index;
    for (const LevelLightMap& map : lightmaps) {
        auto found = group_index.find(map.group);
        if (found == group_index.end()) {
            group_index.emplace(map.group, groups.size());
            groups.push_back({map.group, map.group_vertex_count, {&map}});
        } else {
            Group& group = groups[found->second];
            if (group.vertex_count == 0) {
                group.vertex_count = map.group_vertex_count;
            }
            group.entries.push_back(&map);
        }
    }
    std::map<std::uint32_t, std::vector<const Group*>> groups_by_count;
    for (const Group& group : groups) {
        groups_by_count[group.vertex_count].push_back(&group);
    }
    std::vector<const Group*> model_groups(geometry.model_ranges.size(), nullptr);
    std::map<std::uint32_t, std::size_t> next_group;
    for (std::size_t model = 0; model < geometry.model_ranges.size(); ++model) {
        const ModelRange& range = geometry.model_ranges[model];
        if (!range.present) {
            continue;
        }
        const auto found = groups_by_count.find(range.vertex_count);
        if (found == groups_by_count.end()) {
            continue;
        }
        std::size_t& index = next_group[range.vertex_count];
        if (index < found->second.size()) {
            model_groups[model] = found->second[index];
            ++index;
        }
    }
    // A placed instance is a Terrain named after the instance (FUN_00682c50);
    // the Terrain looks its lighting group up by the lower-cased name
    // (FUN_005cf670, FUN_0060b460). A repeated name is renamed "name|N"
    // first (FUN_006827f0) and no .slm group carries a '|', so only the first
    // instance of a name is lit. Without a symbol table the instances have no
    // names, and the vertex-count match below stands in.
    std::vector<const Group*> instance_groups(geometry.instances.size(), nullptr);
    std::set<std::string> taken_names;
    bool named = false;
    for (std::size_t index = 0; index < geometry.instances.size(); ++index) {
        const LevelInstance& instance = geometry.instances[index];
        if (instance.name.empty()) {
            continue;
        }
        named = true;
        std::string key;
        key.reserve(instance.name.size());
        for (const char character : instance.name) {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        if (!taken_names.insert(key).second) {
            continue;  // renamed "name|N" by the original: no group
        }
        const auto found = group_index.find(key);
        if (found != group_index.end()) {
            instance_groups[index] = &groups[found->second];
        }
    }
    std::map<std::uint32_t, std::size_t> next_instance_group;
    for (std::size_t index = 0; index < geometry.instances.size() && !named; ++index) {
        const LevelInstance& instance = geometry.instances[index];
        if (instance.model >= geometry.model_ranges.size()) {
            continue;
        }
        const ModelRange& range = geometry.model_ranges[instance.model];
        if (!range.present) {
            continue;
        }
        const auto found = groups_by_count.find(range.vertex_count);
        if (found == groups_by_count.end()) {
            continue;
        }
        std::size_t& next = next_instance_group[range.vertex_count];
        if (next < found->second.size()) {
            instance_groups[index] = found->second[next];
            ++next;
        }
    }
    // Per-vertex colours of each lit instance: the group's array, one colour
    // per model vertex in model order (the same order place_instances uses).
    for (std::size_t index = 0; index < geometry.instances.size(); ++index) {
        const Group* group = instance_groups[index];
        if (group == nullptr || index >= geometry.instance_vertex_base.size()) {
            continue;
        }
        const std::uint32_t base = geometry.instance_vertex_base[index];
        const LevelInstance& instance = geometry.instances[index];
        if (base == std::numeric_limits<std::uint32_t>::max() ||
            instance.model >= geometry.model_ranges.size()) {
            continue;
        }
        const std::shared_ptr<const std::vector<Vec3>>& colors = group->entries.front()->group_colors;
        const std::uint32_t count = geometry.model_ranges[instance.model].vertex_count;
        if (colors == nullptr || colors->size() != count ||
            static_cast<std::size_t>(base) + count > geometry.positions.size()) {
            continue;
        }
        if (geometry.vertex_colors.size() != geometry.positions.size()) {
            geometry.vertex_colors.assign(geometry.positions.size(), Vec3{1.0f, 1.0f, 1.0f});
        }
        std::copy(colors->begin(), colors->end(),
                  geometry.vertex_colors.begin() + static_cast<std::ptrdiff_t>(base));
    }
    std::map<const LevelLightMap*, std::int32_t> image_index;
    for (LevelTriangle& triangle : geometry.triangles) {
        const Group* match = nullptr;
        const bool placed = triangle.instance < instance_groups.size();
        if (placed) {
            match = instance_groups[triangle.instance];
        }
        // A named instance without a group is unlit in the original; only
        // unplaced or unnamed geometry falls back to the vertex-count match.
        if (match == nullptr && !(placed && named) && triangle.model < model_groups.size()) {
            match = model_groups[triangle.model];
        }
        if (match == nullptr) {
            continue;
        }
        if (match == nullptr || triangle.texture < 0 ||
            static_cast<std::size_t>(triangle.texture) >= geometry.texture_names.size()) {
            continue;
        }
        const std::string texture = normalized(geometry.texture_names[triangle.texture]);
        const LevelLightMap* entry = nullptr;
        for (const LevelLightMap* candidate : match->entries) {
            if (candidate->name.size() >= texture.size() &&
                candidate->name.compare(candidate->name.size() - texture.size(), texture.size(),
                                        texture) == 0) {
                entry = candidate;
                break;
            }
        }
        if (entry == nullptr) {
            continue;
        }
        auto found = image_index.find(entry);
        if (found == image_index.end()) {
            const std::int32_t index = static_cast<std::int32_t>(geometry.model_lightmaps.size());
            geometry.model_lightmaps.push_back(entry->image);
            image_index.emplace(entry, index);
            triangle.lightmap = index;
        } else {
            triangle.lightmap = found->second;
        }
    }
}

void assign_named_model_lightmaps(LevelGeometry& geometry,
                                  const std::vector<LevelLightMap>& lightmaps,
                                  const std::string& model_name) {
    if (lightmaps.empty() || geometry.triangles.empty() || model_name.empty()) {
        return;
    }
    const auto normalized = [](const std::string& value) {
        std::string result;
        result.reserve(value.size());
        for (char character : value) {
            if (character == '\\') {
                result.push_back('/');
            } else {
                result.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }
        }
        return result;
    };
    const std::string wanted = normalized(model_name);
    std::vector<const LevelLightMap*> entries;
    for (const LevelLightMap& map : lightmaps) {
        const std::string group = normalized(map.group);
        if (group == wanted || (group.size() > wanted.size() &&
                                group.compare(group.size() - wanted.size(), wanted.size(),
                                              wanted) == 0 &&
                                group[group.size() - wanted.size() - 1] == '/')) {
            entries.push_back(&map);
        }
    }
    if (entries.empty()) {
        return;
    }
    std::map<const LevelLightMap*, std::int32_t> image_index;
    for (LevelTriangle& triangle : geometry.triangles) {
        if (triangle.texture < 0 ||
            static_cast<std::size_t>(triangle.texture) >= geometry.texture_names.size()) {
            continue;
        }
        const std::string texture = normalized(geometry.texture_names[triangle.texture]);
        const LevelLightMap* match = nullptr;
        for (const LevelLightMap* candidate : entries) {
            const std::string name = normalized(candidate->name);
            if (name.size() >= texture.size() &&
                name.compare(name.size() - texture.size(), texture.size(), texture) == 0) {
                match = candidate;
                break;
            }
        }
        if (match == nullptr) {
            continue;
        }
        const auto found = image_index.find(match);
        if (found == image_index.end()) {
            const std::int32_t index =
                static_cast<std::int32_t>(geometry.model_lightmaps.size());
            geometry.model_lightmaps.push_back(match->image);
            image_index.emplace(match, index);
            triangle.lightmap = index;
        } else {
            triangle.lightmap = found->second;
        }
    }
}

Vec3 material_color(const LevelGeometry& geometry, const LevelTriangle& triangle) {
    static const Vec3 palette[8] = {
        {0.62f, 0.56f, 0.48f}, {0.50f, 0.52f, 0.56f}, {0.58f, 0.47f, 0.38f},
        {0.45f, 0.50f, 0.43f}, {0.54f, 0.44f, 0.42f}, {0.60f, 0.56f, 0.44f},
        {0.42f, 0.46f, 0.52f}, {0.52f, 0.48f, 0.58f},
    };
    const std::size_t index = static_cast<std::size_t>(triangle.material);
    if (index < geometry.materials.size() && geometry.materials[index].has_colors) {
        Vec3 color = geometry.materials[index].colors[1];
        if (geometry.materials[index].has_controls) {
            const float weight = geometry.materials[index].controls[1].weight;
            if (weight >= 0.0f && weight <= 1.0f) {
                color = color * weight;
            }
        }
        return color;
    }
    return palette[index % 8];
}

Vec3 material_tint(const LevelGeometry& geometry, const LevelTriangle& triangle,
                   MaterialMode mode) {
    if (mode == MaterialMode::Off) {
        return {1.0f, 1.0f, 1.0f};
    }
    const std::size_t index = static_cast<std::size_t>(triangle.material);
    if (index >= geometry.materials.size()) {
        return {1.0f, 1.0f, 1.0f};
    }
    const MaterialLayer& layer = geometry.materials[index];
    if (!layer.has_colors) {
        return {1.0f, 1.0f, 1.0f};
    }
    Vec3 color{};
    if (mode == MaterialMode::Diffuse) {
        color = layer.colors[1];
    } else {
        color = layer.colors[0] + layer.colors[1] + layer.colors[2];
        if (mode == MaterialMode::Layers2x) {
            color = color * 2.0f;
        }
    }
    if (layer.has_controls) {
        const float weight = layer.controls[1].weight;
        if (weight >= 0.0f && weight <= 1.0f) {
            color = color * weight;
        }
    }
    const float limit = (mode == MaterialMode::Layers2x) ? 2.0f : 1.0f;
    color.x = std::clamp(color.x, 0.0f, limit);
    color.y = std::clamp(color.y, 0.0f, limit);
    color.z = std::clamp(color.z, 0.0f, limit);
    return color;
}

}  // namespace bo2::engine
