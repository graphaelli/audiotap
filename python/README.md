# audiotap Python bindings

Python package for capturing system audio and microphone input on macOS, built on top of [libaudiotap](../README.md).

Audio is delivered as interleaved float32 PCM bytes via callbacks using ctypes to load `libaudiotap.dylib`.

## Requirements

- macOS 14.2+ (Sonoma)
- Python 3.10+
- [uv](https://docs.astral.sh/uv/)
- The C library must be built first (`make -C ..`)

## Building

```sh
# Build the C shared library (required)
make -C .. build/libaudiotap.dylib

# Install dependencies
make sync

# Run the capture_both example (default target)
make
```

## Usage

```python
import audiotap

def on_audio(samples: bytes, frame_count: int, channels: int, host_time: int):
    # samples is interleaved float32 PCM
    ...

# Capture from microphone
with audiotap.MicTap(callback=on_audio, sample_rate=48000, channels=1) as tap:
    tap.start()
    ...
```

System audio capture works the same way with `SystemTap`:

```python
tap = audiotap.SystemTap(
    callback=on_audio,
    sample_rate=48000,
    channels=2,
    pids=None,   # None = all processes
    mute=False,  # True to silence speakers
)
```

## API

| Type / Function | Description |
|---|---|
| `SystemTap(callback, sample_rate, channels, *, pids, mute)` | Capture system audio output |
| `MicTap(callback, sample_rate, channels)` | Capture microphone input |
| `.start()` | Begin capturing audio |
| `.stop()` | Pause capture without releasing resources |
| `.destroy()` | Stop and release all resources |
| `.running` | `True` if the tap is currently capturing |
| `request_mic_permission()` | Prompt for microphone permission (blocking) |
| `mic_permission_status()` | Check microphone permission (non-blocking) |
| `error_string(status)` | Human-readable message for an OSStatus code |

Both tap classes support context managers (`with`) and raise `AudioTapError` on failure.

## Examples

### capture_both

Records system audio (stereo) and microphone (mono) to raw PCM files at 48 kHz:

```sh
make capture_both
ffplay -f f32le -ar 48000 -ac 2 system.pcm
ffplay -f f32le -ar 48000 -ac 1 mic.pcm
```

## Architecture

```
src/audiotap/__init__.py   High-level API (SystemTap, MicTap, Permission)
src/audiotap/_bindings.py  Low-level ctypes bindings to libaudiotap.dylib
examples/capture_both.py   Audio capture to PCM files (port of C example)
tests/test_audiotap.py     High-level API tests (mocked)
tests/test_bindings.py     Bindings-layer tests
```

The library uses ctypes to load `libaudiotap.dylib` at runtime. Audio callbacks from the C library are bridged through a ctypes function pointer trampoline that converts the raw float pointer into Python `bytes` before delivering to the user callback.
