#!/usr/bin/env bash
#
# Contract tests for packaging/verify-stripped.sh (GH #572).
#
# That script is what fails a package recipe whose shipped binaries still carry
# a symbol table or debug sections. Both directions are pinned for each of the
# three formats, plus the shapes that would make it pass on nothing: a tree with
# no binary in it, a text file named like one, and a long symbol list (a
# `producer | grep -q` under pipefail reads 141 there and passed an unstripped
# Mach-O executable while the script was being written).
#
# Hermetic: the fixtures are a few bytes of each format's magic followed by a
# MODE= marker, and readelf / objdump / otool / nm are stubs on PATH that answer
# from that marker. Each PE arch has its own objdump stub that refuses the other
# arch's file, so a wrong objdump choice fails a row. The package extractors
# (rpm2cpio + cpio, dpkg-deb, unzip) are stubs too, reading a tar in place of
# the package, so each archive arm is exercised without rpmbuild or dpkg. The
# real binaries and packages are checked by the package rows of
# packaging-test.sh and by the package recipes.
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root

SCRIPT=$PWD/packaging/verify-stripped.sh
RED='\033[0;31m'; GREEN='\033[0;32m'; RESET='\033[0m'

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %-8s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-8s %s\n" "$1" "$2"; fail=$((fail+1)); }

# ------------------------------------------------------------------ stubs
BIN=$WORK/bin
mkdir -p "$BIN"
mode_of='mode=$(grep -a -o "MODE=[a-z0-9_-]*" "${@: -1}" | head -1); mode=${mode#MODE=}'

cat > "$BIN/readelf" <<STUB
#!/usr/bin/env bash
$mode_of
echo "  [ 1] .text   PROGBITS  0000000000001000 001000 000100 00  AX  0   0 16"
case "\$mode" in
    symtab) echo "  [27] .symtab SYMTAB    0000000000000000 002000 000300 18     28  20  8" ;;
    debug)  echo "  [25] .debug_info PROGBITS 0000000000000000 003000 000400 00      0   0  1" ;;
esac
STUB

for arch in x86_64 i686; do
    cat > "$BIN/$arch-w64-mingw32-objdump" <<STUB
#!/usr/bin/env bash
$mode_of
grep -a -q "ARCH=$arch" "\${@: -1}" || { echo "$arch objdump: file format not recognized" >&2; exit 1; }
case "\$1" in
    -h) echo "  0 .text  00001000  0000000140001000  0000000140001000  00000400  2**4"
        if [ "\$mode" = debug ]; then echo " 12 .debug_info 00002000  0000000140100000  0000000140100000  00100000  2**0"; fi
        ;;
    -t) printf '\nx: file format pei\n\nSYMBOL TABLE:\n'
        if [ "\$mode" = symbols ]; then
            echo "[  0](sec -2)(fl 0x00)(ty    0)(scl 103) (nx 1) 0x000000000000005f crtexe.c"
        else
            echo "no symbols"
        fi ;;
esac
STUB
done

cat > "$BIN/otool" <<STUB
#!/usr/bin/env bash
$mode_of
case "\$1" in
    -hv) [ "\$mode" = otool-fail ] && { echo "otool: bad file" >&2; exit 1; }
         echo "MH_MAGIC_64 ARM64 ALL 0x00 \$(grep -a -q TYPE=exe "\${@: -1}" && echo EXECUTE || echo DYLIB) 42 4520 NOUNDEFS" ;;
    -l)  echo "  segname __TEXT"; [ "\$mode" = dwarf ] && echo "  segname __DWARF"; echo "  segname __LINKEDIT" ;;
esac
STUB

cat > "$BIN/nm" <<STUB
#!/usr/bin/env bash
$mode_of
[ "\$1 \$2" = "-m -U" ] || { echo "nm stub: wanted -m -U, got \$*" >&2; exit 2; }
hdr="0000000100000000 (__TEXT,__text) [referenced dynamically] external __mh_execute_header"
case "\$mode" in
    clean-exe)  echo "\$hdr" ;;
    weak-exe)   echo "\$hdr"
                echo "000000010059c088 (__DATA,__data) weak external __ZGVN11GamepadHost12id_registry_E"
                echo "00000001002a0000 (__TEXT,__text) weak external __ZN6spdlog6loggerD2Ev" ;;
    many-syms)  echo "\$hdr"
                for i in \$(seq 1 5000); do printf '00000001002a%04x (__TEXT,__text) external __Z9function%dv\n' "\$i" "\$i"; done ;;
    fat-clean)  printf '\nx (for architecture x86_64):\n%s\n\nx (for architecture arm64):\n%s\n' "\$hdr" "\$hdr" ;;
    clean-lib)  echo "0000000000001000 (__TEXT,__text) external _exported_api" ;;
    local-sym)  echo "0000000000001000 (__TEXT,__text) external _exported_api"
                echo "0000000000002000 (__TEXT,__text) non-external _static_helper" ;;
    nm-fail)    echo "nm: cannot read" >&2; exit 1 ;;
