#!/usr/bin/env bash
#
# Contract test: every package recipe ships the third-party licence texts.
# Hermetic, reads the lists and recipes only (no build); the packages are built
# by the release workflow, whose install/bundle steps fail on a missing source.
#
set -u
unset MACPORTS_PREFIX   # an inherited prefix must not switch the Homebrew rows into MacPorts mode
cd "$(dirname "$0")/../.." || exit 2   # repo root

RED='\033[0;31m'; GREEN='\033[0;32m'; RESET='\033[0m'
pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %-28s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-28s %s\n" "$1" "$2"; fail=$((fail+1)); }

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

# 6. macOS, MacPorts mode (MACPORTS_PREFIX set, the Intel build): a stub `port`
#    answers `provides` / `contents` from fixture files. The output format is
#    MacPorts': "<path> is provided by: <port>", and `contents` lists one
#    indented absolute path per file. A framework is looked up by its real binary
#    under libexec/qt6/lib. Rows: owner + licence, Qt fallback, an unowned library
#    (the gate against a Homebrew library leaking in from /usr/local), and
#    MACPORTS_PREFIX unset staying in Homebrew mode.
MP=$W/mp; MPA=$W/mpapp2/Contents/Frameworks; MPX=$W/mpx
mkdir -p "$MP/lib" "$MP/libexec/qt6/lib/QtCore.framework/Versions/A" "$MP/share/doc/libpng" "$MPX" \
         "$W/mpbin"
touch "$MP/lib/libpng16.16.dylib" "$MP/libexec/qt6/lib/QtCore.framework/Versions/A/QtCore" \
      "$MP/share/doc/libpng/COPYING" "$MP/share/doc/libpng/README"
printf '%s %s\n' "$MP/lib/libpng16.16.dylib" libpng \
    "$MP/libexec/qt6/lib/QtCore.framework/Versions/A/QtCore" qt6-qtbase > "$MPX/provides"
printf '  %s\n  %s\n' "$MP/lib/libpng16.16.dylib" "$MP/share/doc/libpng/COPYING" "$MP/share/doc/libpng/README" > "$MPX/contents.libpng"
printf '  %s\n' "$MP/libexec/qt6/lib/QtCore.framework/Versions/A/QtCore" > "$MPX/contents.qt6-qtbase"
cat > "$W/mpbin/port" <<'STUB'
#!/usr/bin/env bash
# Behaves as MacPorts 2.12.7 src/port/port.tcl does (action_provides, action_contents):
#   port -q provides <file>  -> ONLY the bare port name; nothing at all if no port provides it
#   port provides <file>     -> "<file> is provided by: <port>" / "<file> is not provided by a MacPorts port."
#   port -q contents <port>  -> one "  <path>" line per file (the header is a non-quiet notice)
echo "$*" >> "$MPX/calls"
quiet=0; [ "$1" = -q ] && { quiet=1; shift; }
case "$1" in
provides) p=$(grep -F -m1 "$2 " "$MPX/provides") || p=""
          if [ -n "$p" ]; then
              if [ "$quiet" -eq 1 ]; then echo "${p##* }"; else echo "$2 is provided by: ${p##* }"; fi
          elif [ "$quiet" -eq 0 ]; then echo "$2 is not provided by a MacPorts port."; fi ;;
contents) [ "$quiet" -eq 1 ] || echo "Port $2 contains:"
          cat "$MPX/contents.$2" 2>/dev/null ;;
esac
STUB
chmod +x "$W/mpbin/port"
runmp() {   # runmp <app> <out>
    MPX=$MPX PATH="$W/mpbin:$PATH" MACPORTS_PREFIX="$MP" bash packaging/macos/bundle-licenses.sh "$1" "$2" >"$2.log" 2>&1
}
msg=""
mkdir -p "$MPA"; touch "$MPA/libpng16.16.dylib"
if ! runmp "$W/mpapp2" "$W/mpout"; then
    msg="$msg script failed on a MacPorts-owned dylib: $(tail -3 "$W/mpout.log" | tr "\n" " ");"
else
    [ -f "$W/mpout/bundled/libpng/COPYING" ] || msg="$msg port licence not shipped under the port name;"
    [ ! -e "$W/mpout/bundled/libpng/README" ] || msg="$msg a non-licence file was shipped;"
fi
[ -z "$msg" ] && ok macports-owner-and-licence "MACPORTS_PREFIX: dylib owned by libpng ships its COPYING under bundled/libpng" \
              || bad macports-owner-and-licence "$msg"

