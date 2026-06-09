# CHANGELOG

## 1.0.0 (2026-06-09)

- **Configurable keybindings.** Keys are now driven by a preset file under
  `~/.config/vtplayer/keybindings/`, chosen in config.ini
  (`[keybindings] preset`). Two presets ship and are materialized on first run
  (and never overwritten afterward, so edits survive):
  - `default` — the standard layout, now listed explicitly so every command
    can be remapped. One key per command; no modes or counts.
  - `vi` — modal, vi-style navigation: `h/j/k/l` motions, `gg`/`G`, numeric
    repeat counts (`3j`, `5dd`), chord commands, `<C-w>` window/panel movement,
    and a Visual mode (`v`) that extends the selection (built on the existing
    multi-select). A vim-style *showcmd* hint shows a pending count/chord and
    the Visual indicator in the bottom-right.
  The active preset is loaded once at startup and never switches at runtime, so
  a default session stays non-modal and a vi session stays modal.
- The Help screen (`H`) is now generated from the active keymap, so it always
  reflects the real bindings and names the selected preset.
- The modal input engine (modes / counts / chord trie) lives in the ventty
  framework (≥ 0.4.1); vtplayer supplies the action vocabulary, the
  `dispatch()` that gives each action meaning, and the preset files.
- Requires ventty 0.4.1, whose raw mode now also clears `ISIG` — Ctrl+C /
  Ctrl+\ / Ctrl+Z arrive as ordinary keys instead of terminating the player.

## 0.17.2 (2026-06-09)

- Playlist contents view: Ctrl+E now only *enters* edit mode rather than
  toggling it — once editing, Ctrl+E is ignored. Leaving edit mode
  happens via Ctrl+S (save) or the ESC menu's new "Discard changes",
  which rolls back to the on-disk playlist by re-reading the file and
  dropping the unsaved reorder / trim. The menu lists "Save playlist"
  then "Discard changes" while editing.

## 0.17.1 (2026-06-09)

- Playlist contents view: multi-selection (Shift+Up / Shift+Down) and
  Ctrl+A select-all now work outside edit mode, so a selection can be
  sent straight to the play queue — Enter replaces the queue with the
  whole selection and plays, `a` appends it. Reorder / delete stay gated
  behind edit mode, where Backspace now removes the selection (an alias
  for `d`) instead of leaving the view.
- Ctrl+A no longer highlights the `..` back-row when the cursor sits on
  it: the cursor parks on the first selectable entry so `..` (and, in the
  FileBrowser, directories) are never part of a select-all.
- The track multi-selection is cleared when the panel loses focus,
  matching the FileBrowser / play-queue behavior.
- ESC menu is now focus-aware: the play queue shows its own items
  instead of borrowing the left panel's, the Playlists panel drops
  "Focus playing track", and the playlist contents view leads with an
  "Edit playlist" / "Save playlist" toggle mirroring Ctrl+E / Ctrl+S.
- `b` (add to playlist) stays available on the play queue even while the
  Playlists panel occupies the left side, and appending to a playlist
  that is currently open in the track view refreshes its list in place.

## 0.17.0 (2026-06-09)

- Play queue: Left / Right now mirror Up / Down for cursor movement,
  and Shift+Left / Shift+Right (like Ctrl+Up / Ctrl+Down) move the whole
  contiguous selection — multi-select block included — one row at a time,
  carrying the playing-track highlight with it. Home / End keep their
  jump-to-ends behavior.
- Playlist contents view is now editable behind a Ctrl+E edit mode
  (shown as `[edit]` in the header): Shift+Up / Shift+Down extend a track
  multi-selection, Shift+Left / Shift+Right reorder it, and `d` / `Del`
  remove it. Ctrl+S saves the reordered / trimmed list back to the
  `.m3u8` and leaves edit mode; exiting via `..` discards unsaved edits.
  Backspace keeps its "go up to the list" meaning. Track rows render as
  `<artist> - <title>` (just `<title>` when the artist is empty).
- `b` now adds the focused / selected track(s) to a playlist rather than
  always the playing track: the play-queue selection, a Library group's
  tracks (disabled at the top-level Grouping axis), or the FileBrowser
  selection (file or every audio file in a folder). It is disabled while
  the Playlists panel is focused, and falls back to the playing track on
  the Visualizer screen. The picker carries an "Add to playlist" title.
- Fixed an off-by-one in the modal menu's title rendering that hid the
  title whenever it was wider than the longest item.

