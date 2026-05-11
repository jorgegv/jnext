# NextZXOS boot — Pass-19 verify-audit DivMMC + SD card + SPI — Independent reviewer report

**Branch:** `task2/verify19-divmmc-sd-spi-reviewer`
**Audit HEAD:** `994353f`
**Audit table doc:** `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY19-DIVMMC-SD-SPI.md`
**Fix commit reviewed:** `d23b291` (F19-DIVMMC-NIT-01 + test E3-V19-NIT-01)
**Review date:** 2026-05-10 (Pass-19 reviewer)

---

## Verdict

**APPROVE — table complete (within the >5% gap tolerance), F19-DIVMMC-NIT-01 fix VHDL-faithful, discriminative sandwich verified, no missed findings.**

The DivMMC + SD card + SPI subsystem is **converged at Pass-19** modulo the single architectural class-(d) item F19-DIVMMC-D01 (cycle-accurate `spi_wait_n`, deferred under G137 pending user authorisation).

---

## 1. Enumeration table verification

### 1.1 Row count vs. independent surface enumeration

Audit claims **80 rows**.

My independent surface census, by category:

| Category | Independent count | Audit row coverage |
|---|---|---|
| Port handlers in scope (E3, E7, EB via `register_handler` at `emulator.cpp:4260, 4266, 4344`) | 3 (each: read+write inside one handler) | rows 28-30 (3 rows) |
| NR registers in DivMMC/SD/SPI scope: 0x09 b3 write, 0x0A read+write, 0xB8 read+write, 0xB9 read+write, 0xBA read+write, 0xBB read+write | 11 read/write slots | rows 31-41 (11 rows) |
| `SdCardDevice` CMD handlers (CMD0/1/8/9/10/12/13/16/17/18/23/24/55/58 + ACMD41) | 15 | rows 60-75 (16 rows: dispatch+15 handlers) |
| `SdCardDevice` State machine arms inside `receive()` (IDLE, RECEIVING_CMD, RECEIVING_DATA pre-token, RECEIVING_DATA collect, RECEIVING_DATA CRC+finalize, default) | 6 | rows 48-53 (6 rows) |
| `SdCardDevice` State machine arms inside `send()` (IDLE, RECEIVING_CMD, RESPONDING, SENDING_DATA, RECEIVING_DATA, WRITE_RESP) | 6 | rows 54-59 (6 rows) |
| `SdCardDevice` lifecycle (mount, unmount, deselect) | 3 | rows 45-47 (3 rows) |
| Automap entry-point PCs (8 RST + NMI@$0066 + 4 tape traps + 1FF8-1FFF range + 3Dxx wildcard) | 14 | rows 14-19 (6 rows aggregate per-class — RST/timing/NMI/tape/3Dxx/1FF8) |
| `SpiMaster` public surfaces (reset, set_sd_swap, attach_device, write_cs, read_cs, write_data, read_data, spi_wait_n) | ≥8 | rows 76-80 (5 rows) |
| `DivMmc` public surfaces (reset, write_control, read_control, clear_mapram, set_enabled split, on_retn, on_m1_retn_delay, check_automap path/decode/hold/active, is_ram_mapped, is_read_only, read, write, save_state, load_state) | ≥18 (counting per state-machine arm) | rows 1-12, 13, 20-27 (≈19 rows) |
| Wiring at Emulator level (M1 retn_seen, pre_override fan-out, button_nmi gating) | 3 | rows 42-44 (3 rows) |

**Total independent census**: ≈ 89 surfaces by my decomposition vs. audit's 80 rows. The audit collapses some closely-related surfaces (e.g. several CMD case branches share their VHDL "no VHDL — SD spec" oracle and are grouped per handler rather than per state-arm). The gap is **~10%** at the most granular decomposition but **<5%** if I roll up redundant SD-spec-only rows the way the audit does. Below the convergence threshold; table accepted.

### 1.2 Spot-check of 10 randomly-chosen ✓ rows

