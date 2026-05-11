# Pass-12 Blind Audit — DivMMC + SD + SPI Subsystem

**Branch**: `task2/verify12-divmmc-sd-spi`
**Worktree**: `.claude/worktrees/task2-verify12-divmmc-sd-spi`
**Base**: integration HEAD `df247c8`
**Date**: 2026-05-10

## Methodology

Blind audit of the DivMMC / SD-card / SPI-master subsystem. Forbidden
from reading any earlier `doc/issues/nextzxos-boot/*` document. VHDL
authoritative oracle, no probes, no scope creep, no push.

Files in scope:
- `src/peripheral/divmmc.{cpp,h}`
- `src/peripheral/sd_card.{cpp,h}`
- `src/peripheral/spi.{cpp,h}` (SpiMaster + SpiDevice base)
- `src/core/sd_rom_extractor.{cpp,h}` (host-side FAT32 reader)
- DivMMC / SPI port-write dispatch slice in `src/core/emulator.cpp`
- DivMMC NRs: $B8, $B9, $BA, $BB, $C0
- DivMMC trigger entry-points (RST $08 etc.)

VHDL anchors:
- `device/divmmc.vhd` (153 lines) — DivMMC paging + automap
- `serial/spi_master.vhd` (179 lines) — SPI byte-shift FSM
- `zxnext.vhd` integration: SPI master instantiation
  (`:3282-3298`), `port_e7` decode (`:3308-3326`), DivMMC integration
  (`:4111-4188`), MISO mux (`:3278-3280`), automap entry-point logic
  (`:2848-2912`).

## Pass-12 angles exercised

- Stateful FSM transitions (SD card init, SPI byte pump, automap arming)
- Concurrent-access semantics (Z80 writes mid-transfer)
- Boundary cases on SD image (end-of-image, fragmented FAT32, OOR cluster)
- DivMMC entry-point post-conditions (line-by-line vs VHDL :128-148)
- CMD response timing & correctness (R1, R2, R3, R7, data tokens)
- 0xFF idle-byte semantics during SPI deselect
- CRC computation correctness (MMC vs SD, ignored in our model — class-d)
- Voltage-window check (CMD8) corner cases
- CMD58 OCR fields
- Reset propagation (system reset vs FPGA-load semantics)
- conmem vs mapram precedence under different paging states
- DivMMC RST $08 trigger when CPU is HALTed
- Multi-byte SPI transfer interrupted by Z80 read mid-transfer

## Findings

### V12-DIVMMC-01 — `SpiMaster::reset()` clobbers `rx_data_` but VHDL miso_dat is never reset (class-(c))

**File**: `src/peripheral/spi.cpp` (line 68-69 pre-fix)

**VHDL anchor**: `zxnext.vhd:3282-3298` instantiates `spi_master_mod`
with `i_reset => '0'` HARDWIRED (line 3285, comment "hard reset done
through core load"). The internal `miso_dat` register at
`spi_master.vhd:159-168` has a reset clause:

```vhdl
process (i_CLK)
begin
   if rising_edge(i_CLK) then
      if i_reset = '1' then
         miso_dat <= (others => '1');
      elsif state_last_d = '1' then
         miso_dat <= ishift_r & i_spi_miso;
      end if;
   end if;
end process;
```

But with `i_reset` hardwired to `'0'` at the spi_master instantiation,
that clause **never fires**. `miso_dat` therefore retains its last
latched value across every system reset (only an FPGA bitstream load
can affect it).

**Pre-fix bug**: `SpiMaster::reset()` had `rx_data_ = 0xFF;` with the
comment "VHDL resets miso_dat to all ones". That comment was wrong —
the VHDL reset clause exists, but it's not connected to system reset in
the zxnext core. Pre-fix `Emulator::reset()` (called on every soft
reset / `NR 0x02 ← 0x01`) clobbered the previous-transfer MISO byte,
diverging from VHDL whenever firmware reads port 0xEB after a soft
reset before issuing a new SPI write.

**Practical impact on the boot path**: nil. Firmware always issues a
CMD before reading port 0xEB. Latent test/save-state-replay divergence,
not a boot-blocker.

**Fix**: removed the `rx_data_ = 0xFF;` line; the constructor still
member-initialises it to 0xFF for first-boot defaults. Cross-checked
against MX-04 (no slave selected returns 0xFF on first read) and ML-05
(first-read-after-reset returns 0xFF) — both still pass because the
member-init 0xFF carries through when no prior write has primed the
register.

**Test**: `divmmc_test.cpp` group_ss SS-16 — primes `rx_data_=0x42` via
a `write_data` against a MockSpiDevice that returns 0x42, then resets
the master and verifies the FIRST `read_data()` returns 0x42 (post-fix:
preserved). Pre-fix this would return 0xFF (clobbered).

**Class**: (c) — VHDL-faithfulness divergence, latent, no boot impact.
Resolved.

#### V12-DIVMMC-01-NIT — first-boot default value drift vs VHDL signal-init (Pass-12 fix-of-reviewer, 2026-05-10)

The reviewer (HEAD `c663b3d`, APPROVE-WITH-NITS) flagged a related NIT
on the same VHDL anchor as V12-DIVMMC-01: the constructor's member-init
for `SpiMaster::rx_data_` was `0xFF`, but VHDL `spi_master.vhd:74`
declares the signal-init as

```vhdl
signal miso_dat : std_logic_vector(7 downto 0) := (others => '0');
```

i.e. the FPGA-bitstream initial value is **0x00**. With `i_reset`
hardwired to `'0'` at `zxnext.vhd:3285` (V12-DIVMMC-01 anchor), the
synchronous-reset clause at `spi_master.vhd:159-168`
(`miso_dat <= (others => '1')` on `i_reset='1'`) NEVER fires, so the
actual VHDL behaviour on first power-up is governed by the signal-
declaration init at line 74 — `0x00`. Real hardware therefore surfaces
0x00 on the first port-0xEB read before any SPI transfer has completed.
Pre-fix C++ surfaced 0xFF.

**Practical impact**: nil. Every known firmware (TBBlue, NextZXOS,
esxdos) issues a CMD before the first port-0xEB read, so the first
sampled value is always overwritten by the CMD's R1 (or whatever else
the slave drives). VHDL-faithfulness was the only thing wrong.