## 0.16.2 (2026-06-06)

- Added a **Playlists** left-panel browser (mode `5`): playlists are
  individual `.m3u8` files under a fixed `~/.config/vtplayer/playlists/`
  directory. Create / rename / delete are driven from the ESC menu, with
  name-collision rejection and a yes/no confirm on delete.
- Enter on a playlist replaces the play queue with its tracks and starts
  playback; entries are re-resolved against the library for richer
  metadata (album / grouping / ReplayGain), falling back to the bare M3U
  parse for paths outside the library. An empty playlist clears the queue.
- The right-panel header now shows the active playlist's name while a
  playlist fills the queue, reverting to the default "Play Queue" on any
  queue edit (add / remove / reorder / replace). Session-only.
- `b` opens an "Add to Playlist" picker for the currently-playing track,
  appending it as the last entry of the chosen `.m3u8`. The list follows
  the mode-`5` order, except the playlist most recently used by the
  picker this session floats to the top. No-op when nothing is playing or
  no playlists exist.
- `x` is now a stop/play toggle: pressing it while stopped restarts
  playback from the current track, or the queue start (in shuffle mode,
  the first track of a freshly shuffled order).
- Frame pacing is now per-visualizer: each visualizer can cap its own
  fps, and fully static views (TagInfo) wait on input instead of waking
  periodically.
- Refactored the ESC context menu into a classify-then-build form
  (snapshot focus / left-slot / selection state, then emit the prepared
  menu). The FileBrowser context drops "Focus playing track" and leads
  with "Go to library root" (when a root is configured) followed by
  "Set current directory as library root".
- In the Album tree (mode `1`), album-artist nodes whose label was
  *derived* from the `artist` tag (because `albumArtist` was empty) now
  render a shade darker, distinguishing a real album-artist from an
  artist-only fallback. The album axis is keyed on `(album, derived)` so
  the two never merge under a shared name.

## 0.16.1 (2026-06-02)

- Renamed the internal missing-artist sentinel in the library tree from
  `#unknown_artist` to `#unattributed`, for naming consistency with the
  other `#`-prefixed sentinels (`#ungrouped`, `#unknown_album`,
  `#stream`). Pure internal sort-key rename — no display-label change.

## 0.16.0 (2026-06-02)

- Plugin ABI cleanup ahead of any external consumers: dropped the
  never-shipped Provider plugin kind, so `VtpPluginManifest` no longer
  carries a `kind` enum or `union iface` and exposes the decode backend
  directly as `const VtpInputPlugin *input`. Because that changed the
  manifest layout, `VTP_PLUGIN_ABI_VERSION` is bumped to **2**.
- Hardened `PluginHost` against stale/incompatible modules: the
  layout-stable `abi_version` gate is checked before any relocated
  field is read, and a manifest smaller than expected is rejected. An
  older or mismatched plugin is now skipped cleanly instead of being
  misread and crashing the host.
- Removed the `[formats] extensions` config knob. The set of
  libav-decoded container extensions is a property of the build, not
  user configuration, so it is now a fixed built-in list; plugin-claimed
  extensions are still merged in dynamically via `DecoderRegistry`.
- Added `docs/plugins.md`, a full plugin development guide (contract,
  threading, build flags, install, pitfalls), linked from the README.
- Extended the plugin tag ABI with an append-only `album_artist[256]`
  field on `VtpTagOut`, so plugin-handled tracks can populate the
  depth-1 axis in AlbumArtist mode. Append-only keeps the ABI at 2 — an
  older plugin built before the field leaves it zeroed (read as
  "unknown").
- `TagEditDialog` now opens read-only when every target is a
  plugin-handled file: such files expose `read_tags` but no writable
  tags, so the dialog locks to its inspector View (Ctrl+E disabled,
  title reads "Tags (read-only)") instead of letting an edit silently
  no-op on save.
- Renamed the library's synthetic group labels from parenthesized forms
  to `#`-prefixed sentinels (`#stream`, `#ungrouped`, `#unknown_artist`,
  `#unknown_album`) so they sort and read distinctly from real tag
  values, and added a depth-2 `#unknown_album` bucket for tracks missing
  an album.

## 0.15.0 (2026-06-01)

- Added a dynamic plugin system foundation: `PluginHost` discovers and
  loads C-ABI shared libraries from `~/.config/vtplayer/plugins`,
  validates their manifest/ABI, and wires them into the input pipeline.
  A vendored test harness exercises load/ABI-mismatch paths
  (`tests/test_plugin.cpp` with `dummy_input` / `bad_abi` fixtures).
