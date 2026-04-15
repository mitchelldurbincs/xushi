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

if(NOT RAYLIB_AVAILABLE)
    message(FATAL_ERROR "XUSHI_ENABLE_VIEWER=ON but raylib was not found")
endif()

add_executable(xushi_viewer
    main.cpp
    viewer_load.cpp
    viewer_update.cpp
    viewer_draw.cpp)

target_link_libraries(xushi_viewer PRIVATE xushi_core)
target_include_directories(xushi_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(raylib_FOUND)
    target_link_libraries(xushi_viewer PRIVATE raylib)
    if(WIN32)
        target_link_libraries(xushi_viewer PRIVATE winmm gdi32 opengl32)
    endif()
else()
    target_include_directories(xushi_viewer PRIVATE ${RAYLIB_INCLUDE})
    target_link_libraries(xushi_viewer PRIVATE ${RAYLIB_LIB})
    if(APPLE)
        target_link_libraries(xushi_viewer PRIVATE
            "-framework IOKit"
            "-framework Cocoa"
            "-framework OpenGL")
    elseif(WIN32)
        target_link_libraries(xushi_viewer PRIVATE
            winmm gdi32 opengl32)
    endif()
endif()

message(STATUS "xushi_viewer enabled via viewer subproject")