**Fix**: `src/peripheral/spi.h:110` — change member-init from `0xFF` to
`0x00` to match VHDL `miso_dat` signal-init at `spi_master.vhd:74`. Per
the "0 pending of any class" honest-convergence rule the NIT is now
resolved (not deferred).

**Disc test SS-17** in `test/divmmc/divmmc_test.cpp` — instantiate a
fresh `SpiMaster` with NO attached device and NO prior write_data().
Read port 0xEB. With no active device, `read_data()` returns the
pre-existing `rx_data_` (the member-init value) and refreshes
`rx_data_` to 0xFF for the next read. Post-fix the first read returns
0x00; pre-fix returned 0xFF. Pre-revert SS-17 FAILs (got=0xFF expected
0x00). Discriminative confirmed.

**Existing-row alignments**: three pre-existing rows
(SX-03, SX-04, ML-05) and one MX-04 had been written against the wrong
"miso_dat reset = 0xFF" claim. They happened to pass because the pre-
fix member-init was also 0xFF. Updated to assert 0x00 (the VHDL signal-
init), matching the same VHDL root cause. MX-04 additionally now primes
the pipeline before reading, so the "no device → MISO=0xFF" claim is
observed through the proper state_last_d latch path rather than the
C++ short-circuit. Same VHDL root cause as V12-DIVMMC-01: `i_reset`
hardwired '0' means the synchronous-reset clauses for both `miso_dat`
and `ishift_r` never fire. Pre-revert: 4 FAILs (SS-17 + SX-03 + SX-04 +
ML-05). Post-fix: all 136 divmmc rows pass.

**Class**: (c) — VHDL-faithfulness divergence at first-boot default;
no boot impact, no save-state impact, no firmware-visible impact.
Resolved.

---

### V12-DIVMMC-02 — `CMD24` past-EOF returns "data accepted" (0x05) when VHDL/SD-spec mandates "write error" (0x0D) (class-(b))

**File**: `src/peripheral/sd_card.cpp` (lines 178-192 pre-fix)

**Spec anchor**: SD Physical Layer Simplified Spec § 7.3.3.3 Data
Response Token. Format `0bxxx0_sss1` where status `sss` is:
- `010` = data accepted              → byte `0x05`
- `101` = data rejected (CRC error)  → byte `0x0B`
- `110` = data rejected (write error)→ byte `0x0D`

**Pre-fix bug**: After receiving 512 data bytes + 2 CRC bytes for
CMD24, the code emitted `resp_buf_ = { 0x05 }` (data accepted)
**unconditionally**, even when the actual `file_.write` was silently
SKIPPED because `byte_addr + 512 > file_size_`. The host therefore
believed every past-EOF write succeeded — a write-data-loss divergence
from every real SD card.

**Practical impact on the boot path**: nil. NextZXOS / TBBlue boot
flow does not write past the image (writes target /TBBLUE/CONFIG.INI
or sysfile slots well within the partition). Latent for misbehaving
host firmware, save-state replay, or future bring-up of new firmware.

**Fix**: gate the response token on a local `write_ok` flag mirroring
the seekp/write/flush condition:

