# Keybindings

All shortcuts work regardless of Korean IME input state.

## Playback

| Key | Action |
|-----|--------|
| `Space` | Play / pause |
| `X` | Stop playback (hard stop — tears down the decoder/stream and clears the play marker) |
| `[` / `]` | Previous / next track |
| `<` / `>` | Seek -5s / +5s |
| `Left` / `Right` | Seek -5s / +5s (file browser focus excepted, where they collapse/expand) |
| `R` | Cycle repeat: none → all → one |
| `S` | Toggle shuffle (next/prev follow a random order) |
| `G` | Toggle gain normalization (ReplayGain ↔ auto-gain) |

## Browser — Library panel

| Key | Action |
|-----|--------|
| `1` / `2` / `3` / `4` | Left panel mode: Album / Artist / Directory / Files |
| `L` | Toggle left panel (play queue goes full-width when hidden) |
| `Tab` | Switch focus (left panel ↔ play queue) |
| `Left` / `Right` | Collapse / expand selected group |
| `Enter` | Replace play queue with artist/album/track and play |
| `A` | Append artist/album/track to play queue |
| `/` | Search library (Tab cycles filter: Any / Artist / Album / Title / Year) |
| `N` / `Shift+N` | Jump to next / previous search result |
| `T` | Edit tags (artist / album / track / folder, scoped to selection) |

## Browser — File panel

| Key | Action |
|-----|--------|
| `Up` / `Down` | Navigate entries |
| `PgUp` / `PgDn` | Page scroll |
| `Home` / `End` | Jump to first / last entry |
| `Enter` | Open directory, or replace play queue with selection and play |
| `A` | Add selected file (or every audio file in the selected dir, non-recursive) to play queue |
| `T` | Edit tags of selected audio file |
| `Backspace` | Go up to parent directory |
| `F5` | Refresh directory listing |

## Play queue

| Key | Action |
|-----|--------|
| `Enter` | Play selected track |
| `Del` / `D` / `Backspace` | Remove selection |
| `Ctrl+Up` / `Ctrl+Down` | Move selected track up / down |
| `Ctrl+A` | Select all |
| `T` | Edit tags (multi-selection if any, else cursor track) |

## Visualizer

| Key | Action |
|-----|--------|
| `V` | Toggle visualizer screen |
| `0` | Tag info |
| `1` | Spectrum analyzer |
| `2` | Matrix rain |
| `3` | Debug bars |
| `4` | Oscilloscope |
| `5` | Vinyl / CD disc |
| `Up` / `Down` / `PgUp` / `PgDn` / `Home` / `End` | Scroll within visualizers that support it (e.g. Tag info) |

## Tag editor (TagEditDialog)

| Key | Action |
|-----|--------|
| `Ctrl+E` | Switch to Edit mode |
| `Ctrl+S` | Confirm and save tags |
| `Esc` | Dismiss without saving |

## Misc

| Key | Action |
|-----|--------|
| `H` | Show / scroll this help |
| `Esc` | Open context menu (Focus playing track / Set library root / Rescan / Exit), or dismiss the current overlay |
| `Q` | Quit |

## Mouse

| Action | Effect |
|--------|--------|
| Click progress bar | Seek to position |
| Click browser / play queue | Switch focus |
| Scroll wheel | Navigate lists |
