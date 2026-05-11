# Pass-16 verify-audit (DivMMC + SD + SPI) — Independent Review

- **Date**: 2026-05-10
- **Reviewer branch**: `task2/verify16-divmmc-sd-spi-reviewer`
- **Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify16-divmmc-sd-spi-reviewer`
- **Audit branch HEAD**: `65170ed` (sole commit on top of integration `267764b`)
- **Build**: Release, `cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`
- **Audit-report-only access** per prompt rule (no prior verify reports read).
- **Verdict**: **APPROVE**

---

## Summary

Audit reported 2 class-(c) findings on a fully blind pass:

| ID            | Class | One-line                                                                |
|---------------|-------|-------------------------------------------------------------------------|
| V16-DIVMMC-01 | (c)   | Port 0xE7 read returns CS latch — VHDL has NO `port_e7_rd` (write-only) |
| V16-DIVMMC-02 | (c)   | `find_in_directory` cluster-chain walk lacks cycle bound (host hang)    |

Both findings independently verified against VHDL / SD-spec oracles.
Both fixes correctly land at the obvious minimal-diff location with the
canonical patterns already used elsewhere in jnext (nullptr read handler
mirroring DAC / Kempston-write-only ports; defensive max_chain_len bound
mirroring the existing file-data walk). Both regression tests are
strictly discriminative (revert-checked).

Reviewer also performed a missed-case hunt across **other write-only
ports** in the divmmc/sd/spi space and **other FAT32 traversals** in
`sd_rom_extractor.cpp`. No additional findings.

---

## Finding-by-finding review

### V16-DIVMMC-01 — port 0xE7 read decode

**VHDL claim — VERIFIED.**

Independently checked `zxnext.vhd` (FPGA repo at
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`):

- Line 614-622 signal declarations: `port_e3_rd / port_e3_wr` and
  `port_eb_rd / port_eb_wr` declared as both rd+wr; **`port_e7_wr` only**
  — no `port_e7_rd` declared anywhere in the entire file.
- Line 666-668: `port_e3_rd_dat` and `port_eb_rd_dat` declared; no
  `port_e7_rd_dat`.
- Line 2735: `port_e7_wr <= iowr and port_e7;` — only the write strobe.
- Line 2803-2806: `port_internal_rd_response` OR-tree includes
  `port_e3_rd, port_eb_rd, ...` but **NOT** `port_e7_rd`. So a read of
  port 0xE7 produces `port_internal_rd_response = '0'`.
- Line 2837-2840: `port_rd_dat` OR-tree is the data-byte selector;
  `port_e3_rd_dat` and `port_eb_rd_dat` contribute, no `port_e7_rd_dat`.
- Line 1868-1882: with `port_internal_rd_response = '0'` the IORQ read
  falls through to `i_BUS_DI` (expansion bus) or floating-bus `X"FF"`
  (line 1877). With expansion bus disabled (jnext default), the read
  returns 0xFF.

**Therefore on real hardware port 0xE7 is strictly write-only.**

**Fix correctness — VERIFIED.**

Pre-fix: read handler called `spi_.read_cs()` (host-side CS latch),
leaking internal master state.

Post-fix: read handler is `nullptr`, falling through to
`PortDispatch::default_read_` (floating-bus 0xFF — see
`src/port/port_dispatch.cpp:59-63`). The fix mirrors the
**already-established jnext pattern** for write-only ports — DAC ports
0x1F/0x0F/0x4F at `emulator.cpp:3326-3344` use the same
`register_handler(mask, value, nullptr, write_lambda)` shape.

Comment block at the fix site is thorough and traces every VHDL
reference correctly.

**Discriminative test — VERIFIED.**

Independently revert-checked: I stash-saved the post-fix
`emulator.cpp`, re-applied the pre-fix code (read handler returns
`spi_.read_cs()`), rebuilt port_test, and ran:

