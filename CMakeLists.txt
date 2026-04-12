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
    src/stats.cpp
    src/sim.cpp)

add_executable(xushi src/main.cpp)
target_link_libraries(xushi xushi_core)

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

add_executable(test_replay tests/test_replay.cpp)
target_link_libraries(test_replay xushi_core)
add_test(NAME replay COMMAND test_replay)

add_executable(test_determinism tests/test_determinism.cpp)
target_link_libraries(test_determinism xushi_core)
add_test(NAME determinism COMMAND test_determinism)
set_tests_properties(determinism PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_golden tests/test_golden.cpp)
target_link_libraries(test_golden xushi_core)
add_test(NAME golden COMMAND test_golden)
