cmake_minimum_required(VERSION 3.20)
project(vtamp LANGUAGES CXX VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VTAMP_BUILD_BUNDLE "Build with Graphics Support" OFF)

include(local.cmake OPTIONAL)

# --- ventty dependency ---
include(FetchContent)
FetchContent_Declare(ventty
    GIT_REPOSITORY https://github.com/0xf07ce/ventty.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(VENTTY_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(VENTTY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VENTTY_BUILD_GFX      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ventty)

add_subdirectory(deps)
add_subdirectory(src)
