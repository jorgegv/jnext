#!/usr/bin/env bash
# Build the minimal NextZXOS boot card from the distribution SD image.
#
#   tools/make-minimal-sdcard-image.sh <distribution.img> <minimal.img>
#
# The browser build fetches a small (40 MiB, 512-byte clusters) card that holds
# only the boot set below, instead of the 1 GB distribution image. jnext's own
# two patches (config.ini, doubled zx81.rom) are applied by
# tools/fix-sdcard-image.sh, unchanged.
#
# Needs mtools, sfdisk (util-linux) and coreutils. No root, no loop mount. The
# source is only read. The destination filesystem must support hard links (not
# FAT/exFAT): the card is placed with ln, which never replaces an entry. The output is
# reproducible for one mtools version (mformat writes its version into the boot sector): fixed volume serial,
# SOURCE_DATE_EPOCH and TZ pinned, and a DOS-to-DOS copy so the directory order
# comes from the source card and not from the builder's host filesystem.
#
# The OUTPUT is copyrighted NextZXOS content: never commit, attach or upload it.
set -euo pipefail

BOOT_SET=(TBBLUE.FW MACHINES NEXTZXOS SYS LICENSE.md README.md)
CARD_MB=40
SERIAL=4A4E5854
export SOURCE_DATE_EPOCH=315532800 TZ=UTC LC_ALL=C.UTF-8 MTOOLS_SKIP_CHECK=1

die() { echo "ERROR: $*" >&2; exit 1; }
[ $# -eq 2 ] || { echo "Usage: $0 <distribution.img> <minimal.img>" >&2; exit 2; }
SRC=$(realpath -e -- "$1") || die "no source image: $1"
[ ! -e "$2" ] && [ ! -L "$2" ] || die "destination exists, not overwritten: $2"
DST=$(realpath -m -s -- "$2")
FIX="$(dirname "$(realpath "$0")")/fix-sdcard-image.sh"

start=$(sfdisk --dump "$SRC" 2>/dev/null | sed -n 's/^.* : start= *\([0-9]*\),.*/\1/p' | head -n1) || true
[ -n "$start" ] || die "no partition table in $SRC"

W=$(mktemp -d "$(dirname "$DST")/.minimal-sdcard.XXXXXX")
trap 'rm -rf "$W"' EXIT
cd "$W"
# Relative file= paths (through a symlink) keep odd characters out of mtools' rc parser.
ln -s "$SRC" src.img
truncate -s "${CARD_MB}M" card.img
printf 'label: dos\nlabel-id: 0x00000000\nunit: sectors\n\nstart=63, type=c, bootable\n' \
    | sfdisk -q --no-reread --no-tell-kernel card.img
printf 'drive s: file="src.img" offset=%d\ndrive d: file="card.img" offset=32256\n' \
    $((start * 512)) > mtoolsrc
export MTOOLSRC=$W/mtoolsrc
mformat -F -c 1 -T $((CARD_MB * 2048 - 63)) -h 255 -s 63 -H 63 -N "$SERIAL" d:
# FatFs treats a volume as FAT32 only above 65525 clusters (the root takes one).
free=$(minfo d: | sed -n 's/.*free clusters=\([0-9]*\).*/\1/p')
[ "${free:-0}" -ge 65525 ] || die "only ${free:-0} free clusters; FAT32 needs more than 65525"

root=$(mdir -a -b s:/)
names=()
for p in "${BOOT_SET[@]}"; do
    n=$(grep -iFx -e "S:/$p" -e "S:/$p/" <<<"$root" | head -n1) || die "$p is not in $SRC"
    n=${n#?:/}; names+=("s:/${n%/}")   # the source's own spelling of the name
done
mcopy -s -m -p -Q "${names[@]}" d:/
unset MTOOLSRC
bash "$FIX" "$W/card.img" "$W/fixed.img" >&2
# ln fails on ANY existing entry (a dangling symlink too): a destination that
# appeared during the build is never replaced, unlike mv. It needs a filesystem
# with hard links (not FAT/exFAT), so the two failures get different messages.
if ! ln "$W/fixed.img" "$DST" 2>"$W/ln.err"; then
    { [ -e "$DST" ] || [ -L "$DST" ]; } && die "destination appeared during the build, not overwritten: $2"
    die "cannot create $2: $(cat "$W/ln.err") (the destination filesystem must support hard links)"
fi
echo "$DST: $(stat -c %s "$DST") bytes, sha256 $(sha256sum "$DST" | cut -d' ' -f1)"
