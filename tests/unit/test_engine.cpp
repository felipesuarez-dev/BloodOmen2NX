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
#include "bo2/engine/dds.hpp"
#include "bo2/engine/math.hpp"

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

std::vector<std::uint8_t> build_cell_group() {
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
    std::vector<std::uint8_t> material_texture;
    put32(material_texture, 0);
    put32(material_texture, 1);
    put32(material_texture, 1);
    put16(material_texture, 0);
    put16(material_texture, 0xFFFF);

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
    append_chunk(record, "texture", material_texture);
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

    std::vector<std::uint8_t> cells;
    append_cell_grid(cells);
    append_chunk(cell, "cells", cells);

    std::vector<std::uint8_t> bsp;
    append_cell_bsp(bsp);
    append_chunk(cell, "cellbsp", bsp);
    return cell;
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
    CHECK(textured.triangles[0].texture == 0);
}

}  // namespace

int main() {
    test_reader();
    test_big();
    test_dds();
    test_math();
    test_cell_group();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "engine tests: ok\n";
    return 0;
}
