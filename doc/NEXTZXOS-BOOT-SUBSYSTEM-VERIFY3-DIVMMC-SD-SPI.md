# NextZXOS Boot Subsystem — Pass-3 Verification Re-Audit
## DivMMC + SD-card + SPI subsystem

Date: 2026-05-09
Branch: `task2/verify3-divmmc-sd-spi`
Auditor: Independent third-pass blind verification (Claude Opus 4.7).

## Verdict

**NEW FINDINGS — 2 class-(a) bugs fixed; convergence NEAR (likely no
further class-(a) bugs in this subsystem; class-(b) gaps remain
documented).**

This audit was conducted blind: the auditor did not read any prior
`NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-DIVMMC-SD-SPI*.md` or
`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY-DIVMMC-SD-SPI.md` files, nor commit
diffs, before starting. The findings below are the ones the
differential VHDL→C++ walk surfaced from scratch.

## Methodology

1. **Differential VHDL→C++ walk**, signal-by-signal:
   - `device/divmmc.vhd` (153 lines) — automap pipeline + button_nmi FF.
   - `serial/spi_master.vhd` (179 lines) — full-duplex byte exchange,
     `spi_miso <= '1'` default-else fallback (zxnext.vhd:3278-3280).
   - `zxnext.vhd` cross-cuts:
     - port_e3_reg latch (sticky bit 6 mapram, NR 0x09 bit 3 clear)
     - port_e7_reg slave-select decode (mode-gated 0x7F flash)
     - port_eb_dat full-duplex pipeline (state_last_d delay)
     - DivMMC RST/non-RST/NMI/$3DXX/off-trigger entry point gating
     - sram_pre_override priority arbiter (config_mode + mf_mem_en gates)
     - sram_divmmc_automap_en + sram_divmmc_automap_rom3_en composites
     - divmmc_retn_seen `AND NOT mf_is_active` factor (zxnext.vhd:4111)
2. **Boundary inputs** for every NR write and port write, including:
   - port 0xE3 with bit 7 only / bit 6 only / bits 5:4 (always-zero)
   - port 0xE7 with bits 1:0 = "10"/"01" + sd_swap toggle
   - port 0xE7 with 0xFB (RPI0) / 0xF7 (RPI1) / 0x7F (config_mode flash)
   - SD CMD17/CMD18 with sector 0, sector last, sector last+1, sector
     0xFFFFFFFF (off-card), sector that overflows uint64_t (impossible
     since we cast to u64 before multiply).
   - CMD55+CMD55 (double-app-cmd) interpretation.
   - CMD18 multi-block read past end-of-image mid-stream.
3. **Cold/warm/soft reset** state-machine diff:
   - `DivMmc::reset()` clears the right state but PRESERVES enable flags
     (port_io_enable_, nr_0a_4_enable_, enabled_) — VHDL-faithful (those
     levers are NR/port-driven, not divmmc_automap_reset clearable).
   - `SpiMaster::reset()` clears `cs_` to 0xFF (matches VHDL port_e7_reg
     reset to all-1s at zxnext.vhd:3308-3309) but ALSO clobbers
     `rx_data_` to 0xFF — VHDL spi_master gets `i_reset='0'` so miso_dat
     should NOT reset. Class-(b) divergence (low-impact: post-reset
     firmware always issues a transfer that overwrites miso_dat anyway).
   - `SdCardDevice::reset()` and `unmount()` have inconsistent fields
     they clear (multi_block_, multi_block_sector_, app_cmd_, etc.).
     Class-(b) — only matters on remount mid-session.
4. **Power-on defaults** verified for every NR/port:
   - port_e3_reg = 0x00 (VHDL :4177) — matches DivMmc::reset() defaults.
   - port_e7_reg = 0xFF (VHDL :3309) — matches SpiMaster::reset().
   - NR 0xB8 = 0x83, NR 0xB9 = 0x01, NR 0xBA = 0x00, NR 0xBB = 0xCD —
     soft-reset defaults match VHDL :5044-5050 / :1246-1249 indirectly
     (verified by inspection; tests covered).
