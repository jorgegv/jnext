# Verify-21 DivMMC + SD + SPI subsystem audit — Independent Reviewer

Reviewer pass against `NEXTZXOS-BOOT-SUBSYSTEM-VERIFY21-DIVMMC-SD-SPI.md`
(audit HEAD `142284c`). Pass-21 is the FIRST DivMMC zero-finding audit
since the enumeration-table mandate landed in Pass-19. This review
honours the high-stakes convergence-determination protocol per
`feedback_task2_audit_enumeration_table.md` + `feedback_task2_converged_subsystem_skip.md`.

## Verdict — APPROVE, no missed findings

DivMMC + SD + SPI subsystem is officially **CONVERGED** at byte-level
granularity. Pass-22 and later MUST SKIP this subsystem unless a new
finding in another subsystem appends a new surface to DivMMC/SD/SPI.

## Baseline tests on the reviewer worktree

Release-mode build of integration HEAD `142284c`:

- `ctest`: 38/38 PASS, 0 FAIL.
- `fuse_z80_test`: 1356/1356 PASS.
- `divmmc_test`: 137/137 PASS (all plan rows covered, 0 skips).
- `sdcard_test`: 33/33 PASS.
- `rewind_test`: 22 PASS / 0 FAIL / 10 SKIP (G67-gated).
- Full regression suite `test/00regression/regression.sh`: 33/0/0.

All invariants match the Pass-17 handover baseline. No regressions.

## Row-count validation

Audit table claims **108 rows**. Reviewer count: **108 rows** (counted via
`grep -c "^| [0-9]\+ |" NEXTZXOS-BOOT-SUBSYSTEM-VERIFY21-DIVMMC-SD-SPI.md`).
Numbering is contiguous 1..108. **PASS.**

Coverage is exhaustive per the Pass-19 mandate: every SD CMD handler
(15 commands + default-illegal + ACMD41), the full SD-FSM state machine
(rows 61..74 cover all 6 State enum values plus all transitions
including data-token, past-EOF mid-stream, write-response, multi-block
re-prime), every SPI port handler (E3/E7/EB write+read), every
NextREG-side bit (NR 0x09 bit 3, NR 0x0A bits 7:6/5/4, NR 0x83 bit 0
and bit 3, NR 0xB8..0xBB write+read), every DivMmc automap entry-point
class (RST bank, NMI@$0066, four tape traps, $3Dxx wildcard, $1FF8-$1FFF
off-trigger), and the host-side state machinery (SpiMaster CS decode,
device-binding, save/load schemas). The host-side `sd_rom_extractor`
init path is intentionally not enumerated (no VHDL equivalent — the
runtime SPI/SD path at `src/peripheral/sd_card.cpp` is independently
audited).

## Spot-check of 10 ✓ rows against VHDL

Each VHDL line cited in the audit table was read directly with 5-10
lines of surrounding context.

1. **Row 1** (`divmmc.cpp:105-126` write_control). VHDL
   `zxnext.vhd:4180-4183`: `port_e3_reg(7) <= cpu_do(7); port_e3_reg(6) <=
   cpu_do(6) or port_e3_reg(6); port_e3_reg(3 downto 0) <= cpu_do(3
   downto 0)`. C++ at line 107: `mapram_ = mapram_ || ((val & 0x40) !=
   0)` (OR-latch in place); line 122: `control_reg_ = (val & 0x8F) |
   (mapram_ ? 0x40 : 0x00)`. Bits 7, 3:0 plain; bit 6 OR-latched. F19
   NIT-01 closure confirmed: bits 5:4 dropped from raw input before fold.
   **PASS.**

2. **Row 2** (`divmmc.cpp:128-130` read_control). VHDL `zxnext.vhd:4190`:
   `port_e3_dat <= port_e3_reg(7 downto 6) & "00" & port_e3_reg(3 downto
   0)`. C++: `return control_reg_ & 0xCF`. Bits 5:4 masked to zero.
   **PASS.**

3. **Row 3** (`divmmc.cpp:134-138` clear_mapram). VHDL `zxnext.vhd:4184-
   4185`: `elsif nr_09_we='1' and nr_wr_dat(3)='1' then port_e3_reg(6) <=
   '0'`. C++: clears `mapram_` and `control_reg_ & ~0x40`. Wired from
   `emulator.cpp:4235-4242` via NR 0x09 write_handler. NOT gated by
   `port_divmmc_io_en` — matches VHDL process which has no enable gate.
   **PASS.**

