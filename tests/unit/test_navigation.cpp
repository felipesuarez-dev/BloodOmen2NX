#include <iostream>
#include <vector>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/navigation.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

bo2::engine::CellGrid make_grid() {
    bo2::engine::CellGrid grid;
    for (int index = 0; index < 4; ++index) {
        bo2::engine::LevelCell cell;
        cell.name = "cell" + std::to_string(index);
        cell.minimum = {static_cast<float>(index * 10), 0.0f, 0.0f};
        cell.maximum = {static_cast<float>(index * 10 + 5), 5.0f, 5.0f};
        grid.cells.push_back(cell);
    }
    bo2::engine::CellPortal first;
    first.cell_a = 0;
    first.cell_b = 1;
    grid.cells[0].portals.push_back(first);
    bo2::engine::CellPortal second;
    second.cell_a = 1;
    second.cell_b = 2;
    grid.cells[1].portals.push_back(second);
    return grid;
}

void test_connections_and_path() {
    const bo2::engine::CellNavigation navigation(make_grid());
    CHECK(navigation.connections() == 2);
    CHECK(navigation.connected(0, 1));
    CHECK(navigation.connected(1, 0));
    CHECK(!navigation.connected(0, 2));
    const std::vector<std::uint32_t> path = navigation.path(0, 2);
    CHECK(path.size() == 3);
    CHECK(path[0] == 0);
    CHECK(path[1] == 1);
    CHECK(path[2] == 2);
    CHECK(navigation.path(3, 0).empty());
    CHECK(navigation.path(0, 0) == std::vector<std::uint32_t>{0});
    CHECK(navigation.path(0, 99).empty());
}

}  // namespace

int main() {
    test_connections_and_path();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "navigation tests: ok\n";
    return 0;
}
