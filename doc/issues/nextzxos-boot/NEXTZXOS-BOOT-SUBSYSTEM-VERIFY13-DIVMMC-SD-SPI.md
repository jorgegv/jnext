# Task 2 — Pass-13 verify-audit: DivMMC + SD + SPI subsystem

**Branch**: `task2/verify13-divmmc-sd-spi` (off integration HEAD `adcc752`)
**Worktree**: `.claude/worktrees/task2-verify13-divmmc-sd-spi`
**Mode**: BLIND audit (prior pass reports not consulted)
**Oracle**: VHDL `cores/zxnext/src/{device/divmmc.vhd, serial/spi_master.vhd, zxnext.vhd}` and the SD Physical Layer Simplified Spec v6.00 for the SD-card protocol surface (no VHDL SD model exists — `i_SPI_SD_MISO` is an external pin in the FPGA core).

## Summary

| Class | Count |
|------:|------:|
| (a)   | 0 |
| (b)   | 0 |
| (c)   | 1 |
| (d)   | 0 |
| **Total findings** | **1** |

One class-(c) finding (V13-DIVMMC-01). The DivMMC + SD + SPI subsystem
has had ten prior audit waves and four reviewer-promoted Pass-12 fixes,
which had already brought the SD-card command surface into close
agreement with the SD spec for CMD0/CMD8/CMD13/CMD16/CMD17/CMD18 + the
ACMD41/CMD55/CMD58 init flow + the CMD24 data-token / pre-token /
data-error-token edges. This pass found one residual asymmetry on the
CMD24 past-EOF path.

## Methodology

Read every line of:

- `src/peripheral/divmmc.{cpp,h}` (612 + 353 LOC)
- `src/peripheral/sd_card.{cpp,h}` (vs Pass-12 + the data-write / data-read /
  inter-block / token / R7 / OCR / R1 / R2 / R3 surfaces)
- `src/peripheral/spi.{cpp,h}` (the chip-select decode + 1-byte-pipeline
  miso_dat model + flash-CS gate)
- `src/core/sd_rom_extractor.{cpp,h}` (host-side FAT32-LBA reader)
- the DivMMC + SD + SPI port-write dispatch slice in `src/core/emulator.cpp`
  (handlers for ports 0xE3, 0xE7, 0xEB and NRs 0x83 b0/b1, 0x0A b4/b5,
  0xB8/0xB9/0xBA/0xBB, 0x09 b3, 0xC0)
- the corresponding VHDL: divmmc.vhd top-to-bottom, spi_master.vhd
  top-to-bottom, zxnext.vhd port-decode windows around 2412-2620,
  2848-2912, 3275-3332, 3081-3138, 4108-4188, 4920-5099, 5585-5599

Cross-checked Pass-13 angle list:

* SD R1/R2/R3/R7 framing edge cases (stuff bits, OCR fields, R7 cmd-version
  nibble + voltage echo, R1b busy semantics): **clean** post-V12-DIVMMC-03.
* CMD12 STOP_TRANSMISSION graceful end of multi-block read/write streams:
  **clean** — `cmd12_stop_transmission` clears `multi_block_*` AND emits the
  TBBlue-firmware-required 8 stuff bytes + NCR + R1 frame; persistent byte
  is 0xFF (idle, non-zero — supervisor's `IN A,($EB); AND A; JR Z` poll
  matches).
* CMD13 STATUS register fields: **clean** — NCR + R1 + 1-byte-status
  matches SD spec § 7.3.2.3.
* Multi-block CRC field: **clean** — 2-byte 0x00 CRC trailer per block;
  TBBlue/FatFs ignores per spec convention.
* DivMMC entry-point matrix (every trigger byte × automap on/off ×
  cmd-set/cmd-clr × valid-vs-rom3 × instant-vs-delayed × NMI gate × 3Dxx
  wildcard × auto-unmap range): **clean** — `check_automap`'s 8 RST + NMI +
  4 tape traps + 3Dxx wildcard + 0x1FF8..0x1FFF off-range matches VHDL
  `divmmc_automap_*_on` decoders at zxnext.vhd:2848-2908. Per-path eligibility
  gates (sram_pre_override(2) for main, full ROM3 composite for rom3) match
  zxnext.vhd:3137-3138. Off-trigger gating on `i_automap_active` matches
  divmmc.vhd:131.