4. **Row 13** (`divmmc.cpp:307-312` button_nmi held-clear). VHDL
   `divmmc.vhd:112-113`: `elsif automap_held='1' then button_nmi<='0'`.
   C++: `if (automap_held_ && button_nmi_) button_nmi_ = false;` —
   continuous-while-held semantics. Pass-8 verify-audit fix locked in.
   **PASS.**

5. **Row 16** (`divmmc.cpp:350-372` RST entry decode). VHDL
   `zxnext.vhd:2848-2884`: case statement on `cpu_a(5:3)` for 8 RST
   addresses ($0000..$0038 step 8). C++ uses precomputed `rst_addrs[8]`
   table with `pc == rst_addrs[i]` — mathematically equivalent. **PASS.**

6. **Row 38** (`divmmc.cpp:263` is_nmi_hold). VHDL `divmmc.vhd:150`:
   `o_disable_nmi <= automap or button_nmi`. C++:
   `return automap_active_ || button_nmi_`. Pass-11 fix included
   `automap_active_` = combinational `held OR instant_match` per VHDL
   `automap` definition at line 148. **PASS.**

7. **Row 44/45** (`spi.cpp:120-170` SD0/SD1 match). VHDL `zxnext.vhd:
   3311-3314`: `cpu_do(1:0)="10" -> "111111" & not sd_swap & sd_swap`,
   `cpu_do(1:0)="01" -> "111111" & sd_swap & not sd_swap`. C++:
   `(val & 0x03) == 0x02 -> sd_swap_ ? 0xFD : 0xFE`,
   `(val & 0x03) == 0x01 -> sd_swap_ ? 0xFE : 0xFD`. Bit-exact match.
   **PASS.**

8. **Row 48** (`spi.cpp:141-152` Flash match). VHDL `zxnext.vhd:3319-
   3320`: `cpu_do=X"7F" AND ((nr_03_config_mode='1') OR
   (nr_02_reset_type(2)='1')) -> X"7F"`. C++: `val == 0x7F &&
   flash_cs_enable_ -> 0x7F`. The `flash_cs_enable_` flag is fed from
   `emulator.cpp:2027-2029,2375,4826,7335` after NR 0x02/0x03 writes,
   reset, and load_state — mirroring the VHDL OR-gate. Pass-8 fix
   locked in. **PASS.**

9. **Row 56** (`spi.h:103` spi_wait_n). VHDL `spi_master.vhd:177`:
   `o_spi_wait_n <= state_idle OR state_last_d`. C++: always `true`
   (state_idle approximation at byte-level granularity). Audit
   acknowledges this as class-(d) architectural (G137 / V20-DIVMMC-D01).
   Boot path uses byte-level I/O, no DMA wait. **PASS** (as architectural).

10. **Row 97** (`emulator.cpp:4517-4525` port 0xE3 handler). VHDL
    `zxnext.vhd:2608,2412`: `port_e3 <= '1' when port_e3_lsb='1' and
    port_divmmc_io_en='1'`, `port_divmmc_io_en <=
    internal_port_enable(8)`. Per VHDL `:2392` concat, bits 8-15 map to
    NR 0x83 bits 0-7, so bit 8 = NR 0x83 bit 0. C++ emulator gate:
    `effective_internal_port_enable(0x83) & 0x01`. Bit-exact match.
    **PASS.**

(Additional check: Row 98/99 port E7/EB gate. VHDL `:2419`:
`port_spi_io_en <= internal_port_enable(11)`. Bit 11 = NR 0x83 bit 3.
C++ emulator gate: `& 0x08`. Bit-exact match.)

All 10 spot-checks **PASS**. No spurious citations or bit-mismatched
masks found.

## Sub-class catalogue verification

The audit deliberately left 6 candidates un-fixed. Each is verified
independently below. For each: read VHDL/spec, check NextZXOS boot
path involvement, and decide promote-to-finding or confirm-latent.

### 1. CSD v1.0 vs v2.0 for HCS=0 hosts — **CONFIRM LATENT**

Per SD spec § 5.3.3, a CCS=0 card returns CSD v1.0; a CCS=1 (SDHC) card
returns CSD v2.0. JNEXT's `cmd9_send_csd` (sd_card.cpp:825) always
returns CSD v2.0 with C_SIZE in 512KB units.

Audit's V17-DIVMMC-01 latches `host_supports_sdhc_` from ACMD41 arg
bit 30 and uses it on CMD58. CMD9 ignores it — fixed CSD v2.0 emission.

