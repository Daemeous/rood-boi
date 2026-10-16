cmake_minimum_required(VERSION 3.16)
project(rood_pc LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_library(rood_memory STATIC src/memory.c)
target_include_directories(rood_memory PUBLIC src)
target_compile_options(rood_memory PUBLIC -funsigned-char)
target_compile_options(rood_memory PRIVATE -Wall -Wextra -Werror)
include(CTest)
add_executable(memory_probe tests/memory_probe.c)
target_link_libraries(memory_probe PRIVATE rood_memory)
add_test(NAME memory_probe COMMAND memory_probe)

# The boot diagnostic deliberately uses a 32-bit ABI and the original SDK headers.
# It requires the matching build's extracted assets; it is not part of the
# dependency-free memory probe's default build.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME event_callbacks COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/event_callbacks_test.py)

# Light actor prefabs (JSON, expressions, deterministic animation, the
# built-in library in pc/assets/prefabs) without the game.
enable_language(CXX)
add_executable(prefab_probe tests/prefab_probe.cpp src/light_prefabs.cpp)
target_include_directories(prefab_probe PRIVATE src)
target_compile_features(prefab_probe PRIVATE cxx_std_17)
target_compile_options(prefab_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME prefab_probe COMMAND prefab_probe ${CMAKE_CURRENT_SOURCE_DIR}/assets/prefabs)
# Lighting packs (lighting/README.md): the game's layered room files, merged
# and diffed as pc/tools/lighting_format.py does; the lint on the packs.
add_executable(lighting_probe tests/lighting_probe.cpp src/lighting_layers.cpp src/light_prefabs.cpp)
target_include_directories(lighting_probe PRIVATE src)
target_compile_features(lighting_probe PRIVATE cxx_std_17)
target_compile_options(lighting_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME lighting_layers COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/lighting_layers_test.py
    --probe $<TARGET_FILE:lighting_probe>)
# The fixture catalog (pc/assets/fixtures/catalog.json): the game's reader
# against the tools' (schema, resolved types, canonical file), and the
# generated layer (lint, determinism, up to date when the disc is there).
add_executable(fixture_probe tests/fixture_probe.cpp src/fixture_catalog.cpp src/light_prefabs.cpp)
target_include_directories(fixture_probe PRIVATE src)
target_compile_features(fixture_probe PRIVATE cxx_std_17)
target_compile_options(fixture_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME fixture_catalog COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture_catalog_test.py
    --probe $<TARGET_FILE:fixture_probe>)
set_tests_properties(fixture_catalog PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The editor's selection model and picking math (pc/src/editor_selection.cpp, editor_picking.cpp) without the game.
add_executable(selection_probe tests/selection_probe.cpp src/editor_selection.cpp src/editor_picking.cpp)
target_include_directories(selection_probe PRIVATE src)
target_compile_features(selection_probe PRIVATE cxx_std_17)
target_compile_options(selection_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME selection_probe COMMAND selection_probe)
# Modern menus: the shell's navigation (pc/src/ui/ui_shell.cpp) without the game.
add_executable(ui_shell_probe tests/ui_shell_probe.cpp src/ui/ui_shell.cpp src/ui/ui_loot.cpp src/ui/ui_workshop.cpp
    src/ui/ui_map.cpp
    src/ui/ui_data.cpp src/ui/data_plan.c src/ui/ui_container.cpp
    src/ui/ui_manual.cpp src/ui/manual_parse.c
    src/ui/ui_theme.cpp src/ui/ui_tint.cpp
    src/light_prefabs.cpp)
target_include_directories(ui_shell_probe PRIVATE src)
target_compile_features(ui_shell_probe PRIVATE cxx_std_17)
target_compile_options(ui_shell_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME ui_shell_probe COMMAND ui_shell_probe)
# The same under AddressSanitizer / UBSan where the toolchain has them: the
# Quick manual's file parser (manual_parse.c) on truncated page files.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
check_cxx_source_compiles("int main() { return 0; }" ROOD_HAVE_ASAN)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(ROOD_HAVE_ASAN)
    get_target_property(probe_sources ui_shell_probe SOURCES)
    add_executable(ui_shell_probe_asan ${probe_sources})
    target_include_directories(ui_shell_probe_asan PRIVATE src)
    target_compile_features(ui_shell_probe_asan PRIVATE cxx_std_17)
    target_compile_options(ui_shell_probe_asan PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined
        -fno-sanitize-recover=all)
    target_link_options(ui_shell_probe_asan PRIVATE -fsanitize=address,undefined)
    add_test(NAME ui_shell_probe_asan COMMAND ui_shell_probe_asan)
endif()
add_custom_target(boot
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_boot.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    USES_TERMINAL)

add_custom_target(check-boot
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_smoke.py
    DEPENDS boot
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    USES_TERMINAL)
add_custom_target(battle-probe
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_boot.py --battle
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    USES_TERMINAL)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The gamepad mapping only (SDL headers, no SDL library); the builder links
    # the --hardware variant against SDL.
    add_executable(controller_probe tests/controller_probe.c src/boot_controller.c)
    target_include_directories(controller_probe PRIVATE src third_party/SDL3/include)
    target_compile_options(controller_probe PRIVATE -Wall -Wextra -Werror)
    add_test(NAME controller_probe COMMAND controller_probe)
    # Modern menus, System tab: the settings pages (pc/src/ui/ui_settings.cpp)
    # against stubs; once with the game options bridge, once without it and
    # with the later features' hooks.
    foreach(variant IN ITEMS ui_settings_probe ui_settings_probe_hooks)
        add_executable(${variant} tests/ui_settings_probe.cpp tests/ui_settings_stubs.cpp src/ui/ui_settings.cpp
            src/ui/ui_tint.cpp src/antialias.c src/camera_stick.c src/boot_controller.c)
        target_include_directories(${variant} PRIVATE src tests third_party/SDL3/include)
        target_compile_features(${variant} PRIVATE cxx_std_17)
        target_compile_options(${variant} PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(${variant} PRIVATE m)
        add_test(NAME ${variant} COMMAND ${variant})
    endforeach()
    target_compile_definitions(ui_settings_probe_hooks PRIVATE UI_SETTINGS_PROBE_HOOKS)
endif()

# The right-stick camera's logic (camera_stick.c) against a fake game camera.
add_executable(camera_stick_probe tests/camera_stick_probe.c src/camera_stick.c)
target_include_directories(camera_stick_probe PRIVATE src)
target_compile_options(camera_stick_probe PRIVATE -Wall -Wextra -Werror)
target_link_libraries(camera_stick_probe PRIVATE m)
add_test(NAME camera_stick_probe COMMAND camera_stick_probe)

# The map screen's right stick and triggers (ui/map_pad.c, Map controls Modern / Original).
add_executable(map_pad_probe tests/map_pad_probe.c src/ui/map_pad.c)
target_include_directories(map_pad_probe PRIVATE src/ui)
target_compile_options(map_pad_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME map_pad_probe COMMAND map_pad_probe)

# The native Container screen's data operations (ui/container_ops.c, MENUD ported) without the game.
add_executable(container_ops_probe tests/container_ops_probe.c src/ui/container_ops.c)
target_include_directories(container_ops_probe PRIVATE src/ui)
target_compile_definitions(container_ops_probe PRIVATE PC_CONTAINER_OPS_PROBE)
target_compile_options(container_ops_probe PRIVATE -Wall -Wextra -Werror -funsigned-char)
add_test(NAME container_ops_probe COMMAND container_ops_probe)
# The native Data screen's planner (ui/data_plan.c): goals to MENU7 pad presses, exhaustively.
add_executable(data_plan_probe tests/data_plan_probe.c src/ui/data_plan.c)
target_include_directories(data_plan_probe PRIVATE src/ui)
target_compile_options(data_plan_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME data_plan_probe COMMAND data_plan_probe)

# Exercise the actual packet decoder without running game code or opening a window.
add_library(gpu_probe_runtime OBJECT src/boot_runtime.c)
target_compile_definitions(gpu_probe_runtime PRIVATE main=pc_boot_main)
target_compile_options(gpu_probe_runtime PRIVATE -ffunction-sections -fdata-sections)
add_executable(gpu_probe tests/gpu_probe.c tests/gpu_hooks_stub.c $<TARGET_OBJECTS:gpu_probe_runtime>)
target_link_options(gpu_probe PRIVATE -Wl,--gc-sections)
add_test(NAME gpu_probe COMMAND gpu_probe)
add_custom_target(check-room
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/room_smoke.py
    DEPENDS battle-probe
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    USES_TERMINAL)

add_custom_target(check-opening
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/opening_smoke.py
    DEPENDS battle-probe
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    USES_TERMINAL)

add_custom_target(check-scene
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_smoke.py
    DEPENDS battle-probe
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    USES_TERMINAL)

add_custom_target(check-new-game
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/new_game_smoke.py
    DEPENDS battle-probe
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    USES_TERMINAL)
