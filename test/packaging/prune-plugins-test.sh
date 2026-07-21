#!/usr/bin/env bash
#
# Contract tests for packaging/macos/prune-broken-plugins.sh.
#
# The pruner DELETES files from the bundle, which makes it the most dangerous
# script in the macOS packaging path: an over-eager rule silently strips
# something the app needs, and the failure appears only when a user opens the
# .app. So the two directions are pinned separately — it must remove what cannot
# load, and it must REFUSE (not remove, not ignore) when the unloadable plugin
# is the one without which there is no GUI at all.
#
# Uses the same stubbed otool/file approach as verify-bundle-test.sh; see the
# header there for what that does and does not prove.
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root

SCRIPT=packaging/macos/prune-broken-plugins.sh
RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %-22s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-22s %s\n" "$1" "$2"; fail=$((fail+1)); }

mkdir -p "$WORK/bin"
cat >"$WORK/bin/otool" <<'STUB'
#!/usr/bin/env bash
f=$2
echo "$f:"
case "$1" in
-D) [ -f "$f.id" ] && cat "$f.id"; exit 0 ;;
-l) if [ -f "$f.rpaths" ]; then
        while IFS= read -r rp; do
            printf '          cmd LC_RPATH\n'
            printf '         path %s (offset 12)\n' "$rp"
        done < "$f.rpaths"
    fi
    exit 0 ;;
*)  while IFS= read -r line; do
        printf '\t%s (compatibility version 1.0.0, current version 1.0.0)\n' "$line"
    done < "$f.deps" ;;
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
chmod +x "$WORK/bin/otool" "$WORK/bin/file"
export PATH="$WORK/bin:$PATH"

macho() {
    local app=$1 rel=$2; shift 2
    mkdir -p "$app/$(dirname "$rel")"
    echo "fake mach-o" > "$app/$rel"
    printf '%s\n' "$@" > "$app/$rel.deps"
}
rpaths() { local app=$1 rel=$2; shift 2; printf '%s\n' "$@" > "$app/$rel.rpaths"; }
run() { bash "$SCRIPT" "$1" >"$WORK/out.log" 2>&1; echo $?; }

printf "${BOLD}=== prune-broken-plugins.sh contract tests ===${RESET}\n\n"

# --- PP-01: an unusable optional plugin is removed ---------------------------
# The live case: macdeployqt copied the SVG image-format plugin but could not
# resolve QtSvg on a split Homebrew Qt, so the framework was never bundled.
APP=$WORK/svg.app
macho "$APP" Contents/MacOS/jnext '/usr/lib/libSystem.B.dylib'
macho "$APP" Contents/PlugIns/imageformats/libqsvg.dylib \
    '@rpath/QtSvg.framework/Versions/A/QtSvg'
rpaths "$APP" Contents/PlugIns/imageformats/libqsvg.dylib '@executable_path/../Frameworks'
mkdir -p "$APP/Contents/Frameworks"
rc=$(run "$APP")
if [ "$rc" -eq 0 ] && [ ! -e "$APP/Contents/PlugIns/imageformats/libqsvg.dylib" ]; then
    ok PP-01 "unusable optional plugin removed"
else bad PP-01 "expected 0 and the plugin gone, got $rc: $(cat "$WORK/out.log")"; fi

# --- PP-02: a WORKING plugin is left alone -----------------------------------
# The over-eager-pruning direction. If this fails, the script is stripping
# working functionality out of the shipped app.
APP=$WORK/keep.app
macho "$APP" Contents/MacOS/jnext '/usr/lib/libSystem.B.dylib'
macho "$APP" Contents/PlugIns/platforms/libqcocoa.dylib \
    '@rpath/QtGui.framework/Versions/A/QtGui' '/usr/lib/libSystem.B.dylib'
rpaths "$APP" Contents/PlugIns/platforms/libqcocoa.dylib '@executable_path/../Frameworks'
macho "$APP" Contents/Frameworks/QtGui.framework/Versions/A/QtGui '/usr/lib/libSystem.B.dylib'
rc=$(run "$APP")
if [ "$rc" -eq 0 ] && [ -e "$APP/Contents/PlugIns/platforms/libqcocoa.dylib" ]; then
    ok PP-02 "working plugin kept"
else bad PP-02 "expected 0 and the plugin kept, got $rc: $(cat "$WORK/out.log")"; fi

# --- PP-03: a broken ESSENTIAL plugin FAILS, and is not deleted --------------
# Deleting libqcocoa would produce an .app that starts and dies with "no Qt
# platform plugin could be initialized" — a worse bug than the one this whole
# change exists to fix. It must stop the build instead.
APP=$WORK/nococoa.app
macho "$APP" Contents/MacOS/jnext '/usr/lib/libSystem.B.dylib'
macho "$APP" Contents/PlugIns/platforms/libqcocoa.dylib \
    '@rpath/QtGui.framework/Versions/A/QtGui'
rpaths "$APP" Contents/PlugIns/platforms/libqcocoa.dylib '@executable_path/../Frameworks'
mkdir -p "$APP/Contents/Frameworks"      # QtGui absent
rc=$(run "$APP")
if [ "$rc" -ne 0 ] && [ -e "$APP/Contents/PlugIns/platforms/libqcocoa.dylib" ]; then
    ok PP-03 "broken essential plugin fails the build, not deleted"
else bad PP-03 "expected non-zero with the plugin intact, got $rc: $(cat "$WORK/out.log")"; fi

# --- PP-04: a bundle with no PlugIns dir is not an error ---------------------
APP=$WORK/noplugins.app
macho "$APP" Contents/MacOS/jnext '/usr/lib/libSystem.B.dylib'
rc=$(run "$APP")
if [ "$rc" -eq 0 ]; then ok PP-04 "no PlugIns directory is a clean no-op"
else bad PP-04 "expected 0, got $rc: $(cat "$WORK/out.log")"; fi

printf "\n${BOLD}Total: %d pass, %d fail${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
