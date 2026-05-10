# Pass-11 Verify-Audit Report — DivMMC + SD + SPI Subsystem

**Date**: 2026-05-10
**Branch**: `task2/verify11-divmmc-sd-spi` (off integration HEAD `d385d5e`)
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify11-divmmc-sd-spi`
**Methodology**: Blind audit (no prior pass reports consulted). VHDL is the
oracle — every finding cites a line range from
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.
No probes / instrumentation; semantic VHDL-faithful fixes only.

## Subsystem scope

- `src/peripheral/divmmc.{cpp,h}` — DivMMC ROM/RAM overlay + automap pipeline
- `src/peripheral/sd_card.{cpp,h}` — SD card SPI-mode protocol backend
- `src/peripheral/spi.{cpp,h}` — SPI master + CS arbiter
- `src/core/sd_rom_extractor.{cpp,h}` — host-side FAT32 reader
- DivMMC/SD/SPI port-write dispatch slice in `src/core/emulator.cpp`
- DivMMC-related NextREGs (NR 0x0A, 0x83, 0xB8-0xBB, 0x09 bit 3, 0x03,
  0x02 bit 2)

VHDL primary references: `device/divmmc.vhd`, `serial/spi_master.vhd`,
and `zxnext.vhd:2412-2697 / 3278-3332 / 3081-3138 / 4108-4191`.

---

## Methodology angles audited

- DivMMC entry-point matrix (RST 0x00..0x38, NMI 0x0066, tape traps
  0x04C6/0x0562/0x04D7/0x056A, $3Dxx wildcard, 0x1FF8-0x1FFF off range)
- DivMMC RAM bank addressing (8 KB × 16 = 128 KB), bit-3:0 mask
- conmem vs mapram precedence (VHDL divmmc.vhd:94-100); slot-0 read-only
- mapram OR-latch (port 0xE3 bit 6) + clear via NR 0x09 bit 3
- VHDL `i_divmmc_reg` slicing — bits 5:4 forced "00" (zxnext.vhd:4154);
  read returns same shape (line 4190); C++ mask `0xCF`
- SD/MMC card init FSM: power-up state, CMD0/CMD8/CMD55+ACMD41/CMD58
  sequence, voltage-window check, OCR fields, CCS bit
- SD CSD/CID register synthesis (CMD9/CMD10) — SDHC v2.0 layout
- SPI mode transitions, CS-deselect deassert callback
- SPI clock divider register effect (N/A — JNEXT zero-latency byte wrapper)
- CS-deselect mid-transfer (deselect() on cs change in `write_cs`)
- CMD24/25 single/multi-block write; Stop-Tran token (0xFD) absent (CMD25
  not implemented; CMD24 is)
- CRC enable/disable, CRC value computation (not modelled — host CRC
  byte ignored, common simplification)
- CMD16 SET_BLOCKLEN (arg=512 only)
- ACMD23 SET_WR_BLK_ERASE_COUNT (CMD23 ack)
- Soft reset of SPI master (`i_reset` paths in spi_master_mod) — see
  V11-DIVMMC-01 below
- DivMMC paging precedence vs MMU vs Multiface vs alt-ROM (verified
  zxnext.vhd:3081-3138 mapping into Mmu.cpp's divmmc overlay helpers)
- DivMMC RST $08 trigger depending on automap state (covered by
  divmmc_test.cpp EP-* + AM-*)
- Port 0xEB DivMMC data port read-back during in-progress transfer
  (verified pipeline 1-byte delay matches VHDL :162-166)
- SD-card image bounds: reads/writes past end of image (cmd17/18 emit
  data error token 0x08; cmd24 silently drops out-of-range writes)
- FAT32 reader: cluster chain, LFN handling, fragmented files, subdir
  tree (read-only walk; LFN entries skipped; chain-length bound; 8.3
  uppercase keys)
- SPI cycle FSM and DMA wait_n throttle — kept as class-(d) (already
  documented as G137 long-term FSM rewrite)

---

## Findings

| ID              | Class | File                       | Status   | Commit |
|-----------------|-------|----------------------------|----------|--------|
| V11-DIVMMC-01   | (b)   | `src/peripheral/spi.cpp`   | RESOLVED | (this audit) |
| V11-DIVMMC-02   | (b)   | `src/peripheral/divmmc.h`  | RESOLVED | (this audit) |

---

### V11-DIVMMC-01 — `SpiMaster::reset()` does not pulse `deselect()` on selected slaves

**Class**: (b) — VHDL-faithful semantic divergence; latent on the production
boot path (firmware writes commands before reading), real for any caller
that reads first (test fixtures, save-state replay, host-bug scenarios).

**VHDL reference**: `zxnext.vhd:3308-3309`:

```vhdl
process (i_CLK_28)
begin
   if rising_edge(i_CLK_28) then
      if reset = '1' then
         port_e7_reg <= (others => '1');
      ...
