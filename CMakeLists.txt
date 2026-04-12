cmake_minimum_required(VERSION 3.20)
project(xushi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(xushi
    src/main.cpp
    src/map.cpp
    src/sensing.cpp
    src/comm.cpp
    src/belief.cpp
    src/json.cpp
    src/scenario.cpp)

enable_testing()

add_executable(test_los tests/test_los.cpp src/map.cpp)
add_test(NAME los COMMAND test_los)

add_executable(test_rng tests/test_rng.cpp)
add_test(NAME rng COMMAND test_rng)

add_executable(test_sensing tests/test_sensing.cpp src/map.cpp src/sensing.cpp)
add_test(NAME sensing COMMAND test_sensing)

add_executable(test_comm tests/test_comm.cpp src/map.cpp src/sensing.cpp src/comm.cpp)
add_test(NAME comm COMMAND test_comm)

add_executable(test_belief tests/test_belief.cpp src/belief.cpp)
add_test(NAME belief COMMAND test_belief)

add_executable(test_json tests/test_json.cpp src/json.cpp)
add_test(NAME json COMMAND test_json)

add_executable(test_scenario tests/test_scenario.cpp
    src/json.cpp src/scenario.cpp src/map.cpp src/sensing.cpp src/comm.cpp src/belief.cpp)
add_test(NAME scenario COMMAND test_scenario)
set_tests_properties(scenario PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
