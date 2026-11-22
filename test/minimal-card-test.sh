#!/usr/bin/env bash
#
# Contract test for the make-minimal-card tool (core/minimal_card.cpp, the minimal NextZXOS
# boot card; replaces the earlier mtools shell script and its suite).
# Builds cards from the cached distribution image into a scratch directory under mktemp,
# reads them back with FOREIGN readers (mtools, sfdisk, python zipfile), and never writes the
# source. No image is ever kept: the scratch directory is removed on exit.
#
# Oracle: the source image itself (read back with mtools) and FatFs for the cluster rule
# (third_party/fatfs/ff.c MAX_FAT16 0xFFF5: FAT32 needs MORE than 65525 clusters).
#
# Rows MINIMG-01 .. MINIMG-13, MINIMG-17, MINIMG-19, MINIMG-20 (15), and MINIMG-21 .. MINIMG-26
# (GH #509: extra distribution paths, --dirs) (21), MINIMG-27 (the Makefile orders sdcard-image
# before this script; GH #510) (22), and MINIMG-28 .. MINIMG-30 (GH #549: /dot on the card, /DOT refused in --dirs, the Pages list fits) (25). A missing tool or
# source image FAILS MINIMG-01; there is no SKIP path.
#
# Retired with the script (GH #458), by the same hand as the mtools builder:
#   MINIMG-11  too few clusters: now MCARD-18 (minimal_card_test), the builder's own refusal.
#   MINIMG-14  a ':' in the source path broke mtools' rc parser; the builder uses no mtools.
#   MINIMG-15, MINIMG-16  hard-link placement of the output: the builder creates the card with
#              fopen "wbx", exclusive by construction (MCARD-15); the dangling-symlink half of
#              MINIMG-15 lives on in MINIMG-09.
#   MINIMG-18  the shell tool preflight: the builder needs no external tools.
# MINIMG-17 changed meaning: the script's copies of the two patches are gone, so it now pins
# tools/fix-sdcard-image.sh against sdcard_provisioner.cpp (the tool has no copy: it calls the
# provisioner's functions).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TOOL="${MAKE_MINIMAL_CARD:-$ROOT/build/tools/make-minimal-card/make-minimal-card}"
SRC="${JNEXT_REGRESSION_SD_STATE_DIR:-$HOME/.jnext}/sdcard/cspect-next-1gb.img"   # one variable to rename for another release; the seam lets a branch test a candidate card (sd-master-ensure.sh honours it too)
PROV="$ROOT/src/core/sdcard_provisioner.cpp"
EXPECTED_ROWS=25
export MTOOLS_SKIP_CHECK=1

total=0; passed=0; failed=0
ids_seen=" "
row() {
    local id=$1 ok=$2 text=$3
    total=$((total + 1))
    if [[ "$ids_seen" == *" $id "* ]]; then
        echo "  FAIL $id duplicate row id"; failed=$((failed + 1)); return
    fi
    ids_seen+="$id "
    if [[ "$ok" == 1 ]]; then
        echo "  PASS $id $text"; passed=$((passed + 1))
    else
        echo "  FAIL $id $text"; failed=$((failed + 1))
    fi
}

