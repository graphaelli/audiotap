CC = clang
CFLAGS = -Wall -Wextra -O2 -Iinclude
OBJCFLAGS = $(CFLAGS) -fobjc-arc
LDFLAGS = -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework Foundation -framework AVFoundation
DYLIB_FLAGS = -dynamiclib -install_name @rpath/libaudiotap.dylib
COV_FLAGS = -fprofile-instr-generate -fcoverage-mapping

.PHONY: all clean examples test coverage

all: build/libaudiotap.dylib

build/audiotap_system.o: src/audiotap_system.m src/audiotap_internal.h include/audiotap.h
	@mkdir -p build
	$(CC) $(OBJCFLAGS) -c -o $@ $<

build/audiotap_mic.o: src/audiotap_mic.c src/audiotap_internal.h include/audiotap.h
	@mkdir -p build
	$(CC) $(CFLAGS) -fblocks -c -o $@ $<

build/audiotap_common.o: src/audiotap_common.c src/audiotap_internal.h include/audiotap.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

build/audiotap_permission.o: src/audiotap_permission.m include/audiotap.h
	@mkdir -p build
	$(CC) $(OBJCFLAGS) -c -o $@ $<

build/libaudiotap.dylib: build/audiotap_system.o build/audiotap_mic.o build/audiotap_common.o build/audiotap_permission.o
	$(CC) $(DYLIB_FLAGS) $(LDFLAGS) -o $@ $^

examples: build/capture_both

build/capture_both: examples/capture_both.c build/libaudiotap.dylib include/audiotap.h examples/Info.plist
	$(CC) $(CFLAGS) -o $@ $< -Lbuild -laudiotap $(LDFLAGS) -Wl,-rpath,@executable_path -sectcreate __TEXT __info_plist examples/Info.plist

test: build/test_audiotap build/test_audiotap_system build/test_audiotap_permission
	@echo "Running mic/common tests..."
	@LLVM_PROFILE_FILE=build/test.profraw ./build/test_audiotap
	@echo ""
	@echo "Running system tests..."
	@LLVM_PROFILE_FILE=build/test_system.profraw ./build/test_audiotap_system
	@echo ""
	@echo "Running permission tests..."
	@LLVM_PROFILE_FILE=build/test_permission.profraw ./build/test_audiotap_permission

build/test_audiotap: tests/test_audiotap.c src/audiotap_common.c src/audiotap_mic.c src/audiotap_internal.h include/audiotap.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Wno-nonnull -fblocks -O0 -g $(COV_FLAGS) -DAUDIOTAP_TESTING -o $@ tests/test_audiotap.c

build/test_audiotap_system: tests/test_audiotap_system.m src/audiotap_system.m src/audiotap_internal.h include/audiotap.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Wno-nonnull -fobjc-arc -fblocks -O0 -g $(COV_FLAGS) -DAUDIOTAP_TESTING -o $@ tests/test_audiotap_system.m -framework Foundation -framework CoreAudio -framework AudioToolbox

build/test_audiotap_permission: tests/test_audiotap_permission.m src/audiotap_permission.m include/audiotap.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Wno-nonnull -fobjc-arc -O0 -g $(COV_FLAGS) -DAUDIOTAP_TESTING -o $@ tests/test_audiotap_permission.m -framework AVFoundation

coverage: test
	@xcrun llvm-profdata merge -sparse build/test.profraw -o build/test.profdata
	@xcrun llvm-profdata merge -sparse build/test_system.profraw -o build/test_system.profdata
	@xcrun llvm-profdata merge -sparse build/test_permission.profraw -o build/test_permission.profdata
	@echo ""
	@echo "=== Coverage: audiotap_mic.c + audiotap_common.c ==="
	@xcrun llvm-cov report build/test_audiotap -instr-profile=build/test.profdata -sources src/audiotap_mic.c src/audiotap_common.c
	@echo ""
	@echo "=== Coverage: audiotap_system.m ==="
	@xcrun llvm-cov report build/test_audiotap_system -instr-profile=build/test_system.profdata -sources src/audiotap_system.m
	@echo ""
	@echo "=== Coverage: audiotap_permission.m ==="
	@xcrun llvm-cov report build/test_audiotap_permission -instr-profile=build/test_permission.profdata -sources src/audiotap_permission.m

clean:
	rm -rf build/ *.profraw *.profdata
