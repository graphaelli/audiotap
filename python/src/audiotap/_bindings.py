"""Low-level ctypes bindings to libaudiotap."""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import sys
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    c_float,
    c_int,
    c_uint32,
    c_uint64,
    c_void_p,
)
from pathlib import Path

# pid_t is an int32 on macOS/Linux
c_pid_t = c_int

# Callback: void (*)(const float *, uint32_t, uint32_t, uint64_t, void *)
AUDIOTAP_CALLBACK = CFUNCTYPE(None, POINTER(c_float), c_uint32, c_uint32, c_uint64, c_void_p)


class SystemConfig(Structure):
    _fields_ = [
        ("pids", POINTER(c_pid_t)),
        ("pid_count", c_uint32),
        ("sample_rate", c_float),
        ("channels", c_uint32),
        ("mute", c_int),
        ("callback", AUDIOTAP_CALLBACK),
        ("userdata", c_void_p),
    ]


class MicConfig(Structure):
    _fields_ = [
        ("sample_rate", c_float),
        ("channels", c_uint32),
        ("callback", AUDIOTAP_CALLBACK),
        ("userdata", c_void_p),
    ]


# Permission enum values
PERMISSION_UNKNOWN = 0
PERMISSION_GRANTED = 1
PERMISSION_DENIED = 2


def _find_library() -> str:
    """Locate libaudiotap.dylib using standard search order."""
    # 1. Explicit env var
    env = os.environ.get("LIBAUDIOTAP_PATH")
    if env:
        return env

    # 2. Bundled with the package (pip-installed wheel)
    bundled = Path(__file__).parent / "libaudiotap.dylib"
    if bundled.exists():
        return str(bundled)

    # 3. Adjacent to repo root (build/libaudiotap.dylib)
    repo_root = Path(__file__).resolve().parents[3]  # python/src/audiotap -> repo
    build_lib = repo_root / "build" / "libaudiotap.dylib"
    if build_lib.exists():
        return str(build_lib)

    # 4. System search via ctypes
    found = ctypes.util.find_library("audiotap")
    if found:
        return found

    raise OSError(
        "Cannot find libaudiotap.dylib. Either:\n"
        "  - pip install audiotap (includes the library)\n"
        "  - Set LIBAUDIOTAP_PATH to the full path\n"
        "  - Run `make` in the audiotap repo root so build/libaudiotap.dylib exists\n"
        "  - Install the library system-wide"
    )


def load(path: str | None = None) -> ctypes.CDLL:
    """Load and return the libaudiotap shared library with typed function signatures."""
    lib = ctypes.CDLL(path or _find_library())

    # --- System audio ---
    lib.audiotap_create_system.argtypes = [POINTER(SystemConfig)]
    lib.audiotap_create_system.restype = c_void_p

    # --- Microphone ---
    lib.audiotap_create_mic.argtypes = [POINTER(MicConfig)]
    lib.audiotap_create_mic.restype = c_void_p

    # --- Lifecycle ---
    lib.audiotap_start.argtypes = [c_void_p]
    lib.audiotap_start.restype = c_int

    lib.audiotap_stop.argtypes = [c_void_p]
    lib.audiotap_stop.restype = None

    lib.audiotap_destroy.argtypes = [c_void_p]
    lib.audiotap_destroy.restype = None

    # --- Permissions ---
    lib.audiotap_mic_permission_status.argtypes = []
    lib.audiotap_mic_permission_status.restype = c_int

    lib.audiotap_request_mic_permission.argtypes = []
    lib.audiotap_request_mic_permission.restype = c_int

    # --- Utility ---
    lib.audiotap_error_string.argtypes = [c_int]
    lib.audiotap_error_string.restype = ctypes.c_char_p

    lib.audiotap_is_running.argtypes = [c_void_p]
    lib.audiotap_is_running.restype = c_int

    return lib
