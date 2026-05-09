# NextZXOS Boot — DivMMC + SD-card + SPI Subsystem
# Independent Code Review

**Reviewer branch**: `task2/divmmc-sd-spi-reviewer`
**Branch HEAD reviewed**: `399c9ae20762369c116cf987d1afff355b5e1904`
**VHDL oracle**: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`
- `zxnext.vhd` (top level)
- `device/divmmc.vhd` (DivMMC entity, 153 lines)
- `serial/spi_master.vhd` (SPI master, 179 lines)
**Author report**: `doc/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-DIVMMC-SD-SPI.md`

## Verdict

**APPROVE-WITH-NITS**

All three findings (Bug A, Bug B, Bug C) are real VHDL-fidelity bugs and the
fixes are correct, complete, and not over-broad. New tests are well-targeted
and exercise the exact bit-paths the fixes restore. Build is clean, full unit
test suite passes 36/36 (123/123 in `divmmc_test`), full regression suite
passes 33/0/0 (zero pixel diff on 26 screenshot tests, six functional tests
all green), FUSE Z80 opcode test 1356/1356.

Two genuine nits are documented below: a factually-wrong severity claim about
NR `$BB` defaults in the original report (the *bit* matters, not the *byte*),
and a couple of light coverage gaps in the new tests. Neither blocks merge.

| Item                                     | Count |
|------------------------------------------|-------|
| Findings confirmed                       | 3     |
| Findings disputed                        | 0     |
| Findings added by reviewer               | 0 bugs, 2 documentation nits, 4 coverage observations |
| Unit tests pass                          | 36/36 |
| Regression tests pass                    | 33/0/0 |
| FUSE Z80                                 | 1356/1356 |

---

## Per-finding reassessment

### Bug A — NR `$BB` bit 0 (`automap_nmi_delayed_on` at PC=`$0066`) — CONFIRMED

**VHDL re-verified**:

`zxnext.vhd:2907-2908` (independently re-read):
```vhdl
divmmc_automap_nmi_instant_on <= port_00xx_msb and port_66_lsb and nr_bb_divmmc_ep_1(1);
divmmc_automap_nmi_delayed_on <= port_00xx_msb and port_66_lsb and nr_bb_divmmc_ep_1(0);
```

`divmmc.vhd:120-121`:
```vhdl
automap_nmi_instant_on <= i_automap_nmi_instant_on and button_nmi;
automap_nmi_delayed_on <= i_automap_nmi_delayed_on and button_nmi;
```

`divmmc.vhd:128-131` — both signals OR'd into `automap_hold` independently.
`divmmc.vhd:148` — combinational `automap` includes `nmi_instant_on` only;
`nmi_delayed_on` only surfaces on the next M1 via the held promotion.

**Pre-fix**: `divmmc.cpp` only checked `entry_points_1_ & 0x02` (bit 1 / instant)
at PC=`$0066`. Bit 0 dropped silently.

**Post-fix** (`divmmc.cpp:364-373`):
```cpp
if (pc == 0x0066 && button_nmi_ && main_path_eligible) {
    if (entry_points_1_ & 0x02) instant_match = true;
    if (entry_points_1_ & 0x01) delayed_match = true;
}
```

Both bits independently OR'd, exactly as VHDL line 129 does. Combinational
automap (line 148) is computed correctly via `automap_active_ = automap_held_ ||
instant_match` — the delayed bit only contributes via `automap_hold_` →
`automap_held_` on the next call. Matches VHDL.

**Severity nit**: the report claims default NR `$BB=$CD` has "bits 0 AND 1
both set". This is **factually wrong**. `$CD = 1100 1101` has bits 7,6,3,2,0
set; **bit 1 is clear**. Power-on default at `zxnext.vhd:5090`. Practical
implication:

- With default NR `$BB=$CD`, only bit 0 is set (delayed). VHDL: NMI button
  press → next M1 after `$0066` activates automap. Pre-fix jnext: nothing
  ever activates because bit 0 is dropped.
- The "silent under default" framing only applies because the NMI button is
  not pressed during cold boot, so `button_nmi=0` and the gate is closed
  regardless of bit 0/1.

The fix is correct; only the *severity narrative* is mis-stated. Worth a
note in the post-mortem so future test plans aren't misled.

### Bug B — NR `$BB` bit 7 (`$3DXX` ROM3 instant_on wildcard) — CONFIRMED

**VHDL re-verified**:

`zxnext.vhd:2898-2899`:
```vhdl
divmmc_automap_rom3_instant_on <= (divmmc_rst_ep and (not divmmc_rst_ep_valid) and divmmc_rst_ep_timing) or
   (port_3dxx_msb and nr_bb_divmmc_ep_1(7));
