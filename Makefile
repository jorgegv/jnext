BUILD_DIR_DEBUG       := build/debug
BUILD_DIR_RELEASE     := build/release
BUILD_DIR_GUI_DEBUG   := build/gui-debug
BUILD_DIR_GUI_RELEASE := build/gui-release
CMAKE             := cmake
JOBS              := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
CC                := /usr/bin/gcc
CXX               := /usr/bin/g++

# Every recipe runs in the C locale. Two reasons, both about not lying:
#  - builds: compiler/linker diagnostics stay in English (greppable, quotable).
#  - tests:  a test's verdict must depend on the code, not on the user's LANG.
#    Qt's QApplication calls setlocale(LC_ALL, "") on construction, which localises
#    strerror() — screenshot-io-qt-func greps for "No such file or directory" and
#    duly FAILed on a Spanish desktop while its headless twin (no QApplication)
#    passed. LC_ALL overrides LANG, so this holds regardless of the caller's env.
export LC_ALL := C

# ANSI color palette (matches user prompt theme: 256-color mode)
RESET     := \033[0m
BOLD      := \033[1m
CYAN      := \033[36m
FG_BLACK  := \033[38;5;0m
FG_WHITE  := \033[38;5;15m
BG_PASS   := \033[48;5;42m
BG_SKIP   := \033[48;5;220m
BG_FAIL   := \033[48;5;161m
BADGE_PASS := $(FG_BLACK)$(BG_PASS)
BADGE_SKIP := $(FG_BLACK)$(BG_SKIP)
BADGE_FAIL := $(FG_WHITE)$(BG_FAIL)

.PHONY: default debug release clean debug-clean release-clean debug-run release-run \
       gui-debug gui-release gui-debug-clean gui-release-clean gui-debug-run gui-release-run gui-clean \
       unit-test-clean unit-test-build \
       kloc-count regression unit-test harness-selftest worktree-bootstrap bench \
       bump bump-patch bump-minor bump-major version
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
		-DCMAKE_CXX_FLAGS="-Og -g -fno-omit-frame-pointer" \
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

# Remove all build directories (debug/release/gui + unit-test)
clean: debug-clean release-clean gui-clean unit-test-clean

# Run the full regression test suite (screenshot + functional tests)
# Depends on unit-test-build: regression.sh runs build/test/rewind_test, and a
# `make clean` deletes it. It used to vanish from the suite with no row printed.
regression: unit-test-build
	bash test/00regression/regression.sh

# Run all subsystem unit tests in parallel (exactly those in test/unit-tests.conf)
unit-test: unit-test-build
	@bash test/run-unit-tests.sh build

# Self-test the unit-test harness: inject each fault, assert it refuses to run
harness-selftest:
	@bash test/harness-selftest.sh

# Benchmark the 5 canonical workloads on the fastest core (needs 'make gui-release' first)
bench:
	@bash test/bench/bench.sh

# build/jnext is the binary everyone GUI-verifies against, so it must be the Qt build
# CLAUDE.md mandates. This target used to configure build/ with NEITHER flag, and
# ENABLE_QT_UI defaults OFF — so `make clean` silently downgraded ./build/jnext to an
# SDL binary with no main window, and the next person to check the GUI found no window
# and concluded their change had broken it. That cost a previous author two capture
# runs. Qt6 was never optional here anyway: ENABLE_DEBUGGER already defaults ON and
# src/debugger does find_package(Qt6 REQUIRED). The Qt-less configuration keeps its
# coverage — `make release` / `make debug` still build exactly that.
#
# The else-branch: the `if` only fires on a fresh build/, so a build/ configured by
# hand with other flags would be reused in silence — the same trap, one step removed.
# Refuse to build on it rather than hand back a binary that isn't what it claims.
#
# Configure + build the canonical build/ directory (prerequisite for unit-test)
unit-test-build:
	@if [ ! -f build/CMakeCache.txt ]; then \
		$(CMAKE) -B build -S . \
			-DCMAKE_C_COMPILER=$(CC) \
			-DCMAKE_CXX_COMPILER=$(CXX) \
			-DENABLE_QT_UI=ON \
			-DENABLE_DEBUGGER=ON; \
	else \
		for flag in ENABLE_QT_UI ENABLE_DEBUGGER; do \
			if ! grep -q "^$$flag:BOOL=ON$$" build/CMakeCache.txt; then \
				printf "$(BADGE_FAIL) ERROR $(RESET) build/ is configured with $(BOLD)$$flag=OFF$(RESET).\n"; \
				printf "  ./build/jnext would not be the Qt binary this project mandates.\n"; \
				printf "  Run '$(BOLD)make clean$(RESET)' first, then retry.\n"; \
				exit 1; \
			fi; \
		done; \
	fi
	@$(CMAKE) --build build -j$(JOBS)

# Remove the canonical build/ directory (jnext + test binaries + CMake cache)
unit-test-clean:
	rm -rf build

# Run unit-test and refresh test/SUBSYSTEM-TESTS-STATUS.md from the summary TSV
unit-test-dashboard: unit-test
	@SUMMARY=build/test-summary.tsv; \
	if [ -s $$SUMMARY ] && [ -f test/SUBSYSTEM-TESTS-STATUS.md ]; then \
		if bash test/refresh-subsystem-status.sh $$SUMMARY test/SUBSYSTEM-TESTS-STATUS.md; then \
			printf "$(BOLD)Dashboard refreshed:$(RESET) test/SUBSYSTEM-TESTS-STATUS.md\n\n"; \
		else \
			printf "$(BOLD)Warning:$(RESET) dashboard refresh failed\n\n"; \
		fi; \
	else \
		printf "$(BOLD)Warning:$(RESET) dashboard refresh skipped (no $$SUMMARY — did unit-test produce any rows?)\n\n"; \
	fi; \
	rm -f $$SUMMARY

# Symlink the git-ignored roms/ fixtures from the main worktree into this one
# An agent worktree without them cannot run the unit tests or the regression
# suite (both need roms/nextzxos-1gb-fat32fix.img). No-op in the main worktree.
worktree-bootstrap:
	@main=$$(git worktree list --porcelain | head -1 | sed 's/^worktree //'); \
	here=$$(git rev-parse --show-toplevel); \
	if [ "$$main" = "$$here" ]; then \
		printf "$(BOLD)Main worktree — roms/ fixtures are real files here; nothing to do.$(RESET)\n"; \
		exit 0; \
	fi; \
	if [ ! -d "$$main/roms" ]; then \
		printf "$(BADGE_FAIL) ERROR $(RESET) main worktree has no roms/ at $$main/roms\n"; exit 1; \
	fi; \
	mkdir -p "$$here/roms"; \
	n=0; \
	for f in "$$main"/roms/*; do \
		b=$$(basename "$$f"); \
		if [ ! -e "$$here/roms/$$b" ]; then \
			ln -s "$$f" "$$here/roms/$$b"; \
			printf "  $(CYAN)linked$(RESET) roms/%s\n" "$$b"; \
			n=$$((n + 1)); \
		fi; \
	done; \
	printf "$(BOLD)worktree-bootstrap: %d fixture(s) linked from %s$(RESET)\n" "$$n" "$$main"

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

# Show current version
version:
	@ver=$$(grep '^version:' version.yaml | awk '{print $$2}'); \
	 printf "$(BOLD)jnext $$ver$(RESET)\n"

# Bump patch version (x.y.Z → x.y.Z+1)
bump-patch:
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
	   printf "$(BOLD)Error: uncommitted changes present. Commit or stash first.$(RESET)\n"; exit 1; \
	 fi
	@ver=$$(grep '^version:' version.yaml | awk '{print $$2}'); \
	 major=$$(echo $$ver | cut -d. -f1); \
	 minor=$$(echo $$ver | cut -d. -f2); \
	 patch=$$(echo $$ver | cut -d. -f3); \
	 patch=$$((patch + 1)); \
	 newver="$$major.$$minor.$$patch"; \
	 printf "version: $$newver\n" > version.yaml; \
	 git add version.yaml && git commit -m "chore: bump version to $$newver" && git tag "v$$newver"; \
	 printf "$(BOLD)Bumped to $$newver and tagged v$$newver$(RESET)\n"

# Bump minor version (x.Y.z → x.Y+1.0)
bump-minor:
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
	   printf "$(BOLD)Error: uncommitted changes present. Commit or stash first.$(RESET)\n"; exit 1; \
	 fi
	@ver=$$(grep '^version:' version.yaml | awk '{print $$2}'); \
	 major=$$(echo $$ver | cut -d. -f1); \
	 minor=$$(echo $$ver | cut -d. -f2); \
	 minor=$$((minor + 1)); \
	 newver="$$major.$$minor.0"; \
	 printf "version: $$newver\n" > version.yaml; \
	 git add version.yaml && git commit -m "chore: bump version to $$newver" && git tag "v$$newver"; \
	 printf "$(BOLD)Bumped to $$newver and tagged v$$newver$(RESET)\n"

# Bump major version (X.y.z → X+1.0.0)
bump-major:
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
	   printf "$(BOLD)Error: uncommitted changes present. Commit or stash first.$(RESET)\n"; exit 1; \
	 fi
	@ver=$$(grep '^version:' version.yaml | awk '{print $$2}'); \
	 major=$$(echo $$ver | cut -d. -f1); \
	 major=$$((major + 1)); \
	 newver="$$major.0.0"; \
	 printf "version: $$newver\n" > version.yaml; \
	 git add version.yaml && git commit -m "chore: bump version to $$newver" && git tag "v$$newver"; \
	 printf "$(BOLD)Bumped to $$newver and tagged v$$newver$(RESET)\n"

# Alias: bump → bump-minor
bump: bump-minor
