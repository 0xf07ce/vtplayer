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
| `start_directory` | `~` | Starting directory for the file browser (`~` is expanded) |
| `show_hidden` | `false` | Show hidden files and directories |

### [visualizer]

| Key | Default | Description |
|-----|---------|-------------|
| `bar_count` | `48` | Number of spectrum bars (4-256) |

### [formats]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `mp3,ogg,flac` | Comma-separated list of recognized audio file extensions |

### [theme]

Color overrides in `#RRGGBB` hex format. See `src/view/Theme.h` for all available color fields.

## Example

```ini
[audio]
volume = 90

[ui]
start_directory = ~/Music
show_hidden = false

[visualizer]
bar_count = 64

[formats]
extensions = mp3,ogg,flac

[theme]
theme.primary = #61AFEF
theme.background = #1E1E1E
```
