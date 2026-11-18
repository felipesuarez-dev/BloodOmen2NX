#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/pixmap_font.hpp"
#include "bo2/engine/string_table.hpp"
#include "bo2/game/menu_layout.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

const bo2::game::MenuBox kParent{-1.0f, -1.0f, 2.0f, 2.0f};

std::vector<bo2::game::MenuBox> synthetic_children() {
    return {{0.0f, 0.0f, 0.5f, 0.2f}, {0.0f, 0.0f, 0.3f, 0.1f}, {0.0f, 0.0f, 0.4f, 0.3f}};
}

void check_layout(const std::vector<bo2::game::MenuBox>& children, int mode,
                  const std::vector<std::array<float, 2>>& expected) {
    const std::vector<bo2::game::MenuBox> placed =
        bo2::game::layout_children(kParent, mode, children);
    CHECK(placed.size() == expected.size());
    if (placed.size() != expected.size()) {
        return;
    }
    for (std::size_t index = 0; index < placed.size(); ++index) {
        CHECK(std::fabs(placed[index].x - expected[index][0]) < 1.0e-5f);
        CHECK(std::fabs(placed[index].y - expected[index][1]) < 1.0e-5f);
        CHECK(placed[index].width == children[index].width);
        CHECK(placed[index].height == children[index].height);
    }
}

void test_layout_modes() {
    const std::vector<bo2::game::MenuBox> children = synthetic_children();
    check_layout(children, 1, {{-0.25f, -0.65f}, {-0.15f, -0.1f}, {-0.2f, 0.35f}});
    check_layout(children, 2, {{-1.0f, -0.1f}, {-0.1f, -0.05f}, {0.6f, -0.15f}});
    check_layout(children, 3, {{-0.25f, -0.31f}, {-0.15f, -0.1f}, {-0.2f, 0.01f}});
    check_layout(children, 4, {{-0.62f, -0.1f}, {-0.11f, -0.05f}, {0.2f, -0.15f}});
}

void test_layout_single_child() {
    const std::vector<bo2::game::MenuBox> child = {{0.0f, 0.0f, 0.5f, 0.2f}};
    check_layout(child, 2, {{-1.0f, -0.1f}});
    check_layout(child, 3, {{-0.25f, -0.1f}});
}

void test_layout_unknown_mode() {
    const std::vector<bo2::game::MenuBox> children = synthetic_children();
    for (const int mode : {0, 5, -1}) {
        const std::vector<bo2::game::MenuBox> placed =
            bo2::game::layout_children(kParent, mode, children);
        CHECK(placed.size() == children.size());
        for (std::size_t index = 0; index < children.size() && index < placed.size(); ++index) {
            CHECK(placed[index].x == children[index].x);
            CHECK(placed[index].y == children[index].y);
            CHECK(placed[index].width == children[index].width);
            CHECK(placed[index].height == children[index].height);
        }
    }
}

void test_main_menu_layout() {
    // FUN_005581d0: a mode-3 column over the whole screen with the widths of
    // the five Spanish items and a row of 0.0520833.
    const std::vector<bo2::game::MenuBox> items = {
        {0.0f, 0.0f, 0.307812f, 0.0520833f}, {0.0f, 0.0f, 0.160937f, 0.0520833f},
        {0.0f, 0.0f, 0.128125f, 0.0520833f}, {0.0f, 0.0f, 0.392187f, 0.0520833f},
        {0.0f, 0.0f, 0.29375f, 0.0520833f},
    };
    const std::vector<bo2::game::MenuBox> placed =
        bo2::game::layout_children(kParent, 3, items);
    CHECK(placed.size() == 5);
    if (placed.size() != 5) {
        return;
    }
    const float expected_y[] = {-0.150208f, -0.088125f, -0.026042f, 0.036042f, 0.098125f};
    for (std::size_t index = 0; index < placed.size(); ++index) {
        CHECK(std::fabs(placed[index].y - expected_y[index]) < 1.0e-5f);
        CHECK(std::fabs(placed[index].x + items[index].width * 0.5f) < 1.0e-5f);
    }
}

