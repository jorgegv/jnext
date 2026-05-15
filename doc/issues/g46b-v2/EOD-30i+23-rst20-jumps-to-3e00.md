# EOD-30i+23 — RST $20 vector is `JP $3E00`; slot 1 missing supervisor code

**Date**: 2026-05-15
**Session**: g46b-investigation-v2 EOD-30i+23
**Status**: precise architectural barrier identified
**Predecessor**: [EOD-30i+22](EOD-30i+21-lddr-probe-and-zeros-stall.md) — $55A1 zeros-trap.

## The find

Disassembled the RST $20 vector ($0020) in the CSpect-extracted IPL
(now mapped at slot 0 via the 4-flag boot architecture):

```
$0020: C3 00 3E    JP $3E00     ← UNCONDITIONAL jump to $3E00
```

So every RST $20 instruction the supervisor executes is dispatched
unconditionally to PC=$3E00.

The CSpect IPL at $01D4 issues `RST $20; .DB $01,$1F` (with $01 $1F as
inline service-number parameters). After the RST, PC=$0020 → JP $3E00.

## Why this fails in jnext

$3E00 is in **slot 1 ($2000-$3FFF)**. With the 4-flag boot architecture:

- slot 0 ($0000-$1FFF) = RAM page 0, patched with FPGA-IPL bytes
  (`JNEXT_PATCH_ROM_BANK0_WITH_IPL=1`).
- slot 1 ($2000-$3FFF) = RAM page 1, which still contains the **upper
  8KB of 48.rom** (the Machine ROM seeded into RAM pages 0..7 in
  Next-mode init).

The upper 8KB of 48.rom is the **standard ZX Spectrum BASIC font
data + tail code**. Bytes at $3E00 are font glyphs (`3C 42 99 A1 A1
99 42 3C` = an "@" 8x8 bitmap). Z80 interprets these as instructions
that just walk into more NOPs.

## What CSpect / silicon does differently

In CSpect at the same RST $20, slot 1 has **supervisor code** at
$3E00 — not font data. This supervisor code was loaded earlier in
the boot path (between cold reset and the first RST $20).

Likely sources for slot 1's supervisor code:
1. **SD card I/O during IPL post-init**: the IPL polls the SD card
   via SPI ports ($E3, $E7, $EB), loads NextZXOS supervisor files
   (e.g., `tbblue.fw` continuation, `nextzxos.bin`, etc.) into RAM
   pages, then maps them to slot 1.
2. **NEXTREG $51 / NR $54 / NR $55 / similar MMU paging writes**:
   the IPL might write specific RAM page numbers to slot 1's MMU
   register, mapping in a pre-loaded RAM page.
3. **DivMMC SRAM bank switch**: the IPL might use DivMMC's port $E3
   bank-select to bring in a higher DivMMC SRAM bank that holds
   supervisor code copied from `enNxtmmc.rom`.

In jnext, ONE of these mechanisms is failing:
- Either the IPL's SD reads don't get the bytes they should
- Or the NEXTREG MMU writes don't have the effect they should
- Or DivMMC SRAM has wrong contents in the relevant bank

## Trace evidence

Precise PC transitions from BOOTPC trace:

```
step 206500: PC=$01D4 (in IPL — bytes `E7 01 1F` = RST $20; .DB $01,$1F)
step 206501: PC=$0020 (RST $20 entered)
step 206502: PC=$3E00 (= JP $3E00 from $0020)
step 206503-206761: PC walks sequentially through $3E00-$3FD4
                    (font data interpreted as random opcodes)
step 206761: PC=$3FD4, bytes `10 20` = DJNZ +$20 → $3FF6
step 206762: PC=$3FF6 (still in font padding, all $00 NOPs from here)
step ~213938: PC=$5BFE, transition to $5C01 (some sysvar address)
```

## Cumulative session summary

11 commits across EOD-30i+11..+23. Each layer of the boot is now
verified working OR identified as needing more work:

| Layer | Status |
|-------|--------|
| Cold-boot DI (slot 0) | ✓ |
| DivMMC AUTOMAP overlay at $0001 → enNxtmmc.rom trampoline | ✓ (with FORCE_AUTOMAP flag) |
| AUTOMAP deactivation after `OUT ($E3),0` | ✓ |
| FPGA-IPL NEXTREG init at $00EF-$00FC | ✓ |
| Boot ROM disable on NR $03 doesn't break (BANK0 patched) | ✓ |
| IPL post-init data-copy LDDR loops | ✓ |
| Supervisor RST $20 = JP $3E00 dispatched | ✓ |
| Supervisor code AT $3E00 (in slot 1) | ✗ **MISSING** |

## Possible fixes for next session

1. **Capture CSpect's slot 1 ($2000-$3FFF) at the equivalent boot
   moment** — via DZRP `read_mem(0x2000, 0x2000)` paused at a BP just
   before the first RST $20. See what bytes should be at $3E00.
2. **Capture the IPL's MMU / DivMMC bank-switch writes** during the
   boot via probes on NR $51/$54/$55 and port $E3. The "correct" slot
   1 contents come from somewhere — find that source.
3. **Add a slot-1 patch flag** analogous to `PATCH_ROM_BANK0_WITH_IPL`,
   loading the right bytes into RAM page 1 from a captured CSpect
   dump.
4. **Audit jnext's SD/SPI port emulation** — the IPL probably reads
   SD blocks during the post-init phase to load supervisor code.
   Differences in port behavior might cause data corruption.

## Artifacts

- `src/cpu/z80_cpu.cpp` — `JNEXT_G46B_LDDR_PROBE`, `JNEXT_G46B_55A1_PROBE`,
  `JNEXT_G46B_3FD4_PROBE` env-gated probes.
- Trace logs: `/tmp/before_55a1.log`, `/tmp/3fd4_probe.log`,
  `/tmp/55a1_probe.log`, `/tmp/lddr_probe.log`.

## Cross-references

- `EOD-30i+20-patch-rom-bank0.md` — slot 0 patched.
- `EOD-30i+21-lddr-probe-and-zeros-stall.md` — $55A1 zeros-trap.
- VHDL `divmmc.vhd:94-95` — slot 1 overlay condition.
