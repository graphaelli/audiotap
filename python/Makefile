ROOT = ..
BUILD = $(ROOT)/build

.PHONY: all sync capture_both test coverage clean

all: capture_both

# --- C shared library (for ctypes loading) ---

$(BUILD)/libaudiotap.dylib:
	$(MAKE) -C $(ROOT) build/libaudiotap.dylib

# --- Sync dependencies ---

sync:
	uv sync

# --- Example ---

capture_both: $(BUILD)/libaudiotap.dylib sync
	uv run examples/capture_both.py

# --- Tests ---

test: sync
	uv run pytest tests/ -v

# --- Coverage ---

coverage: sync
	uv run pytest tests/ -v --tb=short

clean:
	rm -rf .venv .pytest_cache
	find . -type d -name __pycache__ -exec rm -rf {} +
