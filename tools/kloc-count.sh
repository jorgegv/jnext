#!/usr/bin/env bash
# Lines of code (comments and blank lines excluded), per directory and in total.
# Run through `make kloc-count` (three rows: src, test, tools, then TOTAL) or
# `make kloc-count-detailed` (--detailed: every row). One counting pass feeds both
# tables, so the summary's rows are the detailed table's subtotals by construction.
# The make targets pass the colour escapes in BOLD/CYAN/RESET.
#
# Three sections, every line counted exactly once:
#
#   src    C++ (*.cpp *.h) under src/: one row per DISCOVERED subdirectory, plus
#          one row for the files directly in src/ (main.cpp).
#   test   C++ under test/: one row per discovered subdirectory, plus one for the
#          files directly in test/.
#   tools  everything else that is code: shell, Python, Perl, build files (Makefile,
#          CMakeLists.txt, *.cmake, *.mk, CI workflows, Dockerfiles, the rpm spec),
#          Z80 asm, and the C-family (C, C#, JS, and the C++ outside src/ and test/).
#          Rows are split by top-level directory, discovered.
#
# All three read `git ls-files` only, so build trees, caches and untracked scratch
# files never count. Not counted: third_party/, the generated renders (doc/man,
# doc/user-guide, doc/developer-guide), vendored third-party code (NextSync, three.js;
# see the exclusions below) and documentation, data and assets (doc/ ROM
# disassemblies, *.md, images, the tokenised tools/debug/*.bas). demo/dapr-nexlib+tests
# is imported and modified in-tree, so it counts. Comment markers: # for shell,
# Python, Perl and build files (plus @# -# +# for make recipes), ; for asm, and
# // /* * for the C-family.
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
# Make recipe comments are written "<TAB>@# ..." (also -# and +#): the prefix is not code.
count_make() { xargs -0 -r grep -h -v '^[[:space:]]*$' | grep -v '^[[:space:]]*[@+-]*#' | wc -l; }

# Table buffer: kind (row|sub|sep|total), label, count.
K=(); L=(); N=()
add() { K+=("$1"); L+=("$2"); N+=("$3"); }

# Every section reads ONE list: the tracked files (git ls-files, NUL-separated), so
# untracked scratch files never count and names with spaces survive. Excluded:
#   third_party/ and the generated renders (doc/man, doc/user-guide, doc/developer-guide);
#   test/00regression/nextsync/: NextSync by Jari Komppa, vendored unmodified, third-party
#     software under test (its README says so);
#   the vendored three.js bundle, by explicit path so that adding "cjs" to a
#     pattern below can never pull 54k lines of third-party code in.
TRACKED=$(mktemp); trap 'rm -f "$TRACKED"' EXIT
git ls-files -z -- . ':!third_party' ':!doc/man' ':!doc/user-guide' ':!doc/developer-guide' \
    ':!test/00regression/nextsync' \
    ':!demo/dapr-nexlib+tests/interactive/test11isometric/scripts/three.cjs' > "$TRACKED" \
    || { echo "kloc-count: git ls-files failed" >&2; exit 1; }
[ -s "$TRACKED" ] || { echo "kloc-count: empty tracked-file list" >&2; exit 1; }

# The row key of a path (awk, via ENVIRON so no backslash is interpreted): depth 1 =
# first component ("." for root files); depth 2 = first two ("src/." for files directly
# in src/).
KEYAWK='{ n = split($0, a, "/");
          if (D == 1) k = (n == 1) ? "." : a[1];
          else        k = (n <= 2) ? a[1] "/." : a[1] "/" a[2]; }'

SUB=0
# group <label> <counter> <PCRE over the whole path> <depth>: one row per key.
group() {
    local label=$1 counter=$2 re=$3 depth=$4 key lab n
    while IFS= read -r key; do
        n=$(grep -zP -- "$re" "$TRACKED" \
            | K=$key awk -v D="$depth" -v RS='\0' -v ORS='\0' "$KEYAWK"' k == ENVIRON["K"]' \
            | $counter)
        if [ "$depth" = 1 ]; then
            if [ "$key" = "." ]; then lab="$label (root files)"; else lab="$label $key/"; fi
        else
            case $key in */.) lab="${key%/.} (top-level files)" ;; *) lab=$key ;; esac
        fi
        # Zero rows exist only for a pattern that matched, so a 0 is a real 0 line count
        # (e.g. a file of comments), never a skipped row.
        add row "$lab" "$n"; SUB=$((SUB + n))
    done < <(grep -zP -- "$re" "$TRACKED" | awk -v D="$depth" -v RS='\0' "$KEYAWK"' { print k }' | LC_ALL=C sort -u)
}

TOTAL=0
section() { add sep "" ""; SUB=0; }
section_end() { add sub "$1 subtotal" "$SUB"; TOTAL=$((TOTAL + SUB)); }

section; group "" count_c '^src/.*\.(cpp|h)$' 2;  section_end "src"
section; group "" count_c '^test/.*\.(cpp|h)$' 2; section_end "test"

# --- tools: everything else that is code ---------------------------------------
section
group "shell"  count_hash '(\.sh$|^test/.*\.inc$)' 1
group "python" count_hash '\.py$' 1
group "perl"   count_hash '\.(pl|pm)$' 1
# Build files: Makefiles, CMake, Debian rules, CI workflows, Dockerfiles, the rpm spec.
group "build"  count_make '((^|/)(Makefile|GNUmakefile|CMakeLists\.txt)$|\.(cmake|mk)$|^packaging/debian/rules$|^\.github/workflows/.*\.ya?ml$|^docker/Dockerfile\.|\.spec$)' 1
group "z80 asm" count_semi '^(demo|test)/.*\.asm$' 1
# C-family: C, C#, JS, demo .inc (z88dk #pragma headers) and the C++ outside src/ and
# test/ (those two are the first sections). Any other top-level directory is included.
group "c-family" count_c '(\.(c|cc|hpp|js|cs)$|^demo/.*\.inc$|^(?!(src|test)/).*\.(cpp|h)$)' 1
section_end "tools"
add total "TOTAL" "$TOTAL"

# --- print ---------------------------------------------------------------------
DETAILED=0
case ${1:-} in
    "") ;;
    --detailed) DETAILED=1 ;;
    *) echo "usage: kloc-count.sh [--detailed]" >&2; exit 2 ;;
esac
if [ "$DETAILED" -eq 0 ]; then
    # Summary: each section subtotal becomes a plain row; the rows and inner separators go.
    K2=(sep); L2=(""); N2=("")
    for i in "${!K[@]}"; do
        case ${K[$i]} in
            sub)   K2+=(row); L2+=("${L[$i]% subtotal}"); N2+=("${N[$i]}") ;;
            total) K2+=(total); L2+=("${L[$i]}"); N2+=("${N[$i]}") ;;
        esac
    done
    K=("${K2[@]}"); L=("${L2[@]}"); N=("${N2[@]}")
fi
W=0; for l in "${L[@]}"; do [ ${#l} -gt "$W" ] && W=${#l}; done
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
