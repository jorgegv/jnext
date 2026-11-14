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
ok()  { printf "  ${GREEN}PASS${RESET} %-24s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-24s %s\n" "$1" "$2"; fail=$((fail+1)); }

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
    # The LC_RPATH load commands and the minimum-OS command matter here; mimic
    # otool's layout. Real otool -l prints (Apple's layout, not derived from
    # the script):
    #       cmd LC_BUILD_VERSION      |       cmd LC_VERSION_MIN_MACOSX
    #   cmdsize 32                    |   cmdsize 16
    #  platform 1                     |   version 10.13
    #     minos 15.0                  |       sdk 10.15
    #       sdk 26.0
    # <file>.minos -> LC_BUILD_VERSION minos; <file>.vmin -> LC_VERSION_MIN_MACOSX.
    # One block per line of <file>.minos: a universal binary has one per slice.
    if [ -f "$f.minos" ]; then
        while IFS= read -r mv; do
            printf '      cmd LC_BUILD_VERSION\n  cmdsize 32\n platform 1\n    minos %s\n      sdk 26.0\n   ntools 1\n' "$mv"
        done < "$f.minos"
    fi
    if [ -f "$f.vmin" ]; then
        printf '      cmd LC_VERSION_MIN_MACOSX\n  cmdsize 16\n  version %s\n      sdk 10.15\n' "$(cat "$f.vmin")"
    fi
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

# minos <app> <relative-path> <version>   - give a file an LC_BUILD_VERSION minos
minos() { printf '%s\n' "$3" > "$1/$2.minos"; }
# vmin <app> <relative-path> <version>    - give a file LC_VERSION_MIN_MACOSX
vmin() { printf '%s\n' "$3" > "$1/$2.vmin"; }
# plist <app> <version>                   - Info.plist with LSMinimumSystemVersion
plist() {
    mkdir -p "$1/Contents"
    printf '<?xml version="1.0"?>\n<plist version="1.0">\n<dict>\n\t<key>CFBundleName</key>\n\t<string>jnext</string>\n\t<key>LSMinimumSystemVersion</key>\n\t<string>%s</string>\n</dict>\n</plist>\n' "$2" > "$1/Contents/Info.plist"
}
# runfloor <app> <floor>                  - run with JNEXT_MACOS_MIN set; the app gets
# an Info.plist that carries the floor first, so each row tests one thing
runfloor() {
    plist "$1" "$2"
    JNEXT_MACOS_MIN=$2 bash "$SCRIPT" "$1" >"$WORK/out.log" 2>&1; echo $?
}

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
macho "$APP" Contents/MacOS/jnext '/usr/local/opt/sdl3/lib/libSDL3.0.dylib'
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

# NOTE (GH #57): VB-15 and VB-16 lived here. They pinned verify-bundle.sh's
# dlopen rule — the one thing otool could not see — for the case of Homebrew's
# `sdl2` being sdl2-compat, which dlopen()s libSDL3 by leaf name and shipped a
# broken .app in GH #46. jnext links SDL3 directly now, so libSDL3 is an
# ordinary load command that VB-01..VB-14 already cover, the rule they pinned
# was removed from verify-bundle.sh, and two rows asserting a deleted rule
# would only assert themselves. Removed with the rule, in the same change.

# --- VB-07: wrong usage exits 2 ----------------------------------------------
bash "$SCRIPT" >/dev/null 2>&1; rc=$?
if [ "$rc" -eq 2 ]; then ok VB-07 "no argument exits 2"
else bad VB-07 "expected 2, got $rc"; fi