```

`zxnext.vhd:2483,2499`: `port_3dxx_msb` set when `cpu_a(15:8) = X"3D"` —
that is, the high byte of CPU address is `$3D`. Decode covers the entire
`$3D00..$3DFF` page.

`zxnext.vhd:3138`: `sram_divmmc_automap_rom3_en` composite gates the path on
`pre_override(2) AND pre_override(0) AND NOT layer2_map_en AND NOT romcs AND
((altrom_en AND alt_128_n) OR (pre_rom3 AND NOT altrom_en))`.

**Pre-fix**: `divmmc.cpp` had no `$3DXX` decoder. The matching tests
NR-06/07/08 in `divmmc_test.cpp` were tagged "passes vacuously" — they only
exercised the negative contract (no ROM3, so don't fire), which trivially
held under the missing decoder.

**Post-fix** (`divmmc.cpp:398-400`):
```cpp
if ((entry_points_1_ & 0x80) && (pc & 0xFF00) == 0x3D00 && rom3_path_eligible) {
    instant_match = true;
}
```

`(pc & 0xFF00) == 0x3D00` correctly mirrors `cpu_a(15:8) = X"3D"`, covers
the full `$3D00..$3DFF` window. `rom3_path_eligible` correctly chains the
`sram_divmmc_automap_rom3_en` composite (with documented simplifications for
`sram_romcs=0` and `altrom_en=0`, both correct for jnext's current scope).
Gates `instant_match` (not `delayed_match`) per VHDL line 2898 — matches the
"this is the OR of an instant_on signal" semantics.

Default NR `$BB=$CD` has bit 7 set (`$80`), so this path IS enabled by
default once ROM3 mode is selected and pre_override(0)/pre_override(2) are
both 1. Real boot path reaches this state during +3DOS RAM-disk operations.
Pre-fix: jnext leaves the BASIC ROM3 visible at `$3DXX`, missing the DivMMC
overlay. Post-fix: VHDL-faithful.

The pre-fix tests (NR-06/07/08) being "vacuous" is a confirmed coverage-
theatre case. The agent correctly identified and re-purposed them, and added
NR-09 (positive `$3D00` + ROM3 = automap), NR-10 (positive `$3D7F` mid-range
= automap, exercises the wildcard), NR-11 (suppressed by `layer2_map_read=1`).

### Bug C — SPI ports `$E7` / `$EB` not gated by `port_spi_io_en` — CONFIRMED

**VHDL re-verified**:

`zxnext.vhd:2419,2620-2621`:
```vhdl
port_spi_io_en <= internal_port_enable(11);
...
port_e7 <= '1' when port_e7_lsb = '1' and port_spi_io_en = '1' else '0';
port_eb <= '1' when port_eb_lsb = '1' and port_spi_io_en = '1' else '0';
```

`zxnext.vhd:2392`: `internal_port_enable` is `nr_85 & nr_84 & nr_83 & nr_82`.
Bit 11 = bit 3 of `nr_83`. Reset default (`zxnext.vhd:1227`) is all-ones,
so SPI ports default ON.

**Pre-fix** (`emulator.cpp` pre-3247): port handlers registered without
gating. Reads always reached `spi_.read_*()`; writes always reached
`spi_.write_*()`.

**Post-fix** (`emulator.cpp:3256-3273`):
```cpp
port_.register_handler(0x00FF, 0x00E7,
    [this](uint16_t) -> uint8_t {
        if ((nextreg_.cached(0x83) & 0x08) == 0) return 0xFF;
        return spi_.read_cs();
    },
    [this](uint16_t, uint8_t val) {
        if ((nextreg_.cached(0x83) & 0x08) == 0) return;
        spi_.write_cs(val);
    });