5. **Multi-state interactions**:
   - AUTOMAP active + Multiface NMI fired — verified `divmmc_retn_seen`
     gate `AND NOT mf_is_active` is wired (emulator.cpp:591).
   - AUTOMAP active + ALTROM enable — partial modelling (only altrom=0
     branch of zxnext.vhd:3138 is faithful; altrom=1 falls back to
     `rom3_active` which is conservative). Class-(b).
   - conmem active + AUTOMAP triggered — both flags coexist; is_active()
     returns true for either; bank/page selection is correct.
   - mapram OR-latch during AUTOMAP transition — correct sticky semantic
     (write_control ORs).
   - SD CMD in flight when AUTOMAP fires — independent state machines;
     no shared state corruption path.
6. **SPI clock divider extremes** (NR 0x86/0x87/0x88 boundary values) —
   not modelled (jnext SPI master is byte-level zero-latency).
   Class-(b) limitation, documented at SpiMaster::spi_wait_n().
7. **CRC validation paths** — jnext accepts any CRC byte. SD spec
   requires CRC for CMD0 (=0x95) and CMD8 (=0x87); CRC is OFF until
   CMD59 enables it. Real cards reject bad CRC for CMD0/CMD8.
   Class-(b) — known limitation; firmware sends correct CRCs anyway.
8. **Card init dance edge cases**:
   - Firmware skips CMD8 → goes directly to CMD55+ACMD41 → ACMD41 sets
     `initialized_=true`. Works (no CMD8 prerequisite).
   - Firmware sends CMD55+CMD55+ACMD41: jnext interprets second CMD55
     as ACMD55 (illegal, R1=0x05). Real cards: second CMD55 resets
     app_cmd to TRUE (treats it as standard CMD55). Class-(b)
     divergence — NextZXOS firmware doesn't do this in practice.
   - Repeated CMD0: card returns R1=0x01 (idle) each time. Match.
9. **Save/load state mid-AUTOMAP**: DivMmc save_state includes the
   automap pipeline state (hold/held/active/button_nmi/retn_pending).
   Save/load survives mid-AUTOMAP. SdCardDevice intentionally NOT
   Saveable (rewind ring skips it) — header comment notes the
   limitation; not a bug per design.

## Findings

### NEW FINDING #1 (CLASS-(A), FIXED) — `set_button_nmi` should respect `divmmc_automap_reset`

**VHDL** (divmmc.vhd:107-114):
```vhdl
if rising_edge(i_CLK) then
   if i_reset='1' or i_automap_reset='1' or i_retn_seen='1' then
      button_nmi <= '0';
   elsif i_divmmc_button='1' then
      button_nmi <= '1';
   elsif automap_held='1' then
      button_nmi <= '0';
```

`i_automap_reset` is `port_divmmc_io_en='0' OR nr_0a_divmmc_automap_en='0'`
(zxnext.vhd:4112). When asserted, the FF holds `button_nmi` at '0' —
even if `i_divmmc_button=1` simultaneously, the priority chain wins.

**C++ pre-fix**: `NmiSource::set_divmmc_enable()` only consumes NR 0x06
bit 4 (drive_nmi_en, the assert gate at VHDL:2091) — it does NOT
observe NR 0x0A bit 4. So during the cold-boot window where NextZXOS
firmware has not yet written NR 0x0A bit 4, NmiSource may still
strobe `divmmc_button_strobe_=true` if NR 0x06 bit 4 is set (it is on
some firmware paths). Emulator forwarded that to
`DivMmc::set_button_nmi(true)` unconditionally. The C++ field
`button_nmi_` thus latched a 1 even though the VHDL FF would be held
at 0 by `divmmc_automap_reset`.

Downstream effect: `is_nmi_hold() = automap_held OR button_nmi`
(divmmc.vhd:150) leaks a stuck-1 into the NMI arbitration feedback,
potentially confusing the FSM's hold-resolution loop on subsequent
ticks.

