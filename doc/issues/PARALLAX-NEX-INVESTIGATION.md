# parallax.nex rendering investigation

**Date**: 2026-04-25
**Status**: PARKED — needs LoRes implementation as prerequisite, then
bringup. Estimated 1–3 sessions of focused work.
**Driver file**: `../CSpect3_1_0_0/parallax.nex` (also at
`/home/jorgegv/src/spectrum/CSpect3_1_0_0/parallax.nex`).
**CSpect launch script**: `/home/jorgegv/src/spectrum/CSpect3_1_0_0/parallax.sh`
(`mono ./CSpect.exe -fullscreen -sound -w5 -60 -vsync -zxnext -mmc=./ parallax.nex`).

## Symptom

Severe corruption: scene rendered as **two side-by-side copies of the
scene with a vertical black band in the middle**, ~30 px wide gap.
Captured at multiple time points in `/tmp/parallax-baseline.png`,
`/tmp/parallax-t1.png`, `/tmp/parallax-t3.png`, `/tmp/parallax-t6.png`
during the 2026-04-25 investigation.

## Trace findings

NR write trace (4 s capture, 2944 NR writes; full log was at
`/tmp/parallax-trace.log`):

- **NR 0x15 toggles between `0x80` and `0x01`**:
  - `0x80` = bit 7 set → LoRes mode ON, sprites disabled
  - `0x01` = bit 0 set → LoRes OFF, sprites enabled
  - Almost certainly Copper-driven per-line layer split.
- **Heavy NR 0x60 / 0x61 / 0x62** — Copper program is active.
- **NR 0x16** continuously incrementing from `0x00` upward — Layer 2
  X-scroll being driven (whether L2 is actually consumed is unclear;
  see below).
- **DMA fires 757 times in 5 s** (~3 enables per frame) — all target
  port 0x57 (sprite attribute upload, auto-increment slot), sourcing
  memory at 0xE000 / 0xE800 / 0xF000 in 80-byte chunks. Standard
  sprite-attribute uploader. **DMA is not the bug**; it's the vehicle
  parallax uses for sprite scrolling.
- **No NR 0x69, NR 0x6B, NR 0x6E, NR 0x6F, NR 0x70 writes** — Layer 2
  is never enabled (NR 0x69 untouched, port 0x123B not seen); Tilemap
  is never enabled. Whole scene = ULA + sprites + LoRes.

## Root cause shortlist

1. **LoRes mode (NR 0x15 bit 7) is unimplemented** — explicit deferral
   in [src/core/emulator.cpp:428](../../src/core/emulator.cpp#L428)
   (`// bit 7 = LoRes enable (deferred)`) and
   [doc/design/EMULATOR-DESIGN-PLAN.md:767](../design/EMULATOR-DESIGN-PLAN.md#L767)
   (`LoRes deferred to Phase 3`). NEX-loader half is done
   (`SCREEN_LORES = 0x04` flag loads 12 288 bytes into bank 5 — see
   [src/core/nex_loader.cpp:189-198](../../src/core/nex_loader.cpp#L189-L198));
   the **renderer half is missing**.
2. **Per-scanline NR 0x15 replay is missing**. Even with LoRes
   implemented, the Copper-driven mid-frame toggle of bit 7 / bit 0
   would collapse to "last value wins" without per-scanline replay
   (same architectural issue the palette change-log just solved). See
   [doc/design/PER-SCANLINE-DISPLAY-STATE-AUDIT.md](../design/PER-SCANLINE-DISPLAY-STATE-AUDIT.md)
   §Category A.
3. **The "two side-by-side copies" symptom is NOT explained by missing
   LoRes alone**. Without LoRes, the background layer should be black
   / fallback, not duplicated. Something else (sprite X-wrap, sprite
   multiplexing, L2 width handling, or an unknown feature) is
   contributing. Will only become visible once steps 1 + 2 are done.

## Required work

In dependency order:

| # | Item | Estimate | Notes |
|---|---|---|---|
| 1 | **TASK-LORES-PLAN.md** — implement LoRes layer end-to-end | 4–8 h | NR 0x15 bit 7 wiring + 128×96 scanline renderer + NR 0x32/0x33 scroll + NR 0x1A clip + compositor integration + transparency vs NR 0x14. VHDL `lores.vhd` is the oracle (~150 lines). Test plan + bare + integration tests + independent review. |
| 2 | **Per-scanline NR 0x15 replay** | 1–2 h | Palette-pattern clone for the Copper mid-frame layer split. |
| 3 | **Per-scanline NR 0x16 / 0x17 / 0x71 (L2 scroll)** | 1–2 h IF needed | Verify after step 2 — trace shows NR 0x16 written but L2 never enabled; may be irrelevant. |
| 4 | **Bringup vs CSpect** | 2–4 h | Capture, side-by-side compare, file new bugs as they surface. The "two copies" symptom needs its own fix once 1+2 are in place. |
| 5 | **Per-scanline sprite attrs** if multiplexing observed | 3–4 h | DMA-driven uploads point at this; only if step 4 reveals sprite-multiplexing dependency. |

**Total**: 1–3 focused sessions.

## Risk factors

- **No reference image on disk** — every iteration needs side-by-side
  compare to a CSpect run. Capture a CSpect screenshot first thing
  before bringup starts.
- **LoRes touches a lot of surface area**: NR 0x14 transparency,
  NR 0x68 bit 3 (`ulap_en`) gating, priority compositor, palette
  routing. Not just a renderer module.
- **Cascade discovery**: building LoRes may surface that we also need
  NR 0x32 / 0x33 per-scanline replay, LoRes-specific clip handling,
  or a feature not yet identified.
- **Two-copies mystery** is unexplained by the obvious story and may
  be a separate bug that doubles the bringup effort.

## Recommended approach

Three smaller commits / sub-plans, NOT bundled:

1. **TASK-LORES-PLAN.md** — implement the layer cleanly, with tests,
   regardless of parallax. Standalone value: other LoRes demos exist
   and the NEX loader already supports the screen flag.
2. **Per-scanline NR 0x15 replay** — quick palette-pattern clone
   after step 1 lands.
3. **Parallax bringup** — only NOW look at parallax.nex with both
   1 + 2 in place. Likely reveals the "two copies" mystery as a
   sprite or L2 issue fixable in isolation.

## Bringup checklist (when picked up)

- [ ] Capture CSpect reference screenshots of parallax at t=1s, 3s, 6s
      (use the CSpect launch script for parity).
- [ ] Author `doc/design/TASK-LORES-PLAN.md` (Phase 0 design).
- [ ] Implement LoRes layer + tests + review.
- [ ] Re-snap parallax. If still wrong → do per-scanline NR 0x15.
- [ ] Re-snap. Diagnose remaining delta vs CSpect.
- [ ] Patch and iterate until parity (or until a separate plan is
      needed for the residual).
- [ ] Update this doc with findings + close.

## Companion docs

- [doc/design/PER-SCANLINE-DISPLAY-STATE-AUDIT.md](../design/PER-SCANLINE-DISPLAY-STATE-AUDIT.md)
  — the per-scanline replay pattern parallax depends on (Category A
  for NR 0x15).
- [doc/issues/BEAST-NEX-INVESTIGATION.md](BEAST-NEX-INVESTIGATION.md)
  — companion investigation; both NEX files surfaced together. Beast
  is RESOLVED; parallax was parked at the same time pending this
  plan.
