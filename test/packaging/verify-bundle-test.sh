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
# otool -D <file>: header line, then the install name (LC_ID_DYLIB) if any.
#   Real otool prints only the header for an executable, which is what the
#   absent sidecar models here.
f=$2
echo "$f:"
case "$1" in
-D)
    # Real otool -D exits 0 whether or not the file has an id.
    [ -f "$f.id" ] && cat "$f.id"
    exit 0
    ;;
-l)
    # Only the LC_RPATH load commands matter here; mimic otool's layout.
    if [ -f "$f.rpaths" ]; then
        while IFS= read -r rp; do
            printf '          cmd LC_RPATH\n'
            printf '         path %s (offset 12)\n' "$rp"
        done < "$f.rpaths"
    fi
    exit 0
    ;;
*)
    while IFS= read -r line; do
        printf '\t%s (compatibility version 1.0.0, current version 1.0.0)\n' "$line"
    done < "$f.deps"
    ;;
esac
STUB

cat >"$WORK/bin/file" <<'STUB'
#!/usr/bin/env bash
# `file <path>...` (batched, no -b): one "<path>: <desc>" line per input.
# Mach-O iff the sidecar exists. Also supports the -b single-file form.
if [ "$1" = "-b" ]; then
    if [ -f "$2.deps" ]; then echo "Mach-O 64-bit executable arm64"; else echo "ASCII text"; fi
    exit 0
fi
for f in "$@"; do
    if [ -f "$f.deps" ]; then echo "$f: Mach-O 64-bit executable arm64"
    else echo "$f: ASCII text"; fi
done
STUB

cat >"$WORK/bin/codesign" <<'STUB'
#!/usr/bin/env bash
# codesign --verify --deep --strict <app>: fails iff the app carries the marker
# file this test uses to model an invalidated signature.
app=${*: -1}
if [ -e "$app/.bad-signature" ]; then
    echo "$app: code object is not signed at all" >&2
    exit 1
fi
exit 0
STUB

chmod +x "$WORK/bin/otool" "$WORK/bin/file" "$WORK/bin/codesign"
export PATH="$WORK/bin:$PATH"

# --- helpers -----------------------------------------------------------------
# macho <app> <relative-path> <dep>...
macho() {
    local app=$1 rel=$2; shift 2
    mkdir -p "$app/$(dirname "$rel")"
    echo "fake mach-o" > "$app/$rel"
    printf '%s\n' "$@" > "$app/$rel.deps"
}

# install_id <app> <relative-path> <id>   — give a file an LC_ID_DYLIB
install_id() { printf '%s\n' "$3" > "$1/$2.id"; }

# rpaths <app> <relative-path> <rpath>...  — give a file LC_RPATH entries
rpaths() { local app=$1 rel=$2; shift 2; printf '%s\n' "$@" > "$app/$rel.rpaths"; }

run() { bash "$SCRIPT" "$1" >"$WORK/out.log" 2>&1; echo $?; }

printf "${BOLD}=== verify-bundle.sh contract tests ===${RESET}\n\n"

# --- VB-01: a fully deployed bundle passes -----------------------------------
APP=$WORK/good.app
macho "$APP" Contents/MacOS/jnext \
    '@executable_path/../Frameworks/libpng16.16.dylib' \
    '@rpath/QtCore.framework/Versions/A/QtCore' \
    '/usr/lib/libSystem.B.dylib' \
    '/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa'
rpaths "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks'
macho "$APP" Contents/Frameworks/libpng16.16.dylib \
    '@executable_path/../Frameworks/libpng16.16.dylib' \
    '/usr/lib/libz.1.dylib'
macho "$APP" Contents/Frameworks/QtCore.framework/Versions/A/QtCore \
    '/usr/lib/libSystem.B.dylib'
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

# --- VB-08: an unrewritten own install name is reported, NOT failed ----------
# The exact shape observed on macos-latest (run 29852867578): macdeployqt
# copied QtWidgets/libbrotlicommon into the bundle but left their LC_ID_DYLIB
# pointing at /opt/homebrew. dyld never loads via a file's own id, so this
# cannot break the shipped app and must not block the package.
APP=$WORK/staleid.app
macho "$APP" Contents/MacOS/jnext \
    '@executable_path/../Frameworks/QtWidgets.framework/Versions/A/QtWidgets'