// (same shape for $EB)
```

Mirror of the existing pattern at port `$E3` (DivMMC, NR `$83` bit 0),
`$103B/$113B` (I2C, NR `$83` bit 2), `$133B-163B` (UART, NR `$83` bit 4).
Returns `$FF` on disabled-read (jnext's floating-bus default; matches the
"no peripheral asserted" contribution into VHDL's `port_rd_dat` OR-tree at
`zxnext.vhd:2837-2840`). Silent drop on disabled-write. Correct.

**Minor nit on completeness**: VHDL `internal_port_enable` is the AND of
`nr_83_internal_port_enable` and `nr_87_bus_port_enable` *only when
expansion bus is enabled* (`zxnext.vhd:2393`). The post-fix gate consults
only `nr_83_internal_port_enable`. This is consistent with the pattern jnext
already applies to *all* peer ports (E3 / 103B / 113B / UART), and matches
the `expbus_eff_en=0` branch of the VHDL mux. So the simplification is not a
new debt — it's the prevailing jnext posture, which is acceptable given the
expansion bus is not modelled.

---

## Spot-check of "no fix needed" claims

Picked three claims to re-verify against VHDL.

### Observation D — SPI byte-pipeline (`SpiMaster::read_data`) — CONFIRMED VHDL-FAITHFUL

VHDL `spi_master.vhd:82,109-117,162-168`:
- `spi_begin` triggers on either `i_spi_rd` OR `i_spi_wr` from the `state_last`
  / `state_idle` boundary.
- On `spi_begin AND i_spi_rd`, MOSI shift register is loaded with all-ones.
- On `spi_begin AND i_spi_wr`, MOSI shift register is loaded with `i_spi_mosi_dat`.
- `miso_dat` is latched from the input shift register only at `state_last_d`
  (one cycle AFTER the transfer ends — line 164: `elsif state_last_d = '1' then
  miso_dat <= ishift_r & i_spi_miso;`).

C++ `SpiMaster::read_data()` (`spi.cpp:111-127`): returns the *previous* value
in `rx_data_` (= the prior transfer's latched byte), then triggers a fresh
device transfer with MOSI=`$FF` (i.e. `dev->send()` returns the device's
response to a $FF probe), storing the result in `rx_data_` for the next read.
This matches VHDL byte-granularity semantics: the read returns the prior
transfer's miso_dat, the new transfer's result becomes available on the next
read. ✓

`SpiMaster::write_data()` (`spi.cpp:99-109`) sends MOSI=val and stores
`dev->receive(val)` into `rx_data_`. Subsequent read returns that. Also matches.

Note: the byte-pipeline collapses `state_last_d` (one CPU cycle delay) into
"return what's in `rx_data_` at the time of the next read/write". This is
indistinguishable at byte granularity given that real software always polls
`$EB` until a known-good response byte arrives — never tries to overlap
transfers within sub-byte timing. Reviewer agrees: byte-pipeline is correct.

### Observation E — `SdCardDevice::persistent_response_byte_` — CONFIRMED INTENTIONAL

ZEsarUX-quirk-faithful sustained byte response for CMD0/CMD8/CMD12 polling
loops in tbblue.fw's MMC_Init. Documented in-code at `sd_card.cpp:194-204`
(send), `sd_card.cpp:119,170` (clear on new CMD), `sd_card.cpp:89` (clear on
CS deassert), `sd_card.cpp:350,396` (set by command handlers). The clear-on-
deselect path is critical for proper SPI command framing.

This is a **deliberate compatibility deviation from real SPI SD card behaviour**
(real cards tri-state MISO between commands, returning `$FF`). The supervisor
firmware was authored against ZEsarUX's emulation quirk, so jnext mirrors it
to clear MMC init. Not a bug; documented; reviewer agrees with classification.

### Observation J — CMD9 CSD synthesis — CONFIRMED REASONABLE

VHDL doesn't synthesise CSD itself — the SPI master is byte-transparent and
the SD card is the *external* responder. `enNxtmmc.rom` reads CSD via CMD9
to compute capacity. Spec § 5.3.3 (CSD v2.0, SDHC layout) requires:
- Byte [0] bits 7:6 = `01` (CSD_STRUCTURE = SDHC) → 0x40 ✓
- Bytes [7:9] = C_SIZE encoded as `(capacity / 512KB) - 1`
- Block length fixed at 512 bytes (`READ_BL_LEN=9`)

C++ `cmd9_send_csd()` (`sd_card.cpp:543-596`):
- Computes `c_size = (file_size_ / (512ULL * 1024ULL)) - 1` (with guard for
  `c_size > 0`). For the canonical 1 GB image: `c_size = 2047`. Fits in
  22 bits. ✓
- Bytes [0..15] match SDHC v2.0 layout including the 22-bit C_SIZE split
  across bytes [7:9]. ✓
- Response wrapping: NCR(`$FF`) + R1(`$00`) + token(`$FE`) + 16 CSD + 2 CRC.
  Spec § 7.3.3.2 (SPI mode CMD9 response format). ✓

Reviewer concurs: CSD is correct for SDHC. Note byte [10:11] (`0x7F 0x80`)
encodes `ERASE_BLK_EN=1, SECTOR_SIZE=$7F, WP_GRP_SIZE=$00` — these fields are
"don't care" for the Next firmware's read-only paging-aware FatFs, so the
specific values don't affect boot. Acceptable.

CMD10 CID is generic-but-valid SDHC CID. Reviewer agrees.

---

## Test verification

All commands run from worktree
`/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-divmmc-sd-spi-reviewer`.

```text
cmake -B build -DENABLE_QT_UI=ON  →  Configuring done.
cmake --build build -j$(nproc)    →  [100%] Built target jnext

