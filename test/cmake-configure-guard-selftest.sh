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
#   * An UNTRACKED CMakeLists.txt is likewise invisible here, and that is
#     accepted rather than worked around: CI and review only ever see
#     committed content, so a bad glob that is not in the index cannot reach
#     anyone but its author, and it starts being checked the moment it is
#     `git add`ed. Scanning the filesystem instead would drag in every
#     generated CMake file under every build dir, making the result depend on
#     which build dirs happen to exist.
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

# EXACT, in the same spirit as test/unit-tests.conf's pinned row counts: the
# number is the project's claim about how much this phase actually scans, and
# it moves only when someone means it to. Updating it when you add or remove a
# glob IS the point, not friction to be engineered away.
#
# A lower bound was tried first and REJECTED in review, because it is
# defeatable in exactly the way that matters. Narrowing the filter above to
# also drop test/ leaves every real CMakeLists.txt untouched, so bad_count
# stays 0 — and with a floor of 10 the count merely falls 18 -> 15 and the
# phase still reports green, having silently stopped looking at three real
# globs. That is the failure class this project's whole test-manifest doctrine
# exists for: a green result is only as good as its denominator.
#
# It also subsumes the vacuity guard the floor was written for: a wrong
# ls-files pattern, or a run from outside a git checkout, scans nothing and
# fails here on 0 != 18 rather than passing silently.
EXPECTED_FIRST_PARTY_GLOBS=18
check "exactly $EXPECTED_FIRST_PARTY_GLOBS first-party globs scanned" \
	"$good_count" "$EXPECTED_FIRST_PARTY_GLOBS"

# ---------------------------------------------------------------------------
# Phases 10-12: a source glob must never match the build's OWN GENERATED OUTPUT.
#
# The second edge of CONFIGURE_DEPENDS, and a direct consequence of it: because
# the glob is re-evaluated at BUILD time, it runs after the build has written
# its generated sources to disk. In an IN-SOURCE build (cmake -S . -B .) the
# binary dir IS the source dir, so those files land inside the globbed
# directory and the glob matches them.
#
# It shipped red in the flatpak CI job at v1.0.12. AUTOMOC writes each
# moc_*.cpp into <target>_autogen/<hash>/ and aggregates them by #include into
# <target>_autogen/mocs_compilation.cpp — the only one it adds as a target
# source. The glob added the aggregated members as translation units of their
# own and every Q_OBJECT symbol was compiled twice:
#   multiple definition of `DebuggerWindow::staticMetaObject'
# Nothing here saw it because every jnext build dir (build/, build/gui-release,
# build/sdl-release) sits OUTSIDE src/. The flatpak jnext module has no
# `builddir: true`, so flatpak-builder's cmake-ninja default built it in-source
# at /run/build/jnext.
#
# The fixture is Qt-free ON PURPOSE, keeping this script's "no Qt/SDL, ~1s per
# call" property. It does not simulate moc; it reproduces moc's SHAPE with
# cmake -E: a custom command writes part.cpp and an aggregate.cpp that
# `#include`s it into a <target>_autogen/ directory of the globbed dir, and
# only the aggregate is a target source. That is precisely the arrangement that
# makes a second compilation of the part a duplicate symbol.
#
# TWO build passes, and that is not padding: pass 1 creates the generated files
# AFTER the glob check has already run, so pass 1 is green either way and a
# single-pass test proves nothing. Pass 2 is where the glob sees them.
# flatpak-builder runs `ninja` then `ninja install`, so it always takes both.
#
# The target is an executable rather than jnext's static libraries so the
# failure is deterministic: every object is linked, so a twice-compiled symbol
# is always a link error. Pulled from a static archive it would depend on
# member ordering deciding whether both the aggregate and the part get pulled.
# ---------------------------------------------------------------------------
GENWORK="$WORK/gentest"

