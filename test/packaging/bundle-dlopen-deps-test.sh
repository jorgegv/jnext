#!/usr/bin/env bash
#
# Contract tests for packaging/macos/bundle-dlopen-deps.sh — the GH #46 fix that
# copies in libSDL3, the dependency NO dependency walk can discover.
#
# This script is the one that PERFORMS the fix, and no routine job runs it:
# ci.yml has no macOS leg, release.yml's macOS leg fires only on a pushed v* tag
# and is continue-on-error, and macos-build.yml is workflow_dispatch-only. A
# regression in the copy mechanism — a stale Homebrew path candidate, a broken
# shim heuristic, a wrong destination — would otherwise surface only when a
# human happened to run something by hand, i.e. after the .dmg shipped.
#
# Tested against a FABRICATED HOMEBREW_PREFIX tree and a stubbed
# install_name_tool, so it runs on the Linux dev host. As with
# verify-bundle-test.sh, that pins the script's DECISION LOGIC, not the parsing
# of any real Mach-O; only a real Mac run (`make package-macos`, or the
# macos-build workflow) proves that.
#
# Invoked by test/packaging/packaging-test.sh (`make package-test`).
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root

SCRIPT=packaging/macos/bundle-dlopen-deps.sh
RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %-22s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-22s %s\n" "$1" "$2"; fail=$((fail+1)); }

# --- stubs -------------------------------------------------------------------
# Only install_name_tool: everything else the script uses (find/grep/cp/chmod)
# is real and behaves identically here. Unlike verify-bundle.sh, this script
# genuinely invokes install_name_tool, so the stub is live, not decorative.
mkdir -p "$WORK/bin"
cat >"$WORK/bin/install_name_tool" <<'STUB'
#!/usr/bin/env bash
target=${*: -1}
echo "$*" >> "$target.int-calls"
exit 0
STUB
chmod +x "$WORK/bin/install_name_tool"
export PATH="$WORK/bin:$PATH"

# --- helpers -----------------------------------------------------------------
# shim <app>          — a bundled sdl2-compat SDL2 (mentions libSDL3 in its text)
shim() {
    mkdir -p "$1/Contents/Frameworks"
    printf 'fake mach-o SDL2 shim; dlopens libSDL3 by leaf name\n' \
        > "$1/Contents/Frameworks/libSDL2-2.0.0.dylib"
}
# classic <app>       — a real SDL2, no SDL3 anywhere in it
classic() {
    mkdir -p "$1/Contents/Frameworks"
    printf 'fake mach-o SDL2 2.30.0, the genuine article\n' \
        > "$1/Contents/Frameworks/libSDL2-2.0.0.dylib"
}
# brew_sdl3 <prefix>  — a fake Homebrew prefix that HAS sdl3 installed
PAYLOAD='REAL-SDL3-PAYLOAD-a13ddc00'
brew_sdl3() {
    mkdir -p "$1/opt/sdl3/lib"
    printf '%s\n' "$PAYLOAD" > "$1/opt/sdl3/lib/libSDL3.0.dylib"
}

run() { HOMEBREW_PREFIX="$2" bash "$SCRIPT" "$1" >"$WORK/out.log" 2>&1; echo $?; }

printf "${BOLD}=== bundle-dlopen-deps.sh contract tests ===${RESET}\n\n"

# --- BD-01: the shim + an available SDL3 -> the library is copied in ----------
# The GH #46 fix itself. `otool -L` on that SDL2 shows no SDL3 entry, so this
# copy is the only way libSDL3 ever reaches the bundle. Asserting the CONTENT
# (not merely a file of that name) pins both the source candidate list and the
# destination: a stale Homebrew path or a wrong Frameworks path fails here.
BREW=$WORK/brew-ok; brew_sdl3 "$BREW"
APP=$WORK/compat.app; shim "$APP"
rc=$(run "$APP" "$BREW")
dst=$APP/Contents/Frameworks/libSDL3.dylib
if [ "$rc" -eq 0 ] && [ -e "$dst" ] && grep -q "$PAYLOAD" "$dst" \
   && grep -q 'added libSDL3.dylib' "$WORK/out.log"; then
    ok BD-01 "sdl2-compat shim: libSDL3 copied into Contents/Frameworks"
else bad BD-01 "expected 0 and the real payload at $dst, got $rc: $(cat "$WORK/out.log")"; fi

# --- BD-02: a re-run with it already present is a no-op ----------------------
# The bundle is copied once per build but the script may run again over an
# existing tree; re-copying would churn the file and (after signing) invalidate
# the signature for nothing. A sentinel makes this discriminative: merely
# re-copying the same source would look identical, so the copy is detected by
# the sentinel being GONE.
printf 'SENTINEL-DO-NOT-OVERWRITE\n' > "$dst"
rc=$(run "$APP" "$BREW")
if [ "$rc" -eq 0 ] && grep -q 'SENTINEL-DO-NOT-OVERWRITE' "$dst" \
   && grep -q 'already present' "$WORK/out.log" \
   && grep -q '0 runtime-loaded' "$WORK/out.log"; then
    ok BD-02 "already-present libSDL3 left untouched"
else bad BD-02 "expected an idempotent no-op, got $rc: $(cat "$WORK/out.log")"; fi

# --- BD-03: the shim with NO SDL3 to be found is a HARD FAILURE --------------
# The whole point of the fix. Warning-and-continuing here would ship exactly the
# .dmg that GH #46 reported: every structural check green, aborts in a load-time
# initializer on the user's Mac. It must break the build instead.
BREW=$WORK/brew-empty; mkdir -p "$BREW/lib"
APP=$WORK/nosdl3.app; shim "$APP"
rc=$(run "$APP" "$BREW")
if [ "$rc" -eq 1 ] && grep -q 'libSDL3' "$WORK/out.log" \
   && grep -q 'brew install sdl3' "$WORK/out.log" \
   && [ ! -e "$APP/Contents/Frameworks/libSDL3.dylib" ]; then
    ok BD-03 "no SDL3 under HOMEBREW_PREFIX fails loud (GH #46)"
else bad BD-03 "expected exit 1 naming libSDL3, got $rc: $(cat "$WORK/out.log")"; fi

# --- BD-04: a classic, non-compat SDL2 pulls nothing in ----------------------
# The other direction. SDL3 IS installed in this prefix, so a script that
# stopped testing for the shim would happily copy it into every bundle — an
# unnecessary library, signed and shipped, on a build that never loads it.
BREW=$WORK/brew-ok
APP=$WORK/classic.app; classic "$APP"
rc=$(run "$APP" "$BREW")
if [ "$rc" -eq 0 ] && [ ! -e "$APP/Contents/Frameworks/libSDL3.dylib" ] \
   && grep -q '0 runtime-loaded' "$WORK/out.log"; then
    ok BD-04 "classic SDL2: nothing copied"
else bad BD-04 "expected 0 with no copy, got $rc: $(cat "$WORK/out.log")"; fi

printf "\n${BOLD}Total: %d pass, %d fail${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
