//go:build darwin

// Command transcribe captures live audio and prints speech-to-text transcription.
//
// Usage:
//
//	./build/transcribe -model models/ggml-base.en.bin -source mic
//	./build/transcribe -model models/ggml-base.en.bin -source system
package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/graphaelli/audiotap"
	"github.com/graphaelli/audiotap/whisper"
)

func main() {
	modelPath := flag.String("model", "models/ggml-base.en.bin", "path to whisper GGML model")
	source := flag.String("source", "mic", "audio source: mic or system")
	lang := flag.String("lang", "en", "language code (or \"auto\")")
	translate := flag.Bool("translate", false, "translate to English")
	duration := flag.Duration("chunk", 5*time.Second, "audio chunk duration for each whisper pass")
	flag.Parse()

	// --- Load whisper model ---
	fmt.Fprintf(os.Stderr, "Loading model %s ...\n", *modelPath)
	wctx, err := whisper.NewContext(*modelPath)
	if err != nil {
		log.Fatal(err)
	}
	defer wctx.Close()
	wctx.SetLanguage(*lang)
	wctx.SetTranslate(*translate)

	// --- Create audio tap ---
	const sampleRate = 16000
	var tap *audiotap.Tap

	switch *source {
	case "mic":
		perm := audiotap.MicPermissionStatus()
		if perm == audiotap.PermissionDenied {
			log.Fatal("Microphone permission denied — grant access in System Settings > Privacy & Security > Microphone")
		}
		if perm == audiotap.PermissionUnknown {
			fmt.Fprintf(os.Stderr, "Requesting microphone permission ...\n")
			if audiotap.RequestMicPermission() != audiotap.PermissionGranted {
				log.Fatal("Microphone permission denied")
			}
		}
		tap, err = audiotap.NewMicTap(audiotap.MicConfig{
			SampleRate: sampleRate,
			Channels:   1,
		})
	case "system":
		tap, err = audiotap.NewSystemTap(audiotap.SystemConfig{
			SampleRate: sampleRate,
			Channels:   1,
		})
	default:
		log.Fatalf("unknown source %q (use mic or system)", *source)
	}
	if err != nil {
		log.Fatal(err)
	}
	defer tap.Close()

	if err := tap.Start(); err != nil {
		log.Fatal(err)
	}
	fmt.Fprintf(os.Stderr, "Listening on %s ... (Ctrl+C to stop)\n", *source)

	// --- Signal handling ---
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)

	// --- Read + transcribe loop ---
	chunkSize := int(float64(sampleRate) * duration.Seconds())
	buf := make([]float32, chunkSize)
	done := make(chan struct{})

	go func() {
		defer close(done)
		offset := 0
		for {
			n, err := tap.Read(buf[offset:])
			if err != nil {
				return
			}
			offset += n
			if offset < chunkSize {
				continue
			}

			segments, err := wctx.Process(buf[:offset])
			if err != nil {
				fmt.Fprintf(os.Stderr, "whisper: %v\n", err)
			}
			for _, seg := range segments {
				fmt.Printf("[%s -> %s]%s\n",
					fmtTS(seg.Start), fmtTS(seg.End), seg.Text)
			}
			offset = 0
		}
	}()

	select {
	case <-sig:
		fmt.Fprintf(os.Stderr, "\nShutting down ...\n")
	case <-done:
	}

	tap.Close()
	<-done
}

func fmtTS(d time.Duration) string {
	m := int(d.Minutes())
	s := int(d.Seconds()) % 60
	ms := int(d.Milliseconds()) % 1000
	return fmt.Sprintf("%02d:%02d.%03d", m, s, ms)
}