# --- VB-17/VB-18: a failed diagnostic write never changes the verdict -------
# CI run 43387706584: an informational `echo >&2` hit EINTR on the runner's pipe
# and `set -e` turned a passing check into exit 1. Streams are made to FAIL
# every write (/dev/full: ENOSPC; `>&-`: EBADF) and the verdict must be what it
# is with healthy streams.
# VB-17: the stale-id bundle (VB-08) prints notes yet passes: exit 0 with
# stderr unwritable, and with stdout and stderr both unwritable (the OK line is
# on stdout). Control: with healthy streams the note is on stderr only and the
# OK line on stdout only.
rc_a=0; bash "$SCRIPT" "$WORK/staleid.app" >/dev/null 2>/dev/full || rc_a=$?
rc_b=0; bash "$SCRIPT" "$WORK/staleid.app" >/dev/null 2>&- || rc_b=$?
rc_c=0; bash "$SCRIPT" "$WORK/staleid.app" >/dev/full 2>/dev/full || rc_c=$?
rc_d=0; bash "$SCRIPT" "$WORK/staleid.app" >&- 2>&- || rc_d=$?
# ...and the "codesign not available" note path (a PATH without codesign;
# assumes the host has none outside the stub dir, true on the Linux dev host).
mkdir -p "$WORK/bin-nocs"; ln -sf "$WORK/bin/otool" "$WORK/bin/file" "$WORK/bin-nocs/"
rc_e=0; PATH="$WORK/bin-nocs:/usr/bin:/bin" bash "$SCRIPT" "$WORK/staleid.app" >/dev/null 2>/dev/full || rc_e=$?
bash "$SCRIPT" "$WORK/staleid.app" >"$WORK/c.out" 2>"$WORK/c.err" || true
ctl=0
grep -q 'id ->' "$WORK/c.err" && ! grep -q 'id ->' "$WORK/c.out" \
    && grep -q 'verify-bundle: OK' "$WORK/c.out" && ! grep -q 'verify-bundle: OK' "$WORK/c.err" && ctl=1
if [ "$rc_a$rc_b$rc_c$rc_d$rc_e" = "00000" ] && [ "$ctl" -eq 1 ]; then
    ok VB-17 "stale-id bundle accepted with stderr/stdout unwritable; streams split as before"
else bad VB-17 "expected 0/0/0/0/0 + control, got /dev/full-err=$rc_a closed-err=$rc_b full-both=$rc_c closed-both=$rc_d no-codesign=$rc_e control=$ctl"; fi

