//go:build darwin

// Command pcmplay plays raw PCM files in the format produced by audiotap.
//
// Usage mirrors the ffplay commands printed by capture_both:
//
//	pcmplay -f f32le -ar 48000 -ac 2 system.pcm
//	pcmplay -f f32le -ar 48000 -ac 1 mic.pcm
//
// Build:
//
//	go build github.com/graphaelli/audiotap/examples/pcmplay
package main

/*
#cgo LDFLAGS: -framework CoreAudio -framework AudioToolbox
#include <AudioToolbox/AudioToolbox.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
	float          *data;
	uint32_t        totalFrames;
	uint32_t        channels;
	uint32_t        curFrame;
	volatile int    finished;
} PlayerState;

// outputCallback is called by AudioQueue each time it needs a new buffer.
static void outputCallback(void *userdata, AudioQueueRef queue, AudioQueueBufferRef buf) {
	PlayerState *st = (PlayerState *)userdata;
	uint32_t remaining = st->totalFrames - st->curFrame;
	if (remaining == 0) {
		if (!st->finished) {
			st->finished = 1;
			// false = drain already-queued buffers before stopping
			AudioQueueStop(queue, false);
		}
		return;
	}
	uint32_t framesPerBuf = buf->mAudioDataBytesCapacity / (sizeof(float) * st->channels);
	if (framesPerBuf > remaining) framesPerBuf = remaining;
	uint32_t bytes = framesPerBuf * st->channels * sizeof(float);
	memcpy(buf->mAudioData, st->data + (uint64_t)st->curFrame * st->channels, bytes);
	buf->mAudioDataByteSize = bytes;
	st->curFrame += framesPerBuf;
	AudioQueueEnqueueBuffer(queue, buf, 0, NULL);
}

// isRunningChanged is called when kAudioQueueProperty_IsRunning changes.
static void isRunningChanged(void *userdata, AudioQueueRef queue, AudioQueuePropertyID prop) {
	(void)prop;
	UInt32 running = 0, size = sizeof(running);
	AudioQueueGetProperty(queue, kAudioQueueProperty_IsRunning, &running, &size);
	if (!running) *(volatile int *)userdata = 1;
}

// play_f32le plays interleaved float32-LE samples through the default output device.
static int play_f32le(const float *data, uint32_t frames, double sampleRate, uint32_t channels) {
	AudioStreamBasicDescription fmt = {
		.mSampleRate       = sampleRate,
		.mFormatID         = kAudioFormatLinearPCM,
		.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
		.mBitsPerChannel   = 32,
		.mChannelsPerFrame = channels,
		.mBytesPerFrame    = sizeof(float) * channels,
		.mFramesPerPacket  = 1,
		.mBytesPerPacket   = sizeof(float) * channels,
	};
	PlayerState state = {
		.data        = (float *)data,
		.totalFrames = frames,
		.channels    = channels,
		.curFrame    = 0,
		.finished    = 0,
	};
	volatile int stopped = 0;

	AudioQueueRef queue;
	OSStatus err = AudioQueueNewOutput(&fmt, outputCallback, &state, NULL, NULL, 0, &queue);
	if (err != noErr) return (int)err;

	AudioQueueAddPropertyListener(queue, kAudioQueueProperty_IsRunning, isRunningChanged,
	                              (void *)&stopped);

	// Prime three buffers so the queue never starves.
	const int      numBufs   = 3;
	const uint32_t bufFrames = 4096;
	AudioQueueBufferRef bufs[3];
	for (int i = 0; i < numBufs; i++) {
		uint32_t cap = bufFrames * channels * sizeof(float);
		if ((err = AudioQueueAllocateBuffer(queue, cap, &bufs[i])) != noErr) {
			AudioQueueDispose(queue, true);
			return (int)err;
		}
		outputCallback(&state, queue, bufs[i]);
	}

	if ((err = AudioQueueStart(queue, NULL)) != noErr) {
		AudioQueueDispose(queue, true);
		return (int)err;
	}

	while (!stopped) usleep(10000); // 10 ms poll

	AudioQueueDispose(queue, true);
	return 0;
}
*/
import "C"

import (
	"flag"
	"fmt"
	"os"
	"unsafe"
)

func main() {
	format := flag.String("f", "f32le", "sample format (only f32le supported)")
	ar := flag.Float64("ar", 48000, "sample rate in Hz")
	ac := flag.Int("ac", 2, "number of channels")
	flag.Parse()

	if *format != "f32le" {
		fmt.Fprintf(os.Stderr, "unsupported format %q: only f32le is supported\n", *format)
		os.Exit(1)
	}
	if *ac < 1 {
		fmt.Fprintf(os.Stderr, "-ac must be >= 1\n")
		os.Exit(1)
	}

	args := flag.Args()
	if len(args) != 1 {
		fmt.Fprintf(os.Stderr, "usage: pcmplay [-f f32le] [-ar 48000] [-ac 2] <file.pcm>\n")
		os.Exit(1)
	}

	raw, err := os.ReadFile(args[0])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	raw = raw[:len(raw)&^3] // trim to a multiple of 4 bytes
	if len(raw) == 0 {
		fmt.Fprintf(os.Stderr, "%s is empty\n", args[0])
		os.Exit(1)
	}

	numFrames := len(raw) / 4 / *ac
	seconds := float64(numFrames) / *ar
	fmt.Printf("Playing %s (%.1fs, %.0f Hz, %d ch)\n", args[0], seconds, *ar, *ac)

	// Copy into C-managed memory so AudioQueue's threads can safely read it
	// after this function returns to the Go runtime.
	cBuf := C.malloc(C.size_t(len(raw)))
	if cBuf == nil {
		fmt.Fprintln(os.Stderr, "out of memory")
		os.Exit(1)
	}
	defer C.free(cBuf)
	C.memcpy(cBuf, unsafe.Pointer(&raw[0]), C.size_t(len(raw)))

	if rc := C.play_f32le((*C.float)(cBuf), C.uint32_t(numFrames), C.double(*ar), C.uint32_t(*ac)); rc != 0 {
		fmt.Fprintf(os.Stderr, "playback error: OSStatus %d\n", int(rc))
		os.Exit(1)
	}
}