msg=""
mkdir -p "$W/mpapp3/Contents/Frameworks/QtCore.framework"
if ! runmp "$W/mpapp3" "$W/mpout3"; then
    msg="$msg script failed on a MacPorts Qt framework: $(tail -3 "$W/mpout3.log" | tr "\n" " ");"
else
    [ -f "$W/mpout3/bundled/qt6-qtbase/LGPL-3.0-only.txt" ] || msg="$msg qt6-* fallback not shipped;"
fi
[ -z "$msg" ] && ok macports-qt-fallback "qt6-qtbase framework with no licence in its contents gets the qt fallback" \
              || bad macports-qt-fallback "$msg"

msg=""
mkdir -p "$W/mpapp4/Contents/Frameworks"; touch "$W/mpapp4/Contents/Frameworks/libhomebrew.9.dylib"
if runmp "$W/mpapp4" "$W/mpout4"; then msg="$msg a library no MacPorts port owns was accepted;"
elif ! grep -q 'no MacPorts port owns libhomebrew.9.dylib' "$W/mpout4.log"; then
    msg="$msg refused, but not with the 'no MacPorts port owns' message: $(cat "$W/mpout4.log");"
fi
[ -z "$msg" ] && ok macports-unowned-fails "a library no port provides is refused, naming it" \
              || bad macports-unowned-fails "$msg"

msg=""
mkdir -p "$W/hbapp/Contents/Frameworks"; touch "$W/hbapp/Contents/Frameworks/libpng16.16.dylib"
: > "$MPX/calls"
if ! env -u MACPORTS_PREFIX MPX="$MPX" PATH="$W/mpbin:$PATH" HOMEBREW_PREFIX="$B" \
        bash packaging/macos/bundle-licenses.sh "$W/hbapp" "$W/hbout" >/dev/null 2>&1; then
    msg="$msg Homebrew run failed with MACPORTS_PREFIX unset;"
else
    [ -f "$W/hbout/bundled/libpng/LICENSE" ] || msg="$msg Homebrew keg licence not shipped;"
    [ ! -s "$MPX/calls" ] || msg="$msg port was consulted with MACPORTS_PREFIX unset;"
fi
[ -z "$msg" ] && ok macports-unset-is-homebrew "MACPORTS_PREFIX unset: Homebrew lookup, port never run" \
              || bad macports-unset-is-homebrew "$msg"

# mpapp <dir> <lib>...  - a bundle holding the named top-level Frameworks entries
mpapp() { local d=$1; shift; mkdir -p "$d/Contents/Frameworks"; for l in "$@"; do touch "$d/Contents/Frameworks/$l"; done; }
# mpown <real path> <port> [contents file...]  - register ownership and contents
mpown() {
    local path=$1 port=$2; shift 2
    printf '%s %s\n' "$path" "$port" >> "$MPX/provides"
    : > "$MPX/contents.$port"
    printf '  %s\n' "$path" "$@" >> "$MPX/contents.$port"
}

# libexec/<port>/lib: openssl3 keeps its libraries there, not in <prefix>/lib.
msg=""
mkdir -p "$MP/libexec/openssl3/lib" "$MP/share/doc/openssl3"; touch "$MP/libexec/openssl3/lib/libssl.3.dylib" "$MP/share/doc/openssl3/LICENSE.txt"
mpown "$MP/libexec/openssl3/lib/libssl.3.dylib" openssl3 "$MP/share/doc/openssl3/LICENSE.txt"
mpapp "$W/mpapp5" libssl.3.dylib
if ! runmp "$W/mpapp5" "$W/mpout5"; then msg="$msg script failed: $(tail -3 "$W/mpout5.log" | tr "\n" " ");"
else [ -f "$W/mpout5/bundled/openssl3/LICENSE.txt" ] || msg="$msg openssl3 licence not shipped;"; fi
[ -z "$msg" ] && ok macports-libexec-owner "a dylib under libexec/openssl3/lib is owned by openssl3" \
              || bad macports-libexec-owner "$msg"

# a licence that is not named LICENSE*/COPYING*/LICENCE* but sits under LICENSES/
msg=""
mkdir -p "$MP/lib" "$MP/share/licenses/foo/LICENSES"; touch "$MP/lib/libfoo.1.dylib" "$MP/share/licenses/foo/LICENSES/MIT.txt"
mpown "$MP/lib/libfoo.1.dylib" foo "$MP/share/licenses/foo/LICENSES/MIT.txt"
mpapp "$W/mpapp6" libfoo.1.dylib
if ! runmp "$W/mpapp6" "$W/mpout6"; then msg="$msg script failed: $(tail -3 "$W/mpout6.log" | tr "\n" " ");"
else [ -f "$W/mpout6/bundled/foo/MIT.txt" ] || msg="$msg a LICENSES/ file was not shipped;"; fi
[ -z "$msg" ] && ok macports-licenses-dir "a file under a LICENSES/ directory is shipped as a licence" \
              || bad macports-licenses-dir "$msg"