# VB-18: every FAILING verdict keeps its exact exit code with stderr unwritable
# (a note that failed open would turn FAIL into PASS). It passes on main's
# script by design: it pins that a write failure never turns FAIL into PASS.
vb18=""
for case_ in "homebrew.app:1" "dangling.app:1" "badsig.app:1" "empty.app:1" \
             "no-such.app:1" ":2"; do
    app=${case_%%:*}; want=${case_##*:}
    for redir in /dev/full closed; do
        if [ "$redir" = closed ]; then
            if [ -n "$app" ]; then bash "$SCRIPT" "$WORK/$app" >/dev/null 2>&-; else bash "$SCRIPT" >/dev/null 2>&-; fi
        else
            if [ -n "$app" ]; then bash "$SCRIPT" "$WORK/$app" >/dev/null 2>/dev/full; else bash "$SCRIPT" >/dev/null 2>/dev/full; fi
        fi
        got=$?
        [ "$got" -eq "$want" ] || vb18="$vb18 ${app:-noarg}/$redir=$got(want $want)"
    done
done
if [ -z "$vb18" ]; then ok VB-18 "every failing verdict keeps its exit code with stderr unwritable"
else bad VB-18 "wrong exit code(s):$vb18"; fi

# --- minimum macOS (JNEXT_MACOS_MIN) -----------------------------------------
# A bundled dylib built for a newer macOS than the target raises the app's real
# floor: dyld refuses it on the older OS. The gate reads every Mach-O's minos.
# mkminosapp <dir> - an app with an executable, a dylib and a plugin, all at 15.0
mkminosapp() {
    local app=$1
    macho "$app" Contents/MacOS/jnext '/usr/lib/libSystem.B.dylib'
    macho "$app" Contents/Frameworks/libpng16.16.dylib '/usr/lib/libSystem.B.dylib'
    macho "$app" Contents/PlugIns/platforms/libqcocoa.dylib '/usr/lib/libSystem.B.dylib'
    minos "$app" Contents/MacOS/jnext 15.0
    minos "$app" Contents/Frameworks/libpng16.16.dylib 15.0
    minos "$app" Contents/PlugIns/platforms/libqcocoa.dylib 15.0
}

APP=$WORK/minos-ok.app; mkminosapp "$APP"
rc=$(runfloor "$APP" 15.0)
if [ "$rc" -eq 0 ]; then ok minos-under-floor "all files at 15.0, floor 15.0 accepted"
else bad minos-under-floor "expected 0, got $rc: $(cat "$WORK/out.log")"; fi

APP=$WORK/minos-dylib.app; mkminosapp "$APP"
minos "$APP" Contents/Frameworks/libpng16.16.dylib 26.0
rc=$(runfloor "$APP" 15.0)
if [ "$rc" -ne 0 ] && grep -q 'libpng16' "$WORK/out.log" && grep -q '26.0' "$WORK/out.log"; then
    ok minos-dylib-over-floor "dylib at 26.0 over floor 15.0 rejected, named"
else bad minos-dylib-over-floor "expected non-zero naming libpng16 and 26.0, got $rc: $(cat "$WORK/out.log")"; fi

APP=$WORK/minos-plugin.app; mkminosapp "$APP"
minos "$APP" Contents/PlugIns/platforms/libqcocoa.dylib 15.4
rc=$(runfloor "$APP" 15.0)
if [ "$rc" -ne 0 ] && grep -q 'libqcocoa' "$WORK/out.log"; then
    ok minos-plugin-over-floor "nested plugin at 15.4 over floor 15.0 rejected"
else bad minos-plugin-over-floor "expected non-zero naming libqcocoa, got $rc: $(cat "$WORK/out.log")"; fi

APP=$WORK/minos-num.app; mkminosapp "$APP"
minos "$APP" Contents/Frameworks/libpng16.16.dylib 15.10
rc_a=$(runfloor "$APP" 15.9)
minos "$APP" Contents/Frameworks/libpng16.16.dylib 15.9
rc_b=$(runfloor "$APP" 15.10)
if [ "$rc_a" -ne 0 ] && [ "$rc_b" -eq 0 ]; then
    ok minos-numeric-compare "15.10 over floor 15.9 rejected; 15.9 under floor 15.10 accepted"
else bad minos-numeric-compare "expected rejected/accepted, got 15.10>15.9=$rc_a 15.9<15.10=$rc_b"; fi

APP=$WORK/minos-legacy.app; mkminosapp "$APP"
rm -f "$APP/Contents/Frameworks/libpng16.16.dylib.minos"
vmin "$APP" Contents/Frameworks/libpng16.16.dylib 10.13
rc=$(runfloor "$APP" 15.0)
if [ "$rc" -eq 0 ]; then ok minos-legacy-cmd "LC_VERSION_MIN_MACOSX 10.13 under floor 15.0 accepted"
else bad minos-legacy-cmd "expected 0, got $rc: $(cat "$WORK/out.log")"; fi

APP=$WORK/minos-report.app; mkminosapp "$APP"
minos "$APP" Contents/Frameworks/libpng16.16.dylib 26.0
rc=$(run "$APP")
if [ "$rc" -eq 0 ] && grep -q 'highest minos 26.0' "$WORK/out.log" && grep -q 'libpng16' "$WORK/out.log"; then
    ok minos-no-floor-reports "no floor: accepted, highest minos 26.0 and its file reported"
else bad minos-no-floor-reports "expected 0 naming 26.0 and libpng16, got $rc: $(cat "$WORK/out.log")"; fi

APP=$WORK/minos-unreadable.app; mkminosapp "$APP"
rm -f "$APP/Contents/Frameworks/libpng16.16.dylib.minos"
rc=$(runfloor "$APP" 15.0)
if [ "$rc" -ne 0 ] && grep -q 'UNREADABLE' "$WORK/out.log" && grep -q 'libpng16' "$WORK/out.log"; then
    ok minos-unreadable "Mach-O with no minimum command rejected under a floor"
else bad minos-unreadable "expected non-zero naming UNREADABLE libpng16, got $rc: $(cat "$WORK/out.log")"; fi

# --- minos: component counts, universal slices, Info.plist ------------------
# R1: versions with a different number of components compare as if padded with 0.
APP=$WORK/minos-short.app; mkminosapp "$APP"
minos "$APP" Contents/Frameworks/libpng16.16.dylib 15.4
rc_a=$(runfloor "$APP" 15)
minos "$APP" Contents/Frameworks/libpng16.16.dylib 15.0.1
rc_b=$(runfloor "$APP" 15.0)
minos "$APP" Contents/Frameworks/libpng16.16.dylib 15
rc_c=$(runfloor "$APP" 15.0)
if [ "$rc_a" -ne 0 ] && [ "$rc_b" -ne 0 ] && [ "$rc_c" -eq 0 ]; then
    ok minos-component-count "15.4 over floor 15 and 15.0.1 over 15.0 rejected; 15 under 15.0 accepted"
else bad minos-component-count "expected fail/fail/pass, got $rc_a/$rc_b/$rc_c"; fi

# R3: a universal binary declares one minimum per slice; the HIGHEST counts, in either order.
APP=$WORK/minos-univ.app; mkminosapp "$APP"
printf '14.0\n26.0\n' > "$APP/Contents/Frameworks/libpng16.16.dylib.minos"
rc_a=$(runfloor "$APP" 15.0)
printf '26.0\n14.0\n' > "$APP/Contents/Frameworks/libpng16.16.dylib.minos"
rc_b=$(runfloor "$APP" 15.0)
if [ "$rc_a" -ne 0 ] && [ "$rc_b" -ne 0 ] && grep -q '26.0' "$WORK/out.log"; then
    ok minos-universal-slices "two-slice file at 14.0 and 26.0 rejected in either order, 26.0 named"
else bad minos-universal-slices "expected non-zero twice naming 26.0, got $rc_a/$rc_b: $(cat "$WORK/out.log")"; fi

# F2: with a floor the bundle must carry LSMinimumSystemVersion equal to it.
APP=$WORK/minos-noplist.app; mkminosapp "$APP"
rc=0; JNEXT_MACOS_MIN=15.0 bash "$SCRIPT" "$APP" >"$WORK/out.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ] && grep -q 'LSMinimumSystemVersion' "$WORK/out.log"; then
    ok minos-plist-missing "no Info.plist key under a floor rejected"
else bad minos-plist-missing "expected non-zero naming LSMinimumSystemVersion, got $rc: $(cat "$WORK/out.log")"; fi
APP=$WORK/minos-plistwrong.app; mkminosapp "$APP"; plist "$APP" 14.0
rc=0; JNEXT_MACOS_MIN=15.0 bash "$SCRIPT" "$APP" >"$WORK/out.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ] && grep -q "'14.0'" "$WORK/out.log"; then
    ok minos-plist-wrong "Info.plist key 14.0 under floor 15.0 rejected"
else bad minos-plist-wrong "expected non-zero naming 14.0, got $rc: $(cat "$WORK/out.log")"; fi
APP=$WORK/minos-plistfree.app; mkminosapp "$APP"
rc=$(run "$APP")
if [ "$rc" -eq 0 ]; then ok minos-plist-no-floor "no floor: a bundle without the key is accepted"
else bad minos-plist-no-floor "expected 0, got $rc: $(cat "$WORK/out.log")"; fi

printf "\n${BOLD}Total: %d pass, %d fail${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