- The Help screen gained a **Plugins** tab listing every loaded plugin
  (name + version); switch tabs with Tab / Left / Right.

## 0.14.0 (2026-05-30)

- Hardening for the 1.0 track: out-of-range `config.ini` values are
  now clamped on load — `stream_buffer_seconds` (1–600),
  `stream_prebuffer_seconds` (0.5–600) and `bar_count` (4–256) — so a
  stray or negative value can no longer size buffers to zero/gigabytes
  or blow up FFT binning. The fail-soft fallback to defaults is kept.
- The libav HTTP `user_agent` now derives from the build version
  instead of the stale hardcoded `vtplayer/0.8`.
- Added a vendored zero-dependency unit-test harness and 21 cases
  covering `M3uReader`, `PlsReader`, `UnicodeNormalize::toNfc`,
  `PlayQueue` and `Config` (including the new clamp behavior). Gated
  behind the `VTPLAYER_BUILD_TESTS` CMake option and wired into the
  macOS CI build via `ctest`.
- Docs: rewrote `docs/keybindings.md` against the actual input handler
  (the `1`–`4` panel modes, `0`–`5` visualizers, `R`/`S`/`G`, `T`,
  `N`/`Shift+N`, `L`, `/`, `X`, the ESC menu and TagEditDialog keys),
  corrected `docs/configuration.md` (defaults, clamp ranges, the `fps`
  key, a valid `[theme]` example) and fixed the in-app help row that
  swapped the Album / Artist panel labels.

## 0.13.3 (2026-05-28)

- Play queue rows now show the track artist between title and
  duration. The artist is right-aligned (its right edge sits one
  cell before the duration) and rendered in a new muted color
  (`play_queue_artist_fg`) so it reads as secondary metadata
  without competing with title or duration. When the title would
  overflow its space, the artist on that row is omitted so the
  title can use the full band.
- New shortcut `X` — hard-stop playback: tears down the decoder /
  stream and clears the `▶` marker on the play queue. Added to
  the in-app help screen and `docs/keybindings.md` (the stale
  `s = Stop` entry, which had been repurposed for shuffle, is
  corrected).

## 0.13.2 (2026-05-28)

- Transport keys remapped: `]` / `[` now drive next / previous
  track. The old `n` / `p` bindings are freed so `n` / `N` can be
  reused for vim-style search-result navigation (see below).
- `LibrarySearchDialog` gained a filter tab bar at the top of the
  dialog — `Any` / `Artist` / `Album` / `Title` / `Year`, cycled
  with `Tab` and `Shift+Tab`. `Any` matches every searchable field
  (the previous behavior); the others narrow to one. The selected
  filter persists across opens within a session.
- The dialog's match list survives a close, so `n` / `N` in the
  library panel step forward / backward through the last search's
  hits (vim-style) without reopening the dialog. The snapshot is
  invalidated whenever the library is rebuilt (rescan or root
  switch) so navigation never resolves a stale path. If `n` / `N`
  fires while the left panel is in FileBrowser, the app first
  switches into a library projection so the located track is
  actually visible.
- ESC menu reordered: `Focus playing track` is now the first
  entry, and it does the right thing for whichever panel has
  focus — on the play queue it scrolls the currently-playing
  track to the top of the visible area; on the library it locates
  the playing track in the tree (switching into the AlbumArtist
  slot if the left panel is currently in FileBrowser).
- Bumped vendored `ventty` to `v0.3.1`.

## 0.13.1 (2026-05-27)

- Idle-aware main loop. The run loop no longer spins at a fixed
  ~62 Hz: it now blocks on STDIN with a screen-appropriate timeout
  (60 / 30 / 15 FPS for the Visualizer, 250 ms while playing, 1 s
  while fully idle). Keystrokes wake the loop immediately, so input
  responsiveness is unchanged while idle CPU drops close to zero on
  the Browser and Help screens — including when the player is sitting
  in a background tmux pane or another desktop.
- `[visualizer] fps` (default 30, accepts 15 / 30 / 60) controls the
  Visualizer screen's animation rate. The value is snapped to the
  nearest supported tier on load so stray edits can't bake an
  arbitrary refresh rate into the loop.
- Spectrum visualizer (key `1`) no longer renders the gray trail
  layer behind dropped bars. Only the currently-lit portion of each
  bar is drawn; the empty cells above rely on the per-frame window
  clear, which removes a whole-grid trail pass from each frame.