```

On `reset='1'`, the FPGA forces `port_e7_reg` to all-ones, deasserting
every chip-select line (`spi_ss_*_n` all '1'). On real hardware that CS
rising edge resets each connected SPI slave's protocol state — SD cards
in particular abort an in-flight transaction (mid CMD17 SENDING_DATA,
mid CMD18 multi-block stream, mid CMD24 RECEIVING_DATA, an unfinished R1
in RESPONDING) and return their FFs to the IDLE command-acceptance state.

**Pre-fix behaviour** (`SpiMaster::reset()`):

```cpp
void SpiMaster::reset() {
    cs_ = 0xFF;
    rx_data_ = 0xFF;
    // ...
}
```

The C++ set `cs_ = 0xFF` directly without invoking the `deselect()`
callback on any currently-selected slave. The `SdCardDevice` therefore
retained its protocol-state FFs (`state_=SENDING_DATA`,
`multi_block_=true`, `pending_write_after_r1_=true`, `resp_buf_` /
`resp_idx_` / `data_idx_` / `data_crc_count_` non-zero, etc.) until the
next firmware-driven CS write. The `receive()` default-case branch ("any
0x40-0x7F byte starts a new command") papers over this on the write path,
but `send()` keeps streaming stale bytes from the prior block until the
firmware writes a command.

**Root cause**: Pre-fix `reset()` mirrored only the CS-register clear, not
the physical CS rising edge that the VHDL `port_e7_reg <= all-ones` would
trigger on connected slaves.

**Fix** (`src/peripheral/spi.cpp`): walk every previously-selected slot
and pulse `deselect()` exactly the way `write_cs(0xFF)` would — same
loop, same precondition (`!(cs_ & (1<<i)) && devices_[i]`). Then clear
`cs_ = 0xFF`. The PASS-7 invariant (do not clear `devices_[]`) is
preserved — the deselect notifies the slaves; the wires remain
physically connected. Note that `SdCardDevice::deselect()` resets all
protocol-state FFs but preserves `initialized_`, matching the comment
at `emulator.cpp` reset-cascade ("soft reset pulls CS high momentarily
but does NOT reset the card's internal init state").

**Discriminative regression test**: `divmmc_test.cpp` row **SS-15**
(group 12 "Port 0xE7 CS"). Stimulus: attach `MockSpiDevice` on CS0,
select via `write_cs(0xFE)` (so `cs_=0xFE`), then call `m.reset()`.
Post-fix the mock's `was_deselected` flag is true and `cs_=0xFF`;
pre-fix the flag stays false. SS-12 ("device bindings survive reset")
is unaffected — it never selects the device before resetting, so
`cs_=0xFF` was already the case and no deselect was ever needed in
either direction.

**Compatibility check**: existing rows that call `write_cs(0xFF)` after
reset see no behaviour change — the deselect is now invoked at reset
instead of "at the first 0xFF→0xFE→0xFF cycle", which fires earlier in
time but with identical observable downstream effect.

---

### V11-DIVMMC-02 — `DivMmc::is_nmi_hold()` drops the same-cycle `instant_on` term

**Class**: (b) — VHDL-faithful semantic divergence in the
`o_disable_nmi` accessor. Latent on the production boot path (the MF
arbiter's other gates dominate during the boot sequence) but a real
gap in the VHDL contract for the line-150 signal.

**VHDL reference**: `divmmc.vhd:148-150`:

```vhdl
automap <= (not i_automap_reset) and (automap_held or
            (i_automap_active and (i_automap_instant_on or
                                    automap_nmi_instant_on)) or
            (i_automap_rom3_active and i_automap_rom3_instant_on));

