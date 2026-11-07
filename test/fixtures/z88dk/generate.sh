#!/bin/bash
# Regenerates the z88dk ground-truth fixtures (GH #343): builds src/ with the
# local z88dk installs (2.4, master before the fixes, master after them, and a nightly with the float-format symbols) and writes the normalised outputs under $DEST
# (default: this script's directory). See README.md.
#   bash test/fixtures/z88dk/generate.sh
# Exits non-zero if a build's outcome differs from its expected one.
#   Z88DK_24      z88dk 2.4 install  (default ~/src/spectrum/z88dk-original-2.4)
#   Z88DK_MASTER  master BEFORE the debug-record fixes, 97731e4e29 (default ~/src/spectrum/z88dk-original-97731e4e29)
#   Z88DK_FIXED   master AFTER the fixes, 0f0a1cf70e (default ~/src/spectrum/z88dk-original-latest; GH #438)
#   Z88DK_NIGHTLY the nightly z88dk-20261107-965fd0e628-26852 (default ~/src/spectrum/z88dk-original-965fd0e628; GH #477): a source kit with
#                 its own prebuilt libraries and no .git, its commit is in its NIGHTLY file; the only install with the float-format map symbols
set -euo pipefail
export LANG=C LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
DEST=${DEST:-$HERE}
SRC=$HERE/src
Z24=$(realpath "${Z88DK_24:-$HOME/src/spectrum/z88dk-original-2.4}")
ZM=$(realpath "${Z88DK_MASTER:-$HOME/src/spectrum/z88dk-original-97731e4e29}")
ZF=$(realpath "${Z88DK_FIXED:-$HOME/src/spectrum/z88dk-original-latest}")
ZN=$(realpath "${Z88DK_NIGHTLY:-$HOME/src/spectrum/z88dk-original-965fd0e628}")
WORK=$(mktemp -d "${TMPDIR:-/tmp}/z88fix.XXXXXX")

commit_of() {  # $1 = install: its git HEAD, or the commit in its NIGHTLY file (a nightly tarball has no .git)
    if [[ -f $1/NIGHTLY ]]; then awk '/^commit:/ { print $2 }' "$1/NIGHTLY"; else git -C "$1" rev-parse HEAD; fi; }

