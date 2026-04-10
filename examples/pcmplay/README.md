# pcmplay

Plays raw PCM files in the format produced by audiotap's `capture_both` example,
using the same flags as `ffplay`.

## Requirements

- macOS (uses CoreAudio via cgo)
- Go 1.22+

## Build

```
make
```

## Usage

```
pcmplay -ar 48000 -ac 2 system.pcm
pcmplay -ar 48000 -ac 1 mic.pcm
```

| Flag     | Default | Description                          |
|----------|---------|--------------------------------------|
| `-ar`    | `48000` | Sample rate in Hz                    |
| `-ac`    | `2`     | Number of channels                   |
| `-noviz` | false   | Disable waveform visualization       |

Input is always interpreted as interleaved IEEE 754 float32, little-endian
(`f32le`) — the only format audiotap produces.

When stdout is a tty, pcmplay displays an ASCII waveform with a moving
playhead cursor. Use `-noviz` to suppress it.
