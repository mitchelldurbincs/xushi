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
        if(WIN32)
            target_link_libraries(xushi_viewer winmm gdi32 opengl32)
        endif()
    else()
        target_include_directories(xushi_viewer PRIVATE ${RAYLIB_INCLUDE})
        target_link_libraries(xushi_viewer ${RAYLIB_LIB})
        # Platform libraries needed by raylib
        if(APPLE)
            target_link_libraries(xushi_viewer
                "-framework IOKit"
                "-framework Cocoa"
                "-framework OpenGL")
        elseif(WIN32)
            target_link_libraries(xushi_viewer
                winmm gdi32 opengl32)
        endif()
    endif()
    message(STATUS "raylib found — building xushi_viewer")
else()
    message(STATUS "raylib not found — skipping xushi_viewer")
endif()

# Tests
enable_testing()

function(add_xushi_test test_name source_file)
    add_executable(test_${test_name} ${source_file})
    target_link_libraries(test_${test_name} xushi_core)
    add_test(NAME ${test_name} COMMAND test_${test_name})
endfunction()

add_xushi_test(los tests/test_los.cpp)
add_xushi_test(rng tests/test_rng.cpp)
add_xushi_test(sensing tests/test_sensing.cpp)
add_xushi_test(comm tests/test_comm.cpp)
add_xushi_test(belief tests/test_belief.cpp)
add_xushi_test(json tests/test_json.cpp)
add_xushi_test(scenario tests/test_scenario.cpp)
set_tests_properties(scenario PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})


add_xushi_test(replay_path_resolution tests/test_replay_path_resolution.cpp)
add_xushi_test(replay tests/test_replay.cpp)

add_xushi_test(movement tests/test_movement.cpp)

add_xushi_test(determinism tests/test_determinism.cpp)
set_tests_properties(determinism PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_xushi_test(golden tests/test_golden.cpp)

add_xushi_test(parity tests/test_parity.cpp)
set_tests_properties(parity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_xushi_test(policy tests/test_policy.cpp)

add_xushi_test(action tests/test_action.cpp)

add_xushi_test(engagement tests/test_engagement.cpp)

add_xushi_test(game_mode tests/test_game_mode.cpp)
