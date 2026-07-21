#!/usr/bin/env bash
#
# Contract tests for packaging/macos/complete-closure.sh.
#
# The script copies libraries INTO the shipped bundle, so the risks are
# shipping the wrong file, missing the one that is actually needed, or looping
# forever. Stubbed otool/file/install_name_tool, and a fake Homebrew prefix via
# HOMEBREW_PREFIX; see verify-bundle-test.sh's header for what stubbing does and
# does not prove.
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root

SCRIPT=packaging/macos/complete-closure.sh
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
*)  [ -f "$f.deps" ] || exit 0
    while IFS= read -r line; do
        printf '\t%s (compatibility version 1.0.0, current version 1.0.0)\n' "$line"
    done < "$f.deps" ;;
esac
STUB
cat >"$WORK/bin/file" <<'STUB'
#!/usr/bin/env bash
if [ -f "$2.deps" ]; then echo "Mach-O 64-bit dynamically linked shared library arm64"
else echo "ASCII text"; fi
STUB
# install_name_tool -id/-change: record the call; the deps sidecar is what the
# other stubs read, so rewrite it too for -change so passes converge.
cat >"$WORK/bin/install_name_tool" <<'STUB'
#!/usr/bin/env bash
target=${*: -1}
echo "$*" >> "$target.int-calls"
if [ "$1" = "-change" ]; then
    [ -f "$target.deps" ] && sed -i "s|^$2\$|$3|" "$target.deps"
fi
exit 0
STUB
# Copying a real Mach-O carries its load commands with it. Here the load
# commands live in sidecar files, so the copy must carry those too — otherwise a
# freshly copied library looks like plain text to the stubbed `file` and the
# transitive pass silently sees nothing. (CC-03 caught exactly that.)
cat >"$WORK/bin/cp" <<'STUB'
#!/usr/bin/env bash
args=(); for a in "$@"; do [ "$a" = "-L" ] || args+=("$a"); done
src=${args[0]}; dst=${args[1]}
/usr/bin/cp -f "$src" "$dst"
for ext in deps rpaths id; do
    [ -f "$src.$ext" ] && /usr/bin/cp -f "$src.$ext" "$dst.$ext"
done
exit 0
STUB

