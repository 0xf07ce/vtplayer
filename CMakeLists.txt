cmake_minimum_required(VERSION 3.20)
project(vtplayer VERSION 1.1.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(local.cmake OPTIONAL)

option(VTPLAYER_BUILD_TESTS "Build unit tests" OFF)
option(BUILD_TESTING "Build unit tests" ${VTPLAYER_BUILD_TESTS})
if(VTPLAYER_BUILD_TESTS OR BUILD_TESTING)
    set(VTPLAYER_ENABLE_TESTS ON)
else()
    set(VTPLAYER_ENABLE_TESTS OFF)
endif()

add_subdirectory(deps)
add_subdirectory(src)

if(VTPLAYER_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