```cpp
bool write_ok = (byte_addr + 512 <= file_size_);
if (write_ok) { ... write ... } else { warn_log(); }
state_ = State::WRITE_RESP;
resp_buf_ = { static_cast<uint8_t>(write_ok ? 0x05 : 0x0D) };
```

The in-bounds `0x05` path is unchanged so SD-14 (CMD24 round-trip)
continues to pass.

**Test**: `sdcard_test.cpp` SD-21 — builds an 8-sector (4 KB) image,
issues CMD24 to sector 100 (= 51200-byte offset, well past 4096), and
verifies the card returns `0x0D` (write error). The test ALSO issues
CMD24 to sector 0 (in-bounds) and verifies `0x05` (data accepted) is
still returned, guarding against an inverted-sense regression of the
fix.

**Class**: (b) — spec-compliance bug with concrete misleading-host
divergence; latent for current boot path. Resolved.

---

## Catalogued but not actioned

The following observations were identified during the audit but are
class-(c)/(d) latent or out-of-scope for Pass-12.

### V12-DIVMMC-03 — CMD8 R7 byte 0 missing command-version nibble (class-(c) latent)

**File**: `src/peripheral/sd_card.cpp:447`

**Spec**: SD Physical Layer Simplified Spec § 7.3.2.6. R7 4-byte data
field bits 31:28 = command version (`0001` = R7 v1). Byte 0 should
therefore be `0x10`.

**JNEXT**: emits byte 0 as `0x00`.

**Why latent**: TBBlue master + FatFs disk-io do not validate byte 0
of R7 — they only check the voltage-accepted nibble (byte 2 bits 3:0)
and check-pattern echo (byte 3). All boot-path firmware ignores byte 0.
Class-(c). No action this pass.

### V12-DIVMMC-04 — CMD17 / CMD18 past-EOF emit error data token but R1 is reported as "no errors" (class-(c) latent)

**File**: `src/peripheral/sd_card.cpp:533-540, 570-578`

**Observation**: When `byte_addr + 512 > file_size_`, both CMD17 and
CMD18 queue `R1=0x00` followed by data error token `0x08`. SD spec
§ 7.3.2.1 (Table 7-9) lists R1 bit 6 = PARAMETER_ERROR for "argument
outside the allowed range". A spec-strict card would set R1 bit 6
*and* emit the error token. JNEXT-style ("R1 OK + error token") is the
weaker but spec-permissible "card accepted the command but couldn't do
the transfer" path per § 7.2.4 — so not a strict bug, just thinner R1
fidelity than ideal.

**Why latent**: TBBlue/NextZXOS never read past EOF in the boot path.
Class-(c). No action this pass.

### V12-DIVMMC-05 — Spi master cycle-accurate FSM not modelled (class-(d) architectural)

**File**: `src/peripheral/spi.{h,cpp}`

**Observation**: VHDL `spi_master.vhd:80-100` implements a 16-cycle
byte-shift FSM with `spi_begin = '1' when (state_last = '1' or
state_idle = '1') and (i_spi_rd = '1' or i_spi_wr = '1') else '0';`
Mid-transfer rd/wr is suppressed. JNEXT collapses every transfer into
an instantaneous synchronous call.

**Implication**: A misbehaving host that issues back-to-back port_eb
writes inside the 16-cycle window would see the SECOND write IGNORED in
VHDL but COMMITTED in JNEXT. Also `o_spi_wait_n` (consumed by DMA at
zxnext.vhd:3297) cannot throttle DMA-via-SPI bursts to the 16-cycle
cadence. Both are documented as JNEXT-byte-level-faithful (the
existing ST-09, SX-06..10, ML-01..06 tests are explicitly category-B
"VHDL-internal pipeline signals not surfaced at the byte boundary").

**Why architectural**: a cycle-accurate SPI FSM rewrite would touch
DMA throttling + Z80 wait-state injection + every SPI test. Out of
scope for Pass-12.

### V12-DIVMMC-06 — Pre-data-token bytes other than 0xFE/0xFF treated as data byte 0 (class-(c) latent)

**File**: `src/peripheral/sd_card.cpp:148-176` (RECEIVING_DATA branch)

**Observation**: After CMD24 R1, the spec § 7.3.3.2 says the host
sends `0xFE` as the data token; any leading `0xFF` clocks before the
token are gap bytes (Pass-4 fix already handles this). But OTHER bytes
(e.g., a stray 0x55 or another command byte) sent BEFORE the token
fall through to `data_block_[data_idx_++] = tx;` — silently absorbed
as the first data byte. Real cards keep waiting for `0xFE`.

