include(FetchContent)

# When ON, taglib / SQLite3 are taken from the system (e.g. Homebrew-provided
# bottles) via find_package instead of being git-cloned and built from source
# by FetchContent. This is the mode the Homebrew formula uses so
# `brew install --build-bottle` does not have to compile TagLib from source on
# every bottle. Default OFF keeps the plain `cmake -B build` developer workflow
# unchanged (no regression).
option(VTPLAYER_USE_SYSTEM_DEPS
    "Use system/Homebrew-provided deps (find_package) instead of FetchContent"
    OFF)

# ventty: terminal UI framework.
#
# ventty is always consumed via FetchContent because upstream (v0.3.1) ships
# no install()/export() rules, so there is no find_package(ventty) package to
# depend on. Offline / Homebrew builds stage the source and point CMake at it
# with -DFETCHCONTENT_SOURCE_DIR_VENTTY=<path>, which FetchContent honors
# transparently (no network during the build).
FetchContent_Declare(ventty
    GIT_REPOSITORY https://github.com/0xf07ce/ventty.git
    GIT_TAG        v0.3.1   # 2cad6bcc74c5be88ba92190e4f07ded5c7541269
    GIT_SHALLOW    TRUE
)
set(VENTTY_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(VENTTY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ventty)

if(VTPLAYER_USE_SYSTEM_DEPS)
    # TagLib: audio metadata library. TagLib 2.x ships a CMake package config
    # exposing the TagLib::tag target (Homebrew `taglib`). Using the prebuilt
    # bottle avoids the multi-minute TagLib source build per Homebrew bottle.
    find_package(taglib CONFIG REQUIRED)

    set(VTPLAYER_TAGLIB_TARGET  TagLib::tag)
else()
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

    set(VTPLAYER_TAGLIB_TARGET  tag)
endif()

# SQLite: system library. macOS provides libsqlite3 in the base SDK; Linux
# distros ship libsqlite3-dev. Avoids vendoring a multi-MB amalgamation.
find_package(SQLite3 REQUIRED)

# ffmpeg / libav: required system dependency. libavformat handles demuxing and
# network I/O (HTTP/HLS), libavcodec decodes every audio codec we care about
# (mp3/wav/ogg/flac/m4a/aac/opus/wma/…), libswresample converts arbitrary
# decoder output to our fixed f32/stereo/44.1 kHz pipeline. Install with
# `brew install ffmpeg` on macOS or
# `apt install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev`
# on Debian/Ubuntu.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswresample
)

# Vendored single-header libraries under deps/include/, exposed via the include
# directory below — no link target, no FetchContent, no system package:
#   - miniaudio  (public domain) — cross-platform audio *output* (ma_device).
#                Decoding is delegated to ffmpeg / libav above.
#   - cxxopts    (MIT)           — CLI option parser. One header, used only by
#                                  main.cpp; not worth a FetchContent download.
add_library(vtplayerDep INTERFACE)
target_include_directories(vtplayerDep INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(vtplayerDep INTERFACE
    ${VTPLAYER_TAGLIB_TARGET}
    SQLite3::SQLite3
    PkgConfig::FFMPEG
)