T=$(mktemp -d "${TMPDIR:-$HOME/tmp}/minimg-test.XXXXXX" 2>/dev/null || mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/a" "$T/b"

finish() {
    printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
    if [[ $total -ne $EXPECTED_ROWS ]]; then
        echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1
    fi
    [[ $failed -eq 0 ]] || exit 1
    exit 0
}

# The pinned card digest: the third SHA-256 of kDistribution (zip, image, card).
pinned_card=$(sed -n '/^const Pins kDistribution/,/^};/p' "$PROV" | grep -oE '"[0-9a-f]{64}"' | sed -n 3p | tr -d '"' || true)

# ---- MINIMG-01: tools, source, a build ----
missing=()
for t in mformat mcopy mdir minfo mmd mattrib sfdisk truncate sha256sum python3; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
if [[ ${#missing[@]} -gt 0 || ! -f "$SRC" || ! -x "$TOOL" || ${#pinned_card} != 64 ]]; then
    [[ ${#missing[@]} -eq 0 ]] || echo "  missing tools: ${missing[*]} (dnf install mtools util-linux coreutils python3)"
    [[ -f "$SRC" ]] || echo "  missing source image $SRC (make sdcard-image, or --sdcard-download-force)"
    [[ -x "$TOOL" ]] || echo "  missing $TOOL (make unit-test-build)"
    [[ ${#pinned_card} == 64 ]] || echo "  cannot read the pinned card digest from $PROV"
    row MINIMG-01 0 "tools, the builder and the distribution image are present"
    for i in 02 03 04 05 06 07 08 09 10 12 13 17 19 20 21 22 23 24 25 26 28 29 30; do
        total=$((total + 1)); failed=$((failed + 1)); echo "  FAIL MINIMG-$i not run: MINIMG-01 failed"
    done
    finish
fi
src_sha0=$(sha256sum "$SRC" | cut -d' ' -f1)
rc=0
out=$("$TOOL" "$SRC" "$T/a/minimal.img" 2>"$T/a/stderr") || rc=$?
CARD="$T/a/minimal.img"
last=$(tail -n1 <<<"$out")
n=$(stat -c %s "$CARD" 2>/dev/null || echo -1)
h=$(sha256sum "$CARD" 2>/dev/null | cut -d' ' -f1 || true)
ok=0
if [[ $rc -eq 0 && "$last" == "$T/a/minimal.img: $n bytes, sha256 $h" && "$h" == "$pinned_card" ]]; then ok=1; fi
row MINIMG-01 "$ok" "builds from the distribution image (pinned); last line is dst, size, sha256 and the sha256 is the pinned card digest"
[[ $ok == 1 ]] || { [[ -s "$T/a/stderr" ]] && tail -n 5 "$T/a/stderr"; echo "  got $h, pinned $pinned_card"; }
sha_a=$h

# ---- MINIMG-02: partition table ----
dump=$(sfdisk --dump "$CARD" 2>/dev/null || true)
nparts=$(grep -c ': start=' <<<"$dump" || true)
ok=0
if grep -qx 'label-id: 0x00000000' <<<"$dump" && [[ "$nparts" == 1 ]] \
   && grep -Eq ': start= +63, size= +81857, type=c, bootable' <<<"$dump" \
   && [[ "$n" == 41943040 ]]; then ok=1; fi
row MINIMG-02 "$ok" "one bootable type-c partition at sector 63 spanning the 40 MiB card, label-id 0"

# ---- MINIMG-03: FAT32, 1-sector clusters, serial, cluster count ----
mi=$(minfo -i "$CARD@@32256" :: 2>/dev/null || true)
field() { sed -n "s/^$1: *\"\{0,1\}\([^\"]*\)\"\{0,1\}\$/\1/p" <<<"$mi" | head -n1; }
reserved=$(field 'reserved (boot) sectors'); fats=$(field 'fats'); big=$(field 'big size'); big=${big%% *}
fatlen=$(sed -n 's/^Big fatlen=\([0-9]*\)/\1/p' <<<"$mi"); csz=$(field 'cluster size')
csz=${csz%% *}; serial=$(field 'serial number'); dtype=$(sed -n 's/^disk type="\(FAT32\) *"/\1/p' <<<"$mi")
hidden=$(field 'hidden sectors'); hidden=${hidden%% *}; heads=$(field 'heads'); spt=$(field 'sectors per track')
label=$(sed -n 's/^disk label="\(.*\)"$/\1/p' <<<"$mi")
mdir_out=$(mdir -i "$CARD@@32256" :: 2>/dev/null || true)
nolabel=0; grep -q 'has no label' <<<"$mdir_out" && nolabel=1
clusters=0
if [[ -n "$reserved" && -n "$fats" && -n "$big" && -n "$fatlen" && "${csz:-0}" -gt 0 ]]; then
    clusters=$(( (big - reserved - fats * fatlen) / csz ))
fi
ok=0
if [[ "$dtype" == FAT32 && "$csz" == 1 && "$serial" == 4A4E5854 && $clusters -gt 65525 \
      && "$hidden" == 63 && "$heads" == 255 && "$spt" == 63 \
      && "$label" == "NO NAME    " && $nolabel == 1 ]]; then ok=1; fi
row MINIMG-03 "$ok" "FAT32, 1-sector clusters, serial 4A4E5854, $clusters clusters (> 65525, FatFs rule), hidden 63, 255 heads, 63 sectors/track, no label"

# ---- MINIMG-04: exactly the boot set ----
# The card keeps the SOURCE's spelling of an entry it replaces (the 26.11 distribution has
# CONFIG.INI, which the old mtools script wrote back as config.ini), so config.ini is compared
# without regard to case, as FAT does.
norm_cfg() { sed 's#^::/machines/next/config\.ini$#::/machines/next/config.ini#I'; }
card_list=$(mdir -/ -a -b -i "$CARD@@32256" ::/ 2>/dev/null | norm_cfg | LC_ALL=C sort || true)
start=$(sfdisk --dump "$SRC" 2>/dev/null | sed -n 's/^.* : start= *\([0-9]*\),.*/\1/p' | head -n1)
src_all=$(mdir -/ -a -b -i "$SRC@@$((start * 512))" ::/ 2>/dev/null || true)
# A distribution that already carries CONFIG.INI (26.11 does, 24.11 did not) has it replaced by the
# patched one, so the source's own entry is dropped whatever its case.
src_list=$( { grep -iE '^::/(TBBLUE\.FW|machines|nextzxos|sys|dot|LICENSE\.md|README\.md)(/|$)' <<<"$src_all" \
                | grep -viFx '::/machines/next/config.ini' || true
              echo "::/machines/next/config.ini"; } | norm_cfg | LC_ALL=C sort)
ok=0
if [[ -n "$card_list" && "$card_list" == "$src_list" ]]; then ok=1; fi
row MINIMG-04 "$ok" "card holds exactly the boot set plus config.ini ($(wc -l <<<"$card_list") entries)"
[[ $ok == 1 ]] || diff <(echo "$card_list") <(echo "$src_list") | head -n 6 || true

# ---- MINIMG-05: contents and mtimes ----
mkdir -p "$T/xc" "$T/xs"
# Only the boot set is extracted from the source: the card's own top-level names.
( export LC_ALL=C.UTF-8 TZ=UTC
  mcopy -s -m -n -i "$CARD@@32256" ::/ "$T/xc/" </dev/null 2>/dev/null
  while IFS= read -r top; do
      mcopy -s -m -n -i "$SRC@@$((start * 512))" "::/${top%/}" "$T/xs/" </dev/null 2>/dev/null
  done < <(mdir -b -i "$CARD@@32256" ::/ 2>/dev/null | sed 's|^::/||') ) || true
ok=1; bad=""
while IFS= read -r f; do
    case "${f,,}" in machines/next/zx81.rom|machines/next/config.ini) continue;; esac
    if [[ ! -f "$T/xs/$f" ]] || ! cmp -s "$T/xc/$f" "$T/xs/$f"; then ok=0; bad+=" $f"; fi
done < <(cd "$T/xc" && find . -type f -printf '%P\n' | LC_ALL=C sort)
tl() { (cd "$1" && find . -type f -printf '%P %T@\n' | { grep -vi -e '^machines/next/zx81.rom ' -e '^machines/next/config.ini ' || true; } | LC_ALL=C sort) || true; }
# The card is a subset of the source: compare the card's files against the same files of the source.
tc=$(tl "$T/xc"); ts=$(tl "$T/xs" | LC_ALL=C sort)
missing_t=$(comm -23 <(cat <<<"$tc") <(cat <<<"$ts") | head -n 3 || true)
[[ -z "$missing_t" ]] || { ok=0; bad+=" mtime:$missing_t"; }
[[ -n "$tc" ]] || ok=0
# Attributes (R/H/S/A) over the boot set; the two patched files and the root line are excluded.
attr() { { mattrib -/ -i "$1" ::/ 2>/dev/null || true; } | { grep -iE '::/(TBBLUE\.FW|machines|nextzxos|sys|dot|LICENSE\.md|README\.md)(/|$)' || true; } \
         | { grep -vi -e '::/machines/next/zx81\.rom$' -e '::/machines/next/config\.ini$' || true; } | LC_ALL=C sort; }
attr_c=$(attr "$CARD@@32256"); attr_s=$(attr "$SRC@@$((start * 512))")
[[ -n "$attr_c" && "$attr_c" == "$attr_s" ]] || { ok=0; bad+=" attributes"; }
row MINIMG-05 "$ok" "every copied file is byte-identical with the same mtime, and every entry has the source's attributes${bad:+ (bad:$bad)}"

# ---- MINIMG-06: the two patches ----
FIXSH="$ROOT/tools/fix-sdcard-image.sh"
sed -n "/<<'CONFIGEOF'/,/^CONFIGEOF\$/p" "$FIXSH" | sed '1d;$d' > "$T/config.expected"
ok=1
cfgcard=$(find "$T/xc/machines/next" -maxdepth 1 -iname config.ini | head -n1)
[[ -n "$cfgcard" ]] && cmp -s "$T/config.expected" "$cfgcard" && [[ -s "$T/config.expected" ]] || ok=0
zsha=$(grep -oE '^ZX81_16K_SHA256=[0-9a-f]{64}$' "$FIXSH" | cut -d= -f2 || true)
zsrc="$T/xs/machines/next/zx81.rom"; zcard="$T/xc/machines/next/zx81.rom"
arm=passthrough
if [[ "$(stat -c %s "$zsrc" 2>/dev/null || echo 0)" == 16384 && "$(sha256sum "$zsrc" 2>/dev/null | cut -d' ' -f1 || true)" == "$zsha" ]]; then
    arm=doubled
    cat "$zsrc" "$zsrc" > "$T/zx81.expected"
else
    cp "$zsrc" "$T/zx81.expected" 2>/dev/null || { : > "$T/zx81.expected"; ok=0; }
fi
cmp -s "$T/zx81.expected" "$zcard" || ok=0
row MINIMG-06 "$ok" "config.ini is fix-sdcard-image.sh's text; zx81.rom arm: $arm"

# ---- MINIMG-07: reproducible, other directory, other TZ, later time ----
sleep 2
rc=0
TZ=JNX-9 "$TOOL" "$SRC" "$T/b/other.img" >/dev/null 2>&1 || rc=$?
sha_b=$(sha256sum "$T/b/other.img" 2>/dev/null | cut -d' ' -f1 || true)
ok=0; [[ $rc -eq 0 && -n "$sha_b" && "$sha_b" == "$sha_a" ]] && ok=1
row MINIMG-07 "$ok" "second build (other directory, TZ, 2 s later) has the same sha256"

# ---- MINIMG-08 is taken last (needs every run to have happened) ----

# ---- MINIMG-09: existing destination refused ----
mkdir -p "$T/d"
echo keep > "$T/d/exists.img"
rc_a=0; err_a=$("$TOOL" "$SRC" "$T/d/exists.img" 2>&1 >/dev/null) || rc_a=$?
ln -s nowhere.img "$T/d/dangle.img"
rc_b=0; err_b=$("$TOOL" "$SRC" "$T/d/dangle.img" 2>&1 >/dev/null) || rc_b=$?
ok=1
[[ $rc_a == 1 && "$err_a" == *"destination exists"* && "$(cat "$T/d/exists.img")" == keep ]] || ok=0
[[ $rc_b == 1 && "$err_b" == *"destination exists"* && ! -e "$T/d/nowhere.img" && "$(readlink "$T/d/dangle.img")" == nowhere.img ]] || ok=0
row MINIMG-09 "$ok" "an existing file or dangling symlink destination is refused and untouched"

# A SYNTHETIC distribution (licence-clean: invented names and bytes), built with the FOREIGN
# writers mtools (FAT32, partition at 63, 4 KiB clusters) and python zipfile (deflate, the image
# entry first, then two dummy ROM entries).
S="$T/s"; mkdir -p "$S/files/machines/next" "$S/files/nextzxos" "$S/files/sys" "$S/files/dot" "$S/files/apps" "$T/e"
truncate -s 24M "$S/synth.img"
printf 'label: dos\nunit: sectors\n\nstart=63, type=c\n' | sfdisk -q "$S/synth.img" >/dev/null 2>&1
SI="$S/synth.img@@32256"
mformat -F -c 8 -h 255 -s 63 -H 63 -i "$SI" :: >/dev/null 2>&1
for f in TBBLUE.FW LICENSE.md README.md; do head -c 3000 /dev/urandom > "$S/files/$f"; done
head -c 5000 /dev/urandom > "$S/files/machines/next/48.rom"
head -c 700 /dev/urandom > "$S/files/nextzxos/a.sys"
head -c 600 /dev/urandom > "$S/files/sys/b.bas"
head -c 800 /dev/urandom > "$S/files/dot/c.bin"
head -c 900 /dev/urandom > "$S/files/apps/x.bin"
mmd -i "$SI" ::/machines ::/machines/next ::/nextzxos ::/sys ::/dot ::/apps >/dev/null 2>&1
mcopy -i "$SI" "$S/files/TBBLUE.FW" "$S/files/LICENSE.md" "$S/files/README.md" ::/ >/dev/null 2>&1
mcopy -i "$SI" "$S/files/machines/next/48.rom" ::/machines/next/ >/dev/null 2>&1
mcopy -i "$SI" "$S/files/nextzxos/a.sys" ::/nextzxos/ >/dev/null 2>&1
mcopy -i "$SI" "$S/files/sys/b.bas" ::/sys/ >/dev/null 2>&1
mcopy -i "$SI" "$S/files/dot/c.bin" ::/dot/ >/dev/null 2>&1
mcopy -i "$SI" "$S/files/apps/x.bin" ::/apps/ >/dev/null 2>&1
python3 -I - "$S/synth.img" "$S/synth.zip" <<'PYEOF'
import sys, zipfile
img, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(img, "cspect-next-1gb.img")
    z.writestr("enNextZX.rom", b"synthetic one")
    z.writestr("enNxtmmc.rom", b"synthetic two")
PYEOF

# ---- MINIMG-10: missing boot-set path refused ----
# A synthetic (mtools-built) source with its partition at sector 2048 and without SYS
# (--unpinned: the pinned image digest would refuse first). It also proves the partition start
# is read from the MBR (the other synthetic and the real image start at 63).
truncate -s 48M "$S/nosys.img"
printf 'label: dos\nunit: sectors\n\nstart=2048, type=c\n' | sfdisk -q "$S/nosys.img" >/dev/null 2>&1
NI="$S/nosys.img@@$((2048 * 512))"
mformat -F -c 1 -i "$NI" :: >/dev/null 2>&1
mmd -i "$NI" ::/machines ::/nextzxos ::/dot >/dev/null 2>&1
mcopy -i "$NI" "$S/files/dot/c.bin" ::/dot/ >/dev/null 2>&1
mcopy -i "$NI" "$S/files/TBBLUE.FW" "$S/files/LICENSE.md" "$S/files/README.md" ::/ >/dev/null 2>&1
rc=0; err=$("$TOOL" --unpinned "$S/nosys.img" "$T/e/out.img" 2>&1 >/dev/null) || rc=$?
ok=0
if [[ $rc == 1 && "$err" == *"SYS is not in"* && ! -e "$T/e/out.img" ]]; then ok=1; fi
# and the same source WITH SYS builds (the refusal is about SYS, not about the geometry)
mmd -i "$NI" ::/sys >/dev/null 2>&1
mcopy -i "$NI" "$S/files/sys/b.bas" ::/sys/ >/dev/null 2>&1
rc2=0; "$TOOL" --unpinned "$S/nosys.img" "$T/e/with-sys.img" >/dev/null 2>&1 || rc2=$?
[[ $rc2 == 0 && -s "$T/e/with-sys.img" ]] || ok=0
row MINIMG-10 "$ok" "a source without SYS (partition at sector 2048, mtools-built) is refused naming SYS; with SYS it builds"
[[ $ok == 1 ]] || echo "  rc=$rc err=$err rc2=$rc2"

# ---- MINIMG-12: no partition table ----
mkdir -p "$T/g"
truncate -s 1M "$T/g/zero.img"
rc=0; err=$("$TOOL" "$T/g/zero.img" "$T/g/out.img" 2>&1 >/dev/null) || rc=$?
# an MBR signature with no partition entry
head -c 510 /dev/zero > "$T/g/sig.img"; printf '\x55\xAA' >> "$T/g/sig.img"
rc2=0; err2=$("$TOOL" --unpinned "$T/g/sig.img" "$T/g/out2.img" 2>&1 >/dev/null) || rc2=$?
ok=0
[[ $rc == 1 && "$err" == *"not a zip or an SD image"* && ! -e "$T/g/out.img" \
   && $rc2 == 1 && "$err2" == *"no FAT32 partition"* && ! -e "$T/g/out2.img" ]] && ok=1
row MINIMG-12 "$ok" "a source with no partition table (or an MBR with no FAT32 partition) is refused, nothing created"
[[ $ok == 1 ]] || echo "  rc=$rc '$err' rc2=$rc2 '$err2'"

# ---- MINIMG-13: usage ----
mkdir -p "$T/h"
rc=0; err=$(cd "$T/h" && "$TOOL" one.img 2>&1 >/dev/null) || rc=$?
ok=0; [[ $rc == 2 && "$err" == Usage:* && -z "$(ls -A "$T/h")" ]] && ok=1
row MINIMG-13 "$ok" "one argument gives usage and exit 2, nothing created"

# ---- MINIMG-17: the copies of jnext's two patches agree ----
# fix-sdcard-image.sh and sdcard_provisioner.cpp each carry the config.ini text and the 24.11
# zx81.rom hash; the minimal-card tool has no copy (it calls the provisioner's functions).
cfg_of() { sed -n "/<<'CONFIGEOF'/,/^CONFIGEOF\$/p" "$1" | sed '1d;$d'; }
# Files, not $(...): command substitution drops trailing newlines, and a lost or extra final one changes the card's file.
cfg_of "$FIXSH" > "$T/cfg.fix"
cfg_raw=$(sed -n '/static const char\* kIni =/,/;$/p' "$PROV" | grep -o '"[^"]*"' | sed 's/^"//;s/"$//' | tr -d '\n')
printf '%b' "$cfg_raw" > "$T/cfg.cpp"
sha_of() { grep -oE '^ZX81_16K_SHA256=[0-9a-f]{64}$' "$1" | cut -d= -f2 || true; }
sha_fix=$(sha_of "$FIXSH")
sha_cpp=$(tr -d '\n' < "$PROV" | grep -oE 'kZx81Rom16kSha256 =[[:space:]]*"[0-9a-f]{64}"' | grep -oE '[0-9a-f]{64}' || true)
ok=1
[[ -s "$T/cfg.fix" && "$(wc -l < "$T/cfg.fix")" == 28 ]] && cmp -s "$T/cfg.fix" "$T/cfg.cpp" || ok=0
[[ ${#sha_fix} == 64 && "$sha_fix" == "$sha_cpp" ]] || ok=0
row MINIMG-17 "$ok" "config.ini and ZX81_16K_SHA256 are the same in fix-sdcard-image.sh and sdcard_provisioner.cpp"

# ---- MINIMG-19: the zip layer against foreign writers ----
# The synthetic distribution (mtools FAT32 + python zip), through the tool from a file and from
# stdin: the same card. mtools reads the boot set back byte-identical.
rc=0; "$TOOL" --unpinned "$S/synth.zip" "$T/e/z1.img" >"$T/e/z1.out" 2>"$T/e/z1.err" || rc=$?
rc2=0; "$TOOL" --unpinned - "$T/e/z2.img" <"$S/synth.zip" >"$T/e/z2.out" 2>"$T/e/z2.err" || rc2=$?
h1=$(sha256sum "$T/e/z1.img" 2>/dev/null | cut -d' ' -f1 || true); h2=$(sha256sum "$T/e/z2.img" 2>/dev/null | cut -d' ' -f1 || true)
ok=0
[[ $rc == 0 && $rc2 == 0 && -n "$h1" && "$h1" == "$h2" ]] && ok=1
mkdir -p "$T/e/back"
mcopy -s -n -i "$T/e/z1.img@@32256" ::/ "$T/e/back/" </dev/null >/dev/null 2>&1 || ok=0
for f in TBBLUE.FW LICENSE.md README.md machines/next/48.rom nextzxos/a.sys sys/b.bas dot/c.bin; do
    cmp -s "$S/files/$f" "$T/e/back/$f" || { ok=0; echo "  differs: $f"; }
done
[[ ! -e "$T/e/back/apps" && -f "$T/e/back/machines/next/config.ini" ]] || ok=0
row MINIMG-19 "$ok" "a synthetic distribution (mtools image, python zip) builds from a file and from stdin to the same card; mtools reads the boot set back byte-identical"
[[ $ok == 1 ]] || { cat "$T/e/z1.err" "$T/e/z2.err" 2>/dev/null | head -n 4; }

# ---- MINIMG-20: pinned mode refuses a zip that is not the official one ----
rc=0; err=$("$TOOL" "$S/synth.zip" "$T/e/pinned.img" 2>&1 >/dev/null) || rc=$?
zsz=$(grep -oE 'kDistribution = \{' "$PROV" >/dev/null && sed -n '/^const Pins kDistribution/,/^};/p' "$PROV" | grep -oE '[0-9]{7,}ull' | tr -d 'ul' || true)
zhash=$(sed -n '/^const Pins kDistribution/,/^};/p' "$PROV" | grep -oE '"[0-9a-f]{64}"' | sed -n 1p | tr -d '"')
zfile=$(sed -n '/^const Pins kDistribution/,/^};/p' "$PROV" | grep -oE '"sn-emulator-[^"]*\.zip"' | sed -n 1p | tr -d '"')
ok=0
if [[ $rc == 1 && -n "$zfile" && "$err" == *"$zfile"* && "$err" == *"$zhash"* && "$err" == *"$zsz"* \
      && "$err" == *"$(sha256sum "$S/synth.zip" | cut -d' ' -f1)"* && ! -e "$T/e/pinned.img" ]]; then ok=1; fi
row MINIMG-20 "$ok" "the synthetic zip without --unpinned is refused naming the official file and both digests, nothing created"
[[ $ok == 1 ]] || echo "  rc=$rc err=$err"

# ---- MINIMG-21 .. MINIMG-26: extra distribution paths (GH #509) ----
# The oracle is the source image read by mtools: the card made with --dirs holds the boot set,
# config.ini and exactly the named trees (with their parent directories), byte for byte.
mkdir -p "$T/l"
mkdir -p "$T/xd"
( export LC_ALL=C.UTF-8 TZ=UTC
  for d in games apps demos; do
      mcopy -s -m -n -i "$SRC@@$((start * 512))" "::/$d" "$T/xd/" </dev/null 2>/dev/null
  done ) || true
LIST="$T/l/list.img"
rc=0; out=$("$TOOL" --dirs /GAMES,/APPS "$SRC" "$LIST" 2>"$T/l/stderr") || rc=$?
ln_=$(stat -c %s "$LIST" 2>/dev/null || echo -1)
list_card=$(mdir -/ -a -b -i "$LIST@@32256" ::/ 2>/dev/null | norm_cfg | LC_ALL=C sort || true)
extra_src=$(grep -iE '^::/(games|apps)(/|$)' <<<"$src_all" | LC_ALL=C sort || true)
list_src=$( { echo "$src_list"; echo "$extra_src"; } | LC_ALL=C sort )
ok=0
[[ $rc -eq 0 && "$ln_" == 41943040 && -n "$extra_src" && "$list_card" == "$list_src" ]] && ok=1
row MINIMG-21 "$ok" "--dirs /GAMES,/APPS on the real image: exit 0, a 40 MiB card holding the boot set, config.ini and exactly the source's games and apps trees ($(wc -l <<<"$list_card") entries)"
[[ $ok == 1 ]] || { tail -n 3 "$T/l/stderr"; diff <(echo "$list_card") <(echo "$list_src") | head -n 6 || true; }

# ---- MINIMG-22: the two trees are byte-identical, with the source's mtimes and attributes ----
mkdir -p "$T/xl"
( export LC_ALL=C.UTF-8 TZ=UTC
  for d in games apps; do
      mcopy -s -m -n -i "$LIST@@32256" "::/$d" "$T/xl/" </dev/null 2>/dev/null
  done ) || true
ok=1; bad=""
nfiles=0
while IFS= read -r f; do
    nfiles=$((nfiles + 1))
    cmp -s "$T/xl/$f" "$T/xd/$f" || { ok=0; bad+=" $f"; }
done < <(cd "$T/xl" && find . -type f -printf '%P\n' | LC_ALL=C sort)
nsrc=$(cd "$T/xd" && find games apps -type f | wc -l)
[[ $nfiles == "$nsrc" && $nfiles -gt 0 ]] || { ok=0; bad+=" count:$nfiles/$nsrc"; }
tlx() { (cd "$1" && find games apps -type f -printf '%P %T@\n' | LC_ALL=C sort) || true; }
[[ "$(tlx "$T/xl")" == "$(tlx "$T/xd")" ]] || { ok=0; bad+=" mtimes"; }
attr2() { { mattrib -/ -i "$1" ::/ 2>/dev/null || true; } | { grep -iE '::/(games|apps)(/|$)' || true; } | LC_ALL=C sort; }
a_c=$(attr2 "$LIST@@32256"); a_s=$(attr2 "$SRC@@$((start * 512))")
[[ -n "$a_c" && "$a_c" == "$a_s" ]] || { ok=0; bad+=" attributes"; }
row MINIMG-22 "$ok" "all $nfiles files of games and apps are byte-identical with the source, with its mtimes and R/H/S/A bits${bad:+ (bad:$bad)}"
rm -rf "$T/xl"

# ---- MINIMG-23: a path below the root: its parents are created, their other children are not ----
rc=0; "$TOOL" --dirs /games/Next/Nextoid "$SRC" "$T/l/one.img" >/dev/null 2>"$T/l/stderr1" || rc=$?
one_card=$(mdir -/ -a -b -i "$T/l/one.img@@32256" ::/ 2>/dev/null | norm_cfg | LC_ALL=C sort || true)
one_src=$( { echo "$src_list"; grep -iE '^::/games/?$|^::/games/Next/?$|^::/games/Next/Nextoid(/|$)' <<<"$src_all"; } | LC_ALL=C sort )
mkdir -p "$T/xo"
( export LC_ALL=C.UTF-8 TZ=UTC
  mcopy -s -m -n -i "$T/l/one.img@@32256" "::/games" "$T/xo/" </dev/null 2>/dev/null ) || true
ok=1
[[ $rc -eq 0 && -n "$one_card" && "$one_card" == "$one_src" ]] || ok=0
grep -qi '^::/games/Next/Nextoid/' <<<"$one_card" || ok=0
! grep -qiE '^::/games/(Classic48|Next/Orb)' <<<"$one_card" || ok=0
while IFS= read -r f; do cmp -s "$T/xo/$f" "$T/xd/$f" || ok=0; done < <(cd "$T/xo" && find . -type f -printf '%P\n')
[[ -n "$(find "$T/xo" -type f | head -n1)" ]] || ok=0
for d in "::/games" "::/games/Next"; do
    [[ "$(mattrib -i "$T/l/one.img@@32256" "$d" 2>/dev/null | sed 's/ *::.*//')" == "$(mattrib -i "$SRC@@$((start * 512))" "$d" 2>/dev/null | sed 's/ *::.*//')" ]] || ok=0
done
row MINIMG-23 "$ok" "--dirs /games/Next/Nextoid: ::/games and ::/games/Next exist with the source's attributes and hold only the path asked for; its subtree is byte-identical"
[[ $ok == 1 ]] || { tail -n 3 "$T/l/stderr1"; diff <(echo "$one_card") <(echo "$one_src") | head -n 6 || true; }
rm -rf "$T/xo" "$T/l/one.img"

# ---- MINIMG-24: over the 32 MiB bound: refused with the exact size and the limit ----
# What the builder counts is file sizes plus directory clusters (32768 bytes each in this image).
# S = the mtools file sizes of the boot set and the three trees; D = their directories and the root.
rc=0; err=$("$TOOL" --dirs /APPS,/DEMOS,/GAMES "$SRC" "$T/l/big.img" 2>&1 >/dev/null) || rc=$?
S=$( { find "$T/xs" -type f -printf '%s\n'; find "$T/xd" -type f -printf '%s\n'; } | awk '{s += $1} END {print s}')
D=$(( 1 + $(find "$T/xs" -mindepth 1 -type d | wc -l) + $(find "$T/xd" -mindepth 1 -type d | wc -l) ))
N=$(grep -oE ': [0-9]+ bytes, the limit is 33554432' <<<"$err" | grep -oE '^: [0-9]+' | grep -oE '[0-9]+' || true)
ok=0
if [[ $rc == 1 && "$err" == *33554432* && -n "$N" && $N -ge $((S + D * 32768)) && $N -le $((S + 2 * D * 32768)) && ! -e "$T/l/big.img" ]]; then ok=1; fi
row MINIMG-24 "$ok" "--dirs /APPS,/DEMOS,/GAMES is refused naming 33554432 and the counted size ($N bytes: $S of files plus $D directories), no card written"
[[ $ok == 1 ]] || echo "  rc=$rc err=$err S=$S D=$D N=$N"

# ---- MINIMG-25: a path the distribution does not hold ----
rc=0; err=$("$TOOL" --dirs /GAMES/NOPE "$SRC" "$T/l/nope.img" 2>&1 >/dev/null) || rc=$?
ok=0; [[ $rc == 1 && "$err" == *"/games/nope"* && ! -e "$T/l/nope.img" ]] && ok=1
row MINIMG-25 "$ok" "--dirs /GAMES/NOPE is refused naming /games/nope, no card written"
[[ $ok == 1 ]] || echo "  rc=$rc err=$err"

# ---- MINIMG-26: case, a trailing slash and the order do not change the card ----
rc1=0; "$TOOL" --dirs "/apps/, /GAMES" "$SRC" "$T/l/o1.img" >/dev/null 2>&1 || rc1=$?
rc2=0; "$TOOL" --dirs /games,/Apps "$SRC" "$T/l/o2.img" >/dev/null 2>&1 || rc2=$?
h1=$(sha256sum "$T/l/o1.img" 2>/dev/null | cut -d' ' -f1 || true); h2=$(sha256sum "$T/l/o2.img" 2>/dev/null | cut -d' ' -f1 || true)
hl=$(sha256sum "$LIST" 2>/dev/null | cut -d' ' -f1 || true)
ok=0; [[ $rc1 == 0 && $rc2 == 0 && -n "$h1" && "$h1" == "$h2" && "$h1" == "$hl" ]] && ok=1
row MINIMG-26 "$ok" "'/apps/, /GAMES', '/games,/Apps' and '/GAMES,/APPS' give the same card sha256"
rm -f "$LIST" "$T/l/o1.img" "$T/l/o2.img"

# ---- MINIMG-28: the card's /dot is the source's /dot ----
# NextZXOS runs every dot command (and so opens most files from its browser) from C:/DOT (GH #549).
# Oracle: the source image read by mtools. $T/xc and $T/xs (MINIMG-05) hold the card's and the
# source's boot-set trees, /dot among them.
dot_card=$(mdir -/ -a -b -i "$CARD@@32256" ::/ 2>/dev/null | { grep -iE '^::/dot(/|$)' || true; } | LC_ALL=C sort || true)
dot_src=$(grep -iE '^::/dot(/|$)' <<<"$src_all" | LC_ALL=C sort || true)
ok=1; bad=""
[[ -n "$dot_card" && "$dot_card" == "$dot_src" ]] || { ok=0; bad+=" list"; }
nd=0
while IFS= read -r f; do
    nd=$((nd + 1))
    cmp -s "$T/xc/$f" "$T/xs/$f" || { ok=0; bad+=" $f"; }
done < <(cd "$T/xs" && find dot -type f -printf '%p\n' 2>/dev/null | LC_ALL=C sort || true)
nd_card=$(cd "$T/xc" && find dot -type f 2>/dev/null | wc -l || true)
[[ $nd -gt 0 && $nd == "$nd_card" ]] || { ok=0; bad+=" count:$nd/$nd_card"; }
tld() { (cd "$1" && find dot -type f -printf '%p %T@\n' 2>/dev/null | LC_ALL=C sort) || true; }
[[ "$(tld "$T/xc")" == "$(tld "$T/xs")" ]] || { ok=0; bad+=" mtimes"; }
attrd() { { mattrib -/ -i "$1" ::/ 2>/dev/null || true; } | { grep -iE '::/dot(/|$)' || true; } | LC_ALL=C sort; }
ad_c=$(attrd "$CARD@@32256"); ad_s=$(attrd "$SRC@@$((start * 512))")
[[ -n "$ad_c" && "$ad_c" == "$ad_s" ]] || { ok=0; bad+=" attributes"; }
row MINIMG-28 "$ok" "the pinned card's ::/dot is the source's: same entry list, all $nd files byte-identical with the source's mtimes and R/H/S/A bits${bad:+ (bad:$bad)}"

# ---- MINIMG-29: /DOT in --dirs is part of the boot set ----
rc=0; err=$("$TOOL" --dirs /DOT "$SRC" "$T/l/dot.img" 2>&1 >/dev/null) || rc=$?
ok=0
[[ $rc == 1 && "$err" == *"/DOT"* && "$err" == *"boot set"* && ! -e "$T/l/dot.img" ]] && ok=1
row MINIMG-29 "$ok" "--dirs /DOT is refused (exit 1) naming /DOT and the boot set, no card written"
[[ $ok == 1 ]] || echo "  rc=$rc err=$err"

# ---- MINIMG-30: the Pages demo's own list fits ----
# The site config is the oracle (web/pages/index.html.in data-card-dirs) with the builder's 32 MiB
# cap: a later edit of the list, or a later distribution, that no longer fits fails here and not
# in a visitor's browser.
pages_dirs=$(grep -oE 'data-card-dirs="[^"]*"' "$ROOT/web/pages/index.html.in" | head -n1 | sed 's/^data-card-dirs="//;s/"$//' || true)
rc=0; "$TOOL" --dirs "$pages_dirs" "$SRC" "$T/l/pages.img" >/dev/null 2>"$T/l/stderr_p" || rc=$?
np=$(stat -c %s "$T/l/pages.img" 2>/dev/null || echo -1)
pages_card=$(mdir -/ -a -b -i "$T/l/pages.img@@32256" ::/ 2>/dev/null | LC_ALL=C sort || true)
ok=0
[[ -n "$pages_dirs" && $rc == 0 && "$np" == 41943040 ]] && ok=1
grep -qiE '^::/dot/' <<<"$pages_card" || ok=0
mkdir -p "$T/l/pdot"
MTOOLS_SKIP_CHECK=1 mcopy -s -n -i "$T/l/pages.img@@32256" ::/dot "$T/l/pdot/" </dev/null >/dev/null 2>&1 || true
[[ -n "$(find "$T/l/pdot" -type f 2>/dev/null | head -n1 || true)" ]] || ok=0   # an empty /dot is not a pass
IFS=, read -ra pd <<<"$pages_dirs"
for d in "${pd[@]}"; do grep -qiE "^::${d,,}/" <<<"${pages_card,,}" || ok=0; done
row MINIMG-30 "$ok" "the Pages demo's list ($pages_dirs) builds with /dot: exit 0, a 40 MiB card holding ::/dot and every listed directory"
[[ $ok == 1 ]] || { tail -n 3 "$T/l/stderr_p"; echo "  rc=$rc size=$np"; }
rm -rf "$T/l/pages.img" "$T/l/pdot"

# ---- MINIMG-27: the raw image it reads is ensured first ----
# unit-test ran this target BEFORE sdcard-image, so a host whose shared master was another
# distribution's (the first run after the default moved from 26.11 to 26.11a) failed MINIMG-01..05
# on the old image, and passed on the second run. The dependency is what orders them.
deps=$(make -C "$ROOT" -qp -f Makefile : 2>/dev/null | sed -n 's/^minimal-image-test: *//p' | head -n 1 || true)   # -q exits 1: nothing is up to date
ok=0; [[ " $deps " == *" sdcard-image "* ]] && ok=1
row MINIMG-27 "$ok" "make minimal-image-test depends on sdcard-image, which re-derives a master of another distribution before this script reads the raw image (prerequisites: $deps)"

# ---- MINIMG-08: the source was only read ----
src_sha1=$(sha256sum "$SRC" | cut -d' ' -f1)
ok=0; [[ "$src_sha0" == "$src_sha1" ]] && ok=1
row MINIMG-08 "$ok" "the distribution image is byte-identical after all runs"

finish