esac
STUB

# Package extractors: the fake package is a tar of the tree it "holds".
cat > "$BIN/rpm2cpio" <<'STUB'
#!/usr/bin/env bash
[ "$#" -eq 1 ] && [ -f "$1" ] || { echo "rpm2cpio stub: bad args: $*" >&2; exit 2; }
cat "$1"
STUB
cat > "$BIN/cpio" <<'STUB'
#!/usr/bin/env bash
[ "$*" = "-idm --quiet" ] || { echo "cpio stub: bad args: $*" >&2; exit 2; }
tar -xf -
STUB
cat > "$BIN/dpkg-deb" <<'STUB'
#!/usr/bin/env bash
[ "$#" -eq 3 ] && [ "$1" = -x ] || { echo "dpkg-deb stub: bad args: $*" >&2; exit 2; }
tar -xf "$2" -C "$3"
STUB
cat > "$BIN/unzip" <<'STUB'
#!/usr/bin/env bash
[ "$#" -eq 4 ] && [ "$1" = -q ] && [ "$3" = -d ] || { echo "unzip stub: bad args: $*" >&2; exit 2; }
tar -xf "$2" -C "$4"
STUB
chmod +x "$BIN"/*

# ------------------------------------------------------------- fixtures
# elf/pe/macho <path> <markers...>: magic bytes, then the markers as text.
elf()   { local f=$1; shift; mkdir -p "$(dirname "$f")"; { printf '\x7fELF'; printf ' %s' "$@"; echo; } > "$f"; }
macho() { local f=$1; shift; mkdir -p "$(dirname "$f")"; { printf '\xcf\xfa\xed\xfe'; printf ' %s' "$@"; echo; } > "$f"; }
# cafebabe <path> <second-word-hex> <markers...>: a universal binary (small
# arch count) or a Java class file (major version >= 45), by its second word.
cafebabe() { local f=$1 w=$2; shift 2; mkdir -p "$(dirname "$f")"; { printf '\xca\xfe\xba\xbe'; printf "$w"; printf ' %s' "$@"; echo; } > "$f"; }
# pkg <path> <tree>: a fake package holding <tree> (read by the stub extractors).
pkg() { tar -cf "$1" -C "$2" .; }
pe() {
    local f=$1 arch=$2; shift 2
    local m; case "$arch" in x86_64) m='\x64\x86' ;; i686) m='\x4c\x01' ;; esac
    mkdir -p "$(dirname "$f")"
    {
        printf 'MZ'; head -c 58 /dev/zero; printf '\x40\x00\x00\x00'   # e_lfanew = 0x40
        printf 'PE\x00\x00'; printf "$m"
        printf ' ARCH=%s' "$arch"; printf ' %s' "$@"; echo
    } > "$f"
}

# run_case <id> <desc> <want_rc> <path...> -- <pattern...>
#   pattern  fixed substring that must appear in the output
#   !pattern fixed substring that must NOT appear
run_case() {
    local id=$1 desc=$2 want_rc=$3; shift 3
    local args=() out rc p okrow=1 why=""
    while [ "$#" -gt 0 ] && [ "$1" != -- ]; do args+=("$1"); shift; done
    [ "$#" -gt 0 ] && shift
    out=$(PATH="$BIN:$PATH" bash "$SCRIPT" "${args[@]}" 2>&1); rc=$?
    [ "$rc" -eq "$want_rc" ] || { okrow=0; why="exit $rc, wanted $want_rc"; }
    for p in "$@"; do
        case "$p" in
            '!'*) grep -qF -- "${p#!}" <<<"$out" && { okrow=0; why="${why:+$why; }present '${p#!}'"; } ;;
            *)    grep -qF -- "$p"    <<<"$out" || { okrow=0; why="${why:+$why; }missing '$p'"; } ;;
        esac
    done
    if [ "$okrow" -eq 1 ]; then ok "$id" "$desc"
    else bad "$id" "$desc [$why]"; printf '%s\n' "$out" | sed -e 's/^/        | /' | head -20; fi
}

F=$WORK/f

# ------------------------------------------------------------------ ELF
elf "$F/elf-clean" MODE=clean
run_case VSTR-01 "a stripped ELF passes" 0 "$F/elf-clean" -- \
    "verify-stripped: 1 binaries stripped" '!NOT STRIPPED'
elf "$F/elf-symtab" MODE=symtab
run_case VSTR-02 "an ELF with a .symtab fails" 1 "$F/elf-symtab" -- \
    "elf-symtab: has a .symtab" "verify-stripped: FAILED"
elf "$F/elf-debug" MODE=debug
run_case VSTR-03 "an ELF with .debug_* sections fails" 1 "$F/elf-debug" -- \
    "elf-debug: has .debug_* sections" '!has a .symtab'

# ------------------------------------------------------------------- PE
pe "$F/pe64-clean.exe" x86_64 MODE=clean
run_case VSTR-04 "a stripped x86_64 PE passes, read by the x86_64 objdump" 0 "$F/pe64-clean.exe" -- \
    "1 binaries stripped" '!not recognized'
pe "$F/pe32-clean.dll" i686 MODE=clean
run_case VSTR-05 "a stripped i686 PE passes, read by the i686 objdump" 0 "$F/pe32-clean.dll" -- \
    "1 binaries stripped" '!not recognized'
pe "$F/pe64-syms.dll" x86_64 MODE=symbols
run_case VSTR-06 "a PE with a COFF symbol table fails" 1 "$F/pe64-syms.dll" -- \
    "pe64-syms.dll: has a COFF symbol table"
pe "$F/pe32-debug.exe" i686 MODE=debug
run_case VSTR-07 "a PE with .debug_* sections fails" 1 "$F/pe32-debug.exe" -- \
    "pe32-debug.exe: has .debug_* sections" '!COFF symbol'

# --------------------------------------------------------------- Mach-O
macho "$F/macho-exe-clean" TYPE=exe MODE=clean-exe
run_case VSTR-08 "a Mach-O executable defining only __mh_execute_header passes" 0 "$F/macho-exe-clean" -- \
    "1 binaries stripped"
macho "$F/macho-exe-syms" TYPE=exe MODE=many-syms
run_case VSTR-09 "a Mach-O executable with 5000 global symbols fails (no SIGPIPE pass)" 1 "$F/macho-exe-syms" -- \
    "macho-exe-syms: defines symbols other than __mh_execute_header and weak ones"
macho "$F/macho-lib-clean.dylib" TYPE=lib MODE=clean-lib
run_case VSTR-10 "a Mach-O dylib keeping its exported symbols passes" 0 "$F/macho-lib-clean.dylib" -- \
    "1 binaries stripped"
macho "$F/macho-lib-local.dylib" TYPE=lib MODE=local-sym
run_case VSTR-11 "a Mach-O dylib with a local symbol fails" 1 "$F/macho-lib-local.dylib" -- \
    "macho-lib-local.dylib: has local symbols"
macho "$F/macho-exe-dwarf" TYPE=exe MODE=dwarf
run_case VSTR-12 "a Mach-O with a __DWARF segment fails" 1 "$F/macho-exe-dwarf" -- \
    "macho-exe-dwarf: has a __DWARF segment"
macho "$F/macho-exe-weak" TYPE=exe MODE=weak-exe
run_case VSTR-19 "a Mach-O executable keeping weak definitions (what strip leaves) passes" 0 "$F/macho-exe-weak" -- \
    "1 binaries stripped"
macho "$F/macho-exe-nmfail" TYPE=exe MODE=nm-fail
run_case VSTR-20 "a Mach-O that nm cannot read fails, never passes on no symbols" 1 "$F/macho-exe-nmfail" -- \
    "macho-exe-nmfail: nm -m -U failed"
macho "$F/macho-exe-otoolfail" TYPE=exe MODE=otool-fail
run_case VSTR-21 "a Mach-O that otool -hv cannot read fails" 1 "$F/macho-exe-otoolfail" -- \
    "macho-exe-otoolfail: otool -hv failed"
cafebabe "$F/fat-clean" '\x00\x00\x00\x02' TYPE=exe MODE=fat-clean
run_case VSTR-22 "a universal Mach-O is checked; its per-arch headers are not symbols" 0 "$F/fat-clean" -- \
    "1 binaries stripped"
cafebabe "$F/fat-bad" '\x00\x00\x00\x02' TYPE=exe MODE=many-syms
run_case VSTR-23 "an unstripped universal Mach-O fails" 1 "$F/fat-bad" -- \
    "fat-bad: defines symbols other than __mh_execute_header and weak ones"
cafebabe "$WORK/java/Main.class" '\x00\x00\x00\x34' MODE=many-syms
run_case VSTR-24 "a Java class file (also 0xcafebabe) is not a binary" 1 "$WORK/java" -- \
    "holds no executable or shared library to check" '!Main.class:'

# ------------------------------------------------------- trees and paths
mkdir -p "$WORK/docs"; echo "readme" > "$WORK/docs/README.md"
run_case VSTR-13 "a directory with no binary in it fails" 1 "$WORK/docs" -- \
    "holds no executable or shared library to check"
echo "not a binary" > "$WORK/fake.exe"
run_case VSTR-14 "a text file named .exe is not a binary: nothing checked, fails" 1 "$WORK/fake.exe" -- \
    "holds no executable or shared library to check"
elf "$WORK/mix/bin/jnext" MODE=clean
pe "$WORK/mix/lib/bad.dll" x86_64 MODE=symbols
echo "text" > "$WORK/mix/LICENSE"
run_case VSTR-15 "a tree is walked: only the unstripped binary is named" 1 "$WORK/mix" -- \
    "bad.dll: has a COFF symbol table" '!bin/jnext:' '!LICENSE'
run_case VSTR-16 "every path is checked: a clean one does not hide a bad one" 1 "$F/elf-clean" "$F/elf-symtab" -- \
    "elf-symtab: has a .symtab" '!elf-clean:'
run_case VSTR-17 "a missing path is a usage error (exit 2)" 2 "$WORK/nope" -- \
    "no such file or directory"
run_case VSTR-18 "no argument is a usage error (exit 2)" 2 -- "usage:"

# ------------------------------------------------------------- packages
elf "$WORK/t-clean/usr/bin/jnext" MODE=clean
mkdir -p "$WORK/t-clean/usr/share"; echo "doc" > "$WORK/t-clean/usr/share/README"
elf "$WORK/t-bad/usr/bin/jnext" MODE=symtab
mkdir -p "$WORK/t-docs/usr/share/doc"; echo "doc" > "$WORK/t-docs/usr/share/doc/README"
pe "$WORK/z-clean/jnext/jnext.exe" x86_64 MODE=clean
pe "$WORK/z-bad/jnext/libstdc++-6.dll" x86_64 MODE=debug
P=$WORK/pkg; mkdir -p "$P"
pkg "$P/clean.rpm" "$WORK/t-clean"; pkg "$P/bad.rpm" "$WORK/t-bad"; pkg "$P/docs.rpm" "$WORK/t-docs"
pkg "$P/clean.deb" "$WORK/t-clean"; pkg "$P/bad.deb" "$WORK/t-bad"; pkg "$P/docs.deb" "$WORK/t-docs"
pkg "$P/clean.zip" "$WORK/z-clean"; pkg "$P/bad.zip" "$WORK/z-bad"; pkg "$P/docs.zip" "$WORK/t-docs"
run_case VSTR-25 "an rpm whose binary is stripped passes" 0 "$P/clean.rpm" -- "1 binaries stripped"
run_case VSTR-26 "an rpm whose binary has a .symtab fails" 1 "$P/bad.rpm" -- "usr/bin/jnext: has a .symtab"
run_case VSTR-27 "an rpm holding no binary fails" 1 "$P/docs.rpm" -- "docs.rpm: holds no executable"
run_case VSTR-28 "a deb whose binary is stripped passes" 0 "$P/clean.deb" -- "1 binaries stripped"
run_case VSTR-29 "a deb whose binary has a .symtab fails" 1 "$P/bad.deb" -- "usr/bin/jnext: has a .symtab"
run_case VSTR-30 "a deb holding no binary fails" 1 "$P/docs.deb" -- "docs.deb: holds no executable"
run_case VSTR-31 "a zip whose binaries are stripped passes" 0 "$P/clean.zip" -- "1 binaries stripped"
run_case VSTR-32 "a zip holding a DLL with debug sections fails" 1 "$P/bad.zip" -- "libstdc++-6.dll: has .debug_* sections"
run_case VSTR-33 "a zip holding no binary fails" 1 "$P/docs.zip" -- "docs.zip: holds no executable"
# VSTR-34  A relative package path still reaches rpm2cpio after the unpack
#          directory change (an early version resolved it from the wrong cwd).
out=$(cd "$WORK" && PATH="$BIN:$PATH" bash "$SCRIPT" pkg/bad.rpm 2>&1); rc=$?
if [ "$rc" -eq 1 ] && grep -qF "usr/bin/jnext: has a .symtab" <<<"$out"; then
    ok VSTR-34 "a relative rpm path is unpacked and checked"
else
    bad VSTR-34 "a relative rpm path is unpacked and checked [exit $rc]"; printf '%s\n' "$out" | sed -e 's/^/        | /' | head -10
fi

printf '\nTotal: %d  Passed: %d  Failed: %d\n' "$((pass + fail))" "$pass" "$fail"
[ "$fail" -eq 0 ]
