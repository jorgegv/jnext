#!/usr/bin/env bash
# Self-test for the build system's CONFIGURE FRESHNESS guarantees — that an
# already-configured build dir still builds the sources that are on disk NOW.
# Prerequisite of `make unit-test` (see Makefile). Two halves, and they are
# the same subject because the second exists as a consequence of the first:
#
#   phases 1-6  cmake-configure-guard.sh, the shared guard gui-release and
#               sdl-release use to SKIP re-running `cmake -B` on an
#               already-configured build dir (#141).
#   phases 7-9  file(GLOB ... CONFIGURE_DEPENDS), which is what keeps the
#               source list fresh once that reconfigure is being skipped.
#               Before #141 the two release targets re-ran `cmake -B` every
#               invocation, which re-evaluated every glob as a side effect
#               and accidentally hid the staleness; build/ (unit-test-build)
#               always had the guard and so always had the hazard. It shipped:
#               GH #252's src/peripheral/joy_uart_link.cpp arrived in a merge
#               and the build died on `undefined reference to
#               JoyUartLink::last_error()`, a symptom a long way from its
#               cause, until `cmake -B build` was forced by hand.
#
# Exercises the EXACT defect the #141 review found, against REAL cmake/gcc/
# g++ (a stub would only prove our own control flow, not that we correctly
# route around CMake's actual behavior — the defect IS real CMake behavior,
# not ours): a CMAKE_C_COMPILER/CMAKE_CXX_COMPILER change must wipe the
# build dir and reconfigure fresh, never trust an in-place `cmake -B`, which
# was observed to silently drop CMAKE_BUILD_TYPE/CMAKE_CXX_FLAGS/
# ENABLE_QT_UI/ENABLE_TESTS on the SAME invocation and to not self-heal even
# on a later, fully-correct invocation.
#
# Fast: no jnext build, no Qt/SDL — a 3-line throwaway CMakeLists.txt project
# configures against real gcc/g++ in ~1s per call, 4 calls total.
#
# Phase 6 covers a SEPARATE #141-review finding: gui-release/sdl-release's
# own `-D...` arguments after the "--" (not this script's assertions, which
# were always quoted) were passed unquoted in the Makefile, so a compiler
# path containing a space would word-split before cmake ever saw it as one
# argument. Fixed by quoting each "-Dkey=value" as its own shell word. That
# defect lives in the MAKEFILE RECIPE TEXT, not in this script (which always
# received its cmake_args as a correctly-quoted bash array — `"$@"` never
# word-splits), so phase 6 checks the Makefile directly via `make -n`
# (prints the exact recipe text Make would hand to the shell; no cmake
# invoked, no build dir touched) rather than re-testing this script.
set -euo pipefail

SELFTEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SELFTEST_DIR/.." && pwd)
GUARD="$SELFTEST_DIR/cmake-configure-guard.sh"

FAIL=0
check() {  # $1 = description  $2 = actual  $3 = expected
	if [ "$2" = "$3" ]; then
		printf "  PASS  %s\n" "$1"
	else
		printf "  FAIL  %s (got '%s', want '%s')\n" "$1" "$2" "$3"
		FAIL=1
	fi
}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(guardselftest C CXX)
option(ENABLE_QT_UI "" OFF)
option(ENABLE_TESTS "" ON)
EOF

REAL_CC=$(command -v gcc)
REAL_CXX=$(command -v g++)
mkdir -p "$WORK/altcxx"
ln -s "$REAL_CXX" "$WORK/altcxx/g++"
ALT_CXX="$WORK/altcxx/g++"

BUILD_ABS="$WORK/build"

