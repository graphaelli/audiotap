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
#include <sys/ioctl.h>
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

// terminalWidth returns the terminal column count, or -1 if stdout is not a tty.
static int terminalWidth(void) {
	if (!isatty(STDOUT_FILENO)) return -1;
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return (int)ws.ws_col;
	return 80;
}
*/
import "C"

import (
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"math"
	"os"
	"time"
)

// waveChars are the block elements used for amplitude bars, from silence to full.
const waveChars = " ▁▂▃▄▅▆▇█"

// computeWaveform scans path in small chunks and returns a slice of width runes
// representing peak amplitude per time column. Memory usage is O(width), not
// O(file size).
func computeWaveform(path string, numFrames int64, channels, width int) ([]rune, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	peaks := make([]float64, width)
	const chunkFrames = 4096
	buf := make([]byte, chunkFrames*channels*4)

	levels := []rune(waveChars)
	var framePos int64
	for {
		n, err := io.ReadFull(f, buf)
		numRead := n / (4 * channels)
		for i := 0; i < numRead; i++ {
			barIdx := int(framePos * int64(width) / numFrames)
			if barIdx >= width {
				barIdx = width - 1
			}
			for ch := 0; ch < channels; ch++ {
				off := (i*channels + ch) * 4
				bits := binary.LittleEndian.Uint32(buf[off : off+4])
				s := math.Abs(float64(math.Float32frombits(bits)))
				if s > peaks[barIdx] {
					peaks[barIdx] = s
				}
			}
			framePos++
		}
		if err != nil {
			break
		}
	}

	maxPeak := 0.0
	for _, p := range peaks {
		if p > maxPeak {
			maxPeak = p
		}
	}
	if maxPeak == 0 {
		maxPeak = 1
	}

	bars := make([]rune, width)
	for i, p := range peaks {
		// Square-root mapping gives more visual resolution in quieter sections.
		idx := int(math.Sqrt(p/maxPeak) * float64(len(levels)-1))
		bars[i] = levels[idx]
	}
	return bars, nil
}

// renderWaveform returns a terminal line showing the waveform with a yellow
// cursor at the current playback position and the time at right.
func renderWaveform(bars []rune, progress, total float64) string {
	cursor := int(progress * float64(len(bars)))
	if cursor >= len(bars) {
		cursor = len(bars) - 1
	}
	line := make([]byte, 0, len(bars)*4)
	for i, ch := range bars {
		if i == cursor {
			line = append(line, "\033[33m"...)
			line = append(line, []byte(string(ch))...)
			line = append(line, "\033[0m"...)
		} else {
			line = append(line, []byte(string(ch))...)
		}
	}
	return fmt.Sprintf("%s  %.1fs / %.1fs", line, progress*total, total)
}

func main() {
	ar := flag.Float64("ar", 48000, "sample rate in Hz")
	ac := flag.Int("ac", 2, "number of channels")
	noviz := flag.Bool("noviz", false, "disable waveform visualization")
	flag.Parse()

	if *ac < 1 {
		fmt.Fprintf(os.Stderr, "-ac must be >= 1\n")
		os.Exit(1)
	}

	args := flag.Args()
	if len(args) != 1 {
		fmt.Fprintf(os.Stderr, "usage: pcmplay [-ar 48000] [-ac 2] [-noviz] <file.pcm>\n")
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

	// Compute waveform if stdout is a tty and -noviz wasn't set.
	var bars []rune
	if !*noviz {
		termW := int(C.terminalWidth())
		if termW > 0 {
			waveWidth := termW / 4
			if waveWidth < 30 {
				waveWidth = 30
			}
			if bars, err = computeWaveform(args[0], numFrames, *ac, waveWidth); err != nil {
				fmt.Fprintf(os.Stderr, "waveform: %v\n", err)
				bars = nil
			}
		}
	}

	play := func() int {
		return int(C.play_f32le(C.int(f.Fd()), C.double(*ar), C.uint32_t(*ac)))
	}

	if bars == nil {
		if rc := play(); rc != 0 {
			fmt.Fprintf(os.Stderr, "playback error: OSStatus %d\n", rc)
			os.Exit(1)
		}
		return
	}

	fmt.Print(renderWaveform(bars, 0, seconds))

	done := make(chan struct{})
	start := time.Now()
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-done:
				return
			case <-ticker.C:
				p := time.Since(start).Seconds() / seconds
				if p > 1 {
					p = 1
				}
				fmt.Print("\r" + renderWaveform(bars, p, seconds))
			}
		}
	}()

	rc := play()
	close(done)
	fmt.Print("\r" + renderWaveform(bars, 1, seconds))
	fmt.Println()

	if rc != 0 {
		fmt.Fprintf(os.Stderr, "playback error: OSStatus %d\n", rc)
		os.Exit(1)
	}
}