* DivMMC RAM bank addressing (16 × 8 KB = 128 KB, boundary at bank 15 and
  bank 0): **clean** — `bank_ * 0x2000 + (addr & 0x1FFF)` always lands in
  `[0, 0x20000)`.
* conmem vs mapram precedence: **clean** — VHDL line 94 `(conmem OR
  automap)` AND mapram=0 for ROM, OR mapram=1 for slot-0 RAM page 3; line
  100 read-only when page0 OR (mapram AND ram_bank=3). C++ matches.
* DivMMC integration with NR $B8/$B9/$BA/$BB: **clean** — write handlers
  forward to `set_entry_*`; defaults match VHDL hard-reset (0x83/0x01/
  0x00/0xCD).
* DivMMC ROM3 write enable: **n/a** — VHDL has no ROM3-specific write
  gate (page0 always read-only per line 100). The altrom_rw write-enable
  is at altrom (out of scope, MMU subsystem).
* Multi-overlay precedence (DivMMC ROM > Multiface ROM > altrom > MMU >
  Layer 2): **clean** in the DivMMC subsystem itself — `Mmu::read/write`
  dispatches in the order bootrom > MF > DivMMC > L2 > config > altrom >
  MMU, which matches VHDL zxnext.vhd:3081-3132 priority cascade. The MMU
  side of the precedence chain is owned by the memory subsystem.
* SD-card image bounds at near-end (read of last sector, write of last
  sector, off-by-one): **clean** — `byte_addr + 512 > file_size_` rejects
  exactly when the would-be read/write extends past EOF; the
  `byte_addr + 512 == file_size_` boundary is allowed (last sector
  inclusive). CMD17/18/24 all use the same comparator.
* FAT32 reader edge cases (FAT_EOC mid-chain, root dir spanning multiple
  clusters, LFN with non-ASCII, "."/".." SFN special-cases): **clean** —
  see audit notes in V13 report below.
* SD_rom_extractor partial reads (file smaller than declared,
  zero-cluster non-empty file, empty file): **clean** — early exit on
  EOC; zero-cluster non-empty file rejected with explicit error;
  `file_size==0` returns success with empty buffer.
* SPI clock divider behavior change mid-transfer: **n/a** — VHDL
  spi_master uses fixed CLK/2 SCK divider, not run-time configurable.
* CMD24 past-EOF data-write path: **divergence found** → V13-DIVMMC-01.

## Findings

### V13-DIVMMC-01 — CMD24 past-EOF rejects at data-response token instead of at R1 (class-(c))

**File**: `src/peripheral/sd_card.cpp` (cmd24_write_single_block)
**VHDL/spec authority**: SD Physical Layer Simplified Spec v6.00
§ 7.3.2.1 Table 7-9 (R1 layout: bit 6 = PARAMETER_ERROR), § 4.3.4 (Data
Write Sequence — data phase is conditional on R1=0x00).

**Status**: fixed in this commit.

**Symptom**: CMD24 issued with a sector index past end-of-image returns
R1=0x00 (no error indicated in the immediate response), then accepts the
host's 0xFE + 512 + 2 CRC bytes (514 wasted bytes), then returns the
data-response token 0x0D (write error) at the very end of the data phase.

**VHDL/spec-faithful behaviour**: per § 7.3.2.1 + § 4.3.4, the card MUST
set R1 bit 6 (PARAMETER_ERROR) when the command argument is out of the
allowed range, AND MUST NOT proceed to the data phase. Real SD cards
implement this — the 0x0D-via-data-response path is the alternative for
errors that are only detectable AT the data phase (e.g. a write-protect
toggle latched between CMD24 and the data-token), not for argument
range checking.

**Root cause**: pre-fix `cmd24_write_single_block` in `src/peripheral/sd_card.cpp`
did not bound-check the sector argument before queuing R1 + setting
`pending_write_after_r1_=true`. The address check was deferred to the
end of the data phase (in the RECEIVING_DATA → WRITE_RESP transition),
where it correctly switched the response token to 0x0D — but by that
point the host had already burned 514 bytes of MOSI clocks on a
non-existent write.

**Symmetry / motivation**: V12-DIVMMC-04 (Pass-12 reviewer-promoted)
ALREADY established this contract for CMD17/CMD18 past-EOF — both
queue R1=0x40 immediately and emit the 0x08 data error token before
any data is transferred. CMD24 past-EOF was the lone outlier. The
underlying spec sentence ("argument was out of allowed range" → R1 bit
6 → no data phase) applies identically to all three commands; jnext now
honours it for all three.

