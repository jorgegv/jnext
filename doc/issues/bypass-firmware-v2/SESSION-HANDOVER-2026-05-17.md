# Task 18 — Session Handover (2026-05-17 EOD)

**Branch:** `bypass-firmware-exploration-v2` at `12103396` (worktree
`.claude/worktrees/bypass-firmware-exploration-v2`).
**+2 commits vs main**, NOT pushed.

## TL;DR — Major milestone

`./build/jnext --machine next --sd-card roms/nextzxos-1gb-fat32fix.img
--bypass-tbblue-fw` boots **NextZXOS** for the FIRST TIME EVER in jnext.

The Z80 runs `enNextZX.rom` from SRAM, completes 196 SD CMD17 reads
(MBR/FAT/data clusters), settles into its IM2 idle loop with the
canonical ISR pattern (PCs `$3F39 / $3CFC / $0452 / $0456`). The 3-week
G46(b) "wrong nextboot.rom" blocker is **sidestepped entirely**.

Open gap: the screen stays uniform gray — the OS booted but the
rendering pipeline isn't painting (BASIC prompt / NextZXOS menu).
**This is a distinct, narrower problem** than G46(b) was.

## What this session built

3 commits across 2 functional units:

### Investigation (`1da146c9`)

Three parallel agents established the empirical groundwork. Doc set:
- `doc/issues/bypass-firmware-v2/CSPECT-STATE-CAPTURE.md` — CSpect cold-reset and post-boot DZRP captures (confirms NR $03=$33 cold-reset is CSpect's VHDL deviation; jnext is VHDL-correct).
- `doc/issues/bypass-firmware-v2/JNEXT-COLD-RESET-STATE.md` — full NR-sweep / MMU-slot / Z80-reg / SRAM baseline of jnext init().
- `doc/issues/bypass-firmware-v2/PLAN-AUDIT.md` — re-audit of the prior `doc/design/FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` plan; Q1/Q2/Q3 RESOLVED, Branch 1/2/3/4 sized at ~5/50-100/30-50/100-150 LOC.

### Implementation (`c0a0ff1b`)

75 LOC across 3 files implements FUTURE plan Branches 1+2+3:
- `src/core/emulator_config.h` — new `bool bypass_tbblue_fw = false`.
- `src/main.cpp` — `--bypass-tbblue-fw` CLI flag + help-text + plumb.
- `src/core/emulator.cpp` — 3 gates:
  - Machine ROM loader: swap `48.rom` → `enNextZX.rom` (64 KB, 4 banks) for ZXN_ISSUE2 when bypass is set.
  - Boot ROM overlay: skipped in bypass mode (`!cfg.bypass_tbblue_fw` gate).
  - End of `init()`: write NR $03=$B3 via the registered handler — single write commits +3 machine type, clears config_mode, disables boot ROM, mirrors config_mode into Mmu.

### Review + nit-fixes (`12103396`)

- `doc/issues/bypass-firmware-v2/REVIEW.md` — independent reviewer's verdict APPROVE-WITH-NITS modulo Finding 2.
- `doc/issues/bypass-firmware-v2/REVIEW-REBUTTAL.md` — empirical rebuttal of Finding 2 (`enNextZX.rom` exists at cluster 10321, 65 536 bytes; load runtime log + Z80 trace confirm).
- Comment clarifications for accepted Findings 1, 4.
- `test/task18_baseline/bypass_state_test.cpp` — 5-assertion discriminative regression test (registered with CTest as `task18_bypass_state`).

## Test triplet @ `12103396`

- **ctest**: 39/39 PASS (includes new `task18_bypass_state`).
- **FUSE Z80 opcode suite**: 1356/1356 PASS.
- **Screenshot regression**: RUNNING at writing time; result to be appended.

## Open question: blank-gray video

Empirical state at idle (`./build/jnext --bypass-tbblue-fw` with screen
captures at t=10s/40s/60s — all identical, md5 `afc4f6a42c74c7c7...`):

- Z80 ISR loop active (interrupt fires every ~9 ms).
- 196 SD reads completed (CMD0/8/55/41/58/9 SD-card init × ~6 cycles
  followed by ~190 CMD17 single-block reads at MBR + FAT + data clusters
  including offsets 0x052b4a00, 0x051ffa00 — i.e. NextZXOS loaded
  multiple files from the canonical SD image).
- NextZXOS made writes to NRs $03, $05, $06, $07, $08, $0A, $11-$1C,
  $26, $27, $2F-$34, $40-$44, $52-$57, $68, $6B, $78, $80-$85, $8A,
  $8C, $8E, $8F, $B8-$BB, $C0, $D8.
- Screen is uniform gray (same color as jnext's "white border" default
  in headless render — i.e. the **whole image** is being filled with
  border color, not just the border).

This means **ULA layer is not painting** in bypass mode. Either:

1. NextZXOS is in a shadow-screen state that jnext's renderer isn't reading from.
2. NextZXOS disabled the ULA layer via NR $69 (Layer 2 / display ctrl).
3. NextZXOS painted attributes/pixels to a SRAM page that isn't the active VRAM bank.
4. NextZXOS hasn't reached the screen-paint phase yet (e.g. stuck waiting on a CMD-completion flag that real firmware sets).

**Next-session priorities for video:**

1. Dump `ram_.page_ptr(5)` (legacy bank 5 = ULA screen at $4000-$5AFF)
   at t=10s in bypass mode. If it contains BASIC prompt bytes / attributes,
   the screen IS painted and jnext's renderer is failing to read it.
   If zero, the OS hasn't reached the paint phase.

2. Capture `nextreg_.cached(0x69)` and `mmu_.shadow_screen_active()`
   (or equivalent) at exit. If shadow-screen is on, dump page 7 (bank 7
   = shadow VRAM) and re-check.

3. Add CSpect-side screenshot via DZRP at t=10s for direct
   apples-to-apples comparison. If CSpect's screen is ALSO gray at the
   equivalent moment (e.g. NextZXOS is waiting for SPACE-bar timeout
   countdown), then we just need a longer delay or a `--keypress SPACE
   --keypress-after 3s` workaround.

4. Look at boot.c lines 247-277 (`display_bootscreen()`) — what
   tbblue.fw paints to set up the "Press SPACEBAR for menu" screen.
   If our bypass needs to paint something to give NextZXOS a non-blank
   initial state, that's a small addition.

## Files touched (cumulative branch diff vs main)

```
src/core/emulator.cpp                              | 77 ++++++++++++++++-
src/core/emulator_config.h                         | 10 +++
src/main.cpp                                       |  9 ++
test/CMakeLists.txt                                | 27 ++++++
test/task18_baseline/bypass_state_test.cpp         | 92 ++++++++++++++++++++
test/task18_baseline/cold_reset_probe.cpp          | 158 ++++++++++++++++++++++
doc/issues/bypass-firmware-v2/CSPECT-STATE-CAPTURE.md     | 282 ++++++++
doc/issues/bypass-firmware-v2/JNEXT-COLD-RESET-STATE.md   | 311 ++++++
doc/issues/bypass-firmware-v2/PLAN-AUDIT.md               | 398 ++++++++
doc/issues/bypass-firmware-v2/REVIEW.md                   | 116 ++
doc/issues/bypass-firmware-v2/REVIEW-REBUTTAL.md          | 110 ++
doc/issues/bypass-firmware-v2/SESSION-HANDOVER-2026-05-17.md | (this file)
tools/cspect_dzrp/task18_*.py (5 files)            | 409 ++++++++
```

## Constraints adhered to (per CLAUDE.md and project memory)

- No writes to `main` (one near-miss with agents writing to main's tree — caught + reverted before the branch was created).
- No push to origin (the branch is local-only awaiting user authorization).
- All work in `.claude/worktrees/bypass-firmware-exploration-v2/`.
- 3 independent agents used in parallel for the evidence-gathering phase.
- 1 independent reviewer used for the implementation review (per `[[feedback_never_self_review]]`).
- Reviewer's claims independently re-verified per `[[feedback_findings_require_independent_review]]` — Finding 2 caught + rebutted with empirical evidence.
- Test added before merge claim, per CLAUDE.md "test coverage for new features".

## Where to resume

`.claude/worktrees/bypass-firmware-exploration-v2/`, run:

```bash
./build/jnext --machine next --headless \
    --sd-card roms/nextzxos-1gb-fat32fix.img \
    --bypass-tbblue-fw \
    --delayed-screenshot /tmp/bypass.png \
    --delayed-screenshot-time 10 --delayed-automatic-exit 12
```

Expected: gray screen (until video is fixed). `task18_bypass_state`
test asserts boot-state correctness — if it fails, the bypass gates
regressed.
