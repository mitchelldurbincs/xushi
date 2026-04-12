cmake_minimum_required(VERSION 3.20)
project(xushi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(xushi src/main.cpp src/map.cpp)

enable_testing()
add_executable(test_los tests/test_los.cpp src/map.cpp)
add_test(NAME los COMMAND test_los)
