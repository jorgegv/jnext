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
An underrun is a run of *exactly zero* stereo samples, long enough to be
audible, that is entered abruptly from a non-trivial signal level and left back
to one. An SDL-injected hole cuts in from whatever the waveform was doing
(…, 972, 971, | 0, 0, 0 … | 970, 969, …) and the emulator's own stream resumes
seamlessly on the far side of the gap, which is precisely what proves the zeros
are foreign.

So: run length >= MIN_RUN, and |sample immediately before the zero-run| >= EDGE
and |sample immediately after| >= EDGE  =>  an SDL-injected hole, i.e. a click.

WHAT MIN_RUN IS LOAD-BEARING FOR (changed by GH #116). This docstring used to
claim the mixer never emits exactly zero — that an idle machine sits at a flat
non-zero DC (the DAC's 0x80/0x80 rest plus the I2S 0x200 midpoint) and so any
zero at all is foreign. That was true only because of the GH #116 defect: the
mixer's AC-coupling reference omitted the I2S term's 0x200 midpoint, so a SILENT
machine came out at +2048 on every sample instead of 0 — which is exactly what
made SDL's zero-padding audible as a constant motor-like buzz. With the
reference fixed, silence IS zero, so "any zero is foreign" no longer holds and
run LENGTH is the whole discriminator.

That is sound, and its failure mode is loud rather than silent. The stimulus
(bin/beeper_tone.bin) is a ~2.2 kHz square wave whose low phase is 9-10 stereo
pairs, well under MIN_RUN, so its own zero plateaus are not flagged; an SDL hole
is a fragment of a 1024-frame device buffer, one to two decades longer. If the
stimulus is ever slowed past MIN_RUN, EVERY low phase gets flagged and the test
fails by thousands — it cannot quietly stop testing. And a real hole landing on
a low phase merges with it and only gets longer, so detection cannot be masked.

The one case this cannot see is a hole in a machine that is genuinely silent —
but that hole is now zeros spliced into zeros, i.e. inaudible, which is the
whole point of the GH #116 fix.

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