o_disable_nmi <= automap or button_nmi;
```

`o_disable_nmi` (line 150) is the OR of the **combinational** `automap`
(line 148, which includes the same-cycle `instant_on` terms) and the
registered `button_nmi`. NOT the registered `automap_held` alone.

**Pre-fix behaviour** (`src/peripheral/divmmc.h`):

```cpp
bool is_nmi_hold() const { return automap_held_ || button_nmi_; }
```

The accessor used the registered `automap_held_` (= the latched FF), so
on the very first M1 fetch where an instant-on entry-point matched
(e.g. PC=0x0066 with `button_nmi_=1` and NR 0xBB bit 1 set, or any RST
entry configured instant-on via NR 0xBA), `is_nmi_hold` returned false
even though VHDL would assert `o_disable_nmi=1` immediately. The
divergence drops back to zero on the NEXT M1 (when held catches up to
hold) — so the existing NM-08 row (which uses two `check_automap` calls
before sampling) failed to discriminate.

**Root cause**: Pre-fix used the registered held bit; VHDL line 150
references the combinational `automap`. The C++ already computes that
combinational signal correctly during `check_automap` (step 4):

```cpp
automap_active_ = automap_held_ || instant_match;  // line 435
```

So the fix is to expose `automap_active_` from the accessor instead of
`automap_held_`.

**Fix** (`src/peripheral/divmmc.h`):

```cpp
bool is_nmi_hold() const { return automap_active_ || button_nmi_; }
```

(plus an updated 30-line VHDL-cited comment block).

The NmiSource consumer's `divmmc_nmi_hold` gates (VHDL :2107 MF-latch
block, :2118 `nmi_hold` MUX) now see the same-cycle assertion the VHDL
would.

**Discriminative regression test**: `divmmc_test.cpp` row **NM-10**
(group 9 "NMI / button"). Stimulus: configure RST 0x00 as instant-on
(NR 0xB8 bit 0 = 1, NR 0xB9 bit 0 = 1, NR 0xBA bit 0 = 1), main path,
issue ONE `check_automap(0x0000, true)`. Post-fix `automap_held_=0`
(just promoted from prior 0), `automap_active_=1` (held(0) ||
instant_match(1)), `is_nmi_hold()=1`. Pre-fix `is_nmi_hold()=0`. NM-08
already covers the steady-state case (held caught up to hold) and is
updated only in its comment to clarify the `automap`-vs-`automap_held`
VHDL terminology — the truth-table outputs are unchanged.

---

## Build & test status (post-fix)

```
cmake --build build -j$(nproc)         OK (jnext + 38 test executables)
ctest --test-dir build                 38/38 PASS (100%)
./build/test/fuse_z80_test             1356/1356 PASS (100%)
bash test/00regression/regression.sh   31-32 PASS / 1-2 FAIL
                                       (FAIL set varies across runs;
                                        same flakiness band as baseline
                                        d385d5e — `parallax-demo` is a
                                        persistent pre-existing
                                        pixel-diff, transient
                                        "emulator crashed/timed out"
                                        failures occur on different
                                        tests across runs and exist in
                                        baseline too — not introduced
                                        by this audit)
```

`divmmc_test` per-group breakdown post-fix:

```
1. Port 0xE3             8/8
2. conmem paging         9/9
3. automap paging        5/5
4. RST entry points     12/12
5. Non-RST entry points 15/15
6. Deactivation          9/9
7. Instant vs delayed    5/5
8. ROM3 conditional      4/4
9. NMI / button         14/14   (was 13/13 — added NM-10)
10. NR 0x0A enable      11/11
11. SRAM mapping         3/3
12. Port 0xE7 CS        14/14   (was 13/13 — added SS-15)
13. Port 0xEB xchg       7/7
14. SPI state machine    1/1
15. MISO latch           2/2
16. MISO mux             2/2
17. Integration          7/7
18. PriOverride G46(b)   6/6