# z80asm's symbol-name encoding of a path: [A-Za-z0-9] kept, anything else _%02x.
enc() { local s=$1 o= c i; for ((i = 0; i < ${#s}; i++)); do c=${s:i:1}
        if [[ $c == [A-Za-z0-9] ]]; then o+=$c; else o+=$(printf '_%02x' "'$c"); fi; done
        printf '%s' "$o"; }

# fixture modules: every map line of these is kept
MODS='^(fpfmt_c|fpmix48_c|fpmix32_c|main_c|util_c|game_util_c|asmfn_asm|banked_c|bmain_c|bankin_asm|incmid_c|intarg_c|longsw_c|calleejp_c|calleejpfn_asm|blocks_c|bits_c|fm_c|u5_c|fl_c|X__xm_c|X1st_c|xm_c|_1st_c|__xm_c|dot_c|dutil_c|vars_c|other_c|bk_c|bkmain_c|fc_c|ext_use_c|ext_def_c|cross_c|floats_c|f3254_c|ifelse_c|x_c|shadow_c|stub1_c|stub2_c|statics_c|shadowagg_c)$'

trim_map() {  # $1 = raw map, $2 = extra module whose addr lines are kept (KEEP_CRT), $3 = for a dot command, the groups that have a prog_<G>.bin; stdout = kept lines, original order
    awk -v MODS="$MODS" -v CRT="${2:-}" -v BINS="${3:-}" -v EXTRA="${KEEP_EXTRA:-}" '
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
      if (n ~ /^(l_debug_push_frame|l_debug_pop_frame|__debug_framepointer|banked_call|l_jphl)$/) { print; next }
      if (EXTRA != "" && n ~ EXTRA) { print; next } }   # KEEP_EXTRA: library names the rows of one build address (GH #349 R1pp)
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
#          xmod   : ./xm.c 1st.c (GH #438: z80asm names the modules X__xm_c and X1st_c; the file list keeps the ./)
#          classic1:<file> : just that file for +zx with the classic library (GH #438: the float-library builds)
#          fpmix  : fpmix48.c --math48 and fpmix32.c --math32 compiled apart, linked with --math32 for +zx (GH #477: two float formats in one map)
#          cvars  : vars.c other.c (GH #431: C variables of every type, -debug records)
#          extern : ext_use.c ext_def.c (GH #433: `extern int g;` in one module, `int g` in the other)
#          cbanked: bk.c -c with code, const and data in PAGE_20, then bkmain.c bk.o (GH #431)
#          (GH #434: the floats builds use single:floats.c, float constants whose bytes are the value formatter's oracle)
#          (GH #434: the cross builds use single:cross.c, a function whose body crosses an 8 KB slot)
#          (GH #431: the fc builds use single:fc.c, a __z88dk_fastcall and a __z88dk_callee function)
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
        xmod)   files=(./xm.c 1st.c) ;;
        classic1:*) files=("${kind#classic1:}"); zcc[${#zcc[@]}-1]=+zx; link=(-m --list -o prog -create-app) ;;
        single:*) IFS=, read -ra files <<< "${kind#single:}" ;;
        dot:*)  files=(dot.c dutil.c); link=(-m --list "-subtype=${kind#dot:}" -o prog -create-app) ;;
        subdir) mkdir -p "$b/game"; mv "$b/util.c" "$b/game/util.c"; files=(main.c game/util.c asmfn.asm); set -- "$@" -I. ;;
        fpmix)  zcc[${#zcc[@]}-1]=+zx; link=(-m --list -o prog -create-app)
                for u in fpmix48:--math48 fpmix32:--math32; do
                    echo "+ zcc +zx $* ${u#*:} --list -c ${u%%:*}.c -o ${u%%:*}.o"
                    (cd "$b" && "${zcc[@]}" "$@" "${u#*:}" --list -c "${u%%:*}.c" -o "${u%%:*}.o") || rc=$?
                done
                set -- "$@" --math32; files=(fpmix48.o fpmix32.o) ;;
        cvars)  files=(vars.c other.c) ;;
        extern) files=(ext_use.c ext_def.c) ;;
        cbanked) echo "+ zcc +zxn $* --codesegPAGE_20 --constsegPAGE_20 --datasegPAGE_20 --list -c bk.c -o bk.o"
                 (cd "$b" && "${zcc[@]}" "$@" --codesegPAGE_20 --constsegPAGE_20 --datasegPAGE_20 --list -c bk.c -o bk.o) || rc=$?
                 files=(bkmain.c bk.o) ;;
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
    { echo "# z88dk $(commit_of "$Z") ($inst)"; normalise "$Z" < "$b/build.log" | sed "s|$b/||g"; } > "$out/build.log"
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
        if [[ -f $Z/NIGHTLY ]]; then
            sed 's/^/nightly /' "$Z/NIGHTLY"
        else
            echo "commit: $(git -C "$Z" rev-parse HEAD)"
            echo "describe: $(git -C "$Z" describe --tags)"
            echo "modified tracked files: $(git -C "$Z" status --porcelain --untracked-files=no | awk '{print $2}' | tr '\n' ' ')"
        fi
        echo "zcc: $(head -n1 <<<"$zcc_out")"
        echo "zsdcc: $(grep -m1 '^Build' <<<"$zsdcc_out")"
    } > "$d/PROVENANCE"
}

for z in "$Z24" "$ZM" "$ZF" "$ZN"; do [[ -x $z/bin/zcc ]] || { echo "no zcc in $z" >&2; exit 1; }; done
rm -rf "${DEST:?}/v2.4" "${DEST:?}/v2.4-dotn" "${DEST:?}/master" "${DEST:?}/master-fixed" "${DEST:?}/master-fixed-zsdcc" "${DEST:?}/master-fixed-80cc" "${DEST:?}/master-fixed-sccz80" "${DEST:?}/nightly" "${DEST:?}/v2.4-fp"
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
# GH #349 R1': long arithmetic (l_long_sub: POP IX ... JP (IX)) and switch (l_case: POP HL ... JP (HL)) are returns with no RET
build "$Z24" v2.4 sccz80-longsw single:longsw.c -clib=new -debug
# GH #349 R1'': a __z88dk_callee body with SP above its slot jumps internally (JP (HL)); bit_beep_raw_callee loops by JP (IX)
KEEP_EXTRA='^(bit_beep_raw_callee|asm_bit_close)$' build "$Z24" v2.4 sccz80-calleejp single:calleejp.c,calleejpfn.asm -clib=new -debug
# GH #351: NextZXOS dot commands (the dot/dotx/dotn subtypes, startupoffset $100/$200/$300)
build "$Z24" v2.4 sccz80-dot    dot:dot        -clib=new -debug
build "$Z24" v2.4 sccz80-dotx   dot:dotx       -clib=new -debug
build "$Z24" v2.4 sdcc-dot      dot:dot        -clib=sdcc_iy -SO2 --c-code-in-asm
# The dotn build lives beside, not under, v2.4/: its C symbols are refused (main bank unplaceable), and
# Z88CN-10 walks every map under v2.4/ and master/ expecting each to show a C name.
build "$Z24" v2.4-dotn sccz80 dot:dotn -clib=new -debug
# GH #431: C variable records (__CDBINFO__) of every type, per compiler; see README "C variables"
RUN="-pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000"
build "$Z24" v2.4 sccz80-cvars        cvars   -clib=new -debug
EXPECT_FAIL=1 build "$Z24" v2.4 sdcc-cvars cvars -clib=sdcc_iy -debug
build "$Z24" v2.4 sccz80-cvars-banked cbanked -clib=new -debug $RUN
build "$Z24" v2.4 sccz80-fc           single:fc.c -clib=new -debug
# GH #434: cross() is 8 KB of code, so its tail is in the next 8 KB slot (F-A)
build "$Z24" v2.4 sccz80-cross        single:cross.c -clib=new -debug
# GH #434 stage 2: float constants in z88dk's 48-bit format
build "$Z24" v2.4 sccz80-floats       single:floats.c -clib=new -debug
# GH #438: the debug-record bugs (block scope, bitfields, fastcall parameters, unsized arrays): README "Debug-record bugs"
build "$Z24" v2.4 sccz80-blocks single:blocks.c -clib=new -debug
build "$Z24" v2.4 sccz80-bits   single:bits.c   -clib=new -debug
build "$Z24" v2.4 sccz80-fm     single:fm.c     -clib=new -debug

