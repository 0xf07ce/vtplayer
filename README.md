# vtplayer

![vtplayer](docs/vtplayer.png)

<!-- ![version](https://img.shields.io/badge/version-VERSION-orange) -->
![build](https://github.com/0xf07ce/vtplayer/actions/workflows/build.yml/badge.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-brightgreen)
![license](https://img.shields.io/badge/license-LGPL--2.1-blue)

A terminal-based music player built on [ventty](https://github.com/0xf07ce/ventty).
Plays local files and internet radio streams, with a built-in media library
and tag editor.

## Features

- **Wide format support** — MP3, WAV, OGG, FLAC, M4A, MP4, AAC, Opus, WMA, WebM
  (decoded via ffmpeg/libav; miniaudio handles output only)
- **Internet radio streaming** — load `.pls` playlists and play HTTP/HLS
  streams with prebuffering and underrun recovery
- **Media library** — SQLite-backed index of a registered root directory with
  incremental, mtime-based rescans. Browse by Album, Artist, Directory, or
  the live filesystem; group tracks by a user-defined Grouping tag
- **Play queue** — session queue with shuffle, persisted across restarts
- **Tag editing** — sparse multi-track tag editor (Artist / Album /
  SingleTrack / MultiTrack scopes) via TagLib
- **Search** — live in-memory filter over the library (`/`) with result
  navigation
- **Six visualizers** — Tag Info, Audio Spectrum (FFT), Matrix Rain, Debug
  Bars, Oscilloscope (braille), Vinyl
- **Gain normalization** — ReplayGain tags or a runtime RMS estimate
  targeting –18 dBFS
- **Configurable** — themes (35+ color fields), keybindings, audio buffer
  sizes, and visualizer frame rate via `~/.config/vtplayer/config.ini`

## Installation

On macOS, install via Homebrew:

```bash
brew install 0xf07ce/tap/vtplayer
```

## Documentation

- [Building](docs/building.md)
- [Configuration](docs/configuration.md)
- [Keybindings](docs/keybindings.md)

## License

See [LICENSE](LICENSE).