Totals: 134 checks, 0 skips, 134 plan rows covered  (was 132)
```

---

## Areas explicitly verified clean (no findings)

These methodology angles were inspected and found to match VHDL or to be
intentional documented divergences:

- **Port 0xE3 read mask**: `read_control() & 0xCF` mirrors VHDL line 4190
  (`port_e3_dat <= port_e3_reg(7 downto 6) & "00" & port_e3_reg(3 downto 0)`).
- **Port 0xE3 write OR-latch on bit 6**: `mapram_ = mapram_ || (val & 0x40)`
  mirrors VHDL line 4182 (`port_e3_reg(6) <= cpu_do(6) or port_e3_reg(6)`).
- **Port 0xE3 bit-mask 5:4 = 00**: write_control stores raw, but read
  masks via `0xCF` — bits 5:4 round-trip as zeros, matching VHDL line
  4154 (`i_divmmc_reg <= port_e3_reg(7 downto 6) & "00" & port_e3_reg(3 downto 0)`).
- **NR 0x09 bit 3 → mapram clear**: `clear_mapram()` matches VHDL
  zxnext.vhd:4184-4185.
- **DivMMC entry-point matrix**: RST 0x00..0x38, NMI 0x0066 (with
  button_nmi gate), tape traps 0x04C6 / 0x0562 / 0x04D7 / 0x056A,
  $3Dxx wildcard, 0x1FF8-0x1FFF off range — all match
  zxnext.vhd:2848-2908.
- **Main-vs-ROM3 path arbitration**: VHDL `i_automap_active` /
  `i_automap_rom3_active` modelled via `sram_pre_override_2`,
  `sram_pre_override_0`, `layer2_map_read_`, `rom3_active_` — correct
  per VHDL :3137-3138 + divmmc.vhd:130,148.
- **`button_nmi` clear-while-held**: Pass-8 fix at
  `divmmc.cpp:294-299` matches VHDL :112-113 (continuous-while-held
  semantics).
- **`apply_enabled_transition_` cascade clear**: matches VHDL
  `i_automap_reset` (= `port_divmmc_io_en=0 OR
  nr_0a_divmmc_automap_en=0`) via divmmc.vhd:108,126,139.
- **CONMEM gated only by port_io, not nr_0a_4**: comment at
  `is_active()` cites VHDL :94-95,98,4147 — confirmed.
- **`port_e7` decoder priority**: `if (val & 0x03) == 0x02 / 0x01 ...
  else if val == 0xFB / 0xF7 / 0x7F` matches VHDL :3311-3322 if/elsif
  chain order exactly.
- **`port_e7` Flash-CS gate**: `flash_cs_enable_` mirrors VHDL line 3319
  composite (`nr_03_config_mode='1' OR nr_02_reset_type(2)='1'`).
- **`port_e7` sd_swap inversion**: SD0/SD1 raw decode unchanged; stored
  pattern flips on swap=1 — matches VHDL :3311-3314.
- **SPI master `i_spi_wr=1` regardless of CS state**: even with no
  active device, VHDL pulls `spi_miso<='1'` and miso_dat captures 0xFF
  (zxnext.vhd:3278-3280). Verify3 fix in `write_data` / `read_data`
  forces `rx_data_=0xFF` on null device — confirmed.
- **CMD17 / CMD18 / CMD24 protocol shapes**: NCR + R1 + 0xFE + 512 +
  CRC for read; CMD24's bridge flag `pending_write_after_r1_`
  correctly transitions RESPONDING → RECEIVING_DATA only after R1 has
  been emitted on MISO.
- **CMD18 multi-block continuation**: between-blocks emits 0xFE token
  + 512 + CRC (no extra NCR/R1), with one 0xFF inter-block filler so
  the host's "skip 0xFF until 0xFE" poll finds the new token cleanly.
- **CMD12 STOP_TRANSMISSION stuff bytes**: 8 stuff bytes + NCR + R1
  matches the firmware's expected pattern.
- **CSD v2.0 SDHC layout (CMD9)**: structure=01, capacity=(C_SIZE+1)\*512KB
  matches SD spec § 5.3.3.
- **CID layout (CMD10)**: 16-byte CID with valid CRC end-bit per spec
  § 5.2.
- **OCR (CMD58)**: bit 31=power-up done, bit 30=CCS=1 (SDHC), voltage
  range bits set in byte 1+2 — per SD spec § 5.1.
- **CMD16 SET_BLOCKLEN(arg≠512)**: returns illegal-command bit + idle
  bit derived from `initialized_` (Pass-5 fix).
- **`CMD55 → !ACMD41`** falls through to regular CMD switch (Pass-9
  fix); spec § 4.3.9.1 compliant for the regular-CMD case (the latent
  `ACMD13/22/23/42/51` shared-index case is documented inline as a
  known limitation, not exercised by the boot path).
- **Default branch unhandled CMD**: returns R1 with illegal-command bit
  (Pass-8 fix).
- **persistent_response_byte_**: ZEsarUX-style sustained byte for
  CMD0 / CMD12 (= 0x01 / 0xFF) — matches the firmware's expected
  poll loop.
- **`mount()` / `unmount()` full-state reset**: Pass-5/8 fixes
  canonical full reset() before/after. Confirmed.
- **FAT32 reader**: MBR signature, BPB sanity (bytes_per_sector ∈ {512,
  1024, 2048, 4096}, sectors_per_cluster power-of-2 ≤128, FATSz32>0,
  root_cluster≥2), cluster-chain bound (`max_chain_len`),
  EOC/BAD-cluster handling — all present.
- **DivMMC integration into Mmu**: `divmmc_read` /
  `divmmc_write` gated on `is_active()` returning the new
  composite (V11-DIVMMC-02 fix preserves current `is_active` reading
  of port_io_enable_ AND (conmem || automap_active_) — fully VHDL-
  faithful).

---

## Class-(d) escalations carried forward (not addressed in this pass)

- **SPI cycle FSM and DMA wait_n throttle** (`spi_master.vhd:62-99`):
  the JNEXT `SpiMaster` is a zero-latency byte wrapper; per-bit
  state_r/oshift_r/ishift_r modelling is needed for cycle-accurate DMA-
  via-SPI burst throttling. Already documented as G137 in `spi.h`'s
  `spi_wait_n()` accessor comment. Out of scope for this pass.

---

## Convergence claim (within scope)

After applying V11-DIVMMC-01 and V11-DIVMMC-02 with their discriminative
regression tests, the DivMMC + SD + SPI subsystem is VHDL-faithful for
every C++-modelled signal exposed at the public API surface. No
remaining class-(a) or class-(b) findings outside the documented
class-(d) item above. The full-build smoke trio (ctest 38/38, FUSE
1356/1356, regression in flakiness band 31-32 / 1-2) is unchanged from
baseline.