provenance "$ZM" master
build "$ZM" master sccz80-debug plain          -clib=new -debug
KEEP_CRT=1 build "$ZM" master sdcc-debug   plain          -clib=sdcc_iy -debug
build "$ZM" master 80cc-debug   plain          -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-incmid single:incmid.c -clib=new -debug
build "$ZM" master 80cc-incmid  single:incmid.c -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-intarg single:intarg.c -clib=new -debug
build "$ZM" master 80cc-intarg  single:intarg.c -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-longsw single:longsw.c -clib=new -debug
KEEP_EXTRA='^(bit_beep_raw_callee|asm_bit_close)$' build "$ZM" master sccz80-calleejp single:calleejp.c,calleejpfn.asm -clib=new -debug
build "$ZM" master 80cc-longsw  single:longsw.c -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-dot   dot:dot        -clib=new -debug
build "$ZM" master sccz80-dotx  dot:dotx       -clib=new -debug
build "$ZM" master sdcc-dot     dot:dot        -clib=sdcc_iy -debug
build "$ZM" master 80cc-dot     dot:dot        -clib=new -compiler=80cc -debug
build "$ZM" master sdcc-banked  banked:PAGE_20:kw -clib=sdcc_iy -debug
build "$ZM" master sdcc-banked-run banked:PAGE_20:kw -clib=sdcc_iy -debug -pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000
KEEP_CRT=1 build "$ZM" master sccz80-banked-crt banked:PAGE_20:kw -clib=new -debug -pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000
KEEP_CRT=1 build "$ZM" master classic-bank10 banked:BANK_10 -clib=classic
# GH #445: producible since z88dk v2.4-2883 (Z1 is fixed on master; v2.4 still fails)
build "$ZM" master classic-debug  banked:BANK_10 -clib=classic -debug
build "$ZM" master sccz80-cvars        cvars   -clib=new -debug
build "$ZM" master sdcc-cvars          cvars   -clib=sdcc_iy -debug
build "$ZM" master 80cc-cvars          cvars   -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-cvars-banked cbanked -clib=new -debug $RUN
build "$ZM" master sdcc-cvars-banked   cbanked -clib=sdcc_iy -debug $RUN
build "$ZM" master 80cc-cvars-banked   cbanked -clib=new -compiler=80cc -debug $RUN
build "$ZM" master sccz80-extern     extern  -clib=new -debug
build "$ZM" master sdcc-extern       extern  -clib=sdcc_iy -debug
build "$ZM" master sccz80-fc           single:fc.c -clib=new -debug
build "$ZM" master sdcc-fc             single:fc.c -clib=sdcc_iy -debug
build "$ZM" master 80cc-fc             single:fc.c -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-cross        single:cross.c -clib=new -debug
build "$ZM" master sdcc-cross          single:cross.c -clib=sdcc_iy -debug
build "$ZM" master 80cc-cross          single:cross.c -clib=new -compiler=80cc -debug
build "$ZM" master sccz80-floats       single:floats.c -clib=new -debug
build "$ZM" master sdcc-floats         single:floats.c -clib=sdcc_iy -debug
# GH #434 stage 3 (P1): zcc -O0 keeps zsdcc's C_LINE records but drops every __CDBINFO__ record
build "$ZM" master sdcc-cvars-O0       cvars   -clib=sdcc_iy -debug -O0

