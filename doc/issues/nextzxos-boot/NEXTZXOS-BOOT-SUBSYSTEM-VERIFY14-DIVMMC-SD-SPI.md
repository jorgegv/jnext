# NextZXOS boot subsystem — Pass-14 verify-audit, DivMMC + SD + SPI slice

**Date**: 2026-05-10
**Branch**: `task2/verify14-divmmc-sd-spi` (off integration HEAD `73c3146`)
**Worktree**: `.claude/worktrees/task2-verify14-divmmc-sd-spi`
**Pass kind**: BLIND audit (no prior pass reports read)

## Scope

- `src/peripheral/divmmc.{cpp,h}` — DivMMC peripheral, automap pipeline, port 0xE3
- `src/peripheral/sd_card.{cpp,h}` — SD card SPI-mode emulation (CMD0/8/9/10/12/13/16/17/18/23/24/55/58 + ACMD41)
- `src/peripheral/spi.{cpp,h}` — SPI master + chip-select decode
- `src/core/sd_rom_extractor.{cpp,h}` — host-side FAT32 reader
- DivMMC/SD/SPI port-write dispatch in `src/core/emulator.cpp`
- DivMMC NRs $B8/$B9/$BA/$BB/$C0
- DivMMC trigger entry points (RST $08 family + 0x0066 NMI + 0x04C6/0x0562/0x04D7/0x056A tape + 0x3Dxx wildcard + 0x1FF8..0x1FFF off range)

## Methodology

Per Pass-14 prompt: **light-trodden angles** since 13 prior passes have been done.
Forbidden from reading prior pass reports. VHDL = oracle (`device/divmmc.vhd`,
`serial/spi_master.vhd`, `zxnext.vhd`). SD spec = oracle for SD-card behavior
(SD Physical Layer Simplified Spec § 4 / § 5 / § 7). Release-mode build
(`-DCMAKE_BUILD_TYPE=Release`).

Angles re-walked fresh:

1. **DivMMC entry-point matrix exhaustive** — every byte × automap state ×
   NMI gate × cmd-set/cmd-clr × alt-trigger × main/rom3.
2. **DivMMC RST $08 trigger interaction with HALT** — HALT continually
   generates M1 fetches at the HALT PC; if PC matches an RST entry the
   automap re-triggers each cycle. Verified: matches VHDL (line 128 clocks
   on every M1+MREQ-low; HALT-internal cycles fire equivalently).
   No finding.
3. **DivMMC enable bit ($B8 b0) flush behavior** — `apply_enabled_transition_`
   clears `automap_active_/hold_/held_/button_nmi_/retn_pending_clear_` on
   true→false edge. Matches VHDL i_automap_reset clear path
   (divmmc.vhd:108,126,139). No finding.
4. **CMD52/CMD53** (SDIO) — not implemented; not required by SD memory mode.
   SDIO out of scope.
5. **CMD55 + ACMD coverage** — only ACMD41 is handled; ACMD13/22/23/42/51
   fall through to regular CMD switch per Pass-9 comment. TBBlue/NextZXOS
   doesn't exercise other ACMDs. **WONT-classified**, latent.
6. **CRC error injection** — not modeled (cmd_buf_[5] CRC byte ignored
   for all commands). WONT SD-01 explicitly covers this.