void test_main_menu_pens(const std::filesystem::path& path) {
    using bo2::engine::BigArchive;
    using bo2::engine::BigEntry;
    using bo2::engine::PixmapFont;
    using bo2::engine::StringTable;
    const BigArchive archive = BigArchive::load(path.string());
    const BigEntry* font_entry = archive.find("texfont", "main/main");
    CHECK(font_entry != nullptr);
    if (font_entry == nullptr) {
        return;
    }
    const PixmapFont font = bo2::engine::parse_pixmap_font(
        archive.extract(*font_entry), font_entry->symbols, archive.names());
    const bo2::engine::TextStyle style =
        bo2::engine::make_text_style(font, 0.0520833f, 1.0f, 240.0f);
    struct Case {
        const char* locale;
        float pens[5][2];
    };
    const Case cases[] = {
        {"spanish", {{270.75f, 216.45f}, {294.25f, 231.35f}, {299.5f, 246.25f},
                     {257.25f, 261.15f}, {273.0f, 276.05f}}},
        {"english", {{287.0f, 216.45f}, {298.0f, 231.35f}, {299.5f, 246.25f},
                     {282.0f, 261.15f}, {291.0f, 276.05f}}},
        {"french", {{267.25f, 216.45f}, {298.0f, 231.35f}, {302.75f, 246.25f},
                    {278.25f, 261.15f}, {267.25f, 276.05f}}},
        {"italian", {{280.75f, 216.45f}, {299.75f, 231.35f}, {302.75f, 246.25f},
                     {271.5f, 261.15f}, {277.5f, 276.05f}}},
        {"german", {{279.25f, 216.45f}, {294.5f, 231.35f}, {299.5f, 246.25f},
                    {265.0f, 261.15f}, {278.0f, 276.05f}}},
    };
    const char* const keys[] = {"fe_start_game", "fe_options", "fe_extras", "fe_replay_movie",
                                "fe_quit_game"};
    for (const Case& item : cases) {
        const BigEntry* entry = archive.find("strtable", "ui/UI", item.locale);
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        const StringTable table = StringTable::parse(archive, *entry);
        std::vector<bo2::game::MenuBox> children;
        for (const char* key : keys) {
            const std::string* text = table.find(key);
            CHECK(text != nullptr);
            if (text == nullptr) {
                continue;
            }
            const bo2::engine::TextSize size =
                bo2::engine::measure_text(font, style, *text, 320.0f, 240.0f);
            children.push_back({0.0f, 0.0f, size.width, size.height});
        }
        CHECK(children.size() == 5);
        if (children.size() != 5) {
            continue;
        }
        const std::vector<bo2::game::MenuBox> placed =
            bo2::game::layout_children(kParent, 3, children);
        CHECK(placed.size() == 5);
        for (std::size_t index = 0; index < placed.size() && index < 5; ++index) {
            const bo2::engine::TextPen pen = bo2::engine::text_pen(
                font, style, placed[index].x, -placed[index].y - placed[index].height, 320.0f,
                240.0f);
            CHECK(std::fabs(pen.x - item.pens[index][0]) < 1.0e-3f);
            CHECK(std::fabs(pen.y - item.pens[index][1]) < 1.0e-3f);
        }
    }
}

}  // namespace

int main() {
    test_layout_modes();
    test_layout_single_child();
    test_layout_unknown_mode();
    test_main_menu_layout();
    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "menu layout asset test skipped (BO2NX_ASSETS_DIR is unset)\n";
    } else {
        const std::filesystem::path code =
            std::filesystem::path(root) / "media/gogames/bo2/code.big";
        if (std::filesystem::exists(code)) {
            test_main_menu_pens(code);
        } else {
            std::cout << "menu layout asset test skipped (code.big not found)\n";
        }
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "menu layout tests: ok\n";
    return 0;
}
