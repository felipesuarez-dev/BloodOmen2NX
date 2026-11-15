#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/data_check.hpp"
#include "bo2/engine/dds.hpp"
#include "bo2/engine/erg.hpp"
#include "bo2/engine/math.hpp"
#include "bo2/engine/pixmap_font.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

template <typename Function>
bool throws(Function&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF));
    }
}

void put_float(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    put32(out, bits);
}

void patch32(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        out[offset + static_cast<std::size_t>(index)] =
            static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
    }
}

void append(std::vector<std::uint8_t>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

struct Item {
    std::string type;
    std::string name;
    std::vector<std::uint8_t> payload;
    std::vector<std::uint16_t> symbols;
};

std::vector<std::uint8_t> build_big(const std::vector<Item>& items) {
    std::vector<std::uint8_t> directory;
    append(directory, "symlist");
    directory.push_back(0);
    put32(directory, 0);
    put32(directory, 0);
    put32(directory, static_cast<std::uint32_t>(items.size()));
    for (const Item& item : items) {
        append(directory, item.name);
        directory.push_back(0);
    }
    if ((directory.size() & 1U) != 0U) {
        directory.push_back(0);
    }
    patch32(directory, 8, static_cast<std::uint32_t>(directory.size()));

    std::vector<std::uint8_t> out;
    append(out, "goefile");
    out.push_back(0);
    put32(out, 0);
    put32(out, 1);
    out.insert(out.end(), directory.begin(), directory.end());

    for (std::size_t index = 0; index < items.size(); ++index) {
        const Item& item = items[index];
        std::vector<std::uint8_t> body;
        const std::size_t symbols_size = (20 + item.symbols.size() * 2 + 3) & ~std::size_t{3U};
        append(body, "symlist");
        body.push_back(0);
        put32(body, static_cast<std::uint32_t>(symbols_size));
        put32(body, 0);
        put32(body, static_cast<std::uint32_t>(item.symbols.size()));
        for (std::uint16_t symbol : item.symbols) {
            put16(body, symbol);
        }
        while (body.size() < symbols_size) {
            body.push_back(0);
        }
        put32(body, static_cast<std::uint32_t>(item.payload.size()));
        body.insert(body.end(), item.payload.begin(), item.payload.end());
        std::size_t entry_length = 20 + body.size();
        while ((entry_length & 3U) != 0U) {
            body.push_back(0);
            ++entry_length;
        }

        std::string type = item.type;
        while (type.size() < 8) {
            type.push_back('\0');
        }
        append(out, type);
        put32(out, static_cast<std::uint32_t>(entry_length));
        put32(out, 0);
        put16(out, static_cast<std::uint16_t>(index));
        put16(out, 0);
        out.insert(out.end(), body.begin(), body.end());
    }
    patch32(out, 8, static_cast<std::uint32_t>(out.size()));
    return out;
}

std::vector<std::uint8_t> make_dds(std::uint32_t width, std::uint32_t height,
                                  std::uint32_t fourcc, std::uint32_t pixel_flags,
                                  std::uint32_t rgb_bits, std::uint32_t red_mask,
                                  std::uint32_t green_mask, std::uint32_t blue_mask,
                                  std::uint32_t alpha_mask,
                                  const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> data(128, 0);
    patch32(data, 0, 0x20534444U);
    patch32(data, 4, 124);
    patch32(data, 8, 0x00081007U);
    patch32(data, 12, height);
    patch32(data, 16, width);
    patch32(data, 20, static_cast<std::uint32_t>(payload.size()));
    patch32(data, 28, 1);
    patch32(data, 76, 32);
    patch32(data, 80, pixel_flags);
    patch32(data, 84, fourcc);
    patch32(data, 88, rgb_bits);
    patch32(data, 92, red_mask);
    patch32(data, 96, green_mask);
    patch32(data, 100, blue_mask);
    patch32(data, 104, alpha_mask);
    patch32(data, 108, 0x1000U);
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

void append_chunk(std::vector<std::uint8_t>& out, const std::string& name,
                  const std::vector<std::uint8_t>& payload) {
    std::string tag = name;
    tag.resize(8, '\0');
    append(out, tag);
    put32(out, static_cast<std::uint32_t>(16 + payload.size()));
    put32(out, 0);
    out.insert(out.end(), payload.begin(), payload.end());
}

void append_table(std::vector<std::uint8_t>& out, const std::string& name,
                  const std::vector<std::uint32_t>& values) {
    std::string tag = name;
    tag.resize(8, '\0');
    append(out, tag);
    put32(out, static_cast<std::uint32_t>(12 + values.size() * 4));
    for (std::uint32_t value : values) {
        put32(out, value);
    }
}

void append_cell_grid(std::vector<std::uint8_t>& out) {
    std::vector<std::uint8_t> attributes;
    append_table(attributes, "default", {0x00b00103U, 3U, 0xFFFFFFFFU});

    std::vector<std::uint8_t> portal;
    put32(portal, 1);
    put32(portal, 2);
    put16(portal, 2);
    put16(portal, 3);
    put32(portal, 3);
    for (const auto& point : std::vector<std::array<float, 3>>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}) {
        put_float(portal, point[0]);
        put_float(portal, point[1]);
        put_float(portal, point[2]);
    }
    put_float(portal, 0.0f);
    put_float(portal, 0.0f);
    put_float(portal, 1.0f);
    put32(portal, 5);

    std::vector<std::uint8_t> terrains;
    put32(terrains, 2);
    put32(terrains, 10);
    put32(terrains, 20);

    std::vector<std::uint8_t> container;
    append_chunk(container, "cellattr", attributes);
    append_chunk(container, "portal2", portal);
    append_chunk(container, "terrains", terrains);
    append_chunk(container, "dbgmodel", {0xAA, 0xBB, 0xCC});

    std::vector<std::uint8_t> cell;
    put32(cell, 2);
    put16(cell, 7);
    put16(cell, 0xFFFF);
    put32(cell, 0x1234);
    for (float value : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}) {
        put_float(cell, value);
    }
    append_chunk(cell, "default", container);

    put32(out, 1);
    out.insert(out.end(), cell.begin(), cell.end());
}

void append_cell_bsp(std::vector<std::uint8_t>& out) {
    put32(out, 1);
    put32(out, 2);
    put32(out, 2);
    out.push_back(1);
    while ((out.size() & 3U) != 0U) {
        out.push_back(0);
    }
    put_float(out, 0.0f);
    put_float(out, 1.0f);
    put_float(out, 0.0f);
    put_float(out, -5.0f);
    out.push_back(0);
    if ((out.size() & 1U) != 0U) {
        out.push_back(0);
    }
    put16(out, 0xFFFF);
    out.push_back(0);
    if ((out.size() & 1U) != 0U) {
        out.push_back(0);
    }
    put16(out, 3);
}

// A model asset with four vertices and no triangle group. It is the smallest
// payload that parse_model_asset accepts.
std::vector<std::uint8_t> build_model_asset() {
    std::vector<std::uint8_t> positions;
    put32(positions, 4);
    for (const auto& point :
         std::vector<std::array<float, 3>>{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}) {
        put_float(positions, point[0]);
        put_float(positions, point[1]);
        put_float(positions, point[2]);
    }
    std::vector<std::uint8_t> prims;
    put32(prims, 0);
    std::vector<std::uint8_t> record;
    append_chunk(record, "position", positions);
    append_chunk(record, "prims", prims);
    std::vector<std::uint8_t> rmd;
    put32(rmd, 2);
    std::string name = "default";
    name.resize(8, '\0');
    append(rmd, name);
    put32(rmd, static_cast<std::uint32_t>(16 + record.size()));
    put32(rmd, 0);
    rmd.insert(rmd.end(), record.begin(), record.end());
    std::vector<std::uint8_t> chunk;
    append_chunk(chunk, "rmd", rmd);
    std::vector<std::uint8_t> model;
    put32(model, 105);
    append(model, "default");
    model.push_back(0);
    put32(model, static_cast<std::uint32_t>(16 + chunk.size()));
    put32(model, 0);
    model.insert(model.end(), chunk.begin(), chunk.end());
    return model;
}

// A region payload with no section: the smallest payload that
// parse_region_entities accepts (docs/FORMATS/region.md).
std::vector<std::uint8_t> build_region() {
    std::vector<std::uint8_t> data;
    put32(data, 1);
    put32(data, 3);
    return data;
}

// An animatio graph with every table empty: the smallest payload that
// parse_anim_graph accepts (docs/FORMATS/animatio.md).
std::vector<std::uint8_t> build_anim_graph() {
    std::vector<std::uint8_t> data;
    put32(data, 0);  // format id
    put32(data, 0);  // version 0, a graph
    put32(data, 0);  // flags
    for (int index = 0; index < 9; ++index) {
        put32(data, 0);  // nine opaque header values
    }
    for (int index = 0; index < 18; ++index) {
        put32(data, 0);  // every table is empty
    }
    put32(data, 0);  // +0x70
    return data;
}

