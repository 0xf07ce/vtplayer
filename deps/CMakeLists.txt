include(FetchContent)

# cxxopts: header-only CLI option parser (MIT)
FetchContent_Declare(cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.2.1
    GIT_SHALLOW    TRUE
)
set(CXXOPTS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXOPTS_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cxxopts)

# miniaudio: header-only (public domain), placed in deps/include/miniaudio/miniaudio.h

add_library(vtplayerDep INTERFACE)
target_include_directories(vtplayerDep INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(vtplayerDep INTERFACE cxxopts)
