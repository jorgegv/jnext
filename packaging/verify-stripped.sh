#!/usr/bin/env bash
#
# verify-stripped.sh <path>...
#
# Fail unless every executable and shared library in each <path> is stripped
# (GH #572). A <path> is a package (.rpm, .deb, .zip, unpacked into a scratch
# directory first), a directory (walked) or a single binary. Binaries are found
# by their magic bytes, never by name, and checked by format:
#
#   ELF      no .symtab and no .debug_* section          (readelf)
#   PE       no COFF symbol and no .debug_* section      (<arch>-w64-mingw32-objdump)
#   Mach-O   no __DWARF segment (otool); an executable defines no symbol but
#            __mh_execute_header and weak ones (dyld coalesces those, so
#            strip keeps them), a dylib or bundle no local one (nm -m -U)
#
# 0xcafebabe opens both a universal Mach-O and a Java class file; the word
# after it tells them apart (a universal binary's arch count is small, a class
# file's major version is 45 or more), and a class file is not a binary.
#
# A <path> holding no binary at all fails too: a check of nothing is not a pass.
#
# Env overrides: READELF, MINGW_OBJDUMP, NM, OTOOL.
#
set -euo pipefail

[ "$#" -ge 1 ] || { echo "usage: $0 <package|dir|binary>..." >&2; exit 2; }

READELF=${READELF:-readelf}
NM=${NM:-nm}
OTOOL=${OTOOL:-otool}

WORK=$(mktemp -d)   # honours TMPDIR; a Windows zip is ~100 MB unpacked
trap 'rm -rf "$WORK"' EXIT

bad=0
checked=0

# magic <file>: elf | pe-i686 | pe-x86_64 | macho | other
magic() {
    local m
    m=$(od -An -tx1 -N 4 "$1" 2>/dev/null | tr -d ' \n')
    case "$m" in
        7f454c46) echo elf ;;
        feedface|feedfacf|cefaedfe|cffaedfe) echo macho ;;
        cafebabe)
            local n
            n=$(od -An -tx1 -j 4 -N 4 "$1" 2>/dev/null | tr -d ' \n')
            if [ -n "$n" ] && [ "$((16#$n))" -lt 45 ]; then echo macho; else echo other; fi ;;
        4d5a*)
            local off pe mach
            off=$(od -An -tu4 -j 0x3c -N 4 "$1" 2>/dev/null | tr -d ' ')
            [ -n "$off" ] || { echo other; return; }
            pe=$(od -An -tx1 -j "$off" -N 4 "$1" 2>/dev/null | tr -d ' \n')
            [ "$pe" = 50450000 ] || { echo other; return; }
            mach=$(od -An -tx2 -j "$((off + 4))" -N 2 "$1" | tr -d ' ')
            case "$mach" in
                014c) echo pe-i686 ;;
                8664) echo pe-x86_64 ;;
                *) echo other ;;
            esac ;;
        *) echo other ;;
    esac
}

fail() { echo "NOT STRIPPED: $1: $2" >&2; bad=1; }

check_elf() {
    local s
    s=$("$READELF" -SW "$1") || { fail "$1" "readelf failed"; return; }
    grep -q '[[:space:]]\.symtab[[:space:]]' <<<"$s" && fail "$1" "has a .symtab"
    grep -q '[[:space:]]\.debug_' <<<"$s" && fail "$1" "has .debug_* sections"
    return 0
}

check_pe() {
    local od=${MINGW_OBJDUMP:-$2-w64-mingw32-objdump} h t
    h=$("$od" -h "$1") || { fail "$1" "$od -h failed"; return; }
    t=$("$od" -t "$1") || { fail "$1" "$od -t failed"; return; }
    grep -q '[[:space:]]\.debug_' <<<"$h" && fail "$1" "has .debug_* sections"
    grep -q '^\[ *[0-9]' <<<"$t" && fail "$1" "has a COFF symbol table"
    return 0
}

# nm -m prints one line per symbol, "<addr> (<seg>,<sect>) <kind> <name>",
# kind being "external", "weak external" or "non-external" (a local symbol); a
# universal binary adds per-arch headers, which are not symbol lines.
check_macho() {
    local l h s syms
    l=$("$OTOOL" -l "$1") || { fail "$1" "otool -l failed"; return; }
    h=$("$OTOOL" -hv "$1") || { fail "$1" "otool -hv failed"; return; }
    s=$("$NM" -m -U "$1") || { fail "$1" "$NM -m -U failed"; return; }
    grep -q 'segname __DWARF' <<<"$l" && fail "$1" "has a __DWARF segment"
    syms=$(grep -E '^[0-9a-f]+ \(' <<<"$s" || true)
    if grep -qw EXECUTE <<<"$h"; then
        [ -n "$(grep -v -e ' weak external ' -e ' __mh_execute_header$' <<<"$syms")" ] \
            && fail "$1" "defines symbols other than __mh_execute_header and weak ones"
    else
        grep -q ' non-external ' <<<"$syms" && fail "$1" "has local symbols"
    fi
    return 0
}

check_tree() {
    local f k
    while IFS= read -r -d '' f; do
        k=$(magic "$f")
        case "$k" in
            elf)       check_elf "$f" ;;
            pe-*)      check_pe "$f" "${k#pe-}" ;;
            macho)     check_macho "$f" ;;
            *)         continue ;;
        esac
        checked=$((checked + 1))
    done < <(find "$1" -type f -print0)
}

n=0
for p in "$@"; do
    n=$((n + 1))
    before=$checked
    if [ -d "$p" ]; then
        check_tree "$p"
    elif [ -f "$p" ]; then
        d="$WORK/$n"
        mkdir -p "$d"
        case "$p" in
            *.rpm) abs=$(cd "$(dirname "$p")" && pwd)/$(basename "$p")
                   (cd "$d" && rpm2cpio "$abs" | cpio -idm --quiet) ;;
            *.deb) dpkg-deb -x "$p" "$d" ;;
            *.zip) unzip -q "$p" -d "$d" ;;
            *)     cp "$p" "$d/" ;;
        esac
        check_tree "$d"
    else
        echo "error: no such file or directory: $p" >&2
        exit 2
    fi
    if [ "$checked" -eq "$before" ]; then
        echo "NOT STRIPPED: $p: holds no executable or shared library to check" >&2
        bad=1
    fi
done

[ "$bad" -eq 0 ] || { echo "verify-stripped: FAILED" >&2; exit 1; }
echo "verify-stripped: $checked binaries stripped in $*"
