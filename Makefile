BUILD_DIR_DEBUG   := build/debug
BUILD_DIR_RELEASE := build/release
CMAKE             := cmake
JOBS              := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
CC                := /usr/bin/gcc
CXX               := /usr/bin/g++

BOLD  := \033[1m
CYAN  := \033[36m
RESET := \033[0m

.PHONY: default debug release clean debug-clean release-clean debug-run release-run
.SILENT:

# Show this help message with descriptions for all targets
default:
	printf "\n$(BOLD)Available targets:$(RESET)\n\n"
	awk 'BEGIN {FS = ":.*?"} /^# / {helpMessage = substr($$0, 3); next} /^[a-zA-Z0-9_-]+:/ {if (helpMessage) {printf "  $(CYAN)$(BOLD)%-34s$(RESET)$(RESET) %s\n", $$1, helpMessage}; helpMessage = ""}' $(MAKEFILE_LIST)
	printf "\n"

# Configure and build in Debug mode (with sanitizers and debug symbols)
debug:
	$(CMAKE) -B $(BUILD_DIR_DEBUG) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX) \
		-DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer" \
		-DENABLE_TESTS=ON
	$(CMAKE) --build $(BUILD_DIR_DEBUG) -j$(JOBS)

# Run the emulator (debug build)
debug-run: debug
	$(BUILD_DIR_DEBUG)/jnext

# Remove debug build directory
debug-clean:
	rm -rf $(BUILD_DIR_DEBUG)

# Configure and build in Release mode (optimized, no sanitizers)
release:
	$(CMAKE) -B $(BUILD_DIR_RELEASE) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX) \
		-DCMAKE_CXX_FLAGS="-O2 -DNDEBUG" \
		-DENABLE_TESTS=OFF
	$(CMAKE) --build $(BUILD_DIR_RELEASE) -j$(JOBS)

# Run the emulator (release build)
release-run: release
	$(BUILD_DIR_RELEASE)/jnext

# Remove release build directory
release-clean:
	rm -rf $(BUILD_DIR_RELEASE)

# Remove all build directories
clean: debug-clean release-clean