// An animreel with no channels: the smallest payload that parse_anim_reel
// accepts (docs/FORMATS/animreel.md).
std::vector<std::uint8_t> build_anim_reel() {
    std::vector<std::uint8_t> body;
    put32(body, 6);  // inner version
    put32(body, 0);
    put32(body, 1);
    put32(body, 7);
    put_float(body, 5.0f);
    put_float(body, 30.0f);
    put32(body, 2);  // frames
    put32(body, 0);  // channels
    put32(body, 0);  // descriptor blob size
    std::vector<std::uint8_t> out = {'a', 'n', 'i', 'm', 'r', 'e', 'e', 'l'};
    put32(out, static_cast<std::uint32_t>(16 + body.size()));
    put32(out, 4);  // chunk version
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<std::uint8_t> build_cell_group(
    bool with_instances = false, bool with_collision = false,
    const std::vector<std::uint8_t>& texture_payload = {},
    const std::vector<std::uint8_t>& cull_payload = {},
    const std::vector<std::uint8_t>& attr_payload = {}) {
    std::vector<std::uint8_t> positions;
    put32(positions, 4);
    for (const auto& point : std::vector<std::array<float, 3>>{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}) {
        put_float(positions, point[0]);
        put_float(positions, point[1]);
        put_float(positions, point[2]);
    }
    std::vector<std::uint8_t> normals;
    put32(normals, 4);
    for (int index = 0; index < 4; ++index) {
        put_float(normals, 0.0f);
        put_float(normals, 0.0f);
        put_float(normals, 1.0f);
    }
    std::vector<std::uint8_t> prims;
    put32(prims, 2);
    put32(prims, 0);
    put32(prims, 2);
    put32(prims, 6);
    for (std::uint16_t index : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{0},
                                std::uint16_t{2}, std::uint16_t{3}}) {
        put16(prims, index);
    }
    put16(prims, 0);
    for (int index = 0; index < 38; ++index) {
        prims.push_back(0);
    }
    put32(prims, 1);
    put32(prims, 1);
    put32(prims, 3);
    for (std::uint16_t index : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}}) {
        put16(prims, index);
    }
    put16(prims, 0xFFFF);
    for (int index = 0; index < 40; ++index) {
        prims.push_back(0);
    }

    std::vector<std::uint8_t> uv_indices;
    put32(uv_indices, 4);
    for (std::uint16_t index : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2},
                                std::uint16_t{3}}) {
        put16(uv_indices, index);
    }
    std::vector<std::uint8_t> uvs;
    put32(uvs, 0);
    put32(uvs, 4);
    for (const auto& uv : std::vector<std::array<float, 2>>{{0, 0}, {1, 0}, {1, 1}, {0, 1}}) {
        put_float(uvs, uv[0]);
        put_float(uvs, uv[1]);
    }
    std::vector<std::uint8_t> lightmap_uvs;
    put32(lightmap_uvs, 1);
    put32(lightmap_uvs, 4);
    for (const auto& uv : std::vector<std::array<float, 2>>{{0.25f, 0.25f}, {0.5f, 0.25f},
                                                            {0.5f, 0.5f}, {0.25f, 0.5f}}) {
        put_float(lightmap_uvs, uv[0]);
        put_float(lightmap_uvs, uv[1]);
    }
    std::vector<std::uint8_t> material_texture;
    if (!texture_payload.empty()) {
        material_texture = texture_payload;
    } else {
        put32(material_texture, 0);
        put32(material_texture, 1);
        put32(material_texture, 1);
        put16(material_texture, 0);
        put16(material_texture, 0xFFFF);
    }

    std::vector<std::uint8_t> layer_colors;
    put32(layer_colors, 0);
    put32(layer_colors, 1);
    for (float value : {0.25f, 0.5f, 0.75f, 1.0f, 0.9f, 0.1f, 0.05f, 0.5f, 0.0f, 0.0f, 0.0f,
                        1.0f}) {
        put_float(layer_colors, value);
    }
    std::vector<std::uint8_t> layer_controls;
    put32(layer_controls, 0);
    put32(layer_controls, 1);
    put32(layer_controls, 7);
    put_float(layer_controls, 0.75f);
    put16(layer_controls, 1);
    put16(layer_controls, 2);
    put32(layer_controls, 8);
    put_float(layer_controls, 1.0f);
    put16(layer_controls, 3);
    put16(layer_controls, 4);
    put32(layer_controls, 9);
    put_float(layer_controls, 0.5f);
    put16(layer_controls, 5);
    put16(layer_controls, 6);

    std::vector<std::uint8_t> record;
    append_chunk(record, "position", positions);
    append_chunk(record, "normals", normals);
    append_chunk(record, "prims", prims);
    append_chunk(record, "textures", uv_indices);
    append_chunk(record, "uvs", uvs);
    append_chunk(record, "uvs", lightmap_uvs);
    append_chunk(record, "texture", material_texture);
    if (!cull_payload.empty()) {
        append_chunk(record, "mtlctrl", cull_payload);
    }
    if (!attr_payload.empty()) {
        append_chunk(record, "mdlattr", attr_payload);
    }
    append_chunk(record, "mlyrcolr", layer_colors);
    append_chunk(record, "mlyrctrl", layer_controls);

    std::vector<std::uint8_t> rmd;
    put32(rmd, 2);
    std::string record_name = "default";
    record_name.resize(8, '\0');
    append(rmd, record_name);
    put32(rmd, static_cast<std::uint32_t>(16 + record.size()));
    put32(rmd, 0);
    rmd.insert(rmd.end(), record.begin(), record.end());

    std::vector<std::uint8_t> model_chunks;
    append_chunk(model_chunks, "rmd", rmd);
    if (with_collision) {
        append_chunk(model_chunks, "collmodc", {1, 2, 3, 4});
    }
    std::vector<std::uint8_t> model;
    put32(model, 105);
    append(model, "default");
    model.push_back(0);
    put32(model, static_cast<std::uint32_t>(16 + model_chunks.size()));
    put32(model, 0);
    model.insert(model.end(), model_chunks.begin(), model_chunks.end());

    std::vector<std::uint8_t> cell(8, 0);
    append(cell, "cellmdls");
    put32(cell, static_cast<std::uint32_t>(20 + model.size()));
    put32(cell, 0);
    put32(cell, 1);
    cell.insert(cell.end(), model.begin(), model.end());

    std::vector<std::uint8_t> attributes;
    append_table(attributes, "default", {1U, 2U, 0x00bf0103U, 4U, 0x000f0f0fU});
    append_chunk(cell, "attrs", attributes);

    std::vector<std::uint8_t> lights;
    put32(lights, 1);
    put32(lights, 3);
    put32(lights, 0x1234);
    put_float(lights, 1.0f);
    put_float(lights, 2.0f);
    put_float(lights, 3.0f);
    for (float value : {0.1f, 0.2f, 0.3f, 1.0f}) {
        put_float(lights, value);
    }
    for (float value : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f}) {
        put_float(lights, value);
    }
    lights.push_back(0);
    lights.push_back(0);
    lights.push_back(0);
    lights.push_back(0);
    put32(lights, 0);
    put32(lights, 0);
    append_chunk(cell, "cellight", lights);

    std::vector<std::uint8_t> markers;
    put32(markers, 1);
    put32(markers, 4);
    put32(markers, 1);
    put32(markers, 2);
    put32(markers, 3);
    put32(markers, 4);
    put_float(markers, 4.0f);
    put_float(markers, 5.0f);
    put_float(markers, 6.0f);
    put_float(markers, 0.0f);
    put_float(markers, 0.0f);
    put_float(markers, 1.0f);
    put32(markers, 1);
    put32(markers, 2);
    put_float(markers, 3.5f);
    append_chunk(cell, "cellmark", markers);

    // `cellanim`: a count and two clip bodies (an animreel from its inner
    // version on). The first is one constant scale Y channel over 28 frames,
    // the second one raw scale X channel over 2 frames.
    std::vector<std::uint8_t> animations;
    put32(animations, 2);
    put32(animations, 6);
    put32(animations, 0);
    put32(animations, 0);
    put_float(animations, 30.0f);
    put32(animations, 28);
    put32(animations, 1);
    put32(animations, 0);
    put32(animations, 0x01020000);  // node 0, type 2 (scale), component 1
    put32(animations, 0x3f800001);  // constant: the float with its tag bit
    put32(animations, 6);
    put32(animations, 0);
    put32(animations, 0);
    put_float(animations, 30.0f);
    put32(animations, 2);
    put32(animations, 1);
    put32(animations, 8);
    put_float(animations, 0.0f);
    put_float(animations, 1.0f);
    put32(animations, 0x00020000);  // node 0, type 2 (scale), component 0
    put32(animations, 0);           // raw floats
    append_chunk(cell, "cellanim", animations);

    std::vector<std::uint8_t> cells;
    append_cell_grid(cells);
    append_chunk(cell, "cells", cells);

    std::vector<std::uint8_t> bsp;
    append_cell_bsp(bsp);
    append_chunk(cell, "cellbsp", bsp);

    if (with_instances) {
        std::vector<std::uint8_t> instances;
        put32(instances, 2);
        put32(instances, 4);
        put16(instances, 0);
        put16(instances, 0);
        put32(instances, 0);
        put_float(instances, 10.0f);
        put_float(instances, 0.0f);
        put_float(instances, 0.0f);
        put_float(instances, 1.0f);
        put_float(instances, 1.0f);
        put_float(instances, 1.0f);
        put_float(instances, 0.0f);
        put_float(instances, 0.0f);
        put_float(instances, 0.0f);
        put32(instances, 0);
        put32(instances, 3);
        put16(instances, 0);
        put16(instances, 0);
        put32(instances, 0);
        put_float(instances, 20.0f);
        put_float(instances, 0.0f);
        put_float(instances, 0.0f);
        put_float(instances, 0.0f);
        put_float(instances, 0.0f);
        put_float(instances, 3.14159265f);
        put32(instances, 0);
        append_chunk(cell, "cellinst", instances);
    }
    return cell;
}

