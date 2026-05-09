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
