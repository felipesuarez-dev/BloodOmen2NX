#pragma once

#include <vector>

namespace bo2::game {

// One element of a menu: its top-left corner (x, y, y grows down), its width
// and its height, in screen units, where the screen goes from -1 to 1 in both
// axes (docs/TEXT.md, "The text of a menu").
struct MenuBox {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Places the children of a menu element (FUN_0055c6c0): mode 1 is a column
// spread over the height, 2 a row spread over the width, 3 a centred column
// and 4 a centred row. Any other mode leaves the children where they are.
// The width and the height of a child do not change.
std::vector<MenuBox> layout_children(const MenuBox& parent, int mode,
                                     const std::vector<MenuBox>& children);

}  // namespace bo2::game