- Matrix Rain (key `2`) trail length halved (`tail = 3..9` rows,
  was `6..18`) and shimmer probability lowered (`0.04`, was `0.10`).
  Steady-state lit-cell count and per-frame RNG passes both drop
  roughly in half, which translates directly to fewer `putChar`
  calls and aging-loop iterations.
- Lower default `[visualizer] bar_count` from 48 to 24.
- `LibrarySearchDialog` now builds a per-track pre-lowered, tab-joined
  haystack once when the dialog opens, and each subsequent keystroke
  does a single `find()` over those strings. Previous behavior
  re-lowered five fields per track on every keystroke, which made
  large libraries stutter on fast typing.

## 0.13.0 (2026-05-27)

- Shuffle mode (`S` on the play screen). Toggling it builds a randomized
  playback order pinned to the currently-playing track; next / prev /
  auto-advance walk that order instead of the queue's natural index
  sequence. The visible play queue order is never reshuffled — only the
  hidden walk changes — so the user's manual ordering is preserved.
  The shuffle order is stored as paths so queue inserts, removes, and
  reorders don't invalidate it. `TransportBar` shows an `s` glyph
  immediately to the left of the repeat indicator while shuffle is on
  (e.g. `sR` for shuffle + repeat-all). State is session-only and not
  persisted to config. The one-shot "shuffle now" action and its
  `PlayQueue::shuffle()` / `PlayQueueView::shuffle()` helpers are gone.
- `TagEditDialog` overhauled into three modes. Opening with `T` now
  lands in **View** (title "Tags", fields read-only); `Ctrl+E` switches
  to **Edit** (title "Edit Tags", fields accept input); `Ctrl+S` from
  Edit raises a **ConfirmSave** Yes/No overlay before anything is
  written. ESC in Edit reverts pending edits and drops back to View;
  a second ESC then closes. The `Scope` enum (Artist / Album /
  SingleTrack / MultiTrack) is gone — every field is shown regardless
  of selection, but multi-track edits stay sparse because only fields
  the user actually typed in are committed. View mode hides the
  cursor; only Edit parks one.

## 0.12.0 (2026-05-26)

- Left-panel modes 1 and 2 were redesigned. The labels swap (key 1 is now
  "Album", key 2 is now "Artist") and the two slots no longer share a
  tree: key 1 groups by `albumArtist` only and key 2 by `artist` only,
  with no fallback between them. A track tagged `albumArtist=VA,
  artist=거미` shows under "VA" in mode 1 and under "거미" in mode 2.
  Tag values are treated as opaque strings — "거미,휘성", "거미, 휘성",
  and "휘성,거미" produce three distinct groups, no comma-splitting or
  reordering. Both modes start fully collapsed; locate() still unfolds
  only the ancestors of the focused track. Group keys are NFC-normalized
  and ASCII-trimmed before grouping, so "015B" (NFD) and "015B"
  (NFC), or "015B" and "015B " (trailing space), fold into the same
  group instead of appearing twice.
- New top-level **Grouping** axis above artist. Both library tree modes
  are now four-level: Grouping → Artist → Album → Track. The depth-0
  key is the new `TrackInfo::grouping` field, populated from ID3v2 TIT1
  / Vorbis `GROUPING` / MP4 `©grp` (all normalized by TagLib to the
  PropertyMap key `GROUPING`). Empty grouping → `(ungrouped)`; other
  empty tag axes → `(null)`. A `library_grouping_fg` theme color
  carries the new top-level tint; `(ungrouped)` / `(null)` / `(stream)`
  reuse `library_null_fg` / `library_stream_fg`.
- Database schema bumped to `PRAGMA user_version = 1`. The migration
  adds a `grouping` column to the `tracks` table and resets every row's
  `mtime` to 0 so the next scan re-reads tags into the new column —
  existing libraries auto-populate without any user action beyond the
  next launch.
- Stream channels (PLS URL entries) now render as a flatter
  `(stream) → <pls stem> → channel` three-level subtree (the album axis
  is skipped, since PlsReader pins both artist and album to the pls
  stem and the duplicate `(stream)/(stream)/...` was redundant). The
  `(stream)` group is still pinned to the top of the Grouping axis.
- Depth-aware sort pinning. `(stream)` and `(ungrouped)` pin to the top
  of the Grouping axis; `(null)` and (in AlbumArtistTree only)
  `Various Artists` pin to the top of the Artist axis inside each
  grouping. Directory mode keeps pure alphabetical ordering.
