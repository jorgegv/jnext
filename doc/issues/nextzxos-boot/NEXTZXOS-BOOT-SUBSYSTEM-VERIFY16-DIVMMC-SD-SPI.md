# Pass-16 verify-audit — DivMMC + SD + SPI subsystem

- **Date**: 2026-05-10
- **Branch**: `task2/verify16-divmmc-sd-spi`
- **Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify16-divmmc-sd-spi`
- **Base HEAD (integration)**: `267764b`
- **Mode**: BLIND audit — prior pass reports under `doc/issues/nextzxos-boot/` were not read.
- **Build**: Release, `cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`.
- **Tests baseline**: ctest 38/38 PASS, FUSE Z80 1356/1356 PASS.
- **Tests after fixes**: ctest 38/38 PASS, FUSE Z80 1356/1356 PASS.
- **VHDL oracle**: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`
  (zxnext.vhd, device/divmmc.vhd, serial/spi_master.vhd).
- **SD oracle**: SD Physical Layer Simplified Specification 6.00.

## Findings summary

| ID            | Class | Subsystem    | One-line                                                              |
|---------------|-------|--------------|-----------------------------------------------------------------------|
| V16-DIVMMC-01 | (c)   | SPI / port   | Port 0xE7 read returns CS latch — VHDL has no `port_e7_rd` (write-only) |
| V16-DIVMMC-02 | (c)   | SD ROM extr. | `find_in_directory` cluster-chain walk lacks cycle bound — host hang   |

- Total findings: **2**
- Class-(a): **0**
- Class-(b): **0**
- Class-(c): **2**
- Class-(d): **0**

Both fixes landed in the same commit (per prompt rule: VHDL-faithful fix + discriminative regression test in same commit). Both new tests pass post-fix and FAIL pre-fix (verified by stash-revert of the source-only diffs).

---

## V16-DIVMMC-01 — Port 0xE7 read decode (write-only in VHDL)

### Class

**(c)** — latent for the boot path (TBBlue / NextZXOS firmware never reads
port 0xE7); real-spec / VHDL-faithfulness divergence.

### VHDL oracle

`zxnext.vhd:614-622` declares the read/write strobes for the SPI port pair:

```vhdl
   signal port_e3_rd             : std_logic;     -- 0xE3 IS readable
   signal port_e3_wr             : std_logic;
   signal port_e7_wr             : std_logic;     -- only WR exists for 0xE7
   signal port_eb_rd             : std_logic;     -- 0xEB IS readable
   signal port_eb_wr             : std_logic;
```

Confirmation that the VHDL never decodes a read of port 0xE7:

- `zxnext.vhd:2735` defines only `port_e7_wr <= iowr and port_e7;`. There
  is **no** `port_e7_rd` companion line.
- `zxnext.vhd:2803-2806` (`port_internal_rd_response` OR-tree, the gate
  that selects the internal port-read response over the expansion bus /
  floating bus) lists every readable port:

  ```vhdl
  port_internal_rd_response <= port_fe_rd or port_ff_rd or port_p3_float_rd or
     port_e3_rd or mf_port_en or port_eb_rd or port_243b_rd or ...;
  ```

  `port_e7_rd` is absent. So a host read of port 0xE7 produces
  `port_internal_rd_response = '0'`.

- `zxnext.vhd:2837-2840` (`port_rd_dat` OR-tree, the data-byte selector):

  ```vhdl
  port_rd_dat <= port_fe_rd_dat or port_ff_rd_dat or port_p3_float_rd_dat or
     port_e3_rd_dat or port_mf_rd_dat or port_eb_rd_dat or ...;
  ```

  No `port_e7_rd_dat` contributor.

- `zxnext.vhd:1868-1882` (`cpu_di` selector during IORQ): with
  `port_internal_rd_response = '0'` the read falls through to either
  `i_BUS_DI` (expansion bus enabled) or `cpu_di <= X"FF"` (line 1877 —
  default else: floating-bus `0xFF`).

Therefore on real Next hardware **a port 0xE7 read returns `0xFF`** (with
no expansion bus driving the byte) — the port is strictly write-only.

### Pre-fix behaviour (jnext)