**Fix shape**: in `cmd24_write_single_block`, add the past-EOF check
BEFORE queuing R1=0x00 and arming the data bridge:

```cpp
if (byte_addr + 512 > file_size_) {
    queue_r1(0x40);  // R1 bit 6 = PARAMETER_ERROR
    return;          // no pending_write_after_r1_ — no data phase per § 4.3.4
}
queue_r1(0x00);
... pending_write_after_r1_ = true;
```

Behaviour after the fix:

| Scenario | R1 | Data phase | Final byte |
|----------|----|-----------:|-----------|
| In-bounds CMD24 | 0x00 | yes (host sends 0xFE + 512 + CRC) | 0x05 (data accepted) |
| Past-EOF CMD24 (V13) | **0x40 (bit 6)** | **no — FSM stays in IDLE after R1** | (host idle 0xFF) |
| Past-EOF CMD24 (pre-V13) | 0x00 | yes (514 wasted bytes) | 0x0D (write error) |

**Class**: (c) — latent for the boot path (TBBlue / FatFs / esxdos /
NextZXOS supervisor never write past EOF; the only past-EOF write
trigger would be a third-party Z80 SD library miscalculating sector
arithmetic), but spec-divergent for any host that legitimately polls
the card's argument-range checking.

**Discriminative regression test**: `test/sdcard/sdcard_test.cpp`

* SD-21 (UPDATED, was V12-DIVMMC-02 anchor): asserts post-V13 R1=0x40 +
  no data-response token + in-bounds R1=0x00 + 0x05 regression guard.
  The pre-V13 assertion (R1=0x00, data-response=0x0D) is now the
  pre-fix shape; the test description records the evolution.
* SD-25 (NEW, V13-DIVMMC-01 anchor): proves the FSM is in IDLE after
  R1=0x40 by issuing CMD13 immediately after — pre-V13 the CMD13
  command-start byte 0x4D would be absorbed as data_block_[0] and CMD13
  would never run; post-V13 CMD13 dispatches cleanly and returns the
  expected NCR + R1 + status_byte frame.

**Tests passed** (release, post-fix):

* sdcard_test: 26 / 0 / 0
* ctest --test-dir build: 38 / 38 pass
* fuse_z80_test: 1356 / 1356 pass

## Defensible-zero areas (audited, no findings)

The following Pass-13 angles were exercised against the VHDL / SD spec
and the C++ matched in every checkable detail. Listing them so a future
auditor can pick up the trail without re-walking ground:

* **divmmc.vhd line 94/95/96/100** (rom_en/ram_en/ram_bank/rdonly
  combinational equations) vs `DivMmc::is_active`/`is_ram_mapped`/
  `is_read_only`/`ram_page_for`: equation-by-equation match.
* **divmmc.vhd line 105-114** (`button_nmi` FF — i_reset / i_automap_reset /
  i_retn_seen / i_divmmc_button / automap_held clear chain) vs
  `DivMmc::on_retn` + `apply_enabled_transition_` + the
  `automap_held && button_nmi → button_nmi=0` clause inside check_automap:
  match. The Pass-8 verify-audit fix at divmmc.cpp:294-299 already
  closed the "button_nmi continuously cleared while held=1" divergence;
  re-verified this pass.
* **divmmc.vhd line 123-148** (automap_hold / automap_held / automap
  combinational) vs `DivMmc::check_automap` 4-step pipeline: match. The
  off-trigger active-gating shape (the third OR-clause in line 131,
  `automap_held AND NOT (i_automap_active AND i_automap_delayed_off)`)
  is correctly modelled by the asymmetric `off_match` gating —
  off_match is set ONLY when main_path_eligible, so when active=0 the
  held term propagates regardless of PC range, and when active=1 +
  off_match=1 the held term is suppressed.
* **zxnext.vhd line 2848-2891** (RST entry-point decode mux on cpu_a[5:3])
  vs `DivMmc::check_automap` rst_addrs[] table: match. Per-bit gating on
  `entry_points_0_(i)` AND validity / timing per `entry_valid_0_(i)` /
  `entry_timing_0_(i)` matches the line 2853-2883 case body.
* **zxnext.vhd line 2892-2908** (combinational divmmc_automap_*_on signals)
  vs the post-RST decode branches in check_automap (NMI@0x0066, tape
  traps at 0x04C6/0x0562/0x04D7/0x056A, 3Dxx wildcard, off-range
  0x1FF8..0x1FFF): match. The button_nmi gate on the NMI path is
  correctly placed at the C++ entry point check (line 372) — VHDL gates
  it at divmmc.vhd:120-121.