- `TagEditDialog` gains a `Grouping` row in the multi-track and
  single-track scopes; `TagUpdate` / `TagWriter` write GROUPING
  end-to-end so the new field is editable as soon as the dialog's
  view-only flag is flipped. A new `SelectionKind::Grouping` lets the
  ESC menu / `T` shortcut scope a tag edit to every track under a
  Grouping node.
- `LeftMode` / `LibraryView::Mode` enums renamed to
  `AlbumArtistTree` / `ArtistTree` so the symbol name reflects the new
  semantics (the previous `Artist` / `Album` names referred to the old
  shared tree and would have lied to anyone reading the diff). Config
  string values ("album" / "artist") are preserved across the upgrade
  — users land in whichever UI label they last picked.

## 0.11.1 (2026-05-22)

- cxxopts is no longer fetched via `FetchContent` (or `find_package` under
  `VTPLAYER_USE_SYSTEM_DEPS`). It is now vendored as a single header at
  `deps/include/cxxopts/cxxopts.hpp`. The library is used only by `main.cpp`
  for argv parsing — pulling a git repo for one header per build added build
  time without any benefit. Homebrew formula no longer `depends_on "cxxopts"`.
- Removed the `VTPLAYER_BUILD_BUNDLE` CMake option and the `ventty_gfx`
  bundle backend code path in `Application`. SDL/graphics support was never
  shipped and ventty separated its SDL3 backend out, so the `#ifdef
  VTPLAYER_BUILD_BUNDLE` branches and the `VENTTY_BUILD_GFX OFF` force-set
  were dead weight. Bundle-mode build instructions removed from `building.md`.
- CLI argument errors no longer crash the player. Previously a bad flag
  (e.g. `--unknown`) escaped `cxxopts::Options::parse` as an uncaught
  exception, aborting before any message reached the user. The parse call
  is now wrapped: on error vtplayer prints `vtplayer: <reason>` followed by
  `--help` to stderr and exits with status 1.
- Validation errors are now visible. The unconditional `stderr → /dev/null`
  redirect introduced in v0.9.0 swallowed CLI errors and the `--dump-tags`
  "open failed" message. The redirect is now deferred until just before
  entering the TUI, so anything printed during argv parsing, version, help,
  or dump-tags lands on the user's terminal as expected. `--debug` continues
  to keep stderr inherited for the whole run.
- Terminal init failures are surfaced. `Application::initTerminal` returns
  early without assigning `_terminal` when `ventty::Terminal::init()` fails
  (no PTY, headless CI, etc.); `Application::run` previously dereferenced
  the null pointer immediately. It now shuts the audio engine down cleanly
  and returns exit code 1. `Application::quit` is null-safe for the same
  reason.
- `AudioEngine::play()` now returns `bool` and records `_lastError` on
  failure (device init / start / resume, or "no source loaded"). The UI no
  longer marks a track as "now playing" when the audio device refused to
  start — `Application::playTrackAt` only updates the play-queue index if
  `_audio.play()` actually succeeded.
- LibraryScanner ingest now runs even when the filesystem walk returned
  zero entries. Previously `collect()` producing an empty list short-
  circuited `startScan` before ingest, leaving the DB stale if every file
  under the library root had been moved out. The early-return now triggers
  only on an actual ESC cancel; an empty walk falls through to ingest,
  which sweeps the orphaned rows via its existing deletion pass.
- Dropped the `[audio] volume` config key. The runtime no longer exposes
  volume up/down keybindings (removed in v0.10) and `Config::volume` was a
  field with no UI affordance left to mutate it. The key is silently
  ignored on load and not re-emitted on save; `gain_norm` remains the
  knob users actually reach for.
- Docs: `docs/configuration.md` was several versions stale. Corrected the
  config path from `~/.config/ventty-player` to `~/.config/vtplayer`
  (renamed in v0.6.0); documented the previously-undocumented `gain_norm`,
  `stream_buffer_seconds`, `stream_prebuffer_seconds`, `visualizer.index`,
  `library.focus_path`, and `library.scan_sig` keys; updated the default
  `[formats] extensions` list to match the codec coverage added in v0.8.0
  and v0.8.1.

## 0.11.0 (2026-05-22)

