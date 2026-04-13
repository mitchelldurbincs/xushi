cmake_minimum_required(VERSION 3.20)
project(xushi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core library — shared by main executable and all tests
add_library(xushi_core STATIC
    src/map.cpp
    src/sensing.cpp
    src/comm.cpp
    src/belief.cpp
    src/json.cpp
    src/scenario.cpp
    src/replay.cpp
    src/path_resolve.cpp
    src/stats.cpp
    src/sim.cpp
    src/sim_engine.cpp
    src/engagement.cpp
    src/game_mode.cpp
    src/asset_protection_mode.cpp)

add_executable(xushi src/main.cpp)
target_link_libraries(xushi xushi_core)

# Replay viewer (requires raylib)
find_package(raylib QUIET)
if(NOT raylib_FOUND)
    find_library(RAYLIB_LIB raylib)
    find_path(RAYLIB_INCLUDE raylib.h)
    if(RAYLIB_LIB AND RAYLIB_INCLUDE)
        set(RAYLIB_AVAILABLE TRUE)
    endif()
else()
    set(RAYLIB_AVAILABLE TRUE)
endif()

if(RAYLIB_AVAILABLE)
    add_executable(xushi_viewer
        viewer/main.cpp
        viewer/viewer_load.cpp
        viewer/viewer_update.cpp
        viewer/viewer_draw.cpp)
    target_link_libraries(xushi_viewer xushi_core)
    target_include_directories(xushi_viewer PRIVATE viewer)
    if(raylib_FOUND)
        target_link_libraries(xushi_viewer raylib)
    else()
        target_include_directories(xushi_viewer PRIVATE ${RAYLIB_INCLUDE})
        target_link_libraries(xushi_viewer ${RAYLIB_LIB})
        # macOS frameworks needed by raylib
        if(APPLE)
            target_link_libraries(xushi_viewer
                "-framework IOKit"
                "-framework Cocoa"
                "-framework OpenGL")
        endif()
    endif()
    message(STATUS "raylib found — building xushi_viewer")
else()
    message(STATUS "raylib not found — skipping xushi_viewer")
endif()

# Tests
enable_testing()

add_executable(test_los tests/test_los.cpp)
target_link_libraries(test_los xushi_core)
add_test(NAME los COMMAND test_los)

add_executable(test_rng tests/test_rng.cpp)
add_test(NAME rng COMMAND test_rng)

add_executable(test_sensing tests/test_sensing.cpp)
target_link_libraries(test_sensing xushi_core)
add_test(NAME sensing COMMAND test_sensing)

add_executable(test_comm tests/test_comm.cpp)
target_link_libraries(test_comm xushi_core)
add_test(NAME comm COMMAND test_comm)

add_executable(test_belief tests/test_belief.cpp)
target_link_libraries(test_belief xushi_core)
add_test(NAME belief COMMAND test_belief)

add_executable(test_json tests/test_json.cpp)
target_link_libraries(test_json xushi_core)
add_test(NAME json COMMAND test_json)

add_executable(test_scenario tests/test_scenario.cpp)
target_link_libraries(test_scenario xushi_core)
add_test(NAME scenario COMMAND test_scenario)
set_tests_properties(scenario PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})


add_executable(test_replay_path_resolution tests/test_replay_path_resolution.cpp)
target_link_libraries(test_replay_path_resolution xushi_core)
add_test(NAME replay_path_resolution COMMAND test_replay_path_resolution)
add_executable(test_replay tests/test_replay.cpp)
target_link_libraries(test_replay xushi_core)
add_test(NAME replay COMMAND test_replay)

add_executable(test_movement tests/test_movement.cpp)
target_link_libraries(test_movement xushi_core)
add_test(NAME movement COMMAND test_movement)

add_executable(test_determinism tests/test_determinism.cpp)
target_link_libraries(test_determinism xushi_core)
add_test(NAME determinism COMMAND test_determinism)
set_tests_properties(determinism PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_golden tests/test_golden.cpp)
target_link_libraries(test_golden xushi_core)
add_test(NAME golden COMMAND test_golden)

add_executable(test_parity tests/test_parity.cpp)
target_link_libraries(test_parity xushi_core)
add_test(NAME parity COMMAND test_parity)
set_tests_properties(parity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_policy tests/test_policy.cpp)
target_link_libraries(test_policy xushi_core)
add_test(NAME policy COMMAND test_policy)

add_executable(test_action tests/test_action.cpp)
target_link_libraries(test_action xushi_core)
add_test(NAME action COMMAND test_action)

add_executable(test_engagement tests/test_engagement.cpp)
target_link_libraries(test_engagement xushi_core)
add_test(NAME engagement COMMAND test_engagement)

add_executable(test_game_mode tests/test_game_mode.cpp)
target_link_libraries(test_game_mode xushi_core)
add_test(NAME game_mode COMMAND test_game_mode)
