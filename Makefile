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
       bump bump-patch bump-minor bump-major version publish-release \
       package-src package-rpm package-deb package-flatpak package-win package-macos gui-release-win package-test
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

# Cross-build a Windows ZIP via Fedora MinGW (needs mingw64 toolchain + Qt6/SDL2)
# Cross-compile ONLY the Windows jnext.exe (Fedora MinGW; no packaging)
gui-release-win:
	@# mingw64-cmake ships in mingw64-filesystem and may be present without the
	@# actual cross toolchain/libraries. Check the cross gcc and mingw Qt6 too,
	@# so a missing package is a clear "install these" message, not a cryptic
	@# "compiler not found" from deep inside CMake's project() call.
	@if ! command -v mingw64-cmake >/dev/null 2>&1 \
	   || ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 \
	   || [ ! -f /usr/x86_64-w64-mingw32/sys-root/mingw/lib/cmake/Qt6/Qt6Config.cmake ]; then \
		printf "$(BADGE_FAIL) ERROR $(RESET) Fedora MinGW cross toolchain/libraries incomplete.\n"; \
		printf "  Install them all:\n"; \
		printf "  $(BOLD)sudo dnf install mingw64-gcc mingw64-gcc-c++ mingw64-qt6-qtbase \\\\\n"; \
		printf "    mingw64-sdl2-compat mingw64-curl mingw64-openssl mingw64-zlib \\\\\n"; \
		printf "    mingw64-libpng mingw64-winpthreads$(RESET)\n"; \
		printf "  (mingw64-filesystem supplies mingw64-cmake; native qt6-qtbase-devel supplies moc/rcc/uic.)\n"; \
		exit 1; \
	fi
	mingw64-cmake -S . -B $(PKG_BUILD_WIN) -DENABLE_QT_UI=ON -DENABLE_TESTS=OFF
	$(CMAKE) --build $(PKG_BUILD_WIN) -j$(JOBS)
	@# Bundle the Qt6/SDL2 runtime DLLs + Qt plugins next to the exe so it runs
	@# in place (jnext.exe alone can't start — missing Qt6Core.dll and, even with
	@# the DLLs, the platforms/qwindows.dll plugin).
	bash packaging/windows/bundle-dlls.sh $(PKG_BUILD_WIN)/jnext.exe $(PKG_BUILD_WIN)
	@printf "$(BOLD)Windows executable (+ bundled DLLs):$(RESET) $(PKG_BUILD_WIN)/jnext.exe\n"

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
	@# Loud, non-fatal drift guard: the dashboard only refreshes on the explicit
	@# 'unit-test-dashboard' target, so it silently rots. This warns (never fails —
	@# a stale doc must not break the parallel-worktree test flow) when regenerating
	@# from test/unit-tests.conf + the live summary would change the committed dashboard
	@# (stale-counts) OR a live suite is not declared in test/unit-tests.conf (no-row).
	@# Both the generator and this guard key off test/unit-tests.conf. Fix: make unit-test-dashboard.
	@if [ -s build/test-summary.tsv ] && [ -f test/SUBSYSTEM-TESTS-STATUS.md ]; then \
		tmp=$$(mktemp); cp test/SUBSYSTEM-TESTS-STATUS.md $$tmp; \
		bash test/refresh-subsystem-status.sh build/test-summary.tsv $$tmp >/dev/null 2>&1 || true; \
		drift=""; \
		diff -q $$tmp test/SUBSYSTEM-TESTS-STATUS.md >/dev/null 2>&1 || drift="stale-counts"; \
		miss=$$(cut -f1 build/test-summary.tsv | while read b; do grep -qE "^[?]?$$b[[:space:]]" test/unit-tests.conf || echo $$b; done | tr '\n' ' '); \
		[ -n "$$miss" ] && drift="$$drift no-row:[$$miss]"; \
		if [ -n "$$drift" ]; then \
			printf "\n$(BADGE_SKIP) DASHBOARD STALE $(RESET) $$drift\n"; \
			printf "  test/SUBSYSTEM-TESTS-STATUS.md is out of date — run '$(BOLD)make unit-test-dashboard$(RESET)' and commit.\n\n"; \
		fi; \
		rm -f $$tmp; \
	fi

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
	 rel=""; \
	 if [ -t 0 ]; then \
	   printf "Add v$$newver to releases.yaml (build a public GitHub Release)? [y/N] "; read ans || ans=n; \
	 else ans=n; fi; \
	 case "$$ans" in [yY]*) bash packaging/add-release.sh "v$$newver" && rel="releases.yaml" ;; *) : ;; esac && \
	 printf "version: $$newver\n" > version.yaml && \
	 bash packaging/sync-version.sh "$$newver" && \
	 git add version.yaml $$rel packaging/rpm/jnext.spec packaging/flatpak/io.github.zxjogv.jnext.yml \
	         packaging/assets/io.github.zxjogv.jnext.metainfo.xml packaging/debian/changelog && \
	 git commit -m "chore: bump version to $$newver" && git tag "v$$newver" && \
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
	 rel=""; \
	 if [ -t 0 ]; then \
	   printf "Add v$$newver to releases.yaml (build a public GitHub Release)? [y/N] "; read ans || ans=n; \
	 else ans=n; fi; \
	 case "$$ans" in [yY]*) bash packaging/add-release.sh "v$$newver" && rel="releases.yaml" ;; *) : ;; esac && \
	 printf "version: $$newver\n" > version.yaml && \
	 bash packaging/sync-version.sh "$$newver" && \
	 git add version.yaml $$rel packaging/rpm/jnext.spec packaging/flatpak/io.github.zxjogv.jnext.yml \
	         packaging/assets/io.github.zxjogv.jnext.metainfo.xml packaging/debian/changelog && \
	 git commit -m "chore: bump version to $$newver" && git tag "v$$newver" && \
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
	 rel=""; \
	 if [ -t 0 ]; then \
	   printf "Add v$$newver to releases.yaml (build a public GitHub Release)? [y/N] "; read ans || ans=n; \
	 else ans=n; fi; \
	 case "$$ans" in [yY]*) bash packaging/add-release.sh "v$$newver" && rel="releases.yaml" ;; *) : ;; esac && \
	 printf "version: $$newver\n" > version.yaml && \
	 bash packaging/sync-version.sh "$$newver" && \
	 git add version.yaml $$rel packaging/rpm/jnext.spec packaging/flatpak/io.github.zxjogv.jnext.yml \
	         packaging/assets/io.github.zxjogv.jnext.metainfo.xml packaging/debian/changelog && \
	 git commit -m "chore: bump version to $$newver" && git tag "v$$newver" && \
	 printf "$(BOLD)Bumped to $$newver and tagged v$$newver$(RESET)\n"

