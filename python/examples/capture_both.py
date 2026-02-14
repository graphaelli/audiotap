#!/usr/bin/env python3
"""Capture system audio and microphone simultaneously — Python port of capture_both.c.

Writes raw float32 PCM to system.pcm and mic.pcm.

Usage:
    uv run examples/capture_both.py [duration_seconds]
"""

from __future__ import annotations

import signal
import sys
import time

import audiotap

SAMPLE_RATE = 48000.0
SYSTEM_CHANNELS = 2
MIC_CHANNELS = 1


def main() -> int:
    duration = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    if duration <= 0:
        duration = 10

    sys_file = open("system.pcm", "wb")
    mic_file = open("mic.pcm", "wb")

    def system_callback(samples: bytes, frame_count: int, channels: int, host_time: int) -> None:
        sys_file.write(samples)

    def mic_callback(samples: bytes, frame_count: int, channels: int, host_time: int) -> None:
        mic_file.write(samples)

    # Request mic permission (blocks for user dialog)
    print("Requesting microphone permission...")
    perm = audiotap.request_mic_permission()
    if perm != audiotap.Permission.GRANTED:
        print(
            f"Microphone permission not granted (status={perm.name}).\n"
            "Grant access in System Settings > Privacy & Security > Microphone.",
            file=sys.stderr,
        )

    # Create taps
    sys_tap = None
    mic_tap = None

    print("Creating system audio tap...")
    try:
        sys_tap = audiotap.SystemTap(
            callback=system_callback,
            sample_rate=SAMPLE_RATE,
            channels=SYSTEM_CHANNELS,
        )
    except audiotap.AudioTapError as e:
        print(f"Failed to create system tap: {e}", file=sys.stderr)

    print("Creating microphone tap...")
    try:
        mic_tap = audiotap.MicTap(
            callback=mic_callback,
            sample_rate=SAMPLE_RATE,
            channels=MIC_CHANNELS,
        )
    except audiotap.AudioTapError as e:
        print(f"Failed to create mic tap: {e}", file=sys.stderr)

    if sys_tap is None and mic_tap is None:
        print("No audio sources available. Exiting.", file=sys.stderr)
        sys_file.close()
        mic_file.close()
        return 1

    # Start capturing
    if sys_tap is not None:
        try:
            sys_tap.start()
            print("System audio capture started.")
        except audiotap.AudioTapError as e:
            print(f"System tap start error: {e}", file=sys.stderr)

    if mic_tap is not None:
        try:
            mic_tap.start()
            print("Microphone capture started.")
        except audiotap.AudioTapError as e:
            print(f"Mic tap start error: {e}", file=sys.stderr)

    # Wait for duration or Ctrl+C
    running = True

    def stop_handler(sig, frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop_handler)
    signal.signal(signal.SIGTERM, stop_handler)

    print(f"Recording for {duration} seconds (Ctrl+C to stop early)...")
    elapsed = 0
    while elapsed < duration and running:
        time.sleep(1)
        elapsed += 1

    print("\nStopping...")

    if sys_tap is not None:
        sys_tap.destroy()
    if mic_tap is not None:
        mic_tap.destroy()

    sys_file.close()
    mic_file.close()

    print("Done. Playback with:")
    print(f"  ffplay -f f32le -ar {SAMPLE_RATE:.0f} -ac {SYSTEM_CHANNELS} system.pcm")
    print(f"  ffplay -f f32le -ar {SAMPLE_RATE:.0f} -ac {MIC_CHANNELS} mic.pcm")

    return 0


if __name__ == "__main__":
    sys.exit(main())
