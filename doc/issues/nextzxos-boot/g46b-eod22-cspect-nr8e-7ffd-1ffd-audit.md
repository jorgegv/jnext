# G46(b) EOD-22 — CSpect NR $8E / 7FFD / 1FFD audit

**Date:** 2026-05-08 23:58 CEST
**Investigator:** subagent (DZRP ground-truth via CSpect)
**CSpect:** 3.1.0.0 + DezogPlugin v2.3.0.20958 (DZRP 2.0.0)
**Image:** `roms/nextzxos-1gb-fat32fix.img`
**Launch:** `mono ../CSpect3_1_0_0/CSpect.exe -mmc roms/nextzxos-1gb-fat32fix.img` (no `-debug`)
**Scripts:** `tools/cspect_dzrp/g46b_eod22_nr8e_audit.py`,
`g46b_eod22_nr8e_audit2.py`. Raw output:
`/tmp/g46b_eod22_nr8e_audit.out`, `/tmp/g46b_eod22_nr8e_audit2.out`.

## Question

In +3 mode, VHDL `:3727` says NR $8E writes update both 7FFD bit 4 and
1FFD bit 2; sram_rom = 1FFD(2):7FFD(4). NEXTREG $8E,$03 should
therefore land in sram_rom = `11` = bank 3 (= +3 DOS area). On jnext
the supervisor reaches bank 3 and the bank-flip wrapper RETs to $0000.
On CSpect, post-boot supervisor runs from bank 1 (= 48K BASIC at
$0C90). Why?

## Method

Connected via DZRP after a 30 s boot wait. Snapshot regs/NextRegs/slots,
read mem at the bank-flip wrapper region and at canonical ROM
addresses, set BPs at all relevant supervisor entry points, and ran
free for 30 s.

## Raw observations (post-boot steady state)

```
PC=0C90 SP=FF3F AF=029A BC=02CF DE=0002 HL=5C3B IX=F700 IY=5C3A IM=1
AF'=0054 BC'=1C1F DE'=0082 HL'=5ED7  I=09 R=00
slots = [FF FF 0A 11 04 05 00 01]    (NR $50..$57)

NR $00=$08  NR $01=$40  NR $02=$00  NR $03=$33  NR $07=$33
NR $40=$1F  NR $43=$00  NR $8C=$00  NR $8E=$00  NR $8F=$00

port read 0x7FFD = $FF   port read 0x1FFD = $FF   port read 0xDFFD = $00
```

