"""Tests for the low-level ctypes bindings module."""

from __future__ import annotations

import ctypes
from unittest import mock

import pytest

from audiotap import _bindings


def _library_available() -> bool:
    """Check whether the real libaudiotap shared library can be found."""
    try:
        _bindings._find_library()
        return True
    except OSError:
        return False


def test_callback_type_is_callable():
    """AUDIOTAP_CALLBACK should be a ctypes function pointer type."""
    assert issubclass(_bindings.AUDIOTAP_CALLBACK, ctypes._CFuncPtr)


def test_system_config_fields():
    cfg = _bindings.SystemConfig()
    cfg.sample_rate = 48000.0
    cfg.channels = 2
    cfg.mute = 0
    cfg.pid_count = 0
    assert cfg.sample_rate == 48000.0
    assert cfg.channels == 2


def test_mic_config_fields():
    cfg = _bindings.MicConfig()
    cfg.sample_rate = 44100.0
    cfg.channels = 1
    assert cfg.sample_rate == 44100.0
    assert cfg.channels == 1


def test_permission_constants():
    assert _bindings.PERMISSION_UNKNOWN == 0
    assert _bindings.PERMISSION_GRANTED == 1
    assert _bindings.PERMISSION_DENIED == 2


def test_find_library_env_var(monkeypatch, tmp_path):
    lib_path = str(tmp_path / "libaudiotap.dylib")
    monkeypatch.setenv("LIBAUDIOTAP_PATH", lib_path)
    assert _bindings._find_library() == lib_path


def test_find_library_raises_when_not_found(monkeypatch):
    monkeypatch.delenv("LIBAUDIOTAP_PATH", raising=False)
    monkeypatch.setattr(_bindings.ctypes.util, "find_library", lambda _: None)
    # Patch Path.exists to return False for build dir
    with mock.patch("pathlib.Path.exists", return_value=False):
        with pytest.raises(OSError, match="Cannot find libaudiotap"):
            _bindings._find_library()


def test_load_sets_argtypes():
    """load() should set argtypes/restype on the CDLL handle."""
    fake_lib = mock.MagicMock()
    with mock.patch("audiotap._bindings._find_library", return_value="/fake/lib.dylib"):
        with mock.patch("ctypes.CDLL", return_value=fake_lib):
            lib = _bindings.load()

    assert lib is fake_lib
    # Verify key functions had their signatures set
    assert lib.audiotap_create_system.argtypes is not None
    assert lib.audiotap_create_mic.argtypes is not None
    assert lib.audiotap_start.argtypes is not None


# --- Integration: load the real shared library ---


@pytest.mark.skipif(not _library_available(), reason="libaudiotap not built")
def test_load_real_library():
    """Loading the real library should resolve all expected symbols."""
    lib = _bindings.load()

    # Every symbol declared in audiotap.h must be present.  ctypes raises
    # AttributeError from dlsym if a symbol is missing, so load() would
    # already have failed — but enumerate them explicitly so a future
    # addition to load() that isn't backed by C code is caught too.
    expected = [
        "audiotap_create_system",
        "audiotap_create_mic",
        "audiotap_start",
        "audiotap_stop",
        "audiotap_destroy",
        "audiotap_mic_permission_status",
        "audiotap_request_mic_permission",
        "audiotap_error_string",
        "audiotap_is_running",
    ]
    for name in expected:
        assert hasattr(lib, name), f"missing symbol: {name}"