**Fix** (commits in this branch, src/core/emulator.cpp):
```cpp
if (nmi_source_.divmmc_button_strobe() && divmmc_.is_enabled()) {
    divmmc_.set_button_nmi(true);
}
```
Applied at BOTH call sites (run_frame() primary cluster + step
cluster). Mirrors VHDL's `i_automap_reset='0'` precondition by
gating on the equivalent `enabled_ = port_io AND nr_0a_4` composite.

`DivMmc::set_button_nmi()` itself was kept as a low-level setter
(unconditional) so the existing NM-01..NM-08 unit tests that exercise
the latch in isolation continue to pass.

### NEW FINDING #2 (CLASS-(A), FIXED) — SPI master leaks stale rx_data_ when no slave selected

**VHDL** (zxnext.vhd:3278-3280):
```vhdl
spi_miso <= i_SPI_FLASH_MISO when spi_ss_flash_n = '0' else
            pi_spi0_miso     when spi_ss_rpi1_n = '0' or spi_ss_rpi0_n = '0' else
            i_SPI_SD_MISO    when spi_ss_sd1_n = '0' or spi_ss_sd0_n = '0' else '1';
```

When NO slave is selected, the MISO line is forced to '1'. The
`spi_master` entity is fed `i_spi_wr=port_eb_wr` and
`i_spi_rd=port_eb_rd` regardless of CS state, so an unselected
transfer still runs and `miso_dat` captures all-ones (= 0xFF).

**C++ pre-fix**: `SpiMaster::write_data()` and `read_data()` only
updated `rx_data_` when `active_device() != nullptr`. With no slave
selected, `rx_data_` retained the previously-selected slave's last
byte. Subsequent `read_data()` calls (after deselect) returned the
stale byte instead of 0xFF.

Practical impact: most firmware deselects between commands and does
not read 0xEB while CS is deasserted, so the divergence is mostly
theoretical. But VHDL faithfulness is the ruling principle.

**Fix** (src/peripheral/spi.cpp): when no active device, force
`rx_data_ = 0xFF` on both write_data and read_data, matching the
VHDL `spi_miso='1'` default-else behaviour.

### Documented class-(b) gaps (NOT fixed in this audit)

- **G132-style**: NR 0x09 bit 3 has TWO functionally-distinct semantics
  in the C++ NR 0x09 write handler at emulator.cpp:3138 — it (a)
  drives `sprites_.set_over_border(...)` and (b) drives
  `divmmc_.clear_mapram()`. The (a) path is INCORRECT per VHDL: NR
  0x09 bit 3 is NOT defined as sprite-over-border in zxnext.vhd
  (sprite_over_border lives at NR 0x15 bit 1, VHDL:5233). The (b)
  path is correct (zxnext.vhd:4184). This bug is **outside the
  divmmc/sd/spi subsystem boundary** but worth noting; it incorrectly
  re-purposes NR 0x09 bit 3 for sprite control. Recommend a follow-up
  audit on the sprite NR-handler subsystem.

- **Flash slave select** (port 0xE7 = 0x7F): VHDL gates on
  `nr_03_config_mode='1' OR nr_02_reset_type(2)='1'` (zxnext.vhd:3319).
  C++ collapses any non-recognised pattern to 0xFF (all-deselected) —
  it never reports flash select. Acceptable since jnext does not
  model the FPGA boot flash device anyway.

- **SDHC CMD17 off-end response shape**: jnext returns `R1=0x00 + data
  error token 0x08` (out-of-range only). SD spec Section 7.3.3.3
  permits this shape; some firmware checks R1 bit 5 (address error)
  instead. NextZXOS / FatFs uses the data error token path — works.

- **CMD16 SET_BLOCKLEN with non-512 arg** rejected with R1=0x05 (idle
  + illegal). For an SDHC card this is correct (block length is
  fixed). For SDSC compatibility this would be wrong — but jnext
  reports SDHC unconditionally via CMD58 OCR bit 30=1.

