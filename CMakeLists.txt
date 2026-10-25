cmake_minimum_required(VERSION 3.28)
project(bo2nx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BO2NX_WARNINGS -Wall -Wextra -Wshadow -Wconversion)
add_compile_options(-ffp-contract=off)

add_library(bo2_common STATIC
    src/common/version.cpp
)
target_include_directories(bo2_common PUBLIC include)
target_compile_options(bo2_common PRIVATE ${BO2NX_WARNINGS})

add_library(bo2_engine STATIC
    src/engine/big_archive.cpp
    src/engine/dds.cpp
)
target_link_libraries(bo2_engine PUBLIC bo2_common)
target_compile_options(bo2_engine PRIVATE ${BO2NX_WARNINGS})

add_library(bo2_platform_headless STATIC
    src/platform/headless/software_renderer.cpp
)
target_link_libraries(bo2_platform_headless PUBLIC bo2_engine)
target_compile_options(bo2_platform_headless PRIVATE ${BO2NX_WARNINGS})

add_executable(bo2nx src/main.cpp)
target_link_libraries(bo2nx PRIVATE bo2_platform_headless)
target_compile_options(bo2nx PRIVATE ${BO2NX_WARNINGS})

include(CTest)
if(BUILD_TESTING)
    add_executable(bo2nx_tests tests/unit/test_version.cpp)
    target_link_libraries(bo2nx_tests PRIVATE bo2_common)
    target_compile_options(bo2nx_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME version COMMAND bo2nx_tests)

    add_executable(bo2nx_engine_tests tests/unit/test_engine.cpp)
    target_link_libraries(bo2nx_engine_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_engine_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME engine COMMAND bo2nx_engine_tests)

    add_executable(bo2nx_renderer_tests tests/unit/test_renderer.cpp)
    target_link_libraries(bo2nx_renderer_tests PRIVATE bo2_platform_headless)
    target_compile_options(bo2nx_renderer_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME renderer COMMAND bo2nx_renderer_tests)
endif()
