cmake_minimum_required(VERSION 3.28)
project(bo2nx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BO2NX_WARNINGS -Wall -Wextra -Wshadow -Wconversion)

add_library(bo2_common STATIC
    src/common/version.cpp
)
target_include_directories(bo2_common PUBLIC include)
target_compile_options(bo2_common PRIVATE ${BO2NX_WARNINGS})

add_executable(bo2nx src/main.cpp)
target_link_libraries(bo2nx PRIVATE bo2_common)
target_compile_options(bo2nx PRIVATE ${BO2NX_WARNINGS})

include(CTest)
if(BUILD_TESTING)
    add_executable(bo2nx_tests tests/unit/test_version.cpp)
    target_link_libraries(bo2nx_tests PRIVATE bo2_common)
    target_compile_options(bo2nx_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME version COMMAND bo2nx_tests)
endif()
