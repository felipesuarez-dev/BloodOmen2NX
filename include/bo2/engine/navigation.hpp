#pragma once

#include <cstdint>
#include <vector>

#include "bo2/engine/cell_group.hpp"

namespace bo2::engine {

class CellNavigation {
public:
    explicit CellNavigation(const CellGrid& grid);

    bool has_cell(std::uint32_t cell) const;
    bool connected(std::uint32_t first, std::uint32_t second) const;
    std::size_t connections() const;
    std::vector<std::uint32_t> path(std::uint32_t from, std::uint32_t to) const;

private:
    struct Node {
        Vec3 center{0.0f, 0.0f, 0.0f};
        std::vector<std::uint32_t> neighbours;
    };

    std::vector<Node> nodes_;
};

}  // namespace bo2::engine
