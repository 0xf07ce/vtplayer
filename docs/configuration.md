# Configuration

vtplayer reads settings from `~/.config/ventty-player/config.ini`.

## Sections

### [audio]

| Key | Default | Description |
|-----|---------|-------------|
| `volume` | `80` | Initial volume (0-100) |

### [ui]

| Key | Default | Description |
|-----|---------|-------------|
| `start_directory` | `~` | Starting directory for the file browser (`~` is expanded) |
| `show_hidden` | `false` | Show hidden files and directories |

### [visualizer]

| Key | Default | Description |
|-----|---------|-------------|
| `mode` | `spectrum` | Display mode: `spectrum`, `waveform`, or `both` |
| `bar_count` | `48` | Number of spectrum bars |

### [formats]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `mp3,ogg,flac,vgm,vgz,nsf,spc,gbs,gym,hes` | Comma-separated list of recognized audio file extensions |

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
mode = spectrum
bar_count = 64

[formats]
extensions = mp3,ogg,flac,vgm,vgz,nsf,spc

[theme]
theme.primary = #61AFEF
theme.background = #1E1E1E
```
