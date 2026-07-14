# DeciLoad TZX Direct Recording — Investigation Notes

## Problem

TZX files using Direct Recording blocks (ID 0x15) with the DeciLoad 12k8 turbo loader format fail to load in real-time playback mode. Standard and turbo blocks (0x10, 0x11) work correctly.

Test file: `test/tzx/Xevious_ZX0_DeciLoad12k8.tzx`

FUSE emulator loads this file successfully. JNext does not.

## File Structure

```
TZX version 1.10, 59535 bytes, 5 blocks:

Block 1 @ 0x000A: ID 0x11 Turbo Data
  pilot=2168 sync=667/735 zero=855 one=1710
  pilot_pulses=3200 used_bits=8 pause=10ms
  len=19, flag=0x00
  Header: Program "XevioZX0DL" len=401 autostart=10

Block 2 @ 0x0030: ID 0x11 Turbo Data
  pilot=2168 sync=667/735 zero=855 one=1710
  pilot_pulses=2400 used_bits=8 pause=10ms
  len=403, flag=0xFF
  (BASIC loader + DeciLoad machine code)

Block 3 @ 0x01D6: ID 0x15 Direct Recording
  tstates=77, pause=0ms, used_bits=3, len=13066

Block 4 @ 0x34E9: ID 0x20 Pause — 200ms

Block 5 @ 0x34EC: ID 0x15 Direct Recording
  tstates=77, pause=0ms, used_bits=7, len=45978
```

## What Works

- Blocks 1-2 (turbo data) load correctly in real-time mode. The ROM's LD-BYTES routine detects the pilot tone, syncs, and loads the BASIC program.
- The BASIC program auto-starts at line 10 and sets the border to cyan.
- The custom DeciLoad machine code (loaded from block 2) begins execution.

## What Fails

- The DeciLoad routine reads port 0xFE in a tight loop to detect edges from the Direct Recording blocks (3 and 5).
- It fails to detect valid data and the loading stalls at the cyan border screen.

## DeciLoad Format Details

DeciLoad 12k8 is an extremely fast turbo loader:
- **77 T-states per sample** (each bit in the 0x15 block represents 77 T-states of signal)
- At 3.5 MHz: 77 T-states = ~22 microseconds per sample
- Data rate: ~12.8 kbit/s (vs ~1.5 kbit/s for standard ROM loading)
- The loader uses its own edge-detection and bit-decoding protocol, completely bypassing the ROM routines.

## Investigation So Far

### Clock Source Fix (Resolved)

Initial real-time TZX playback was completely broken for ALL TZX files (not just DeciLoad). The port 0xFE read handler was using `clock_.get() / cpu_divisor()` which only updates **between** instructions. In the ROM's tight `IN A,(0xFE)` loop, this meant ZOT's edge state machine never advanced — every port read saw the same stale clock value.

**Fix:** Use `*fuse_z80_tstates_ptr()` — the FUSE Z80's live T-state counter that advances **during** instruction execution, including mid-I/O. This fixed standard and turbo block real-time playback.

### ZOT Direct Recording Analysis

ZOT's `TZX_PHASE_DIRECT` handler (tzx.c line 644-676):
- Sets `p->level` directly from each sample bit: `p->level = (data[pos] >> bit_pos) & 1`
- Returns `p->sample_tstates` (77) as the pulse duration
- Advances to next bit position

The `tzx_update()` hot loop (line 128-139):
- **Always toggles** `p->level ^= 1` before calling `tzx_next_pulse()`
- For DIRECT phase, the toggle is immediately overwritten by the direct level set
- The returned level should be correct since the direct set happens last

### Verified Working

- Pilot tone timing from ZOT is accurate: average 2168 T-states between edges (verified via logging)
- EAR bit toggles correctly during pilot/sync/data phases
- Port 0xFE bit 6 correctly reflects the EAR value
- Beeper receives tape EAR for audio output

### Theories for DeciLoad Failure

1. **ZOT direct recording edge timing** — The `tzx_update` toggle-then-overwrite pattern might cause a brief glitch. Between two consecutive calls to `tzx_update`, if the level was toggled (line 129) and then set from a bit, the returned value is from the bit. But what if the DeciLoad routine reads port 0xFE between two `tzx_update` calls where the loop didn't execute (cpu_clocks < edge_clock)? It would see the correctly-set level from the previous iteration. This should be fine.

2. **Sample timing granularity** — At 77 T-states per sample, the DeciLoad routine's `IN A,(0xFE)` loop (~11-14 T-states per iteration) should get ~5-7 reads per sample. This should be sufficient for edge detection.

3. **Edge polarity/protocol mismatch** — DeciLoad may expect specific level transitions that differ from how ZOT presents direct recording data. The 0x15 block encodes raw 1-bit samples, not edge-based data. DeciLoad likely decodes this as a specific protocol (e.g., pulse-width encoding where the time between edges encodes bits).

4. **FUSE vs ZOT implementation difference** — FUSE's libspectrum may handle 0x15 blocks differently. Comparing the two implementations could reveal the discrepancy.

## Next Steps

1. **Compare with FUSE** — Examine FUSE's tape playback code for Direct Recording (0x15) handling. FUSE source: `libspectrum/tape.c` or `fuse/tape.c`. Look for differences in how the EAR level is generated.

2. **Log edge timing** — Add detailed logging of EAR level changes during the Direct Recording phase to verify the signal waveform matches what DeciLoad expects.

3. **Disassemble DeciLoad** — Examine the machine code from block 2 (403 bytes at 0x0030) to understand the exact edge-detection protocol. Key questions: what timing thresholds does it use? Does it expect specific initial level? Does it use a pilot tone detection phase?