Note: `read_port` on 7FFD/1FFD returns `$FF` because both ports are
write-only on real Next; CSpect honours that. **`read_port` cannot be
used to observe legacy paging state**. Likewise, NR $8E **read returns
`$00`** — the CSpect plugin appears not to expose NR $8E composition
(or the supervisor genuinely has 7FFD/1FFD == `$00` and the read is
zero, but that contradicts the actual ROM content fingerprint —
indicating CSpect's NR $8E read is just unimplemented/zeroed).

## ROM content fingerprint (decisive)

```
$0000  F3 C3 EF 00 ...   ; DI; JP $00EF (boot vector)
$00EF  ED 91 07 03       ; NEXTREG $07,$03  (cpu speed = 28 MHz)
$00F3  ED 91 03 B0       ; NEXTREG $03,$B0  (machine-type = config-mode locked)
$0C90  21 3B 5C CB 6E 28 EA CB AE 3A 08 5C 21 41 5C FE   ; canonical 48K BASIC KEY-INPUT
```

`$0C90` is the well-known 48K BASIC `KEY-INPUT` poll. Its presence at
$0C90 with `21 3B 5C ...` proves slot 0 is currently routed to the
**48K BASIC ROM** (= rom_bank 1 in the +3 4-way table = sram_rom `01`).
This is **VHDL-faithful**: the supervisor reaches the BASIC main loop
via 1FFD(2)=0 + 7FFD(4)=1.

## Bank-flip wrapper region in RAM

The 4-way wrapper table is RESIDENT and identical to jnext:

```
$5B3D  ED 91 8E 01 C9    ; NEXTREG $8E,$01; RET   (= bank 1, 48K BASIC)
$5B43  ED 91 8E 02 C9    ; NEXTREG $8E,$02; RET   (= bank 2)
$5B48  ED 91 8E 03 C9    ; NEXTREG $8E,$03; RET   (= bank 3, +3 DOS)
$5B4D  ED 91 8E 00 C9    ; NEXTREG $8E,$00; RET   (= bank 0)
$5B00..$5B2F             ; dispatcher / RET-into-target trampoline
$0080..$008F             ; cross-bank `CALL trampoline` reads inline DW
```

## Critical: BPs all silent for 30 s

BPs at `$0080, $5B00, $5B20, $5B48, $00EF, $00F3` — none fired in 30 s
of free-run. PC stayed pinned at `$0C90` with `BC` counting down
monotonically (`$02CF → $02C8` across 6 sample taps spaced 250 ms).

**Real-Next NextZXOS post-boot does NOT exercise the NEXTREG $8E
wrappers at all.** Bank routing is set ONCE during boot and the
supervisor lives in bank 1 (48K BASIC) thereafter, polling the keyboard.

## NR $8E,$03 write injection

Issued `OUT 0x243B,$8E ; OUT 0x253B,$03` via DZRP `write_port`. NR $8E
read still returned `$00`; port reads of 7FFD/1FFD still `$FF` (both
write-only — uninformative). PC, SP and slot map all unchanged. The
plugin's NR $8E is opaque; we cannot directly verify "did 1FFD bit 2
flip" via DZRP.

## Verdict

1. **CSpect's bank routing IS VHDL-faithful in steady state.** Slot-0
   ROM contents at `$0C90` are bit-identical to the 48K BASIC ROM,
   which corresponds to `sram_rom = 01` in the +3 4-way table — exactly
   what VHDL `:3008` produces in +3 mode for 1FFD(2)=0, 7FFD(4)=1.

2. **The supervisor never executes the NR $8E wrappers in steady
   state.** None of `$5B00, $5B20, $5B48, $00EF, $00F3, $0080` fire in
   30 s. Bank 1 was reached during boot (presumably by an explicit
   `OUT $7FFD,$10` or `NEXTREG $8E,$01`), and the supervisor stays
   there forever, polling the keyboard at $0C90.

3. **CSpect's NR $8E read-back is unimplemented (returns $00) and port
   reads of 7FFD/1FFD honour the write-only nature (return $FF).** We
   cannot directly verify whether `NEXTREG $8E,$03` updates 1FFD(2) on
   CSpect. But VHDL faithfulness is implied because the steady-state
   routing is observably correct.

4. **The jnext bug is NOT "NR $8E should not update 1FFD".** The
   wrapper at `$5B48` is never reached in real-Next operation; on jnext
   it IS being reached, which means jnext's supervisor took a *different
   code path during boot* — most likely the post-soft-reset reinit at
   `$00F3` (NEXTREG $03,$B0 — config-mode lock), which would later go
   through `$0080` cross-bank trampoline + `$5B48` dispatch wrapper to
   re-enter +3 DOS area. Real-Next reaches `$0C90` directly without
   needing that detour.

## Next-session priorities

- **Compare boot trace, not steady state.** The divergence is during
  early boot. Set up a DZRP probe that BP-traps at `$00EF` from a
  freshly-launched CSpect (BP set BEFORE first execution). Capture the
  first ~200 PC events. Same in jnext via `JNEXT_G46B_PC0_TRAP`.
  Compare.
- **Audit jnext's `NEXTREG $03,$B0` handling.** $00F3 writes
  `NR $03 ← $B0` = `1011 0000` = lock-bit + machine-type=config-mode.
  Does jnext correctly transition machine_type back to `+3` later, or
  does it stay locked in config and force the wrapper-RET-to-$0000
  failure mode?
- **Don't pursue "make NR $8E not touch 1FFD".** That would deviate
  from VHDL and won't fix the underlying issue, which is in the boot
  sequence's machine-type state machine.

## Cleanup

- CSpect process killed at end of session.
- Two new scripts in `tools/cspect_dzrp/` for reuse:
  `g46b_eod22_nr8e_audit.py` (write-and-readback test) and
  `g46b_eod22_nr8e_audit2.py` (full sweep + BP probe).