macho "$APP" Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets \
    '/opt/homebrew/opt/qtbase/lib/QtWidgets.framework/Versions/A/QtWidgets' \
    '/usr/lib/libSystem.B.dylib'
install_id "$APP" Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets \
    '/opt/homebrew/opt/qtbase/lib/QtWidgets.framework/Versions/A/QtWidgets'
rc=$(run "$APP")
if [ "$rc" -eq 0 ] && grep -q 'id ->' "$WORK/out.log"; then
    ok VB-08 "stale own install name reported, not failed"
else bad VB-08 "expected 0 with an 'id ->' note, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-09: a REAL external load on a file that also has a stale id FAILS ----
# The dangerous case the VB-08 relaxation must not swallow: same file, same
# stale id, but it also LOADS something from /opt/homebrew. That one does abort
# in dyld. If this passes, VB-08 has been implemented as "ignore everything on
# a file that has an id", which would have hidden the original bug.
APP=$WORK/staleid-plus-real.app
macho "$APP" Contents/MacOS/jnext \
    '@executable_path/../Frameworks/QtWidgets.framework/Versions/A/QtWidgets'
macho "$APP" Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets \
    '/opt/homebrew/opt/qtbase/lib/QtWidgets.framework/Versions/A/QtWidgets' \
    '/opt/homebrew/opt/libpng/lib/libpng16.16.dylib'
install_id "$APP" Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets \
    '/opt/homebrew/opt/qtbase/lib/QtWidgets.framework/Versions/A/QtWidgets'
rc=$(run "$APP")
if [ "$rc" -ne 0 ] && grep -q 'libpng16' "$WORK/out.log"; then
    ok VB-09 "real load dep still fails alongside a stale id"
else bad VB-09 "expected non-zero naming libpng, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-10: an executable has no id, so nothing is excused on it -------------
# `otool -D` prints only a header for an executable. If the script mistook an
# empty id for "matches everything", the main binary — the one that carried the
# reported bug — would stop being checked at all.
APP=$WORK/exe-no-id.app
macho "$APP" Contents/MacOS/jnext '/opt/homebrew/opt/libpng/lib/libpng16.16.dylib'
rc=$(run "$APP")
if [ "$rc" -ne 0 ]; then ok VB-10 "executable with no id is still fully checked"
else bad VB-10 "expected non-zero, got 0 — an absent id excused a real dep"; fi

# --- VB-11: a bundle-relative reference to a MISSING file fails --------------
# macdeployqt silently drops a framework whose @rpath it cannot resolve, leaving
# the reference behind (observed: "Cannot resolve rpath @rpath/QtSvg..." while
# exiting 0). Checking only that the STRING looks bundle-relative would accept
# that and ship a .dmg that dies at launch — the GH #46 symptom one layer down.
APP=$WORK/dangling.app
macho "$APP" Contents/MacOS/jnext '@rpath/QtSvg.framework/Versions/A/QtSvg'
rpaths "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks'
mkdir -p "$APP/Contents/Frameworks"      # ...but QtSvg was never copied into it
rc=$(run "$APP")
if [ "$rc" -ne 0 ] && grep -q 'MISSING' "$WORK/out.log"; then
    ok VB-11 "unresolved @rpath reference rejected"
else bad VB-11 "expected non-zero naming it MISSING, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-12: an invalid code signature fails ----------------------------------
# Every install_name_tool rewrite invalidates the signature of the file it
# edits; on Apple Silicon the kernel then kills the app at launch (Qt
# QTBUG-138019). No other check here can see it: `--version` returns before Qt
# loads the Cocoa plugin, so the rewritten plugins are never touched.
APP=$WORK/badsig.app
macho "$APP" Contents/MacOS/jnext '/usr/lib/libSystem.B.dylib'
touch "$APP/.bad-signature"
rc=$(run "$APP")
if [ "$rc" -ne 0 ] && grep -qi 'signature' "$WORK/out.log"; then
    ok VB-12 "invalid code signature rejected"