# glib2 / sqlite3 have no licence in their contents; the fallback dirs are keyed glib / sqlite
msg=""
touch "$MP/lib/libglib-2.0.0.dylib" "$MP/lib/libsqlite3.0.dylib"
mpown "$MP/lib/libglib-2.0.0.dylib" glib2
mpown "$MP/lib/libsqlite3.0.dylib" sqlite3
mpapp "$W/mpapp7" libglib-2.0.0.dylib libsqlite3.0.dylib
if ! runmp "$W/mpapp7" "$W/mpout7"; then msg="$msg script failed: $(tail -3 "$W/mpout7.log" | tr "\n" " ");"
else
    [ -f "$W/mpout7/bundled/glib2/LGPL-2.1-or-later.txt" ] || msg="$msg glib2 fallback not shipped;"
    [ -f "$W/mpout7/bundled/sqlite3/PUBLIC-DOMAIN.txt" ] || msg="$msg sqlite3 fallback not shipped;"
fi
[ -z "$msg" ] && ok macports-glib-sqlite-fallback "glib2 and sqlite3 get the glib and sqlite fallback texts" \
              || bad macports-glib-sqlite-fallback "$msg"

# EVERY failing library is reported, then the run fails: two licence-less ports
# with no fallback, plus an unowned library, all named, exit non-zero.
msg=""
touch "$MP/lib/libbar.1.dylib" "$MP/lib/libbaz.1.dylib"
mpown "$MP/lib/libbar.1.dylib" bar
mpown "$MP/lib/libbaz.1.dylib" baz
mpapp "$W/mpapp8" libaaghost.1.dylib libbar.1.dylib libbaz.1.dylib libzzghost.1.dylib
if runmp "$W/mpapp8" "$W/mpout8"; then msg="$msg licence-less ports without fallback were accepted;"
else
    grep -q 'port bar (libbar.1.dylib) has no licence' "$W/mpout8.log" || msg="$msg bar not named;"
    grep -q 'port baz (libbaz.1.dylib) has no licence' "$W/mpout8.log" || msg="$msg baz not named (stopped at the first?);"
    grep -q 'no MacPorts port owns libaaghost.1.dylib' "$W/mpout8.log" || msg="$msg the first unowned library not named;"
    grep -q 'no MacPorts port owns libzzghost.1.dylib' "$W/mpout8.log" || msg="$msg the unowned library after it not named (stopped at the first?);"
fi
[ -z "$msg" ] && ok macports-reports-all "licence-less and unowned libraries are all named, then the run fails" \
              || bad macports-reports-all "$msg"

# zlib (a dependency of Qt, libpng, freetype, glib2) installs no licence file: its fallback ships
msg=""
touch "$MP/lib/libz.1.dylib"
mpown "$MP/lib/libz.1.dylib" zlib
mpapp "$W/mpappz" libz.1.dylib
if ! runmp "$W/mpappz" "$W/mpoutz"; then msg="$msg script failed: $(tail -3 "$W/mpoutz.log" | tr "\n" " ");"
else [ -f "$W/mpoutz/bundled/zlib/LICENSE" ] || msg="$msg zlib fallback not shipped;"; fi
[ -z "$msg" ] && ok macports-zlib-fallback "a licence-less zlib gets the zlib fallback text" \
              || bad macports-zlib-fallback "$msg"

# a licence-less port with no fallback is fatal ON ITS OWN (no unowned library to fail the run)
msg=""
mpapp "$W/mpapp9" libbar.1.dylib
if runmp "$W/mpapp9" "$W/mpout9"; then msg="$msg a licence-less port without fallback was accepted;"
elif ! grep -q 'port bar (libbar.1.dylib) has no licence' "$W/mpout9.log"; then msg="$msg refused without naming bar: $(cat "$W/mpout9.log");"; fi
[ -z "$msg" ] && ok macports-licenceless-fails "a port with no licence and no fallback fails the run by itself" \
              || bad macports-licenceless-fails "$msg"

printf "\nTotal: %d  Pass: %d  Fail: %d\n" $((pass+fail)) "$pass" "$fail"
[ "$fail" -eq 0 ]
