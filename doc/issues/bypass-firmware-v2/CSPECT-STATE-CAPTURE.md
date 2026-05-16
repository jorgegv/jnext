# Task 18 — CSpect State Capture (firmware-bypass empirical study)

Date: 2026-05-17. CSpect 3.1.0.0 / DZRP 2.0.0.
SD image: `roms/nextzxos-1gb-fat32fix.img` (canonical jnext test image).

Goal: empirically capture CSpect's state at (a) cold reset (`-debug`,
CPU halted at PC=0) and (b) after a full NextZXOS boot (no `-debug`,
~15 s after launch). If the post-boot state is simple enough to
replicate, jnext can skip running tbblue.fw entirely.

Capture scripts: `tools/cspect_dzrp/task18_*.py` (5 files).

---

## 1. Cold-reset state (CSpect launched with `-debug`)

CPU halted at PC=0 before any FPGA/firmware instruction has executed.

### Z80 registers (cold)

```
PC=0000 SP=0000 AF=0000 BC=0000 DE=0000 HL=0000 IX=0000 IY=0000
AF'=0000 BC'=0000 DE'=0000 HL'=0000  I=00 R=00 IM=0
slots = [FF FF 0A 0B 04 05 00 01]
```

### Cold-reset NextRegs (non-zero only — 29/256)

| Reg | Val | Meaning |
|-----|-----|---------|
| $01 | $40 | core ID byte 1 |
| $03 | $33 | **machine_type=+3 (lower nibble 3), config_mode=0, boot ROM enabled** |
| $05 | $51 | peripheral 1 (`50Hz, joystick, scandbl, scanlines`) |
| $06 | $05 | **peripheral 2 (matches prior-session VHDL-divergence claim: VHDL says $A0)** |
| $08 | $98 | peripheral 3 |
| $09 | $03 | peripheral 4 |
| $0A | $11 | peripheral 5 |
| $11 | $07 | video timing (`07` = NTSC-VGA-default?) |
| $12 | $08 | layer 2 active page = 8 |
| $13 | $0B | layer 2 shadow page = 11 |
| $14 | $E3 | global transparent colour |
| $1F | $F8 | sprite/layer-2 clip / pixel state |
| $42 | $0F | ULA-mode/palette ctrl |
| $4B | $E3 | transparent colour (palette-aware) |
| $4C | $0F | fallback colour |
| $50..$55 | FF FF 0A 0B 04 05 | **slot map: boot ROM at $0000/$2000, then standard +3 banks** |
| $57 | $01 | slot 7 = bank 1 |
| $6E | $2C | copper ctrl |
| $6F | $0C | copper data |
| $7F | $FF | DivMMC/expansion |
| $B8 | $83 | int control / source |
| $B9 | $01 | |
| $BB | $CD | |

### Cold-reset memory at $0000 (16 bytes)

```
F3 C3 EF 00  45 44 09 02  C3 3B 10 2A  2E 2A FF 00
DI JP $00EF  ... ... ...  ... ... ...  ... ... ...
```

Matches the EOD-30i+14 CSpect boot-ROM disassembly exactly.

---

## 2. Post-boot state (CSpect free-running ~15 s, then `pause()`)

NextZXOS has booted; CSpect was paused mid-execution.

### Z80 registers (post-boot)

```
PC=0C90 SP=FF3F AF=03AA BC=0ABC DE=0002 HL=5C3B IX=F700 IY=5C3A
AF'=0054 BC'=1C1F DE'=0082 HL'=5ED7  I=09 R=00 IM=1
slots = [FF FF 0A 11 04 05 00 01]
```

PC=$0C90 is **inside the boot ROM** (slot 0 = $FF). The code there
reads sysvars `$5C3B` (FLAGS), `$5C68` (TVFLAG), `$5C41` (MODE) — i.e.
the boot ROM contains BASIC-compatible runtime that uses standard
ZX-BASIC sysvars at $5B/$5C. CSpect was caught in NextZXOS's main
input/idle loop.

IM 1 enabled, IFF1/2 reflected in I=$09 (interrupt vector page).

### Slot delta (cold → post-boot)

| Slot | Cold | Post | Delta |
|------|------|------|-------|
| 0 | $FF | $FF | unchanged (boot ROM still mapped at $0000) |
| 1 | $FF | $FF | unchanged (boot ROM still mapped at $2000) |
| 2 | $0A | $0A | unchanged (legacy bank 5 lower half) |
| 3 | $0B | **$11** | **changed: now bank $11 (8.5 in legacy terms — NextZXOS text-data bank)** |
| 4 | $04 | $04 | unchanged |
| 5 | $05 | $05 | unchanged |
| 6 | $00 | $00 | unchanged |
| 7 | $01 | $01 | unchanged |