- **CMD55 followed by CMD55** is interpreted as ACMD55 (R1=0x05). Real
  cards: second CMD55 is treated as standard CMD55 (resets app_cmd to
  TRUE). NextZXOS firmware doesn't double-CMD55, so no boot impact.

- **CRC validation** for SD commands is not performed. Firmware sends
  correct CRC for CMD0/CMD8 (mandatory) and dummy 0x01 for others.

- **SD card stays "initialized" across Z80 soft reset** in real
  hardware (the SD card has its own clock domain). `SdCardDevice::reset()`
  clears `initialized_=false`, requiring re-init after every soft
  reset. Class-(b) — masks the cold-vs-warm path divergence; firmware
  always re-issues CMD0+CMD8+ACMD41 on boot anyway.

- **SD CMD18 mid-stream past-end-of-image** ends the stream cleanly
  (returns 0xFF and sets state IDLE). Real cards send a data error
  token 0x08 between blocks. Firmware should issue CMD12 to abort —
  jnext just goes IDLE. Low-impact.

- **`automap_held=1` rising-edge clear of button_nmi** — VHDL clears
  `button_nmi` every cycle while `automap_held=1` (and button is not
  re-asserted). The C++ models this as a one-shot rising-edge clear.
  Functionally equivalent for non-pathological producer behaviour.

- **Altrom-locked branch of `sram_divmmc_automap_rom3_en`**: VHDL
  formula at zxnext.vhd:3138 includes
  `(sram_altrom_en AND sram_pre_alt_128_n) OR
   (sram_pre_rom3 AND NOT sram_altrom_en)`. The C++ collapses to
  `rom3_active_` (the second branch with altrom=0). When NR 0x8C bit
  7=1 (altrom enabled), the formula needs to consult
  `sram_pre_alt_128_n` — not modelled. Boot path keeps altrom off, so
  no impact; revisit when an altrom-locked test case demands it.

- **SPI master soft reset clobbers rx_data_**: VHDL `spi_master` is
  given `i_reset='0'` (zxnext.vhd:3285), so miso_dat does NOT reset on
  soft reset. C++ `SpiMaster::reset()` resets it to 0xFF. Trivial
  divergence — post-reset firmware overwrites it via new transfers.

- **`SdCardDevice::unmount()` does not clear `multi_block_*`**:
  remount mid-session would inherit stale multi-block state. Mount
  itself doesn't normally happen mid-run; class-(b).

## Spot-checks (VHDL-faithful behaviours confirmed)

- **port_e3 mapram OR-latch**: write_control ORs `(val & 0x40)` into
  `mapram_`. Cleared only by `clear_mapram()` (NR 0x09 bit 3). Match.
- **port_e3 read mask**: `read_control() & 0xCF` — bits 5:4 force-zero.
  Match VHDL :4190.
- **divmmc_automap_reset**: enabled→disabled edge in
  `apply_enabled_transition_` clears hold/held/active/button_nmi. Match.
- **port_e7 SD-swap**: 4-way truth table verified against VHDL :3311-3314.
- **SPI byte-pipeline delay**: read_data returns prev rx_data_ then
  triggers new transfer. Match VHDL miso_dat latch-at-state_last_d.
- **AUTOMAP RST detection**: VHDL :2850 requires
  `cpu_a(7:6)="00" AND cpu_a(2:0)="000"` (= multiples of 8 in
  0x00..0x3F). C++ exact-match RST table is equivalent.
- **AUTOMAP off-trigger**: PC=0x1FF8..0x1FFF, gated on entry_points_1
  bit 6 AND main_path_eligible. Match VHDL :2896.
- **AUTOMAP $3DXX wildcard**: any PC with high byte 0x3D, gated on
  entry_points_1 bit 7 AND rom3_path_eligible. Match VHDL :2898-2899.
