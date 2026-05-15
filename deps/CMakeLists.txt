include(FetchContent)

# ventty: terminal UI framework
FetchContent_Declare(ventty
    GIT_REPOSITORY https://github.com/0xf07ce/ventty.git
    GIT_TAG        v0.2.0   # 970e9d1e6ff7da225f2ea4ac9ef0aa999c8b0efa
    GIT_SHALLOW    TRUE
)
set(VENTTY_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(VENTTY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VENTTY_BUILD_GFX      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ventty)

# cxxopts: header-only CLI option parser (MIT)
FetchContent_Declare(cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.2.1
    GIT_SHALLOW    TRUE
)
set(CXXOPTS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXOPTS_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cxxopts)

# TagLib: audio metadata library (dual-licensed LGPL-2.1 / MPL-1.1; used under MPL-1.1)
FetchContent_Declare(taglib
    GIT_REPOSITORY https://github.com/taglib/taglib.git
    GIT_TAG        v2.0.2
    GIT_SHALLOW    TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING     OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS    OFF CACHE BOOL "" FORCE)
set(VISIBILITY_HIDDEN ON  CACHE BOOL "" FORCE)
set(WITH_ZLIB         ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(taglib)

# TagLib only sets INSTALL_INTERFACE include dirs on its `tag` target; expose
# the source tree so `#include <taglib/...>` works when consumed via subdir.
# `toolkit` is needed because TagLib's public headers use unqualified
# `#include "tfile.h"` etc. for cross-references between subdirectories.
target_include_directories(tag INTERFACE
    $<BUILD_INTERFACE:${taglib_SOURCE_DIR}>
    $<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib>
    $<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/toolkit>
)

# miniaudio: header-only (public domain), placed in deps/include/miniaudio/miniaudio.h

# SQLite: system library. macOS provides libsqlite3 in the base SDK; Linux
# distros ship libsqlite3-dev. Avoids vendoring a multi-MB amalgamation.
find_package(SQLite3 REQUIRED)

add_library(vtplayerDep INTERFACE)
target_include_directories(vtplayerDep INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(vtplayerDep INTERFACE
    cxxopts
    tag
    SQLite3::SQLite3
)
