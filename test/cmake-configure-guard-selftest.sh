#!/usr/bin/env bash
# Self-test for cmake-configure-guard.sh, the shared guard gui-release and
# sdl-release use to skip re-running `cmake -B` on an already-configured
# build dir (#141). Prerequisite of `make unit-test` (see Makefile).
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

if [ "$FAIL" -eq 1 ]; then
	echo "cmake-configure-guard-selftest: FAILED"
	exit 1
fi
echo "cmake-configure-guard-selftest: all checks passed"