`src/core/emulator.cpp:4043-4051` (pre-fix):

```cpp
port_.register_handler(0x00FF, 0x00E7,
    [this](uint16_t) -> uint8_t {
        if ((nextreg_.cached(0x83) & 0x08) == 0) return 0xFF;
        return spi_.read_cs();      // <-- leaks internal CS latch
    },
    [this](uint16_t, uint8_t val) {
        if ((nextreg_.cached(0x83) & 0x08) == 0) return;
        spi_.write_cs(val);
    });
```

The read handler returned `spi_.read_cs()` (= the host-side CS register
state, e.g. `0xFE` after `OUT (0xE7), 0xFE`). That leaks an internal
SPI-master register state that real Next firmware has no architectural
way to access — pure divergence from the VHDL signal map.

### Boot-path impact

Nil. TBBlue / NextZXOS / esxdos firmware writes port 0xE7 to drive CS
selection but never reads it. The CS register is host-side bookkeeping.

### Fix

Pass `nullptr` for the read callback so the dispatcher falls through to
`default_read_` (floating bus 0xFF) — mirroring the write-only-port
pattern already used for port 0x001F / 0x0037 (Kempston joystick:
read-only, write nullptr, the inverse of this case).

```cpp
port_.register_handler(0x00FF, 0x00E7,
    nullptr,
    [this](uint16_t, uint8_t val) {
        if ((nextreg_.cached(0x83) & 0x08) == 0) return;
        spi_.write_cs(val);
    });
```

### Discriminative regression test

`test/port/port_test.cpp` row **V16-DIVMMC-01** (added immediately after
`REG-12`):

1. Write `0xFE` to port 0xE7 (selects SD0 — the latch becomes `0xFE`).
2. Read back `spi_.read_cs()` to confirm the latch DID update (sanity
   that the write half is still wired).
3. Read port 0xE7 via `emu.port().in(0x00E7)`. Pre-fix returns the latch
   value `0xFE`; post-fix returns `0xFF` (floating-bus default).

Revert-check: stash-popped the `src/core/emulator.cpp` diff only,
rebuilt, ran `port_test` → **FAIL** with `latched_cs=0xfe in_0xE7=0xfe`.
Restored fix → PASS.

---

## V16-DIVMMC-02 — `find_in_directory` cluster-chain walk lacks cycle bound

### Class

**(c)** — latent for the canonical NextZXOS fixture (well-formed image);
defensive / robustness gap that lets a malformed or hostile image hang
the host process indefinitely.

### Spec / oracle

FAT32 specification (Microsoft FAT32 File System Specification rev 1.03,
August 6, 2000, plus Section 4 "FAT structures"): the FAT defines a
linked list of clusters, terminated by an EOC mark (`>= 0x0FFFFFF8`).
A well-formed FAT contains no cycles — but a malformed or hostile image
may. The host MUST defend against such cycles when walking any chain.

### Pre-fix behaviour (jnext)