# Alias: bump → bump-minor
bump: bump-minor

# Publish the latest release: push commits, then push ONLY the newest tag alone
publish-release:
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
	   printf "$(BOLD)Error: uncommitted changes present. Commit or stash first.$(RESET)\n"; exit 1; \
	 fi
	@branch=$$(git rev-parse --abbrev-ref HEAD); \
	 tag=$$(git tag -l 'v*' | sort -V | tail -1); \
	 if [ -z "$$tag" ]; then printf "$(BOLD)Error: no v* tag found.$(RESET)\n"; exit 1; fi; \
	 public=no; \
	 tagre=$$(printf '%s' "$$tag" | sed 's/\./\\./g'); \
	 if grep -qE "^[[:space:]]*-[[:space:]]*$$tagre[[:space:]]*$$" releases.yaml; then public=yes; fi; \
	 onorigin=no; \
	 if git ls-remote --tags origin "refs/tags/$$tag" 2>/dev/null | grep -q .; then onorigin=yes; fi; \
	 printf "$(BOLD)Publish plan:$(RESET)\n"; \
	 printf "  push commits : origin %s\n" "$$branch"; \
	 printf "  push tag     : %s (in releases.yaml: %s)\n" "$$tag" "$$public"; \
	 if [ "$$public" != "yes" ]; then \
	   printf "  $(BOLD)WARNING:$(RESET) %s is NOT in releases.yaml — the CI gate will build NO public GitHub Release.\n" "$$tag"; \
	 fi; \
	 if [ "$$onorigin" = "yes" ]; then \
	   printf "  $(BOLD)WARNING:$(RESET) %s already on origin — re-pushing sends no create event and will NOT re-trigger a Release.\n" "$$tag"; \
	 fi; \
	 if [ -t 0 ]; then printf "Proceed? [y/N] "; read ans || ans=n; else ans=n; fi; \
	 case "$$ans" in [yY]*) : ;; *) printf "Aborted.\n"; exit 1 ;; esac; \
	 git push origin "$$branch" && \
	 git push origin "refs/tags/$$tag" && \
	 printf "$(BOLD)Pushed %s + tag %s. Single-tag push triggers the GH Release gate.$(RESET)\n" "$$branch" "$$tag"

