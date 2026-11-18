#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

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

}  // namespace

int main() {
    test_layout_modes();
    test_layout_single_child();
    test_layout_unknown_mode();
    test_main_menu_layout();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "menu layout tests: ok\n";
    return 0;
}
