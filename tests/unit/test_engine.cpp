#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/engine/big_archive.hpp"
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
        append(body, "symlist");
        body.push_back(0);
        put32(body, 20);
        put32(body, 0);
        put32(body, 0);
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
        {"model", "alpha", {1, 2, 3}},
        {"texture", "Beta", {4, 5, 6, 7}},
    });
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(data);
    CHECK(archive.entries().size() == 2);
    CHECK(archive.entries()[0].type == "model");
    CHECK(archive.entries()[0].name == "alpha");
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

}  // namespace

int main() {
    test_reader();
    test_big();
    test_dds();
    test_math();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "engine tests: ok\n";
    return 0;
}