chmod +x "$WORK/bin"/*
export PATH="$WORK/bin:$PATH"

macho() {
    local app=$1 rel=$2; shift 2
    mkdir -p "$app/$(dirname "$rel")"
    echo "fake mach-o" > "$app/$rel"
    printf '%s\n' "$@" > "$app/$rel.deps"
}
rpaths() { local app=$1 rel=$2; shift 2; printf '%s\n' "$@" > "$app/$rel.rpaths"; }

# A fake Homebrew prefix holding installable libraries.
BREW=$WORK/brew
brewlib() {   # brewlib <formula> <libname> [dep...]
    mkdir -p "$BREW/opt/$1/lib"
    echo "fake mach-o" > "$BREW/opt/$1/lib/$2"
    shift 2
    printf '%s\n' "$@" > "$BREW/opt/${FORMULA:-x}/lib/x.deps" 2>/dev/null || true
}
brewlib_deps() { printf '%s\n' "${@:2}" > "$1.deps"; }

run() { HOMEBREW_PREFIX="$BREW" bash "$SCRIPT" "$1" >"$WORK/out.log" 2>&1; echo $?; }

printf "${BOLD}=== complete-closure.sh contract tests ===${RESET}\n\n"

# --- CC-01: a referenced-but-absent library is copied in ---------------------
# The live case: macdeployqt copied libwebp but not the libsharpyuv it loads.
APP=$WORK/webp.app
macho "$APP" Contents/MacOS/jnext '@rpath/libwebp.7.dylib'
rpaths "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks'
macho "$APP" Contents/Frameworks/libwebp.7.dylib '@rpath/libsharpyuv.0.dylib'
mkdir -p "$BREW/opt/webp/lib"
echo "fake mach-o" > "$BREW/opt/webp/lib/libsharpyuv.0.dylib"
brewlib_deps "$BREW/opt/webp/lib/libsharpyuv.0.dylib" '/usr/lib/libSystem.B.dylib'
rc=$(run "$APP")
if [ "$rc" -eq 0 ] && [ -e "$APP/Contents/Frameworks/libsharpyuv.0.dylib" ]; then
    ok CC-01 "missing library copied into the bundle"
else bad CC-01 "expected 0 and the lib present, got $rc: $(cat "$WORK/out.log")"; fi

# --- CC-02: its install name is rewritten to be bundle-relative --------------
# A copy that kept its Homebrew id would leave the bundle referencing an
# absolute path again — the very thing GH #46 is about.
if grep -q -- '-id @rpath/libsharpyuv.0.dylib' \
        "$APP/Contents/Frameworks/libsharpyuv.0.dylib.int-calls" 2>/dev/null; then
    ok CC-02 "copied library's install name made bundle-relative"
else bad CC-02 "install_name_tool -id was not called as expected"; fi

# --- CC-03: the closure is transitive ----------------------------------------
# A library copied in brings its own dependencies; a single pass would leave
# the second level dangling and the bundle still broken.
APP=$WORK/chain.app
macho "$APP" Contents/MacOS/jnext '@rpath/libone.1.dylib'
rpaths "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks'
mkdir -p "$BREW/opt/chain/lib"
echo "fake mach-o" > "$BREW/opt/chain/lib/libone.1.dylib"
brewlib_deps "$BREW/opt/chain/lib/libone.1.dylib" '@rpath/libtwo.2.dylib'
echo "fake mach-o" > "$BREW/opt/chain/lib/libtwo.2.dylib"
brewlib_deps "$BREW/opt/chain/lib/libtwo.2.dylib" '/usr/lib/libSystem.B.dylib'
rc=$(run "$APP")
if [ "$rc" -eq 0 ] && [ -e "$APP/Contents/Frameworks/libtwo.2.dylib" ]; then
    ok CC-03 "transitive dependency of a copied library also copied"
else bad CC-03 "expected 0 and libtwo present, got $rc: $(cat "$WORK/out.log")"; fi

# --- CC-04: a complete bundle is left untouched ------------------------------
# The script must be a no-op when nothing is missing, or every build would
# churn the bundle and invalidate the signature for no reason.
APP=$WORK/complete.app
macho "$APP" Contents/MacOS/jnext '@rpath/libok.1.dylib'
rpaths "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks'
macho "$APP" Contents/Frameworks/libok.1.dylib '/usr/lib/libSystem.B.dylib'
before=$(find "$APP" -type f | sort | md5sum)
rc=$(run "$APP")
after=$(find "$APP" -type f | sort | md5sum)
if [ "$rc" -eq 0 ] && [ "$before" = "$after" ] && grep -q '0 librar' "$WORK/out.log"; then
    ok CC-04 "complete bundle untouched"
else bad CC-04 "expected a no-op, got $rc: $(cat "$WORK/out.log")"; fi

# --- CC-05: a framework-shaped reference is NOT satisfied by basename --------
# Copying one file out of Foo.framework would produce a malformed framework
# that looks present to a naive check but cannot load.
APP=$WORK/fw.app
macho "$APP" Contents/MacOS/jnext '@rpath/QtSvg.framework/Versions/A/QtSvg'
rpaths "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks'
mkdir -p "$BREW/opt/qtsvg/lib"
echo "fake mach-o" > "$BREW/opt/qtsvg/lib/QtSvg"
rc=$(run "$APP")
if [ ! -e "$APP/Contents/Frameworks/QtSvg" ]; then
    ok CC-05 "framework reference not faked with a bare file"
else bad CC-05 "a bare QtSvg was copied in, producing a malformed framework"; fi

# --- CC-06: an unfindable library is reported, not silently ignored ----------
APP=$WORK/nowhere.app
macho "$APP" Contents/MacOS/jnext '@rpath/libnowhere.9.dylib'
rpaths "$APP" Contents/MacOS/jnext '@executable_path/../Frameworks'
rc=$(run "$APP")
if grep -q 'cannot find libnowhere.9.dylib' "$WORK/out.log"; then
    ok CC-06 "unfindable library reported by name"
else bad CC-06 "no report for an unfindable library: $(cat "$WORK/out.log")"; fi

printf "\n${BOLD}Total: %d pass, %d fail${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