**Boot path check**: TBBlue's MMC_Init (per audit citations and TBBlue
firmware source) always sets HCS=1 in its ACMD41 arg. FatFs's
diskinit.c does the same. NextZXOS's supervisor consumes the CSD only
for capacity readback (C_SIZE), which is layout-compatible with v2.0.
A spec-strict HCS=0 host would observe a v2.0 CSD instead of v1.0 — but
no such host exists in NextZXOS / esxdos / TBBlue. **Confirmed latent.**

### 2. ACMD41 single-step init — **CONFIRM LATENT**

Real SD cards take multiple ACMD41 polls (each returning R1 bit 0 = 1
"in idle state") before transitioning to ready. JNEXT sets
`initialized_=true` on the FIRST call (`acmd41_sd_send_op_cond` at
sd_card.cpp:950-974) — single-step.

**Boot path check**: TBBlue's MMC_Init polls ACMD41 in a `while (R1 &
0x01)` loop, accepting immediate completion. FatFs and esxdos use the
same idiom. **Confirmed latent** — a single-step card is a permitted
(if unusual) timing.

### 3. CMD13 error-bit latching — **CONFIRM LATENT**

Per spec § 7.3.2 R2, byte 1 status latches error bits from prior failed
commands until CMD13 reads (then clears). JNEXT's `cmd13_send_status`
(sd_card.cpp:614-623) emits a fixed status byte = 0x00 ("no errors").

**Boot path check**: TBBlue's MMC_Init only consults CMD13 after a
catastrophic failure (e.g. CMD8 voltage rejection); on the happy path
CMD13 is never issued. NextZXOS's supervisor doesn't issue CMD13.
A forensic test rig that injects a failed write then reads CMD13 would
see 0x00 instead of the latched error — but no boot-path firmware does
this. **Confirmed latent.**

### 4. CMD0 CRC validation — **CONFIRM LATENT**

Real cards validate CMD0's fixed CRC=0x95 and may reject mismatched
input with R1 bit 3 (COM_CRC_ERROR). JNEXT's `cmd0_go_idle` (sd_card.cpp:
523-545) doesn't validate CRC.

**Boot path check**: TBBlue, FatFs, and esxdos all send CMD0 with the
correct CRC byte. Real cards would only reject a malformed host
firmware that omits CRC. **Confirmed latent.**

### 5. ACMD13/22/23/42/51 fallthrough — **CONFIRM LATENT**

Per spec § 4.3.9.5, ACMD13 (SD_STATUS), ACMD22 (SEND_NUM_WR_BLOCKS),
ACMD23 (SET_WR_BLK_ERASE_COUNT), ACMD42 (SET_CLR_CARD_DETECT), ACMD51
(SEND_SCR) are application-specific commands. JNEXT's `process_command`
(sd_card.cpp:453-515) falls through `app_cmd_=true` + non-41 to the
regular CMD switch (Pass-9 design per spec § 4.3.9.1: "if the next
command is not ACMD, the flag is cleared and the command is treated as
a regular command").

**Boot path check**: TBBlue's MMC layer issues only CMD55+ACMD41 (init).
NextZXOS and FatFs never issue ACMD13/22/23/42/51 on the boot path.
A strict-spec host that requested these would observe regular-CMD
responses (e.g. CMD55+ACMD13 -> regular CMD13's status byte, which is
0x00) instead of the proper ACMD13 SD_STATUS register. **Confirmed
latent.**

### 6. `set_button_nmi` not gated on `enabled_` — **CONFIRM LATENT** (contracted)

`DivMmc::set_button_nmi` (divmmc.h:224) is documented as a "low-level
setter that mirrors the VHDL FF state directly. It does NOT model the
i_automap_reset gate".

**Production callers** (verified via grep `set_button_nmi`):
- `emulator.cpp:6012-6013`: `if (nmi_source_.divmmc_button_strobe() &&
  divmmc_.is_enabled()) divmmc_.set_button_nmi(true)` — gated.
- `emulator.cpp:6232-6233`: same gate (`divmmc_.is_enabled()`).

Both production sites gate on `divmmc_.is_enabled()` (composite of
port_io AND nr_0a_4 — exactly the `divmmc_automap_reset='0'` check from
VHDL `zxnext.vhd:4112`). Test fixtures (NM-06 in `divmmc_test.cpp`)
that intentionally bypass the gate to pin specific latch shapes are
explicitly testing the low-level FF behavior, not the gated production
path. **Confirmed latent.** The "contracted" status is correct: any
new test rig MUST gate the call at the call-site, not at the setter,
to preserve the existing 137-row divmmc_test coverage.

## Independent re-audit of cross-cutting families

Each family was independently re-checked against the current code:

- **WO-NR readback on NR $B8-$BB (P19 NIT-01)**: `emulator.cpp:2634-
  2637` set_read_handler for 0xB8..0xBB delegates to
  `divmmc_.entry_points_0() / entry_valid_0() / entry_timing_0() /
  entry_points_1()` (live state, not cached `regs_[reg]`). Reset
  defaults (`divmmc.cpp:43-46`): 0x83 / 0x01 / 0x00 / 0xCD per VHDL
  `:5087-5090`. **CLOSED.**

- **Past-EOF token (P12-P14)**: CMD17 (sd_card.cpp:667-684) emits R1
  PARAMETER_ERROR (0x40) + 0x08 token. CMD18 initial-block (sd_card.cpp:
  715-725) emits R1=0x40 + 0x08. CMD18 mid-stream (sd_card.cpp:386-422)
  emits 0x08 data error token in place of 0xFE. CMD24 (sd_card.cpp:778-
  785) emits R1=0x40 with no data phase. CMD24 host-fstream-fail (sd_card.cpp:
  228-247) emits 0x0D write-error token (V15-DIVMMC-01). **CLOSED.**

- **Full-duplex stream advance (P18)**: `sd_card.cpp:291-311` default
  branch in `receive()` delegates to `send()` so the response stream
  advances and the host observes the correct byte on MISO during a
  write_data mid-response. V18-DIVMMC-NIT-01. **CLOSED.**

- **control_reg bit-masking (P19)**: `divmmc.cpp:122` `control_reg_ =
  (val & 0x8F) | (mapram_ ? 0x40 : 0x00)` — drops bits 6/5:4 from raw
  input and re-folds the OR-latched bit 6. F19-DIVMMC-NIT-01 closure
  verified. **CLOSED.**

- **R1 APP_CMD bit (P20)**: CMD55 (sd_card.cpp:801-816) and ACMD41
  (sd_card.cpp:950-974) both emit R1 with bit 5 set. V20-DIVMMC-01.
  **CLOSED.**

- **XADC family (P19/P20)**: not directly DivMMC/SD/SPI surface;
  out-of-scope but noted as closed in the wider audit aggregate.

- **automap on/off transitions**: `divmmc.cpp:142-162`
  `apply_enabled_transition_` clears automap_hold/held/active +
  button_nmi + retn_pending on enabled->disabled edge. Mirrors VHDL
  `i_automap_reset` clear path (divmmc.vhd:108,126,139). **PASS.**

- **SPI port_io_en gate (P16)**: `emulator.cpp:4433-4438` port 0xE7
  write-only (nullptr read), `:4439-4447` port 0xEB read+write, both
  gated on `effective_internal_port_enable(0x83) & 0x08` (= NR 0x83
  bit 3 = port_spi_io_en). V16-DIVMMC-01 closure verified. **CLOSED.**

## Findings

**ZERO class-(a/b/c) missed findings.** No NITs.

The 3 class-(d) architectural items pre-existing are correctly catalogued
and gated on user authorization (G137 cycle-accurate SPI master FSM,
SD-card NOT Saveable, altrom branch in `sram_divmmc_automap_rom3_en`).

The 6 sub-class candidates are each genuinely latent and correctly
deferred. No promotion required.

## Convergence determination

DivMMC + SD + SPI subsystem is **OFFICIALLY CONVERGED** per
`feedback_task2_converged_subsystem_skip.md`. Pass-22 and later MUST
SKIP this subsystem unless a new finding in another subsystem
appends a new surface to DivMMC/SD/SPI.

This is the first DivMMC zero-finding pass since the enumeration-table
mandate (Pass-19), passing both the row-count + spot-check thoroughness
gate AND the convergence gate. Trajectory across recent passes:
- Pass-17: 1 finding (V17-DIVMMC-01 SDHC HCS)
- Pass-18: 1 NIT (V18-DIVMMC-NIT-01 full-duplex advance)
- Pass-19: 1 NIT (V19-DIVMMC-NIT-01 control_reg bits 5:4 + NR 0xB8-0xBB
  readback)
- Pass-20: 1 finding (V20-DIVMMC-01 R1 APP_CMD bit)
- Pass-21: **0 findings** ← convergence

## Reviewer note

The audit's documentation quality is also high: every row cites a
specific C++ line range and a specific VHDL line range, with a one-line
match status and a notes column that records past fix history (Pass-N
verify-audit fix). The "Findings" section and the architectural
class-(d) catalogue are unambiguous, and the deliberately-not-promoted
catalogue is thorough enough that a reviewer can verify each item in
a few minutes. No reviewer-side improvements suggested.
