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

## Keybinding presets

Keybindings are chosen by *preset*. Two ship built-in, materialized on first
run into `~/.config/vtplayer/keybindings/` (and never rewritten afterwards, so
your edits and comments survive):

- **`default`** — the standard keys documented above, listed explicitly so each
  can be remapped. Every command is a single key — no modes, chords, or counts.
- **`vi`** — modal, vi-style navigation: motions, chord sequences, repeat
  counts, and a Visual mode.

The active preset is loaded once at startup and never switches at runtime, so a
default-mode session stays non-modal and a vi-mode session stays modal.

Select one in `~/.config/vtplayer/config.ini`:

```ini
[keybindings]
preset = vi        ; or "default"
```

You can add your own `<name>.keys` file in that directory and point `preset` at
it; the loader falls back to the built-ins if the named file is missing.

### vi preset

Keys not rebound below keep their built-in behavior, so playback, search (`/`,
`n`, `N`), append (`a`), tags (`t`), quit (`q`) and so on still work. Because
`h` / `l` / `v` become motions, their built-in commands move to the uppercase
keys: **`H`** = help, **`L`** = toggle left panel, **`V`** = visualizer.

| Key | Action |
|-----|--------|
| `h` `j` `k` `l` | Collapse / down / up / expand (`j`/`k` move the cursor) |
| `gg` / `G` | First / last item |
| `Ctrl+F` / `Ctrl+B` | Page down / up |
| `{count}` + motion | Repeat, e.g. `3j` moves down 3, `5dd` removes 5 |
| `dd` | Remove focused / selected item(s) from the play queue |
| `Ctrl+Up` / `Ctrl+Down` | Reorder selection up / down |
| `Ctrl+W` `w` | Cycle panel focus (like `Tab`) |
| `Ctrl+W` `h` / `Ctrl+W` `l` | Focus left panel / play queue |
| `Ctrl+W` `1`…`5` | Left-panel mode: Album / Artist / Directory / Files / Playlists |
| `Ctrl+G` | Toggle gain normalization (`g` is taken by `gg`) |
| `v` | Enter Visual mode |
| `Esc` | Cancel a pending count/chord, or leave Visual mode |

In **Visual mode**, `j`/`k` (and `Ctrl+F`/`Ctrl+B`) extend the selection and `d`
deletes it. A pending count or chord, and the Visual indicator, show in the
bottom-right corner (vim-style *showcmd*).

The preset file format is one directive per line:

```
modes  = normal, visual      # first mode is the initial one
counts = on                  # enable numeric repeat (3j, 5dd)
map <mode> <lhs> <action>    # e.g. map normal <C-w>l focus-right
```

`<lhs>` uses vim key notation (`j`, `dd`, `gg`, `<C-w>l`, `<CR>`, `<Esc>`,
`<Space>`, `<C-Up>`). See the comments in `vi.keys` for the full action list.
