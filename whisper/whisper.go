//go:build darwin

// Package whisper provides Go bindings for the whisper.cpp speech-to-text engine.
package whisper

/*
#cgo CFLAGS:  -I${SRCDIR}/../third_party/whisper.cpp/include
#cgo LDFLAGS: ${SRCDIR}/../build/libwhisper-bundle.a -lstdc++ -lm -framework Accelerate

#include "whisper_bridge.h"
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"runtime"
	"time"
	"unsafe"
)

// Segment is a single transcription result with timestamps.
type Segment struct {
	Start time.Duration
	End   time.Duration
	Text  string
}

// Context holds a loaded whisper model and its processing parameters.
type Context struct {
	state   *C.whisper_state_t
	langStr *C.char // kept alive for whisper's params.language pointer
}

// NewContext loads a GGML model file and returns a ready-to-use Context.
func NewContext(modelPath string) (*Context, error) {
	cpath := C.CString(modelPath)
	defer C.free(unsafe.Pointer(cpath))

	state := C.whisper_state_create(cpath)
	if state == nil {
		return nil, fmt.Errorf("whisper: failed to load model from %s", modelPath)
	}

	ctx := &Context{state: state}
	runtime.SetFinalizer(ctx, (*Context).Close)
	return ctx, nil
}

// SetLanguage sets the language for recognition (e.g. "en", "es", "auto").
func (c *Context) SetLanguage(lang string) {
	if c.langStr != nil {
		C.free(unsafe.Pointer(c.langStr))
	}
	c.langStr = C.CString(lang)
	C.whisper_state_set_language(c.state, c.langStr)
}

// SetTranslate enables translation to English.
func (c *Context) SetTranslate(translate bool) {
	v := C.int(0)
	if translate {
		v = 1
	}
	C.whisper_state_set_translate(c.state, v)
}

// SetThreads sets the number of CPU threads for processing.
func (c *Context) SetThreads(n int) {
	C.whisper_state_set_n_threads(c.state, C.int(n))
}

// Process runs speech recognition on 16 kHz mono float32 PCM samples.
func (c *Context) Process(samples []float32) ([]Segment, error) {
	if len(samples) == 0 {
		return nil, nil
	}

	ret := C.whisper_state_process(c.state,
		(*C.float)(unsafe.Pointer(&samples[0])),
		C.int(len(samples)))
	if ret != 0 {
		return nil, fmt.Errorf("whisper: processing failed (code %d)", int(ret))
	}

	n := int(C.whisper_state_n_segments(c.state))
	segs := make([]Segment, 0, n)
	for i := 0; i < n; i++ {
		ci := C.int(i)
		text := C.GoString(C.whisper_state_segment_text(c.state, ci))
		t0 := int64(C.whisper_state_segment_t0(c.state, ci))
		t1 := int64(C.whisper_state_segment_t1(c.state, ci))
		segs = append(segs, Segment{
			Start: time.Duration(t0*10) * time.Millisecond,
			End:   time.Duration(t1*10) * time.Millisecond,
			Text:  text,
		})
	}
	return segs, nil
}

// Close releases the model and all associated resources.
func (c *Context) Close() error {
	if c.state != nil {
		runtime.SetFinalizer(c, nil)
		C.whisper_state_destroy(c.state)
		c.state = nil
	}
	if c.langStr != nil {
		C.free(unsafe.Pointer(c.langStr))
		c.langStr = nil
	}
	return nil
}