7. **OCR busy bit timing** — `cmd58_read_ocr` returns bit 31 = 0 (busy)
   when not initialized, bit 31 = 1 (ready) when initialized. Correct.
   ACMD41 sets `initialized_=true` immediately (firmware-friendly,
   diverges from real card's poll loop but BC-classified latent).
8. **SD physical layer voltage signal** — CMD8 R7 echo correctly reflects
   voltage accepted (byte 2 low nibble = 0x1 = 2.7-3.6V) and check-pattern
   echo. CMD58 OCR voltage windows (bits 23:15 set) correct.
9. **Multi-block read after CMD12** — CMD12 properly aborts CMD18; tested
   in CMD18-03. State machine transitions to RESPONDING with stuff
   bytes + R1, then IDLE. No finding.
10. **CMD17 → CMD13 → CMD17 sequence robustness** — CMD13 returns R2
    response cleanly; subsequent CMD17 works. No finding.
11. **SD card power cycle simulation** — `mount()` + `unmount()` both
    invoke `reset()` per Pass-5 + Pass-8 fixes. Persistent
    `initialized_` is correctly cleared on `reset()`. No finding.
12. **FAT32 reader exotic cases** — `extract_sd_rom` handles empty file
    (size==0 → success with empty buffer at line 391-393), zero-cluster
    non-empty (rejected line 395-398), multi-cluster chain
    (`max_chain_len = (file_size / bytes_per_cluster) + 2` bound),
    `.`/`..` first_cluster=0 → root-cluster remap (line 379). No finding.
13. **sd_rom_extractor name canonicalization** — `make_sfn_key` handles
    8.3 truncation, uppercase, dot-separator, special `.`/`..`. The 0x05
    aliasing (Asian filesystem support) is honored at `match_sfn`
    (line 221: `if (en[0] == 0x05) en[0] = 0xE5;`). No finding.
14. **DivMMC RAM read-only when MAPRAM bit set** — slot 0 always read-only
    (page0 → rdonly), bank 3 in slot 1 read-only when mapram set. Matches
    VHDL line 100. No finding.

## Findings

### V14-DIVMMC-01 — CMD18 mid-stream past-EOF emits 0xFF instead of data error token 0x08

**Class**: (c) — latent for boot path; spec divergence

**Subsystem**: `src/peripheral/sd_card.cpp` — `send()` SENDING_DATA
multi-block past-EOF branch (pre-fix lines 322-333).

**VHDL/spec authority**: SD Physical Layer Simplified Spec § 7.3.3.3
(Data Error Token format) — when the card cannot deliver the requested
data block (out-of-range, ECC failure, CC error, generic error), it sends
a 1-byte error token in place of the 0xFE start-of-block token. The
OUT_OF_RANGE-only token is `0x08`.

**Pre-fix behavior**: CMD18 mid-stream past-EOF (next block's
`byte_addr + 512 > file_size_`) silently aborted by emitting `0xFF` and
transitioning to IDLE. The host's "skip until non-0xFF then expect 0xFE"
token-poll loop (e.g. `rcvr_datablock` in TBBlue diskio.c:156-164) would
never see either `0xFE` (continued stream) or `0x08` (error signal) and
would time out — diverging from spec-compliant SD cards that emit `0x08`
as a discriminative error token.

**Asymmetry with prior Pass fixes**: V12-DIVMMC-04 + V13-DIVMMC-01 already
fix the CMD17 / CMD18-initial-block past-EOF cases (`queue_r1(0x40)` +
emit `0x08`). The mid-stream case was not covered.

**Boot-path impact**: nil — TBBlue / FatFs / esxdos never read past EOF
mid-stream. Class-(c) latent, but real-spec divergence.

**Fix (one-liner discriminative)**: `return 0xFF;` → `return 0x08;` in
the past-EOF branch of `send()`. The state still transitions to IDLE so
no spurious 0xFE follows.

**Regression test**: `test/sdcard/sdcard_test.cpp::test_sd_26_cmd18_midstream_past_eof_error_token`
— 4-sector image, CMD18 starts at sector 3 (last valid). After block 3's
CRC, samples up to 16 post-CRC bytes for the FIRST non-`0xFF` byte. Pre-fix
sees only `0xFF`s; post-fix sees `0x08`. Asserts `signal_byte == 0x08` AND
no spurious `0xFE` (would mean another block was being prepared).

### V14-DIVMMC-02 — CMD8 R7 byte 0 (R1 portion) hard-coded to 0x01, ignores `initialized_`

**Class**: (c) — latent for boot path; spec divergence

**Subsystem**: `src/peripheral/sd_card.cpp` — `cmd8_send_if_cond()`
(pre-fix line 480).

**VHDL/spec authority**: SD Physical Layer Simplified Spec § 7.3.2.6
(Format R7 — Card Interface Condition) explicitly states the R7 register
is preceded by an R1-format byte (Table 7-9). R1 bit 0 = "in idle state",
which reflects the live card state — `1` before ACMD41 init completes,
`0` after.

**Pre-fix behavior**: `resp_buf_ = { 0xFF, 0x01, 0x10, 0x00, 0x01, check }`
hard-codes R1 = `0x01` regardless of `initialized_`. A CMD8 issued AFTER
successful ACMD41 init still reports idle.

**Asymmetry with siblings**: All other CMD handlers (CMD16, CMD23, CMD24,
CMD55, CMD58, CMD13, CMD0/CMD17/CMD18 success paths) correctly use
`initialized_ ? 0x00 : 0x01`. Only CMD8 was hard-coded. Pass-12's
V12-DIVMMC-03 fixed the cmd-version field (byte 1 = 0x10) but did not
touch the R1 byte.

**Boot-path impact**: nil — TBBlue/NextZXOS / FatFs only issue CMD8
once during init (before ACMD41), so `initialized_=false` always when
CMD8 fires. A strict host that re-probes CMD8 after init would see
misleading idle status. Class-(c) latent.

**Fix**: replace hard-coded `0x01` with `initialized_ ? 0x00 : 0x01`.

**Regression test**: `test/sdcard/sdcard_test.cpp::test_sd_27_cmd8_r7_r1_post_init`
— full init sequence (CMD0 → CMD8 → CMD55 + ACMD41 → CMD58), confirm
`initialized_=true` via CMD17 R1=0x00, then re-issue CMD8 and assert
its R1 byte is `0x00` (post-init). Pre-fix asserts `r1_post_init=0x00`
fails (gets 0x01). Existing SD-22 test still passes (its CMD0 precedes
CMD8 → `initialized_=false` → R1=0x01 expected).

## Class-(d) (architectural) — listed only

None identified in this pass for the DivMMC/SD/SPI slice.

The known class-(d) catalogue items relevant to this slice (carried from
prior passes' inheritance, NOT touched here):

- **DivMMC SPI cycle FSM** — spi_master is byte-level, not cycle-accurate.
  `spi_wait_n()` always returns true (always-idle). Cycle-accurate consumer
  (DMA-via-SPI throttling) would need an FSM rewrite.

These are not new findings; they are listed for traceability only.

## Tests

Release-mode build:

```
ctest --test-dir build --output-on-failure
38/38 tests passed, 0 failed (FUSE Z80: 1356/1356)
sdcard_test:  28/28 passed, 0 failed, 0 skipped
divmmc_test:  unchanged from integration HEAD
```

`./build/test/fuse_z80_test build/test/fuse` → `Total: 1356  Passed: 1356
Failed: 0  Skipped: 0`.

Two new SD-card test rows:
- **SD-26** — CMD18 mid-stream past-EOF emits data error token 0x08
  (V14-DIVMMC-01).
- **SD-27** — CMD8 R7 R1-byte reflects `initialized_` (V14-DIVMMC-02).

## Defensible-zero check

All Pass-14 angles enumerated above were re-walked. Two genuine findings
surfaced (both class-(c)). Other angles either matched VHDL/spec
faithfully or are documented WONT-classifications (CRC validation, CMD9/CMD10
not used by firmware, ACMD13/22/23/42/51 not exercised, HCS=0 SDSC
fallback not modeled, CMD25 multi-block-write not modeled, MMC-mode
fallback not modeled).

The honest convergence target ("0 pending of any class") is NOT yet met
because the two class-(c) divergences from spec were silently present and
required a fresh audit pass to discover. They are now corrected and
covered by discriminative regression tests.

## Summary

| Class | Count |
|-------|-------|
| (a)   | 0     |
| (b)   | 0     |
| (c)   | 2     |
| (d)   | 0     |

Both class-(c) findings (V14-DIVMMC-01, V14-DIVMMC-02) corrected with
VHDL/spec-faithful fixes + discriminative regression tests in the same
commit.
