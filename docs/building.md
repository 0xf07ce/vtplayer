# Building vtplayer

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.20+
- zlib (system-installed)

## Dependencies

| Library | Version | Source | License |
|---------|---------|--------|---------|
| [ventty](https://github.com/0xf07ce/ventty) | latest | FetchContent | - |
| [game-music-emu](https://github.com/libgme/game-music-emu) | 0.6.3 | FetchContent | LGPL-2.1 |
| [miniaudio](https://miniaud.io) | latest | Vendored header (`deps/include/`) | Public Domain |
| zlib | system | System package | zlib |

ventty and game-music-emu are automatically fetched by CMake. No manual installation needed.

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