# Mirrors gui-release: relative build dir, CWD = the source dir (cmake
# defaults -S to CWD when not given, exactly like the real Makefile targets).
configure() {  # $1 = CXX path to assert/pass this call
	local cxx="$1"
	(
		cd "$WORK"
		bash "$GUARD" "build" \
			"CMAKE_BUILD_TYPE=Release" \
			"CMAKE_C_COMPILER=$REAL_CC" \
			"CMAKE_CXX_COMPILER=$cxx" \
			"CMAKE_CXX_FLAGS=-O2 -DNDEBUG" \
			"ENABLE_QT_UI=ON" \
			"ENABLE_TESTS=OFF" \
			-- \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_C_COMPILER="$REAL_CC" \
			-DCMAKE_CXX_COMPILER="$cxx" \
			-DCMAKE_CXX_FLAGS="-O2 -DNDEBUG" \
			-DENABLE_QT_UI=ON \
			-DENABLE_TESTS=OFF
	) > "$WORK/cmake.out" 2>&1
}

cache_val() {  # $1 = key
	grep "^$1:" "$BUILD_ABS/CMakeCache.txt" 2>/dev/null | head -1 | sed 's/^[^=]*=//'
}

echo "cmake-configure-guard-selftest:"

echo "  -- 1: fresh configure --"
configure "$REAL_CXX"
check "fresh: BUILD_TYPE"   "$(cache_val CMAKE_BUILD_TYPE)"   "Release"
check "fresh: CXX_FLAGS"    "$(cache_val CMAKE_CXX_FLAGS)"    "-O2 -DNDEBUG"
check "fresh: ENABLE_QT_UI" "$(cache_val ENABLE_QT_UI)"       "ON"
check "fresh: ENABLE_TESTS" "$(cache_val ENABLE_TESTS)"       "OFF"
check "fresh: CXX_COMPILER" "$(cache_val CMAKE_CXX_COMPILER)" "$REAL_CXX"

echo "  -- 2: no-op must skip cmake entirely (empty output) --"
: > "$WORK/cmake.out"
configure "$REAL_CXX"
check "no-op: cmake was not invoked" "$(cat "$WORK/cmake.out")" ""

echo "  -- 3: compiler switch must wipe + fully reconfigure, not silently drop the rest --"
configure "$ALT_CXX"
check "altcxx: BUILD_TYPE survives"   "$(cache_val CMAKE_BUILD_TYPE)"   "Release"
check "altcxx: CXX_FLAGS survives"    "$(cache_val CMAKE_CXX_FLAGS)"    "-O2 -DNDEBUG"
check "altcxx: ENABLE_QT_UI survives" "$(cache_val ENABLE_QT_UI)"       "ON"
check "altcxx: ENABLE_TESTS survives" "$(cache_val ENABLE_TESTS)"       "OFF"
check "altcxx: CXX_COMPILER updated"  "$(cache_val CMAKE_CXX_COMPILER)" "$ALT_CXX"

echo "  -- 4: switching back must recover fully too (symmetry, no permanent stuck state) --"
configure "$REAL_CXX"
check "back: BUILD_TYPE"   "$(cache_val CMAKE_BUILD_TYPE)"   "Release"
check "back: CXX_FLAGS"    "$(cache_val CMAKE_CXX_FLAGS)"    "-O2 -DNDEBUG"
check "back: ENABLE_QT_UI" "$(cache_val ENABLE_QT_UI)"       "ON"
check "back: ENABLE_TESTS" "$(cache_val ENABLE_TESTS)"       "OFF"
check "back: CXX_COMPILER" "$(cache_val CMAKE_CXX_COMPILER)" "$REAL_CXX"

echo "  -- 5: no-op after recovery must also skip cmake --"
: > "$WORK/cmake.out"
configure "$REAL_CXX"
check "no-op after recovery: cmake was not invoked" "$(cat "$WORK/cmake.out")" ""

