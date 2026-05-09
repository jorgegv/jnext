# Pass-7 Blind Verification Re-Audit — DivMMC + SD + SPI

**Branch**: `task2/verify7-divmmc-sd-spi`
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify7-divmmc-sd-spi`
**Date**: 2026-05-09
**Auditor**: Pass-7 (independent of Pass-1..6)

## Verdict

**1 class-(a) bug found and fixed**:
* `SpiMaster::reset()` clearing `devices_[]` array (unhooking attached
  SD card on every reset).

**Net trend**:
* Pass-5 → 2 class-(a)
* Pass-6 → 1 class-(a)
* Pass-7 → 1 class-(a)

Audit has not yet converged — but the remaining find is a latent /
defensive-correctness bug, not a firmware-observable boot defect. The
production hot path (`Emulator::init()` re-attaches after reset) was
masking it. The boot-trajectory-impacting bug surface for these three
subsystems appears to be exhausted; the remaining class-(b/c) items
(see findings table) are spec-deviation curiosities not exercised by
TBBlue or NextZXOS firmware.

## Methodology — five angles

### Angle 1 — Final cross-reset matrix

Built a complete table for every state field of DivMmc / SdCardDevice /
SpiMaster across reset paths (cold construction, hard reset, soft
reset, manual unmount/mount, runtime SD swap). All fields verified
against VHDL `device/divmmc.vhd:105-150`, `serial/spi_master.vhd:40-179`
and the SD physical layer spec.

Findings:
* DivMmc: `port_io_enable_ / nr_0a_4_enable_ / enabled_` correctly
  preserved across soft reset (VHDL signals are NextREG-resident, not
  in DivMMC reset domain).
* DivMmc: `automap_active_ / hold_ / held_ / button_nmi_ /
  layer2_map_read_ / retn_pending_clear_` cleared by `reset()` —
  matches VHDL `i_reset` clauses on lines 108, 126, 139.
* DivMmc: `entry_points_0_ / valid_0_ / timing_0_ / entry_points_1_`
  reset to soft defaults (0x83 / 0x01 / 0x00 / 0xCD) — matches NR 0xB8-
  0xBB power-on values.
* SdCardDevice: `initialized_` correctly preserved across soft reset
  (Emulator::reset gates `sd_card_.reset()` on `!preserve_memory`).
  External SPI peripheral is not in the FPGA reset domain.
* SpiMaster: `cs_ / rx_data_ / sd_swap_` handled correctly. `devices_`
  cleared incorrectly — **class-(a) bug — fixed**.

### Angle 2 — Signal coherence audit per Pass-6 fix shape

Pass-6 added a post-reset sync of NR 0x83 bits 0:1 into DivMmc /
Multiface (because NextReg::reset() reloads `regs_[0x83]` to 0xFF but
does not fire registered write_handlers). I scanned for other NRs with
the same shape:

* NR 0x0A: bits 4 (divmmc_automap_en), 5 (sd_swap), 7:6 (mf_type), 3
  (mouse_button_reverse), 1:0 (mouse_dpi). VHDL declares all as
  signal-init (line 1124-1128, e.g. `:= '0'`) but **no `i_reset`
  clauses** anywhere in zxnext.vhd. They survive both hard and soft
  reset; only power-on init sets them. Corresponding C++ subsystem
  fields are constructor-initialized to matching defaults, so cold
  construction is correct, and there is no propagation issue. **No
  Pass-6-class fix needed for NR 0x0A.**
* NR 0x82, 0x84, 0x85: bit positions are mostly consulted via
  `cached(0x83)` at port-decode time (e.g. `port_dffd_io_en` at NR 0x83
  bit 2), not stored in subsystems. Cached read covers reset
  propagation correctly.
* NR 0x86-0x89: bus-port enables. Same pattern as 0x82-0x85 (cached
  read at port-decode).

No additional Pass-6-class write_handler-skip-on-reset bugs found.

### Angle 3 — Test-suite-vs-VHDL coverage for `device/divmmc.vhd`

Walked every input/output port of `divmmc.vhd` and confirmed:

| VHDL signal | C++ surface | Test coverage |
|---|---|---|
| `i_cpu_a_15_13` | implicit in `read/write/check_automap` `addr` arg | E3-*, EP-*, TM-* |
| `i_cpu_mreq_n` / `i_cpu_m1_n` | `is_m1` arg | TM-04 (is_m1=false), EP-12 |
| `i_en` (= port_divmmc_io_en) | `port_io_enable_` | NA-01..03, R3-04 |
| `i_automap_reset` | composite of NA gates | NA-01..03, DA-08 |
| `i_automap_active` | `sram_pre_override_2` arg | CM-01..09 |
| `i_automap_rom3_active` | `sram_pre_override_*` + `rom3_active_` | R3-01..04 |
| `i_retn_seen` | `on_retn()` / `on_m1_retn_delay()` | DA-06, NM-06, RETN-PROPER-* |
| `i_divmmc_button` | `set_button_nmi()` | NM-01..08 |
| `i_divmmc_reg` | `write_control()` | E3-* |
| `i_automap_*_on` (7 signals) | NR 0xB8-0xBB | EP-*, NR-* |
| `o_divmmc_rom_en` | `is_rom_mapped()` | E3-*, NA-* |
| `o_divmmc_ram_en` | `is_ram_mapped()` | E3-*, MEM-* |
| `o_divmmc_rdonly` | `is_read_only()` | MEM-* |
| `o_divmmc_ram_bank` | `bank_` (via `read/write`) | E3-*, MEM-* |
| `o_disable_nmi` | `is_nmi_hold()` | NM-08 |
| `o_automap_held` | `automap_held()` | TM-* |

**No uncovered signals** — every output and input gate has at least one
test row. (The cycle-accurate timing of `automap_hold/held` two-stage
pipeline is approximated as "single check_automap call per M1" — see
TM-* — which is the documented per-M1 collapse model.)

### Angle 4 — Behavioural fingerprint per tbblue.fw boot step

For each TBBLUE.FW MMC_Init step I traced the byte stream through
SpiMaster + SdCardDevice + verified against VHDL spi_master.vhd
pipeline semantics:

| Step | Host TX | C++ RX | VHDL RX | Match |
|---|---|---|---|---|
| CMD0 | `40 00 00 00 00 95` | `FF 01` (NCR+R1=idle) | same | OK |
| CMD8 | `48 00 00 01 AA 87` | `FF 01 00 00 01 AA` (R7) | same | OK |
| CMD55 | `77 00 00 00 00 65` | `FF 01` (NCR+R1=idle) | same | OK |
| ACMD41 | `69 40 00 00 00 77` | `FF 00` (NCR+R1=ready) | same | OK |
| CMD58 | `7A 00 00 00 00 FD` | `FF 00 C0 FF 80 00` (R3 SDHC) | same | OK |
| CMD17 (MBR sector 0) | `51 00 00 00 00 ...` | `FF 00 FE <512> 00 00` | same | OK |
| CMD17 (BPB sector N) | `51 00 00 00 N ...` | `FF 00 FE <512> 00 00` | same | OK |

All firmware-relevant byte streams match. No behavioural divergence
found.

### Angle 5 — "Boring tests" — line-by-line read of all 1481 lines

Read every function in divmmc.cpp / sd_card.cpp / spi.cpp. Findings:

* `DivMmc::write_control` — OR-latch on bit 6 (mapram) per VHDL :4182-
  4183. Correct.
* `DivMmc::read_control` — masks bits 5:4 to zero per VHDL :4190.
  Correct.
* `DivMmc::clear_mapram` — clears bit 6 per VHDL :4184-4185. Correct.
* `DivMmc::check_automap` — full pipeline modelled including
  `prev_held → button_nmi_` clear (VHDL :112-113), main vs ROM3 path
  decomposition (zxnext.vhd:3137-3138 with layer2_map and altrom
  factors), $3Dxx wildcard (zxnext.vhd:2898-2899). Correct.
* `DivMmc::on_retn` and `on_m1_retn_delay` — both clear paths covered.
  G46(a) one-M1-cycle delay register is properly modelled. Correct.
* `DivMmc::is_active/is_rom_mapped/is_ram_mapped/is_read_only/read/
  write` — gating chain matches VHDL :94-101. Correct.
* `DivMmc::save_state/load_state` — schema is append-only with
  documented compat caveats. Correct.
* `SdCardDevice::receive` — IDLE → RECEIVING_CMD → process_command
  pipeline. New-CMD-during-RESPONDING handling resets multi_block_ /
  pending_write_after_r1_ defensively. Correct.
* `SdCardDevice::send` — RESPONDING / SENDING_DATA / WRITE_RESP /
  RECEIVING_DATA / IDLE branches all correct. Multi-block re-prime
  emits 0xFF inter-block filler before next 0xFE token. Correct.
* `SdCardDevice::cmd0_go_idle` through `cmd58_read_ocr` — all SD-spec
  responses correct (R1 idle bit, R3 OCR with CCS, R7 voltage echo).
  Minor non-spec details (CMD12 omits busy phase; CMD55+nonACMD
  returns illegal R1) documented as class-(c) — see Findings below.
* `SpiMaster::reset` — `devices_.fill(nullptr)` is wrong (class-(a) —
  fixed).
* `SpiMaster::write_data / read_data` — VHDL pipeline semantics
  modelled correctly including no-active-device → MISO=0xFF
  (zxnext.vhd:3278-3280 default-else clause). Correct after Pass-3.

## Findings

### Class-(a) — fixed

**SPI-RESET-DEVICES**: `SpiMaster::reset()` cleared the attached-device
array, unhooking the SD card on every reset. VHDL spi_ss_*_n outputs
are wired permanently; only `port_e7_reg` FF state is in the reset
domain (zxnext.vhd:3308-3322). The bug was masked in production by
`Emulator::init()` re-attaching after reset, but it would silently
disconnect the SPI bus for any future caller (save-state restore,
runtime SD swap, future test fixture) that called `spi_.reset()`
without re-attaching.

* **File**: `src/peripheral/spi.cpp:26-33`
* **VHDL ref**: `zxnext.vhd:3300-3332` (no reset clears connectivity)
* **Test**: new row `SS-12` in `test/divmmc/divmmc_test.cpp` —
  discriminative (would fail with pre-fix code: write_data with
  null device sets rx_data=0xFF, the device's 0x42 response never
  surfaces).

### Class-(b) — defensive observations, not fixed

None this pass.

### Class-(c) — spec-deviation curiosities, not fixed

* **CMD12 busy-phase elision**: The C++ `cmd12_stop_transmission`
  emits 8 stuff bytes + R1 + persistent 0xFF (idle). SD spec § 7.3.3.3
  R1b says the card should drive MISO low (busy = 0x00) until
  programming completes, then release to 0xFF. Instant-idle is fine
  for simulation (busy time is trivially short) and the project
  comment at sd_card.cpp:418-426 documents the deliberate non-busy
  return so the supervisor's `IN A,($EB); AND A; JR Z,...` loop at
  bank-2 $1972 doesn't see infinite busy. **Class-(c) — deliberate.**

* **CMD55 + non-ACMD returns illegal**: After CMD55, the C++ rejects
  any command other than CMD41 with R1=0x05 (idle + illegal). Per SD
  spec § 4.3.9 the card *should* fall back to processing the command
  as a regular CMD if it isn't a valid ACMD. TBBlue / NextZXOS only
  ever sends CMD55 + ACMD41, so this never fires in practice.
  **Class-(c) — latent, no firmware impact.**

* **CMD12 outside CMD18 stream**: The C++ accepts CMD12 unconditionally;
  the SD spec says CMD12 outside a multi-block transfer should return
  illegal command. Same impact assessment — never fires in firmware
  flows. **Class-(c) — latent, no firmware impact.**

* **SPI Flash CS gating** (`SS-08` already documented as WONT): VHDL
  zxnext.vhd:3319 gates 0x7F → flash select on `nr_03_config_mode='1'
  OR nr_02_reset_type(2)='1'`. The C++ comment at spi.cpp:73-77
  documents the deliberate omission since jnext has no Flash backend.
  **Class-(c) — out of scope.**

## Build + tests

```text
build:   cmake --build build -j$(nproc)  → 100% built
ctest -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80|spi':
  fuse_z80_tests          PASS
  divmmc_tests            PASS  (incl. new SS-12)
  sdcard_tests            PASS
  sd_rom_extractor_tests  PASS
ctest (full):             37/37 PASS
```

## Honest convergence verdict

**The DivMMC + SD + SPI subsystem audit has reached the "diminishing
returns" frontier.** Pass-5/6/7 trend: 2 → 1 → 1 class-(a) per pass,
with each pass requiring deeper or more lateral angles to surface the
next bug. Pass-7's find is a latent / defensive bug rather than a hot
boot-path defect — pass-6 fixed the last firmware-observable issue
(NR 0x83 reset propagation).

**The remaining open items are class-(c) spec deviations** that are
deliberately non-spec-compliant (CMD12 busy-elision is documented as
intentional; CMD55+nonACMD and CMD12-outside-stream never fire in
TBBlue/NextZXOS firmware).

**Convergence direction signal**: pass-7 found exactly 1 class-(a),
matching the pass-6 yield. The bug shape (latent, masked-by-init-order,
not boot-path-observable) suggests the audit is approaching a steady
"defensive correctness" baseline rather than continuing to surface
boot-defect-class bugs. Recommend one more pass (pass-8) to confirm
zero or near-zero class-(a) yield, then declare convergence.

**Branch state**: `task2/verify7-divmmc-sd-spi`
**Tests**: 37/37 pass (PASS-7 added SS-12)