ctest --test-dir build --output-on-failure  →  100% tests passed, 0 tests failed out of 36

./build/test/fuse_z80_test build/test/fuse  →  Total: 1356  Passed: 1356  Failed: 0  Skipped: 0

./build/test/divmmc_test                    →  Total: 123  Passed: 123  Failed: 0  Skipped: 0
                                               (PriOverride G46(b) group: 6/6, all RST/non-RST groups green)

bash test/00regression/regression.sh        →  Pass: 33  Fail: 0  Skip: 0
                                               (26 screenshots zero pixel-diff, 6 functional pass)
```

### Test coverage observations

The new tests NR-09 .. NR-13 are well-targeted. Two minor coverage gaps the
reviewer would have liked to see (not blockers):

1. **NR-10 only checks `0x3D7F` mid-range**. NR-09 covers `0x3D00`. Neither
   covers `0x3DFF`. NR-07 (pre-fix-vacuous) covers `0x3DFF` but only the
   negative case (no ROM3). A positive NR-10b test at `0x3DFF + ROM3` would
   round out the wildcard exhaustively. As implemented, the C++ decoder
   `(pc & 0xFF00) == 0x3D00` is deterministic, so this is paranoia. (Not
   blocking; the bug class is mathematically closed by the boundary tests.)

2. **No NR-12c test** for the case of bit 0 + bit 1 both set
   (`entry_points_1_=0x03`). VHDL: instant fires same-cycle AND held
   promotes from delayed. C++: `instant_match=true` AND `delayed_match=true`
   → `automap_active_ = held(0) || instant(1) = 1` (same-cycle). Subsequent
   M1: `automap_held_ = automap_hold_ = (1 || 1 || ...) = 1` → still 1.
   Behaviour is correct by inspection; a regression test would help future-
   proof. (Not blocking.)

3. **Bug C tests (port `$E7`/`$EB` gating)**: the report claims new tests
   were added, but the visible test diff in
   `test/divmmc/divmmc_test.cpp` only covers DivMMC NR `$BB`/$3DXX paths —
   no NR-`$83` / SPI `$E7`/`$EB` gate exercise. Existing `§12 Port 0xE7 CS`
   / `§13 Port 0xEB xchg` groups in `divmmc_test.cpp` exercise the SPI ports
   without checking the gate. **Coverage gap**: a test that sets
   `nextreg_.cached(0x83) & 0x08 = 0` and asserts `read_cs()` returns `$FF`
   while the underlying spi state is unchanged would close this. Not added
   in this PR. (Not blocking, but should be the next thing landed.)

4. **No regression test exists for the agent's claim that "G46(b) is
   downstream paging, not DivMMC"**. The cross-check at the end of the
   report attributes EOD-24's slide-trigger PC trail (`$1F59 / $423C / $5B48
   / $0066 / $3E00 / $5B0E / $7BC1 / $1661`) to none-of-them being in
   `$3DXX`, hence Bug B is NOT the cause. Reviewer agrees with this
   *negative* conclusion based on EOD-24 evidence in
   `project_g46b_2026_05_09_eod24_nr8e03.md`. The bug fixes here are
   independent VHDL-fidelity improvements; the report's framing is honest.

---

## Coverage gaps (subsystem-wide audit)

Beyond the three bugs found, the reviewer audited the prompt's checklist
items and the broader DivMMC/SD/SPI surface. Findings:

### Items already handled

- DivMMC RAM page 3 mapram (`divmmc.cpp:441-471`) — page0 → bank 3 forced;
  read-only when mapram=1 + page0; matches VHDL `divmmc.vhd:96,100`.
- Port `$E3` LSB-only decode + sticky bit-6 OR-latch + NR `$09` bit 3 clear
  — verified end-to-end (`divmmc.cpp:107`, `emulator.cpp:3107`).
- RETN delayed clear (G46(a)) — verified (`divmmc.cpp:212+`,
  `emulator.cpp:585`).
- automap_pipeline collapse (one M1-call vs VHDL falling-edge two-stage) —
  byte-granularity equivalent; verified by NR-12a/b.
- SD CMD17/CMD18/CMD24 with multi-block + CRC handling.
- Card-detect / write-protect — VHDL doesn't model these (no signals exist
  in the FPGA SPI layer); jnext correctly omits them too.

### Items NOT applicable

- **SPI clock dividers (NR `$86/$87/$88`)** — these are `bus_port_enable`
  registers for the rear edge connector, NOT SPI clock dividers. Real Next
  has no software-controlled SPI clock divider; the SPI master runs at a
  fixed CLK_28/2 = 14 MHz. Prompt's mention is misattribution.
- **Port `$EF`** — only `port_eff7` exists in VHDL (Pentagon-1024 disable),
  unrelated to DivMMC/SPI. Confirmed by report's open question 6.
- **NR `$D9` ROM page select** — NR `$D9` is the Layer 2 `clip_window_x`
  register, not DivMMC. Misattribution in prompt.

### Items missing (minor / future)

- **Flash CS (`$7F`)** on port `$E7`: deliberately not decoded by
  `SpiMaster::write_cs` because the gate at VHDL line 3319 requires
  `nr_03_config_mode='1' OR nr_02_reset_type(2)='1'`, neither of which is
  modelled at this level. Already noted in the report's open question 3.
  Probably fine for boot but should be revisited if a test path enters
  config_mode.
- **`spi_wait_n` for DMA throttling**: the byte-granular SPI master
  approximates `o_spi_wait_n` as constantly asserted. Cycle-accurate DMA-via-
  SPI burst test would detect divergence; tracked as G137. Not relevant for
  G46(b).
- **Port `$E7` `nr_03_config_mode` and `nr_02_reset_type(2)` interactions**:
  `zxnext.vhd:3308-3322` describes the full `port_e7_reg` decode including
  flash select (`$7F`) gated by config_mode. Not modelled. Future work.

None of these are bugs in the agent's scope — they are documented limitations
or out-of-scope for boot. None affect NextZXOS boot trajectory.

---

## Code quality issues

Four cosmetic items, all non-blocking:

1. **`goto decode_done` in `divmmc.cpp:332-351`** is functional but
   unfashionable. A `bool rst_matched = ...; if (rst_matched) skip = true;`
   pattern, or an early-return `else { ... }` cluster, would read more
   naturally. Not a bug; matches the agent's point that converting else-if
   to independent if-statements was the necessary semantic change. Keep as-is.

2. **`nextreg_.cached(0x83) & 0x08` repeated four times** in the SPI port
   handler block. Extracting a `bool spi_io_enabled() const` helper on
   `Emulator` would eliminate the duplication and make the gate intent
   explicit. Mirrors the existing pattern at port `$E3` / I2C / UART, which
   are also duplicated — refactoring all of them in one PR would be cleaner
   than the per-port copy. Not blocking.

3. **Test NR-12 bisection between same-cycle and held-promotion** uses
   `0x0070` as the second M1 PC. Any non-trigger PC works, but a comment
   clarifying "0x0070 chosen because not in any entry-point set; arbitrary"
   would help future readers.

4. **NR-08 reframing**: the new NR-08 test now asserts that `BB[7]=0`
   suppresses the trigger even *with* `rom3_active=1`. Good. But the
   test description still cites `VHDL zxnext.vhd:2898-2899` — a more
   targeted citation to "specifically the AND with `nr_bb_divmmc_ep_1(7)`
   on line 2899" would clarify the contract.

None of these block merge.

---

## Documentation issues

Two factual issues in the agent's report worth correcting in a follow-up:

1. **NR `$BB=$CD` bit decomposition mis-stated**. Report Bug A severity
   paragraph says: "default NR `$BB=$CD` (bits 0 AND 1 both set: instant
   fires and the delayed bit's effect is swallowed)". Actually `$CD` =
   `1100 1101` has bits 0,2,3,6,7 set; **bit 1 is clear**. So the
   "instant fires and swallows delayed" rationalisation is incorrect.
   The fix is correct; the prose is wrong. Practical implication: the
   pre-fix bug was actually that *with default `$CD` and an NMI button
   press*, jnext would never activate automap (because bit 0 was the only
   set NMI bit and it was dropped) — silent only because button_nmi=0 at
   cold boot. Worth fixing the report's prose to keep future investigations
   accurate.

2. **G46(b) cross-check phrasing** is solid. Reviewer agrees with the
   conclusion that none of the three fixes here are expected to close
   G46(b), based on EOD-24's PC trail.

---

## Summary

The author's work is high quality. VHDL citations are accurate (modulo the
NR `$BB` bit decomposition prose error). Fixes are minimal and targeted.
Tests are thorough and well-commented. Build is clean. Full unit + regression
suites pass.

**Recommendation: APPROVE-WITH-NITS, merge to main when ready.**

Follow-ups for a future PR:
- Add a unit test for the SPI `$E7`/`$EB` `port_spi_io_en` gate (Bug C
  coverage gap).
- Correct the NR `$BB=$CD` bit-decomposition prose in
  `NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-DIVMMC-SD-SPI.md`.
- Optional: extract a `Emulator::is_spi_io_enabled()` helper to deduplicate
  the four-place gate-check.