**Breaking:** The `.stream` descriptor file format is gone. Internet radio
is now described by standard PLS (`.pls`) playlist files, which can hold
multiple stations per file. Existing `.stream` files placed in the library
root are no longer collected and their library rows are swept on the next
scan; replace them with one PLS per broadcaster / region.

Mapping: PLS *file* → album, PLS *channel* (a `FileN=` URL entry) → track.
A library row's `path` for a PLS-sourced channel is synthesized as
`<absolute_pls_path>#CH<N>`, which keeps each channel addressable as a
distinct primary key without colliding with the source file's path.
`streamUrl` carries the URL the AudioEngine opens.

- Added `src/util/PlsReader.{h,cpp}`. The parser accepts the `[playlist]`
  section case-insensitively, trusts the actual `FileN`/`TitleN`/`LengthN`
  triples it sees rather than `NumberOfEntries`, and handles both URL
  channels (any `scheme://…` value) and local-file channels (resolved
  relative to the `.pls` parent, like `M3uReader`). `album` on every
  returned `TrackInfo` is set to the `.pls` file's stem so channels of one
  file group naturally in LibraryView.
- FileBrowser: `.pls` joins `.m3u`/`.m3u8` as a playlist entry. Activating
  one appends every channel to the play queue (URL channels and local-file
  channels alike).
- LibraryScanner: `.pls` files inside the library root are always
  collected (mirroring the previous `.stream` behaviour) and expanded into
  N synthetic-path rows per file. Only URL channels are indexed — local
  file references in a library-root PLS are skipped to avoid clashing
  with the regular file scan's own row for the same path. The mtime
  fast-path naturally falls through for `.pls` (the source path is never
  itself a DB key), so each scan re-parses; PLS files are small enough
  that the cost is negligible, and any channel removed from a PLS gets
  swept on the next scan via the existing deletion-sweep mechanism.
- Removed: `src/util/StreamFile.{h,cpp}`, `extractStreamMetadata` in
  LibraryScanner, the `FileEntry::isStream` flag and all its branches in
  FileBrowser, the `.stream` resolution in
  `Application::trackInfoFromBrowserPath`, and the `.stream` entry in
  `TrackInfo::formatFromPath`. `AudioFormat::Stream`, `TrackInfo::streamUrl`,
  the `(stream)` virtual artist node in LibraryView, and the
  `stream_url` DB column are all retained — PLS URL channels rely on
  them.
- Tag viewing dialog: tag editing is **disabled** in this release. The
  dialog still opens from the play queue / library / file browser so you
  can read a track's existing tags, but its commit action no longer
  writes anything and the `Enter: save` hint has been removed from the
  footer (only `ESC: close` remains). The dialog code path and the
  underlying `TagWriter` plumbing are kept intact so editing can be
  re-enabled cleanly in a future release.

## 0.10.0 (2026-05-21)

**Breaking:** Internet radio has been folded into the media library. The
separate left-panel mode `5` (RadioView) and `~/.config/vtplayer/streams.m3u`
are gone, and the F5 shortcut ("Refresh listing") is removed. Left-panel
shortcuts are now `1`–`4` only (Artist / Album / Directory / FileBrowser).

Radio stations are now described by `.stream` descriptor files. Drop one
into the library root and `LibraryScanner` picks it up like any other
track:

```ini
[stream]
url = http://stream.example.com:8000/live
title = MBC FM4U
album = MBC
genre = Radio
year = 2026
```

Stream tracks always live under a virtual `(stream)` artist node in the
library tree — `artist` / `album_artist` keys in the descriptor are
ignored. The recommended mapping is `album` = broadcaster, `title` =
channel name.

- Added `AudioFormat::Stream` and `TrackInfo::streamUrl`. The SQLite
  `tracks` table gains a `stream_url` column; existing databases migrate
  automatically via `ALTER TABLE ... ADD COLUMN`.
- `AudioEngine` now exposes a unified `load(TrackInfo)` entry point; the
  old `loadStream(url, name)` is gone.
- FileBrowser can open `.stream` files directly — even outside the
  library root, in which case they play but are not indexed.
- `[library] left_mode = "radio"` is gracefully remapped to `"album"`
  on load. Existing `streams.m3u` entries are not migrated automatically;
  rewrite them as `.stream` files.
- Removed `src/view/RadioView.{h,cpp}` and `src/util/StreamList.{h,cpp}`;
  added `src/util/StreamFile.{h,cpp}`.