```
FAIL V16-DIVMMC-01: IN 0xE7 returns 0xFF (port is write-only in VHDL — no
  port_e7_rd signal); pre-fix returned the internal CS latch.
  [latched_cs=0xfe in_0xE7=0xfe expected_in=0xFF]
Total:   84  Passed:   83  Failed:    1  Skipped:    0
```

Restored fix → 84/84 PASS. Confirms the test is exactly discriminative
on the V16-DIVMMC-01 fix.

### V16-DIVMMC-02 — find_in_directory cluster-chain cycle bound

**SD / FAT32 spec claim — VERIFIED.**

The FAT32 filesystem (Microsoft FAT32 spec rev 1.03 § 4 "FAT structures")
defines a per-cluster linked list with no built-in cycle protection. A
well-formed FAT contains no cycles, but malformed/forensic/hostile
images can. Every mainstream FAT32 reader (Linux fs/fat, FatFs, ChibiOS)
bounds the chain by the total cluster count. Cycle defense is correct
and standard.

**Pre-fix gap — VERIFIED.**

Pre-fix `find_in_directory` (sd_rom_extractor.cpp:264+) only exited on:
1. Read failure
2. `0x00` end-of-directory marker
3. Match found
4. EOC / cluster=0 / bad mark

A 2 ↔ 3 ↔ 2 ↔ ... cycle with no `0x00` end-of-dir marker and no
matching SFN entry triggers none of these → infinite loop → host hang.
By contrast, `extract_sd_rom`'s file-data walk
(sd_rom_extractor.cpp:436) already has `max_chain_len = (file_size /
bytes_per_cluster) + 2`. The asymmetry was real.

I checked all chain walks in the file:
- `fat_next` (line 157) — single FAT entry read, no loop.
- `find_in_directory` (line 273) — directory walk, **was unbounded**,
  now fixed.
- `extract_sd_rom` file-data loop (line 439) — bounded by file_size.
- `extract_sd_rom` path-component loop (line 374) — bounded by
  `parts.size()` from `split_path` (input string).

So `find_in_directory` was the only unbounded chain walk. The fix
closes the gap.

**Fix correctness — VERIFIED.**

`max_chain_len = fat_size_sectors * bytes_per_sector / 4` is the count
of FAT entries the partition can hold (each FAT32 entry is 4 bytes).
A well-formed chain CANNOT exceed this — the FAT itself simply doesn't
have room for a longer chain. The bound is mathematically tight for any
non-cyclic chain and trips immediately on any cycle.

`fat_size_sectors > 0` is validated in `parse_bpb` (line 135);
`bytes_per_sector ∈ {512, 1024, 2048, 4096}` (line 124). The product is
guaranteed positive and within u64.

The fix mirrors the existing file-data walk pattern in the same file
(line 436), using the same `++steps > max_chain_len` early-return
shape.

**Discriminative test (SD-EXT-09) — VERIFIED.**

Independently revert-checked: stash-saved the post-fix
`sd_rom_extractor.cpp`, re-applied the pre-fix code (no max_chain_len),
rebuilt sd_rom_extractor_test, and ran:

```
$ timeout 10s ./build/test/sd_rom_extractor_test
EXIT=143  (= SIGTERM, killed by timeout — process hung)
```

Restored fix → 26/26 PASS in microseconds. Confirms the SD-EXT-09 test
is exactly discriminative on the V16-DIVMMC-02 fix.

Test geometry sanity-check:
- `kFatSizeSectors=1`, `kSectorSize=512` → `max_chain_len = 1 * 512 / 4 = 128`
- Cycle 2 ↔ 3 → loop walks 2,3,2,3,... and trips guard at step 129
- `entry[2] @ FAT offset 8 = 0x03` and `entry[3] @ offset 12 = 0x02` —
  little-endian u32 placement is correct
- BPB at offset 11 = 0x00 0x02 (= 512 LE) — correct
- BPB at offset 36 = 0x01 (FATSz32 = 1) — correct
- BPB at offset 44 = 0x02 (root cluster = 2) — correct
- MBR at 0x1BE: type=0x0C (FAT32-LBA), LBA=1 — correct
- Both data clusters filled with ATTR_VOLUME_ID (0x08) — properly skipped
  by `find_in_directory` per FAT32 spec (volume label entries are not
  file entries)