`src/core/sd_rom_extractor.cpp` `find_in_directory` (the function that
walks a directory's cluster chain looking for a matching SFN):

```cpp
while (cluster >= 2 && cluster < FAT32_BAD_MARK) {
    if (!read_cluster(f, g, cluster, buf)) return false;
    for (uint32_t off = 0; off + 32 <= buf.size(); off += 32) {
        ...
        if (first == 0x00) return false;   // end-of-dir marker
        ...
        if (match_sfn(...)) { fill out; return true; }
    }
    const uint32_t nxt = fat_next(f, g, cluster);
    if (nxt == 0 || nxt >= FAT32_EOC_MARK) return false;
    cluster = nxt;
}
```

Exit conditions in the pre-fix code: read failure, end-of-directory
marker (`first == 0x00`), match found, or chain reaches EOC / cluster 0
/ bad mark. **None of these triggers if the FAT chain has a cycle and
none of the directory entries either matches the lookup key OR contains
the `0x00` end-of-directory marker.** Example pathological case:

- Cluster 2 → cluster 3 → cluster 2 (cycle).
- Both clusters filled with `ATTR_VOLUME_ID` directory entries (volume
  labels — skipped by the lookup loop).
- No `0x00` byte in either cluster.

The loop walks forever, hanging the host process. The
extract_sd_rom main loop (the file-data read path) already has a
`max_chain_len = (file_size / bytes_per_cluster) + 2` bound (lines
405-417) — but `find_in_directory` had no equivalent guard.

By contrast, every mainstream FAT32 reader (Linux kernel, FatFs,
ChibiOS) bounds the chain walk by the total number of cluster slots in
the FAT itself, since no chain can be longer than the cluster-count
representable in the FAT.

### Boot-path impact

Nil for the canonical NextZXOS fixture (`roms/nextzxos-1gb-fat32fix.img`)
— the image is well-formed and emerges cleanly. But a forensic /
user-supplied / hostile image with a cyclic directory chain would hang
jnext at startup (during DivMMC-ROM / Multiface-ROM extraction).

### Fix

Add an upper bound: the FAT contains exactly `fat_size_sectors *
bytes_per_sector / 4` 4-byte entries — the absolute maximum length any
cluster chain can have. If `find_in_directory` walks more clusters
than that, the chain MUST contain a cycle.

```cpp
const uint64_t max_chain_len =
    (static_cast<uint64_t>(g.fat_size_sectors) * g.bytes_per_sector) / 4;
uint64_t steps = 0;
while (cluster >= 2 && cluster < FAT32_BAD_MARK) {
    if (++steps > max_chain_len) {
        Log::emulator()->error(
            "sd_rom_extractor: directory cluster chain longer than "
            "total FAT entries ({} > {}) — malformed/cyclic FAT",
            steps, max_chain_len);
        return false;
    }
    ...
}
```

`fat_size_sectors > 0` is already validated in `parse_bpb` (line 135);
`bytes_per_sector` is one of {512, 1024, 2048, 4096} (line 124). So
`max_chain_len` is a positive value bounded by the partition geometry.

### Discriminative regression test

`test/sd_rom_extractor/sd_rom_extractor_test.cpp` row **SD-EXT-09 /
V16-DIVMMC-02** builds a synthetic FAT32 image in-memory:

- 7 sectors total: MBR + BPB + 1-sector FAT + 4 data clusters.
- BPB declares 1 sector/cluster, 1 reserved sector, 1 FAT sector, 1 FAT.
- FAT: `entry[2] = 3` and `entry[3] = 2` → 2-cluster cycle.
- Data clusters 2 + 3: filled with 16 × `ATTR_VOLUME_ID` directory
  entries each (skipped by the lookup loop). No `0x00` end-of-directory
  marker. No SFN match for the lookup key.

`extract_sd_rom(tmpl, "/anything.rom", out)` is called. Pre-fix the
call hangs forever (the loop walks 2 ↔ 3 ↔ 2 ↔ 3 ad infinitum).
Post-fix the bound trips at `steps = 129` (because
`fat_size_sectors * bytes_per_sector / 4 = 1 * 512 / 4 = 128`) and the
function returns false with a logged error.

Revert-check: stash-popped the `src/core/sd_rom_extractor.cpp` diff
only, rebuilt, ran `sd_rom_extractor_test` under `timeout 10s` →
**hang** (process killed by timeout, exit 143). Restored fix → PASS in
microseconds.

---

## Methodology — angles scrutinised in this pass

The prompt asked for fresh angles after 15 prior passes. I covered:

1. **VHDL gate matrix for port 0xE7 / 0xEB / 0xE3** — re-derived
   `port_internal_rd_response` and `port_rd_dat` OR-trees. **Found
   V16-DIVMMC-01.**
2. **VHDL ↔ JNext q-register pipeline for divmmc_automap_*_on** at
   `zxnext.vhd:4115-4135` (falling-edge clock for the `_q` shadow). The
   q-register clears on `cpu_m1_n='1'` and latches on `cpu_mreq_n='1'` —
   semantically equivalent to JNext's per-M1 `check_automap` model
   because all entry-point evaluation only happens at M1 fetches.
3. **VHDL line 148 (`automap` combinational output) gate decomposition**
   — verified jnext's `instant_match` correctly gates RST entries with
   `valid=1` on `main_path_eligible` and `valid=0` on `rom3_path_eligible`,
   matching the VHDL bucket split at `zxnext.vhd:2898-2902`. Also the
   NMI@0x0066 path, the $3Dxx wildcard, and the off-trigger gate on
   `i_automap_active`.
4. **CMD24 R1-bridge (`pending_write_after_r1_`) abort cases** — new
   command mid-CMD24 (between R1 and data phase) clears the bridge;
   CS deassert clears it; soft reset clears it. All paths verified.
5. **CMD12 STOP_TRANSMISSION mid-CMD18** — receive() default branch
   correctly aborts the multi-block stream and starts a new RECEIVING_CMD
   sequence. CMD12's R1b-busy semantics deliberately model `0xFF` (line
   idle) instead of `0x00` (busy) per the existing well-documented
   ZEsarUX-compat trade-off.