else bad VB-12 "expected non-zero mentioning the signature, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-13: the allow-list anchors are real prefixes, not substrings ---------
# Deleting the ^ anchors from ALLOWED_RE left all earlier rows green, so nothing
# pinned them. A Homebrew Cellar path can legitimately contain "/usr/lib/"
# mid-path, and unanchored matching would wave it straight through.
APP=$WORK/substring.app
macho "$APP" Contents/MacOS/jnext '/opt/homebrew/Cellar/foo/1.0/usr/lib/libfoo.dylib'
rc=$(run "$APP")
if [ "$rc" -ne 0 ]; then ok VB-13 "path merely containing /usr/lib/ rejected"
else bad VB-13 "expected non-zero, got 0 — allow-list matched a substring"; fi

# --- VB-14: @rpath resolves through the MAIN EXECUTABLE's rpaths -------------
# dyld builds its @rpath list from the whole load chain, not just the referring
# file. macdeployqt leaves bundled dylibs referencing each other via @rpath
# without giving them an LC_RPATH of their own — they resolve through the
# executable's. Checking only the referring file's rpaths reported
# libwebpmux -> @rpath/libwebp.7.dylib as MISSING on run 29856062806 while
# libwebp.7.dylib was sitting in Contents/Frameworks the whole time.
APP=$WORK/rpath-chain.app
macho "$APP" Contents/MacOS/jnext '@rpath/libwebpmux.3.dylib'
rpaths "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks'
macho "$APP" Contents/Frameworks/libwebpmux.3.dylib '@rpath/libwebp.7.dylib'
#   ...deliberately NO rpaths on libwebpmux itself
macho "$APP" Contents/Frameworks/libwebp.7.dylib '/usr/lib/libSystem.B.dylib'
rc=$(run "$APP")
if [ "$rc" -eq 0 ]; then ok VB-14 "@rpath resolved via the executable's rpaths"
else bad VB-14 "expected 0, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-15: a dlopen'd dependency that is MISSING is caught -------------------
# THE ONE otool CANNOT SEE. Homebrew's sdl2 is sdl2-compat: an SDL2 API over
# SDL3 that dlopen()s libSDL3 by leaf name, so it appears in no load command.
# v0.98.72 passed every structural check in this file and still aborted at
# launch on a clean Mac for want of it (GH #46). A dependency walk is
# structurally incapable of catching this; only an explicit rule can.
APP=$WORK/sdl2compat-missing.app
macho "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks/libSDL2-2.0.0.dylib'
macho "$APP" Contents/Frameworks/libSDL2-2.0.0.dylib '/usr/lib/libSystem.B.dylib'
printf 'this build dlopens libSDL3 by name\n' >> "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib"
rc=$(run "$APP")
if [ "$rc" -ne 0 ] && grep -qi 'libSDL3' "$WORK/out.log"; then
    ok VB-15 "missing dlopen'd libSDL3 caught despite being invisible to otool"
else bad VB-15 "expected non-zero naming libSDL3, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-16: ...and accepted once it IS bundled --------------------------------
# The other direction: the rule must not fire on a correctly bundled app, or it
# would block every macOS package permanently.
APP=$WORK/sdl2compat-ok.app
macho "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks/libSDL2-2.0.0.dylib'
macho "$APP" Contents/Frameworks/libSDL2-2.0.0.dylib '/usr/lib/libSystem.B.dylib'
printf 'this build dlopens libSDL3 by name\n' >> "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib"
macho "$APP" Contents/Frameworks/libSDL3.dylib '/usr/lib/libSystem.B.dylib'
rc=$(run "$APP")
if [ "$rc" -eq 0 ]; then ok VB-16 "sdl2-compat bundle with libSDL3 accepted"
else bad VB-16 "expected 0, got $rc: $(cat "$WORK/out.log")"; fi

# --- VB-07: wrong usage exits 2 ----------------------------------------------
bash "$SCRIPT" >/dev/null 2>&1; rc=$?
if [ "$rc" -eq 2 ]; then ok VB-07 "no argument exits 2"
else bad VB-07 "expected 2, got $rc"; fi

printf "\n${BOLD}Total: %d pass, %d fail${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