struct GlyphSpec {
    std::uint8_t code;
    float x;
    float y;
    float width;
    float height;
};

std::vector<std::uint8_t> make_font(bool upper_case_only, const std::vector<GlyphSpec>& glyphs) {
    std::vector<std::uint8_t> data;
    append(data, std::string("texfont\0", 8));
    put32(data, static_cast<std::uint32_t>(40 + glyphs.size() * 20));
    put32(data, 0);
    put16(data, 0);
    put16(data, 0xFFFF);
    for (float value : {20.0f, 2.0f, 5.0f, 8.0f}) {
        put_float(data, value);
    }
    data.push_back(upper_case_only ? 1 : 0);
    data.push_back(0xFF);
    put16(data, static_cast<std::uint16_t>(glyphs.size()));
    for (const GlyphSpec& glyph : glyphs) {
        data.push_back(glyph.code);
        data.push_back(0xFF);
        data.push_back(0xFF);
        data.push_back(0xFF);
        put_float(data, glyph.x);
        put_float(data, glyph.y);
        put_float(data, glyph.width);
        put_float(data, glyph.height);
    }
    return data;
}

void test_reader() {
    const std::vector<std::uint8_t> data = {0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A, 0xEF, 0xCD};
    const bo2::common::Reader reader(data);
    CHECK(reader.u16(0) == 0x1234);
    CHECK(reader.u32(0) == 0x56781234);
    CHECK(reader.u64(0) == 0xCDEF9ABC56781234ULL);
    CHECK(throws([&] { reader.u32(6); }));

    const std::vector<std::uint8_t> text = {'a', 'b', 'c', 0};
    CHECK(bo2::common::Reader(text).cstr(0, text.size()) == "abc");
}

