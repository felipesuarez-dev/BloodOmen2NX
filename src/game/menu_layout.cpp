#include "bo2/game/menu_layout.hpp"

namespace bo2::game {

namespace {

// The gap between the children of a centred layout: the float 0x3c23d70a
// (0.01) written at 0x841c08, and one half, the float at 0x83f5c8.
constexpr float kGap = 0.01f;
constexpr float kHalf = 0.5f;

}  // namespace

std::vector<MenuBox> layout_children(const MenuBox& parent, int mode,
                                     const std::vector<MenuBox>& children) {
    std::vector<MenuBox> placed = children;
    const float count = static_cast<float>(children.size());
    float sum_width = 0.0f;
    float sum_height = 0.0f;
    for (const MenuBox& child : children) {
        sum_width += child.width;
        sum_height += child.height;
    }
    if (mode == 1) {
        // A column spread over the height: the free height is shared by the
        // count + 1 spaces before, between and after the children.
        float y = parent.y;
        for (MenuBox& child : placed) {
            y += (parent.height - sum_height) / (count + 1.0f);
            child.x = (parent.width - child.width) * kHalf + parent.x;
            child.y = y;
            y += child.height;
        }
    } else if (mode == 2) {
        // A row spread over the width; the step needs two children.
        const float step =
            count > 1.0f ? (parent.width - sum_width) / (count - 1.0f) : 0.0f;
        float x = parent.x;
        for (MenuBox& child : placed) {
            child.x = x;
            child.y = (parent.height - child.height) * kHalf + parent.y;
            x += child.width + step;
        }
    } else if (mode == 3) {
        // A centred column with one gap between the children; a single child
        // takes no gap.
        const float gap = count > 1.0f ? (count - 1.0f) * kGap : 0.0f;
        float y = (parent.height - sum_height - gap) * kHalf + parent.y;
        for (MenuBox& child : placed) {
            child.x = (parent.width - child.width) * kHalf + parent.x;
            child.y = y;
            y += child.height + kGap;
        }
    } else if (mode == 4) {
        // A centred row with one gap before each child and one after the last.
        float x = (parent.width - sum_width - (count + 1.0f) * kGap) * kHalf + parent.x;
        for (MenuBox& child : placed) {
            child.x = x;
            child.y = (parent.height - child.height) * kHalf + parent.y;
            x += child.width + kGap;
        }
    }
    return placed;
}

}  // namespace bo2::game