Only one slot changed during boot: slot 3 ($6000-$7FFF), from bank
$0B → $11. NR $53 is the only MMU register actively used by
NextZXOS's boot in this run.

### NextReg delta (non-trivial changes)

| Reg | Cold | Post | Notes |
|-----|------|------|-------|
| $05 | $51 | $59 | peripheral 1 — bit 3 set |
| $06 | $05 | $AC | peripheral 2 — almost-full change (PS/2 key map / mouse / audio) |
| $07 | $00 | **$33** | **CPU speed bumped to 28 MHz (bits 0-1 = 11)** |
| $08 | $98 | $DE | peripheral 3 — audio/AY config |
| $11 | $07 | $07 | unchanged |
| $12 | $08 | $09 | layer 2 page moved 8 → 9 |
| $13 | $0B | $09 | layer 2 shadow moved 11 → 9 |
| $1F | $F8 | $A9 | |
| $40 | $00 | $1F | palette index |
| $42 | $0F | $00 | ULA-mode reset to default after boot |
| $4B | $E3 | $E3 | unchanged |
| $4C | $0F | $0F | unchanged |
| $53 | $0B | $11 | slot 3 (mirrors slots[]) |
| $82 | $00 | $FF | expansion bus / IO trap masks |
| $83 | $00 | $FF | " |
| $84 | $00 | $FF | " |
| $85 | $00 | $FF | " |
| $B8 | $83 | $82 | int control: bit 0 cleared |
| $BB | $CD | $F2 | |
| $C0 | $00 | $08 | raster int control / bit 3 = ULA INT? |
| $D8 | $00 | $01 | |

### Slot-0 remap probe — which physical bank holds the boot ROM image?

```
Bank   First 16 bytes @ $0000     Verdict
$FF    F3 C3 EF 00 45 44 09 02    boot ROM (DI JP $00EF)
$FE    F3 C3 EF 00 45 44 09 02    boot ROM (same image — $FE/$FF are
                                   special sentinels resolving to the
                                   FPGA-baked boot ROM)
$FD    00 44 BB 7A ...            non-ROM (looks like uninit SRAM)
$FC    6A EF A1 43 ...            non-ROM
$00    zero
$01    zero
$02    E5 E5 E5...                uninit SRAM pattern
$03    E5 E5 E5...                "
$04    text: " choose to run C…"  NextZXOS welcome banner (slot 4)
$05    text: "software for you…"  banner cont. (slot 5)
$80-$85, $A0-$A3   random         dynamic / SD-loaded
```

### Physical-bank fingerprint (16 banks, first 32 bytes)

```
$00 zero            $08 E5 fill         $10 partial init
$01 zero            $09 E5 fill         $11 NextZXOS BASIC text (slot 3 at pause)
$02 E5 fill         $0A NextZXOS BASIC  $12-$17 zero/E5
$03 E5 fill         $0B zero            $18-$1F random (=uninit)
$04 NextZXOS text   $0C E5 fill         $A0-$BF random (likely
$05 NextZXOS text   $0D E5 fill           NextZXOS heap / DivMMC SRAM)
$06 E5 fill         $0E small code      
$07 E5 fill         $0F zero
```

Only **banks $04, $05, $0A, $0E, $10, $11** show evidence of having
been populated by NextZXOS during boot. The rest are either zero (BASIC
RAM cleared) or `$E5` (DRAM-startup signature pattern that CSpect
emulates).

---

## 3. Verdict on user's three sub-questions

### (a) Does CSpect default to "+3 mode" at cold reset?

