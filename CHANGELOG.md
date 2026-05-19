# CHANGELOG

## 0.7.1 (2026-05-19)

- Bumped the pinned `ventty` dependency from `v0.2.0` to `v0.2.1`
  (`deps/CMakeLists.txt` `GIT_TAG`). The v0.7.0 bottle build failed
  because the new `ventty::BrailleCanvas` API used by the oscilloscope
  did not exist in the `v0.2.0` tarball that FetchContent / the Homebrew
  formula pulled; ventty was tagged `v0.2.1` with that API and the pin
  updated to match.

## 0.7.0 (2026-05-19)

- Oscilloscope visualizer now draws a continuous waveform with Braille
  sub-pixels (2x4 dots per cell) instead of one `•` per column: consecutive
  samples are joined, so fast/loud passages no longer leave vertical gaps
  (and a hollow centre). The per-cell braille packing was generalized into
  a reusable `ventty::BrailleCanvas` (ventty `art` module) with point and
  Bresenham-line plotting; the visualizer plots into it and blits. The
  trace is drawn in a brighter amber (the smaller dots dimmed the old theme
  color) and the centered axis line was removed.

- Fixed the Radio left-panel mode (key `5`) not persisting across restarts.
  `leftModeToConfig` already wrote `radio` to `[library] left_mode`, but the
  config parser only accepted `artist`/`album`/`directory`, so the value was
  rejected on load and silently fell back to `album`. The parser now also
  accepts `radio`.

- Internet-radio buffering reworked to eliminate frequent ~0.1 s dropouts.
  `StreamSource` now prebuffers before the first sample plays and re-arms
  that gate on an underrun (rebuffering cleanly instead of feeding a torn
  partial chunk); `buffering()` drives a `○ BUFFERING` transport indicator
  shown in place of `◉ LIVE` while the gate is held. The ring-buffer
  overflow policy changed from "drop the oldest samples to stay at the live
  edge" to reader backpressure: when the ring is full the reader parks until
  the consumer frees space, propagating flow control to `ffmpeg`. This keeps
  a deep, stable cushion (the previous policy discarded the whole cushion on
  any fast delivery, leaving playback one jitter spike away from an
  underrun) at the cost of added latency behind the live edge. Buffer depth
  and the prebuffer/rebuffer threshold are configurable via `[audio]`
  `stream_buffer_seconds` (default 20) and `stream_prebuffer_seconds`
  (default 5); the prebuffer is clamped below the buffer depth at runtime.

## 0.6.0 (2026-05-19)

- Internet radio. A new left-panel mode (key `5`, `LeftMode::Radio`) lists
  streams from `~/.config/vtplayer/streams.m3u` (Extended-M3U, seeded with a
  commented example on first run; URLs are kept verbatim, never
  path-normalized). Enter or double-click starts a stream. Playback is
  decoded by an external `ffmpeg` subprocess (`StreamSource`) that handles
  HTTP/HLS, playlist polling, token rotation, AAC/MP3/Opus and reconnection,
  emitting float32 PCM into a bounded ring buffer that drops the oldest
  samples on overflow to stay near the live edge. `AudioEngine::loadStream`
  drives it; if `ffmpeg` is not on PATH the load fails with an error instead
  of crashing. The transport bar shows a centered `◉ LIVE` indicator and
  elapsed-only time (no progress bar, no seeking) while a stream plays.
  Radio is independent of the media index — it works with an empty library,
  and the queue/library-root actions are suppressed on that panel.
- Config directory renamed from `~/.config/ventty-player` to
  `~/.config/vtplayer` (config.ini, library.db, playqueue.cache, streams.m3u
  all move). A one-time migration deletes the legacy `ventty-player`
  directory on first launch.
- Switching the left-panel axis (keys `1`-`4`) now lands on the matching
  artist/album group instead of always drilling to a track: Artist/Album
  mode stops at the group with ancestors expanded and the group left
  folded, while Directory mode still expands folders down to the file
  (`LibraryView::locateForMode`).
