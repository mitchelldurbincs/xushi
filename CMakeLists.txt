cmake_minimum_required(VERSION 3.20)
project(xushi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core library — shared by main executable and all tests.
add_library(xushi_core STATIC
    src/asset_protection_mode.cpp
    src/belief.cpp
    src/comm.cpp
    src/game_mode.cpp
    src/grid.cpp
    src/json.cpp
    src/office_breach_mode.cpp
    src/replay.cpp
    src/scenario.cpp
    src/sim_engine.cpp
    src/world_hash.cpp)

target_include_directories(xushi_core PUBLIC src)

add_executable(xushi src/main.cpp)
target_link_libraries(xushi xushi_core)

# Tests
enable_testing()

function(add_xushi_test test_name source_file)
    add_executable(test_${test_name} ${source_file})
    target_link_libraries(test_${test_name} xushi_core)
    add_test(NAME ${test_name} COMMAND test_${test_name})
endfunction()

add_xushi_test(rng       tests/test_rng.cpp)
add_xushi_test(json      tests/test_json.cpp)
add_xushi_test(grid      tests/test_grid.cpp)
add_xushi_test(comm      tests/test_comm.cpp)
add_xushi_test(belief    tests/test_belief.cpp)
add_xushi_test(replay    tests/test_replay.cpp)

add_xushi_test(scenario  tests/test_scenario.cpp)
set_tests_properties(scenario PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_xushi_test(contract  tests/test_contract.cpp)
set_tests_properties(contract PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_xushi_test(action    tests/test_action.cpp)
set_tests_properties(action PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_xushi_test(game_mode tests/test_game_mode.cpp)
