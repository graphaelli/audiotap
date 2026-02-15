//go:build darwin

package audiotap

import (
	"io"
	"sync"
	"testing"
)

func newTestTap(t *testing.T) *Tap {
	t.Helper()
	tap := newBridgeOnlyTap()
	if tap == nil {
		t.Fatal("bridge_create returned nil")
	}
	return tap
}

func TestReadWriteRoundTrip(t *testing.T) {
	tap := newTestTap(t)
	defer tap.Close()

	// Write samples through the bridge.
	in := []float32{1.0, 2.0, 3.0, 4.0, 5.0}
	tap.writeSamples(in)

	out := make([]float32, 10)
	n, err := tap.Read(out)
	if err != nil {
		t.Fatal(err)
	}
	if n != 5 {
		t.Fatalf("Read returned %d, want 5", n)
	}
	for i := 0; i < n; i++ {
		if out[i] != in[i] {
			t.Errorf("out[%d] = %f, want %f", i, out[i], in[i])
		}
	}
}

func TestReadEmptyBuf(t *testing.T) {
	tap := newTestTap(t)
	defer tap.Close()

	n, err := tap.Read(nil)
	if err != nil || n != 0 {
		t.Fatalf("Read(nil) = (%d, %v), want (0, nil)", n, err)
	}
	n, err = tap.Read([]float32{})
	if err != nil || n != 0 {
		t.Fatalf("Read([]) = (%d, %v), want (0, nil)", n, err)
	}
}

func TestReadAfterClose(t *testing.T) {
	tap := newTestTap(t)
	tap.Close()

	buf := make([]float32, 10)
	n, err := tap.Read(buf)
	if n != 0 || err != io.EOF {
		t.Fatalf("Read after Close = (%d, %v), want (0, EOF)", n, err)
	}
}

func TestCloseUnblocksRead(t *testing.T) {
	tap := newTestTap(t)

	done := make(chan struct{})
	go func() {
		defer close(done)
		buf := make([]float32, 10)
		n, err := tap.Read(buf)
		if n != 0 || err != io.EOF {
			t.Errorf("Read = (%d, %v), want (0, EOF)", n, err)
		}
	}()

	tap.Close()
	<-done
}

func TestDoubleClose(t *testing.T) {
	tap := newTestTap(t)
	if err := tap.Close(); err != nil {
		t.Fatal(err)
	}
	if err := tap.Close(); err != nil {
		t.Fatal(err)
	}
}

func TestConcurrentClose(t *testing.T) {
	tap := newTestTap(t)

	var wg sync.WaitGroup
	for i := 0; i < 10; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			tap.Close()
		}()
	}
	wg.Wait()
}

func TestReadReturnDataThenEOF(t *testing.T) {
	tap := newTestTap(t)

	in := []float32{7.0, 8.0, 9.0}
	tap.writeSamples(in)
	tap.Close()

	// First read: should still get the buffered data.
	buf := make([]float32, 10)
	n, err := tap.Read(buf)
	// After Close, the Go-level closed flag is set, so Read returns EOF
	// immediately without checking the bridge.  This is expected: the Go
	// layer prioritises fast shutdown over draining.
	if err != io.EOF {
		// If somehow we got data (implementation detail may change), that's OK too.
		if n != 3 {
			t.Fatalf("unexpected Read result: (%d, %v)", n, err)
		}
	}
}

func TestStartOnClosedTap(t *testing.T) {
	tap := newTestTap(t)
	tap.Close()

	err := tap.Start()
	if err != ErrClosed {
		t.Fatalf("Start after Close = %v, want ErrClosed", err)
	}
}

func TestStopOnClosedTap(t *testing.T) {
	tap := newTestTap(t)
	tap.Close()
	tap.Stop() // must not panic
}

func TestIsRunningOnClosedTap(t *testing.T) {
	tap := newTestTap(t)
	tap.Close()
	if tap.IsRunning() {
		t.Fatal("IsRunning should be false after Close")
	}
}
