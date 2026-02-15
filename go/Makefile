ROOT       = ..
BUILD      = $(ROOT)/build
CC         = clang
CFLAGS     = -Wall -Wextra -O2 -I$(ROOT)/include
COV_FLAGS  = -fprofile-instr-generate -fcoverage-mapping

.PHONY: all capture_both-go test test-bridge coverage clean

all: capture_both-go

# --- Static library (for CGO linking) ---

$(BUILD)/libaudiotap.a:
	$(MAKE) -C $(ROOT) build/libaudiotap.a

# --- Go capture_both-go binary ---

capture_both-go: $(BUILD)/capture_both-go

$(BUILD)/capture_both-go: $(BUILD)/libaudiotap.a $(wildcard *.go cmd/capture_both/*.go)
	go build -o $@ ./cmd/capture_both

# --- Tests ---

test: $(BUILD)/libaudiotap.a test-bridge
	go test ./...

# --- Bridge tests ---

test-bridge: $(BUILD)/test_bridge
	@LLVM_PROFILE_FILE=$(BUILD)/test_bridge.profraw ./$(BUILD)/test_bridge

$(BUILD)/test_bridge: tests/test_bridge.c bridge.c bridge.h $(ROOT)/include/audiotap.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-variable -O0 -g $(COV_FLAGS) -o $@ tests/test_bridge.c -lpthread

# --- Go coverage ---

coverage: $(BUILD)/libaudiotap.a
	go test -coverprofile=$(BUILD)/go-coverage.out ./...
	go tool cover -func=$(BUILD)/go-coverage.out

clean:
	rm -f $(BUILD)/test_bridge $(BUILD)/capture_both-go $(BUILD)/libaudiotap.a
	rm -f $(BUILD)/test_bridge.profraw $(BUILD)/test_bridge.profdata $(BUILD)/go-coverage.out