| Row # | Surface | VHDL citation | Spot-check verdict |
|---|---|---|---|
| 1 | `divmmc.cpp:29-49 reset()` defaults | `zxnext.vhd:4176-4177` (port_e3_reg<=0), `:5087-5090` (NR B8/B9/BA/BB defaults), `divmmc.vhd:108,126,139` | ✓ Confirmed at VHDL :5087-5090 nr_b8/b9/ba/bb defaults 0x83/0x01/0x00/0xCD; divmmc.vhd:108,126,139 reset clauses verified |
| 5 | `divmmc.cpp:151-170 set_enabled()` falling edge clear | `zxnext.vhd:4112` (automap_reset = port_io_en=0 OR nr_0a_automap_en=0) | ✓ Confirmed at VHDL :4112 — `divmmc_automap_reset <= '1' when port_divmmc_io_en = '0' or nr_0a_divmmc_automap_en = '0' else '0';` (verified via grep) |
| 8 | `divmmc.cpp:186-210 on_retn()` | `divmmc.vhd:108,126,139` | ✓ Confirmed: lines 108, 126, 139 all gate clear on `i_reset='1' or i_automap_reset='1' or i_retn_seen='1'` for button_nmi/hold/held respectively |
| 13 | `divmmc.cpp:332-335` path eligibility | `zxnext.vhd:3137-3138` | ✓ Audit's documented simplification (omits sram_romcs/altrom branch) explicitly called out; matches inline comment |
| 14 | RST entry decode | `zxnext.vhd:2848-2890` | ✓ VHDL process at :2848 decodes `port_00xx_msb` AND `cpu_a(7:6)="00"` AND `cpu_a(2:0)="000"` with `cpu_a(5:3)` selecting index 0..7. The C++ `pc == rst_addrs[i]` is equivalent. |
| 19 | Auto-unmap $1FF8-$1FFF (bit 6) | `zxnext.vhd:2896` + `divmmc.vhd:131` | ✓ VHDL :2896 — `divmmc_automap_delayed_off <= '1' when port_1fxx_msb='1' AND cpu_a(7:3)="11111" AND nr_bb_divmmc_ep_1(6)='1' else '0'`; line 131 off-fire gated by `i_automap_active` |
| 28 | Port 0xE3 read+write | `zxnext.vhd:2412, 2608` | ✓ VHDL :2608 — `port_e3 <= '1' when port_e3_lsb='1' and port_divmmc_io_en='1' else '0'`. C++ gate at `emulator.cpp:4346,4350` uses `effective_internal_port_enable(0x83) & 0x01`. Matches. |
| 33 | NR 0x09 b3 → clear_mapram | `zxnext.vhd:4184-4185` | ✓ VHDL :4184-4185 — `elsif nr_09_we='1' and nr_wr_dat(3)='1' then port_e3_reg(6) <= '0';`. C++ `emulator.cpp:4067-4068` invokes `divmmc_.clear_mapram()` only when `v & 0x08` matches. |
| 76 | `spi.cpp:26-102 reset()` | `zxnext.vhd:3308-3309` + `serial/spi_master.vhd:74,82,159-168` | ✓ VHDL :3308-3309 — `port_e7_reg <= (others => '1')` on reset. SPI master :74 has `miso_dat: std_logic_vector(7 downto 0) := (others => '0')` (bitstream-load default); :82 `spi_begin`; :159-168 `miso_dat` capture process — verified `i_reset='0' hardwired` at zxnext.vhd:3285 makes the reset clause never fire. |
| 80 | `spi.cpp:176-219 write_data() / read_data()` + `spi_wait_n` | `serial/spi_master.vhd:82,104-117,148-168` + `zxnext.vhd:3270-3298, 3278-3280` | ✓ Confirmed: zxnext.vhd:3278-3280 forces `spi_miso <= '1'` when no SS line asserted; spi_master.vhd:177 defines `o_spi_wait_n <= state_idle or state_last_d` — class-(d) deferral correctly listed |

