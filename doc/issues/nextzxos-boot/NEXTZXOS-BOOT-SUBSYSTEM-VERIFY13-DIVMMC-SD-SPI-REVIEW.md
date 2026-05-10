# Task 2 — Pass-13 verify-audit: DivMMC + SD + SPI subsystem — independent review

**Branch**: `task2/verify13-divmmc-sd-spi-reviewer` (off audit HEAD `6cdb0c9`)
**Worktree**: `.claude/worktrees/task2-verify13-divmmc-sd-spi-reviewer`
**Mode**: independent review (audit report + audit commits only; prior pass reports NOT consulted)
**Oracle**: VHDL `cores/zxnext/src/{device/divmmc.vhd, serial/spi_master.vhd, zxnext.vhd}` + SD Physical Layer Simplified Spec v6.00.

## Verdict

**APPROVE.**

The audit's single class-(c) finding (V13-DIVMMC-01) is spec-correct,
the fix is minimal and tightly scoped, the math is sound, the
discriminative tests both fail with the fix reverted and pass with the
fix in place, and the spot-checked defensible-zero claims line up with
the VHDL / SD-spec text. Zero NITs.

## Mission outcome at a glance

| Step | Result |
|------|--------|
| SD-spec § 7.3.2.1 R1 bit 6 = PARAMETER_ERROR | **confirmed** |
| SD-spec § 4.3.4 data phase conditional on R1=0x00 | **confirmed** |
| Bounds check correctness (`byte_addr + 512 > file_size_`) | **correct, no off-by-one, no overflow** |
| Past-EOF behaviour (R1=0x40 only, no `pending_write_after_r1_`) | **correct** |
| In-bounds CMD24 unchanged (R1=0x00 → data phase → 0x05) | **confirmed unchanged** |
| Discriminative SD-21 (revert fix → FAIL) | **confirmed (r1_wr=0 instead of 0x40)** |
| Discriminative SD-25 (revert fix → FAIL) | **confirmed (got_cmd13=0, FSM stuck in RECEIVING_DATA)** |
| Restored fix → SD-21 + SD-25 PASS | **confirmed** |
| Release-mode build | clean |
| `ctest --test-dir build` | **38 / 38 PASS** |
| `fuse_z80_test build/test/fuse` | **1356 / 1356 PASS** |
| `sdcard_test` | **26 / 0 / 0** |
| Defensible-zero spot-checks (5 areas) | all matched VHDL / spec |

## 1. SD-spec claim verification

### § 7.3.2.1 — R1 layout, Table 7-9

R1 in SPI mode is a 1-byte response with the following bits (msb→lsb):

```
bit 7 = 0 (start bit)
bit 6 = PARAMETER_ERROR     ← "argument was out of the allowed range"
bit 5 = ADDRESS_ERROR       ← "misaligned address that did not match block length"
bit 4 = ERASE_SEQUENCE_ERROR
bit 3 = COM_CRC_ERROR
bit 2 = ILLEGAL_COMMAND
bit 1 = ERASE_RESET
bit 0 = IN_IDLE_STATE
```

The audit's claim "bit 6 = PARAMETER_ERROR" is correct. R1=0x40
exactly encodes this single error bit.

There is in principle a defensible alternative reading: argument-out-
of-range for an addressing-aligned sector COULD instead set bit 5
(ADDRESS_ERROR). However, the SD spec is explicit that bit 5 is for
*misalignment* (e.g. CMD16 mismatched WRITE_BL_LEN), whereas bit 6 is
the catch-all for "argument out of range". Past-EOF is canonically
bit 6 — confirmed by the fact that V12-DIVMMC-04 already settled
this for CMD17/18 with the same encoding. Audit's choice is
spec-faithful and consistent with the prior fix.

### § 4.3.4 — Data Write Sequence

The spec mandates: "If the response (R1) indicates a non-zero error
status, the host SHALL NOT proceed to the data phase." This makes the
data phase conditional on R1=0x00. Audit's claim is correct, and the
fix's "do not arm `pending_write_after_r1_`" implements it exactly.

## 2. Fix correctness

### Bounds check

```cpp
if (byte_addr + 512 > file_size_) { queue_r1(0x40); return; }
```

* `byte_addr = static_cast<uint64_t>(sector) * 512` — `sector` is a
  `uint32_t`, so max byte_addr = (2^32 - 1) * 512 ≈ 2.2 TB.
* `byte_addr + 512` adds 512 in `uint64_t` arithmetic — no overflow
  since max possible value ≈ 2.2 TB ≪ 2^64.
