#!/usr/bin/env bash
#
# Contract test for tools/make-minimal-sdcard-image.sh (the minimal NextZXOS
# boot card). Builds cards from the cached distribution image into a scratch
# directory under mktemp, reads them back with mtools, and never writes the
# source. No image is ever kept: the scratch directory is removed on exit.
#
# Oracle: the source image itself (read back with mtools) and FatFs for the
# cluster rule (third_party/fatfs/ff.c MAX_FAT16 0xFFF5: FAT32 needs MORE than
# 65525 clusters).
#
# Rows MINIMG-01 .. MINIMG-13. A missing tool or source image FAILS MINIMG-01;
# there is no SKIP path.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT="$ROOT/tools/make-minimal-sdcard-image.sh"
SRC="$HOME/.jnext/sdcard/cspect-next-1gb.img"   # one variable to rename for another release
EXPECTED_ROWS=16
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

# ---- MINIMG-01: tools, source, a build ----
missing=()
for t in mformat mcopy mdir minfo mmd sfdisk truncate sha256sum realpath; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
if [[ ${#missing[@]} -gt 0 || ! -f "$SRC" ]]; then
    [[ ${#missing[@]} -eq 0 ]] || echo "  missing tools: ${missing[*]} (dnf install mtools util-linux coreutils)"
    [[ -f "$SRC" ]] || echo "  missing source image $SRC (make sdcard-image, or --sdcard-download-force)"
    row MINIMG-01 0 "tools and the distribution image are present"
    for i in 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16; do
        total=$((total + 1)); failed=$((failed + 1)); echo "  FAIL MINIMG-$i not run: MINIMG-01 failed"
    done
    finish
fi
src_sha0=$(sha256sum "$SRC" | cut -d' ' -f1)
rc=0
out=$(bash "$SCRIPT" "$SRC" "$T/a/minimal.img" 2>"$T/a/stderr") || rc=$?
CARD="$T/a/minimal.img"
last=$(tail -n1 <<<"$out")
n=$(stat -c %s "$CARD" 2>/dev/null || echo -1)
h=$(sha256sum "$CARD" 2>/dev/null | cut -d' ' -f1 || true)
ok=0
if [[ $rc -eq 0 && "$last" == "$T/a/minimal.img: $n bytes, sha256 $h" ]] \
   && ! compgen -G "$T/a/.minimal-sdcard.*" >/dev/null; then ok=1; fi
row MINIMG-01 "$ok" "builds from the distribution image; last line is dst, size, sha256; no work dir left"
[[ $ok == 1 ]] || { [[ -s "$T/a/stderr" ]] && tail -n 5 "$T/a/stderr"; }
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
card_list=$(mdir -/ -a -b -i "$CARD@@32256" ::/ 2>/dev/null | LC_ALL=C sort || true)
start=$(sfdisk --dump "$SRC" 2>/dev/null | sed -n 's/^.* : start= *\([0-9]*\),.*/\1/p' | head -n1)
src_all=$(mdir -/ -a -b -i "$SRC@@$((start * 512))" ::/ 2>/dev/null || true)
src_list=$( { grep -iE '^::/(TBBLUE\.FW|machines|nextzxos|sys|LICENSE\.md|README\.md)(/|$)' <<<"$src_all" || true
              echo "::/machines/next/config.ini"; } | LC_ALL=C sort)
ok=0
if [[ -n "$card_list" && "$card_list" == "$src_list" ]]; then ok=1; fi
row MINIMG-04 "$ok" "card holds exactly the boot set plus config.ini ($(wc -l <<<"$card_list") entries)"

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
    case "$f" in machines/next/zx81.rom|machines/next/config.ini) continue;; esac
    if [[ ! -f "$T/xs/$f" ]] || ! cmp -s "$T/xc/$f" "$T/xs/$f"; then ok=0; bad+=" $f"; fi
done < <(cd "$T/xc" && find . -type f -printf '%P\n' | LC_ALL=C sort)
tl() { (cd "$1" && find . -type f -printf '%P %T@\n' | { grep -v -e '^machines/next/zx81.rom ' -e '^machines/next/config.ini ' || true; } | LC_ALL=C sort) || true; }
# The card is a subset of the source: compare the card's files against the same files of the source.
tc=$(tl "$T/xc"); ts=$(tl "$T/xs" | LC_ALL=C sort)
missing_t=$(comm -23 <(cat <<<"$tc") <(cat <<<"$ts") | head -n 3 || true)
[[ -z "$missing_t" ]] || { ok=0; bad+=" mtime:$missing_t"; }
[[ -n "$tc" ]] || ok=0
# Attributes (R/H/S/A) over the boot set; the two patched files and the root line are excluded.
attr() { { mattrib -/ -i "$1" ::/ 2>/dev/null || true; } | { grep -iE '::/(TBBLUE\.FW|machines|nextzxos|sys|LICENSE\.md|README\.md)(/|$)' || true; } \
         | { grep -v -e '::/machines/next/zx81\.rom$' -e '::/machines/next/config\.ini$' || true; } | LC_ALL=C sort; }
attr_c=$(attr "$CARD@@32256"); attr_s=$(attr "$SRC@@$((start * 512))")
[[ -n "$attr_c" && "$attr_c" == "$attr_s" ]] || { ok=0; bad+=" attributes"; }
row MINIMG-05 "$ok" "every copied file is byte-identical with the same mtime, and every entry has the source's attributes${bad:+ (bad:$bad)}"

# ---- MINIMG-06: the two patches ----
FIXSH="$ROOT/tools/fix-sdcard-image.sh"
sed -n "/<<'CONFIGEOF'/,/^CONFIGEOF\$/p" "$FIXSH" | sed '1d;$d' > "$T/config.expected"
ok=1
cmp -s "$T/config.expected" "$T/xc/machines/next/config.ini" && [[ -s "$T/config.expected" ]] || ok=0
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
row MINIMG-06 "$ok" "config.ini is the script's text; zx81.rom arm: $arm"

# ---- MINIMG-07: reproducible, other directory, other TZ, later time ----
sleep 2
rc=0
TZ=JNX-9 bash "$SCRIPT" "$SRC" "$T/b/other.img" >/dev/null 2>&1 || rc=$?
sha_b=$(sha256sum "$T/b/other.img" 2>/dev/null | cut -d' ' -f1 || true)
ok=0; [[ $rc -eq 0 && -n "$sha_b" && "$sha_b" == "$sha_a" ]] && ok=1
row MINIMG-07 "$ok" "second build (other directory, TZ, 2 s later) has the same sha256"

# ---- MINIMG-08 is taken last (needs every run to have happened) ----

# ---- MINIMG-09: existing destination refused ----
mkdir -p "$T/d"
echo keep > "$T/d/exists.img"
rc_a=0; err_a=$(bash "$SCRIPT" "$SRC" "$T/d/exists.img" 2>&1 >/dev/null) || rc_a=$?
ln -s nowhere.img "$T/d/dangle.img"
rc_b=0; err_b=$(bash "$SCRIPT" "$SRC" "$T/d/dangle.img" 2>&1 >/dev/null) || rc_b=$?
ok=1
[[ $rc_a == 1 && "$err_a" == *"destination exists"* && "$(cat "$T/d/exists.img")" == keep ]] || ok=0
[[ $rc_b == 1 && "$err_b" == *"destination exists"* && ! -e "$T/d/nowhere.img" && "$(readlink "$T/d/dangle.img")" == nowhere.img ]] || ok=0
compgen -G "$T/d/.minimal-sdcard.*" >/dev/null && ok=0
row MINIMG-09 "$ok" "an existing file or dangling symlink destination is refused and untouched"

# ---- MINIMG-10: missing boot-set path refused ----
mkdir -p "$T/s" "$T/s/files"
truncate -s 48M "$T/s/synth.img"
printf 'label: dos\nunit: sectors\n\nstart=2048, type=c\n' | sfdisk -q "$T/s/synth.img" >/dev/null 2>&1
mformat -F -c 1 -i "$T/s/synth.img@@$((2048 * 512))" :: >/dev/null 2>&1
for f in TBBLUE.FW LICENSE.md README.md; do echo x > "$T/s/files/$f"; done
mcopy -i "$T/s/synth.img@@$((2048 * 512))" "$T/s/files/TBBLUE.FW" "$T/s/files/LICENSE.md" "$T/s/files/README.md" ::/ >/dev/null 2>&1
mmd -i "$T/s/synth.img@@$((2048 * 512))" ::/machines ::/nextzxos >/dev/null 2>&1
mkdir -p "$T/e"
rc=0; err=$(bash "$SCRIPT" "$T/s/synth.img" "$T/e/out.img" 2>&1 >/dev/null) || rc=$?
ok=0
if [[ $rc == 1 && "$err" == *"SYS is not in"* && ! -e "$T/e/out.img" ]] && ! compgen -G "$T/e/.minimal-sdcard.*" >/dev/null; then ok=1; fi
row MINIMG-10 "$ok" "a source without SYS (partition at sector 2048) is refused naming SYS"

# ---- MINIMG-11: too few clusters refused ----
mkdir -p "$T/tools" "$T/f"
cp "$SCRIPT" "$T/tools/make-minimal-sdcard-image.sh"; cp "$FIXSH" "$T/tools/fix-sdcard-image.sh"
sed -i 's/^CARD_MB=40$/CARD_MB=32/' "$T/tools/make-minimal-sdcard-image.sh"
changed=$(diff "$SCRIPT" "$T/tools/make-minimal-sdcard-image.sh" | grep -c '^>' || true)
rc=0; err=$(bash "$T/tools/make-minimal-sdcard-image.sh" "$SRC" "$T/f/out.img" 2>&1 >/dev/null) || rc=$?
ok=0
if [[ "$changed" == 1 && $rc == 1 && "$err" == *"free clusters"* && ! -e "$T/f/out.img" ]] \
   && ! compgen -G "$T/f/.minimal-sdcard.*" >/dev/null; then ok=1; fi
row MINIMG-11 "$ok" "a 32 MiB card (too few clusters for FAT32) is refused"

# ---- MINIMG-12: no partition table ----
mkdir -p "$T/g"
truncate -s 1M "$T/g/zero.img"
rc=0; err=$(bash "$SCRIPT" "$T/g/zero.img" "$T/g/out.img" 2>&1 >/dev/null) || rc=$?
ok=0; [[ $rc == 1 && "$err" == *"no partition table"* && ! -e "$T/g/out.img" ]] && ok=1
row MINIMG-12 "$ok" "a source with no partition table is refused"

# ---- MINIMG-13: usage ----
mkdir -p "$T/h"
rc=0; err=$(cd "$T/h" && bash "$SCRIPT" one.img 2>&1 >/dev/null) || rc=$?
ok=0; [[ $rc == 2 && "$err" == Usage:* && -z "$(ls -A "$T/h")" ]] && ok=1
row MINIMG-13 "$ok" "one argument gives usage and exit 2, nothing created"

# ---- MINIMG-14: a ':' in the source path ----
mkdir -p "$T/co:lon"
cp "$T/s/synth.img" "$T/co:lon/synth.img"
mmd -i "$T/co:lon/synth.img@@$((2048 * 512))" ::/sys ::/machines/next >/dev/null 2>&1 || true
mkdir -p "$T/i"
rc=0; err=$(bash "$SCRIPT" "$T/co:lon/synth.img" "$T/i/out.img" 2>&1 >/dev/null) || rc=$?
ok=0; [[ $rc == 0 && -s "$T/i/out.img" && "$err" != *"no partition table"* ]] && ok=1
row MINIMG-14 "$ok" "a source path containing a colon is read (partition table found, card built)"

# ---- MINIMG-15: a destination that appears during the build is not replaced ----
# A stub fix-sdcard-image.sh (beside a copy of the script) creates the destination
# while the build is in flight, then does the card copy the real one would.
mkdir -p "$T/race/tools" "$T/race/out"
cp "$SCRIPT" "$T/race/tools/make-minimal-sdcard-image.sh"
printf '#!/usr/bin/env bash\ncp "$1" "$2"\necho late > "%s"\n' "$T/race/out/out.img" > "$T/race/tools/fix-sdcard-image.sh"
rc=0; err=$(bash "$T/race/tools/make-minimal-sdcard-image.sh" "$SRC" "$T/race/out/out.img" 2>&1 >/dev/null) || rc=$?
ok=0
if [[ $rc == 1 && "$err" == *"destination appeared"* && "$(cat "$T/race/out/out.img")" == late ]] \
   && ! compgen -G "$T/race/out/.minimal-sdcard.*" >/dev/null; then ok=1; fi
row MINIMG-15 "$ok" "a destination created during the build is refused, not overwritten"

# ---- MINIMG-16: no hard links on the destination filesystem: the real cause is reported ----
# A PATH stub ln fails like link(2) on FAT (EPERM) and delegates the symlink the script makes.
mkdir -p "$T/nl/bin" "$T/nl/out"
real_ln=$(command -v ln)
printf '#!/usr/bin/env bash\nif [ "${1:-}" = -s ]; then exec "%s" "$@"; fi\necho "ln: failed to create hard link: Operation not permitted" >&2\nexit 1\n' "$real_ln" > "$T/nl/bin/ln"
chmod +x "$T/nl/bin/ln"
rc=0; err=$(PATH="$T/nl/bin:$PATH" bash "$SCRIPT" "$SRC" "$T/nl/out/out.img" 2>&1 >/dev/null) || rc=$?
ok=0
if [[ $rc == 1 && "$err" == *"Operation not permitted"* && "$err" == *"hard links"* && "$err" != *appeared* \
      && ! -e "$T/nl/out/out.img" ]] && ! compgen -G "$T/nl/out/.minimal-sdcard.*" >/dev/null; then ok=1; fi
row MINIMG-16 "$ok" "a destination filesystem without hard links is reported as such, not as a race"

# ---- MINIMG-08: the source was only read ----
src_sha1=$(sha256sum "$SRC" | cut -d' ' -f1)
ok=0; [[ "$src_sha0" == "$src_sha1" ]] && ok=1
row MINIMG-08 "$ok" "the distribution image is byte-identical after all runs"

finish
