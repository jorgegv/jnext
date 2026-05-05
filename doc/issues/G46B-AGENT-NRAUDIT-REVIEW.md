# G46(b) NR-Handler Audit — Independent Reviewer Report

**Date**: 2026-05-05
**Reviewer**: independent read-only review agent
**Reviewing**: `doc/issues/G46B-AGENT-NRAUDIT.md`
**Verdict**: **APPROVE WITH ADDITIONS / CORRECTIONS**

The original audit is broadly accurate and methodologically sound. Cross-checked
about a dozen of the higher-risk handlers against `zxnext.vhd` directly and
against `src/core/emulator.cpp`, `src/port/nextreg.cpp`, `src/peripheral/divmmc.cpp`,
and `src/memory/mmu.cpp`. The "no HIGH-severity divergence" headline holds for
the registers actually exercised during the early supervisor loop ($00EF →
$011E → $1DF3/$2043/$20E6). However, the report contains **two factual errors**
and **one subtle miss** that warrant correction; one of the listed MED culprits
should be downgraded.

## Corrections to the original report

### CORRECTION 1 — NR 0x85 bit-2 leak claim is FALSE (Culprit #3 is invalid)

The audit's "Top-3 Culprit #3" claims VHDL "hard-zeroes bit 2 via the write
mask" so jnext's bare-store leaks bit 2 into the `cached(0x85) & 0x04` gate at
`emulator.cpp:2460`.

**This is wrong.** VHDL `zxnext.vhd:5508`:
```
nr_85_internal_port_enable <= nr_wr_dat(3 downto 0);
```
The mask is bits **3:0** = `0x0F`, which **includes bit 2** (`0x04`). VHDL DOES
store bit 2. The read mux at `zxnext.vhd:6138` returns
`reset_type & "000" & enable[3:0]` — bit 2 of the read = bit 2 of the original
write. jnext's read_handler at `emulator.cpp:1786-1788` returns
`cached(0x85) & 0x8F` which preserves bit 2 identically. The 2460 gate sees the
same value as VHDL would.

**Verdict**: drop Culprit #3 entirely. The supervisor's `nextreg $85,a` (a=0xFF)
at PC=$0106 sets bit 2 = 1 in BOTH jnext and VHDL. No divergence.

### CORRECTION 2 — NR 0x02 read does propagate reset_type[1:0]

Audit text in §"NR 0x02" claims jnext's NR 0x02 read "does NOT set… bits 1:0
(`reset_type[1:0]`)". This is incorrect. `NmiSource::nr_02_read()` at
`src/peripheral/nmi_source.cpp:136` does
`r |= static_cast<uint8_t>(reset_type_ & 0x03);` — bits 1:0 ARE present.
The bypass-init at `emulator.cpp:3803` calls `nmi_source_.strobe_soft_reset()`
which advances the FSM (100 → 010), so a supervisor read of NR 0x02 sees bits
1:0 = `10`. **MATCH** with VHDL behaviour after the bypass init.

The remaining unmodelled bits are 7 (`nr_02_bus_reset`) and 4 (`nr_02_iotrap`)
— both write-only side-effects, neither is observed live during the loop in
the LIVE doc. Severity stays LOW (not MED).

## Additions / items not flagged with sufficient prominence

### ADDITION 1 — NR 0x85 cold-init uses 0x8F which differs from VHDL

`nextreg.cpp:65`:
```
regs_[0x85] = 0x8F;  // bit7=reset_type(1), bits6:4=0, bits3:0=0xF
```
VHDL reset (`zxnext.vhd:5052-5057`) sets `nr_85_internal_port_reset_type='1'`
and `nr_85_internal_port_enable=(others=>'1')` — so cold readback would be
`1 & "000" & "1111" = 0x8F`. **MATCH.** The audit didn't enumerate cold
defaults; this one is fine. Noting for the next person who diffs cold state.

### ADDITION 2 — NR 0x44 read divergence DOES potentially affect supervisor

The audit rates NR 0x44 read divergence as LOW because "supervisor doesn't read
NR 0x44 during early boot". The reviewer's disasm confirms the supervisor
reads NR 0x44 at PC=$0A3B inside a palette-dump loop at $0A28-$0A44:

```
0a36: ld a,$44 ; out (c),a ; inc b ; in a,(c) ; dec b ; ld (hl),a ; inc hl
```

This loop runs over 256 palette entries and stores raw NR 0x44 reads into a
RAM table. VHDL returns `nr_palette_dat(10:9) & "00000" & nr_palette_dat(0)`
(3 meaningful bits: priority + ext_blue_lsb), jnext returns the raw last-
written 9-bit-mode byte. **However**: this PC is reached in NextZXOS scratchpad
init AFTER the supervisor exits the $00EF → $1DF3/$2043/$20E6 loop, so it does
NOT contribute to the loop blocker. Severity stays LOW for the immediate G46(b)
investigation but should be revisited once boot progresses past the loop.

