#!/usr/bin/env bash
# Build the minimal NextZXOS boot card from the distribution SD image. Linux only.
#
#   tools/make-minimal-sdcard-image.sh <distribution.img> <minimal.img>
#
# The browser build fetches a small (40 MiB, 512-byte clusters) card that holds
# only the boot set below, instead of the 1 GB distribution image. The card is
# formatted as a FAT32 volume FatFs accepts from the start, and gets jnext's two
# patches here: the default /MACHINES/NEXT/config.ini and, for the 24.11 16 KB
# zx81.rom, that file twice. Both are copies of tools/fix-sdcard-image.sh's (and
# of src/core/sdcard_provisioner.cpp's); test/minimal-sdcard-image-test.sh fails
# if the copies differ. This script calls no other script.
#
# Linux only. Needs mtools, sfdisk (util-linux), coreutils, grep and sed: all are
# checked before anything is done. No root, no loop mount. The source is only
# read. The destination filesystem must support hard links (not FAT/exFAT): the
# card is placed with ln, which never replaces an entry. The output is
# reproducible for one mtools version (mformat writes its version into the boot
# sector): fixed volume serial, SOURCE_DATE_EPOCH and TZ pinned, and a
# DOS-to-DOS copy so the directory order comes from the source card and not from
# the builder's host filesystem.
#
# The OUTPUT is copyrighted NextZXOS content: never commit, attach or upload it.
set -euo pipefail

BOOT_SET=(TBBLUE.FW MACHINES NEXTZXOS SYS LICENSE.md README.md)
CARD_MB=40
SERIAL=4A4E5854
# The 24.11 distribution's one-page zx81.rom (full SHA-256): doubled on the card.
ZX81_16K_SHA256=c294e4b60a0eba85c02a3d8b37f77de6c4516d873682dcf333813bd392d82aac
# Every external command this script runs. Checked first; nothing is done if one is missing.
TOOLS=(mformat minfo mdir mcopy mmd sfdisk realpath dirname mktemp ln rm truncate cat stat sha256sum cut head grep sed)
export SOURCE_DATE_EPOCH=315532800 TZ=UTC LC_ALL=C.UTF-8 MTOOLS_SKIP_CHECK=1

die() { echo "ERROR: $*" >&2; exit 1; }

missing=() dnf="" apt=""
for t in "${TOOLS[@]}"; do
    command -v "$t" >/dev/null 2>&1 && continue
    missing+=("$t")
    case $t in
        mformat|minfo|mdir|mcopy|mmd) d=mtools apt_p=mtools ;;
        sfdisk) d=util-linux apt_p=fdisk ;;
        grep|sed) d=$t apt_p=$t ;;
        *) d=coreutils apt_p=coreutils ;;
    esac
    case " $dnf " in *" $d "*) ;; *) dnf+=" $d" ;; esac
    case " $apt " in *" $apt_p "*) ;; *) apt+=" $apt_p" ;; esac
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "ERROR: missing tools: ${missing[*]}" >&2
    echo "  Fedora:        sudo dnf install$dnf" >&2
    echo "  Debian/Ubuntu: sudo apt install$apt" >&2
    exit 1
fi

[ $# -eq 2 ] || { echo "Usage: $0 <distribution.img> <minimal.img>" >&2; exit 2; }
SRC=$(realpath -e -- "$1") || die "no source image: $1"
[ ! -e "$2" ] && [ ! -L "$2" ] || die "destination exists, not overwritten: $2"
DST=$(realpath -m -s -- "$2")

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

# jnext's two patches, written the way tools/fix-sdcard-image.sh writes them (same
# commands, order and names), so the card is the one that script made from it.
mmd -Ds d:/machines      </dev/null 2>/dev/null || true
mmd -Ds d:/machines/next </dev/null 2>/dev/null || true
cat > config.ini <<'CONFIGEOF'
scandoubler=1
50_60hz=0
timex=1
psgmode=0
stereomode=1
intsnd=1
turbosound=1
dac=1
divmmc=0
divports=1
mf=0
joystick1=2
joystick2=0
ps2=0
scanlines=0
turbokey=1
timing=0
default=0
dma=0
keyb_issue=0
ay48=0
uart_i2c=1
kmouse=1
ulaplus=1
hdmisound=1
beepmode=0
buttonswap=0
mousedpi=1
CONFIGEOF
mcopy -o config.ini d:/machines/next/config.ini </dev/null
if mcopy -n d:/machines/next/zx81.rom zx81.rom </dev/null 2>/dev/null \
   && [ "$(stat -c %s zx81.rom)" -eq 16384 ] \
   && [ "$(sha256sum zx81.rom | cut -d' ' -f1)" = "$ZX81_16K_SHA256" ]; then
    cat zx81.rom zx81.rom > zx81-32k.rom
    mcopy -o zx81-32k.rom d:/machines/next/zx81.rom </dev/null
fi
unset MTOOLSRC
# ln fails on ANY existing entry (a dangling symlink too): a destination that
# appeared during the build is never replaced, unlike mv. It needs a filesystem
# with hard links (not FAT/exFAT), so the two failures get different messages.
if ! ln "$W/card.img" "$DST" 2>"$W/ln.err"; then
    { [ -e "$DST" ] || [ -L "$DST" ]; } && die "destination appeared during the build, not overwritten: $2"
    die "cannot create $2: $(cat "$W/ln.err") (the destination filesystem must support hard links)"
fi
echo "$DST: $(stat -c %s "$DST") bytes, sha256 $(sha256sum "$DST" | cut -d' ' -f1)"
