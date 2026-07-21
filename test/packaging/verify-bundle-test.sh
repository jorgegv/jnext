#!/usr/bin/env bash
#
# Contract tests for packaging/macos/verify-bundle.sh — the GH #46 gate that
# decides whether a jnext.app is self-contained.
#
# That gate is the ONLY thing standing between a deployment tool silently
# missing a library and another .dmg that aborts in dyld on every Mac but the
# builder's, and it cannot be exercised on the Linux dev host because it needs
# otool. So it is tested here against STUBBED otool/file: the stubs let the
# script's decision logic run anywhere, which is what these tests pin. They do
# NOT prove otool's real output parses correctly — only a real Mac run does
# that (`make package-macos`, or the macos-build workflow).
#
# A fake bundle is a directory of files; a file is "Mach-O" to the stubs iff a
# sidecar <file>.deps exists, whose lines are that file's otool -L dependencies.
#
# Invoked by test/packaging/packaging-test.sh (`make package-test`).
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root

SCRIPT=packaging/macos/verify-bundle.sh
RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %-22s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-22s %s\n" "$1" "$2"; fail=$((fail+1)); }

# --- stubs -------------------------------------------------------------------
mkdir -p "$WORK/bin"

cat >"$WORK/bin/otool" <<'STUB'
#!/usr/bin/env bash
# otool -L <file>: header line, then one tab-indented dependency per line.
f=$2
echo "$f:"
while IFS= read -r line; do
    printf '\t%s (compatibility version 1.0.0, current version 1.0.0)\n' "$line"
done < "$f.deps"
STUB

cat >"$WORK/bin/file" <<'STUB'
#!/usr/bin/env bash
# file -b <path>: Mach-O iff the sidecar exists.
if [ -f "$2.deps" ]; then
    echo "Mach-O 64-bit executable arm64"
else
    echo "ASCII text"
fi
STUB

chmod +x "$WORK/bin/otool" "$WORK/bin/file"
export PATH="$WORK/bin:$PATH"

# --- helpers -----------------------------------------------------------------
# macho <app> <relative-path> <dep>...
macho() {
    local app=$1 rel=$2; shift 2
    mkdir -p "$app/$(dirname "$rel")"
    echo "fake mach-o" > "$app/$rel"
    printf '%s\n' "$@" > "$app/$rel.deps"
}

run() { bash "$SCRIPT" "$1" >"$WORK/out.log" 2>&1; echo $?; }

printf "${BOLD}=== verify-bundle.sh contract tests ===${RESET}\n\n"

# --- VB-01: a fully deployed bundle passes -----------------------------------
APP=$WORK/good.app
macho "$APP" Contents/MacOS/jnext \
    '@executable_path/../Frameworks/libpng16.16.dylib' \
    '@rpath/QtCore.framework/Versions/A/QtCore' \
    '/usr/lib/libSystem.B.dylib' \
    '/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa'
macho "$APP" Contents/Frameworks/libpng16.16.dylib \
    '@executable_path/../Frameworks/libpng16.16.dylib' \
    '/usr/lib/libz.1.dylib'
rc=$(run "$APP")
if [ "$rc" -eq 0 ]; then ok VB-01 "deployed bundle accepted"
else bad VB-01 "expected 0, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-02: the reported defect is rejected ----------------------------------
# The exact GH #46 dependency, on the main executable.
APP=$WORK/homebrew.app
macho "$APP" Contents/MacOS/jnext \
    '/opt/homebrew/opt/libpng/lib/libpng16.16.dylib' \
    '/usr/lib/libSystem.B.dylib'
rc=$(run "$APP")
if [ "$rc" -ne 0 ] && grep -q 'libpng16' "$WORK/out.log"; then
    ok VB-02 "/opt/homebrew dependency rejected"
else bad VB-02 "expected non-zero naming the dep, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-03: /usr/local (Intel Homebrew) and MacPorts are rejected too --------
APP=$WORK/usrlocal.app
macho "$APP" Contents/MacOS/jnext '/usr/local/opt/sdl2/lib/libSDL2-2.0.0.dylib'
rc=$(run "$APP")
if [ "$rc" -ne 0 ]; then ok VB-03 "/usr/local dependency rejected"
else bad VB-03 "expected non-zero, got $rc"; fi

# --- VB-04: a violation in a NESTED file is caught ---------------------------
# The main executable is clean; only a bundled Qt plugin is not. A checker that
# looked at the executable alone would pass this and ship a broken .dmg.
APP=$WORK/nested.app
macho "$APP" Contents/MacOS/jnext '/usr/lib/libSystem.B.dylib'
macho "$APP" Contents/PlugIns/platforms/libqcocoa.dylib \
    '/opt/homebrew/opt/qt@6/lib/QtGui.framework/Versions/A/QtGui'
rc=$(run "$APP")
if [ "$rc" -ne 0 ] && grep -q 'libqcocoa' "$WORK/out.log"; then
    ok VB-04 "violation in a nested plugin caught"
else bad VB-04 "expected non-zero naming the plugin, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-05: an empty bundle FAILS rather than vacuously passing --------------
# Without this, a packaging change that stops producing the .app would report
# "OK — no references outside the bundle" and the gate would be worthless.
APP=$WORK/empty.app
mkdir -p "$APP/Contents/MacOS"
rc=$(run "$APP")
if [ "$rc" -ne 0 ]; then ok VB-05 "empty bundle rejected, not a vacuous pass"
else bad VB-05 "expected non-zero, got 0 — the gate can pass on nothing"; fi

# --- VB-06: a missing bundle is an error -------------------------------------
rc=$(run "$WORK/does-not-exist.app")
if [ "$rc" -ne 0 ]; then ok VB-06 "missing bundle rejected"
else bad VB-06 "expected non-zero, got 0"; fi

# --- VB-07: wrong usage exits 2 ----------------------------------------------
bash "$SCRIPT" >/dev/null 2>&1; rc=$?
if [ "$rc" -eq 2 ]; then ok VB-07 "no argument exits 2"
else bad VB-07 "expected 2, got $rc"; fi

printf "\n${BOLD}Total: %d pass, %d fail${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