### ADDITION 3 — NR 0x06 write-handler has no `nr_06_psg_mode_` bit-1 hold for ay_mode_bit alias

Cosmetic. The VHDL at line 6389 only uses bit 0 of psg_mode for `aymode_i`.
The audit notes this correctly. No change needed.

### ADDITION 4 — `--bypass-tbblue-fw` writes NR 0x83 = 0x3D which DOES sync `port_io_enable_`

The audit's MED-rated "Culprit #2" worries that bypass-init might leave
`divmmc_.port_io_enable_=false` while `regs_[0x83]=0xFF`. Verified:
`emulator.cpp:3781` calls `nextreg_.write(0x83, 0x3D)`, which fires the
write_handler at `emulator.cpp:1774-1781`, which calls
`divmmc_.set_port_io_enable((0x3D & 0x01) != 0)` = `set_port_io_enable(true)`.
After bypass-init `divmmc_.port_io_enable_ == ((cached(0x83) & 0x01) != 0)` =
`true == true`. **MATCH** — the worry is unfounded once bypass-init runs to
completion. Downgrade severity from MED to LOW. The proposed assertion is
still cheap and worth adding defensively, but no bug here.

## Items the audit got right (independently confirmed)

- NR 0x06 write/read parity — VHDL :5161-5170 / :5900 vs `emulator.cpp:2715-2799`. **MATCH**.
- NR 0x07 — single-state model is fine in the no-expbus jnext build.
  `cpu_speed=nr_07_cpu_speed` per VHDL :5817 when `expbus_eff_en='0'`.
- NR 0x8E — `mmu.cpp:434-509` vs VHDL :3662-3735, :6158-6159. **MATCH**.
- NR 0x8C altrom redirect — `mmu.h:212-322` honours `altrom_sram_page_()` for both
  read and write paths, gated on `nr_8c_altrom_en` + `nr_8c_altrom_rw`. **MATCH**.
- NR 0x82/0x83/0x84 — bare store of full 8 bits matches VHDL. The bypass-init
  bytes (0xDA, 0x3D, 0xFF) flow through correctly.
- NR 0x10 anti-brick — config_mode-gated coreid update via `nextreg_.nr_03_config_mode()`.
- NR 0xC2/C3 NMI return shadow — bare regs_ access via `set_nmi_return_address()`.
- NR 0x68 ULA control — bit 7 enable, bits 6:5 blend, bits 2/3, all wired.

## Top recommendation (single highest-leverage next step)

The original audit's **Culprit #1** stands as the highest-leverage diagnostic.
The triple-source instrumentation at HOOK #1 (mmu_.get_page(7) vs
nextreg_.cached(0x57) vs nextreg_.read(0x57)) remains the single best
experiment — it's cheap (3 log lines), it discriminates between three distinct
bug classes, and it directly addresses the MMU-read divergence that the LIVE
doc has cornered as the proximate cause of the wrapper-vs-dispatcher
branch at PC=$2734.

**Drop** the audit's Culprit #3 (NR 0x85 bit-2 leak) from the fix list — it's
based on a misreading of `nr_wr_dat(3 downto 0)`. **Downgrade** Culprit #2 to a
defensive assertion (no actual bug). **Promote** Culprit #1 (NR 0x57 / mmu page 7
divergence) to the sole HIGH-priority next step, exactly as the original audit
recommends.

## Audit coverage gaps (not blockers, deferrable)

- Reviewer did not exhaustively re-walk NR 0x40/41/43/44 palette state machine
  vs supervisor's $0A28 dump loop. Recommend a second-pass audit of the palette
  read path AFTER the loop is unblocked.
- NR 0x05 Pentagon-override path is unmodelled — irrelevant for the +3 boot
  path NextZXOS takes here.
- NR 0x81 `expbus_speed` bits-1:0 leak — irrelevant without expbus.

## Summary

- **VERDICT**: **APPROVE WITH ADDITIONS** — original audit is solid; Culprit
  #3 is a false positive (audit misread the VHDL mask); Culprit #2 is a
  defensive non-bug; Culprit #1 stands as the right next step.
- **Highest-leverage next experiment**: NR 0x57 triple-source dump at HOOK #1.
- **No HIGH-severity NR-handler bugs** were missed by the original audit. The
  G46(b) loop is NOT explained by the NR-handler scaffolding; the cause lies
  in MMU page-7 storage / port-decode path, exactly as the LIVE doc indicates.
