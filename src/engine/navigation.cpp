#include "bo2/engine/navigation.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <utility>

namespace bo2::engine {

CellNavigation::CellNavigation(const CellGrid& grid) {
    nodes_.resize(grid.cells.size());
    std::map<std::uint16_t, std::uint32_t> symbol_to_index;
    for (std::size_t index = 0; index < grid.cells.size(); ++index) {
        const LevelCell& cell = grid.cells[index];
        nodes_[index].center = (cell.minimum + cell.maximum) * 0.5f;
        symbol_to_index.emplace(cell.symbol, static_cast<std::uint32_t>(index));
    }
    const auto resolve = [&symbol_to_index, this](std::uint16_t value) {
        const auto found = symbol_to_index.find(value);
        if (found != symbol_to_index.end()) {
            return found->second;
        }
        return value < nodes_.size() ? static_cast<std::uint32_t>(value)
                                     : static_cast<std::uint32_t>(nodes_.size());
    };
    for (const LevelCell& cell : grid.cells) {
        for (const CellPortal& portal : cell.portals) {
            const std::uint32_t first_index = resolve(portal.cell_a);
            const std::uint32_t second_index = resolve(portal.cell_b);
            if (first_index >= nodes_.size() || second_index >= nodes_.size() ||
                first_index == second_index) {
                continue;
            }
            auto& first = nodes_[first_index].neighbours;
            auto& second = nodes_[second_index].neighbours;
            if (std::find(first.begin(), first.end(), second_index) == first.end()) {
                first.push_back(second_index);
            }
            if (std::find(second.begin(), second.end(), first_index) == second.end()) {
                second.push_back(first_index);
            }
        }
    }
    for (Node& node : nodes_) {
        std::sort(node.neighbours.begin(), node.neighbours.end());
    }
}

bool CellNavigation::has_cell(std::uint32_t cell) const {
    return cell < nodes_.size();
}

bool CellNavigation::connected(std::uint32_t first, std::uint32_t second) const {
    if (first >= nodes_.size() || second >= nodes_.size()) {
        return false;
    }
    return std::find(nodes_[first].neighbours.begin(), nodes_[first].neighbours.end(), second) !=
           nodes_[first].neighbours.end();
}

std::size_t CellNavigation::connections() const {
    std::size_t total = 0;
    for (const Node& node : nodes_) {
        total += node.neighbours.size();
    }
    return total / 2;
}

std::vector<std::uint32_t> CellNavigation::path(std::uint32_t from, std::uint32_t to) const {
    if (from >= nodes_.size() || to >= nodes_.size()) {
        return {};
    }
    if (from == to) {
        return {from};
    }
    constexpr float infinity = std::numeric_limits<float>::infinity();
    std::vector<float> cost(nodes_.size(), infinity);
    std::vector<std::uint32_t> previous(nodes_.size(), 0);
    std::vector<bool> visited(nodes_.size(), false);
    using Entry = std::pair<float, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    cost[from] = 0.0f;
    queue.emplace(0.0f, from);
    while (!queue.empty()) {
        const auto [current_cost, current] = queue.top();
        queue.pop();
        if (visited[current]) {
            continue;
        }
        visited[current] = true;
        if (current == to) {
            break;
        }
        for (const std::uint32_t neighbour : nodes_[current].neighbours) {
            const float next_cost = current_cost + length(nodes_[current].center - nodes_[neighbour].center);
            if (next_cost < cost[neighbour]) {
                cost[neighbour] = next_cost;
                previous[neighbour] = current;
                queue.emplace(next_cost, neighbour);
            }
        }
    }
    if (!std::isfinite(cost[to])) {
        return {};
    }
    std::vector<std::uint32_t> result;
    for (std::uint32_t current = to;; current = previous[current]) {
        result.push_back(current);
        if (current == from) {
            break;
        }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

}  // namespace bo2::engine
