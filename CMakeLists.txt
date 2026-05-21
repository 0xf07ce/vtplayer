cmake_minimum_required(VERSION 3.20)
project(vtplayer VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

option(VTPLAYER_BUILD_BUNDLE "Build with Graphics Support" OFF)

include(local.cmake OPTIONAL)

add_subdirectory(deps)
add_subdirectory(src)
