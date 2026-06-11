# Configuration

vtplayer reads settings from `~/.config/vtplayer/config.ini`.

## Sections

### [audio]

| Key | Default | Description |
|-----|---------|-------------|
| `gain_norm` | `false` | Loudness normalization. Uses `REPLAYGAIN_TRACK_GAIN` when present, otherwise runtime RMS toward -18 dBFS. Toggle live with `G`. |
| `stream_buffer_seconds` | `20` | Internet-radio buffer depth in seconds (clamped to 1–600). |
| `stream_prebuffer_seconds` | `5` | Initial/rebuffer threshold for internet radio streams in seconds (clamped to 0.5–600, and pinned below the buffer depth at runtime). |

### [ui]

| Key | Default | Description |
|-----|---------|-------------|
| `show_hidden` | `false` | Show hidden files and directories |

The file browser always opens at the directory the player was launched from.

### [visualizer]

| Key | Default | Description |
|-----|---------|-------------|
| `bar_count` | `24` | Number of spectrum bars (clamped to 4–256). |
| `index` | `1` | Visualizer selected on launch (0=Tag info, 1=Spectrum, 2=Matrix rain, 3=Debug bars, 4=Oscilloscope, 5=Vinyl). |
| `fps` | `30` | Visualizer screen animation rate. Snapped to the nearest of 15 / 30 / 60 on load. |

### [formats]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `mp3,wav,ogg,flac,m4a,mp4,aac,opus,wma,webm` | Comma-separated list of recognized audio file extensions. User values are merged with built-in defaults on load. |

### [library]

| Key | Default | Description |
|-----|---------|-------------|
| `root` | _(unset)_ | Library root directory (set via the ESC menu) |
| `left_mode` | `album` | Source panel mode restored on launch: `album`, `artist`, `directory`, or `playlists`. The transient file-browser mode (key `4`) is never persisted — it normalizes back to `album` on save. |
| `focus_path` | _(unset)_ | Last focused library track path, restored on the next launch. |
| `scan_sig` | _(unset)_ | Internal root signature used to skip unchanged startup scans. |

### [theme]

Color overrides in `#RRGGBB` hex format. See `src/view/Theme.h` for all available color fields.

## Example

```ini
[audio]
gain_norm = false
stream_buffer_seconds = 20
stream_prebuffer_seconds = 5

[ui]
show_hidden = false

[visualizer]
bar_count = 64
index = 1
fps = 30

[formats]
extensions = mp3,wav,ogg,flac,m4a,mp4,aac,opus,wma,webm

[library]
root = /Users/me/Music
left_mode = album
focus_path =
scan_sig =

[theme]
background = #1E1E1E
foreground = #D4D4D4
```

Keys under `[theme]` are the bare color-field names listed in `src/view/Theme.h` (e.g. `background`, `foreground`, `border`, `transport_progress_fg`, …), each set to a `#RRGGBB` hex value.
