# EOD-30i — Real-hardware test procedure

## Question

At CPU address $2331 with DivMMC AUTOMAP active and CONMEM=0 (slot 0 running
DivMMC ROM), does real Next silicon route:
- **Slot 1 → DivMMC SRAM bank N** (jnext-faithful, VHDL-correct)
- **Slot 1 → main RAM** (CSpect-faithful, deviates from VHDL)

This is THE pinpoint divergence behind the G46(b)-v2 boot stall. jnext
implements VHDL spec literally; CSpect deviates. The firmware boots on
CSpect, not on jnext. Real-hardware test resolves which is correct.

## Test procedure (simplest form — pass/fail)

1. Insert the SD image (`roms/nextzxos-1gb-fat32fix.img`) into a real
   ZX Spectrum Next.
2. Power on.
3. Observe the boot sequence:
   - **PASS = CSpect-faithful**: Boots fully through TBBlue.fw welcome
     screen → 3-ROMs detection → KEYB load → NextZXOS prompt.
   - **FAIL = jnext-faithful**: Stalls at a specific point — most
     likely an INIR or LDIR loop that depends on the cache. Symptom may
     be a black screen with a frozen border colour, or repeating ROM
     load attempts.

## Test procedure (more decisive — capture state)

If the boot result is ambiguous (e.g. boots eventually after a delay),
the deeper test is to inspect mem[$2330..$233F] at $14C0 hit 3 via the
DivMMC esxDOS command line or a custom NEX-loaded probe. The expected
cache marker should be:
- **`00 01 ...`** if jnext-faithful (cache in DivMMC SRAM bank 0)
- **`01 00 ...`** if CSpect-faithful (cache in main RAM page $0B)

(Both emulators converge cache contents at hits 3-5 to the same byte
sequence except for the marker phase — that's the off-by-one rotation.)

## Result reporting

Report back to the AI assistant:
- The visual boot outcome (full boot / stall / crash).
- Approximate timing (how long until boot or stall).
- Any error message or visible state on screen.

## Interpretation matrix

| Real HW behavior | Implication |
|---|---|
| Boots fully (like CSpect) | CSpect is faithful. VHDL has an additional gate that disables slot-1 overlay in this scenario, which the oracle missed. jnext needs to match. |
| Stalls (like jnext) | jnext is VHDL-faithful AND silicon-faithful. The firmware accidentally depends on CSpect's deviation. Either the firmware is buggy on real HW too (would explain it never works on hardware without CSpect-style emulator), or there is a specific Next firmware version that fixes it. |

## Background

See `EOD-30i-pulse-expiry-drops-cpu-int.md` and commits `13826d80`,
`62747f8d` for the investigation trail. The CSpect plugin
(`tools/cspect_plugin/JnextG46bTrace.cs`) captured per-event physical
memory state with no BP perturbation — the most decisive measurement
technique we've established for this investigation.
