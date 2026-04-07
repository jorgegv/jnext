BUILD_DIR_DEBUG       := build/debug
BUILD_DIR_RELEASE     := build/release
BUILD_DIR_GUI_DEBUG   := build/gui-debug
BUILD_DIR_GUI_RELEASE := build/gui-release
CMAKE             := cmake
JOBS              := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
CC                := /usr/bin/gcc
CXX               := /usr/bin/g++

BOLD  := \033[1m
CYAN  := \033[36m
RESET := \033[0m

.PHONY: default debug release clean debug-clean release-clean debug-run release-run \
       gui-debug gui-release gui-debug-clean gui-release-clean gui-debug-run gui-release-run gui-clean \
       kloc-count regression
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

# Configure and build Qt GUI in Debug mode
gui-debug:
	$(CMAKE) -B $(BUILD_DIR_GUI_DEBUG) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX) \
		-DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer" \
		-DENABLE_QT_UI=ON \
		-DENABLE_TESTS=ON
	$(CMAKE) --build $(BUILD_DIR_GUI_DEBUG) -j$(JOBS)

# Run the emulator with Qt GUI (debug build)
gui-debug-run: gui-debug
	$(BUILD_DIR_GUI_DEBUG)/jnext

# Remove GUI debug build directory
gui-debug-clean:
	rm -rf $(BUILD_DIR_GUI_DEBUG)

# Configure and build Qt GUI in Release mode
gui-release:
	$(CMAKE) -B $(BUILD_DIR_GUI_RELEASE) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX) \
		-DCMAKE_CXX_FLAGS="-O2 -DNDEBUG" \
		-DENABLE_QT_UI=ON \
		-DENABLE_TESTS=OFF
	$(CMAKE) --build $(BUILD_DIR_GUI_RELEASE) -j$(JOBS)

# Run the emulator with Qt GUI (release build)
gui-release-run: gui-release
	$(BUILD_DIR_GUI_RELEASE)/jnext

# Remove GUI release build directory
gui-release-clean:
	rm -rf $(BUILD_DIR_GUI_RELEASE)

# Remove all GUI build directories
gui-clean: gui-debug-clean gui-release-clean

# Remove all build directories
clean: debug-clean release-clean gui-clean

# Run the full regression test suite (FUSE Z80 opcodes + screenshot tests)
regression:
	bash test/regression.sh

# Count lines of code (excluding comments and blanks), per directory and total
kloc-count:
	printf "\n$(BOLD)Lines of code (excluding comments and blank lines):$(RESET)\n\n"
	total=0; \
	for dir in src/core src/cpu src/memory src/video src/audio src/port src/peripheral src/input src/platform src/debug src/debugger src/gui src/save test; do \
		if [ -d "$$dir" ]; then \
			count=$$(find $$dir -name '*.cpp' -o -name '*.h' | xargs grep -v '^\s*$$' | grep -v '^\s*//' | grep -v '^\s*/\*' | grep -v '^\s*\*' | wc -l); \
			printf "  $(CYAN)$(BOLD)%-30s$(RESET) %6d\n" "$$dir" "$$count"; \
			total=$$((total + count)); \
		fi; \
	done; \
	printf "\n  $(BOLD)%-30s %6d$(RESET)\n\n" "TOTAL" "$$total"
