#include <iostream>

#include "bo2/version.hpp"

int main() {
    std::cout << bo2::kProjectName << ' ' << bo2::version() << '\n';
    return 0;
}
