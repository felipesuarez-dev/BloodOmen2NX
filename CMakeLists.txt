cmake_minimum_required(VERSION 3.28)
project(bo2nx VERSION 0.1.0 LANGUAGES C CXX)

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

set_source_files_properties(third_party/stb_vorbis.c PROPERTIES COMPILE_OPTIONS "-w")

add_library(bo2_engine STATIC
    src/engine/anim_reel.cpp
    src/engine/audio.cpp
    src/engine/big_archive.cpp
    src/engine/cell_group.cpp
    src/engine/collision.cpp
    src/engine/dds.cpp
    src/engine/entity_template.cpp
    src/engine/erg.cpp
    src/engine/navigation.cpp
    src/engine/ogg.cpp
    src/engine/region.cpp
    src/engine/string_table.cpp
    src/engine/tick.cpp
    src/engine/world.cpp
    third_party/stb_vorbis.c
)
target_link_libraries(bo2_engine PUBLIC bo2_common ${CMAKE_DL_LIBS})
target_compile_options(bo2_engine PRIVATE ${BO2NX_WARNINGS})

add_library(bo2_game STATIC
    src/game/camera.cpp
    src/game/kain_tuning.cpp
    src/game/player.cpp
    src/game/save.cpp
)
target_link_libraries(bo2_game PUBLIC bo2_engine)
target_include_directories(bo2_game PUBLIC include)
target_compile_options(bo2_game PRIVATE ${BO2NX_WARNINGS})

add_library(bo2_platform_core STATIC
    src/platform/input.cpp
    src/platform/linux/platform.cpp
)
target_include_directories(bo2_platform_core PUBLIC include)
target_compile_options(bo2_platform_core PRIVATE ${BO2NX_WARNINGS})

add_library(bo2_platform_headless STATIC
    src/platform/headless/software_renderer.cpp
)
target_link_libraries(bo2_platform_headless PUBLIC bo2_engine)
target_compile_options(bo2_platform_headless PRIVATE ${BO2NX_WARNINGS})

option(BO2NX_ENABLE_SDL "Build the SDL2 and OpenGL host backend" ON)
set(BO2NX_HAVE_SDL OFF)
if(BO2NX_ENABLE_SDL)
    find_package(SDL2 CONFIG QUIET)
    find_package(OpenGL QUIET)
    if(SDL2_FOUND AND OPENGL_FOUND)
        add_library(bo2_platform_sdl STATIC
            src/platform/sdl/audio.cpp
            src/platform/sdl/render_pipeline.cpp
            src/platform/sdl/sdl_backend.cpp
        )
        target_link_libraries(bo2_platform_sdl PUBLIC bo2_engine bo2_game bo2_platform_core
                                                     SDL2::SDL2 OpenGL::GL)
        target_include_directories(bo2_platform_sdl PUBLIC include)
        target_compile_options(bo2_platform_sdl PRIVATE ${BO2NX_WARNINGS})
        set(BO2NX_HAVE_SDL ON)
    endif()
endif()

add_executable(bo2nx src/main.cpp)
target_link_libraries(bo2nx PRIVATE bo2_platform_headless bo2_platform_core)
target_compile_options(bo2nx PRIVATE ${BO2NX_WARNINGS})
if(BO2NX_HAVE_SDL)
    target_compile_definitions(bo2nx PRIVATE BO2NX_HAVE_SDL)
    target_link_libraries(bo2nx PRIVATE bo2_platform_sdl)
