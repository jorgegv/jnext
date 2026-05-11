# NextZXOS Boot — DivMMC + SD-card + SPI Subsystem Code Review

**Branch**: `task2/divmmc-sd-spi-review`
**VHDL oracle**: `device/divmmc.vhd`, `serial/spi_master.vhd`, `zxnext.vhd`
**Files reviewed**:
- `src/peripheral/divmmc.{cpp,h}` (580+312 LOC)
- `src/peripheral/sd_card.{cpp,h}` (672+171 LOC)
- `src/peripheral/spi.{cpp,h}` (155+104 LOC)
- Cross-cutting (read-only): `src/core/emulator.cpp` port-decode + `src/core/sd_rom_extractor.{h,cpp}`

## Executive summary

Three discrepancies identified vs the VHDL oracle, all classified as **clear bugs, fixed**. The DivMMC automap engine was missing two trigger paths from NR `$BB`, and the SPI ports had no `port_spi_io_en` gate. Two of the three bugs are silent under the default boot trajectory of NextZXOS (default NR `$83=$FF` + no Plus3DOS RAM-disk fetches), but the `$3DXX` + ROM3-mode wildcard trap is enabled by default and would mis-trigger if a future supervisor fetched code from page `$3D` while ROM3 was selected.

The SD-card + SPI byte-level pipeline is VHDL-faithful at byte granularity. The two non-VHDL deliberate compatibility hacks (`persistent_response_byte_` for ZEsarUX-style sustained CMD8/CMD0/CMD12 responses; explicit CSD/CID synthesis for CMD9/CMD10) are documented in-code and required for tbblue.fw to clear MMC init.

After fixes, all 36 unit tests pass and all 33 regression tests pass.

## Methodology

1. Walked the VHDL `divmmc.vhd` entity (153 lines, complete) plus the `zxnext.vhd` decoder slice that produces `divmmc_automap_*_on`, `port_e3_*`, `port_e7_*`, `port_eb_*`, and `divmmc_automap_reset` (lines 2412–4190).
2. Cross-referenced the VHDL `spi_master.vhd` (179 lines, complete) against `SpiMaster` byte-level behaviour.
3. Walked `divmmc.cpp` / `sd_card.cpp` / `spi.cpp` line-by-line against the cited VHDL.
4. Verified port dispatch in `emulator.cpp` (gates, mask/match patterns).
5. Cross-checked findings against the G46(b) investigation log (`doc/issues/nextzxos-boot/G46B-INVESTIGATION-LIVE.md` etc.).

## Findings (classified)

### Bug A — NR `$BB` bit 0 (`automap_nmi_delayed_on` at PC=`$0066`) was NOT decoded

**VHDL**: `zxnext.vhd:2908`
```
divmmc_automap_nmi_delayed_on <= port_00xx_msb and port_66_lsb and nr_bb_divmmc_ep_1(0);
```
combined with `divmmc.vhd:121,131` — both `nmi_instant_on` (bit 1) AND `nmi_delayed_on` (bit 0) feed `automap_hold` independently.

**jnext (pre-fix)**: `divmmc.cpp:357` only checked NR `$BB` bit 1 (instant) at PC=`$0066`. Bit 0 (delayed) was dropped.

