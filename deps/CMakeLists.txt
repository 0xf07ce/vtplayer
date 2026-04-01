include(FetchContent)

# zlib — required by GME for VGZ (gzip-compressed VGM) support
find_package(ZLIB REQUIRED)

# game-music-emu for VGM/VGZ support (LGPL 2.1)
FetchContent_Declare(gme
    GIT_REPOSITORY https://github.com/libgme/game-music-emu.git
    GIT_TAG        0.6.3
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(gme)

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
    "${gme_SOURCE_DIR}"
)
target_link_libraries(vtplayerDep INTERFACE gme cxxopts)