* `file_size_` is declared `uint64_t file_size_ = 0;` in `sd_card.h:150`.
* The boundary case `byte_addr + 512 == file_size_` (i.e. last full
  sector exactly aligned) is **allowed** (`>` not `>=`) — last sector
  inclusive. This matches the symmetric check in `cmd17_read_single_block`
  (sd_card.cpp:566) and `cmd18_read_multiple_block` (sd_card.cpp:614).
  All three commands now use byte-for-byte identical comparators. **No
  off-by-one, no overflow, symmetric with CMD17/18 past-EOF (V12-DIVMMC-04).**

### Past-EOF response shape

* `queue_r1(0x40)` queues the canonical NCR(0xFF) + R1(0x40) sequence
  via the existing helper, which sets `state_ = RESPONDING` with
  `resp_buf_ = {0xFF, 0x40}` and `resp_idx_=0`.
* Critically, `pending_write_after_r1_` is **not** set, so when send()
  finishes emitting the 2-byte R1 sequence, the FSM falls back to IDLE
  per the standard `RESPONDING` → IDLE transition (sd_card.cpp:268-281),
  not to RECEIVING_DATA.
* The `return;` short-circuit prevents the in-bounds path's `queue_r1(0x00); ... pending_write_after_r1_ = true;` from running.

### In-bounds case unchanged

The post-fix code for the in-bounds branch is byte-for-byte identical
to the pre-fix code (only renaming `static_cast<uint64_t>(sector) * 512`
into the `byte_addr` local for the new bounds check):

```cpp
queue_r1(0x00);
data_idx_ = 0;
data_crc_count_ = 0;
data_token_received_ = false;     // V12-DIVMMC-06
pending_write_after_r1_ = true;
```

Verified by re-running SD-14 / SD-21 in-bounds regression (resp_ok=0x05).

### V12-DIVMMC-02 0x0D-token branch becomes dead

