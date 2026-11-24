cmake_minimum_required(VERSION 3.25)
project(blackhole VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4)
FetchContent_MakeAvailable(glfw)

add_executable(blackhole
    src/main.cpp
    src/gl_api.cpp
    src/gl_api.h
    src/font8x8.h)

target_link_libraries(blackhole PRIVATE glfw)

if (MSVC)
    target_compile_options(blackhole PRIVATE /O2 /fp:fast /W3 /utf-8)
    target_compile_definitions(blackhole PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(blackhole PRIVATE -O3 -ffast-math)
endif()

target_compile_definitions(blackhole PRIVATE
    SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")

add_custom_command(TARGET blackhole POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:blackhole>/shaders")