endif()

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

    add_executable(bo2nx_model_asset_tests tests/unit/test_model_asset.cpp)
    target_link_libraries(bo2nx_model_asset_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_model_asset_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME model_asset COMMAND bo2nx_model_asset_tests)

    add_executable(bo2nx_entity_template_tests tests/unit/test_entity_template.cpp)
    target_link_libraries(bo2nx_entity_template_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_entity_template_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME entity_template COMMAND bo2nx_entity_template_tests)

    add_executable(bo2nx_region_tests tests/unit/test_region.cpp)
    target_link_libraries(bo2nx_region_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_region_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME region COMMAND bo2nx_region_tests)

    add_executable(bo2nx_save_tests tests/unit/test_save.cpp)
    target_link_libraries(bo2nx_save_tests PRIVATE bo2_game)
    target_compile_options(bo2nx_save_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME save COMMAND bo2nx_save_tests)

    add_executable(bo2nx_camera_tests tests/unit/test_camera.cpp)
    target_link_libraries(bo2nx_camera_tests PRIVATE bo2_game)
    target_compile_options(bo2nx_camera_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME camera COMMAND bo2nx_camera_tests)

    add_executable(bo2nx_anim_reel_tests tests/unit/test_anim_reel.cpp)
    target_link_libraries(bo2nx_anim_reel_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_anim_reel_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME anim_reel COMMAND bo2nx_anim_reel_tests)

    add_executable(bo2nx_kain_tuning_tests tests/unit/test_kain_tuning.cpp)
    target_link_libraries(bo2nx_kain_tuning_tests PRIVATE bo2_game)
    target_compile_options(bo2nx_kain_tuning_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME kain_tuning COMMAND bo2nx_kain_tuning_tests)

    add_executable(bo2nx_kain_asset_tests tests/unit/test_kain_asset.cpp)
    target_link_libraries(bo2nx_kain_asset_tests PRIVATE bo2_game)
    target_compile_options(bo2nx_kain_asset_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME kain_asset COMMAND bo2nx_kain_asset_tests)

    add_executable(bo2nx_player_tests tests/unit/test_player.cpp)
    target_link_libraries(bo2nx_player_tests PRIVATE bo2_game)
    target_compile_options(bo2nx_player_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME player COMMAND bo2nx_player_tests)

    add_executable(bo2nx_collision_tests tests/unit/test_collision.cpp)
    target_link_libraries(bo2nx_collision_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_collision_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME collision COMMAND bo2nx_collision_tests)

    add_executable(bo2nx_navigation_tests tests/unit/test_navigation.cpp)
    target_link_libraries(bo2nx_navigation_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_navigation_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME navigation COMMAND bo2nx_navigation_tests)

    add_executable(bo2nx_world_tests tests/unit/test_world.cpp)
    target_link_libraries(bo2nx_world_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_world_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME world COMMAND bo2nx_world_tests)

    add_executable(bo2nx_tick_tests tests/unit/test_tick.cpp)
    target_link_libraries(bo2nx_tick_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_tick_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME tick COMMAND bo2nx_tick_tests)

    add_executable(bo2nx_audio_tests tests/unit/test_audio.cpp)
    target_link_libraries(bo2nx_audio_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_audio_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME audio COMMAND bo2nx_audio_tests)

    add_executable(bo2nx_locale_tests tests/unit/test_locale.cpp)
    target_link_libraries(bo2nx_locale_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_locale_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME locale COMMAND bo2nx_locale_tests)

    add_executable(bo2nx_locale_asset_tests tests/unit/test_locale_asset.cpp)
    target_link_libraries(bo2nx_locale_asset_tests PRIVATE bo2_engine)
    target_compile_options(bo2nx_locale_asset_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME locale_asset COMMAND bo2nx_locale_asset_tests)

    add_executable(bo2nx_platform_tests tests/unit/test_platform.cpp)
    target_link_libraries(bo2nx_platform_tests PRIVATE bo2_platform_core)
    target_compile_options(bo2nx_platform_tests PRIVATE ${BO2NX_WARNINGS})
    add_test(NAME platform COMMAND bo2nx_platform_tests)
endif()

if(BO2NX_HAVE_SDL)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME sdl_triangle
            COMMAND ${XVFB_RUN} -a -s "-screen 0 320x240x24" $<TARGET_FILE:bo2nx> --sdl
                    --width 128 --height 96 --out ${CMAKE_BINARY_DIR}/sdl_triangle.ppm)
        set_tests_properties(sdl_triangle PROPERTIES PASS_REGULAR_EXPRESSION "sdl frame")
    endif()
endif()
