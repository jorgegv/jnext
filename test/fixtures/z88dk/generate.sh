#!/bin/bash
# Regenerates the z88dk ground-truth fixtures (GH #343): builds src/ with the
# two local z88dk installs and writes the normalised outputs under $DEST
# (default: this script's directory). See README.md.
#   bash test/fixtures/z88dk/generate.sh
# Exits non-zero if a build's outcome differs from its expected one.
#   Z88DK_24      z88dk 2.4 install  (default ~/src/spectrum/z88dk-original-2.4)
#   Z88DK_MASTER  master install     (default ~/src/spectrum/z88dk-original-latest)
set -euo pipefail
export LANG=C LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
DEST=${DEST:-$HERE}
SRC=$HERE/src
Z24=$(realpath "${Z88DK_24:-$HOME/src/spectrum/z88dk-original-2.4}")
ZM=$(realpath "${Z88DK_MASTER:-$HOME/src/spectrum/z88dk-original-latest}")
WORK=$(mktemp -d "${TMPDIR:-/tmp}/z88fix.XXXXXX")

# z80asm's symbol-name encoding of a path: [A-Za-z0-9] kept, anything else _%02x.
enc() { local s=$1 o= c i; for ((i = 0; i < ${#s}; i++)); do c=${s:i:1}
        if [[ $c == [A-Za-z0-9] ]]; then o+=$c; else o+=$(printf '_%02x' "'$c"); fi; done
        printf '%s' "$o"; }

# fixture modules: every map line of these is kept
MODS='^(main_c|util_c|game_util_c|asmfn_asm|banked_c|bmain_c|bankin_asm|incmid_c|intarg_c|dot_c|dutil_c)$'

trim_map() {  # $1 = raw map, $2 = extra module whose addr lines are kept (KEEP_CRT), $3 = for a dot command, the groups that have a prog_<G>.bin; stdout = kept lines, original order
    awk -v MODS="$MODS" -v CRT="${2:-}" -v BINS="${3:-}" '
    function nm(l,  n) { n = l; sub(/ *=.*/, "", n); return n }
    BEGIN { nb = split(BINS, bg, " "); for (i = 1; i <= nb; i++) keep[bg[i]] = 1 }   # a group with a section binary keeps its bounds even when __<G>_size is 0 (dotx MAIN, GH #351)
    NR == FNR { n = nm($0)
                if (n ~ /^__.*_size$/ && $0 !~ /= \$0+ ;/) { s = n; sub(/^__/, "", s); sub(/_size$/, "", s); keep[s] = 1 }
                next }
    { n = nm($0); split($0, p, " ; "); split(p[2], f, ", ")
      if (n ~ /_2ftmp_2ftmpXX/) next                     # zcc temp-file markers: random names
      if (f[4] ~ MODS) { print; next }
      if (CRT != "" && f[4] == CRT && f[1] == "addr") { print; next }   # KEEP_CRT: addr symbols of the CRT module
      if (n ~ /^__(C|ASM)_LINE_/) { print; next }        # library (crt0) line records too
      if (n ~ /^__.*_(head|tail|size)$/) { s = n; sub(/^__/, "", s); sub(/_(head|tail|size)$/, "", s); if (s in keep) print; next }
      if (BINS != "" && n ~ /^(startupoffset|__crt_org_code)$/) { print; next }   # dot commands only (GH #351)
      if (n ~ /^CRT_ORG_(CODE|BANK_10|BANK_10_L|BANK_10_H|PAGE_20|PAGE_21)$/) { print; next }
      if (n ~ /^(l_debug_push_frame|l_debug_pop_frame|__debug_framepointer|banked_call|l_jphl)$/) { print; next } }
    ' "$1" "$1"
}

normalise() {  # $1 = install root; stdin -> stdout
    local e; e=$(enc "$1/")
    sed -e "s|$1/|z88dk/|g" -e "s|$e|z88dk_2f|g" \
        -e 's|tmp\(zcc\)\{0,1\}XX[A-Za-z0-9]\{6\}|tmp\1XXXXXXXX|g' \
        -e 's|\(Module compile time: \).*|\1(normalised)|'
}

BAD=0
# build <install> <instdir> <name> <kind> <zcc flags...>   (EXPECT_FAIL=1: recorded failure)
#   kind = plain  : main.c util.c asmfn.asm
#          subdir : main.c game/util.c asmfn.asm
#          single:<file> : just that file (GH #348: the #include-after-a-function program)
#          tape   : main.c util.c asmfn.asm for +zx (a .tap, GH #344 stage 3)
#          dot:<subtype> : dot.c dutil.c as a NextZXOS dot command (-subtype=dot|dotx|dotn, GH #351)
#          banked:<SEG>[:kw] : banked.c -c --codeseg<SEG>, then bmain.c bankin.asm banked.o
build() {
    local Z=$1 inst=$2 name=$3 kind=$4; shift 4
    local b=$WORK/$inst/$name out=$DEST/$inst/$name
    mkdir -p "$b" "$out"; cp "$SRC"/* "$b"/
    local zcc=(env TMPDIR=/tmp PATH="$Z/bin:$PATH" ZCCCFG="$Z/lib/config" zcc +zxn)
    local link=(-m --list -subtype=nex -o prog -create-app) files rc=0
    {
    case $kind in
        plain)  files=(main.c util.c asmfn.asm) ;;
        tape)   files=(main.c util.c asmfn.asm); zcc[${#zcc[@]}-1]=+zx; link=(-m --list -o prog -create-app) ;;
        single:*) files=("${kind#single:}") ;;
        dot:*)  files=(dot.c dutil.c); link=(-m --list "-subtype=${kind#dot:}" -o prog -create-app) ;;
        subdir) mkdir -p "$b/game"; mv "$b/util.c" "$b/game/util.c"; files=(main.c game/util.c asmfn.asm); set -- "$@" -I. ;;
        banked:*) local seg=${kind#banked:} kw=; [[ $seg == *:kw ]] && { kw=-DUSE_BANKED_KW; seg=${seg%:kw}; }
                  echo "+ zcc +zxn $* $kw --codeseg$seg --list -c banked.c -o banked.o"
                  (cd "$b" && "${zcc[@]}" "$@" $kw "--codeseg$seg" --list -c banked.c -o banked.o) || rc=$?
                  set -- "$@" $kw; files=(bmain.c bankin.asm banked.o) ;;
    esac
    if (( rc == 0 )); then
        echo "+ zcc ${zcc[${#zcc[@]}-1]} $* ${link[*]} ${files[*]}"
        (cd "$b" && "${zcc[@]}" "$@" "${link[@]}" "${files[@]}") || rc=$?
    fi
    } > "$b/build.log" 2>&1
    { echo "# z88dk $(git -C "$Z" rev-parse HEAD) ($inst)"; normalise "$Z" < "$b/build.log" | sed "s|$b/||g"; } > "$out/build.log"
    echo "rc=$rc" >> "$out/build.log"
    if (( rc != 0 )); then
        echo "NOT PRODUCIBLE: $inst/$name (rc=$rc)"
        (( ${EXPECT_FAIL:-0} )) || { echo "UNEXPECTED FAILURE: $inst/$name" >&2; BAD=1; }
        return 0
    fi
    (( ${EXPECT_FAIL:-0} )) && { echo "UNEXPECTED SUCCESS: $inst/$name (recorded as not producible)" >&2; BAD=1; }
    # KEEP_CRT=1: also keep the CRT listing (prog.lis) and the CRT module's own map lines (GH #348)
    local crt=
    if (( ${KEEP_CRT:-0} )); then
        crt=$(awk '$2 == "MODULE" { m = $3 } END { print m }' "$b/prog.lis")
        [[ -n $crt ]] || { echo "KEEP_CRT: no MODULE line in $inst/$name prog.lis" >&2; BAD=1; }
        normalise "$Z" < "$b/prog.lis" > "$out/prog.lis"
    fi
    local bins=   # dot commands only (GH #351): the groups that have a section binary
    if [[ $kind == dot:* ]]; then for f in "$b"/prog_*.bin; do f=${f##*/prog_}; bins+="${f%.bin} "; done; fi
    trim_map "$b/prog.map" "$crt" "$bins" | normalise "$Z" > "$out/prog.map"
    (cd "$b" && for l in *.c.lis *.asm.lis game/*.c.lis; do [[ -f $l ]] || continue
        mkdir -p "$out/$(dirname "$l")"; normalise "$Z" < "$l" > "$out/$l"; done)
    for f in "$b"/prog.nex "$b"/prog.tap; do [[ -f $f ]] && cp "$f" "$out/"; done
    for f in "$b"/prog_*.bin "$b"/PROG "$b"/PROG.X; do [[ -f $f ]] && cp "$f" "$out/"; done   # PROG, PROG.X: a dot command (GH #351)
    [[ -s $b/prog ]] && cp "$b/prog" "$out/prog"        # classic: the main binary has no group suffix
    echo "built: $inst/$name"
}

provenance() {  # $1 = install, $2 = dir name
    local Z=$1 d=$DEST/$2; mkdir -p "$d"
    local zcc_out zsdcc_out
    zcc_out=$(env PATH="$Z/bin:$PATH" ZCCCFG="$Z/lib/config" zcc 2>&1 || true)
    zsdcc_out=$("$Z/bin/z88dk-zsdcc" --version </dev/null 2>&1 || true)
    {
        echo "commit: $(git -C "$Z" rev-parse HEAD)"
        echo "describe: $(git -C "$Z" describe --tags)"
        echo "modified tracked files: $(git -C "$Z" status --porcelain --untracked-files=no | awk '{print $2}' | tr '\n' ' ')"
        echo "zcc: $(head -n1 <<<"$zcc_out")"
        echo "zsdcc: $(grep -m1 '^Build' <<<"$zsdcc_out")"
    } > "$d/PROVENANCE"
}

for z in "$Z24" "$ZM"; do [[ -x $z/bin/zcc ]] || { echo "no zcc in $z" >&2; exit 1; }; done
rm -rf "${DEST:?}/v2.4" "${DEST:?}/v2.4-dotn" "${DEST:?}/master"
provenance "$Z24" v2.4
build "$Z24" v2.4 sccz80-debug  plain          -clib=new -debug
build "$Z24" v2.4 sdcc-SO0      plain          -clib=sdcc_iy -SO0 --c-code-in-asm
KEEP_CRT=1 build "$Z24" v2.4 sdcc-SO2      plain          -clib=sdcc_iy -SO2 --c-code-in-asm
EXPECT_FAIL=1 build "$Z24" v2.4 sdcc-debug    plain          -clib=sdcc_iy -debug
build "$Z24" v2.4 sccz80-page20 banked:PAGE_20 -clib=new -debug
build "$Z24" v2.4 sccz80-bank10 banked:BANK_10 -clib=new -debug
build "$Z24" v2.4 sccz80-banked banked:PAGE_20:kw -clib=new -debug
build "$Z24" v2.4 sdcc-banked   banked:PAGE_20:kw -clib=sdcc_iy --c-code-in-asm
# GH #345: the banked fixtures above link a program that cannot run (newlib's
# CRT_ORG_PAGE_20 is 16-bit, so banked_call pages in page 0; the stack at $FF58
# is in the banking segment). The -run builds carry the two pragmas that fix it.
build "$Z24" v2.4 sccz80-banked-run banked:PAGE_20:kw -clib=new -debug -pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000
# GH #426 F4: the same build with its CRT listing kept: code_crt_init shares the banking driver's bytes.
KEEP_CRT=1 build "$Z24" v2.4 sccz80-banked-crt banked:PAGE_20:kw -clib=new -debug -pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000
KEEP_CRT=1 build "$Z24" v2.4 classic-bank10 banked:BANK_10 -clib=classic
EXPECT_FAIL=1 build "$Z24" v2.4 classic-debug plain          -clib=classic -debug
build "$Z24" v2.4 sccz80-subdir subdir         -clib=new -debug
build "$Z24" v2.4 sccz80-incmid single:incmid.c -clib=new -debug
build "$Z24" v2.4 sccz80-tape   tape           -clib=new -debug
# GH #349 R1: an int parameter and local, so sccz80 calls l_gintspsp (EX (SP),HL; JP (HL): a return with no RET)
build "$Z24" v2.4 sccz80-intarg single:intarg.c -clib=new -debug
# GH #351: NextZXOS dot commands (the dot/dotx/dotn subtypes, startupoffset $100/$200/$300)
build "$Z24" v2.4 sccz80-dot    dot:dot        -clib=new -debug
build "$Z24" v2.4 sccz80-dotx   dot:dotx       -clib=new -debug
build "$Z24" v2.4 sdcc-dot      dot:dot        -clib=sdcc_iy -SO2 --c-code-in-asm
# The dotn build lives beside, not under, v2.4/: its C symbols are refused (main bank unplaceable), and
# Z88CN-10 walks every map under v2.4/ and master/ expecting each to show a C name.
build "$Z24" v2.4-dotn sccz80 dot:dotn -clib=new -debug

provenance "$ZM" master
build "$ZM" master sccz80-debug plain          -clib=new -debug
KEEP_CRT=1 build "$ZM" master sdcc-debug   plain          -clib=sdcc_iy -debug
build "$ZM" master 80cc-debug   plain          -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-incmid single:incmid.c -clib=new -debug
build "$ZM" master 80cc-incmid  single:incmid.c -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-intarg single:intarg.c -clib=new -debug
build "$ZM" master 80cc-intarg  single:intarg.c -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-dot   dot:dot        -clib=new -debug
build "$ZM" master sccz80-dotx  dot:dotx       -clib=new -debug
build "$ZM" master sdcc-dot     dot:dot        -clib=sdcc_iy -debug
build "$ZM" master 80cc-dot     dot:dot        -clib=new -compiler=80cc -debug
build "$ZM" master sdcc-banked  banked:PAGE_20:kw -clib=sdcc_iy -debug
build "$ZM" master sdcc-banked-run banked:PAGE_20:kw -clib=sdcc_iy -debug -pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000
KEEP_CRT=1 build "$ZM" master sccz80-banked-crt banked:PAGE_20:kw -clib=new -debug -pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000
KEEP_CRT=1 build "$ZM" master classic-bank10 banked:BANK_10 -clib=classic
EXPECT_FAIL=1 build "$ZM" master classic-debug  banked:BANK_10 -clib=classic -debug

# every file the rows hash: sources, this script, the outputs (README.md and the manifest itself excluded)
(cd "$DEST" && find src generate.sh v2.4 v2.4-dotn master -type f | LC_ALL=C sort | xargs sha256sum) > "$DEST/MANIFEST.sha256"
if (( BAD )); then echo "unexpected build outcome; scratch kept: $WORK" >&2; exit 1; fi
rm -rf "$WORK"