void test_big() {
    const std::vector<std::uint8_t> data = build_big({
        {"model", "alpha", {1, 2, 3}, {3, 4}},
        {"texture", "Beta", {4, 5, 6, 7}, {}},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    CHECK(archive.entries().size() == 2);
    CHECK(archive.names() == std::vector<std::string>({"alpha", "Beta"}));
    CHECK(archive.entries()[0].type == "model");
    CHECK(archive.entries()[0].name == "alpha");
    CHECK(archive.entries()[0].symbols == std::vector<std::uint16_t>({3, 4}));
    CHECK(archive.entries()[1].type == "texture");
    CHECK(archive.entries()[1].name == "Beta");
    CHECK(archive.extract(archive.entries()[0]) == std::vector<std::uint8_t>({1, 2, 3}));
    CHECK(archive.find("TEXTURE", "beta") != nullptr);
    CHECK(archive.find("texture", "missing") == nullptr);

    std::vector<std::uint8_t> bad = data;
    bad[0] = 'x';
    CHECK(throws([&] { bo2::engine::BigArchive::parse(bad); }));

    std::vector<std::uint8_t> truncated = data;
    truncated.resize(truncated.size() - 2);
    CHECK(throws([&] { bo2::engine::BigArchive::parse(truncated); }));
}

void test_dds() {
    std::vector<std::uint8_t> red_block;
    put16(red_block, 0xF800);
    put16(red_block, 0x07E0);
    put32(red_block, 0);
    const auto red = bo2::engine::decode_dds(
        make_dds(4, 4, 0x31545844U, 4, 0, 0, 0, 0, 0, red_block));
    CHECK(red.width == 4 && red.height == 4);
    CHECK(red.rgba[0] == 255 && red.rgba[1] == 0 && red.rgba[2] == 0 && red.rgba[3] == 255);

    std::vector<std::uint8_t> green_block;
    put16(green_block, 0xF800);
    put16(green_block, 0x07E0);
    put32(green_block, 0x55555555U);
    const auto green = bo2::engine::decode_dds(
        make_dds(4, 4, 0x31545844U, 4, 0, 0, 0, 0, 0, green_block));
    CHECK(green.rgba[0] == 0 && green.rgba[1] == 255 && green.rgba[2] == 0);

    std::vector<std::uint8_t> dxt5;
    dxt5.push_back(0);
    dxt5.push_back(255);
    for (int index = 0; index < 6; ++index) {
        dxt5.push_back(0);
    }
    put16(dxt5, 0xF800);
    put16(dxt5, 0x07E0);
    put32(dxt5, 0);
    const auto alpha = bo2::engine::decode_dds(
        make_dds(4, 4, 0x35545844U, 4, 0, 0, 0, 0, 0, dxt5));
    CHECK(alpha.rgba[3] == 0);

    std::vector<std::uint8_t> pixels;
    put32(pixels, 0xFF0000FFU);
    put32(pixels, 0xFFFF0000U);
    const auto bgra = bo2::engine::decode_dds(
        make_dds(2, 1, 0, 0x41, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, pixels));
    CHECK(bgra.rgba[0] == 0 && bgra.rgba[1] == 0 && bgra.rgba[2] == 255);
    CHECK(bgra.rgba[4] == 255 && bgra.rgba[5] == 0 && bgra.rgba[6] == 0);

    auto truncated = make_dds(4, 4, 0x31545844U, 4, 0, 0, 0, 0, 0, red_block);
    truncated.pop_back();
    CHECK(throws([&] { bo2::engine::decode_dds(truncated); }));
}

void test_math() {
    const bo2::engine::Mat4 view =
        bo2::engine::Mat4::look_at({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const bo2::engine::Vec4 origin = view * bo2::engine::Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(std::fabs(origin.x) < 1.0e-5f);
    CHECK(std::fabs(origin.y) < 1.0e-5f);
    CHECK(std::fabs(origin.z + 3.0f) < 1.0e-5f);

    const bo2::engine::Mat4 projection =
        bo2::engine::Mat4::perspective(1.57079632679f, 1.0f, 1.0f, 10.0f);
    const bo2::engine::Vec4 near_point = projection * bo2::engine::Vec4{0.0f, 0.0f, -1.0f, 1.0f};
    CHECK(std::fabs(near_point.z / near_point.w + 1.0f) < 1.0e-5f);
    const bo2::engine::Vec4 far_point = projection * bo2::engine::Vec4{0.0f, 0.0f, -10.0f, 1.0f};
    CHECK(std::fabs(far_point.z / far_point.w - 1.0f) < 1.0e-5f);
}

void test_cell_animations() {
    const bo2::engine::LevelGeometry geometry = bo2::engine::parse_cell_group(build_cell_group());
    CHECK(geometry.cell_animations.size() == 2);
    if (geometry.cell_animations.size() != 2) {
        return;
    }
    const bo2::engine::AnimReel& first = geometry.cell_animations[0];
    CHECK(first.frames == 28);
    CHECK(first.channels.size() == 1);
    CHECK(first.channels[0].node == 0 && first.channels[0].type == 2 &&
          first.channels[0].component == 1);
    CHECK(first.channels[0].constant);
    const bo2::engine::AnimReel& second = geometry.cell_animations[1];
    CHECK(second.frames == 2);
    CHECK(std::fabs(second.channels[0].sample(0.5f) - 0.5f) < 1.0e-6f);
}

void test_cell_instances() {
    const std::vector<std::uint8_t> data = build_cell_group(true);
    const bo2::engine::LevelGeometry geometry = bo2::engine::parse_cell_group(data);
    CHECK(geometry.has_instances);
    CHECK(geometry.instances.size() == 2);
    CHECK(geometry.placed_instances == 2);
    CHECK(geometry.skipped_instances == 0);
    CHECK(geometry.positions.size() == 8);
    CHECK(geometry.triangles.size() == 6);
    CHECK(std::fabs(geometry.positions[0].x - 10.0f) < 1.0e-5f);
    CHECK(std::fabs(geometry.positions[4].x - 20.0f) < 1.0e-5f);
    CHECK(std::fabs(geometry.minimum.x - 10.0f) < 1.0e-5f);
    CHECK(std::fabs(geometry.maximum.x - 20.0f) < 1.0e-5f);
    // The light-map UVs follow the placed vertices: the second instance's
    // vertex 1 carries the model's vertex 1 UV.
    CHECK(geometry.lightmap_texcoords.size() == geometry.positions.size());
    CHECK(geometry.lightmap_texcoords.size() == 8 &&
          std::fabs(geometry.lightmap_texcoords[5].x - 0.5f) < 1.0e-5f &&
          std::fabs(geometry.lightmap_texcoords[5].y - 0.25f) < 1.0e-5f);

    std::vector<std::uint8_t> broken = data;
    const std::string needle = "cellinst";
    const auto found = std::search(broken.begin(), broken.end(), needle.begin(), needle.end());
    CHECK(found != broken.end());
    const std::size_t offset = static_cast<std::size_t>(found - broken.begin());
    patch32(broken, offset + 16 + 4 + 8, 99);
    const bo2::engine::LevelGeometry partial = bo2::engine::parse_cell_group(broken);
    CHECK(partial.placed_instances == 1);
    CHECK(partial.skipped_instances == 1);
    CHECK(partial.triangles.size() == 3);
}

void test_lightmaps() {
    std::vector<std::uint8_t> data;
    put32(data, 1);
    put32(data, 112);
    put32(data, 4);
    append(data, std::string("test", 4));
    for (int index = 0; index < 12; ++index) {
        data.push_back(0);
    }
    put32(data, 8);
    append(data, "test-tex");
    put16(data, 1);
    data.push_back(4);
    data.push_back(0x20);
    put16(data, 0x10);
    put16(data, 0x61);
    put16(data, 2);
    put16(data, 2);
    for (int index = 0; index < 16; ++index) {
        put32(data, static_cast<std::uint32_t>(index) << 28);
    }
    data.push_back(0x01);
    data.push_back(0x23);
    // The group's per-vertex colours: u32 count, then RGBA f32 per vertex.
    put32(data, 2);
    for (const float value : {0.25f, 0.5f, 0.75f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f}) {
        put_float(data, value);
    }
    const std::vector<bo2::engine::LevelLightMap> maps =
        bo2::engine::parse_lightmaps(data);
    CHECK(maps.size() == 1);
    CHECK(maps[0].name == "test-tex");
    CHECK(maps[0].group_vertex_count == 2);
    CHECK(maps[0].group_colors != nullptr && maps[0].group_colors->size() == 2);
    CHECK(maps[0].group_colors != nullptr && (*maps[0].group_colors)[0].y == 0.5f &&
          (*maps[0].group_colors)[1].z == 0.5f);
    CHECK(maps[0].image.width == 2 && maps[0].image.height == 2);
    CHECK(maps[0].image.rgba[0] == 0);
    CHECK(maps[0].image.rgba[4] == 0x10);
    CHECK(maps[0].image.rgba[8] == 0x20);
    CHECK(maps[0].image.rgba[12] == 0x30);
}

void test_erg() {
    const std::string fixture =
        "\"Debug Common\\Draw UI\"=1\r\n"
        "NoOutOfCellWarningLighting=1\r\n"
        "\r\n"
        "LocaleSet=\"english,spanish\"\r\n"
        "Volume=\"\"\r\n"
        "basePath=\"/gogames/bo2/\"\r\n";
    const bo2::engine::ErgConfig config = bo2::engine::ErgConfig::parse(fixture);
    CHECK(config.entries().size() == 6);
    const std::string* value = config.find("NoOutOfCellWarningLighting");
    CHECK(value != nullptr && *value == "1");
    value = config.find("LocaleSet");
    CHECK(value != nullptr && *value == "english,spanish");
    value = config.find("Volume");
    CHECK(value != nullptr && value->empty());
    CHECK(config.serialize() == fixture);
    CHECK(throws([&] { bo2::engine::ErgConfig::parse("missing equals\n"); }));
}

void test_model_lightmaps() {
    bo2::engine::LevelGeometry geometry;
    geometry.texture_names = {"Alpha", "Beta"};
    geometry.model_ranges.resize(3);
    geometry.model_ranges[0].present = true;
    geometry.model_ranges[0].vertex_count = 4;
    geometry.model_ranges[1].present = true;
    geometry.model_ranges[1].vertex_count = 4;
    geometry.model_ranges[2].present = true;
    geometry.model_ranges[2].vertex_count = 8;
    geometry.triangles = {{0, 1, 2, 0, 0, 0xFFFFFFFFU, 0, -1},
                          {0, 1, 2, 0, 1, 0xFFFFFFFFU, 0, -1},
                          {0, 1, 2, 0, 2, 0xFFFFFFFFU, 1, -1}};
    std::vector<bo2::engine::LevelLightMap> maps(3);
    maps[0].name = "roomA-alpha";
    maps[0].group = "roomA";
    maps[0].group_vertex_count = 4;
    maps[0].image.width = 1;
    maps[0].image.height = 1;
    maps[0].image.rgba = {10, 10, 10, 255};
    maps[1].name = "roomB-alpha";
    maps[1].group = "roomB";
    maps[1].group_vertex_count = 4;
    maps[1].image.width = 1;
    maps[1].image.height = 1;
    maps[1].image.rgba = {20, 20, 20, 255};
    maps[2].name = "roomC-beta";
    maps[2].group = "roomC";
    maps[2].group_vertex_count = 8;
    maps[2].image.width = 1;
    maps[2].image.height = 1;
    maps[2].image.rgba = {30, 30, 30, 255};
    bo2::engine::assign_model_lightmaps(geometry, maps);
    CHECK(geometry.model_lightmaps.size() == 3);
    CHECK(geometry.triangles[0].lightmap == 0);
    CHECK(geometry.triangles[1].lightmap == 1);
    CHECK(geometry.triangles[2].lightmap == 2);
}

void test_shared_instance_lightmaps() {
    bo2::engine::LevelGeometry geometry;
    geometry.texture_names = {"Alpha"};
    geometry.model_ranges.resize(1);
    geometry.model_ranges[0].present = true;
    geometry.model_ranges[0].vertex_count = 4;
    geometry.instances.resize(2);
    geometry.instances[0].model = 0;
    geometry.instances[1].model = 0;
    geometry.triangles = {{0, 1, 2, 0, 0, 0, 0, -1}, {0, 1, 2, 0, 0, 1, 0, -1}};
    std::vector<bo2::engine::LevelLightMap> maps(2);
    for (int index = 0; index < 2; ++index) {
        maps[static_cast<std::size_t>(index)].name =
            std::string(index == 0 ? "coffin01" : "coffin02") + "-alpha";
        maps[static_cast<std::size_t>(index)].group =
            index == 0 ? "coffin01" : "coffin02";
        maps[static_cast<std::size_t>(index)].group_vertex_count = 4;
        maps[static_cast<std::size_t>(index)].image.width = 1;
        maps[static_cast<std::size_t>(index)].image.height = 1;
        maps[static_cast<std::size_t>(index)].image.rgba = {
            static_cast<std::uint8_t>(index == 0 ? 1 : 2), 0, 0, 255};
    }
    bo2::engine::assign_model_lightmaps(geometry, maps);
    CHECK(geometry.model_lightmaps.size() == 2);
    CHECK(geometry.triangles[0].lightmap != geometry.triangles[1].lightmap);
}

void test_named_model_lightmaps() {
    bo2::engine::LevelGeometry geometry;
    geometry.texture_names = {"Alpha"};
    geometry.triangles = {{0, 1, 2, 0, 0, 0xFFFFFFFFU, 0, -1}};
    std::vector<bo2::engine::LevelLightMap> maps(2);
    maps[0].name = "sanctuary-pilara-alpha";
    maps[0].group = "sanctuary/pilara";
    maps[0].group_vertex_count = 4;
    maps[1].name = "other-alpha";
    maps[1].group = "other";
    maps[1].group_vertex_count = 4;
    bo2::engine::assign_named_model_lightmaps(geometry, maps, "pilara");
    CHECK(geometry.model_lightmaps.size() == 1);
    CHECK(geometry.triangles[0].lightmap == 0);
}

void test_instance_lightmaps_by_name() {
    // FUN_005cf670 looks a Terrain's lighting up by its lower-cased instance
    // name; a repeated name is renamed "name|N" (FUN_006827f0) and gets none.
    bo2::engine::LevelGeometry geometry;
    geometry.texture_names = {"Alpha"};
    geometry.model_ranges.resize(1);
    geometry.model_ranges[0].present = true;
    geometry.model_ranges[0].vertex_count = 4;
    geometry.instances.resize(3);
    geometry.instances[0].name = "Box02";
    geometry.instances[1].name = "box01";
    geometry.instances[2].name = "BOX01";
    geometry.triangles = {{0, 1, 2, 0, 0, 0, 0, -1}, {0, 1, 2, 0, 0, 1, 0, -1},
                          {0, 1, 2, 0, 0, 2, 0, -1}};
    // Same vertex count as the model: a vertex-count match would light all three.
    std::vector<bo2::engine::LevelLightMap> maps(2);
    for (int index = 0; index < 2; ++index) {
        bo2::engine::LevelLightMap& map = maps[static_cast<std::size_t>(index)];
        map.group = index == 0 ? "box01" : "box02";
        map.name = map.group + "-alpha";
        map.group_vertex_count = 4;
        map.image.width = 1;
        map.image.height = 1;
        map.image.rgba = {static_cast<std::uint8_t>(index + 1), 0, 0, 255};
    }
    bo2::engine::assign_model_lightmaps(geometry, maps);
    CHECK(geometry.model_lightmaps.size() == 2);
    const auto red = [&](std::size_t triangle) {
        const std::int32_t index = geometry.triangles[triangle].lightmap;
        return index < 0 ? -1 : geometry.model_lightmaps[static_cast<std::size_t>(index)].rgba[0];
    };
    CHECK(red(0) == 2);   // Box02 -> box02
    CHECK(red(1) == 1);   // box01 -> box01
    CHECK(red(2) == -1);  // repeated name: unlit

    // The group's vertex colours land on the instance's placed vertices.
    geometry.positions.assign(12, bo2::engine::Vec3{});
    geometry.instance_vertex_base = {0, 4, 8};
    geometry.model_lightmaps.clear();
    const auto box01 = std::make_shared<std::vector<bo2::engine::Vec3>>(
        4, bo2::engine::Vec3{0.5f, 0.25f, 0.125f});
    maps[0].group_colors = box01;
    bo2::engine::assign_model_lightmaps(geometry, maps);
    CHECK(geometry.vertex_colors.size() == 12);
    CHECK(geometry.vertex_colors.size() == 12 && geometry.vertex_colors[4].x == 0.5f &&
          geometry.vertex_colors[7].z == 0.125f);
    // box02 has no colours and the repeated name is unlit: both stay white.
    CHECK(geometry.vertex_colors.size() == 12 && geometry.vertex_colors[0].x == 1.0f &&
          geometry.vertex_colors[8].y == 1.0f);
}

void test_collision_chunk() {
    const std::vector<std::uint8_t> data = build_cell_group(false, true);
    const bo2::engine::LevelGeometry geometry = bo2::engine::parse_cell_group(data);
    CHECK(geometry.collisions.size() == 1);
    CHECK(geometry.collisions[0].model == 0);
    CHECK(geometry.collisions[0].data.size() == 4);
    CHECK(geometry.collisions[0].data[0] == 1);
}

void test_cell_group() {
    const std::vector<std::uint8_t> data = build_cell_group();
    const bo2::engine::LevelGeometry geometry = bo2::engine::parse_cell_group(data);
    CHECK(geometry.model_count == 1);
    CHECK(geometry.skipped_models == 0);
    CHECK(geometry.positions.size() == 4);
    CHECK(geometry.normals.size() == 4);
    CHECK(geometry.triangles.size() == 3);
    CHECK(geometry.triangles[0].material == 0);
    CHECK(geometry.triangles[2].material == 1);
    CHECK(geometry.triangles[2].texture == -1);
    CHECK(geometry.materials.size() == 2);
    CHECK(geometry.materials[0].has_colors);
    CHECK(std::fabs(geometry.materials[0].colors[1].x - 0.9f) < 1.0e-5f);
    CHECK(std::fabs(geometry.materials[0].alphas[1] - 0.5f) < 1.0e-5f);
    CHECK(geometry.materials[0].has_controls);
    CHECK(geometry.materials[0].controls[1].value == 8);
    CHECK(std::fabs(geometry.materials[0].controls[1].weight - 1.0f) < 1.0e-5f);
    CHECK(geometry.materials[0].controls[1].arg0 == 3);
    CHECK(geometry.materials[0].controls[1].arg1 == 4);
    CHECK(!geometry.materials[1].has_colors);
    const bo2::engine::Vec3 base_color =
        bo2::engine::material_color(geometry, geometry.triangles[0]);
    CHECK(std::fabs(base_color.x - 0.9f) < 1.0e-5f);
    CHECK(std::fabs(base_color.y - 0.1f) < 1.0e-5f);
    CHECK(std::fabs(base_color.z - 0.05f) < 1.0e-5f);
    const bo2::engine::Vec3 fallback_color =
        bo2::engine::material_color(geometry, geometry.triangles[2]);
    CHECK(std::fabs(fallback_color.x - 0.50f) < 1.0e-5f);
    CHECK(std::fabs(fallback_color.y - 0.52f) < 1.0e-5f);
    CHECK(std::fabs(fallback_color.z - 0.56f) < 1.0e-5f);
    const bo2::engine::Vec3 off_tint = bo2::engine::material_tint(
        geometry, geometry.triangles[0], bo2::engine::MaterialMode::Off);
    CHECK(std::fabs(off_tint.x - 1.0f) < 1.0e-5f);
    const bo2::engine::Vec3 diffuse_tint = bo2::engine::material_tint(
        geometry, geometry.triangles[0], bo2::engine::MaterialMode::Diffuse);
    CHECK(std::fabs(diffuse_tint.x - 0.9f) < 1.0e-5f);
    CHECK(std::fabs(diffuse_tint.y - 0.1f) < 1.0e-5f);
    CHECK(std::fabs(diffuse_tint.z - 0.05f) < 1.0e-5f);
    const bo2::engine::Vec3 layers_tint = bo2::engine::material_tint(
        geometry, geometry.triangles[0], bo2::engine::MaterialMode::Layers);
    CHECK(std::fabs(layers_tint.x - 1.0f) < 1.0e-5f);
    CHECK(std::fabs(layers_tint.y - 0.6f) < 1.0e-5f);
    CHECK(std::fabs(layers_tint.z - 0.8f) < 1.0e-5f);
    const bo2::engine::Vec3 layers2x_tint = bo2::engine::material_tint(
        geometry, geometry.triangles[0], bo2::engine::MaterialMode::Layers2x);
    CHECK(std::fabs(layers2x_tint.x - 2.0f) < 1.0e-5f);
    CHECK(std::fabs(layers2x_tint.y - 1.2f) < 1.0e-5f);
    CHECK(std::fabs(layers2x_tint.z - 1.6f) < 1.0e-5f);
    CHECK(std::fabs(geometry.minimum.x) < 1.0e-5f);
    CHECK(std::fabs(geometry.maximum.x - 1.0f) < 1.0e-5f);
    CHECK(geometry.has_cell_grid);
    CHECK(geometry.cell_grid.cells.size() == 1);
    const bo2::engine::LevelCell& cell = geometry.cell_grid.cells[0];
    CHECK(cell.version == 2);
    CHECK(cell.symbol == 7);
    CHECK(cell.flags == 0x1234);
    CHECK(std::fabs(cell.minimum.z - 3.0f) < 1.0e-5f);
    CHECK(std::fabs(cell.maximum.x - 4.0f) < 1.0e-5f);
    CHECK(cell.attributes.size() == 1);
    CHECK(cell.attributes[0].name == "default");
    CHECK(cell.attributes[0].size == 24);
    CHECK(cell.attributes[0].values.size() == 3);
    CHECK(cell.attributes[0].values[0] == 0x00b00103U);
    CHECK(cell.terrains.size() == 2);
    CHECK(cell.terrains[1] == 20);
    CHECK(cell.portals.size() == 1);
    const bo2::engine::CellPortal& portal = cell.portals[0];
    CHECK(portal.version == 2);
    CHECK(portal.cell_a == 2 && portal.cell_b == 3);
    CHECK(portal.vertices.size() == 3);
    CHECK(std::fabs(portal.plane.z - 1.0f) < 1.0e-5f);
    CHECK(portal.index == 5);
    CHECK(cell.dbgmodel_size == 3);
    CHECK(geometry.has_cell_bsp);
    CHECK(geometry.cell_bsp.leaf_count == 2);
    CHECK(geometry.cell_bsp.depth == 2);
    CHECK(geometry.cell_bsp.nodes.size() == 3);
    CHECK(!geometry.cell_bsp.nodes[0].leaf);
    CHECK(std::fabs(geometry.cell_bsp.nodes[0].plane.y - 1.0f) < 1.0e-5f);
    CHECK(std::fabs(geometry.cell_bsp.nodes[0].distance + 5.0f) < 1.0e-5f);
    CHECK(geometry.cell_bsp.nodes[0].left == 1);
    CHECK(geometry.cell_bsp.nodes[0].right == 2);
    CHECK(geometry.cell_bsp.nodes[1].leaf && geometry.cell_bsp.nodes[1].value == 0xFFFF);
    CHECK(geometry.cell_bsp.nodes[2].leaf && geometry.cell_bsp.nodes[2].value == 3);
    CHECK(geometry.has_level_attributes);
    CHECK(geometry.level_attributes.size() == 1);
    CHECK(geometry.level_attributes[0].name == "default");
    CHECK(geometry.level_attributes[0].values.size() == 5);
    CHECK(geometry.level_attributes[0].values[2] == 0x00bf0103U);
    CHECK(geometry.has_lights);
    CHECK(geometry.lights.size() == 1);
    CHECK(geometry.lights[0].version == 3);
    CHECK(geometry.lights[0].handle == 0x1234);
    CHECK(std::fabs(geometry.lights[0].position.y - 2.0f) < 1.0e-5f);
    CHECK(std::fabs(geometry.lights[0].color[2] - 0.3f) < 1.0e-5f);
    CHECK(std::fabs(geometry.lights[0].params[4] - 5.0f) < 1.0e-5f);
    CHECK(geometry.lights[0].type == 0);
    CHECK(geometry.lights[0].indices.empty());
    CHECK(geometry.has_markers);
    CHECK(geometry.markers.size() == 1);
    CHECK(geometry.markers[0].version == 4);
    CHECK(geometry.markers[0].model == 1 && geometry.markers[0].clip == 2);
    CHECK(geometry.markers[0].name_symbol == 3 && geometry.markers[0].class_symbol == 4);
    CHECK(std::fabs(geometry.markers[0].position.z - 6.0f) < 1.0e-5f);
    CHECK(std::fabs(geometry.markers[0].normal.z - 1.0f) < 1.0e-5f);
    CHECK(std::fabs(geometry.markers[0].number - 3.5f) < 1.0e-5f);
    CHECK(throws([&] {
        std::vector<std::uint8_t> broken = data;
        broken.resize(12);
        bo2::engine::parse_cell_group(broken);
    }));

    const auto find = [](const std::vector<std::uint8_t>& bytes, const std::string& name) {
        const auto found = std::search(bytes.begin(), bytes.end(), name.begin(), name.end());
        return static_cast<std::size_t>(found - bytes.begin());
    };
    const std::size_t cells_at = find(data, "cells");
    const std::size_t bsp_at = find(data, "cellbsp");
    const std::size_t attrs_at = find(data, "attrs");
    CHECK(cells_at + 20 < data.size());
    CHECK(bsp_at + 28 < data.size());
    CHECK(attrs_at + 24 < data.size());

    std::vector<std::uint8_t> bad_cell_version = data;
    patch32(bad_cell_version, cells_at + 20, 3);
    CHECK(throws([&] { bo2::engine::parse_cell_group(bad_cell_version); }));

    std::vector<std::uint8_t> bad_leaf_count = data;
    patch32(bad_leaf_count, bsp_at + 20, 3);
    CHECK(throws([&] { bo2::engine::parse_cell_group(bad_leaf_count); }));

    std::vector<std::uint8_t> bad_node_type = data;
    bad_node_type[bsp_at + 28] = 2;
    CHECK(throws([&] { bo2::engine::parse_cell_group(bad_node_type); }));

    std::vector<std::uint8_t> bad_attrs_size = data;
    patch32(bad_attrs_size, attrs_at + 24, 999);
    CHECK(throws([&] { bo2::engine::parse_cell_group(bad_attrs_size); }));

    std::vector<std::uint16_t> symbols = {100};
    std::vector<std::string> names(101);
    names[100] = "synthetic/tex";
    const bo2::engine::LevelGeometry textured =
        bo2::engine::parse_cell_group(data, symbols, names);
    CHECK(textured.texture_names.size() == 1);
    CHECK(textured.texture_names[0] == "synthetic/tex");
    CHECK(textured.texcoords.size() == 4);
    CHECK(std::fabs(textured.texcoords[1].x - 1.0f) < 1.0e-5f);
    CHECK(textured.lightmap_texcoords.size() == 4);
    CHECK(std::fabs(textured.lightmap_texcoords[1].x - 0.5f) < 1.0e-5f);
    CHECK(std::fabs(textured.lightmap_texcoords[1].y - 0.25f) < 1.0e-5f);
    CHECK(textured.triangles[0].texture == 0);
}

// The `texture` chunk holds one variable-length record per material: the
// count, that many symbols and a 0xffff pad when the count is odd.
void test_material_texture_lists() {
    std::vector<std::uint8_t> textures;
    put32(textures, 0);
    put32(textures, 2);
    put32(textures, 3);
    put16(textures, 0);
    put16(textures, 1);
    put16(textures, 2);
    put16(textures, 0xFFFF);
    put32(textures, 1);
    put16(textures, 3);
    put16(textures, 0xFFFF);

    const std::vector<std::uint16_t> symbols = {100, 101, 102, 103};
    std::vector<std::string> names(104);
    names[100] = "synthetic/a";
    names[101] = "synthetic/b";
    names[102] = "synthetic/c";
    names[103] = "synthetic/d";
    const std::vector<std::uint8_t> data = build_cell_group(false, false, textures);
    const bo2::engine::LevelGeometry geometry =
        bo2::engine::parse_cell_group(data, symbols, names);
    CHECK(geometry.texture_names.size() == 4);
    CHECK(geometry.texture_names[0] == "synthetic/a");
    CHECK(geometry.texture_names[1] == "synthetic/b");
    CHECK(geometry.texture_names[2] == "synthetic/c");
    CHECK(geometry.texture_names[3] == "synthetic/d");
    CHECK(geometry.material_textures.size() == 2);
    CHECK(geometry.material_textures[0].size() == 3);
    CHECK(geometry.material_textures[0][0] == 0);
    CHECK(geometry.material_textures[0][1] == 1);
    CHECK(geometry.material_textures[0][2] == 2);
    CHECK(geometry.material_textures[1].size() == 1);
    CHECK(geometry.material_textures[1][0] == 3);
    CHECK(geometry.triangles.size() == 3);
    CHECK(geometry.triangles[0].texture == 0);
    CHECK(geometry.triangles[1].texture == 0);
    CHECK(geometry.triangles[2].texture == 3);

    std::vector<std::uint8_t> trailing = textures;
    trailing.push_back(0);
    CHECK(throws([&] {
        bo2::engine::parse_cell_group(build_cell_group(false, false, trailing), symbols, names);
    }));
}

// Word 1 of a `mtlctrl` record with 0x4000 marks a material that discards
// back faces (1); a record without the bit keeps both (0).
void test_material_cull() {
    std::vector<std::uint8_t> cull;
    put32(cull, 2);
    put32(cull, 0);
    put32(cull, 0x4c00);
    put32(cull, 1);
    put32(cull, 0);
    put32(cull, 0x8c00);
    put32(cull, 1);
    const std::vector<std::uint8_t> data = build_cell_group(false, false, {}, cull);
    const bo2::engine::LevelGeometry geometry = bo2::engine::parse_cell_group(data);
    CHECK(geometry.material_cull.size() == 2);
    CHECK(geometry.material_cull[0] == 1);
    CHECK(geometry.material_cull[1] == 0);

    std::vector<std::uint8_t> short_chunk;
    put32(short_chunk, 2);
    put32(short_chunk, 0);
    put32(short_chunk, 0x4c00);
    put32(short_chunk, 1);
    CHECK(throws([&] {
        bo2::engine::parse_cell_group(build_cell_group(false, false, {}, short_chunk));
    }));
}

// FUN_0056d130: RenderPriority is the type 0 record of the mdlattr table
// whose key resolves to RenderPriority; a model without it keeps 0.
void test_model_render_priority() {
    std::vector<std::uint8_t> attributes;
    append(attributes, std::string("default\0\0\0\0\0", 8));
    put32(attributes, 20 + 3 * 8);
    put32(attributes, 1);
    put32(attributes, 2);
    attributes.push_back(3);  // bool
    attributes.push_back(0);
    put16(attributes, 0);
    attributes.push_back(1);
    attributes.push_back(0xFF);
    attributes.push_back(0xFF);
    attributes.push_back(0xFF);
    attributes.push_back(7);  // symbol
    attributes.push_back(0);
    put16(attributes, 1);
    put16(attributes, 0);
    put16(attributes, 0xFFFF);
    attributes.push_back(0);  // float
    attributes.push_back(0);
    put16(attributes, 2);
    put_float(attributes, 30.0f);

    const std::vector<std::uint16_t> symbols = {100, 101, 102};
    std::vector<std::string> names(103);
    names[100] = "ObjectType";
    names[101] = "EntityClass";
    names[102] = "RenderPriority";
    const std::vector<std::uint8_t> data = build_cell_group(false, false, {}, {}, attributes);
    const bo2::engine::LevelGeometry geometry =
        bo2::engine::parse_cell_group(data, symbols, names);
    CHECK(geometry.model_render_priority.size() == 1);
    CHECK(std::fabs(geometry.model_render_priority[0] - 30.0f) < 1.0e-5f);

    const bo2::engine::LevelGeometry plain = bo2::engine::parse_cell_group(build_cell_group());
    CHECK(plain.model_render_priority.size() == 1);
    CHECK(plain.model_render_priority[0] == 0.0f);

    std::vector<std::uint8_t> broken = attributes;
    broken.push_back(0);  // 20 + 8 n + 1
    CHECK(throws([&] {
        bo2::engine::parse_cell_group(build_cell_group(false, false, {}, {}, broken), symbols,
                                      names);
    }));
}

// A record of the `mdlattr` table with the array flag set is a count and that
// many values padded to four bytes; the walk accepts it and still reaches the
// records after it (docs/FORMATS/models.md, "mdlattr").
void test_model_attribute_arrays() {
    std::vector<std::uint8_t> attributes;
    append(attributes, std::string("default\0\0\0\0\0", 8));
    put32(attributes, 20 + 8 + 12 + 8);
    put32(attributes, 1);
    put32(attributes, 2);
    attributes.push_back(7);  // symbol
    attributes.push_back(0);
    put16(attributes, 0);
    put16(attributes, 0);
    put16(attributes, 0xFFFF);
    attributes.push_back(3);  // byte array of four values
    attributes.push_back(1);
    put16(attributes, 1);
    put32(attributes, 4);
    for (const std::uint8_t value : {std::uint8_t{0x10}, std::uint8_t{0x20}, std::uint8_t{0x30},
                                     std::uint8_t{0x40}}) {
        attributes.push_back(value);
    }
    attributes.push_back(0);  // float
    attributes.push_back(0);
    put16(attributes, 2);
    put_float(attributes, 30.0f);

    const std::vector<std::uint16_t> symbols = {100, 101, 102};
    std::vector<std::string> names(103);
    names[100] = "EntityClass";
    names[101] = "FogColor";
    names[102] = "RenderPriority";
    const bo2::engine::LevelGeometry geometry = bo2::engine::parse_cell_group(
        build_cell_group(false, false, {}, {}, attributes), symbols, names);
    CHECK(geometry.model_render_priority.size() == 1);
    CHECK(std::fabs(geometry.model_render_priority[0] - 30.0f) < 1.0e-5f);

    std::vector<std::uint8_t> cut_array;
    append(cut_array, std::string("default\0\0\0\0\0", 8));
    put32(cut_array, 30);
    put32(cut_array, 1);
    put32(cut_array, 2);
    cut_array.push_back(3);
    cut_array.push_back(1);
    put16(cut_array, 0);
    put32(cut_array, 4);
    cut_array.push_back(0x10);
    cut_array.push_back(0x20);
    CHECK(throws([&] {
        bo2::engine::parse_cell_group(build_cell_group(false, false, {}, {}, cut_array));
    }));

    std::vector<std::uint8_t> bad_type = attributes;
    bad_type[20] = 5;
    CHECK(throws([&] {
        bo2::engine::parse_cell_group(build_cell_group(false, false, {}, {}, bad_type));
    }));

    std::vector<std::uint8_t> bad_flag = attributes;
    bad_flag[21] = 2;
    CHECK(throws([&] {
        bo2::engine::parse_cell_group(build_cell_group(false, false, {}, {}, bad_flag));
    }));
}

// FUN_005df900: the texfont entry and its upper-case-only rule.
void test_pixmap_font() {
    const std::vector<std::uint16_t> symbols = {100};
    std::vector<std::string> names(101);
    names[100] = "fonts/revised";
    const std::vector<std::uint8_t> data = make_font(
        false, {{'a', 0, 0, 12, 20}, {'A', 105, 105, 12, 20}, {'S', 63, 63, 10, 20}});
    const bo2::engine::PixmapFont font = bo2::engine::parse_pixmap_font(data, symbols, names);
    CHECK(font.texture == "fonts/revised");
    CHECK(std::fabs(font.header[0] - 20.0f) < 1.0e-5f);
    CHECK(std::fabs(font.header[1] - 2.0f) < 1.0e-5f);
    CHECK(std::fabs(font.header[2] - 5.0f) < 1.0e-5f);
    CHECK(std::fabs(font.header[3] - 8.0f) < 1.0e-5f);
    CHECK(!font.upper_case_only);
    CHECK(font.glyphs.size() == 3);
    const bo2::engine::FontGlyph* a = font.find('a');
    CHECK(a != nullptr && a->x == 0.0f && a->width == 12.0f);
    const bo2::engine::FontGlyph* capital = font.find('A');
    CHECK(capital != nullptr && capital->x == 105.0f && capital->y == 105.0f);
    const bo2::engine::FontGlyph* s = font.find('S');
    CHECK(s != nullptr && s->width == 10.0f);
    CHECK(font.find('z') == nullptr);

    const std::vector<std::uint8_t> upper = make_font(true, {{'A', 105, 105, 12, 20}});
    const bo2::engine::PixmapFont upper_font =
        bo2::engine::parse_pixmap_font(upper, symbols, names);
    CHECK(upper_font.upper_case_only);
    CHECK(upper_font.glyphs.size() == 2);
    const bo2::engine::FontGlyph* lower = upper_font.find('a');
    CHECK(lower != nullptr && lower->x == 105.0f && lower->y == 105.0f);

    const std::vector<std::uint8_t> both =
        make_font(true, {{'A', 105, 105, 12, 20}, {'a', 1, 2, 3, 4}});
    const bo2::engine::PixmapFont both_font =
        bo2::engine::parse_pixmap_font(both, symbols, names);
    CHECK(both_font.glyphs.size() == 2);
    const bo2::engine::FontGlyph* own = both_font.find('a');
    CHECK(own != nullptr && own->x == 1.0f);

    std::vector<std::uint8_t> truncated = data;
    truncated.pop_back();
    CHECK(throws([&] { bo2::engine::parse_pixmap_font(truncated, symbols, names); }));
    std::vector<std::uint8_t> bad_size = data;
    patch32(bad_size, 8, 12);
    CHECK(throws([&] { bo2::engine::parse_pixmap_font(bad_size, symbols, names); }));
    const std::vector<std::uint16_t> no_symbols;
    CHECK(throws([&] { bo2::engine::parse_pixmap_font(data, no_symbols, names); }));
}

// The data checker counts every entry of a container by type and reports the
// ones whose reader throws, without stopping the walk (docs/DATA_COVERAGE.md).
void test_data_check() {
    const std::vector<std::uint16_t> symbols = {0};
    const std::vector<std::uint8_t> font = make_font(false, {{'a', 0, 0, 12, 20}});
    std::vector<std::uint8_t> truncated = font;
    truncated.resize(truncated.size() / 2);
    const std::vector<std::uint8_t> data = build_big({
        {"texfont", "fonts/one", font, symbols},
        {"texfont", "fonts/two", truncated, symbols},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    bo2::engine::DataReport report;
    bo2::engine::check_archive(archive, "test.big", report);
    CHECK(report.containers == 1);
    CHECK(report.types.size() == 1);
    const bo2::engine::TypeCount& count = report.types.at("texfont");
    CHECK(count.entries == 2);
    CHECK(count.checked == 2);
    CHECK(count.failed == 1);
    CHECK(report.failures.size() == 1);
    CHECK(report.failures[0].container == "test.big");
    CHECK(report.failures[0].type == "texfont");
    CHECK(report.failures[0].entry == "fonts/two");
    CHECK(!report.failures[0].message.empty());
}

// A cell group that the model reader rejects is one failure; the entries
// after it are still checked (docs/DATA_COVERAGE.md, "cellgrup").
void test_data_check_cellgrup() {
    const std::vector<std::uint8_t> group = build_cell_group();
    std::vector<std::uint8_t> cut = group;
    cut.resize(cut.size() / 2);
    const std::vector<std::uint8_t> data = build_big({
        {"cellgrup", "level/cut", cut, {}},
        {"cellgrup", "level/ok", group, {}},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    bo2::engine::DataReport report;
    bo2::engine::check_archive(archive, "test.big", report);
    CHECK(report.containers == 1);
    const bo2::engine::TypeCount& count = report.types.at("cellgrup");
    CHECK(count.entries == 2);
    CHECK(count.checked == 2);
    CHECK(count.failed == 1);
    CHECK(report.failures.size() == 1);
    CHECK(report.failures[0].type == "cellgrup");
    CHECK(report.failures[0].entry == "level/cut");
}

// A model asset that the reader rejects is one failure; the entries after it
// are still checked (docs/DATA_COVERAGE.md, "model").
void test_data_check_model() {
    const std::vector<std::uint8_t> model = build_model_asset();
    std::vector<std::uint8_t> cut = model;
    cut.resize(cut.size() / 2);
    const std::vector<std::uint8_t> data = build_big({
        {"model", "model/cut", cut, {}},
        {"model", "model/ok", model, {}},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    bo2::engine::DataReport report;
    bo2::engine::check_archive(archive, "test.big", report);
    CHECK(report.containers == 1);
    const bo2::engine::TypeCount& count = report.types.at("model");
    CHECK(count.entries == 2);
    CHECK(count.checked == 2);
    CHECK(count.failed == 1);
    CHECK(report.failures.size() == 1);
    CHECK(report.failures[0].type == "model");
    CHECK(report.failures[0].entry == "model/cut");
}

// An animreel that the reader rejects is one failure; the entries after it
// are still checked (docs/DATA_COVERAGE.md, "animreel").
void test_data_check_animreel() {
    const std::vector<std::uint8_t> reel = build_anim_reel();
    std::vector<std::uint8_t> cut = reel;
    cut.resize(cut.size() / 2);
    const std::vector<std::uint8_t> data = build_big({
        {"animreel", "reel/cut", cut, {}},
        {"animreel", "reel/ok", reel, {}},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    bo2::engine::DataReport report;
    bo2::engine::check_archive(archive, "test.big", report);
    CHECK(report.containers == 1);
    const bo2::engine::TypeCount& count = report.types.at("animreel");
    CHECK(count.entries == 2);
    CHECK(count.checked == 2);
    CHECK(count.failed == 1);
    CHECK(report.failures.size() == 1);
    CHECK(report.failures[0].type == "animreel");
    CHECK(report.failures[0].entry == "reel/cut");
}

// An animatio that the reader rejects is one failure; the entries after it
// are still checked (docs/DATA_COVERAGE.md, "animatio").
void test_data_check_animatio() {
    const std::vector<std::uint8_t> graph = build_anim_graph();
    std::vector<std::uint8_t> cut = graph;
    cut.resize(cut.size() / 2);
    const std::vector<std::uint8_t> data = build_big({
        {"animatio", "graph/cut", cut, {}},
        {"animatio", "graph/ok", graph, {}},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    bo2::engine::DataReport report;
    bo2::engine::check_archive(archive, "test.big", report);
    CHECK(report.containers == 1);
    const bo2::engine::TypeCount& count = report.types.at("animatio");
    CHECK(count.entries == 2);
    CHECK(count.checked == 2);
    CHECK(count.failed == 1);
    CHECK(report.failures.size() == 1);
    CHECK(report.failures[0].type == "animatio");
    CHECK(report.failures[0].entry == "graph/cut");
}

// A region that the reader rejects is one failure; the entries after it are
// still checked (docs/DATA_COVERAGE.md, "region").
void test_data_check_region() {
    const std::vector<std::uint8_t> region = build_region();
    std::vector<std::uint8_t> cut = region;
    cut.resize(cut.size() / 2);
    const std::vector<std::uint8_t> data = build_big({
        {"region", "region/cut", cut, {}},
        {"region", "region/ok", region, {}},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    bo2::engine::DataReport report;
    bo2::engine::check_archive(archive, "test.big", report);
    CHECK(report.containers == 1);
    const bo2::engine::TypeCount& count = report.types.at("region");
    CHECK(count.entries == 2);
    CHECK(count.checked == 2);
    CHECK(count.failed == 1);
    CHECK(report.failures.size() == 1);
    CHECK(report.failures[0].type == "region");
    CHECK(report.failures[0].entry == "region/cut");
}

// A texture entry that the DDS reader rejects is one failure; the entries
// after it are still checked (docs/DATA_COVERAGE.md, "texture").
void test_data_check_texture() {
    std::vector<std::uint8_t> block;
    put16(block, 0xF800);
    put16(block, 0x07E0);
    put32(block, 0);
    const std::vector<std::uint8_t> texture = make_dds(4, 4, 0x31545844U, 4, 0, 0, 0, 0, 0, block);
    std::vector<std::uint8_t> cut = texture;
    cut.resize(cut.size() / 2);
    const std::vector<std::uint8_t> data = build_big({
        {"texture", "tex/two", cut, {}},
        {"texture", "tex/one", texture, {}},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    bo2::engine::DataReport report;
    bo2::engine::check_archive(archive, "test.big", report);
    CHECK(report.containers == 1);
    const bo2::engine::TypeCount& count = report.types.at("texture");
    CHECK(count.entries == 2);
    CHECK(count.checked == 2);
    CHECK(count.failed == 1);
    CHECK(report.failures.size() == 1);
    CHECK(report.failures[0].type == "texture");
    CHECK(report.failures[0].entry == "tex/two");
}

// The modlskel record is 104 bytes (FUN_0067bdd0), not the old guessed
// count + pairs + 96 bytes layout.
void test_skeleton_bones() {
    std::vector<std::uint8_t> data;
    put32(data, 2);
    for (int index = 0; index < 2; ++index) {
        put32(data, 0x100U + static_cast<unsigned>(index));
        put32(data, 0x10U + static_cast<unsigned>(index));
        put32(data, 0x20U + static_cast<unsigned>(index));
        put32(data, static_cast<unsigned>(index));
        put32(data, static_cast<unsigned>(index + 1));
        put32(data, static_cast<unsigned>(index + 2));
        put32(data, static_cast<unsigned>(index + 3));
        put32(data, static_cast<unsigned>(index + 4));
        put32(data, static_cast<unsigned>(index + 5));
        put32(data, static_cast<unsigned>(index + 6));
        put16(data, static_cast<std::uint16_t>(0x40 + index));
        put16(data, static_cast<std::uint16_t>(index + 1));
        for (int value = 0; value < 15; ++value) {
            put_float(data, static_cast<float>(index * 100 + value));
        }
    }
    const std::vector<bo2::engine::SkeletonBone> bones = bo2::engine::parse_skeleton(data);
    CHECK(bones.size() == 2);
    CHECK(bones[0].name_symbol == 0x100);
    CHECK(bones[0].state_mask == 0x10);
    CHECK(bones[0].state_value == 0x20);
    CHECK(bones[0].bone_index == 0);
    CHECK(bones[0].current_matrix == 1);
    CHECK(bones[0].parent_matrix == 2);
    CHECK(bones[0].first_collision_prim == 3);
    CHECK(bones[0].collision_prim_count == 4);
    CHECK(bones[0].first_vertex == 5);
    CHECK(bones[0].vertex_count == 6);
    CHECK(bones[0].channel_mask == 0x40);
    CHECK(bones[0].rotation_order == 1);
    CHECK(bones[0].pivot.z == 2.0f);
    CHECK(bones[0].rest_orientation.x == 3.0f);
    CHECK(bones[0].joint_orientation.x == 6.0f);
    CHECK(bones[0].rotation.x == 9.0f);
    CHECK(bones[0].scale.x == 12.0f);
    CHECK(bones[0].scale.z == 14.0f);
    CHECK(bones[1].scale.x == 112.0f);
    CHECK(throws([&] { bo2::engine::parse_skeleton({1, 2, 3}); }));
    std::vector<std::uint8_t> truncated = data;
    truncated.pop_back();
    CHECK(throws([&] { bo2::engine::parse_skeleton(truncated); }));
}

}  // namespace

int main() {
    test_reader();
    test_big();
    test_dds();
    test_math();
    test_cell_group();
    test_material_texture_lists();
    test_material_cull();
    test_model_render_priority();
    test_model_attribute_arrays();
    test_pixmap_font();
    test_data_check();
    test_data_check_cellgrup();
    test_data_check_model();
    test_data_check_animreel();
    test_data_check_animatio();
    test_data_check_region();
    test_data_check_texture();
    test_skeleton_bones();
    test_collision_chunk();
    test_cell_animations();
    test_cell_instances();
    test_lightmaps();
    test_model_lightmaps();
    test_shared_instance_lightmaps();
    test_named_model_lightmaps();
    test_instance_lightmaps_by_name();
    test_erg();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "engine tests: ok\n";
    return 0;
}