The synthetic image is well-formed enough for `parse_bpb` and
`find_fat32_partition_lba` to accept, but its FAT contains a deliberate
cycle. The post-condition assertions (`!ok && out.empty()`) are
appropriate.

---

## Missed-case hunt

### Other write-only ports in divmmc/sd/spi

Searched `zxnext.vhd:600-660` for all `_wr` signals without a `_rd`
counterpart in the divmmc/sd/spi address range:

| Port  | VHDL `_rd`?           | jnext handler                  | Status                          |
|-------|-----------------------|--------------------------------|---------------------------------|
| 0xE3  | YES (`port_e3_rd`)    | divmmc_.read_control()         | OK — readable in VHDL           |
| 0xE7  | NO                    | nullptr (post V16-DIVMMC-01)   | OK — fix correct                |
| 0xEB  | YES (`port_eb_rd`)    | spi_.read_data()               | OK — readable in VHDL           |

No additional write-only port leaks in this subsystem. (Multiface ports
0x1FFD-related and the MF enable/disable pair are out of scope per the
audit's prompt — they belong to the NMI/MF subsystem.)

### Other FAT32 traversals

Audited every `while`/`for` loop in `src/core/sd_rom_extractor.cpp`:

| Site                                     | Bounded by                    | Status |
|------------------------------------------|-------------------------------|--------|
| `find_in_directory` cluster walk (line 273) | `max_chain_len` (NEW)        | OK after fix |
| `find_in_directory` entry scan (line 282)   | `buf.size()` (cluster bytes) | OK     |
| `extract_sd_rom` file-data walk (line 439)  | `max_chain_len` (file_size)  | OK     |
| `extract_sd_rom` path-component loop (374)  | `parts.size()`               | OK     |
| `fat_next` (line 157)                       | single read, no loop         | OK     |
| `parse_bpb` partition-table scan (line 89)  | bounded `for i<4`            | OK     |
| `make_sfn_key` (line 206-223)               | bounded by component length   | OK     |

No other unbounded traversals.

---

## Style / pattern consistency

- Fix at `emulator.cpp:4069` follows existing nullptr-read-handler
  convention (DAC ports 0x1F/0x0F/0x4F at line 3326-3344).
- Fix at `sd_rom_extractor.cpp:270-279` follows the existing file-data
  bound at the same file's line 436-447.
- Both fixes preserve the gate (`nextreg_.cached(0x83) & 0x08`) on the
  write side correctly.
- Comments are thorough, name VHDL line numbers, and explain class
  rationale.
- Commit message terse-but-complete; pre-fix behaviour, fix shape,
  test, and revert-check all stated.

---

## Build & test results

```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
   (after creating roms symlink in worktree)
$ cmake --build build -j$(nproc)
   [100%] Built target jnext
$ ctest --test-dir build --output-on-failure
   100% tests passed, 0 tests failed out of 38
   Total Test time (real) =   0.46 sec
$ ./build/test/fuse_z80_test build/test/fuse
   Total: 1356  Passed: 1356  Failed:    0  Skipped:    0
```

All Release-mode tests pass. Both new discriminative tests
(V16-DIVMMC-01 in port_test, SD-EXT-09 in sd_rom_extractor_test) are
green post-fix. Both revert-check FAIL (port_test) / HANG (sd_rom_-
extractor_test under `timeout 10s`, exit 143).

---

## Verdict

**APPROVE.**

Both findings are real, both VHDL/spec claims are independently
verified, both fixes use canonical jnext patterns at minimal-diff
locations, both regression tests are strictly discriminative, the full
Release-mode test suite is green (38/38 ctest, 1356/1356 FUSE Z80), and
the missed-case hunt across other write-only ports and other FAT32
traversals turned up nothing further.