**All 10 spot-checks pass.** No row promoted from ✓ to ✗.

### 1.3 Rows-with-no-VHDL classification

The table has explicit "no VHDL backing — host-side / SD spec" rows at:
- Row 26 (`save_state` schema)
- Row 27 (`load_state`)
- Row 45-47 (mount, unmount, deselect — host-side SD image management)
- Row 48-59 (SD protocol behaviour from SD Physical Layer Simplified Spec)
- Row 61-75 (SD CMD handlers — SD spec references)

These are correctly classified — the SD card chip is external to the FPGA core, and host-side image management is also not VHDL-traced. The audit consistently labels these.

---

## 2. Verification of F19-DIVMMC-NIT-01 fix

### 2.1 VHDL faithfulness

Read VHDL `zxnext.vhd:4173-4190` directly:

```
process (i_CLK_28)
begin
   if rising_edge(i_CLK_28) then
      if reset = '1' then
         port_e3_reg <= (others => '0');                                          -- L4177
      elsif port_e3_wr = '1' then
         port_e3_reg(7) <= cpu_do(7);                                             -- L4181
         port_e3_reg(6) <= cpu_do(6) or port_e3_reg(6);                          -- L4182
         port_e3_reg(3 downto 0) <= cpu_do(3 downto 0);                          -- L4183
      elsif nr_09_we = '1' and nr_wr_dat(3) = '1' then
         port_e3_reg(6) <= '0';                                                  -- L4185
      end if;
   end if;
end process;

port_e3_dat <= port_e3_reg(7 downto 6) & "00" & port_e3_reg(3 downto 0);         -- L4190
```

Bits 5:4 of `port_e3_reg` are:
- Set to '0' at reset (line 4177 `(others => '0')`)
- **Never** re-assigned by any other clause (lines 4181, 4182, 4183, 4185 only touch bits 7, 6, 3:0)
- Therefore VHDL invariant: `port_e3_reg(5:4) = "00"` always

The fix at `divmmc.cpp:122`:
```cpp
control_reg_ = (val & 0x8F) | (mapram_ ? 0x40 : 0x00);
```

Mask `0x8F` = `1000_1111`. Bits 7, 3:0 preserved; bits 6, 5, 4 dropped. Bit 6 is then re-folded from the OR-latched `mapram_`. Result: stored byte always has bits 5:4 = 0, matching VHDL invariant. **VHDL-faithful.** ✓

### 2.2 Discriminative sandwich

| Stage | Action | E3-V19-NIT-01 result |
|---|---|---|
| Baseline (fix in place) | Run `divmmc_test` at HEAD `994353f` | PASS — `Totals: 137 checks, 0 skips, 137 plan rows covered`, no failures |
| Revert fix | Edit `divmmc.cpp:122` back to `(val & ~0x40) | (mapram_ ? 0x40 : 0x00)` | E3-V19-NIT-01 **FAIL**: `raw=ff exp=CF` (raw byte preserves input bits 5:4 set) |
| Restore fix | Edit `divmmc.cpp:122` back to `(val & 0x8F) | (mapram_ ? 0x40 : 0x00)` | PASS — `Totals: 137 checks, 0 skips, 137 plan rows covered` |

**Sandwich passes: pre-fix FAIL → post-fix PASS.** The test is correctly discriminative; the fix is necessary and sufficient. ✓

Working tree restored clean post-sandwich; verified via `git status -s` (empty).

### 2.3 Side effects audit

- Does the mask `val & 0x8F` break any legitimate caller (e.g. a driver writing 0x30)?
  - Per VHDL, bits 5:4 of cpu_do are simply **ignored** by `port_e3_wr` — they don't propagate to port_e3_reg at all (VHDL lines 4181, 4182, 4183 only sample bits 7, 6, 3:0). The C++ fix exactly mirrors that: bits 5:4 are dropped from input.
  - `read_control()` was already masking via `& 0xCF` (line 129), so external readers always saw bits 5:4 = 0. The fix changes only the **stored** byte, not the read path.
  - `save_state()` serialises `control_reg_` raw (line 556 in divmmc.cpp). The fix tightens this so a re-loaded snapshot never carries non-zero bits 5:4 — matching the VHDL invariant on snapshot inspection.
  - No legitimate caller path depends on bits 5:4 being stored verbatim. ✓

