ROOT       = ..
BUILD      = $(ROOT)/build
CC         = clang
CFLAGS     = -Wall -Wextra -O2 -I$(ROOT)/include
COV_FLAGS  = -fprofile-instr-generate -fcoverage-mapping

.PHONY: all capture_both-go pcmplay test test-bridge coverage clean

all: capture_both-go pcmplay

# --- Go capture_both-go binary ---

capture_both-go: $(BUILD)/capture_both-go

$(BUILD)/capture_both-go: $(wildcard *.go *.c *.h *.m cmd/capture_both/*.go) $(ROOT)/include/audiotap.h $(wildcard $(ROOT)/src/*)
	@mkdir -p $(BUILD)
	go build -o $@ ./cmd/capture_both

# --- Go pcmplay binary ---

pcmplay: $(BUILD)/pcmplay

$(BUILD)/pcmplay: $(wildcard cmd/pcmplay/*.go)
	@mkdir -p $(BUILD)
	go build -o $@ ./cmd/pcmplay

# --- Tests ---

test: test-bridge
	go test ./...

# --- Bridge tests ---

test-bridge: $(BUILD)/test_bridge
	@LLVM_PROFILE_FILE=$(BUILD)/test_bridge.profraw ./$(BUILD)/test_bridge

$(BUILD)/test_bridge: tests/test_bridge.c bridge.c bridge.h $(ROOT)/include/audiotap.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-variable -O0 -g $(COV_FLAGS) -o $@ tests/test_bridge.c -lpthread

# --- Go coverage ---

coverage:
	go test -coverprofile=$(BUILD)/go-coverage.out ./...
	go tool cover -func=$(BUILD)/go-coverage.out

clean:
	rm -f $(BUILD)/test_bridge $(BUILD)/capture_both-go $(BUILD)/pcmplay
	rm -f $(BUILD)/test_bridge.profraw $(BUILD)/test_bridge.profdata $(BUILD)/go-coverage.out