# GH #438: the same bugs on master before the six fixes (97731e4e29), plus 80cc, zsdcc and the float libraries
build "$ZM" master sccz80-blocks single:blocks.c -clib=new -debug
build "$ZM" master sccz80-bits   single:bits.c   -clib=new -debug
build "$ZM" master sccz80-fm     single:fm.c     -clib=new -debug
build "$ZM" master sccz80-xmod   xmod            -clib=new -debug
build "$ZM" master sdcc-blocks   single:blocks.c -clib=sdcc_iy -debug
build "$ZM" master sdcc-bits     single:bits.c   -clib=sdcc_iy -debug
build "$ZM" master sdcc-xmod     xmod            -clib=sdcc_iy -debug
build "$ZM" master 80cc-blocks   single:blocks.c -clib=new -compiler=80cc -debug
build "$ZM" master 80cc-bits     single:bits.c   -clib=new -compiler=80cc -debug
build "$ZM" master 80cc-fm       single:fm.c     -clib=new -compiler=80cc -debug
build "$ZM" master 80cc-u5       single:u5.c     -clib=new -compiler=80cc -debug
build "$ZM" master 80cc-xmod     xmod            -clib=new -compiler=80cc -debug
build "$ZM" master classic-mbf32  classic1:fl.c  -debug --math-mbf32
build "$ZM" master classic-math32 classic1:fl.c  -debug --math32

# GH #438: master after the debug-record fixes (upstream 0f0a1cf70e: sccz80, 80cc, ticks and, with #3262 and #3265, zsdcc changed since 97731e4e29)
provenance "$ZF" master-fixed
provenance "$ZF" master-fixed-zsdcc
provenance "$ZF" master-fixed-80cc
provenance "$ZF" master-fixed-sccz80
build "$ZF" master-fixed sccz80-cvars  cvars         -clib=new -debug
build "$ZF" master-fixed sccz80-fc     single:fc.c   -clib=new -debug
build "$ZF" master-fixed sccz80-blocks single:blocks.c -clib=new -debug
build "$ZF" master-fixed sccz80-bits   single:bits.c   -clib=new -debug
build "$ZF" master-fixed sccz80-fm     single:fm.c     -clib=new -debug
build "$ZF" master-fixed 80cc-cvars    cvars         -clib=new -compiler=80cc -debug
build "$ZF" master-fixed 80cc-bits     single:bits.c -clib=new -compiler=80cc -debug
build "$ZF" master-fixed 80cc-fm       single:fm.c   -clib=new -compiler=80cc -debug
build "$ZF" master-fixed 80cc-u5       single:u5.c   -clib=new -compiler=80cc -debug
# zsdcc after the fixes (GH #438): #3262 (no TAB after the C_LINE function name) and #3265 (the -debug module names are the whole source path)
build "$ZF" master-fixed-zsdcc sdcc-cvars    cvars         -clib=sdcc_iy -debug
build "$ZF" master-fixed-zsdcc sdcc-blocks   single:blocks.c -clib=sdcc_iy -debug
build "$ZF" master-fixed-zsdcc sdcc-xmod     xmod          -clib=sdcc_iy -debug
build "$ZF" master-fixed-zsdcc sdcc-debug    plain         -clib=sdcc_iy -debug
# 80cc with the block records of PR #3243 (GH #438 S4a): a function with a `for` that declares a variable, a brace-less inner `for` and code out of source order
build "$ZF" master-fixed-80cc 80cc-scope single:f3254.c -clib=new -compiler=80cc -debug
# GH #438 S4b: 80cc stamps some code with another block's scope (an if/else's then-branch jump, a trailing block's epilogue, hoisted loop code); a function named x; shadowing on the three compilers
build "$ZF" master-fixed-80cc 80cc-ifelse single:ifelse.c -clib=new -compiler=80cc -debug
build "$ZF" master-fixed-80cc 80cc-x single:x.c -clib=new -compiler=80cc -debug
build "$ZF" master-fixed-80cc 80cc-shadow single:shadow.c -clib=new -compiler=80cc -debug
build "$ZF" master-fixed-sccz80 sccz80-shadow single:shadow.c -clib=new -debug
build "$ZF" master-fixed-zsdcc sdcc-shadow single:shadow.c -clib=sdcc_iy -debug
build "$ZF" master-fixed-zsdcc sdcc-stub single:stub1.c,stub2.c -clib=sdcc_iy -debug
# GH #438 S4b2 (review 8): block-scoped static locals (G1) and zsdcc writing two F: records for a function that has one (G3)
build "$ZF" master-fixed-sccz80 sccz80-static single:statics.c -clib=new -debug
build "$ZF" master-fixed-80cc 80cc-static single:statics.c -clib=new -compiler=80cc -debug
build "$ZF" master-fixed-zsdcc sdcc-static single:statics.c -clib=sdcc_iy -debug
# GH #353 review (epic): an aggregate shadowed by an inner aggregate of its name, and two block statics of one name in one function (zsdcc; sccz80 rejects the program)
build "$ZF" master-fixed-zsdcc sdcc-shadowagg single:shadowagg.c -clib=sdcc_iy -debug
build "$ZF" master-fixed-80cc 80cc-shadowagg single:shadowagg.c -clib=new -compiler=80cc -debug

