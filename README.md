A minimal C-compatible shared library (`libaudiotap.dylib`) that provides two independent audio capture streams on macOS:

1. **System audio capture** — tap outgoing audio from one or more processes (or all) using the Core Audio Taps API (macOS 14.2+)
2. **Microphone capture** — capture from the system default input device, automatically following device changes

Both streams deliver PCM float buffers via user-supplied callbacks. No Swift, no Objective-C — pure C against CoreAudio/AudioToolbox frameworks.