- Library scan is now two phases. Pass 1 walks the filesystem and collects
  the file list inline (no pre-count pass); it pumps input every 512 entries
  so ESC cancels, and shows a running "Collecting N" count. Pass 2 reads
  tags on a background thread, writing only the SQLite repository so the UI
  keeps using the pre-scan index; progress shows as a plain yellow `NN%` in
  the bottom-right corner that disappears at 100%. The full-screen modal and
  progress bar are gone. Pass 2 is not user-cancellable but stops promptly
  on exit (no hang while a scan is in flight). On startup the whole scan is
  skipped when the library root's signature (path + mtime, persisted as
  `[library] scan_sig`) is unchanged and the DB index is non-empty, so an
  unchanged library no longer re-walks the tree every launch; "Rescan
  library" and a root change always force a full scan. A path/dir CLI
  argument and a directory startup target are also supported.
- Suppressed TagLib's stderr warnings (e.g. "Invalid UTF16 string. BOM is
  broken." from legacy ID3v2 tags) via a no-op debug listener, so library
  scanning no longer corrupts the terminal UI.
- CI: release workflow `merge` job moved to a macOS runner.

## 0.5.0 (2026-05-18)

- New Vinyl/CD disc visualizer (VinylVis) registered on slot 5.
- `l` key toggles the left panel; PlayQueue uses the full width when it is hidden.
- Header bar now shows the player version.
- Packaging: added Homebrew formula/README and rewrote `release.yml` to automate prepare/bottle/merge on tag push.
- Build: new `VTPLAYER_USE_SYSTEM_DEPS` option to source TagLib/cxxopts/SQLite3 from system (Homebrew) packages.

## 0.4.0 (2026-05-15)

- ReplayGain normalization: read `REPLAYGAIN_TRACK_GAIN`/`PEAK` on load, falling back to RMS auto-gain when absent. Renamed `auto_gain` config and `AudioEngine` API to `gain_norm`; transport bar shows an RG/AG label.
- New TagInfoView (slot 4) with scroll via arrow keys / PgUp-Dn / Home-End / wheel.
- Added `--dump-tags` CLI flag to inspect a file's TagLib PropertyMap.
- Integrated TagLib via FetchContent; consolidated the ventty dependency under `deps/`.
- Split PlayQueue into a volatile container plus a standalone M3uReader.
- New MediaLibrary domain with extended TrackInfo metadata fields.
- Library index persisted in SQLite and scanned via TagLib; library actions exposed in the context menu.
- Connected the library to the play queue with session restore.
- New LibraryView panel with directory tree and ArtistAlbum group mode (`G` toggle).
- Modal search dialog with live filtering; "locate playing track" action and empty-library guidance.
- Unified the left panel into a single F1-F4 mode axis (Artist / Album / Directory / FileBrowser), persisted in `[library] left_mode`. Dropped `[ui] start_directory` and the Shift+Enter quiet-append feature; ESC menu is now built per mode.

## 0.3.0 (2026-05-14)

- CI: release workflow builds arm64_tahoe/sequoia bottles on tag push.
- Repeat now has three modes (none / all / one). `R` key cycles through them, and the transport bar shows the current mode on its left edge as `.` / `R` / `r`. The play/pause/stop glyph in that slot is removed — playback state is already conveyed by the time display on the right.
- Pressing `a` on a directory in the file browser now adds every audio file in that directory to the play queue (non-recursive).

## 0.2.0 (2026-05-12)

- Added `--version` CLI flag.
- Pinned ventty v0.2.0 and persisted last visualizer index across runs.
- Added WAV format support.
- New DebugBars visualizer on slot 3; moved gain/position metrics to bottom of layout.
- New Matrix rain visualizer with bass-reactive density and beat sync (with precomputed color LUT for empty-cell skipping).
- Reworked spectrum visualizer: row gradient, fade trail, and scaling fixes; prevented bars from saturating at full scale.
- Scrollable Help screen; dropped hint row and extended frame to bottom.
- Enter now replaces the playlist; Shift+Enter appends quietly. Added repeat/shuffle keys and a plain dir header.
- Decluttered track info display with CJK-safe truncation.
- Playlist multi-select with Backspace bulk-delete; playing-indicator polish.
- Korean filenames normalized to NFC for display.
- New oscilloscope visualizer with number-key switching.
- Multi-playlist support with M3U save/load.
- ESC context menu (volume control removed).
- Config persisted to disk.

## 0.1.0 (2026-03-31)

- Initial vtplayer release.
- CLI file argument for direct playback.
- Auto-gain and configurable visualizer; dropped game-music-emu backend.
- Fixed Tab key panel switching and shortcut keys during Korean IME composition.
- Fixed Tab switching when playlist is empty.
- README rewritten in English; added developer documentation.
