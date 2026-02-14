//go:build darwin

// Command capture_both records system audio and microphone input to raw PCM files.
//
// Usage:
//
//	./build/capture_both 5          # record for 5 seconds
//	ffplay -f f32le -ar 48000 -ac 2 system.pcm
//	ffplay -f f32le -ar 48000 -ac 1 mic.pcm
package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"math"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"

	"github.com/graphaelli/audiotap"
)

const (
	sampleRate  = 48000
	sysChannels = 2
	micChannels = 1
)

func main() {
	duration := 10
	if len(os.Args) > 1 {
		if d, err := strconv.Atoi(os.Args[1]); err == nil && d > 0 {
			duration = d
		}
	}

	sysFile, err := os.Create("system.pcm")
	if err != nil {
		log.Fatal(err)
	}
	defer sysFile.Close()

	micFile, err := os.Create("mic.pcm")
	if err != nil {
		log.Fatal(err)
	}
	defer micFile.Close()

	// --- Microphone permission ---
	fmt.Println("Requesting microphone permission...")
	perm := audiotap.RequestMicPermission()
	if perm != audiotap.PermissionGranted {
		fmt.Fprintf(os.Stderr, "Microphone permission not granted (status=%d).\n"+
			"Grant access in System Settings > Privacy & Security > Microphone.\n", int(perm))
	}

	// --- Create taps ---
	fmt.Println("Creating system audio tap...")
	sysTap, sysErr := audiotap.NewSystemTap(audiotap.SystemConfig{
		SampleRate: sampleRate,
		Channels:   sysChannels,
	})
	if sysErr != nil {
		fmt.Fprintf(os.Stderr, "Failed to create system tap: %v\n", sysErr)
	}

	fmt.Println("Creating microphone tap...")
	micTap, micErr := audiotap.NewMicTap(audiotap.MicConfig{
		SampleRate: sampleRate,
		Channels:   micChannels,
	})
	if micErr != nil {
		fmt.Fprintf(os.Stderr, "Failed to create mic tap: %v\n", micErr)
	}

	if sysTap == nil && micTap == nil {
		log.Fatal("No audio sources available. Exiting.")
	}

	// --- Start capture ---
	if sysTap != nil {
		if err := sysTap.Start(); err != nil {
			fmt.Fprintf(os.Stderr, "System tap start error: %v\n", err)
		} else {
			fmt.Println("System audio capture started.")
		}
	}
	if micTap != nil {
		if err := micTap.Start(); err != nil {
			fmt.Fprintf(os.Stderr, "Mic tap start error: %v\n", err)
		} else {
			fmt.Println("Microphone capture started.")
		}
	}

	// --- Read loops ---
	done := make(chan struct{}, 2)

	readLoop := func(tap *audiotap.Tap, f *os.File) {
		defer func() { done <- struct{}{} }()
		buf := make([]float32, 4096)
		for {
			n, err := tap.Read(buf)
			if err != nil {
				return
			}
			if err := writeFloat32s(f, buf[:n]); err != nil {
				fmt.Fprintf(os.Stderr, "write error: %v\n", err)
				return
			}
		}
	}

	active := 0
	if sysTap != nil {
		go readLoop(sysTap, sysFile)
		active++
	}
	if micTap != nil {
		go readLoop(micTap, micFile)
		active++
	}

	// --- Wait for duration or signal ---
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)

	fmt.Printf("Recording for %d seconds (Ctrl+C to stop early)...\n", duration)

	select {
	case <-sig:
	case <-time.After(time.Duration(duration) * time.Second):
	}

	fmt.Println("\nStopping...")
	if sysTap != nil {
		sysTap.Close()
	}
	if micTap != nil {
		micTap.Close()
	}
	for i := 0; i < active; i++ {
		<-done
	}

	fmt.Println("Done. Playback with:")
	fmt.Printf("  ffplay -f f32le -ar %d -ac %d system.pcm\n", sampleRate, sysChannels)
	fmt.Printf("  ffplay -f f32le -ar %d -ac %d mic.pcm\n", sampleRate, micChannels)
}

func writeFloat32s(f *os.File, samples []float32) error {
	for _, s := range samples {
		if err := binary.Write(f, binary.LittleEndian, math.Float32bits(s)); err != nil {
			return err
		}
	}
	return nil
}
