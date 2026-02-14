//go:build darwin

// Package audiotap provides Go bindings for the audiotap macOS audio capture library.
//
// Audio is delivered as interleaved float32 PCM via a lock-free ring buffer,
// keeping the real-time audio callback out of the Go runtime.
package audiotap

/*
#cgo CFLAGS:  -I${SRCDIR}/include
#cgo LDFLAGS: ${SRCDIR}/build/libaudiotap.a -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework Foundation -framework AVFoundation -lobjc

#include "bridge.h"
#include <stdlib.h>
*/
import "C"

import (
	"errors"
	"fmt"
	"io"
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"
)

var (
	ErrClosed = errors.New("audiotap: tap is closed")
)

// Permission represents microphone permission status.
type Permission int

const (
	PermissionUnknown Permission = C.AUDIOTAP_PERMISSION_UNKNOWN
	PermissionGranted Permission = C.AUDIOTAP_PERMISSION_GRANTED
	PermissionDenied  Permission = C.AUDIOTAP_PERMISSION_DENIED
)

// MicPermissionStatus returns the current microphone permission without prompting.
func MicPermissionStatus() Permission {
	return Permission(C.audiotap_mic_permission_status())
}

// RequestMicPermission blocks until the user responds to the system permission dialog.
func RequestMicPermission() Permission {
	return Permission(C.audiotap_request_mic_permission())
}

// SystemConfig configures a system audio tap.
type SystemConfig struct {
	PIDs       []int   // Process IDs to capture; nil = all processes.
	SampleRate float32 // e.g. 16000 or 48000
	Channels   uint32  // 1 (mono) or 2 (stereo)
	Mute       bool    // Silence tapped audio in speakers.
}

// MicConfig configures a microphone capture tap.
type MicConfig struct {
	SampleRate float32
	Channels   uint32 // 1 (mono) or 2 (stereo)
}

// Tap is a handle to an audio capture stream.  It is safe to call Read from
// one goroutine and Close from another.
type Tap struct {
	tap    *C.audiotap_t
	bridge *C.bridge_state_t
	once   sync.Once
	closed atomic.Bool
}

// NewSystemTap creates a system audio tap.  Call Start to begin capture.
func NewSystemTap(cfg SystemConfig) (*Tap, error) {
	bridge := C.bridge_create()
	if bridge == nil {
		return nil, errors.New("audiotap: failed to create bridge")
	}

	var pids *C.pid_t
	var pidCount C.uint32_t
	var cpids []C.pid_t
	if len(cfg.PIDs) > 0 {
		cpids = make([]C.pid_t, len(cfg.PIDs))
		for i, p := range cfg.PIDs {
			cpids[i] = C.pid_t(p)
		}
		pids = &cpids[0]
		pidCount = C.uint32_t(len(cfg.PIDs))
	}

	var mute C.int
	if cfg.Mute {
		mute = 1
	}

	tap := C.bridge_create_system(bridge, pids, pidCount,
		C.float(cfg.SampleRate), C.uint32_t(cfg.Channels), mute)
	if tap == nil {
		C.bridge_destroy(bridge)
		return nil, errors.New("audiotap: failed to create system tap")
	}

	t := &Tap{tap: tap, bridge: bridge}
	runtime.SetFinalizer(t, (*Tap).Close)
	return t, nil
}

// NewMicTap creates a microphone capture tap.  Call Start to begin capture.
func NewMicTap(cfg MicConfig) (*Tap, error) {
	bridge := C.bridge_create()
	if bridge == nil {
		return nil, errors.New("audiotap: failed to create bridge")
	}

	tap := C.bridge_create_mic(bridge,
		C.float(cfg.SampleRate), C.uint32_t(cfg.Channels))
	if tap == nil {
		C.bridge_destroy(bridge)
		return nil, errors.New("audiotap: failed to create mic tap")
	}

	t := &Tap{tap: tap, bridge: bridge}
	runtime.SetFinalizer(t, (*Tap).Close)
	return t, nil
}

// Start begins audio capture.
func (t *Tap) Start() error {
	if t.closed.Load() {
		return ErrClosed
	}
	status := C.audiotap_start(t.tap)
	if status != 0 {
		return fmt.Errorf("audiotap: start failed: %s (status %d)",
			C.GoString(C.audiotap_error_string(status)), int(status))
	}
	return nil
}

// Stop pauses audio capture without releasing resources.
func (t *Tap) Stop() {
	if t.closed.Load() {
		return
	}
	C.audiotap_stop(t.tap)
}

// IsRunning reports whether the tap is currently capturing audio.
func (t *Tap) IsRunning() bool {
	if t.closed.Load() {
		return false
	}
	return C.audiotap_is_running(t.tap) != 0
}

// Read fills buf with PCM float32 samples and returns the number written.
// It blocks until data is available.  Returns io.EOF after Close.
func (t *Tap) Read(buf []float32) (int, error) {
	if t.closed.Load() {
		return 0, io.EOF
	}
	if len(buf) == 0 {
		return 0, nil
	}
	n := C.bridge_read(t.bridge,
		(*C.float)(unsafe.Pointer(&buf[0])),
		C.uint32_t(len(buf)))
	if n < 0 {
		return 0, errors.New("audiotap: read error")
	}
	if n == 0 {
		return 0, io.EOF
	}
	return int(n), nil
}

// Close stops capture and releases all resources.  Safe to call multiple times.
func (t *Tap) Close() error {
	t.once.Do(func() {
		t.closed.Store(true)
		runtime.SetFinalizer(t, nil)
		C.bridge_close(t.bridge)
		C.audiotap_destroy(t.tap) // stops callback before returning
		C.bridge_destroy(t.bridge)
	})
	return nil
}
