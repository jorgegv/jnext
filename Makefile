BUILD_DIR_DEBUG       := build/debug
BUILD_DIR_RELEASE     := build/release
BUILD_DIR_GUI_DEBUG   := build/gui-debug
BUILD_DIR_GUI_RELEASE := build/gui-release
CMAKE             := cmake
JOBS              := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
CC                := /usr/bin/gcc
CXX               := /usr/bin/g++

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
       kloc-count regression unit-test \
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

# Remove all build directories (debug/release/gui + unit-test)
clean: debug-clean release-clean gui-clean unit-test-clean

# Run the full regression test suite (FUSE Z80 opcodes + screenshot tests)
regression:
	bash test/regression.sh

# Run all subsystem unit tests in parallel (rebuilds test binaries first if sources changed)
unit-test: unit-test-build
	@BUILD=build; \
	TMPDIR=$$(mktemp -d); \
	SUMMARY=$$BUILD/test-summary.tsv; \
	rm -f $$SUMMARY; \
	TESTS="fuse_z80_test z80n_test rewind_test copper_test mmu_test nextreg_test \
	       nextreg_integration_test input_test input_integration_test ctc_test ctc_interrupts_test layer2_test \
	       uart_test uart_integration_test divmmc_test sdcard_test sprites_test compositor_test ula_test ula_integration_test \
	       floating_bus_test videotiming_test contention_test port_test audio_test audio_nextreg_test dma_test tilemap_test"; \
	for t in $$TESTS; do \
		bin="$$BUILD/test/$$t"; \
		if [ ! -x "$$bin" ]; then continue; fi; \
		( \
			case $$t in \
				fuse_z80_test) $$bin $$BUILD/test/fuse ;; \
				z80n_test)     $$bin $$BUILD/test/z80n  ;; \
				*)             $$bin                    ;; \
			esac >$$TMPDIR/$$t.out 2>&1; \
			echo $$? >$$TMPDIR/$$t.rc \
		) & \
	done; \
	wait; \
	printf "\n$(BOLD)Subsystem unit test results:$(RESET)\n\n"; \
	suites_pass=0; suites_fail=0; suites_skip=0; \
	sum_total=0; sum_passed=0; sum_failed=0; sum_skipped=0; \
	for t in $$TESTS; do \
		rc_file="$$TMPDIR/$$t.rc"; \
		if [ ! -f "$$rc_file" ]; then \
			printf "  $(CYAN)%-34s$(RESET) $(BADGE_SKIP) SKIP $(RESET)  (not built)\n" "$$t"; \
			suites_skip=$$((suites_skip + 1)); \
			continue; \
		fi; \
		rc=$$(cat $$rc_file); \
		line=$$(grep '^Total:' $$TMPDIR/$$t.out | tail -1); \
		if [ -z "$$line" ]; then \
			t_total=0; t_passed=0; t_failed=0; t_skipped=0; \
		else \
			t_total=$$(echo "$$line" | sed 's/.*Total: *\([0-9]*\).*/\1/'); \
			t_passed=$$(echo "$$line" | sed 's/.*Passed: *\([0-9]*\).*/\1/'); \
			t_failed=$$(echo "$$line" | sed 's/.*Failed: *\([0-9]*\).*/\1/'); \
			t_skipped=$$(echo "$$line" | sed 's/.*Skipped: *\([0-9]*\).*/\1/'); \
		fi; \
		sum_total=$$((sum_total + t_total)); \
		sum_passed=$$((sum_passed + t_passed)); \
		sum_failed=$$((sum_failed + t_failed)); \
		sum_skipped=$$((sum_skipped + t_skipped)); \
		if [ -n "$$line" ]; then \
			printf "%s\t%s\t%s\t%s\t%s\n" "$$t" "$${t_total:-0}" "$${t_passed:-0}" "$${t_failed:-0}" "$${t_skipped:-0}" >> $$SUMMARY; \
		fi; \
		if [ $$rc -ne 0 ] || [ "$$t_failed" -gt 0 ] 2>/dev/null; then \
			printf "  $(CYAN)%-34s$(RESET) $(BADGE_FAIL) FAIL $(RESET)  %s\n" "$$t" "$$line"; \
			suites_fail=$$((suites_fail + 1)); \
		elif [ "$$t_skipped" -gt 0 ] 2>/dev/null; then \
			printf "  $(CYAN)%-34s$(RESET) $(BADGE_SKIP) SKIP $(RESET)  %s\n" "$$t" "$$line"; \
			suites_pass=$$((suites_pass + 1)); \
		else \
			printf "  $(CYAN)%-34s$(RESET) $(BADGE_PASS) PASS $(RESET)  %s\n" "$$t" "$$line"; \
			suites_pass=$$((suites_pass + 1)); \
		fi; \
	done; \
	printf "\n$(BOLD)Total: %d  Passed: %d  Failed: %d  Skipped: %d$(RESET)\n" \
		$$sum_total $$sum_passed $$sum_failed $$sum_skipped; \
	printf "$(BOLD)Suites: %d pass, %d fail, %d skip$(RESET)\n\n" \
		$$suites_pass $$suites_fail $$suites_skip; \
	rm -rf $$TMPDIR

# Configure + build the canonical build/ directory (prerequisite for unit-test)
unit-test-build:
	@if [ ! -f build/CMakeCache.txt ]; then \
		$(CMAKE) -B build -S . \
			-DCMAKE_C_COMPILER=$(CC) \
			-DCMAKE_CXX_COMPILER=$(CXX); \
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
