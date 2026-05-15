# Configuration

vtplayer reads settings from `~/.config/ventty-player/config.ini`.

## Sections

### [audio]

| Key | Default | Description |
|-----|---------|-------------|
| `volume` | `100` | Initial volume (0-200, values above 100 apply soft-clipped amplification) |
| `auto_gain` | `false` | Runtime RMS-based loudness normalization toward -18 dBFS (range ±12 dB). Toggle live with `G`. |

### [ui]

| Key | Default | Description |
|-----|---------|-------------|
| `show_hidden` | `false` | Show hidden files and directories |

The file browser always opens at the directory the player was launched from.

### [visualizer]

| Key | Default | Description |
|-----|---------|-------------|
| `bar_count` | `48` | Number of spectrum bars (4-256) |

### [formats]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `mp3,wav,ogg,flac` | Comma-separated list of recognized audio file extensions |

### [library]

| Key | Default | Description |
|-----|---------|-------------|
| `root` | _(unset)_ | Library root directory (set via the ESC menu) |
| `left_mode` | `album` | Left-panel mode restored on launch: `artist`, `album`, or `directory`. The transient `filebrowser` mode (F4) is never persisted. |

### [theme]

Color overrides in `#RRGGBB` hex format. See `src/view/Theme.h` for all available color fields.

## Example

```ini
[audio]
volume = 90

[ui]
show_hidden = false

[visualizer]
bar_count = 64

[formats]
extensions = mp3,wav,ogg,flac

[library]
left_mode = album

[theme]
theme.primary = #61AFEF
theme.background = #1E1E1E
```