**Yes, empirically confirmed.** `NR $03 = $33` at cold reset. Lower
nibble = 3 = "+3" machine type per
[Machine ID register spec](https://wiki.specnext.dev/Machine_ID_Register).
This is set by the FPGA before instruction 0 — it is silicon state, not
firmware state.

Note: this is what jnext's `nextreg.cpp` should reflect at cold reset
too. The prior-session claim about NR $06 ($05 in CSpect vs $A0 in VHDL
spec) is **also empirically confirmed** — CSpect's silicon-faithful
cold-reset value is $05, contradicting some VHDL reads. Worth a
secondary VHDL audit; for the bypass scope here, take CSpect's values
as ground truth.

### (b) Is post-boot state simple enough to replicate?

**Mixed verdict — moderate complexity.** Three categories of state:

1. **Simple register-level state (trivial to replicate):** ~20 NextReg
   deltas (table in §2). All single-byte writes; could be set by a
   short jnext init routine in <50 lines of C++. Slot map: only
   NR $53 changed.
2. **Boot ROM still mapped (not really "post-boot" in the bypass
   sense):** slot 0/1 = $FF means CSpect's CPU is **still executing
   from the FPGA boot ROM** even after NextZXOS is "fully booted" and
   sitting at the BASIC ready prompt. The boot ROM image is permanent;
   no replication needed.
3. **Populated SRAM banks (the hard part):** banks $04, $05, $0A, $0E,
   $10, $11 contain NextZXOS code/data loaded from SD. These are
   ~96 KB of content (6 × 16 KB) that must be either (a) extracted
   from the SD image by jnext at init time (the path Task 8 already
   enables for ROMs), or (b) shipped as a pre-built blob. Banks $18-$1F
   and $A0-$BF look random; they are likely DivMMC SRAM regions whose
   contents are residual / not load-bearing for boot, but this needs
   verification.

### (c) Biggest risk in the bypass approach

**The boot ROM is the OS.** Post-boot, the CPU is still inside the
boot ROM at PC=$0C90. The boot ROM contains not just the firmware
loader (boot.c, tbblue.fw) but **also the NextZXOS embedded supervisor
and BASIC-compatible runtime** that consumes the loaded SRAM banks.
This means a "bypass" cannot just initialise registers and SRAM and
then `JP <NextZXOS_entry>` — the boot ROM IS the entry, and its
internal state (sysvars at $5B/$5C in legacy RAM bank 5 / slot 2 / bank
$0A) is co-evolved with the SD-loaded bytes in banks $04/$05/$11.

To replicate the post-boot state we would have to capture:

1. All 8 NR $50..$57 values (trivial).
2. ~20 other NR deltas (trivial).
3. Full contents of banks $04, $05, $0A, $0E, $10, $11 (and possibly
   more for shadow-screen / sysvars-spillage).
4. Z80 register state (PC=$0C90, all main and shadow regs, IM=1).
5. SP and stack contents (~few hundred bytes).

That's ~100 KB of binary state plus a ~30-byte register snapshot. It
would be a "saved state" by another name, gated on a specific NextZXOS
version + ROM build.

**Single biggest risk:** the bypass tightly couples jnext to a frozen
NextZXOS image. Any NextZXOS update (e.g. v2.13 → v2.14) would
invalidate the captured state and require re-capture; users who use a
different SD image entirely (custom firmware, alternate distros) would
break entirely. The current jnext model (run the firmware against the
user's SD) is *user-data-faithful*; the bypass is *frozen-image-faithful*.

**Secondary risk:** the captured state contains transient values
(e.g. NR $40=$1F is a palette write-position, NR $05=$59 has
keyboard-poll-cycle implications). Some of these may be sampled
mid-operation; replicating a frozen mid-operation state into a fresh
emulator could produce subtle desync.

**Tertiary risk:** the boot ROM at slot 0/1 is the FPGA-baked image
(`nextboot.rom`). The 2026-05-15 EOD-30i+14 finding established that
jnext's `roms/nextboot.rom` differs from CSpect's boot ROM by ~97 %.
A bypass that depends on the boot ROM containing the NextZXOS
supervisor + RST handlers must first solve the wrong-boot-ROM problem.

---

## 4. Recommendation

A pure-NR + slot-init bypass (no SD-loaded bank contents) **will not
work** — it would leave the CPU executing boot ROM code that expects
banks $04/$05/$0A/$0E/$10/$11 to contain NextZXOS-loaded bytes.

A full state-capture bypass (~100 KB blob) **is feasible** but is
better characterised as **"shipping a pre-booted NextZXOS save state"**
rather than firmware bypass. It addresses the symptom (boot is slow /
fragile) but not the cause (jnext doesn't faithfully run tbblue.fw).

**The empirical state is not as simple as the plan assumes.** Task 18
should either (a) accept the save-state framing and pursue it
explicitly (with clear UX scope: users get a frozen NextZXOS), or
(b) re-focus on fixing the boot-ROM divergence (EOD-30i+14 root cause)
so the firmware path works correctly.

---

## 5. Raw capture artefacts

- `tools/cspect_dzrp/task18_cold_reset_capture.py` → curated cold-reset state.
- `tools/cspect_dzrp/task18_full_nr_sweep.py` → all 256 NextRegs at cold reset.
- `tools/cspect_dzrp/task18_post_boot_capture.py` → post-boot regs + NR sweep + per-slot memory + 64-bank fingerprint.
- `tools/cspect_dzrp/task18_full_memory.py` → 64 KB CPU view + slot-0 remap probe.
- `tools/cspect_dzrp/task18_probe_pc.py` → code window around current PC + stack.

Raw output saved out-of-tree to `/tmp/task18_*.md` during the capture
session (regenerable from the scripts above against a fresh CSpect
launch).
