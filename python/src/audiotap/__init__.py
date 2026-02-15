"""audiotap — Python bindings for libaudiotap.

Capture system audio and microphone input on macOS.

    import audiotap

    def on_audio(samples: bytes, frame_count: int, channels: int, host_time: int):
        ...

    tap = audiotap.SystemTap(callback=on_audio, sample_rate=48000, channels=2)
    tap.start()
    ...
    tap.stop()
"""

from __future__ import annotations

import ctypes
import enum
from ctypes import POINTER, c_float, c_uint32, c_uint64, c_void_p
from typing import Callable, Sequence

from . import _bindings

# Module-level library handle, loaded lazily
_lib: ctypes.CDLL | None = None


def _get_lib() -> ctypes.CDLL:
    global _lib
    if _lib is None:
        _lib = _bindings.load()
    return _lib


# ---------------------------------------------------------------------------
# Public types
# ---------------------------------------------------------------------------

AudioCallback = Callable[[bytes, int, int, int], None]
"""Callback signature: (samples_bytes, frame_count, channels, host_time) -> None.

``samples_bytes`` contains interleaved 32-bit float PCM data.
"""


class Permission(enum.IntEnum):
    UNKNOWN = _bindings.PERMISSION_UNKNOWN
    GRANTED = _bindings.PERMISSION_GRANTED
    DENIED = _bindings.PERMISSION_DENIED


class AudioTapError(Exception):
    """Raised when a libaudiotap operation fails."""

    def __init__(self, status: int, message: str | None = None):
        self.status = status
        if message is None:
            raw = _get_lib().audiotap_error_string(status)
            message = raw.decode() if raw else f"OSStatus {status}"
        super().__init__(message)


# ---------------------------------------------------------------------------
# Permission helpers
# ---------------------------------------------------------------------------


def mic_permission_status() -> Permission:
    """Check current microphone permission (non-blocking)."""
    return Permission(_get_lib().audiotap_mic_permission_status())


def request_mic_permission() -> Permission:
    """Request microphone permission. Blocks until the user responds."""
    return Permission(_get_lib().audiotap_request_mic_permission())


def error_string(status: int) -> str:
    """Return a human-readable message for an OSStatus code."""
    raw = _get_lib().audiotap_error_string(status)
    return raw.decode() if raw else f"OSStatus {status}"


# ---------------------------------------------------------------------------
# Tap classes
# ---------------------------------------------------------------------------


class _BaseTap:
    """Common lifecycle for system and mic taps."""

    def __init__(self, callback: AudioCallback):
        self._handle: int | None = None  # c_void_p value
        self._user_callback = callback
        # prevent GC of the C callback trampoline
        self._c_callback = _bindings.AUDIOTAP_CALLBACK(self._trampoline)

    def _trampoline(
        self,
        samples: ctypes.Array,
        frame_count: int,
        channels: int,
        host_time: int,
        _userdata: int,
    ) -> None:
        nbytes = frame_count * channels * ctypes.sizeof(c_float)
        buf = ctypes.string_at(samples, nbytes)
        self._user_callback(buf, frame_count, channels, host_time)

    @property
    def running(self) -> bool:
        if self._handle is None:
            return False
        return bool(_get_lib().audiotap_is_running(self._handle))

    def start(self) -> None:
        """Start capturing audio. Raises AudioTapError on failure."""
        if self._handle is None:
            raise AudioTapError(0, "Tap not created or already destroyed")
        err = _get_lib().audiotap_start(self._handle)
        if err:
            raise AudioTapError(err)

    def stop(self) -> None:
        """Stop capturing (tap can be restarted)."""
        if self._handle is not None:
            _get_lib().audiotap_stop(self._handle)

    def destroy(self) -> None:
        """Stop and release all resources. The tap cannot be reused."""
        if self._handle is not None:
            _get_lib().audiotap_destroy(self._handle)
            self._handle = None

    def __del__(self) -> None:
        try:
            self.destroy()
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.destroy()


class SystemTap(_BaseTap):
    """Capture system audio output (requires macOS 14.2+).

    Parameters
    ----------
    callback:
        Called on a real-time thread with ``(samples_bytes, frame_count, channels, host_time)``.
    sample_rate:
        Desired sample rate in Hz (e.g. 48000).
    channels:
        1 for mono, 2 for stereo.
    pids:
        Process IDs to tap. ``None`` taps all system audio.
    mute:
        If ``True``, silence the tapped audio on speakers.
    """

    def __init__(
        self,
        callback: AudioCallback,
        sample_rate: float = 48000.0,
        channels: int = 2,
        *,
        pids: Sequence[int] | None = None,
        mute: bool = False,
    ):
        super().__init__(callback)
        lib = _get_lib()

        cfg = _bindings.SystemConfig()
        if pids is not None:
            pid_arr = (_bindings.c_pid_t * len(pids))(*pids)
            cfg.pids = ctypes.cast(pid_arr, POINTER(_bindings.c_pid_t))
            cfg.pid_count = len(pids)
            self._pid_arr = pid_arr  # prevent GC
        else:
            cfg.pids = None
            cfg.pid_count = 0
        cfg.sample_rate = sample_rate
        cfg.channels = channels
        cfg.mute = int(mute)
        cfg.callback = self._c_callback
        cfg.userdata = None

        self._cfg = cfg  # prevent GC
        self._handle = lib.audiotap_create_system(ctypes.byref(cfg))
        if not self._handle:
            raise AudioTapError(
                0, "Failed to create system tap (need macOS 14.2+ and audio permissions)"
            )


class MicTap(_BaseTap):
    """Capture microphone input from the default device.

    Parameters
    ----------
    callback:
        Called on a real-time thread with ``(samples_bytes, frame_count, channels, host_time)``.
    sample_rate:
        Desired sample rate in Hz (e.g. 48000).
    channels:
        1 for mono, 2 for stereo.
    """

    def __init__(
        self,
        callback: AudioCallback,
        sample_rate: float = 48000.0,
        channels: int = 1,
    ):
        super().__init__(callback)
        lib = _get_lib()

        cfg = _bindings.MicConfig()
        cfg.sample_rate = sample_rate
        cfg.channels = channels
        cfg.callback = self._c_callback
        cfg.userdata = None

        self._cfg = cfg  # prevent GC
        self._handle = lib.audiotap_create_mic(ctypes.byref(cfg))
        if not self._handle:
            raise AudioTapError(0, "Failed to create mic tap (no input device?)")


__all__ = [
    "AudioCallback",
    "AudioTapError",
    "MicTap",
    "Permission",
    "SystemTap",
    "error_string",
    "mic_permission_status",
    "request_mic_permission",
]