* **zxnext.vhd line 3137-3138** (sram_divmmc_automap_en / *_rom3_en
  composites) vs `main_path_eligible` / `rom3_path_eligible` in
  check_automap: match (modulo the comment at divmmc.cpp:328-331
  acknowledging the altrom_en simplification, which is class-(d) per
  prior pass — boot path keeps NR 0x8C bit 7 clear so the simplified
  expression is equivalent).
* **zxnext.vhd line 3308-3322** (port_e7_reg decode: SD0/SD1 strict
  bits-1:0 = "10"/"01" + RPI0/RPI1 strict cpu_do=0xFB/0xF7 + Flash
  strict cpu_do=0x7F gated on config_mode OR reset_type_2) vs
  `SpiMaster::write_cs`: match. sd_swap inversion of bits 1:0 is
  applied symmetrically to both SD0 and SD1 decode arms.
* **zxnext.vhd line 4111-4188** (divmmc_retn_seen, divmmc_automap_reset,
  port_e3_reg three-clause writer with mapram OR-latch + NR 0x09 bit 3
  mapram clear) vs `DivMmc::write_control` / `clear_mapram` + the NR
  0x09 write handler in emulator.cpp:3699-3715: match.
* **spi_master.vhd line 159-167** (miso_dat update at state_last_d, with
  i_reset hardwired '0' at zxnext.vhd:3285) vs `SpiMaster::reset`
  (rx_data_ NOT clobbered, per V12-DIVMMC-01 + NIT) and
  `SpiMaster::{write_data, read_data}` (rx_data_ updated on every
  transfer): match.
* **spi_master.vhd line 177** (`o_spi_wait_n <= state_idle OR state_last_d`)
  vs `SpiMaster::spi_wait_n() = true`: match at byte granularity (jnext
  is zero-latency wrapper, always idle after each byte exchange — the
  cycle-accurate model is deferred to the documented G137 long-term).
* **SD spec § 5.3.3** (CSD v2.0 16-byte register) vs `cmd9_send_csd`:
  match. C_SIZE encodes capacity = (C_SIZE+1) * 512 KB; CSD_STRUCTURE=01
  (= v2.0); TRAN_SPEED=0x32 (= 25 MHz default); CCC=0x5B5; ERASE_BLK_EN=1.
* **SD spec § 5.2** (CID 16-byte register) vs `cmd10_send_cid`: match.
  Generic SDHC-shaped CID; manufacturer/OEM/PNM/PSN/MDT all valid.
* **SD spec § 7.3.3.2** (data token 0xFE for single block, 0xFC for
  multi-block, 0xFD stop token) vs `cmd17_read_single_block` /
  `cmd18_read_multiple_block` / send() inter-block re-prime / receive()
  RECEIVING_DATA: match for the implemented commands. CMD25 multi-block
  write is a documented WONT (TBBlue / FatFs only ever invoke CMD24 in a
  loop, never CMD25).
* **SD spec § 7.2.4 / 7.3.3.1** (CMD24 R1 → host-data-bridge → response
  token) vs the `pending_write_after_r1_` flag-based bridge: match.
  The flag transitions RESPONDING → RECEIVING_DATA only after the LAST
  R1 byte has actually been emitted on MISO (closed Pass-4 verify-audit
  hang).
* **SD spec § 4.3.9** (CMD55 → ACMD41 vs CMD55 → non-ACMD fall-through)
  vs `process_command`'s app_cmd_ fall-through branch (Pass-9): match.
* **sd_rom_extractor.cpp** MBR + BPB + FAT chain + cluster-walk paths:
  bounds-checked correctly. BPB sanity guards (bytes_per_sector ∈ {512,
  1024, 2048, 4096}, sectors_per_cluster power-of-two ∈ [1, 128],
  num_fats!=0, fat_size!=0, root_cluster≥2) all enforced.
  Cycle-protection via `max_chain_len` bound. Empty file (size=0)
  short-circuits with success. Zero-cluster-with-nonzero-size rejected.
  8.3 SFN matching with 0x05 → 0xE5 first-byte aliasing handled. LFN
  attribute (0x0F) and volume-label entries skipped. "." and ".."
  special-cased in `make_sfn_key`. Defensive remap of cur_dir_cluster=0
  back to root_cluster.