# ---------------------------------------------------------------------------
# Packaging (Task 67 follow-up). Each target wraps the packaging inputs under
# packaging/ and CMake's install()/CPack config (see packaging/README.md).
# Linux native targets (src/rpm/deb) are verified on the Fedora dev host;
# win/macos/flatpak guard-and-exit when their tooling/platform is absent.
# ---------------------------------------------------------------------------

PKG_BUILD_RPM := build/package-rpm
PKG_BUILD_DEB := build/package-deb
PKG_BUILD_WIN := build/gui-release-win

# Build source packages: v<ver>.tar.gz (rpm/deb source) + jnext-<ver>-src.zip (release); vendors submodules
package-src:
	@mkdir -p build/dist
	bash packaging/make-dist-tarball.sh build/dist

# Build an RPM package via CPack (Fedora/RHEL); needs rpmbuild
package-rpm:
	$(CMAKE) -B $(PKG_BUILD_RPM) -S . \
		-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON -DENABLE_TESTS=OFF
	$(CMAKE) --build $(PKG_BUILD_RPM) -j$(JOBS)
	cd $(PKG_BUILD_RPM) && cpack -G RPM
	@printf "$(BOLD)RPM(s) produced:$(RESET)\n"; ls -1 $(PKG_BUILD_RPM)/*.rpm

# Build a DEB package via CPack (Debian/Ubuntu); dep autodetection is weak off-Debian
package-deb:
	$(CMAKE) -B $(PKG_BUILD_DEB) -S . \
		-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON -DENABLE_TESTS=OFF
	$(CMAKE) --build $(PKG_BUILD_DEB) -j$(JOBS)
	cd $(PKG_BUILD_DEB) && cpack -G DEB
	@printf "$(BOLD)DEB(s) produced:$(RESET)\n"; ls -1 $(PKG_BUILD_DEB)/*.deb

# Build a Flatpak bundle from packaging/flatpak (needs flatpak-builder + the
# org.kde.Platform//6.8 runtime and org.kde.Sdk//6.8 SDK the manifest targets)
package-flatpak:
	@if ! command -v flatpak-builder >/dev/null 2>&1; then \
		printf "$(BADGE_FAIL) ERROR $(RESET) flatpak-builder not found.\n"; \
		printf "  Install it first, e.g.: $(BOLD)sudo dnf install flatpak-builder$(RESET)\n"; \
		printf "  (or 'sudo apt install flatpak-builder'), then re-run 'make package-flatpak'.\n"; \
		exit 1; \
	fi
	@# The manifest builds against org.kde.Sdk//6.10 and runs on org.kde.Platform//6.10.
	@# Pre-check both so a missing runtime is a clear "install this" message rather
	@# than a cryptic "org.kde.Sdk/x86_64/6.10 not installed" from deep inside
	@# flatpak-builder (which is what the user hit).
	@miss=""; \
	 flatpak info org.kde.Platform//6.10 >/dev/null 2>&1 || miss="$$miss org.kde.Platform//6.10"; \
	 flatpak info org.kde.Sdk//6.10      >/dev/null 2>&1 || miss="$$miss org.kde.Sdk//6.10"; \
	 if [ -n "$$miss" ]; then \
		printf "$(BADGE_FAIL) ERROR $(RESET) Flatpak runtime/SDK missing:$$miss\n"; \
		printf "  Install from Flathub, then re-run 'make package-flatpak':\n"; \
		printf "  $(BOLD)flatpak install flathub$$miss$(RESET)\n"; \
		printf "  (add the remote first if needed: flatpak remote-add --if-not-exists \\\\\n"; \
		printf "    flathub https://flathub.org/repo/flathub.flatpakrepo)\n"; \
		exit 1; \
	 fi
	@# Build into a local repo, then export a single-file .flatpak bundle so
	@# this target yields a shippable artifact like package-rpm/deb/win do.
	rm -rf build/flatpak build/flatpak-repo
	flatpak-builder --force-clean --user --repo=build/flatpak-repo build/flatpak \
		packaging/flatpak/io.github.zxjogv.jnext.yml
	@ver=$$(grep '^version:' version.yaml | awk '{print $$2}'); \
	 bundle="build/jnext-$$ver-x86_64.flatpak"; \
	 rm -f "$$bundle"; \
	 flatpak build-bundle build/flatpak-repo "$$bundle" io.github.zxjogv.jnext; \
	 printf "$(BOLD)Flatpak bundle produced:$(RESET)\n"; ls -1 "$$bundle"

# Cross-compile + ZIP the Windows build (Fedora MinGW)
# gui-release-win already bundled the DLLs into $(PKG_BUILD_WIN); we stage a
# cleanly-named top-level layout (exe + DLLs + plugin subdirs + qt.conf + docs,
# no CMake build junk) and zip that. Not CPack -G ZIP: CPack would apply the
# Unix /usr install prefix, giving a broken usr/bin/jnext.exe layout on Windows.
package-win: gui-release-win
	@ver=$$(grep '^version:' version.yaml | awk '{print $$2}'); \
	 name="jnext-$$ver-windows-x64"; \
	 stage="$(PKG_BUILD_WIN)/dist/$$name"; \
	 rm -rf "$(PKG_BUILD_WIN)/dist"; mkdir -p "$$stage"; \
	 bash packaging/windows/bundle-dlls.sh $(PKG_BUILD_WIN)/jnext.exe "$$stage"; \
	 cp LICENSE USAGE.md "$$stage"/; \
	 rm -f "$(PKG_BUILD_WIN)/$$name.zip"; \
	 ( cd "$(PKG_BUILD_WIN)/dist" && zip -rq "../$$name.zip" "$$name" ); \
	 printf "$(BOLD)ZIP(s) produced:$(RESET)\n"; ls -1 $(PKG_BUILD_WIN)/*.zip

# The whole recipe is one shell invocation so the non-Darwin early-exit
# actually stops it — a bare `exit 0` on its own recipe line would only end
# that line and make would still run the build+cpack lines that follow.
#
# Build a macOS .dmg via CPack DragNDrop (Darwin only)
package-macos:
	@if [ "$$(uname -s)" != "Darwin" ]; then \
		printf "$(BADGE_SKIP) SKIP $(RESET) macOS packaging requires a Mac (or the GitHub Actions\n"; \
		printf "  macos-latest runner — see the macos job in .github/workflows/release.yml).\n"; \
		printf "  It cannot be produced on this $$(uname -s) host.\n"; \
		exit 0; \
	fi; \
	$(CMAKE) -B build/package-macos -S . \
		-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON -DENABLE_TESTS=OFF && \
	$(CMAKE) --build build/package-macos -j$(JOBS) && \
	( cd build/package-macos && cpack -G DragNDrop )

# Integration-test every package target (src/rpm/deb/win/flatpak) — tooling-guarded, macOS excluded
package-test:
	bash test/packaging/packaging-test.sh