- TransportBar: the `LIVE` / `BUFFERING` label is now centred on the
  box's bottom border for live streams. Previously only the left side
  was padded, leaving the border showing past the label on the right
  and looking asymmetric.

## 0.9.0 (2026-05-21)

- Radio streams again leaked libav diagnostics onto the TUI
  (`http @0x… Error reading HTTP response: End of file` and friends).
  v0.7.2's fix — redirecting the spawned `ffmpeg` child's stderr to
  `/dev/null` — became a no-op in v0.8.0 once decoding moved in-process,
  and lowering libav's log level was insufficient because the offending
  messages are emitted at `AV_LOG_ERROR` and survive that threshold.
  Without `--debug`, vtplayer now redirects its own `stderr` to
  `/dev/null` before entering the TUI, which silences libav as well as
  any other component that might write to `stderr`. `--debug` keeps
  `stderr` inherited and continues to raise libav's verbosity.

## 0.8.1 (2026-05-20)

- `.mp4` files (MPEG-4 containers carrying an audio track) now appear in the
  file browser and are picked up by the library scanner. The decoder backend
  (libav) already handled them, but `mp4` was missing from the
  `[formats] extensions` default list and from `TrackInfo::formatFromPath`,
  so the FileBrowser filter and `LibraryScanner::collect` silently skipped
  every `*.mp4`. Added `AudioFormat::Mp4`, included `mp4` in the default
  extensions string, and updated the `--help` banner.