echo "  -- 6: gui-release/sdl-release's own -D args stay one shell word each (space-safe) --"
SPACE_CXX="a weird/path with spaces/g++"
for target in gui-release sdl-release; do
	recipe=$(cd "$REPO_ROOT" && LANG=C make -n "CXX=$SPACE_CXX" "$target" 2>&1)
	check "$target: -DCMAKE_CXX_COMPILER is one quoted shell word" \
		"$(printf '%s\n' "$recipe" | grep -cF "\"-DCMAKE_CXX_COMPILER=$SPACE_CXX\"")" \
		"1"
done

# ---------------------------------------------------------------------------
# Phases 7-8: file(GLOB ... CONFIGURE_DEPENDS) against REAL cmake/g++.
#
# Same reasoning as phases 1-5: the behaviour under test is CMake's, not ours,
# so a stub would only prove our own control flow. The fixture reproduces the
# GH #252 shape exactly — a new .cpp and its caller arriving together, into a
# build dir that is already configured AND already built — and the rebuild is
# `cmake --build` ALONE. No `cmake -B`, because "the build step alone notices"
# is the entire property.
#
# Phase 8 is the negative control and is not optional: without it, phase 7
# passes just as happily against a build system that reconfigures for some
# unrelated reason, and would prove nothing about CONFIGURE_DEPENDS.
# ---------------------------------------------------------------------------
GLOBWORK="$WORK/globtest"

# $1 = "with" | "without" — whether the fixture's glob carries CONFIGURE_DEPENDS
glob_fixture() {
	local kw=""
	[ "$1" = "with" ] && kw="CONFIGURE_DEPENDS "
	rm -rf "$GLOBWORK"
	mkdir -p "$GLOBWORK/src"
	cat > "$GLOBWORK/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.16)
project(globselftest CXX)
file(GLOB_RECURSE LIBSRC ${kw}"src/*.cpp")
add_library(globlib STATIC \${LIBSRC})
add_executable(globmain main.cpp)
target_link_libraries(globmain globlib)
EOF
	# A static lib + an executable that references INTO it: the same shape as
	# jnext's src/<subsystem> libs and src/main.cpp, so the failure mode is the
	# same undefined reference rather than a merely-uncompiled file.
	cat > "$GLOBWORK/src/present.cpp" <<'EOF'
int present_symbol() { return 0; }
EOF
	cat > "$GLOBWORK/main.cpp" <<'EOF'
int present_symbol();
int main() { return present_symbol(); }
EOF
}

# Add the new source AND its caller, exactly as a merge would.
glob_add_source() {
	cat > "$GLOBWORK/src/added_later.cpp" <<'EOF'
int added_symbol() { return 0; }
EOF
	cat > "$GLOBWORK/main.cpp" <<'EOF'
int present_symbol();
int added_symbol();
int main() { return present_symbol() + added_symbol(); }
EOF
}

glob_configure() { cmake -B "$GLOBWORK/build" -S "$GLOBWORK" \
	-DCMAKE_CXX_COMPILER="$REAL_CXX" > "$WORK/glob.out" 2>&1; }
# The crux: BUILD only. Never `cmake -B` here.
glob_build() { cmake --build "$GLOBWORK/build" > "$WORK/glob.out" 2>&1; }

for mode in with without; do
	if [ "$mode" = "with" ]; then
		echo "  -- 7: a new .cpp must be picked up by a build-only rebuild (CONFIGURE_DEPENDS) --"
		want="built"; label="picked up"
	else
		echo "  -- 8: negative control — the same fixture WITHOUT it must NOT be picked up --"
		want="failed"; label="stays invisible"
	fi

	# Classify the OUTCOME, never a specific exit status: `cmake --build`
	# forwards the generator's code (make exits 2 on a build failure, not 1).
	outcome() { if "$@"; then echo built; else echo failed; fi; }

	glob_fixture "$mode"
	set +e
	glob_configure
	rc_first=$(outcome glob_build)
	set -e
	check "$mode: fixture builds clean before the new source" "$rc_first" "built"

	glob_add_source
	set +e
	rc=$(outcome glob_build)
	set -e
	check "$mode: new source $label on a build-only rebuild" "$rc" "$want"

	if [ "$mode" = "with" ]; then
		# Not just "the build succeeded" — the object for the new file must
		# actually exist, i.e. it really was compiled rather than elided.
		found=$(find "$GLOBWORK/build" -name 'added_later.cpp.o' | wc -l)
		check "with: added_later.cpp really was compiled" "$found" "1"
	else
		check "without: failure is the undefined reference, not something else" \
			"$(grep -c 'undefined reference to .added_symbol' "$WORK/glob.out")" "1"
	fi
