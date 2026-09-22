#!/usr/bin/env bash
# Shared guard used by `make gui-release` / `make sdl-release` to skip
# `cmake -B` (pure reconfigure, no compilation — 6-8s on an otherwise fully
# warm build) when the target build dir already has every flag the caller
# is about to pass. Self-tested by cmake-configure-guard-selftest.sh.
#
# #141 review found a real defect in the first version of this guard: it
# trusted `cmake -B` to correctly re-apply all `-D` values in place after a
# CMAKE_C_COMPILER/CMAKE_CXX_COMPILER mismatch. That trust is false. On a
# compiler-identity change, CMake prints "You have changed variables that
# require your cache to be deleted. Configure will be re-run..." and, in
# that internal restart, SILENTLY DROPS every other -D on the same
# invocation: CMAKE_BUILD_TYPE reverted to unset (defaulted to
# RelWithDebInfo), CMAKE_CXX_FLAGS emptied, ENABLE_QT_UI/ENABLE_TESTS
# reverted to their option() defaults — while CMAKE_C_COMPILER itself
# resolved through PATH to a ccache masquerade instead of the absolute path
# explicitly passed. Worse: the compiler cache entries can come back typed
# ":UNINITIALIZED=" instead of ":STRING=" with the SAME value, which a
# TYPE-anchored assertion then never matches again — permanently defeating
# the guard for that build dir with no error. Measured: a build dir left in
# this state did NOT self-heal even on a LATER invocation carrying every
# correct flag — only wiping the dir and reconfiguring fresh recovered it.
#
# So: this guard NEVER relies on in-place `cmake -B` across a compiler
# identity change — it wipes the build dir first, unconditionally, in that
# one case. Every other assertion is matched by VALUE only (ignoring the
# cache's TYPE tag), which is what survives the type-degradation above.
#
# Usage:
#   cmake-configure-guard.sh <build_dir> <KEY=VALUE>... -- <cmake -D args...>
set -euo pipefail

if [ "$#" -lt 1 ]; then
	echo "usage: cmake-configure-guard.sh <build_dir> <KEY=VALUE>... -- <cmake args...>" >&2
	exit 2
fi

build_dir=$1
shift

case "$build_dir" in
	"" | / | /*)
		echo "cmake-configure-guard.sh: refusing to operate on build_dir='$build_dir' (must be a non-empty relative path)" >&2
		exit 2
		;;
esac

assertions=()
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do
	assertions+=("$1")
	shift
done
if [ "$#" -eq 0 ]; then
	echo "cmake-configure-guard.sh: missing '--' separator before cmake args" >&2
	exit 2
fi
shift  # drop the "--"
cmake_args=("$@")

cache="$build_dir/CMakeCache.txt"

# $1 = cache key (no ":TYPE=" suffix), $2 = expected value. Matches the
# literal value after the FIRST '=' on the key's cache line, regardless of
# the TYPE tag cmake gave it — see the header comment for why that matters.
cache_has() {
	local line
	line=$(grep "^$1:" "$cache" 2>/dev/null | head -1)
	[ "x${line#*=}" = "x$2" ]
}

# Step 1: a compiler-identity mismatch is never repaired in place. Wipe the
# whole dir so the next `cmake -B` is a genuinely fresh configure, immune to
# CMake's own compiler-change restart cascade.
if [ -f "$cache" ]; then
	for a in "${assertions[@]}"; do
		key=${a%%=*}
		case "$key" in
			CMAKE_C_COMPILER | CMAKE_CXX_COMPILER)
				if ! cache_has "$key" "${a#*=}"; then
					rm -rf "$build_dir"
					break
				fi
				;;
		esac
	done
fi

# Step 2: reconfigure only if the dir is (now, or already was) unconfigured,
# or any assertion still doesn't hold.
need_configure=0
if [ ! -f "$cache" ]; then
	need_configure=1
else
	for a in "${assertions[@]}"; do
		if ! cache_has "${a%%=*}" "${a#*=}"; then
			need_configure=1
			break
		fi
	done
fi

if [ "$need_configure" -eq 1 ]; then
	cmake -B "$build_dir" "${cmake_args[@]}"
fi
