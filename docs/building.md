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
| [cxxopts](https://github.com/jarro2783/cxxopts) | 3.2.1 | FetchContent | MIT |
| [TagLib](https://taglib.org) | 2.0.2 | FetchContent (or system via `VTPLAYER_USE_SYSTEM_DEPS`) | LGPL-2.1 / MPL-1.1 |
| [miniaudio](https://miniaud.io) | latest | Vendored header (`deps/include/`) | Public Domain |
| SQLite3 | system | `find_package` | Public Domain |

ventty, cxxopts, and TagLib are automatically fetched by CMake. ffmpeg, SQLite3,
and pkg-config must be installed on the system. miniaudio is used only for
audio output (`ma_device`); all decoding is delegated to libav so vtplayer
supports every codec ffmpeg supports (mp3, wav, ogg, flac, m4a, aac, opus, wma,
webm, …) and uses libavformat for HTTP/HLS internet-radio streaming.

## Build

### Standard (terminal backend)

```bash
cmake -B build && cmake --build build --config Release
```

### Bundle mode (SDL3 graphics backend)

```bash
cmake -B build -DVTPLAYER_BUILD_BUNDLE=ON && cmake --build build --config Release
```

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `VTPLAYER_BUILD_BUNDLE` | `OFF` | Enable SDL3 graphics backend instead of ANSI terminal |

## Output

The build produces a single `vtplayer` executable in the build directory.
