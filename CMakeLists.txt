cmake_minimum_required(VERSION 3.20)
project(vtplayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

option(VTPLAYER_BUILD_BUNDLE "Build with Graphics Support" OFF)

include(local.cmake OPTIONAL)

# --- ventty dependency ---
include(FetchContent)
FetchContent_Declare(ventty
    GIT_REPOSITORY https://github.com/0xf07ce/ventty.git
    GIT_TAG        v0.2.0   # 970e9d1e6ff7da225f2ea4ac9ef0aa999c8b0efa
    GIT_SHALLOW    TRUE
)
set(VENTTY_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(VENTTY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VENTTY_BUILD_GFX      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ventty)

add_subdirectory(deps)
add_subdirectory(src)