**Why latent**: All known firmware (TBBlue, FatFs, esxdos) sends
either the token immediately or a string of `0xFF` gap bytes followed
by the token. Out-of-spec hosts are theoretical. Class-(c).

### V12-DIVMMC-07 — `divmmc_automap_*_q` registered M1-edge pipeline absent (class-(d) latent)

**File**: `src/peripheral/divmmc.cpp:check_automap`

**Observation**: VHDL `zxnext.vhd:4114-4135` registers
`divmmc_automap_*_q` on the falling edge of `i_CLK_28` based on
`cpu_m1_n` and `cpu_mreq_n` boundaries — it samples the combinational
`divmmc_automap_*_on` signals at the MREQ rising edge, then drives the
divmmc_mod inputs from the registered values one cycle later. JNEXT
collapses the registered `_q` pipeline into the same `check_automap`
call: the delayed/instant matches feed `automap_hold_` and
`automap_active_` directly without the 1-cycle register latency.

**Implication**: At the byte boundary the model agrees with VHDL — the
two-stage hold/held latch (Step 1 / Step 3 in JNEXT) absorbs the
single-cycle pipeline delay. A cycle-accurate distinction would only
be observable if external code probed automap state on a specific
clock edge during an M1 — which jnext's M1 callback already
synchronises to. Class-(d) architectural; documented.

### V12-DIVMMC-08 — `port_e3_reg(6) <= cpu_do(6) or port_e3_reg(6)` and NR 0x09 bit 3 clear are mutually-exclusive on the same VHDL clock (class-(d) latent)

**File**: `src/peripheral/divmmc.cpp:write_control` and emulator.cpp NR 0x09 handler

**Observation**: VHDL `:4180-4186` uses an `elsif` ladder — port_e3
write takes priority over the NR 0x09 bit-3 mapram-clear in the same
cycle. JNEXT serialises Z80 OUT instructions, so two writes in the
same VHDL cycle are impossible. Theoretical edge only matters for an
imaginary same-instruction Z80 OUT-OUT pair, which doesn't exist.
Class-(d).

---

## Tests run

```
$ cmake --build build -j$(nproc)
[100%] Built target jnext

$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 38
Total Test time (real) = 0.40 sec

$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/divmmc_test
Total:  136  Passed:  136  Failed:    0  Skipped:    0   # post fix-of-reviewer (SS-17 added)

$ ./build/test/sdcard_test
Total:   22  Passed:   22  Failed:    0  Skipped:    0
```

Three new regression tests added across the audit + reviewer + fix-of-
reviewer chain:
- `test/divmmc/divmmc_test.cpp` — SS-16 (rx_data_ preservation across reset)
- `test/divmmc/divmmc_test.cpp` — SS-17 (rx_data_ first-boot default = 0x00 per VHDL signal-init; Pass-12 fix-of-reviewer V12-DIVMMC-01-NIT)
- `test/sdcard/sdcard_test.cpp` — SD-21 (CMD24 past-EOF write-error response)

(Reviewer also promoted V12-DIVMMC-03/04/06 to class-(c) fixes with
SD-22/23/24 regressions — see `NEXTZXOS-BOOT-SUBSYSTEM-VERIFY12-
DIVMMC-SD-SPI-REVIEW.md` for those.)

## Summary

| Class | Count | Status   |
|-------|-------|----------|
| (a)   | 0     | n/a      |
| (b)   | 1     | resolved (V12-DIVMMC-02 + SD-21 regression) |
| (c)   | 1+1 NIT | resolved (V12-DIVMMC-01 + SS-16 regression; V12-DIVMMC-01-NIT + SS-17 regression — Pass-12 fix-of-reviewer 2026-05-10) |
| (d)   | 6 catalogued (V12-DIVMMC-03..08); 3 promoted+resolved by reviewer (V12-DIVMMC-03/04/06 + SD-22/23/24); 3 remain class-(d) architectural / class-(c) latent. |

**Overall**: Pass-12 audit found 2 actionable findings (1 class-(b),
1 class-(c)) and 6 latent / architectural notes. The reviewer raised
1 NIT (V12-DIVMMC-01-NIT, first-boot default value drift vs VHDL
signal-init) and promoted 3 catalogued items to class-(c) fixes
(V12-DIVMMC-03/04/06 + SD-22/23/24). Fix-of-reviewer (2026-05-10)
resolves the NIT VHDL-faithfully with disc test SS-17 + alignment of
SX-03 / SX-04 / ML-05 / MX-04 to the same VHDL root cause (i_reset
hardwired '0' at zxnext.vhd:3285 → synchronous-reset clauses never
fire). All test suites pass (1356/1356 FUSE; 136/136 divmmc; 22/22
sdcard; 38/38 ctest).
