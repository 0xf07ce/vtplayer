# Building vtplayer

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.20+
- `pkg-config` (used to locate ffmpeg)
- ffmpeg / libav (required system dependency — see below)

Install ffmpeg before configuring CMake:

```bash
# macOS
brew install ffmpeg pkg-config

# Debian / Ubuntu
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev pkg-config
```

## Dependencies

| Library | Version | Source | License |
|---------|---------|--------|---------|
| [ffmpeg / libav](https://ffmpeg.org) | system | `pkg-config` (libavformat, libavcodec, libavutil, libswresample) | LGPL-2.1+ |
| [ventty](https://github.com/0xf07ce/ventty) | latest | FetchContent | - |
| [TagLib](https://taglib.org) | 2.0.2 | FetchContent (or system via `VTPLAYER_USE_SYSTEM_DEPS`) | LGPL-2.1 / MPL-1.1 |
| [cxxopts](https://github.com/jarro2783/cxxopts) | 3.2.1 | Vendored header (`deps/include/cxxopts/`) | MIT |
| [miniaudio](https://miniaud.io) | latest | Vendored header (`deps/include/miniaudio/`) | Public Domain |
| SQLite3 | system | `find_package` | Public Domain |

ventty and TagLib are automatically fetched by CMake. ffmpeg, SQLite3, and
pkg-config must be installed on the system. cxxopts and miniaudio are vendored
as single headers under `deps/include/`. miniaudio is used only for audio
output (`ma_device`); all decoding is delegated to libav so vtplayer supports
every codec ffmpeg supports (mp3, wav, ogg, flac, m4a, aac, opus, wma, webm, …)
and uses libavformat for HTTP/HLS internet-radio streaming.

## Build

```bash
cmake -B build && cmake --build build --config Release
```

## Output

The build produces a single `vtplayer` executable in the build directory.
