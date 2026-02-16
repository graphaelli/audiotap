# audiotap Go bindings

Go package for capturing system audio and microphone input on macOS, built on top of [libaudiotap](../README.md).

Audio is delivered as interleaved float32 PCM via a lock-free ring buffer, keeping the real-time Core Audio callback out of the Go runtime.

## Requirements

- macOS 14.2+ (Sonoma)
- Go 1.22+

## Building

CGO compiles the C/Objective-C sources automatically during `go build` — no manual `make` step is needed.

```sh
# Build the capture_both-go example
make

# Build and run the bridge tests
make test-bridge

# Run all Go tests
make test
```

## Usage

```go
import "github.com/graphaelli/audiotap/go"

// Capture from microphone
tap, err := audiotap.NewMicTap(audiotap.MicConfig{
    SampleRate: 16000,
    Channels:   1,
})
if err != nil {
    log.Fatal(err)
}
defer tap.Close()

if err := tap.Start(); err != nil {
    log.Fatal(err)
}

buf := make([]float32, 16000) // 1 second at 16 kHz mono
for {
    n, err := tap.Read(buf)
    if err != nil {
        break
    }
    // process buf[:n] ...
}
```

System audio capture works the same way with `NewSystemTap`:

```go
tap, err := audiotap.NewSystemTap(audiotap.SystemConfig{
    SampleRate: 48000,
    Channels:   2,
    PIDs:       nil,   // nil = all processes
    Mute:       false, // true to silence speakers
})
```

## API

| Type / Function | Description |
|---|---|
| `NewSystemTap(SystemConfig)` | Create a system audio tap |
| `NewMicTap(MicConfig)` | Create a microphone tap |
| `(*Tap).Start()` | Begin capturing audio |
| `(*Tap).Stop()` | Pause capture without releasing resources |
| `(*Tap).Read([]float32)` | Block until audio is available; returns `io.EOF` after Close |
| `(*Tap).Close()` | Stop capture and release all resources |
| `(*Tap).IsRunning()` | Check if the tap is currently capturing |
| `MicPermissionStatus()` | Check microphone permission (non-blocking) |
| `RequestMicPermission()` | Prompt for microphone permission (blocking) |

## Examples

### capture_both-go

Records system audio (stereo) and microphone (mono) to raw PCM files at 48 kHz:

```sh
make capture_both-go
../build/capture_both-go 5          # record for 5 seconds
ffplay -f f32le -ar 48000 -ac 2 system.pcm
ffplay -f f32le -ar 48000 -ac 1 mic.pcm
```

## Architecture

```
audiotap.go          Go bindings (CGO, compiles C sources directly)
audiotap_test.go     Go-level tests (ring buffer round-trip, lifecycle)
bridge.c / bridge.h  Lock-free SPSC ring buffer + self-pipe for blocking reads
cgo_audiotap_*.c/.m  Thin wrappers that #include ../src/* for CGO compilation
cmd/capture_both/    Audio capture to PCM files
tests/test_bridge.c  C-level bridge tests (29 tests, 100% coverage)
```

The bridge layer sits between Go and the C library. Core Audio delivers audio via a callback on a real-time thread — the bridge writes samples into a lock-free ring buffer and wakes the Go side through a self-pipe, so `(*Tap).Read` can block without involving the audio thread.
