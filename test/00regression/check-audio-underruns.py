#!/usr/bin/env python3
"""Detect audio-device underruns in a raw capture of jnext's audio output.

Used by the `audio-underrun-func` regression test (GitHub issue #7 / Task 23).

Background
----------
jnext synthesises audio on the *emulated* clock (44100 samples per emulated
second). If the frontend paces emulation on the wall clock instead of on the
sound card's clock, it feeds the device fewer samples per real second than the
device consumes, the device queue drains to empty, and SDL pads the buffer with
ZEROS. Those zeros are spliced into a live waveform: the signal steps abruptly
to 0 and back a few milliseconds later. That is an audible click, and it
repeats for as long as the emulator runs — the "constant noise" of issue #7 and
the clicking over beeper music of Task 23.

Detection
---------
An underrun is a run of *exactly zero* stereo samples that is entered abruptly
from a non-trivial signal level. A genuine musical silence cannot look like
this: the mixer's DC-blocking output stage decays smoothly (…, 3, 2, 1, 0), so
the sample immediately before a real silence is near zero. An SDL-injected hole
cuts in from whatever the waveform was doing (…, 972, 971, | 0, 0, 0 … | 970,
969, …) — the emulator's own stream continues seamlessly across the gap, which
is precisely what proves the zeros are foreign.

So: |sample immediately before the zero-run| >= EDGE and |sample immediately
after| >= EDGE  =>  an SDL-injected hole, i.e. a click.

Usage: check-audio-underruns.py <capture.raw> [--skip-secs N]
Input is raw S16LE stereo at 44100 Hz (what SDL's `disk` audio driver writes).
Exit status 0 if clean, 1 if any underrun was found.
"""

import array
import sys

RATE = 44100
MIN_RUN = 20     # stereo pairs; shorter zero-runs are not audible as a hole
EDGE = 200       # signal level that a zero-run must cut in from / back to


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: check-audio-underruns.py <capture.raw> [--skip-secs N]")
        return 2
    path = sys.argv[1]
    skip_secs = 0.5
    if "--skip-secs" in sys.argv:
        skip_secs = float(sys.argv[sys.argv.index("--skip-secs") + 1])

    pcm = array.array("h")
    with open(path, "rb") as fh:
        pcm.frombytes(fh.read())
    if sys.byteorder == "big":
        pcm.byteswap()

    frames = len(pcm) // 2
    left = pcm[0::2]
    right = pcm[1::2]
    skip = int(skip_secs * RATE)   # startup: the device legitimately plays zeros
                                   # before the first push, and the machine is
                                   # silent then anyway

    underruns = []
    i = skip
    while i < frames:
        if left[i] == 0 and right[i] == 0:
            start = i
            while i < frames and left[i] == 0 and right[i] == 0:
                i += 1
            run = i - start
            if run >= MIN_RUN and start > 0 and i < frames:
                before = abs(left[start - 1])
                after = abs(left[i])
                if before >= EDGE and after >= EDGE:
                    underruns.append((start / RATE, run * 1000.0 / RATE, before))
        else:
            i += 1

    if underruns:
        print("FAIL: %d audio underrun(s) — SDL injected silence into a live "
              "waveform (audible clicks)" % len(underruns))
        for t, ms, lvl in underruns[:10]:
            print("    t=%7.3fs  %5.1f ms of zeros, cut in from level %d" % (t, ms, lvl))
        if len(underruns) > 10:
            print("    ... and %d more" % (len(underruns) - 10))
        return 1

    print("OK: no audio underruns in %.1f s of capture" % (frames / RATE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