# GH #477: z88dk master from 2026-11-06 writes the float format of every C module into the map (__fp_mode, __fp_exponent_bias,
# __fp_mantissa_size); one build per format and compiler, every one -debug. nightly/ is the only root built with that install; v2.4-fp/
# is the same program on 2.4, which writes none (the fallback A/B). Spectrum builds only: README "Floats".
provenance "$ZN" nightly
provenance "$Z24" v2.4-fp
FP='single:fpfmt.c'
build "$ZN" nightly sccz80-zx-math48  classic1:fpfmt.c -debug --math48
build "$ZN" nightly sccz80-zx-genmath classic1:fpfmt.c -debug --genmath
build "$ZN" nightly sccz80-zx-lmz     classic1:fpfmt.c -debug -lmz
build "$ZN" nightly sccz80-zx-math32  classic1:fpfmt.c -debug --math32
build "$ZN" nightly sccz80-zx-lm      classic1:fpfmt.c -debug -lm
build "$ZN" nightly sccz80-zx-am9511  classic1:fpfmt.c -debug --am9511
build "$ZN" nightly sccz80-zx-mbf32   classic1:fpfmt.c -debug --math-mbf32
build "$ZN" nightly sccz80-zx-mbf40   classic1:fpfmt.c -debug -Cc-fp-mode=mbf40
build "$ZN" nightly sccz80-zx-mbf64   classic1:fpfmt.c -debug --math-mbf64
build "$ZN" nightly sccz80-zx-bbc     classic1:fpfmt.c -debug --math-bbc
build "$ZN" nightly sccz80-zx-z88     classic1:fpfmt.c -debug -Cc-math-z88 -lz88_math
build "$ZN" nightly sccz80-zx-dai32   classic1:fpfmt.c -debug --math-dai32
build "$ZN" nightly sccz80-zx-bias129 classic1:fpfmt.c -debug --math48 -Cc-fp-exponent-bias=129 -Cc-fp-mantissa-size=4
build "$ZN" nightly 80cc-zx-math48    classic1:fpfmt.c -debug -compiler=80cc --math48
build "$ZN" nightly 80cc-zx-math32    classic1:fpfmt.c -debug -compiler=80cc --math32
build "$ZN" nightly 80cc-zx-mbf32     classic1:fpfmt.c -debug -compiler=80cc --math-mbf32
# fl.c does float arithmetic, so these two link their library and the map names it (__code_fp_<lib>_size): the format beats that guess
build "$ZN" nightly sccz80-zx-fl-mbf32 classic1:fl.c -debug --math-mbf32
build "$ZN" nightly sccz80-zx-fl-dai32 classic1:fl.c -debug --math-dai32
build "$ZN" nightly sccz80-zxn        $FP -clib=new -debug
build "$ZN" nightly sccz80-zxn-math32 $FP -clib=new -debug --math32
build "$ZN" nightly 80cc-zxn          $FP -clib=new -compiler=80cc -debug
build "$ZN" nightly sccz80-zx-mix     fpmix -debug
build "$Z24" v2.4-fp sccz80-zxn       $FP -clib=new -debug

# every file the rows hash: sources, this script, derive-scopes.py, the outputs (README.md and the manifest itself excluded)
(cd "$DEST" && find src generate.sh derive-scopes.py v2.4 v2.4-dotn master master-fixed master-fixed-zsdcc master-fixed-80cc master-fixed-sccz80 nightly v2.4-fp -type f | LC_ALL=C sort | xargs sha256sum) > "$DEST/MANIFEST.sha256"
if (( BAD )); then echo "unexpected build outcome; scratch kept: $WORK" >&2; exit 1; fi
rm -rf "$WORK"