- Config: extensions saved by an older release are now merged with the
  built-in default list on load rather than replacing it verbatim. v0.8.0
  preserved the user's existing `extensions =` value to avoid clobbering
  custom edits, but the trade-off was that anyone who had ever saved a
  config before a new format was added would never see that format until
  they hand-edited the file. The loader now unions the two lists (user
  order first, then any built-in defaults the user didn't have), and
  re-serialises the merged value on next save. Custom user-added
  extensions remain intact.

## 0.8.0 (2026-05-20)

- Unified the audio-decoding backend on ffmpeg / libav. The previous split —
  miniaudio's `ma_decoder` for local files and an external `ffmpeg`
  subprocess for internet-radio streams — has been replaced by a single
  `vtplayer::Decoder` class built on `libavformat` + `libavcodec` +
  `libswresample`. miniaudio is now used only for cross-platform audio
  *output* (`ma_device`), giving a clean responsibility split: libav decodes
  every codec ffmpeg supports and handles network I/O (HTTP/HLS); miniaudio
  drives the output device. As a direct consequence, m4a / aac / opus / wma /
  webm files are now playable alongside the original mp3 / wav / ogg / flac
  set; the `[formats] extensions` default and the library scanner accept the
  expanded list out of the box (existing user `config.ini` values are
  preserved). `--debug` now raises libav's log level instead of just keeping
  the spawned `ffmpeg` child's stderr inherited.
- `StreamSource` rewritten to call `vtplayer::Decoder` directly instead of
  spawning an `ffmpeg` child and piping raw PCM through a UNIX pipe. The
  prebuffer / backpressure / ring-buffer machinery — and the `LIVE` /
  `BUFFERING` transport indicators wired from `buffering()` — are kept
  unchanged; `posix_spawn`, the PATH lookup for `ffmpeg`, the stderr
  `/dev/null` redirect, and the SIGKILL/`waitpid` cleanup are all gone. HTTP
  reconnect / `user_agent` / connect-timeout options are now passed as
  `AVDictionary` entries to `avformat_open_input`.
- Build: `ffmpeg` is now a required system dependency (in addition to
  `pkg-config`). CMake finds it via `pkg_check_modules(FFMPEG REQUIRED
  IMPORTED_TARGET libavformat libavcodec libavutil libswresample)`; the
  Homebrew formula adds `depends_on "ffmpeg"` and `depends_on "pkg-config"`.
- Fixed an intermittent segfault when switching from a playing radio stream
  to a local file. `StreamSource::stop()` previously called
  `_decoder->close()` *before* joining the reader thread, freeing libav
  contexts the reader could still be using inside a blocking network read.
  `Decoder` now exposes `setInterrupt(atomic<bool> const*)` which installs
  an `AVIOInterruptCB` on the format context, and `StreamSource` arms it
  against its stop flag so libav returns promptly when stop is requested;
  the decoder is only torn down after the reader has joined.
- Oscilloscope visualizer gain re-tuned from `2.0` to `1.0` (unity). The
  Braille-canvas amplification needed for the miniaudio sample stream
  clipped too often once decoding moved to libav, which delivers full-scale
  peaks more faithfully.

## 0.7.2 (2026-05-19)

- Internet-radio streams no longer corrupt the TUI with ffmpeg's transient
  HTTP diagnostics. The `ffmpeg` subprocess spawned by `StreamSource` only
  redirected its stdout to the sample pipe; its stderr stayed inherited from
  the parent, so libavformat messages such as `Error reading HTTP response:
  End of file` — which are expected for live radio (the server recycles the
  connection and `-reconnect` immediately re-establishes it) — printed
  straight onto the terminal UI. By default the child's stderr is now
  redirected to `/dev/null`. A new `--debug` flag keeps stderr inherited so
  the ffmpeg diagnostics remain visible for troubleshooting; the flag is
  plumbed `main` → `Application` → `AudioEngine` → `StreamSource`.

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
- `l` key toggles the left panel; PlayQueue uses the full width when it
  is hidden.
- Header bar now shows the player version.
- Packaging: added Homebrew formula/README and rewrote `release.yml` to
  automate prepare/bottle/merge on tag push.
- Build: new `VTPLAYER_USE_SYSTEM_DEPS` option to source
  TagLib/cxxopts/SQLite3 from system (Homebrew) packages.

## 0.4.0 (2026-05-15)

- ReplayGain normalization: read `REPLAYGAIN_TRACK_GAIN`/`PEAK` on load,
  falling back to RMS auto-gain when absent. Renamed `auto_gain` config
  and `AudioEngine` API to `gain_norm`; transport bar shows an RG/AG
  label.
- New TagInfoView (slot 4) with scroll via arrow keys / PgUp-Dn /
  Home-End / wheel.
- Added `--dump-tags` CLI flag to inspect a file's TagLib PropertyMap.
- Integrated TagLib via FetchContent; consolidated the ventty dependency
  under `deps/`.
- Split PlayQueue into a volatile container plus a standalone M3uReader.
- New MediaLibrary domain with extended TrackInfo metadata fields.
- Library index persisted in SQLite and scanned via TagLib; library
  actions exposed in the context menu.
- Connected the library to the play queue with session restore.
- New LibraryView panel with directory tree and ArtistAlbum group mode
  (`G` toggle).
- Modal search dialog with live filtering; "locate playing track" action
  and empty-library guidance.
- Unified the left panel into a single F1-F4 mode axis (Artist / Album /
  Directory / FileBrowser), persisted in `[library] left_mode`. Dropped
  `[ui] start_directory` and the Shift+Enter quiet-append feature; ESC
  menu is now built per mode.

## 0.3.0 (2026-05-14)

- CI: release workflow builds arm64_tahoe/sequoia bottles on tag push.
- Repeat now has three modes (none / all / one). `R` key cycles through
  them, and the transport bar shows the current mode on its left edge as
  `.` / `R` / `r`. The play/pause/stop glyph in that slot is removed —
  playback state is already conveyed by the time display on the right.
- Pressing `a` on a directory in the file browser now adds every audio
  file in that directory to the play queue (non-recursive).

## 0.2.0 (2026-05-12)

- Added `--version` CLI flag.
- Pinned ventty v0.2.0 and persisted last visualizer index across runs.
- Added WAV format support.
- New DebugBars visualizer on slot 3; moved gain/position metrics to
  bottom of layout.
- New Matrix rain visualizer with bass-reactive density and beat sync
  (with precomputed color LUT for empty-cell skipping).
- Reworked spectrum visualizer: row gradient, fade trail, and scaling
  fixes; prevented bars from saturating at full scale.
- Scrollable Help screen; dropped hint row and extended frame to bottom.
- Enter now replaces the playlist; Shift+Enter appends quietly. Added
  repeat/shuffle keys and a plain dir header.
- Decluttered track info display with CJK-safe truncation.
- Playlist multi-select with Backspace bulk-delete; playing-indicator
  polish.
- Korean filenames normalized to NFC for display.
- New oscilloscope visualizer with number-key switching.
- Multi-playlist support with M3U save/load.
- ESC context menu (volume control removed).
- Config persisted to disk.

## 0.1.0 (2026-03-31)

- Initial vtplayer release.
- CLI file argument for direct playback.
- Auto-gain and configurable visualizer; dropped game-music-emu backend.
- Fixed Tab key panel switching and shortcut keys during Korean IME
  composition.
- Fixed Tab switching when playlist is empty.
- README rewritten in English; added developer documentation.
