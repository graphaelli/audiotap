//go:build darwin

// Command pcmplay plays raw PCM files in the format produced by audiotap.
//
// Usage mirrors the playback commands printed by capture_both:
//
//	pcmplay -ar 48000 -ac 2 system.pcm
//	pcmplay -ar 48000 -ac 1 mic.pcm
package main

/*
#cgo LDFLAGS: -framework CoreAudio -framework AudioToolbox
#include <AudioToolbox/AudioToolbox.h>
#include <unistd.h>

typedef struct {
	int          fd;
	uint32_t     channels;
	volatile int finished;
} PlayerState;

// outputCallback reads the next chunk from the file and enqueues it.
static void outputCallback(void *userdata, AudioQueueRef queue, AudioQueueBufferRef buf) {
	PlayerState *st = (PlayerState *)userdata;
	if (st->finished) return;

	ssize_t n = read(st->fd, buf->mAudioData, buf->mAudioDataBytesCapacity);
	if (n <= 0) {
		st->finished = 1;
		AudioQueueStop(queue, false);
		return;
	}
	// Truncate to a whole number of frames.
	uint32_t frameBytes = sizeof(float) * st->channels;
	n -= n % frameBytes;
	if (n == 0) {
		st->finished = 1;
		AudioQueueStop(queue, false);
		return;
	}
	buf->mAudioDataByteSize = (uint32_t)n;
	AudioQueueEnqueueBuffer(queue, buf, 0, NULL);
}

// isRunningChanged fires when the queue starts or stops.
static void isRunningChanged(void *userdata, AudioQueueRef queue, AudioQueuePropertyID prop) {
	(void)prop;
	UInt32 running = 0, size = sizeof(running);
	AudioQueueGetProperty(queue, kAudioQueueProperty_IsRunning, &running, &size);
	if (!running) *(volatile int *)userdata = 1;
}

// play_f32le plays interleaved float32-LE samples from fd through the default output device.
static int play_f32le(int fd, double sampleRate, uint32_t channels) {
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
	PlayerState state = { .fd = fd, .channels = channels, .finished = 0 };
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
)

func main() {
	ar := flag.Float64("ar", 48000, "sample rate in Hz")
	ac := flag.Int("ac", 2, "number of channels")
	flag.Parse()

	if *ac < 1 {
		fmt.Fprintf(os.Stderr, "-ac must be >= 1\n")
		os.Exit(1)
	}

	args := flag.Args()
	if len(args) != 1 {
		fmt.Fprintf(os.Stderr, "usage: pcmplay [-ar 48000] [-ac 2] <file.pcm>\n")
		os.Exit(1)
	}

	f, err := os.Open(args[0])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer f.Close()

	fi, err := f.Stat()
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	numFrames := fi.Size() / int64(4**ac)
	seconds := float64(numFrames) / *ar
	fmt.Printf("Playing %s (%.1fs, %.0f Hz, %d ch)\n", args[0], seconds, *ar, *ac)

	if rc := C.play_f32le(C.int(f.Fd()), C.double(*ar), C.uint32_t(*ac)); rc != 0 {
		fmt.Fprintf(os.Stderr, "playback error: OSStatus %d\n", int(rc))
		os.Exit(1)
	}
}
