# Pass-15 Verify-Audit Report — DivMMC + SD + SPI Subsystem

**Date:** 2026-05-10
**Branch:** `task2/verify15-divmmc-sd-spi`
**Worktree:** `.claude/worktrees/task2-verify15-divmmc-sd-spi`
**Base commit:** `a86c671` (integration HEAD after Pass-14)
**Auditor:** Pass-15 blind audit agent (no access to prior Pass-1..14
report files, no probes, build in Release mode).

## Findings

### V15-DIVMMC-01 — class-(c) — CMD24 silently lies about write success when host stream rejects the write

**File:** `src/peripheral/sd_card.cpp` — `SdCardDevice::receive()`
RECEIVING_DATA branch (the post-CRC commit block).

**Spec reference:** SD Physical Layer Simplified Spec § 7.3.3.3 *Data
Response Token format*. Token is `0bxxx0_sss1` where the status code
`sss` is one of:

  * `010` = **data accepted** → 0x05
  * `101` = **data rejected (CRC error)** → 0x0B
  * `110` = **data rejected (write error)** → 0x0D

A real SD card emits `0x0D` whenever the card cannot physically commit
the host's 512-byte payload — the canonical case being a card with a
mechanical write-protect tab engaged, or a flash erase/program failure.
The closest analogue on the host emulation side is `std::fstream::write()`
silently setting `failbit` because the file was opened in fall-back
read-only mode (mount path at `sd_card.cpp:33`, e.g. user does not have
write permission on the image, or the image lives on a read-only
filesystem) or the underlying disk failed (full, I/O error, etc.).

**Pre-fix:** the CMD24 commit block computed `write_ok = (byte_addr +
512 <= file_size_)` (= the past-EOF guard) and unconditionally invoked
`file_.write()` for the in-bounds branch, then emitted the data
response token based ONLY on `write_ok`:

```cpp
bool write_ok = (byte_addr + 512 <= file_size_);
if (write_ok) {
    file_.seekp(...);
    file_.write(reinterpret_cast<const char*>(data_block_), 512);
    file_.flush();
}
...
resp_buf_ = { static_cast<uint8_t>(write_ok ? 0x05 : 0x0D) };
```

`file_.good()` was never checked. When the fstream's `failbit` was set
(the image was opened RO via the fall-back path at `mount()` line 33),
the `write()` call is silently a no-op but JNEXT still emitted `0x05`
(data accepted) — telling the host the write succeeded when in fact
nothing physically committed.

**Real-card divergence:** on a write-protected card, the CMD24 R1
itself can be 0x00 (the command is recognized and accepted), but the
DATA RESPONSE TOKEN must be `0x0D` (write error) per § 7.3.3.3. JNEXT's
`0x05` lied about commit, so any host code that polled the data
response token to verify write success (FatFs `disk_write` does this)
would see false positive on every RO-mounted image — silently corrupting
host expectations.

**Boot-path impact:** zero — the canonical NextZXOS fixture
(`roms/nextzxos-1gb-fat32fix.img`) is opened with default permissions,
and the firmware's writes are in-bounds. Class-(c) latent. But a
forensic scenario (analyst hands jnext a RO-locked image to study
filesystem state without modifying it), an emulator-test scenario
(intentional RO mount of a system image), or a real I/O error during a
runtime write would all hit this — and would do so silently.

**Fix:** check `file_.good()` after the `write()` + `flush()` pair. If
the stream rejected the write, set `write_ok = false` so the data
response token comes back as `0x0D`, and clear the failbit via
`file_.clear()` so subsequent reads (CMD17/CMD18 `seekg`/`read`) on
the same stream are not short-circuited by the sticky failbit.

```cpp
file_.write(reinterpret_cast<const char*>(data_block_), 512);
file_.flush();
if (!file_.good()) {
    sd_log()->warn("CMD24 host-side write failed at sector={} ...");
    write_ok = false;
    file_.clear();
}
```

Symmetric with V12-DIVMMC-02 (past-EOF data-response token), V12-
DIVMMC-04 (CMD17 past-EOF R1 PARAMETER_ERROR), V13-DIVMMC-01 (CMD24
past-EOF R1 PARAMETER_ERROR) and V14-DIVMMC-01 (CMD18 mid-stream
past-EOF data error token 0x08).

**Test:** `SD-28` in `test/sdcard/sdcard_test.cpp` —
`test_sd_28_cmd24_ro_image_write_error()`.