* **emulator.cpp NR write handlers** for 0x83 (b0 → divmmc.set_port_io_enable,
  b1 → multiface.set_enabled), 0x0A (b4 → divmmc.set_nr_0a_4_enable, b5
  config_mode-gated → spi.set_sd_swap, b7:6 config_mode-gated →
  multiface.set_mode), 0x09 (b3 → divmmc.clear_mapram), 0xB8/0xB9/0xBA/
  0xBB (→ divmmc.set_entry_*), 0xC0 (im2_ controller — out of subsystem
  scope but cross-checked): match.
* **emulator.cpp port handlers** for 0xE3 (gated by NR 0x83 b0,
  delegates to divmmc.read_control / write_control), 0xE7 (gated by NR
  0x83 b3, delegates to spi.read_cs / write_cs), 0xEB (same gate,
  delegates to spi.read_data / write_data): match.

## Build & test

```
cd /home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify13-divmmc-sd-spi
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure   # 38/38 PASS
./build/test/fuse_z80_test build/test/fuse   # 1356/1356 PASS
./build/test/sdcard_test                     # 26/0/0 PASS (was 24, +SD-25 + revised SD-21)
```

## Tests changed in this commit

* `test/sdcard/sdcard_test.cpp`: SD-21 updated to assert the V13 shape
  (R1=0x40, no data phase) and the in-bounds R1=0x00 + 0x05 regression
  guard. New SD-25 added that proves the FSM returns to IDLE after the
  R1=0x40 emission by dispatching CMD13 immediately after.
* `src/peripheral/sd_card.cpp`: `cmd24_write_single_block` now
  short-circuits past-EOF before arming the data-phase bridge.

## VHDL anchors cited

* `cores/zxnext/src/device/divmmc.vhd` lines 88-101 (rom_en/ram_en/
  ram_bank/rdonly), 105-114 (button_nmi FF), 123-145 (hold/held FFs),
  148 (combinational automap), 150 (o_disable_nmi).
* `cores/zxnext/src/serial/spi_master.vhd` lines 74 (miso_dat sig-decl
  initial 0x00), 82 (spi_begin), 86-87 (state_last/idle), 162-167
  (miso_dat update at state_last_d), 177 (o_spi_wait_n).
* `cores/zxnext/src/zxnext.vhd` lines 2412 (port_divmmc_io_en),
  2470-2540 (port_*xx_msb / port_*_lsb decoders), 2620-2621
  (port_e7/port_eb gated by port_spi_io_en), 2848-2908 (RST + NMI +
  tape + 3Dxx + off-range automap-on signals), 3081-3132 (SRAM
  arbiter priority cascade), 3137-3138 (sram_divmmc_automap_en
  composites), 3278-3280 (spi_miso default '1' when no SS asserted),
  3282-3298 (spi_master_mod instantiation, i_reset hardwired '0' at
  3285), 3308-3326 (port_e7_reg writer / decoder), 4111-4188
  (divmmc_mod instantiation + port_e3_reg writer + NR 0x09 bit 3
  mapram clear), 4920+ (NR reset block including 5087-5090 NR $B8/B9/BA/
  BB defaults), 5191-5198 (NR 0x0A field commits gated on config_mode),
  5585-5599 (NR $B8/B9/BA/BB write fan-out + NR 0xC0 write fan-out).

## SD spec anchors cited

SD Physical Layer Simplified Spec v6.00:
* § 4.3.4 Data Write Sequence (R1=0x00 prerequisite for data phase)
* § 4.9.1 SDHC fixed 512-byte block length
* § 4.10.1 R1 status decode
* § 5.1 OCR layout (CCS bit 30, voltage windows)
* § 5.2 CID register layout
* § 5.3.3 CSD v2.0 register layout
* § 7.2.4 Command + R1 framing + NCR
* § 7.3.2.1 R1 layout (Table 7-9 — bit 6 = PARAMETER_ERROR)
* § 7.3.2.3 R2 layout (R1 + 1-byte status)
* § 7.3.2.6 R7 layout (R1 + cmd version + voltage echo + check pattern)
* § 7.3.3.1 NCR / R1 / data-token sequencing
* § 7.3.3.2 Data Tokens (0xFE / 0xFC / 0xFD)
* § 7.3.3.3 Data Response Token (0x05 / 0x0B / 0x0D)
* § 7.3.3.4 Data Error Token (bit 3 = out-of-range)