# The fixture filters with the PROJECT'S OWN regex, read out of the root
# CMakeLists, never a copy of it pasted here. A copy would make phase 10 pass
# against a JNEXT_GENERATED_DIR_REGEX that had been changed to something that
# no longer matches an autogen directory — the exact defect returning, with
# phase 12 still green because the filter LINE is still there.
JNEXT_GEN_REGEX=$(sed -nE 's/^set\(JNEXT_GENERATED_DIR_REGEX "(.*)"[[:space:]]*$/\1/p' \
	"$REPO_ROOT/CMakeLists.txt")
check "the project's generated-dir regex was extracted (non-empty)" \
	"$([ -n "$JNEXT_GEN_REGEX" ] && echo yes || echo no)" "yes"

# $1 = "with" | "without" — whether the fixture filters generated dirs out
gen_fixture() {
	local flt=""
	[ "$1" = "with" ] && flt="list(FILTER SRC EXCLUDE REGEX \"$JNEXT_GEN_REGEX\")"
	rm -rf "$GENWORK"
	mkdir -p "$GENWORK/app"
	cat > "$GENWORK/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(genselftest CXX)
add_subdirectory(app)
EOF
	# CMAKE_CURRENT_BINARY_DIR == CMAKE_CURRENT_SOURCE_DIR here, because the
	# configure below is in-source. That identity is the whole hazard.
	cat > "$GENWORK/app/CMakeLists.txt" <<EOF
add_custom_command(
    OUTPUT \${CMAKE_CURRENT_BINARY_DIR}/genapp_autogen/aggregate.cpp
    COMMAND \${CMAKE_COMMAND} -E make_directory \${CMAKE_CURRENT_BINARY_DIR}/genapp_autogen
    COMMAND \${CMAKE_COMMAND} -E echo "int generated_symbol() { return 7; }" > \${CMAKE_CURRENT_BINARY_DIR}/genapp_autogen/part.cpp
    COMMAND \${CMAKE_COMMAND} -E echo "#include \\"part.cpp\\"" > \${CMAKE_CURRENT_BINARY_DIR}/genapp_autogen/aggregate.cpp
    VERBATIM)
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS "*.cpp")
$flt
add_executable(genmain \${SRC} \${CMAKE_CURRENT_BINARY_DIR}/genapp_autogen/aggregate.cpp)
EOF
	cat > "$GENWORK/app/main.cpp" <<'EOF'
int generated_symbol();
int main() { return generated_symbol(); }
EOF
}

# IN-SOURCE on purpose: -S and -B are the same directory.
gen_configure() { cmake -S "$GENWORK" -B "$GENWORK" \
	-DCMAKE_CXX_COMPILER="$REAL_CXX" > "$WORK/gen.out" 2>&1; }
gen_build() { cmake --build "$GENWORK" > "$WORK/gen.out" 2>&1; }

for mode in with without; do
	if [ "$mode" = "with" ]; then
		echo "  -- 10: in-source build must not compile generated aggregate members twice --"
	else
		echo "  -- 11: negative control — the same fixture WITHOUT the filter must break --"
	fi

	outcome() { if "$@"; then echo built; else echo failed; fi; }

	gen_fixture "$mode"
	set +e
	gen_configure
	rc1=$(outcome gen_build)
	set -e
	# Pass 1 is green in BOTH modes: the generated files appear after the glob
	# has been checked. Asserted, so a fixture that failed early cannot be
	# mistaken for the defect in pass 2.
	check "$mode: pass 1 builds clean (files not yet globbable)" "$rc1" "built"

	set +e
	rc2=$(outcome gen_build)
	set -e
	parts=$(find "$GENWORK" -name 'part.cpp.o' | wc -l)

	if [ "$mode" = "with" ]; then
		check "with: pass 2 still builds clean" "$rc2" "built"
		check "with: the generated part was never compiled on its own" "$parts" "0"
		# Convergence: a third pass must be a genuine no-op, not an endless
		# reconfigure loop. The GLOB re-check itself still fires once (CMake
		# compares the RAW glob, before list(FILTER)), so "it stops" is a
		# separate claim from "it links".
		set +e
		rc3=$(outcome gen_build)
		set -e
		check "with: pass 3 builds clean too (converges)" "$rc3" "built"
	else
		check "without: pass 2 fails" "$rc2" "failed"
		check "without: failure is the duplicate symbol, not something else" \
			"$(grep -c 'multiple definition of .generated_symbol' "$WORK/gen.out")" "1"
		check "without: the generated part really was compiled twice" "$parts" "1"
	fi
