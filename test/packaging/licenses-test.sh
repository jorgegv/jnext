#!/usr/bin/env bash
#
# Contract test: every package recipe ships the third-party licence texts.
# Hermetic, reads the lists and recipes only (no build); the packages are built
# by the release workflow, whose install/bundle steps fail on a missing source.
#
set -u
cd "$(dirname "$0")/../.." || exit 2   # repo root

RED='\033[0;31m'; GREEN='\033[0;32m'; RESET='\033[0m'
pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %-16s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-16s %s\n" "$1" "$2"; fail=$((fail+1)); }

LIST=${LICENSES_LIST:-packaging/licenses.list}
entries=$(grep -E '^[^#].*\|' "$LIST")

# 1. every entry's source exists, names are unique, and every third_party
#    directory has an entry (a new vendored library cannot go unlicensed)
msg=""
dups=$(printf '%s\n' "$entries" | cut -d'|' -f1 | sort | uniq -d)
[ -z "$dups" ] || msg="$msg duplicate names: $dups;"
while IFS='|' read -r name src; do
    [ -f "$src" ] || msg="$msg missing source $src;"
done <<<"$entries"
for d in third_party/*/; do
    d=${d%/}
    grep -q "|$d/" <<<"$entries" || msg="$msg no entry for $d;"
done
[ -z "$msg" ] && ok licenses-list "$(wc -l <<<"$entries") entries, sources exist, every third_party/ dir covered" \
              || bad licenses-list "$msg"

# 2. Linux/macOS layouts (CPack tarball, deb, rpm, Flatpak all go through
#    cmake --install) and the rpm file list
msg=""
grep -q 'licenses.list' CMakeLists.txt || msg="$msg CMake does not read the list;"
grep -qE '^jnext_install_licenses\(\.\)' CMakeLists.txt || msg="$msg macOS layout not installed;"
grep -qE '^jnext_install_licenses\(\$\{CMAKE_INSTALL_DOCDIR\}\)' CMakeLists.txt || msg="$msg FHS layout not installed;"
grep -qE '^%license .*/licenses$' packaging/rpm/jnext.spec || msg="$msg rpm spec lacks %license licenses dir;"
grep -q 'SDL3-LICENSE.txt' packaging/build-sdl3.sh && grep -q 'SDL3-LICENSE.txt' CMakeLists.txt \
    || msg="$msg vendored SDL3 licence not wired;"
[ -z "$msg" ] && ok licenses-install "CMake installs licenses/ (FHS + macOS), rpm lists it, vendored SDL3 wired" \
              || bad licenses-install "$msg"

# 3. deb copyright names every third_party directory and SDL3
msg=""
for d in third_party/*/; do
    d=${d%/}
    grep -qF "Files: $d/*" packaging/debian/copyright || msg="$msg $d;"
done
grep -q '^Files: SDL3' packaging/debian/copyright || msg="$msg SDL3;"
[ -z "$msg" ] && ok licenses-debian "debian/copyright covers every third_party dir and SDL3" \
              || bad licenses-debian "missing from debian/copyright:$msg"

# 4. Windows zips: all five recipes bundle through bundle-dlls.sh (guarded by
#    package-recipe), which ships the list and the bundled DLLs' licences
msg=""
grep -q 'packaging/licenses.list' packaging/windows/bundle-dlls.sh || msg="$msg bundle-dlls.sh ignores the list;"
grep -q 'licenses/bundled' packaging/windows/bundle-dlls.sh || msg="$msg no per-DLL licences;"
n=$(grep -c 'bash packaging/windows/bundle-dlls.sh' Makefile)
[ "$n" -ge 5 ] || msg="$msg only $n Windows recipes call bundle-dlls.sh;"
[ -z "$msg" ] && ok licenses-windows "bundle-dlls.sh ships licenses/ ($n bundle-dlls.sh calls in the Makefile)" \
              || bad licenses-windows "$msg"

# 5. macOS: bundle-licenses.sh on a fake bundle and Homebrew prefix: a library's
#    formula licences are shipped (also a LICENSES/ dir), and an unowned library
#    or a licence-less formula is refused
msg=""
grep -q 'packaging/macos/bundle-licenses.sh' CMakeLists.txt || msg="$msg not called by the macOS install;"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$W/app/Contents/Frameworks/QtCore.framework" "$W/brew/opt/qtbase/lib/QtCore.framework" \
         "$W/brew/opt/qtbase/LICENSES" "$W/brew/opt/libfoo/lib"
touch "$W/app/Contents/Frameworks/libfoo.1.dylib" "$W/brew/opt/libfoo/lib/libfoo.1.dylib" \
      "$W/brew/opt/libfoo/COPYING" "$W/brew/opt/qtbase/LICENSES/LGPL-3.0-only.txt"
if ! HOMEBREW_PREFIX="$W/brew" bash packaging/macos/bundle-licenses.sh "$W/app" "$W/out" >/dev/null 2>&1; then
    msg="$msg script failed on a complete fake bundle;"
else
    [ -f "$W/out/bundled/libfoo/COPYING" ] || msg="$msg libfoo licence not shipped;"
    [ -f "$W/out/bundled/qtbase/LGPL-3.0-only.txt" ] || msg="$msg qtbase LICENSES/ not shipped;"
fi
touch "$W/app/Contents/Frameworks/libbar.dylib"
HOMEBREW_PREFIX="$W/brew" bash packaging/macos/bundle-licenses.sh "$W/app" "$W/out2" >/dev/null 2>&1 \
    && msg="$msg unowned library accepted;"
mkdir -p "$W/brew/opt/libbar/lib"; touch "$W/brew/opt/libbar/lib/libbar.dylib"
HOMEBREW_PREFIX="$W/brew" bash packaging/macos/bundle-licenses.sh "$W/app" "$W/out3" >/dev/null 2>&1 \
    && msg="$msg licence-less formula accepted;"
[ -z "$msg" ] && ok licenses-macos "bundle-licenses.sh ships per-formula licences, refuses unowned/licence-less" \
              || bad licenses-macos "$msg"

printf "\nTotal: %d  Pass: %d  Fail: %d\n" $((pass+fail)) "$pass" "$fail"
[ "$fail" -eq 0 ]
