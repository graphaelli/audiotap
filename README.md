# audiotap

A minimal C shared library (`libaudiotap.dylib`) for capturing audio on macOS. It provides two independent streams:

1. **System audio** — tap outgoing audio from specific processes or all system audio using the Core Audio Taps API (macOS 14.2+)
2. **Microphone** — capture from the default input device, automatically following device changes

Both streams deliver interleaved float PCM via callbacks on real-time audio threads.

## Requirements

- macOS 14.2+ (Sonoma) for system audio tapping
- Xcode command line tools (`clang`)
- Microphone permission for mic capture
- Screen recording / audio capture permission for system audio tapping

## Building

```
make            # build libaudiotap.dylib
make examples   # build example program
make test       # run all tests (87 tests)
make coverage   # run tests with line coverage report
make clean
```

## Usage

```c
#include "audiotap.h"

void my_callback(const float *samples, uint32_t frame_count,
                 uint32_t channels, uint64_t host_time, void *userdata) {
    // Called on a real-time audio thread - don't allocate, lock, or block.
    // Write samples to a file, ring buffer, etc.
}

// System audio: tap all processes, stereo
audiotap_system_config_t sys_cfg = {
    .pids = NULL, .pid_count = 0,  // NULL = all processes
    .sample_rate = 48000, .channels = 2,
    .mute = 0,                     // 1 to silence output speakers
    .callback = my_callback,
};
audiotap_t *sys = audiotap_create_system(&sys_cfg);
audiotap_start(sys);

// Microphone: mono (built-in mics are typically mono)
audiotap_permission_t perm = audiotap_request_mic_permission();
if (perm == AUDIOTAP_PERMISSION_GRANTED) {
    audiotap_mic_config_t mic_cfg = {
        .sample_rate = 48000, .channels = 1,
        .callback = my_callback,
    };
    audiotap_t *mic = audiotap_create_mic(&mic_cfg);
    audiotap_start(mic);
}

// ... later
audiotap_destroy(sys);
audiotap_destroy(mic);
```

The example program `capture_both` records both streams to raw PCM files:

```
./build/capture_both 5          # record for 5 seconds
ffplay -f f32le -ar 48000 -ac 2 system.pcm
ffplay -f f32le -ar 48000 -ac 1 mic.pcm
```

## Bindings

- **[Go](go/)** — `go get github.com/graphaelli/audiotap`
- **[Python](python/)** — `pip install audiotap`

## API

| Function | Description |
|---|---|
| `audiotap_create_system(config)` | Create a system audio tap (process tap + aggregate device) |
| `audiotap_create_mic(config)` | Create a mic tap on the default input device |
| `audiotap_start(tap)` | Start capturing (returns 0 or OSStatus error) |
| `audiotap_stop(tap)` | Stop capturing |
| `audiotap_destroy(tap)` | Stop + free all resources |
| `audiotap_mic_permission_status()` | Check mic permission (non-blocking) |
| `audiotap_request_mic_permission()` | Request mic permission (blocks for user response) |
| `audiotap_error_string(status)` | Human-readable string for an OSStatus error code |
| `audiotap_is_running(tap)` | Check if a tap is currently capturing |

## Architecture

```
include/audiotap.h          Public C API
src/audiotap_common.c       Lifecycle dispatch (start/stop/destroy routing)
src/audiotap_mic.c          Mic capture, device-change listener, channel conversion
src/audiotap_system.m       System tap (ObjC for CATapDescription)
src/audiotap_permission.m   AVCaptureDevice permission wrapper
src/audiotap_internal.h     Shared struct and internal declarations
```

### System audio capture

System audio capture uses three Core Audio primitives chained together:

1. **`AudioHardwareCreateProcessTap`** with a `CATapDescription` — creates a tap on one or more processes' audio output (or all processes for a global tap)
2. **`AudioHardwareCreateAggregateDevice`** — wraps the tap in a private aggregate device that can be read via an IO proc
3. **`AudioDeviceCreateIOProcID` + `AudioDeviceStart`** — registers a callback on the aggregate device to receive PCM buffers

The tap can optionally mute the audio so it's captured but not played through speakers.

### Microphone capture

Mic capture uses `AudioDeviceCreateIOProcID` on the default input device. It registers a property listener for `kAudioHardwarePropertyDefaultInputDevice` to automatically switch to a new device (e.g. plugging in a headset) without the caller needing to do anything.

The listener runs on a private serial dispatch queue (not the main queue) to avoid deadlocks with Core Audio's internal synchronization during `AudioDeviceStart`. All mic start/stop operations are serialized with a pthread mutex to prevent races between the device-change listener and the caller's thread.

Since most built-in macs have mono microphones, the IO proc handles channel conversion: if the caller requests stereo but the device delivers mono, each sample is duplicated into L+R. The reverse (stereo device, mono request) averages L+R.

### The AudioDeviceStart hang

The most interesting problem encountered during development: **`AudioDeviceStart` for a physical input device blocks indefinitely while an aggregate device is running.** This means if you start a system tap first and then try to start the mic, the mic start hangs forever.

This appears to be a Core Audio limitation. The workaround is to temporarily stop the aggregate device (system tap), start the mic, then restart the aggregate device. The library tracks the active system tap via a global pointer (`audiotap_active_system_tap`) so the mic start code can pause and resume it automatically. Callers don't need to worry about the ordering.

### Microphone permissions

`AudioDeviceStart` also blocks indefinitely when microphone permission hasn't been granted and the permission dialog can't be shown (common in CLI contexts). The library checks permission via `AVCaptureDevice` before attempting to start, returning `kAudioDevicePermissionsError` immediately instead of hanging.

## Testing

Tests use compile-time mocking: each test file `#include`s the source files directly and provides its own implementations of Core Audio functions. No frameworks are linked for the mocked functions, so tests run without audio hardware. 87 tests across three suites with 100% line coverage on all modules.

```
make coverage
```
