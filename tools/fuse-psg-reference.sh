#!/usr/bin/env bash
# Maintainer tool: record test/audio/fixtures/psg_fixture.fuse.psg, the oracle the .psg export
# is checked against (audio_debug_test PSF-01..03, regression row psg-record-func).
#
#   tools/fuse-psg-reference.sh [OUT]       OUT defaults to test/audio/fixtures/psg_fixture.fuse.psg
#
# It runs the committed program (psg_fixture.bin) in the packaged Fuse (1.6.0 when this was written)
# on an Xvfb display, drives File > AY Logging > Record... and Stop with xdotool, and checks the
# result with tools/psg-normalise.py. NEVER run by CI: CI compares against the committed file, and
# installs none of Fuse, xdotool, ImageMagick or Xvfb. Needs: fuse, xdotool, xvfb-run, python3.
#
# The program is loaded as a 128K .sna the tool writes itself (no z88dk, no ROM bytes in it: the
# snapshot holds only the program and zeroed RAM). The snapshot, Fuse's settings and the run's
# scratch live in a directory under ~/tmp that is removed at the end. Nothing from Fuse's ROMs
# reaches the output: the recording starts while the program waits in its gate, and the program
# writes every AY register itself.
#
# Every external command runs under LANG=C with a bound that escalates to SIGKILL; Fuse runs under
# a 4 GiB address-space cap. Exit status: 0 recorded and valid, 1 a step failed.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/.." && pwd)"
fixture_dir="$repo/test/audio/fixtures"
out="${1:-$fixture_dir/psg_fixture.fuse.psg}"
export LANG=C

if [[ "${2:-}" != "--inside" ]]; then
    for c in fuse xdotool xvfb-run python3; do
        command -v "$c" >/dev/null || { echo "fuse-psg-reference: $c is not installed" >&2; exit 1; }
    done
    work="$(mktemp -d "$HOME/tmp/fuse-psg.XXXXXX")"
    cleanup() { rm -rf "$work"; }
    trap cleanup EXIT
    # A 128K snapshot: header, banks 5, 2 (the program at 0x8000) and 0 (paged in), then PC, port 0x7FFD,
    # TR-DOS flag and the five other banks. SP 0xBFF0, IM 1; the program sets up its own interrupts.
    python3 -I - "$fixture_dir/psg_fixture.bin" "$work/fx.sna" <<'PY'
import struct, sys
prog = open(sys.argv[1], 'rb').read()
hdr = bytearray(27)
struct.pack_into('<H', hdr, 23, 0xBFF0)
hdr[25] = 1
bank2 = bytearray(16384)
bank2[:len(prog)] = prog
data = bytes(hdr) + bytes(16384) + bytes(bank2) + bytes(16384) + struct.pack('<HBB', 0x8000, 0, 0) + bytes(16384 * 5)
open(sys.argv[2], 'wb').write(data)
PY
    mkdir -p "$work/home"
    HOME="$work/home" WORK="$work" timeout --foreground --kill-after=5s 240s \
        xvfb-run -d -s "-screen 0 1024x768x24 -noreset" bash "$here/fuse-psg-reference.sh" "$out" --inside
    exit
fi

# ---- inside the X session ------------------------------------------------------------------
work="$WORK"
export GDK_BACKEND=x11
rec="$work/rec.psg"
rm -f "$rec"
( ulimit -v 4194304; exec timeout --foreground --kill-after=5s 150s \
      fuse --machine 128 --no-sound --snapshot "$work/fx.sna" ) >"$work/fuse.log" 2>&1 &
fuse_pid=$!

# The Fuse window is the one named exactly "Fuse".
win=""
for _ in $(seq 1 50); do
    win="$(timeout --kill-after=2s 5s xdotool search --name '^Fuse$' 2>/dev/null | head -1 || true)"
    [[ -n "$win" ]] && break
    sleep 0.2
done
[[ -n "$win" ]] || { echo "fuse-psg-reference: the Fuse window did not appear" >&2; cat "$work/fuse.log" >&2; exit 1; }
sleep 2

# File > AY Logging (the 4th row of the File menu) > Record..., then the chooser: type the path.
xd() { timeout --kill-after=2s 10s xdotool "$@"; }
xd mousemove 14 8 click 1; sleep 1
xd mousemove 65 122 click 1; sleep 1
xd key Right; sleep 0.5
xd key Return
chooser=""
for _ in $(seq 1 30); do
    chooser="$(xd search --name '^Fuse - Start AY Log$' 2>/dev/null | head -1 || true)"
    [[ -n "$chooser" ]] && break
    sleep 0.2
done
[[ -n "$chooser" ]] || { echo "fuse-psg-reference: the Start AY Log chooser did not appear" >&2; exit 1; }
sleep 1
xd type --delay 30 "$rec"; sleep 0.5
xd key Return; sleep 2
[[ -e "$rec" ]] || { echo "fuse-psg-reference: Fuse did not create the log" >&2; exit 1; }

# The program waits for a key (or 1500 frames, which would make the sequence run past the wait below):
# a click in the Fuse window gives it the focus back from the closed chooser, then a few space presses.
xd mousemove 160 150 click 1; sleep 0.5
for _ in 1 2 3 4; do xd key space; sleep 0.5; done
# 1431 frames from the key at 50 Hz is 28.6 s; leave a margin.
sleep 36

# File > AY Logging > Stop (second item of the submenu), which writes the trailing separators.
xd mousemove 14 8 click 1; sleep 1
xd mousemove 65 122 click 1; sleep 1
xd key Right; sleep 0.3
xd key Down; sleep 0.3
xd key Return; sleep 2
kill "$fuse_pid" 2>/dev/null || true
wait "$fuse_pid" 2>/dev/null || true

# One sentinel, parses; and the whole sequence is in it: the program's 1101-frame gap (FE FF FE 14 FF) and a
# trailing run after its last write (a stop that came before the sequence ended has neither).
python3 -I "$here/psg-normalise.py" "$rec" "$work/body.psg" || { echo "fuse-psg-reference: the recording does not normalise" >&2; exit 1; }
python3 -I - "$rec" <<'PY' || { echo "fuse-psg-reference: the recording is cut short (the gate opened late?); run it again" >&2; exit 1; }
import sys
d = open(sys.argv[1], 'rb').read()
j, tail = 16, 0
while j < len(d):
    if d[j] == 0xFF:
        tail += 1; j += 1
    elif d[j] == 0xFE:
        tail += 4 * d[j + 1]; j += 2
    else:
        tail = 0; j += 2
sys.exit(0 if b'\xfe\xff\xfe\x14\xff' in d and tail >= 20 else 1)
PY
cp "$rec" "$out"
echo "wrote $out ($(stat -c %s "$out") bytes, normalised body $(stat -c %s "$work/body.psg") bytes)"
sha256sum "$out"
rpm -q fuse-emulator 2>/dev/null || fuse --version 2>/dev/null | head -1 || true
