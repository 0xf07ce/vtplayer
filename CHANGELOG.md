# CHANGELOG

## 0.4.0 (Unreleased)

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