done

# ---------------------------------------------------------------------------
# Phase 9: every FIRST-PARTY file(GLOB...) in the repo carries CONFIGURE_DEPENDS.
#
# Phases 7-8 prove the mechanism works; this proves we actually USE it — the
# part that rots, because a new subsystem directory gets made by copying an
# existing CMakeLists.txt, and a copy taken before this change carries the
# bare glob forward in silence.
#
# SCOPE is the repo INDEX minus third_party/, which is exactly "the CMake
# files this project maintains". Two consequences worth stating, because the
# mutation test of this phase found the comment easy to get wrong:
#
#   * Index-scoped means generated CMake files under build/ never enter, so
#     the result does not depend on which build dirs happen to exist.
#   * third_party/spdlog is a git SUBMODULE — one gitlink entry, no file
#     content in this index — so its five ide.cmake globs are already out of
#     scope by the submodule boundary, NOT by the filter below. They are
#     upstream's file and pinning them would turn a submodule bump into a
#     false failure. The filter earns its keep for the trees that ARE tracked
#     directly here (third_party/{zot,fatfs,fuse-z80}): ours to edit, but
#     drop-in vendored code that lists sources explicitly and is deliberately
#     not held to our conventions.
# ---------------------------------------------------------------------------
echo "  -- 9: no first-party file(GLOB) may omit CONFIGURE_DEPENDS --"

# No xargs: it needs -r (GNU) to not run with an empty list, which this must
# survive — an empty list is precisely the vacuous-pass case guarded below.
( cd "$REPO_ROOT" && git ls-files -- '*CMakeLists.txt' '*.cmake' ) \
	| grep -v '^third_party/' > "$WORK/cmake-files.txt" || true

all_globs=""
while IFS= read -r f; do
	[ -n "$f" ] || continue
	hits=$(grep -nE '^[[:space:]]*file\([[:space:]]*GLOB' "$REPO_ROOT/$f") || true
	[ -n "$hits" ] && all_globs="$all_globs$(printf '%s\n' "$hits" | sed "s|^|$f:|")
"
done < "$WORK/cmake-files.txt"

bad_globs=$(printf '%s' "$all_globs" | grep . | grep -v CONFIGURE_DEPENDS) || true
bad_count=$(printf '%s' "$bad_globs" | grep -c .) || true
good_count=$(printf '%s' "$all_globs" | grep -c CONFIGURE_DEPENDS) || true

if [ -n "$bad_globs" ]; then
	printf '%s\n' "$bad_globs" | sed 's/^/        /'
fi
check "no first-party glob without CONFIGURE_DEPENDS" "$bad_count" "0"

# A lower bound, not an exact pin: a NEW glob is already covered by the
# assertion above, so an exact count would only add churn on every legitimate
# addition. What this defends against is the check passing VACUOUSLY — a wrong
# ls-files pattern, or a run from outside a git checkout, would otherwise scan
# nothing and report a silent green. Mutation-tested: breaking the ls-files
# pattern must reach THIS line and fail it, not abort the script earlier.
check "the scan is not vacuous (>=10 first-party globs found)" \
	"$([ "$good_count" -ge 10 ] && echo yes || echo no)" "yes"

if [ "$FAIL" -eq 1 ]; then
	echo "cmake-configure-guard-selftest: FAILED"
	exit 1
fi
echo "cmake-configure-guard-selftest: all checks passed"
