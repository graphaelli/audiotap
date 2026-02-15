"""Tests for the high-level audiotap API."""

from __future__ import annotations

import ctypes
from unittest import mock

import pytest

import audiotap
from audiotap import _bindings


@pytest.fixture(autouse=True)
def _reset_lib():
    """Reset the module-level library handle between tests."""
    old = audiotap._lib
    audiotap._lib = None
    yield
    audiotap._lib = old


def _make_fake_lib():
    """Create a mock that behaves like the loaded CDLL."""
    lib = mock.MagicMock()
    lib.audiotap_create_system.return_value = 0xDEAD
    lib.audiotap_create_mic.return_value = 0xBEEF
    lib.audiotap_start.return_value = 0
    lib.audiotap_is_running.return_value = 1
    lib.audiotap_error_string.return_value = b"no error"
    lib.audiotap_mic_permission_status.return_value = _bindings.PERMISSION_GRANTED
    lib.audiotap_request_mic_permission.return_value = _bindings.PERMISSION_GRANTED
    return lib


@pytest.fixture
def fake_lib():
    lib = _make_fake_lib()
    with mock.patch.object(audiotap, "_lib", lib):
        # Also ensure _get_lib returns our mock
        with mock.patch.object(audiotap, "_get_lib", return_value=lib):
            yield lib


# --- Permission tests ---


def test_permission_enum():
    assert audiotap.Permission.UNKNOWN == 0
    assert audiotap.Permission.GRANTED == 1
    assert audiotap.Permission.DENIED == 2


def test_mic_permission_status(fake_lib):
    result = audiotap.mic_permission_status()
    assert result == audiotap.Permission.GRANTED
    fake_lib.audiotap_mic_permission_status.assert_called_once()


def test_request_mic_permission(fake_lib):
    result = audiotap.request_mic_permission()
    assert result == audiotap.Permission.GRANTED
    fake_lib.audiotap_request_mic_permission.assert_called_once()


# --- error_string ---


def test_error_string(fake_lib):
    fake_lib.audiotap_error_string.return_value = b"bad format"
    assert audiotap.error_string(42) == "bad format"


def test_error_string_none(fake_lib):
    fake_lib.audiotap_error_string.return_value = None
    assert audiotap.error_string(99) == "OSStatus 99"


# --- SystemTap ---


def test_system_tap_create(fake_lib):
    cb = mock.Mock()
    tap = audiotap.SystemTap(callback=cb, sample_rate=44100, channels=1)
    assert tap._handle == 0xDEAD
    fake_lib.audiotap_create_system.assert_called_once()
    tap.destroy()


def test_system_tap_create_failure(fake_lib):
    fake_lib.audiotap_create_system.return_value = 0
    with pytest.raises(audiotap.AudioTapError, match="Failed to create system tap"):
        audiotap.SystemTap(callback=mock.Mock())


def test_system_tap_with_pids(fake_lib):
    tap = audiotap.SystemTap(callback=mock.Mock(), pids=[123, 456])
    assert tap._handle == 0xDEAD
    tap.destroy()


def test_system_tap_mute(fake_lib):
    tap = audiotap.SystemTap(callback=mock.Mock(), mute=True)
    assert tap._handle == 0xDEAD
    tap.destroy()


def test_system_tap_start_stop(fake_lib):
    tap = audiotap.SystemTap(callback=mock.Mock())
    tap.start()
    fake_lib.audiotap_start.assert_called_once_with(0xDEAD)
    assert tap.running
    tap.stop()
    fake_lib.audiotap_stop.assert_called_once_with(0xDEAD)
    tap.destroy()


def test_system_tap_start_error(fake_lib):
    fake_lib.audiotap_start.return_value = -50
    tap = audiotap.SystemTap(callback=mock.Mock())
    with pytest.raises(audiotap.AudioTapError) as exc_info:
        tap.start()
    assert exc_info.value.status == -50
    tap.destroy()


def test_system_tap_context_manager(fake_lib):
    with audiotap.SystemTap(callback=mock.Mock()) as tap:
        tap.start()
    fake_lib.audiotap_destroy.assert_called_once_with(0xDEAD)


# --- MicTap ---


def test_mic_tap_create(fake_lib):
    tap = audiotap.MicTap(callback=mock.Mock(), sample_rate=48000, channels=1)
    assert tap._handle == 0xBEEF
    tap.destroy()


def test_mic_tap_create_failure(fake_lib):
    fake_lib.audiotap_create_mic.return_value = 0
    with pytest.raises(audiotap.AudioTapError, match="Failed to create mic tap"):
        audiotap.MicTap(callback=mock.Mock())


def test_mic_tap_start_stop(fake_lib):
    tap = audiotap.MicTap(callback=mock.Mock())
    tap.start()
    fake_lib.audiotap_start.assert_called_once_with(0xBEEF)
    tap.stop()
    fake_lib.audiotap_stop.assert_called_once_with(0xBEEF)
    tap.destroy()


# --- _BaseTap edge cases ---


def test_start_after_destroy(fake_lib):
    tap = audiotap.SystemTap(callback=mock.Mock())
    tap.destroy()
    with pytest.raises(audiotap.AudioTapError, match="not created or already destroyed"):
        tap.start()


def test_stop_after_destroy(fake_lib):
    tap = audiotap.SystemTap(callback=mock.Mock())
    tap.destroy()
    # stop() after destroy is a no-op, should not raise
    tap.stop()


def test_running_after_destroy(fake_lib):
    tap = audiotap.SystemTap(callback=mock.Mock())
    tap.destroy()
    assert not tap.running


def test_double_destroy(fake_lib):
    tap = audiotap.SystemTap(callback=mock.Mock())
    tap.destroy()
    tap.destroy()  # should not raise


def test_trampoline_delivers_bytes(fake_lib):
    received = []

    def cb(samples: bytes, frame_count: int, channels: int, host_time: int):
        received.append((samples, frame_count, channels, host_time))

    tap = audiotap.SystemTap(callback=cb)

    # Simulate the C library invoking the callback
    import struct

    float_data = [1.0, -1.0, 0.5, -0.5]
    c_arr = (ctypes.c_float * 4)(*float_data)

    tap._trampoline(c_arr, 2, 2, 12345, 0)

    assert len(received) == 1
    samples, fc, ch, ht = received[0]
    assert fc == 2
    assert ch == 2
    assert ht == 12345
    assert len(samples) == 4 * ctypes.sizeof(ctypes.c_float)

    # Verify the bytes decode back to our floats
    unpacked = struct.unpack(f"{4}f", samples)
    for got, expected in zip(unpacked, float_data):
        assert abs(got - expected) < 1e-6

    tap.destroy()
