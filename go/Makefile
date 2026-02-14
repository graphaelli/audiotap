ROOT       = ..
BUILD      = $(ROOT)/build
CC         = clang
CFLAGS     = -Wall -Wextra -O2 -I$(ROOT)/include
COV_FLAGS  = -fprofile-instr-generate -fcoverage-mapping

WHISPER_DIR   = $(ROOT)/third_party/whisper.cpp
WHISPER_BUILD = $(WHISPER_DIR)/build
NPROC         = $(shell sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)

.PHONY: all deps model transcribe-go capture_both-go test-bridge clean

all: transcribe-go model

# --- Static library (for CGO linking) ---

$(BUILD)/libaudiotap.a:
	$(MAKE) -C $(ROOT) build/libaudiotap.a

# --- whisper.cpp dependency ---

deps: $(WHISPER_BUILD)/src/libwhisper.a

$(WHISPER_DIR)/CMakeLists.txt:
	git clone --depth 1 https://github.com/ggerganov/whisper.cpp.git $(WHISPER_DIR)

$(WHISPER_BUILD)/src/libwhisper.a: $(WHISPER_DIR)/CMakeLists.txt
	cmake -B $(WHISPER_BUILD) -S $(WHISPER_DIR) \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_SHARED_LIBS=OFF \
		-DWHISPER_BUILD_EXAMPLES=OFF \
		-DWHISPER_BUILD_TESTS=OFF \
		-DGGML_METAL=OFF
	cmake --build $(WHISPER_BUILD) --config Release -j$(NPROC)

# Bundle all whisper.cpp static libs into one archive for easy linking.
$(BUILD)/libwhisper-bundle.a: $(WHISPER_BUILD)/src/libwhisper.a
	libtool -static -o $@ $$(find $(WHISPER_BUILD) -name '*.a')

# --- Whisper model ---

model: $(ROOT)/models/ggml-base.en.bin

$(ROOT)/models/ggml-base.en.bin: $(WHISPER_DIR)/CMakeLists.txt
	@mkdir -p $(ROOT)/models
	$(WHISPER_DIR)/models/download-ggml-model.sh base.en $(ROOT)/models

# --- Go transcribe binary ---

transcribe-go: $(BUILD)/transcribe-go

$(BUILD)/transcribe-go: $(BUILD)/libaudiotap.a $(BUILD)/libwhisper-bundle.a $(wildcard *.go whisper/*.go cmd/transcribe/*.go)
	go build -o $@ ./cmd/transcribe

# --- Go capture_both-go binary ---

capture_both-go: $(BUILD)/capture_both-go

$(BUILD)/capture_both-go: $(BUILD)/libaudiotap.a $(wildcard *.go cmd/capture_both/*.go)
	go build -o $@ ./cmd/capture_both

# --- Bridge tests ---

test-bridge: $(BUILD)/test_bridge
	@./$(BUILD)/test_bridge

$(BUILD)/test_bridge: tests/test_bridge.c bridge.c bridge.h $(ROOT)/include/audiotap.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-variable -O0 -g $(COV_FLAGS) -o $@ tests/test_bridge.c -lpthread

clean:
	rm -f $(BUILD)/test_bridge $(BUILD)/transcribe-go $(BUILD)/capture_both-go $(BUILD)/libaudiotap.a $(BUILD)/libwhisper-bundle.a