Discriminative shape:

  1. Build a fresh 8-sector temp image, then `chmod(0444)` (read-only).
  2. `mount()` falls through to RO mode (returns true, mounted is set,
     `file_size_ = 4096`).
  3. After standard CMD0/8/55/41/58 init, issue CMD24 sector=0 (in-
     bounds: R1 path is OK so the data phase starts).
  4. Send `0xFE + 512 × 0xAA + 2 CRC` bytes.
  5. Poll for the data response token.
     * **Pre-fix:** `0x05` (data accepted, lying — the write was silently
       discarded by the stream's failbit).
     * **Post-fix:** `0x0D` (write error — the failbit was caught and
       reflected to the host).
  6. Symmetric guard: re-mount the same image with `chmod(0644)` (RW),
     same CMD24 must still emit R1=0x00 + `0x05` (data accepted).

The post-fix run emits `Total: 29 Passed: 29 Failed: 0`. Reverting just
the `sd_card.cpp` fix (keeping the test) yields `Total: 29 Passed: 28
Failed: 1` with `r1=0 resp=5 r1_rw=0 resp_rw=5` — the discriminator
fires exactly on the pre-fix shape.

## Build & test results

Configuration: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`.

  * `cmake --build build -j$(nproc)`: clean, no warnings touching the
    audited subsystem.
  * `ctest --test-dir build --output-on-failure`: **38/38 passed, 0
    failed**.
  * `./build/test/fuse_z80_test build/test/fuse`: **1356/1356 passed,
    0 failed**.
  * `./build/test/sdcard_test` directly: **29/29 passed, 0 failed**
    (28 pre-existing rows + new SD-28).

## Methodology — coverage at Pass-15

Honest convergence is the goal. Pass-15 walked the audit angles flagged
in the orchestrator prompt as candidates for prior-pass blind spots:

  * **DivMMC entry-point matrix re-walk** — every byte × automap × NMI ×
    cmd-set/cmd-clr × alt-trigger × main/rom3 was traced through
    `DivMmc::check_automap` and the `entry_points_0_/_1_` /
    `entry_valid_0_` / `entry_timing_0_` decode against zxnext.vhd:
    2848-2908 + divmmc.vhd:120-148. The two-stage hold/held latch
    matches VHDL semantics; the off-trigger gate
    `main_path_eligible && pc∈[0x1FF8..0x1FFF]` faithfully gates on
    `i_automap_active = sram_pre_override(2)` per VHDL line 131.
  * **DivMMC RST $08 + HALT** — the HALT instruction is just an
    iterated M1 fetch of opcode 0x76. Interrupts cause the canonical
    PC=0x0066 (NMI) or PC=0x0038 (INT IM1) M1 fetch, which feeds
    `check_automap` exactly like a normal RST. No special-casing
    needed; no divergence found.
  * **DivMMC RST $08 + EI grace** — EI's interrupt-disable window
    persists for one instruction after EI. The interrupt service
    routine's first M1 (at 0x0038 or 0x0066) is what
    `check_automap` sees. EI itself has no effect on the automap
    decode. No divergence.
  * **DivMMC enable bit ($B8 b0) flush** — the actual port-IO/automap
    enable lever pair is `port_io_enable_` (NR 0x83 b0) and
    `nr_0a_4_enable_` (NR 0x0A b4). NR 0xB8 bit 0 is just the entry-
    point gate for RST 0x00, latched into `entry_points_0_`. No
    flush-on-disable needed for that bit; the actual disable path
    (`apply_enabled_transition_`) clears `automap_active_` /
    `automap_hold_` / `automap_held_` / `button_nmi_` /
    `retn_pending_clear_` on the prev_enabled→disabled edge,
    matching divmmc.vhd:108,126,139 i_automap_reset semantics.
  * **NR $B8/$B9/$BA/$BB write-only / read-back behavior** — VHDL
    zxnext.vhd:6218-6227 reads back the stored register values; JNEXT
    routes through the default cache path (`regs_[reg]`) since no
    explicit read_handler is registered. Read-back returns last-
    written. Matches VHDL.
  * **SD CMD12 STOP_TRANSMISSION** — JNEXT emits 8 stuff bytes + NCR +
    R1 + persistent_response_byte_=0xFF. Spec § 7.3.1.5 says "stuff
    byte" (singular) followed by R1b (R1 + busy). JNEXT's 8 stuff bytes
    is a documented over-tolerance for hosts that poll for non-0xFF;
    skipping the BUSY phase is acceptable since the supervisor's
    bank-2 $196D-$1978 poll loop looks for non-zero (treating 0x00 as
    busy). Comment in `cmd12_stop_transmission` documents the design
    choice. No divergence.
  * **SD CMD13 STATUS** — emits NCR + R1 + 1 status byte. Spec § 7.3.1.6
    R2 is exactly 2 bytes (R1 + status). JNEXT's status=0x00 (no errors)
    is correct for the happy path. Bit 5 (WP violation) is set after a
    failed write attempt, not just because the card is WP-locked — so
    initial CMD13 returning 0x00 is spec-correct. No divergence.
  * **SD multi-block CRC field** — JNEXT emits 2 zero bytes for the
    CRC16 between blocks (line 312 in send()). FatFs/TBBlue/esxdos do
    not validate CRC16 on data blocks (CMD59 not exercised — see
    "WONT SD-01" rationale in sdcard_test.cpp). Real cards with
    CRC validation off accept any 2 bytes. JNEXT is spec-permissible
    for cards-without-CRC-validation (the default). No divergence at
    the firmware level. Class-(b) hypothetical — would surface only
    if a Z80 program actively enables CRC via CMD59 and validates.
  * **SPI clock divider mid-transfer** — JNEXT models SPI as
    instantaneous byte exchange with no clock divider FSM
    (`spi_master.cpp` is byte-level). VHDL has clock-divider state
    in spi_master.vhd that JNEXT collapses. Not a per-pass-15
    finding (architectural simplification documented in
    `SpiMaster::spi_wait_n` comment).
  * **SPI mode transitions** — SD/MMC/HighSpeed mode switch is not
    modeled; the card is fixed-SDHC. CMD8 and OCR declare SDHC; the
    initialization sequence is followed faithfully. WONT MMC-02 +
    WONT MMC-03 in `sdcard_test.cpp` document the deliberate
    SDHC-only choice.
  * **SD power cycle simulation** — `mount()` and `unmount()` call
    `reset()` which clears all protocol state EXCEPT the device
    bindings (per Pass-7 fix). `reset()` itself does not invoke
    `deselect()` on the device — but that's the SD card's own state
    only. The SpiMaster's `reset()` walks every previously-selected
    device and calls `deselect()` (Pass-11 fix). No additional
    finding here.
  * **conmem vs mapram precedence per VHDL exact ordering** —
    `DivMmc::is_ram_mapped`/`is_read_only`/`read`/`write` faithfully
    follow divmmc.vhd:88-100. Slot 0 always read-only (page0 OR
    (mapram=1 AND ram_bank=3)). Slot 1 read-only only when
    mapram=1 AND bank_=3 (matching ram_bank=3 in slot 1). No divergence.
  * **Multi-overlay precedence: DivMMC ROM > Multiface ROM > altrom >
    MMU > Layer 2** — owned by Mmu, not by DivMMC; Pass-15 audited
    only the DivMMC's `is_active()` / `is_rom_mapped()` /
    `is_ram_mapped()` outputs and confirmed they reflect the VHDL
    `o_divmmc_rom_en` / `o_divmmc_ram_en` outputs (gated on
    `port_io_enable_` per VHDL line 98 / zxnext.vhd:4147).
  * **SD-card image bounds at near-end** — JNEXT's `byte_addr + 512 >
    file_size_` past-EOF guard is correct (off-by-one verified at
    sectors `(file_size_/512) - 1` and `file_size_/512`).
  * **FAT32 reader exotic cases** — Pass-15 reviewed `extract_sd_rom`:
    - Empty file (`file_size == 0`): returns true with empty buffer
      (line 390-393). Correct.
    - Single-cluster file: `max_chain_len = 0 + 2 = 2` accommodates.
      Correct.
    - Multi-cluster spanning >1 FAT sector: `fat_next` walks the
      chain via direct byte-offset reads into the FAT region,
      crossing FAT sector boundaries transparently. Correct.
    - Cluster cycle protection: `max_chain_len = (file_size /
      bytes_per_cluster) + 2` with `++steps > max_chain_len` guard
      (line 415-418). Correct.
    - Reserved cluster 1 / cluster 0 in dirent: rejected at line 395
      with `if (file_cluster < 2)`. Correct.
  * **sd_rom_extractor case-insensitive lookup** — `make_sfn_key`
    uppercases the input, matches against FAT raw bytes (which are
    stored uppercase per spec). Correct. The 4-char-extension
    truncation behavior is a documented limitation, not a Pass-15
    finding.
  * **DivMMC RAM read-only when MAPRAM bit set** — bank 3 in slot 1
    with mapram=1 is read-only per VHDL line 100; JNEXT's
    `is_read_only` and `write` both honor this. Correct.

The single Pass-15 finding (V15-DIVMMC-01) lives in the SD-card
write-failure-detection space, which the prior 14 passes had not
specifically exercised — the past-EOF and pre-token bands of the
CMD24 path are well-covered, but the post-write-stream-state band
(checking `file_.good()` after a successful past-EOF gate) was a
genuine blind spot.

## Honest convergence claim

Pass-15 surfaced 1 class-(c) finding (V15-DIVMMC-01) with a discriminative
regression test (SD-28) committed in the same commit as the fix.

The audit is **not** a defensible-zero — there was a real residual
finding to surface — but it is a small, latent, RO-mount-specific
divergence with no boot-path impact. Subject to reviewer approval, this
finding closes the known-class-(a/b/c) catalog for the DivMMC / SD /
SPI subsystem on the audit angles enumerated above.

| ID                | Class | Status   | Test ID |
|-------------------|-------|----------|---------|
| V15-DIVMMC-01     | (c)   | FIXED    | SD-28   |

## Final return

```json
{
  "findings": 1,
  "class_a": 0,
  "class_b": 0,
  "class_c": 1,
  "class_d": 0,
  "tests_passed": true,
  "head_sha": "<filled-after-commit>",
  "report_path": "doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY15-DIVMMC-SD-SPI.md"
}
```