6. **CMD9 / CMD10 CSD / CID 16-byte register field-by-field** — verified
   bit positions per SD spec § 5.3.3 (CSD v2.0):
   - CSD_STRUCTURE bits 127:126, byte 0 = `0x40` ✓
   - TAAC byte 1 = `0x0E`, NSAC byte 2 = `0x00`, TRAN_SPEED byte 3 = `0x32` ✓
   - CCC[11:4] byte 4 = `0x5B`, CCC[3:0] | READ_BL_LEN byte 5 = `0x59` ✓
   - C_SIZE bits 69:48, mask 0x3F on byte 7 ✓
   - ERASE_BLK_EN bit 46 in byte 10 = `0x7F` (sector_size 0x7F) ✓
   - SECTOR_SIZE[0]=1 + WP_GRP_SIZE=0 byte 11 = `0x80` ✓
   - R2W_FACTOR=2 + WRITE_BL_LEN=9 (bytes 12+13) = `0x0A 0x40` ✓
7. **OCR layout** — bit 31 power-up done (post-init `0xC0`), bit 30 CCS,
   bits 23:15 voltage windows for 2.7-3.6V (`0xFF 0x80`). All correct.
8. **R7 layout** (CMD8) — byte 0 R1 (`0x00`/`0x01`), byte 1 cmd version
   `0x10`, byte 3 voltage `0x01`, byte 4 check pattern echo. All correct
   per V12-DIVMMC-03 / V14-DIVMMC-02 prior fixes.
9. **DivMMC enable transitions and latch clearing** — `apply_enabled_-
   transition_` correctly clears `automap_active_` /`automap_hold_` /
   `automap_held_` / `button_nmi_` / `retn_pending_clear_` on
   `enabled→disabled` edge, matching VHDL `i_automap_reset` semantics.
10. **DivMMC OR-latch for mapram (port 0xE3 bit 6)** — verified
    `mapram_ |= (val & 0x40) != 0` and `clear_mapram()` from NR 0x09
    bit 3 path. Read-back masks bits 5:4 to 0 per VHDL.
11. **DivMMC RETN delayed clear (`on_m1_retn_delay`)** — pending-flag
    state machine matches VHDL register-shift shape.
12. **SDcard `mount` / `unmount` / `reset` / `deselect` symmetric reset
    coverage** — all four paths reset every relevant FSM field
    (V8 / V11 / V15 prior fixes).
13. **SD CMD0 / ACMD41 / CMD55 chain** — `app_cmd_` cleared correctly;
    pass-9 fall-through to regular CMD switch for non-ACMD41 verified.
14. **CMD16 SET_BLOCKLEN** — arg=512 acks; arg≠512 returns illegal-
    command (V5 prior fix).
15. **CMD17 / CMD18 / CMD24 past-EOF rejections** — V12/V13/V14/V15
    prior fixes verified intact and correctly emit:
    - CMD17/18 initial: R1=0x40 + token 0x08
    - CMD18 mid-stream: token 0x08 + IDLE
    - CMD24: R1=0x40 (no data phase)
    - CMD24 RO file fstream failure: data response 0x0D
16. **`cmd_buf_` CRC byte** — not validated (CRC OFF in SPI mode by
    default, per SD spec § 4.5). CMD0 / CMD8 CRC validation deliberately
    skipped (WONT — TBBlue never asks for CRC).
