// Command pcm2wav converts raw PCM files to WAV format.
//
// Usage mirrors the flags used by pcmplay and ffmpeg:
//
//	pcm2wav -ar 48000 -ac 2 system.pcm
//	pcm2wav -ar 48000 -ac 1 mic.pcm -o output.wav
//
// Input is always interpreted as interleaved IEEE 754 float32, little-endian
// (f32le) — the only format audiotap produces. The output WAV uses
// WAVE_FORMAT_IEEE_FLOAT (format tag 3) so no precision is lost.
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"
)

// writeWAVHeader writes a 44-byte RIFF/WAVE header for IEEE float32 audio to w.
func writeWAVHeader(w io.Writer, sampleRate uint32, channels uint16, dataSize uint32) error {
	const bitsPerSample = uint16(32)
	blockAlign := channels * bitsPerSample / 8
	byteRate := sampleRate * uint32(blockAlign)

	le := binary.LittleEndian
	write := func(v any) error { return binary.Write(w, le, v) }

	// RIFF chunk descriptor
	if _, err := io.WriteString(w, "RIFF"); err != nil {
		return err
	}
	if err := write(uint32(36 + dataSize)); err != nil { // file size minus 8
		return err
	}
	if _, err := io.WriteString(w, "WAVE"); err != nil {
		return err
	}

	// fmt sub-chunk (16 bytes)
	if _, err := io.WriteString(w, "fmt "); err != nil {
		return err
	}
	if err := write(uint32(16)); err != nil { // sub-chunk size
		return err
	}
	if err := write(uint16(3)); err != nil { // WAVE_FORMAT_IEEE_FLOAT
		return err
	}
	if err := write(channels); err != nil {
		return err
	}
	if err := write(sampleRate); err != nil {
		return err
	}
	if err := write(byteRate); err != nil {
		return err
	}
	if err := write(blockAlign); err != nil {
		return err
	}
	if err := write(bitsPerSample); err != nil {
		return err
	}

	// data sub-chunk header
	if _, err := io.WriteString(w, "data"); err != nil {
		return err
	}
	return write(dataSize)
}

func main() {
	ar := flag.Uint("ar", 48000, "sample rate in Hz")
	ac := flag.Uint("ac", 2, "number of channels")
	outFlag := flag.String("o", "", "output WAV file (default: input with .wav extension)")
	flag.Parse()

	if *ac < 1 {
		fmt.Fprintln(os.Stderr, "-ac must be >= 1")
		os.Exit(1)
	}

	args := flag.Args()
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "usage: pcm2wav [-ar 48000] [-ac 2] [-o output.wav] <file.pcm>")
		os.Exit(1)
	}

	inPath := args[0]
	outPath := *outFlag
	if outPath == "" {
		outPath = strings.TrimSuffix(inPath, ".pcm") + ".wav"
	}

	in, err := os.Open(inPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer in.Close()

	fi, err := in.Stat()
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	dataSize := uint32(fi.Size())
	numFrames := fi.Size() / int64(4*int(*ac))
	seconds := float64(numFrames) / float64(*ar)

	out, err := os.Create(outPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer out.Close()

	if err := writeWAVHeader(out, uint32(*ar), uint16(*ac), dataSize); err != nil {
		fmt.Fprintln(os.Stderr, "writing WAV header:", err)
		os.Exit(1)
	}

	if _, err := io.Copy(out, in); err != nil {
		fmt.Fprintln(os.Stderr, "writing WAV data:", err)
		os.Exit(1)
	}

	fmt.Printf("Wrote %s (%.1fs, %d Hz, %d ch)\n", outPath, seconds, *ar, *ac)
}