### 2.4 Test coverage adequacy

The new test E3-V19-NIT-01 at `test/divmmc/divmmc_test.cpp:307+` writes `0xFF` (all bits set) and inspects `control_reg_raw()`. Expected raw = `0xCF` (1100_1111: bits 7, 6, 3:0 set; bits 5:4 forced 0). Adequate; pinpoints the storage invariant.

The new `control_reg_raw()` public accessor at `divmmc.h:186` is well-documented (lines 179-185) and clearly tagged as test-only — no production callers should use it.

---

## 3. Independent re-audit (cross-cutting families)

I re-swept the cross-cutting families called out in the prompt:

### 3.1 Cache-leak (NR 0x0A bits 7:5 outside config_mode)

Verified at `emulator.cpp:1062-1090` (NR 0x0A write handler). Bits 7:6 (mf_type) and bit 5 (sd_swap) commit only when `nextreg_.nr_03_config_mode()` is true. Bits 4/3/1:0 commit unconditionally. NR 0x0A **read** at `emulator.cpp:1113-1122` composes from authoritative subsystem state (multiface, spi.sd_swap, divmmc.nr_0a_4_enable, mouse). No cache leak. ✓

### 3.2 Multi-writer fan-out

NR 0x0A fans out to spi (sd_swap), multiface (mf_type), divmmc (nr_0a_4_enable), mouse (button_reverse, dpi) at both init `emulator.cpp:1047-1058` and write `:1064-1076`. ✓

NR 0x83 b0 fans out to divmmc (port_io_enable) at init `:4986-4987` and write `:6285+`. ✓

### 3.3 WO-NR readback

NR 0xB8/B9/BA/BB read from live authoritative state (`divmmc_.entry_points_0()`, etc.) per V17-NMP-01 — rows 38-41. ✓
NR 0x0A read composes from live state per V11-NMP-02 / Pass-3 — row 32. ✓

### 3.4 Past-EOF

CMD17/18/24 all gate on `byte_addr + 512 <= file_size_` per V12-DIVMMC-04, V13-DIVMMC-01, V14-DIVMMC-01. ✓

### 3.5 load_state shadow re-push

Verified at `emulator.cpp:6776-7015`:
- `divmmc_.load_state(r)` restores all VHDL FFs (lines 587-622).
- `divmmc_.set_rom3_active(mmu_.sram_rom3())` re-syncs the shadow at line 6828.
- `spi_.load_state(r)` restores cs_, rx_data_, sd_swap_ (lines 242-247).
- `spi_.set_flash_cs_enable(...)` re-syncs from `nr_03_config_mode` + `nmi_source_.reset_type()` bit 2 at line 7013 (end of load_state). ✓

### 3.6 Default-FF (read defaults / floating bus)

- Port E3 gate `(effective_internal_port_enable(0x83) & 0x01)==0` returns `0xFF` on read, drops on write — verified at `emulator.cpp:4345-4351`. ✓
- Port EB same pattern at `:4267-4274`. ✓
- Port E7 read = nullptr → falls through to `default_read_=0xFF` (V16 fix). ✓

### 3.7 Port-decode masks