17. **CMD55 multiple-stack semantic** — multiple CMD55s in a row each
    set `app_cmd_=true`. Spec-allowed.
18. **SPI write-then-read pipeline timing** — `write_data` immediately
    triggers `dev->receive(val)` and captures rx_data. `read_data`
    returns prev rx_data and triggers `dev->send()` for next byte.
    Equivalent observable sequence to VHDL one-cycle pipeline.
19. **SPI `port_e7` CS decode equivalence** — VHDL bits-1:0 strict-match
    branch + strict-equality `0xFB` / `0xF7` / `0x7F` branches all match
    JNext's case ladder. Sd_swap inversion correctly applied to the SD
    branches only.
20. **`spi_master.vhd` reset semantics** — `i_reset` hardwired to '0'
    in zxnext.vhd:3285 (V12 prior fix recognised this for `rx_data_`).
    Confirmed nothing else relies on the unused reset clause.
21. **Layer 2 read-map gate on ROM3-conditional automap path** —
    `set_layer2_map_read` correctly fed from `Mmu::l2_read_enable()`
    on every port 0x123B write.
22. **DivMMC `is_active` gate** — checks `port_io_enable_` only (i_en
    in VHDL); CONMEM goes through this gate alone, automap goes through
    the combined enable. Matches VHDL.
23. **DivMMC `o_divmmc_rdonly` semantics** — page0 always read-only;
    bank 3 + mapram read-only. Verified.
24. **NR 0xB8 / 0xB9 / 0xBA / 0xBB write & read** — VHDL pass-through;
    JNext write_handler returns `v` and no read_handler → cache returns
    last-written byte. Matches VHDL `port_253b_dat` mux.
25. **NR 0x0A bit 5 sd_swap config_mode gate** — matches VHDL :5193
    config_mode-conditional commit (V11-NMP-02 prior fix verified).
26. **FAT32 reader degenerate cases** — MBR signature, partition type,
    BPB validation, FAT chain bounds, max_chain_len bound on file-data
    walk. **Found V16-DIVMMC-02** (no equivalent bound on directory
    walk).
27. **CMD13 SEND_STATUS R2 layout** — NCR + R1 + status byte = 3
    bytes, correct.
28. **CMD58 OCR field-by-field** as item 7 above.
29. **CMD24 data-token receipt explicit flag** (`data_token_received_`)
    — V12-DIVMMC-06 prior fix verified intact.
30. **SD CMD numbering uniqueness** — process_command switch covers
    every CMD jnext supports; default returns R1=illegal (V8 prior fix).

## Areas explicitly NOT in scope

- The 15 prior pass reports (verify1..verify15) — BLIND audit per
  prompt rule.
- Any work outside `src/peripheral/divmmc.{cpp,h}`,
  `src/peripheral/sd_card.{cpp,h}`, `src/peripheral/spi.{cpp,h}`,
  `src/core/sd_rom_extractor.{cpp,h}`, the DivMMC / SD / SPI port-
  write dispatch in `src/core/emulator.cpp`, NRs $B8/$B9/$BA/$BB/$C0,
  and the DivMMC trigger entry-points.

## Build & test results

### Pre-fix baseline (HEAD `267764b`)

- `cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON` — OK
- `cmake --build build -j$(nproc)` — OK
- `ctest --test-dir build --output-on-failure` — **38/38 PASS**
- `./build/test/fuse_z80_test build/test/fuse` — **1356/1356 PASS**

### Post-fix (after V16-DIVMMC-01 + V16-DIVMMC-02 land)

- `ctest --test-dir build --output-on-failure` — **38/38 PASS**
  (port_test, sd_rom_extractor_test both green; both contain new
  discriminative rows)
- `./build/test/fuse_z80_test build/test/fuse` — **1356/1356 PASS**

### Revert-check (stash-pop source-only diffs, keep tests)

- Pre-fix `port_test` → V16-DIVMMC-01 row FAILs:
  `latched_cs=0xfe in_0xE7=0xfe expected_in=0xFF`.
- Pre-fix `sd_rom_extractor_test` → process **hangs** under
  `timeout 10s` (exit 143).
- Restored fixes → both tests green.

Both tests are properly discriminative.
