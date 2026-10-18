#!/usr/bin/env bash
# Lines of code (comments and blank lines excluded), per directory and in total.
# Run through `make kloc-count`, which passes the colour escapes in BOLD/CYAN/RESET.
#
# Three sections, every line counted exactly once:
#
#   src    C++ (*.cpp *.h) under src/: one row per DISCOVERED subdirectory, plus
#          one row for the files directly in src/ (main.cpp).
#   test   C++ under test/: one row per discovered subdirectory, plus one for the
#          files directly in test/.
#   tools  everything else that is code, from `git ls-files` so build trees and
#          caches never count: shell, Python, Perl, build files (Makefile,
#          CMakeLists.txt, *.cmake, *.mk), demo programs, and the C/C++ outside
#          the two sections above (tools/, test/*.c fixtures). Rows are split by
#          top-level directory, discovered. Not counted: third_party/ and the
#          generated renders (doc/man, doc/user-guide, doc/developer-guide), plus
#          documentation, data and assets (doc/ ROM disassemblies, *.md, images).
#
# The row list is DISCOVERED, never written down: a new subsystem or a new tracked
# script needs no edit here and cannot silently go uncounted.
#
# `grep -h` is load-bearing. With several files grep prefixes every output line
# with `filename:`, anchoring the following ^-patterns against the path instead of
# the code, so the comment filters matched NOTHING and this count included comments
# (reported total 165,897 -> 105,803).
#
# The table is drawn with Unicode box-drawing characters written literally (owner
# decision 2026-10-18: U+2500-U+257F are exempt from the ASCII rule). Column widths
# count characters: every padded field is ASCII, so bytes = columns.

set -u
cd "$(dirname "$0")/.."

BOLD=${BOLD:-}; CYAN=${CYAN:-}; RESET=${RESET:-}
# ${VAR} values arrive as literal "\033[1m" text from make; make printf expand them.
BOLD=$(printf '%b' "$BOLD"); CYAN=$(printf '%b' "$CYAN"); RESET=$(printf '%b' "$RESET")

# Comment/blank filters, one per language family. Input: NUL-separated file list.
count_c()    { xargs -0 -r grep -h -v '^[[:space:]]*$' | grep -v '^[[:space:]]*//' | grep -v '^[[:space:]]*/\*' | grep -v '^[[:space:]]*\*' | wc -l; }
count_hash() { xargs -0 -r grep -h -v '^[[:space:]]*$' | grep -v '^[[:space:]]*#' | wc -l; }
count_semi() { xargs -0 -r grep -h -v '^[[:space:]]*$' | grep -v '^[[:space:]]*;' | wc -l; }

# Table buffer: kind (row|sub|sep|total), label, count.
K=(); L=(); N=()
add() { K+=("$1"); L+=("$2"); N+=("$3"); }

# Files on the build side that are counted in a section by `find`, as a NUL list.
cpp_files() { # dir [maxdepth]
    if [ $# -eq 2 ]; then find "$1" -maxdepth "$2" -type f \( -name '*.cpp' -o -name '*.h' \) -print0
    else find "$1" -type f \( -name '*.cpp' -o -name '*.h' \) -print0; fi
}

section_cpp() { # section-name root
    local name=$1 root=$2 sub=0 dir n
    add sep "" ""
    for dir in $(find "$root" -mindepth 1 -maxdepth 1 -type d | sort); do
        n=$(cpp_files "$dir" | count_c)
        [ "$n" -gt 0 ] || continue
        add row "$dir" "$n"; sub=$((sub + n))
    done
    n=$(cpp_files "$root" 1 | count_c)
    if [ "$n" -gt 0 ]; then add row "$root (top-level files)" "$n"; sub=$((sub + n)); fi
    add sub "$name subtotal" "$sub"
    TOTAL=$((TOTAL + sub))
}

TOTAL=0
section_cpp "src" src
section_cpp "test" test

# --- tools: tracked, non-C++-in-src/test code ----------------------------------
TRACKED=$(mktemp); trap 'rm -f "$TRACKED"' EXIT
git ls-files -z -- . ':!third_party' ':!doc/man' ':!doc/user-guide' ':!doc/developer-guide' > "$TRACKED"

# group <label> <counter> <ere over the whole path>: one row per top-level dir.
tools_sub=0
group() {
    local label=$1 counter=$2 re=$3 top n
    local list; list=$(grep -zP -- "$re" "$TRACKED" | tr '\0' '\n' | awk -F/ '{print (NF==1) ? "." : $1}' | sort -u)
    for top in $list; do
        if [ "$top" = "." ]; then
            n=$(grep -zP -- "$re" "$TRACKED" | grep -zvP '/' | $counter)
            add row "$label (root files)" "$n"
        else
            n=$(grep -zP -- "$re" "$TRACKED" | grep -zP -- "^$top/" | $counter)
            add row "$label $top/" "$n"
        fi
        tools_sub=$((tools_sub + n))
    done
}

add sep "" ""
group "shell"  count_hash '(\.sh$|^test/.*\.inc$)'
group "python" count_hash '\.py$'
group "perl"   count_hash '\.(pl|pm)$'
group "build"  count_hash '((^|/)(Makefile|GNUmakefile|CMakeLists\.txt)$|\.(cmake|mk)$|^packaging/debian/rules$)'
group "z80 asm" count_semi '^(demo|test)/.*\.asm$'
# C/C++/JS: demo programs, tools/ and fixtures. Excludes the .cpp/.h under src/ and
# test/ already counted above (any other top-level directory is included); the demo
# .inc files are z88dk #pragma headers.
group "c/c++/js" count_c '(\.(c|cc|hpp|js)$|^demo/.*\.inc$|^(?!(src|test)/).*\.(cpp|h)$)'
add sub "tools subtotal" "$tools_sub"
TOTAL=$((TOTAL + tools_sub))
add total "TOTAL" "$TOTAL"

# --- print ---------------------------------------------------------------------
W=${#L[0]}; for l in "${L[@]}"; do [ ${#l} -gt "$W" ] && W=${#l}; done
HDR="Path"; [ ${#HDR} -gt "$W" ] && W=${#HDR}
CW=8
H='─'; V='│'
rep() { local i; for ((i = 0; i < $2; i++)); do printf '%s' "$1"; done; }
border() { # left mid right
    printf '%s' "$1"; rep "$H" $((W + 2)); printf '%s' "$2"; rep "$H" $((CW + 2)); printf '%s\n' "$3"
}
TL='┌'; TM='┬'; TR='┐'
ML='├'; MM='┼'; MR='┤'
BL='└'; BM='┴'; BR='┘'
line() { # style label count
    printf '%s %s%-*s%s %s %s%*s%s %s\n' "$V" "$1" "$W" "$2" "$RESET" "$V" "$1" "$CW" "$3" "$RESET" "$V"
}

printf '\n%sLines of code (excluding comments and blank lines):%s\n\n' "$BOLD" "$RESET"
border "$TL" "$TM" "$TR"
line "$BOLD" "$HDR" "Lines"
for i in "${!K[@]}"; do
    case ${K[$i]} in
        sep)   border "$ML" "$MM" "$MR" ;;
        row)   line "$CYAN" "${L[$i]}" "${N[$i]}" ;;
        sub)   line "$BOLD" "${L[$i]}" "${N[$i]}" ;;
        total) border "$ML" "$MM" "$MR"; line "$BOLD" "${L[$i]}" "${N[$i]}" ;;
    esac
done
border "$BL" "$BM" "$BR"
printf '\n'