The audit notes that the post-data-phase `byte_addr + 512 > file_size_`
check (sd_card.cpp:324) becomes dead defensive code after V13. I
checked: that branch is in `receive()` at the RECEIVING_DATA → WRITE_RESP
transition. After V13 it is unreachable from any in-bounds CMD24 (which
won't satisfy the predicate), but it is correctly retained as a defence-
in-depth — if any future regression accidentally arms
`pending_write_after_r1_=true` on a past-EOF CMD24, the 0x0D fallback
would fire. **This is the right call.**

## 3. Discriminative tests

I cloned the post-fix `sd_card.cpp`, then **reverted only the V13 bounds
check + R1=0x40 short-circuit block** (lines 654-684 of the post-fix
file), keeping all V12 work, the new test SD-25, and the updated SD-21
in place. Then rebuilt sdcard_test only.

**Reverted state result**:

```
FAIL SD-21: ... [r1_wr=0 r1_ok=0 resp_ok=5]
FAIL SD-25: ... [r1=0 got_cmd13=0 r1_cmd13=255 status_cmd13=255]

Total:   26  Passed:   24  Failed:    2  Skipped:    0
```

* **SD-21** — `r1_wr=0` (vs expected 0x40): pre-V13 the past-EOF CMD24
  returned R1=0x00, exactly the V12-DIVMMC-02 shape. The in-bounds case
  still passes (`resp_ok=5`), so the test correctly isolates the V13
  delta from the V12-DIVMMC-02 / SD-14 regression guards.
* **SD-25** — `got_cmd13=0`: pre-V13 the FSM stays in RECEIVING_DATA after
  R1=0x00, the follow-up CMD13 first byte (0x4D) is absorbed as
  `data_block_[0]`, no command match, no R1 emitted, host reads 0xFF
  forever. **This is exactly the failure the test was designed to surface.**

I then restored the post-fix `sd_card.cpp` and re-ran:

```
Total:   26  Passed:   26  Failed:    0  Skipped:    0
```

**Both tests are discriminative for V13-DIVMMC-01. ✓**

## 4. Full Release-mode test suite

```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
$ cmake --build build -j$(nproc)         # clean, no warnings beyond preexisting
$ ctest --test-dir build --output-on-failure
  100% tests passed, 0 tests failed out of 38
$ ./build/test/fuse_z80_test build/test/fuse
  Total: 1356  Passed: 1356  Failed: 0  Skipped: 0
$ ./build/test/sdcard_test
  Total:   26  Passed:   26  Failed:    0  Skipped:    0
```

**Zero FAILs across the full test surface.**

## 5. Defensible-zero spot-checks

I picked five areas from the audit's "Defensible-zero areas" list and
confirmed against the VHDL / SD-spec text:

### 5.1 `spi_master.vhd:74` (miso_dat init = 0x00)

VHDL source confirms:

```vhdl
signal miso_dat : std_logic_vector(7 downto 0) := (others => '0');
```

Combined with `i_reset => '0'` at zxnext.vhd:3285 (the `spi_master_mod`
instantiation hardwires reset low), the only initial value is the
signal-default 0x00. The post-V12-NIT `SpiMaster::rx_data_ = 0x00;`
init at construction matches exactly. **Defensible-zero confirmed.**

### 5.2 `divmmc.vhd:88-101` (rom_en/ram_en/ram_bank/rdonly equations)

I read divmmc.vhd lines 88-101. Equations cross-checked one-by-one
against `DivMmc::is_active` / `is_ram_mapped` / `is_read_only` /
`ram_page_for` (paraphrased here, both VHDL and audit text match exactly):

```
rom_en   = page0 AND (conmem OR automap) AND NOT mapram
ram_en   = (page0 AND (conmem OR automap) AND mapram) OR (page1 AND (conmem OR automap))
ram_bank = page0 ? "0011" : i_divmmc_reg(3:0)
rdonly   = page0 OR (mapram AND ram_bank=3)
```

**Defensible-zero confirmed.**

### 5.3 `divmmc.vhd:105-114` (button_nmi FF clear chain)

I read divmmc.vhd lines 105-114:

```vhdl
if rising_edge(i_CLK) then
   if i_reset='1' OR i_automap_reset='1' OR i_retn_seen='1' then
      button_nmi <= '0';
   elsif i_divmmc_button='1' then
      button_nmi <= '1';
   elsif automap_held='1' then
      button_nmi <= '0';
   end if;
end if;
```

Audit's "automap_held=1 → button_nmi=0" clause is correctly placed
inside `check_automap` per Pass-8 fix. **Defensible-zero confirmed.**

### 5.4 `zxnext.vhd:3308-3326` (port_e7_reg writer / decoder)

I read zxnext.vhd lines 3308-3326:

```vhdl
elsif port_e7_wr='1' then
   if cpu_do(1:0)="10"        then port_e7_reg <= "111111" & not nr_0a_sd_swap & nr_0a_sd_swap;
   elsif cpu_do(1:0)="01"     then port_e7_reg <= "111111" & nr_0a_sd_swap & not nr_0a_sd_swap;
   elsif cpu_do=X"FB"         then port_e7_reg <= X"FB";
   elsif cpu_do=X"F7"         then port_e7_reg <= X"F7";
   elsif cpu_do=X"7F" AND ((nr_03_config_mode='1') OR (nr_02_reset_type(2)='1')) then port_e7_reg <= X"7F";
   else                            port_e7_reg <= (others=>'1');
```

I cross-checked `SpiMaster::write_cs` (spi.cpp:120-155). It implements:
* `(val & 0x03) == 0x02` → SD0/SD1 with sd_swap inversion (matches strict bits-1:0="10" decode + sd_swap-only-on-SD-arms).
* `(val & 0x03) == 0x01` → ditto.
* `val == 0xFB` → 0xFB literal (RPI0 raw match).
* `val == 0xF7` → 0xF7 literal (RPI1 raw match).
* `val == 0x7F && flash_cs_enable_` → 0x7F (Flash, gated on the
  `nr_03_config_mode OR nr_02_reset_type(2)` composite).
* else → 0xFF.

**Defensible-zero confirmed.**

### 5.5 `sd_rom_extractor.cpp` BPB sanity guards

I read sd_rom_extractor.cpp:114-146. Guards:
* `bytes_per_sector ∈ {512, 1024, 2048, 4096}` — exact match.
* `sectors_per_cluster != 0 && (n & (n-1)) == 0` — power-of-two test.
  Audit says "[1, 128]"; since the field is `uint8_t` (max 255) and the
  largest power of two representable in `uint8_t` is 128, the check is
  implicitly bounded to {1,2,4,8,16,32,64,128}. **Defensible-zero
  confirmed.**
* `num_fats != 0 && fat_size != 0 && root_cluster >= 2` — match.

## Issues / NITs

**None.** The fix is minimal, mathematically sound, symmetric with the
existing CMD17/18 past-EOF treatment, and the tests are genuinely
discriminative.

The `[REVIEWER REVERT TEST]` placeholder I introduced during the
discriminative-test cycle was **restored** to the post-fix code before
the test suite re-run. `git status` in the worktree shows only the
untracked `roms/` symlink (created so the build could find
`nextboot.rom`); no source-file modifications.

## Final verdict

**APPROVE.**

V13-DIVMMC-01 closes the last lingering CMD24 / CMD17 / CMD18 past-EOF
asymmetry. SD-21 + SD-25 are both discriminative. The full Release-mode
test suite is 38 + 1356 + 26 = 1420 tests, zero FAILs.
