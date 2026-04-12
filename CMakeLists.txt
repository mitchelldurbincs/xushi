cmake_minimum_required(VERSION 3.20)
project(xushi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(xushi src/main.cpp src/map.cpp src/sensing.cpp)

enable_testing()

add_executable(test_los tests/test_los.cpp src/map.cpp)
add_test(NAME los COMMAND test_los)

add_executable(test_rng tests/test_rng.cpp)
add_test(NAME rng COMMAND test_rng)

add_executable(test_sensing tests/test_sensing.cpp src/map.cpp src/sensing.cpp)
add_test(NAME sensing COMMAND test_sensing)