4. **Test with other Direct Recording TZX files** — Try other games that use 0x15 blocks (not necessarily DeciLoad) to narrow down whether this is a DeciLoad-specific issue or a general 0x15 problem.

5. **Consider replacing ZOT's 0x15 handler** — If ZOT's implementation is the bottleneck, we could override the DIRECT phase handling in our C++ wrapper while keeping ZOT for all other block types.

## RESOLVED — Task 57 (2026-07-14)

Both G36 (TZX 0x15) and G37 (WAV) are fixed. Xevious loads end-to-end to
its game menu in `--tape-realtime` mode; Dizzy loads from the WAV to its
title screen. None of the §Theories above was the root cause — there were
**three** independent bugs, all found by measurement:

### Root cause 1 — frame-relative tape clock (killed ALL real-time TZX/WAV)

`Emulator::begin_new_frame()` resets the FUSE `tstates` counter to 0 every
frame (introduced by f3665f25, 2026-04-13, for the contention `hc/vc`
derivation — ONE WEEK after the ZOT clock fix described above was landed
and verified). ZOT models the tape as an absolute timeline
(`edge_clock += pulse`); with a frame-relative clock, the first edge that
landed past a frame boundary became unreachable (`cpu_clocks >=
edge_clock` never true again) and the tape froze forever. The WAV loader
(`elapsed = now - start`) froze identically. This is why the "Verified
Working" observations above (pilot timing, EAR toggling) could not be
reproduced on 2026-07-14: real-time TZX had been silently broken for
three months — nothing in the test suites covered `--tape-realtime`.

**Fix**: `Emulator::monotonic_tstates()` = accumulated completed-frame
T-states + the live FUSE counter (still advances mid-instruction, the
property the original clock fix needed). Used at every tape clock site.

### Root cause 2 — ZOT pause swallowed every block's terminating edge

`TZX_PHASE_PAUSE` forced `level = 0` immediately. When a block's final
pulse ended at level 0, the terminating edge (the caller-side toggle to 1)
was overwritten — the ROM times the LAST bit of every block against that
edge, so the checksum byte of every block failed. Measured directly:
LD-BYTES entered with A=0x00 (header) returned carry-clear at the exact
end-of-data T-state of block 1, for every block, forever.

**Fix** (TZX spec / libspectrum behaviour): the pause holds the previous
block's final level for ~1 ms (3500 T), then drops low. Additionally,
DIRECT (0x15) samples are SET rather than toggled, so `tzx_update()` no
longer toggles over them — a 0x15 block's final sample level survives
into the pause un-inverted.

With causes 1+2 fixed, ZOT's per-sample 0x15 DIRECT handler proved
faithful as-is: DeciLoad decoded both Direct Recording blocks with **no
change to the 0x15 code path**. Theories 1-4 above are all dismissed.

### Root cause 3 (WAV only) — edge quantisation to the sample grid

Stepwise per-sample thresholding quantises every edge to the 79.4 T
sample grid (44.1 kHz). DeciLoad's short/long pulse classes are ~60 T
apart (~285 T vs ~535 T); measured on the Dizzy WAV, ~25% of the long
pulses quantised down to 6 samples (476 T) and were misclassified — the
loader's failure path is `RST 0`, which matches the observed reset to the
© banner. **Fix**: linear sub-sample interpolation of the threshold
crossing (8.8 fixed point) in `WavLoader::get_ear_bit()` — reconstructs
the analog crossing instant the hardware EAR Schmitt trigger sees.
Interpolated pulse widths cluster cleanly at ~490–590 T (measured).

### DeciLoad loader anatomy (from the block-2 disassembly)

The 403-byte block 2 holds a BASIC line-0 REM with a relocator: USR entry
at PROG+7 copies 0x120 bytes to 0xFED4-0xFFF3 (uncontended top RAM) and
jumps there. The loader reads port **0xFFFE** (high byte 0xFF = no
keyboard row selected, so idle bits are always 1) and senses EAR via the
**parity flag** of `IN (C)` / `IN F,(C)`: 0xFF (EAR=1) has even parity,
0xBF (EAR=0) odd — `RET PO` is the edge test. Pulse widths are measured
by 32 T polling loops with counted timeouts and decoded through an
IY-indexed table at 0xFFBD+ with self-modifying patches. Block 3 decodes
2549 bytes to 0x5CD0 (a ZX0 second-stage), block 5 the main payload.

### FUSE cross-check (honest note)

The claim "FUSE handles the same file correctly" could NOT be reproduced
headless on this box (FUSE 1.6.0 GTK, Xvfb, isolated config): with tape
traps on, FUSE flash-loads blocks 1-2 and stops the tape — the DeciLoad
loader polls a stopped tape forever; with `--no-traps` full realtime,
FUSE reaches the loader entry (0xFED4, debugger-verified) but decodes
zero bytes of block 3 in 110 s (`break write 0x6000` never fires past the
boot RAM-clear). jnext post-fix is verified end-to-end instead: the
loaded Xevious menu is pixel-deterministic across runs, and both ZX0
streams decompress without fault — a single corrupt bit would crash the
loader, not render the genuine menu.

### Regression coverage

- `xevious-deciload` screenshot row (test/00regression/regression_tests.conf)
  — full realtime TZX load to the game menu at frame 1300.
- `mmu_test` BOOT-DECI-01/02 (ZOT 0x15 level-vs-time; pause
  terminating-edge + DIRECT final-sample hold).
- `mmu_test` BOOT-DECI-03/04 (WAV threshold playback; sub-sample
  interpolation discriminator).
