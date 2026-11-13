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

# 3. deb copyright names every third_party directory
msg=""
for d in third_party/*/; do
    d=${d%/}
    grep -qF "Files: $d/*" packaging/debian/copyright || msg="$msg $d;"
done
[ -z "$msg" ] && ok licenses-debian "debian/copyright covers every third_party dir" \
              || bad licenses-debian "missing from debian/copyright:$msg"

W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

# 4. Windows zips: bundle-dlls.sh run on a fake sysroot with stub objdump and
#    rpm (all five recipes bundle through it, guarded by package-recipe). Three
#    packages: with licences, with only a fallback, with a plugin; then a
#    package with nothing must refuse the bundle.
msg=""
FX=$W/win; S=$FX/sysroot/mingw
mkdir -p "$FX/third_party/spdlog" "$S/bin" "$S/lib/qt6/plugins/platforms" "$FX/own" "$FX/lic" "$FX/fb" "$FX/imp"
head -c 128 /dev/zero > "$FX/jnext.exe"   # minimal PE header: e_lfanew=0x40, PE\0\0, Machine 0x8664
printf '\x40' | dd of="$FX/jnext.exe" bs=1 seek=60 conv=notrunc 2>/dev/null
printf 'PE\0\0\x64\x86' | dd of="$FX/jnext.exe" bs=1 seek=64 conv=notrunc 2>/dev/null
touch "$S/bin/liba.dll" "$S/bin/libb.dll" "$S/bin/qt6core.dll" "$S/lib/qt6/plugins/platforms/qwindows.dll"
printf 'liba.dll\nlibb.dll\nqt6core.dll\n' > "$FX/imp/jnext.exe"
echo mingw64-pa > "$FX/own/liba.dll"; echo mingw64-fb > "$FX/own/libb.dll"
echo mingw64-qt > "$FX/own/qt6core.dll"; echo mingw64-plug > "$FX/own/qwindows.dll"
echo "pa licence" > "$FX/pa.txt"; echo "$FX/pa.txt" > "$FX/lic/mingw64-pa"
echo "qt licence" > "$FX/qt.txt"; echo "$FX/qt.txt" > "$FX/lic/mingw64-qt"
echo "plug licence" > "$FX/plug.txt"; echo "$FX/plug.txt" > "$FX/lic/mingw64-plug"
echo "fb licence" > "$FX/fb/fb.txt"
mkdir -p "$FX/bin"
cat > "$FX/bin/x86_64-w64-mingw32-objdump" <<'STUB'
#!/usr/bin/env bash
# objdump -p <file>: "DLL Name:" lines from the fixture's import list
[ -f "$FIX/imp/$(basename "$2")" ] && sed 's/^/\tDLL Name: /' "$FIX/imp/$(basename "$2")"; exit 0
STUB
cat > "$FX/bin/rpm" <<'STUB'
#!/usr/bin/env bash
case "$1" in
-qf) cat "$FIX/own/$(basename "$4" | tr A-Z a-z)" ;;
-qL) [ -f "$FIX/lic/$2" ] && cat "$FIX/lic/$2"; exit 0 ;;
esac
STUB
chmod +x "$FX/bin/"*
runwin() {
    FIX=$FX PATH="$FX/bin:$PATH" MINGW_SYSROOT="$S" MINGW_OBJDUMP="$FX/bin/x86_64-w64-mingw32-objdump" \
        LICENSE_FALLBACKS="$FX/fb" bash packaging/windows/bundle-dlls.sh "$FX/jnext.exe" "$1" >"$1.log" 2>&1
}
mkdir -p "$W/o1"
if ! runwin "$W/o1"; then
    msg="$msg bundle failed on a fixture with licences/fallback: $(tail -3 "$W/o1.log" | tr "\n" " ");"
else
    L=$W/o1/licenses
    for n in $(cut -d'|' -f1 <<<"$entries"); do [ -f "$L/$n" ] || msg="$msg licenses/$n missing;"; done
    [ -f "$L/bundled/mingw64-pa/pa.txt" ] || msg="$msg rpm licence of a resolved DLL missing;"
    [ -f "$L/bundled/mingw64-fb/fb.txt" ] || msg="$msg fallback licence missing;"
    [ -f "$L/bundled/mingw64-qt/qt.txt" ] || msg="$msg licence of a Qt DLL missing;"
    [ -f "$L/bundled/mingw64-plug/plug.txt" ] || msg="$msg licence of a plugin DLL missing;"
fi
touch "$S/bin/libc.dll"; echo libc.dll >> "$FX/imp/jnext.exe"; echo mingw64-none > "$FX/own/libc.dll"
mkdir -p "$W/o2"
runwin "$W/o2" && msg="$msg a package with no licence and no fallback was accepted;"
# an empty leftover from a failed run must not pass the next one
mkdir -p "$W/o2/licenses/bundled/mingw64-none"
runwin "$W/o2" && msg="$msg stale empty licence dir accepted;"
[ -z "$msg" ] && ok licenses-windows "bundle-dlls.sh ships the list + per-package licences/fallbacks, refuses a licence-less DLL" \
              || bad licenses-windows "$msg"

# 5. macOS: bundle-licenses.sh on a fake bundle and a fake Homebrew prefix
#    modelled on the real arm64 bottles (listed from ghcr.io): libpng has a
#    LICENSE, qtbase/qtsvg only sbom.spdx.json, glib nothing; Qt and glib use
#    the in-tree fallbacks; an unowned library or a keg with neither is refused.
msg=""
grep -qE 'COMMAND bash .*packaging/macos/bundle-licenses.sh' CMakeLists.txt || msg="$msg not run by the macOS install;"
grep -qE '^cp "\$SRC/LICENSE.txt" "\$PREFIX/share/licenses/SDL3-LICENSE.txt"' packaging/build-sdl3.sh || msg="$msg build-sdl3.sh does not copy the SDL3 licence;"
B=$W/brew; A=$W/app/Contents/Frameworks
mkdir -p "$B/opt/libpng/lib" "$B/opt/qtbase/lib/QtCore.framework" "$B/opt/qtsvg/lib/QtSvg.framework" \
         "$B/opt/glib/lib" "$A/QtCore.framework" "$A/QtSvg.framework"
touch "$B/opt/libpng/LICENSE" "$B/opt/libpng/lib/libpng16.16.dylib" "$B/opt/qtbase/sbom.spdx.json" \
      "$B/opt/qtsvg/sbom.spdx.json" "$B/opt/glib/lib/libglib-2.0.0.dylib" "$A/libpng16.16.dylib" "$A/libglib-2.0.0.dylib"
if ! HOMEBREW_PREFIX="$B" bash packaging/macos/bundle-licenses.sh "$W/app" "$W/out" >/dev/null 2>&1; then
    msg="$msg script failed on a real-keg-shaped bundle;"
else
    [ -f "$W/out/bundled/libpng/LICENSE" ] || msg="$msg keg licence not shipped;"
    [ -f "$W/out/bundled/qtbase/LGPL-3.0-only.txt" ] || msg="$msg qtbase fallback not shipped;"
    [ -f "$W/out/bundled/qtsvg/LGPL-3.0-only.txt" ] || msg="$msg qtsvg fallback not shipped;"
    [ -f "$W/out/bundled/glib/LGPL-2.1-or-later.txt" ] || msg="$msg glib fallback not shipped;"
fi
touch "$A/libbar.dylib"
HOMEBREW_PREFIX="$B" bash packaging/macos/bundle-licenses.sh "$W/app" "$W/out2" >/dev/null 2>&1 \
    && msg="$msg unowned library accepted;"
mkdir -p "$B/opt/libbar/lib"; touch "$B/opt/libbar/lib/libbar.dylib"
HOMEBREW_PREFIX="$B" bash packaging/macos/bundle-licenses.sh "$W/app" "$W/out3" >/dev/null 2>&1 \
    && msg="$msg licence-less formula without fallback accepted;"
HOMEBREW_PREFIX="$B" bash packaging/macos/bundle-licenses.sh "$W/app" "$W/out3" >/dev/null 2>&1 \
    && msg="$msg stale empty licence dir accepted on a re-run;"
[ -z "$msg" ] && ok licenses-macos "bundle-licenses.sh: keg licences, Qt/glib fallbacks, refuses unowned/licence-less" \
              || bad licenses-macos "$msg"

printf "\nTotal: %d  Pass: %d  Fail: %d\n" $((pass+fail)) "$pass" "$fail"
[ "$fail" -eq 0 ]