**Severity**: silent under default NR `$BB=$CD` (bits 0 AND 1 both set: instant fires and the delayed bit's effect is swallowed). Surfaces only if the firmware/host configures `$BB=$01` (= delayed-only NMI). Default is NOT this configuration, so NextZXOS boot is unaffected. But this is still a VHDL divergence and a failure case for any test plan that enumerates the `$BB` bits.

**Fix**: split the `$0066` clause to fire `instant_match` on bit 1 AND `delayed_match` on bit 0 independently. Both gated on `button_nmi_` per `divmmc.vhd:120-121`.

### Bug B — NR `$BB` bit 7 (`$3DXX` ROM3 instant_on wildcard) was NOT decoded

**VHDL**: `zxnext.vhd:2898-2899`
```
divmmc_automap_rom3_instant_on <= (divmmc_rst_ep and (not divmmc_rst_ep_valid) and divmmc_rst_ep_timing) or
                                  (port_3dxx_msb and nr_bb_divmmc_ep_1(7));
```
where `port_3dxx_msb = '1' when cpu_a(15:8) = X"3D"` (line 2499). So **any** PC fetch with high byte `$3D` fires `rom3_instant_on` when bit 7 is set AND ROM3 is selected (gated by `sram_divmmc_automap_rom3_en` at line 3138).

**jnext (pre-fix)**: `divmmc.cpp:357-392` had no `$3DXX` decoder at all. The existing tests at `test/divmmc/divmmc_test.cpp:829-867` (NR-06/NR-07/NR-08) explicitly acknowledge "C++ DivMmc does not implement 0x3Dxx at all — leave failing" and "passes vacuously" — i.e. the contract was visible-but-not-enforced.

**Severity**: enabled by default in NR `$BB=$CD` (bit 7 set), but only fires when the ROM3 path is active (`rom3_active=1`, `pre_override(0)=1`, `pre_override(2)=1`, `layer2_map_read=0`). NextZXOS boot uses NR-controlled ROM banking and the supervisor's `$3DXX` is not a typical jump target. However, the `$3DXX` window IS the +3DOS RAM-disk subsystem in +3 mode and a legitimate bank-flipping primitive in some Next supervisor paths. Real hardware MUST overlay DivMMC ROM there when the conditions match; jnext was leaving the ROM3 BASIC code visible instead.

This is the only one of the three bugs with non-zero probability of contributing to G46(b) — see G46(b) cross-check below.

**Fix**: added a `(pc & 0xFF00) == 0x3D00` test gated on bit 7 + `rom3_path_eligible`, setting `instant_match`. Per VHDL line 2898, this is `_instant_on` (NOT delayed), so it activates `automap` same-cycle (subject to the held latch promotion semantics).

### Bug C — SPI ports `$E7` and `$EB` were NOT gated by `port_spi_io_en` (NR `$83` bit 3)

**VHDL**: `zxnext.vhd:2419,2620-2621`
```
port_spi_io_en <= internal_port_enable(11);  -- = NR $83 bit 3
port_e7 <= '1' when port_e7_lsb = '1' and port_spi_io_en = '1' else '0';
port_eb <= '1' when port_eb_lsb = '1' and port_spi_io_en = '1' else '0';
```

**jnext (pre-fix)**: `emulator.cpp:3250-3255` registered the handlers without consulting NR `$83` bit 3. The same gating is correctly applied to port `$E3` (DivMMC, NR `$83` bit 0), `$103B/$113B` (I2C, NR `$83` bit 2), and `$133B-$163B` (UART, NR `$83` bit 4).

**Severity**: silent under default NR `$83=$FF` (all bits set), which is the firmware boot configuration. Surfaces if firmware deliberately disables SPI I/O via NR `$83` (rare, but a real config in some multi-card / port-disable test paths).

**Fix**: added `(nextreg_.cached(0x83) & 0x08) == 0` early-return on read (returns `$FF`, the floating-bus value) and write (silent drop), matching the pattern at port `$E3`.

### Observation D — `read()` of port `$EB` triggering an SPI transfer is correct

VHDL `spi_master.vhd:82` decodes `spi_begin` from `(state_last OR state_idle) AND (i_spi_rd OR i_spi_wr)`. Both port reads and writes initiate a full byte transfer; reads send MOSI=`$FF`. The CPU read returns the **previous** `miso_dat` because the new transfer hasn't completed by the time IORQ ends.

jnext `SpiMaster::read_data()` returns the prior `rx_data_` AND triggers a new `dev->send()` call, storing the new MISO byte for the next read. This matches VHDL byte-granularity semantics. ✓

### Observation E — `SdCardDevice::persistent_response_byte_` is intentional

ZEsarUX-style sustained-byte responses for CMD0/CMD8/CMD12 are required because `tbblue.fw`'s MMC_Init polls the bus for non-`$00` / non-`$FF` patterns; without sustained responses the firmware retries CMD8 forever. The mechanism is documented in-code (`sd_card.h:118-135`, `sd_card.cpp:194-204`). Real SD cards in SPI mode tri-state MISO between commands (`$FF`), but the firmware was authored against ZEsarUX's emulation quirk and depends on it. This is a deliberate compatibility deviation, not a bug.

### Observation F — automap `_on_q` pipeline collapse is functionally equivalent

VHDL stages `divmmc_automap_*_on` into `*_on_q` registers on `falling_edge(CLK_28)` when `cpu_mreq_n='1'` (`zxnext.vhd:4115-4131`). The `divmmc.vhd` `automap_hold`/`automap_held` two-stage latch is then driven by the q signals. jnext's `check_automap()` collapses this to a single per-M1 call (latch `held = hold`, decode PC, recompute `hold`, compute `automap_active`). At byte-fetch granularity the observable behaviour is identical: instant matches fire same-cycle; delayed matches surface on the next M1 via the held promotion.

### Observation G — port `$E3` LSB-only decode is correct

VHDL: `port_e3 <= port_e3_lsb AND port_divmmc_io_en;` (line 2608) — high byte don't-care. jnext registers mask=`0x00FF`, match=`0x00E3` so the high byte of the I/O address is ignored. ✓ Same pattern for `$E7` / `$EB` / `$EF` (via `$EFF7`).

### Observation H — port `$E3` mapram OR-latch + NR `$09` bit 3 clear path is correct

`port_e3_reg(6)` is sticky-set (`zxnext.vhd:4182-4185`); cleared only via NR `$09` bit 3 write. jnext implements both halves correctly (`divmmc.cpp:107` for OR-latch on writes; `divmmc.cpp:121` `clear_mapram()` invoked from `emulator.cpp:3107` when NR `$09` bit 3 is written).

### Observation I — RETN delayed clear (G46(a)) is VHDL-faithful

`divmmc.vhd:108,126,139` clear `button_nmi`, `automap_hold`, `automap_held` on `i_retn_seen='1'`. `zxnext.vhd:4111` gates `divmmc_retn_seen` on `NOT mf_is_active` (Multiface mask). jnext routes the canonical ED 45 pulse from `Im2Controller` through `DivMmc::on_m1_retn_delay()` with the `multiface_.is_active()` mask (`emulator.cpp:585`). The delay register lets the overlay survive the RETN's own ED 45 fetch and drop on the next M1 — matching the VHDL register-shift shape.

### Observation J — SD CSD / CID synthesis is reasonable

`SdCardDevice::cmd9_send_csd()` synthesises a CSD v2.0 register from `file_size_`, computing `C_SIZE` so that `(C_SIZE+1)*512KB == file_size`. This is correct per SD Physical Layer Spec § 5.3.3. The CID at `cmd10_send_cid()` is generic-but-valid. Both fields are required by the supervisor's SD-init flow (`enNxtmmc.rom`).

## G46(b) cross-check

G46(b) is a **supervisor stack divergence** between RST `$08` hits #2 and #3 (3 missing `PUSH`es / 3 extra `POP`s vs CSpect), causing the supervisor to land on `PC=$423C` (= font glyph data executed as Z80 ops) instead of `$5CFB` (BASIC interpreter). The cascade ends at the `$5B0E` toggle wrapper / `$5B48` NEXTREG `$8E,$03` slide trigger (memory log: `project_g46b_2026_05_09_eod24_nr8e03.md`).

DivMMC/SD/SPI implications:
1. **CMD8 retry storms have been ruled out** by the `persistent_response_byte_` fix (commit history pre-dates this review). The supervisor reaches productive bank-2 init.
2. **Multiface NMI** isn't asserted at boot, so Bug A (NMI delayed) is dormant.
3. **Bug B ($3DXX wildcard)** is the only finding with G46(b)-relevant boot reach. The supervisor at `$5B48` does paging math; if a `$3DXX` fetch ever occurred while ROM3 was selected, jnext would have failed to overlay DivMMC ROM where real hardware would. **However**, the EOD-24 trace pinpoints the divergence at SP/PUSH-balance between RST `$08` hits — a cause-effect chain through `$3DXX` would need to fire in those exact M1 windows. Probability: low. The fix here is correct-by-spec but unlikely to close G46(b) on its own. Verifying: the EOD-24 log shows `pc[$1F59] / $423C / $5B48 / $0066 / $3E00 / $5B0E / $7BC1 / $1661` as the active PC trail — none in `$3DXX`. **G46(b) is downstream paging, not DivMMC.**

This review's fixes are independent VHDL-fidelity improvements; they do not target G46(b) and are not expected to move the boot needle on it.

## Files modified

1. `src/peripheral/divmmc.cpp` — `check_automap()`: split `$0066` NMI gate into independent bit-0 (delayed) + bit-1 (instant) clauses; added `$3DXX` wildcard decoder gated on NR `$BB` bit 7 + `rom3_path_eligible`. The 4 mid-PC clauses (`$04C6/$0562/$04D7/$056A`) and the off-range (`$1FF8-$1FFF`) are now independent `if` statements (not else-if) so multiple paths can match in the same fetch — VHDL `_on` signals are independently OR'd into `automap_hold`.
2. `src/core/emulator.cpp` — port `$E7` and `$EB` registrations now check `(NR $83 & 0x08) == 0` early-return, mirroring the existing pattern at port `$E3`, the I2C ports, and the UART ports.
3. `test/divmmc/divmmc_test.cpp` — re-purposed NR-06/NR-07/NR-08 (previously vacuous), added NR-09/NR-10/NR-11 (positive `$3DXX` cases with ROM3, mid-wildcard, and L2-read-suppression), NR-12a/NR-12b (`$0066` NMI delayed: same-cycle suppression + next-M1 held promotion), NR-13 (`$0066` delayed without `button_nmi` is a no-op).

## Test status

| Suite | Pre-fix | Post-fix |
|---|---|---|
| `divmmc_tests` | PASS (existing NR-06/NR-07/NR-08 "vacuous") | PASS + 6 new positive tests |
| `sdcard_tests` | PASS | PASS |
| `fuse_z80_tests` | PASS | PASS |
| `mmu_tests` (impact check) | PASS | PASS |
| Full unit-test suite (36) | PASS | PASS (36/36) |
| Full regression suite (33) | PASS | PASS (33/0/0) |

## Open questions / future work

1. **Multi-bit NR `$BB` test coverage**: the new tests cover bit 0 and bit 7 in isolation. Combined `$BB` configurations (e.g. bit 0 + bit 1 + bit 7 simultaneously fired in a single supervisor sweep) are not exercised. If a regression appears in a `$BB`-customising firmware, add a mixed test.
2. **`port_e3_reg(6)` clear on `divmmc_automap_reset`**: the VHDL behaviour (line 4180+) does NOT clear `port_e3_reg` on `divmmc_automap_reset`. jnext mirrors this. But the MAPRAM clear via NR `$09` bit 3 only goes through the dedicated `clear_mapram()` setter — it does NOT write the cached NR `$09` value. If a feature ever adds a save-state for the cached NR `$09` low bits, double-check the OR-latch survives through reset.
3. **Flash CS (`$7F` value on port `$E7`)**: jnext's `SpiMaster::write_cs()` deliberately does not recognize `$7F` (Flash) because the gate at VHDL line 3319 requires `nr_03_config_mode='1' OR nr_02_reset_type(2)='1'`. Neither condition is modelled at the SpiMaster level. If a future test or boot path enters config_mode and writes `$7F`, jnext will silently drop it (the bypass is `cs_=$FF`, all deselected). Likely fine for NextZXOS boot.
4. **`spi_wait_n` for DMA throttling**: the SPI master is byte-granular; the VHDL `o_spi_wait_n` signal at byte boundaries is approximated as constantly asserted. A cycle-accurate DMA-via-SPI burst test would reveal a divergence here, but G137 (long-term FSM rewrite) covers this.
5. **`nr_d8`/`nr_d9`/`nr_da` were mis-labelled in the prompt** as DivMMC config; they are the IO-trap FDC NMI registers (correctly modelled). No action.
6. **Port `$EF` mentioned in the prompt is not a DivMMC port**; the only `port_ef*` in VHDL is `port_eff7` (Pentagon-1024 disable). No action.

## VHDL ↔ jnext line cross-reference

| VHDL | jnext | What |
|---|---|---|
| `divmmc.vhd:85-86` | `divmmc.cpp:106-107` | port `$E3` bit 7/6 decode (with sticky-OR for bit 6) |
| `divmmc.vhd:88-89` | `divmmc.cpp:419-429` | page0 / page1 detection |
| `divmmc.vhd:94-95` | `divmmc.cpp:419-429` | rom_en / ram_en formula |
| `divmmc.vhd:96` | `divmmc.cpp:443-449` | ram_bank fixed-3 for page0 |
| `divmmc.vhd:100` | `divmmc.cpp:431-441` | rdonly formula |
| `divmmc.vhd:108,126,139` | `divmmc.cpp:200-210` (on_retn) | RETN clears for all 3 latches + button_nmi |
| `divmmc.vhd:128-131` | `divmmc.cpp:397-399` | automap_hold formula |
| `divmmc.vhd:141-148` | `divmmc.cpp:273-274,404-405` | automap_held promotion + automap output |
| `divmmc.vhd:120-121` | `divmmc.cpp:357-365` | nmi_instant_on / nmi_delayed_on gated by button_nmi |
| `zxnext.vhd:2898-2908` | `divmmc.cpp:368-405` | NR `$BB` bits 0..7 decoders |
| `zxnext.vhd:3138` | `divmmc.cpp:325-327` | sram_divmmc_automap_rom3_en composite |
| `zxnext.vhd:4180-4185` | `divmmc.cpp:105-117` + `emulator.cpp:3107` | port `$E3` reg sticky bit 6 + NR `$09` bit 3 clear |
| `zxnext.vhd:4190` | `divmmc.cpp:115-117` | port `$E3` read mask 0xCF |
| `zxnext.vhd:2412,4112` | `divmmc.cpp:151-182` | port_divmmc_io_en + nr_0a_divmmc_automap_en split levers |
| `zxnext.vhd:2419,2620-2621` | `emulator.cpp:3250-3271` (post-fix) | port_spi_io_en gating port `$E7`/`$EB` |
| `zxnext.vhd:3308-3322` | `spi.cpp:52-93` | port `$E7` CS decode + sd_swap |
| `spi_master.vhd:82,109-117,162-168` | `spi.cpp:99-127` | full-duplex byte exchange + miso_dat capture |
