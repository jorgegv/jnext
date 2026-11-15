#!/usr/bin/env bash
# A SYNTHETIC stand-in for the official NextZXOS distribution zip, for the browser rows that
# test the card flow (card.mjs). Licence-clean: invented file names with random bytes, built here
# with the FOREIGN writers mtools (a FAT32 volume with 32 KiB clusters, the partition at sector 63,
# like the real image) and python zipfile (the image entry first, then two dummy ROM entries).
# It is NOT NextZXOS and boots nothing; the rows that use it start a 48K machine.
#
#   make-synthetic-distro.sh <dir>     writes <dir>/synthetic-distro.zip
set -euo pipefail
export MTOOLS_SKIP_CHECK=1
D=${1:?usage: make-synthetic-distro.sh <dir>}
mkdir -p "$D/files/machines/next" "$D/files/nextzxos" "$D/files/sys"
truncate -s 24M "$D/synthetic.img"
printf 'label: dos\nunit: sectors\n\nstart=63, type=c\n' | sfdisk -q "$D/synthetic.img" >/dev/null
I="$D/synthetic.img@@32256"
mformat -F -c 64 -h 255 -s 63 -H 63 -i "$I" :: >/dev/null
for f in TBBLUE.FW LICENSE.md README.md; do head -c 4000 /dev/urandom >"$D/files/$f"; done
head -c 9000 /dev/urandom >"$D/files/machines/next/48.rom"
head -c 700 /dev/urandom >"$D/files/nextzxos/a.sys"
head -c 600 /dev/urandom >"$D/files/sys/b.bas"
mmd -i "$I" ::/machines ::/machines/next ::/nextzxos ::/sys >/dev/null
mcopy -i "$I" "$D/files/TBBLUE.FW" "$D/files/LICENSE.md" "$D/files/README.md" ::/ >/dev/null
mcopy -i "$I" "$D/files/machines/next/48.rom" ::/machines/next/ >/dev/null
mcopy -i "$I" "$D/files/nextzxos/a.sys" ::/nextzxos/ >/dev/null
mcopy -i "$I" "$D/files/sys/b.bas" ::/sys/ >/dev/null
python3 -I - "$D/synthetic.img" "$D/synthetic-distro.zip" <<'PYEOF'
import sys, zipfile
img, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(img, "cspect-next-1gb.img")
    z.writestr("enNextZX.rom", b"synthetic one")
    z.writestr("enNxtmmc.rom", b"synthetic two")
PYEOF
rm -rf "$D/files" "$D/synthetic.img"