done

# ---------------------------------------------------------------------------
# Phase 12: every first-party file(GLOB_RECURSE ...) actually carries the
# filter. Phases 10-11 prove the mechanism; this proves we USE it, and it is
# the part that rots — a new subsystem CMakeLists.txt is made by copying an
# existing one, and a copy taken from the wrong place carries the gap forward
# in silence. Same scope and same reasoning as phase 9 (repo INDEX minus
# third_party/); see that phase's comment for why the index, not the
# filesystem.
#
# Pairing, not a bare count: the filter must name the SAME variable the glob
# just set, on the line immediately after it. A file that filters some other
# variable, or filters the right one twenty lines later with an add_library in
# between, is not protected.
#
# IMMEDIATELY AFTER is literal: put NOTHING between the two lines, not even a
# comment or a blank. A functionally harmless separator is reported as an
# unfiltered glob — a false positive on correct configuration, accepted because
# it fails loud on a green tree and can never mask a real gap. Loosening it to
# "somewhere below" is what would silently accept the twenty-lines-later case.
#
# Non-recursive file(GLOB ...) is deliberately out of scope: it cannot descend
# into a generated subdirectory. test/CMakeLists.txt's two `tests.*` data globs
# are the only ones, and they glob fixture data, not sources.
# ---------------------------------------------------------------------------
echo "  -- 12: every first-party GLOB_RECURSE must exclude generated dirs --"

check "JNEXT_GENERATED_DIR_REGEX is defined exactly once, in the root CMakeLists" \
	"$(grep -c '^set(JNEXT_GENERATED_DIR_REGEX ' "$REPO_ROOT/CMakeLists.txt")" "1"

unfiltered=""
recursive_globs=0
while IFS= read -r f; do
	[ -n "$f" ] || continue
	# Emit "<lineno>:<var>" for every GLOB_RECURSE in this file.
	while IFS= read -r hit; do
		[ -n "$hit" ] || continue
		lno=${hit%%:*}
		var=$(printf '%s' "${hit#*:}" | sed -E 's/^[[:space:]]*file\([[:space:]]*GLOB_RECURSE[[:space:]]+([A-Za-z_][A-Za-z0-9_]*).*/\1/')
		recursive_globs=$((recursive_globs + 1))
		next=$(sed -n "$((lno + 1))p" "$REPO_ROOT/$f")
		case "$next" in
			*"list(FILTER $var EXCLUDE REGEX \"\${JNEXT_GENERATED_DIR_REGEX}\")"*) ;;
			*) unfiltered="$unfiltered$f:$lno: $var
" ;;
		esac
	done <<-EOF
		$(grep -nE '^[[:space:]]*file\([[:space:]]*GLOB_RECURSE' "$REPO_ROOT/$f" || true)
	EOF
done < "$WORK/cmake-files.txt"

if [ -n "$unfiltered" ]; then
	printf '%s' "$unfiltered" | grep . | sed 's/^/        /'
fi
check "no first-party GLOB_RECURSE without the generated-dir filter" \
	"$(printf '%s' "$unfiltered" | grep -c .)" "0"

# EXACT for the same reason phase 9's count is exact: a green result is only as
# good as its denominator. Narrowing the file list would leave the unfiltered
# count at 0 while silently scanning less. Updating this when you add or remove
# a glob IS the point.
EXPECTED_RECURSIVE_GLOBS=16
check "exactly $EXPECTED_RECURSIVE_GLOBS first-party GLOB_RECURSE globs scanned" \
	"$recursive_globs" "$EXPECTED_RECURSIVE_GLOBS"

if [ "$FAIL" -eq 1 ]; then
	echo "cmake-configure-guard-selftest: FAILED"
	exit 1
fi
echo "cmake-configure-guard-selftest: all checks passed"
