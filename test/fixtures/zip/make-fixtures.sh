#!/usr/bin/env bash
# Regenerates the committed ZIP fixtures of program_archive_test and
# load_error_test (GH #392). Documentation of provenance: NOT run by any gate.
# The archives are written by FOREIGN writers (Info-ZIP 3.0, CPython zipfile),
# so the interchange reader is proved against shapes our own Writer cannot emit.
# Needs Info-ZIP `zip` 3.0 and python3. Contents are our own demo programs.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
tap=$repo/test/00regression/tap
nex=$repo/test/00regression/nex
export TZ=UTC
work=$(mktemp -d "$HOME/tmp/zipfix.XXXXXX")
trap 'rm -rf "$work"' EXIT
stamp() { find "$1" -exec touch -d 2026-01-01T00:00:00 {} +; }

# beeper.tap.zip: flag 0x0002, extra fields, directory entries, subdirs, a POK.
d=$work/beeper; mkdir -p "$d/SCRSHOT" "$d/POKES"
cp "$tap/beeper_demo.tap" "$d/BEEPER.TAP"
head -c 6912 /dev/zero > "$d/SCRSHOT/BEEPER.SCR"
printf 'NInfinite beeper\r\nZ   8 32768   0   0\r\nY\r\n' > "$d/POKES/BEEPER.POK"
stamp "$d"
(cd "$d" && rm -f "$here/beeper.tap.zip" && zip -q -9 -r "$here/beeper.tap.zip" BEEPER.TAP SCRSHOT POKES)

# twotapes.zip: several programs.
d=$work/two; mkdir -p "$d"
cp "$tap/beeper_demo.tap" "$d/SIDEA.TAP"; cp "$tap/rom_charset_test.tap" "$d/SIDEB.TAP"
stamp "$d"
(cd "$d" && rm -f "$here/twotapes.zip" && zip -q -9 "$here/twotapes.zip" SIDEA.TAP SIDEB.TAP)

# noprog.zip: no program inside.
d=$work/nop; mkdir -p "$d/SCRSHOT"
printf 'A readme.\r\n' > "$d/README.TXT"; head -c 6912 /dev/zero > "$d/SCRSHOT/X.SCR"
stamp "$d"
(cd "$d" && rm -f "$here/noprog.zip" && zip -q -9 -r "$here/noprog.zip" README.TXT SCRSHOT)

# encrypted.zip: ZipCrypto (flag 0x000b).
d=$work/enc; mkdir -p "$d"; cp "$tap/beeper_demo.tap" "$d/BEEPER.TAP"; stamp "$d"
(cd "$d" && rm -f "$here/encrypted.zip" && zip -q -e -P jnext -9 "$here/encrypted.zip" BEEPER.TAP)

# lores.nex.zip: CPython zipfile to a NON-SEEKABLE stream: flag 0x0008 (data
# descriptor), local sizes 0.
python3 -I - "$nex/lores_demo.nex" "$here/lores.nex.zip" <<'PY'
import sys, zipfile, subprocess
src, dst = sys.argv[1], sys.argv[2]
class Pipe:                      # no seek/tell: zipfile must use data descriptors
    def __init__(self, f): self.f = f
    def write(self, b): return self.f.write(b)
    def flush(self): self.f.flush()
p = subprocess.Popen(["cat"], stdin=subprocess.PIPE, stdout=open(dst, "wb"))
with zipfile.ZipFile(Pipe(p.stdin), "w", zipfile.ZIP_DEFLATED) as z:
    zi = zipfile.ZipInfo("LORES.NEX", (2026, 1, 1, 0, 0, 0))
    zi.compress_type = zipfile.ZIP_DEFLATED
    with z.open(zi, "w") as f:
        f.write(open(src, "rb").read())
p.stdin.close(); p.wait()
PY
echo "fixtures regenerated in $here"