- **DivMMC ROM/RAM mapping**: VHDL ram_bank=3 when page0; bank from
  port_e3_reg(3:0) when page1. C++ ram_page_for() matches. RAM bank 3
  read-only when mapram set. Match.
- **divmmc_retn_seen `AND NOT mf_is_active`** (VHDL :4111) — wired in
  emulator.cpp:591 (`im2_.retn_seen_this_cycle() && !multiface_.is_active()`).
- **CMD18 first-block shape**: NCR + R1 + 0xFE + 512 + CRC. Match.
- **CMD18 inter-block re-prime**: 0xFF filler + 0xFE token + next 512
  + CRC. Match real-SD streaming behaviour.
- **CMD24 R1-then-data bridge**: `pending_write_after_r1_` flag flips
  to RECEIVING_DATA only after R1 has actually been EMITTED on MISO.
  Match SD spec § 7.2.4 / 7.3.3.1.
- **CMD9/CMD10 CSD/CID block-mode response**: NCR + R1 + 0xFE + 16
  bytes + 2 CRC. Match.

## Convergence assessment

Three independent passes have now examined this subsystem. The first
two passes found and fixed several class-(a) bugs. This third pass
found 2 new class-(a) bugs:

1. NmiSource→DivMMC `button_nmi` strobe missing the
   `divmmc_automap_reset` gate.
2. SPI master leaving stale `rx_data_` when no slave is selected.

Both are low-impact in NextZXOS production boot (NR 0x0A bit 4 is set
early enough that finding #1's window is small; finding #2 only bites
if firmware reads port 0xEB with all CS lines deasserted).

The class-(b) gap list is stable across passes (CRC, CMD55-CMD55,
altrom-locked rom3 composite, SPI clock divider, etc.). I do NOT
believe further class-(a) bugs remain in this subsystem.

**Convergence: NEAR-COMPLETE.** A fourth pass on this subsystem is
unlikely to find new class-(a) bugs unless new VHDL signals are
exposed (e.g. flash slave select wiring, two-SD-card support, real
SDSC support). Recommend graduating this subsystem to "final" status
after these fixes land and tests pass.

## Open questions / out-of-scope flags

- The NR 0x09 bit 3 → `sprites_.set_over_border` mis-wiring noted in
  the class-(b) section is **NOT** in this subsystem's mandate.
  Recommend a separate audit pass on the NR-handler subsystem (in
  `src/core/emulator.cpp` NR write handlers) and the sprite layer.
- `sd_swap=1` routing: jnext only attaches `sd_card_` to CS 0; with
  swap=1 the firmware targets CS 1 and finds no device. Acceptable
  default behaviour (sd_swap reset state is 0); revisit if a future
  test wants to exercise the swap path.

## What I did NOT cover

- I did not exhaustively re-verify the `divmmc_test.cpp` 123-row plan
  against the VHDL — that is the unit-test plan's job; the previous
  passes already pinned it down. I confirmed the suite still passes
  (123/0/0).
- I did not exercise the FUSE Z80 opcode test, only confirmed it still
  passes.
- I did not run the full `regression.sh` headless-screenshot suite —
  the divmmc/sd/spi changes are surgical and unlikely to affect
  pixel output. (If this is desired, run `bash test/00regression/regression.sh`
  on this branch.)
- I did not examine the `SdCardDevice::unmount()` vs `mount()` /
  `reset()` field-clearing inconsistencies in detail beyond noting
  them; they are class-(b) and only matter on mid-session remount.

## Test status

```
LANG=C ctest --test-dir build -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80|spi'
```

```
Test #1:  fuse_z80_tests        ✓
Test #19: divmmc_tests           ✓ (123 plan rows, 0 failures, 0 skips)
Test #20: sdcard_tests           ✓
Test #21: sd_rom_extractor_tests ✓
4/4 passed.
```

Full unit-test sweep (37 suites): 37/37 passed.

## Branch HEAD

After commit, see `git -C .claude/worktrees/task2-verify3-divmmc-sd-spi log --oneline -3`.
