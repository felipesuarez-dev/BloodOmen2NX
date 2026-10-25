#include "bo2/version.hpp"

int main() {
    if (bo2::kProjectName != "Blood Omen 2 NX") {
        return 1;
    }
    if (bo2::version() != bo2::kVersion) {
        return 2;
    }
    return 0;
}