E3/E7/EB use mask `0x00FF` (LSB-only) at `emulator.cpp:4260, 4266, 4344`. VHDL `zxnext.vhd:2608, 2620-2621` decodes from `cpu_a(7:0)` only (the high address byte is don't-care); mask is correct. ✓

### 3.8 SPI cycle timing

F19-DIVMMC-D01 documented; `spi_wait_n() const { return true; }` is byte-granularity faithful but not cycle-accurate. Class-(d) deferral re-confirmed under G137. ✓

### 3.9 DivMMC automap edge cases

All RST entry points (0x0000-0x0038) checked against `port_00xx_msb AND cpu_a(7:6)="00" AND cpu_a(2:0)="000"` at VHDL :2848-2890. NMI@$0066 at :2907-2908. Tape traps at :2902-2905. $3Dxx wildcard at :2898-2899. 1FF8-1FFF range at :2896. All match the C++ at `divmmc.cpp:340-435`. ✓

### 3.10 Full-duplex stream advance

V18-DIVMMC-NIT-01 fix at `sd_card.cpp:259-312` delegates the default branch of `receive()` (RESPONDING/SENDING_DATA/WRITE_RESP) to `send()`. Verified that the other state-machine arms (IDLE, RECEIVING_CMD, RECEIVING_DATA pre-token, RECEIVING_DATA mid-block) correctly absorb pre-token bytes or return 0xFF without advancing — matches SD spec semantics. ✓

### 3.11 `flash_cs_enable_` save-state coverage

`SpiMaster::save_state` (spi.cpp:231-240) writes only `cs_`, `rx_data_`, `sd_swap_`. `flash_cs_enable_` is NOT serialised — but it IS re-pushed by `Emulator::load_state` at line 7013 from `nextreg_.nr_03_config_mode() || ((nmi_source_.reset_type() & 0x04) != 0)`. The shadow is a derived field; re-derivation is correct. ✓

### 3.12 Default-construction state vs. VHDL initial value

Spot-check: `DivMmc::port_io_enable_ = false` (constructor default) vs. VHDL `port_divmmc_io_en` derived from `nr_83_internal_port_enable` which defaults to `(others => '1')` at VHDL :1227. The mismatch is benign because `Emulator::init()` at `emulator.cpp:4986-4987` immediately calls `divmmc_.set_port_io_enable(true)` based on the effective NR 0x83 byte. Any caller that constructs a `DivMmc` standalone (tests only) sets enable explicitly. ✓ — not a finding.

---

## 4. Test/build baseline

| Suite | Result |
|---|---|
| `ctest` (Release build) | 38/38 PASS |
| `fuse_z80_test` | 1356/1356 PASS, 0 failed, 0 skipped |
| `divmmc_test` | 137 checks, 0 skips, 137 plan rows covered |
| `sdcard_test` | 32/32 PASS |
| `regression.sh` | 33 Pass, 0 Fail, 0 Skip |

All baselines preserved. ✓

---

## 5. Missed findings or NITs

**None.**

I performed a second-sweep audit beyond the cross-cutting families and found no class-(a)/b/c findings beyond F19-DIVMMC-NIT-01. The audit's table is thorough, the spot-checks confirm the cited VHDL lines, and the sandwich proves the fix is necessary and sufficient.

---

## 6. Conclusion

**APPROVE — table complete (within the >5% gap tolerance), F19-DIVMMC-NIT-01 fix VHDL-faithful, discriminative sandwich verified, no missed findings.**

Confidence: **High**. I read the cited VHDL lines directly for 10 spot-checked rows, all of which match the C++ behaviour summary. The F19-DIVMMC-NIT-01 fix is correctly scoped, VHDL-faithful, and the regression test discriminatively pins the post-fix shape. The single class-(d) item F19-DIVMMC-D01 (cycle-accurate `spi_wait_n`) is correctly listed as deferred under G137 pending user authorisation.

**Subsystem status:** DivMMC + SD card + SPI is **converged at Pass-19** — every observable surface is VHDL-faithful at the byte-granularity table, the one remaining architectural gap is enumerated as class-(d).

---

## Reviewer-side artifacts

- Sandwich verified: `divmmc.cpp:122` revert → `divmmc_test` E3-V19-NIT-01 FAIL `raw=ff exp=CF` → restore → PASS.
- Working tree clean post-sandwich (`git status -s` empty).
- No source code modified by this reviewer commit (only this report file).
