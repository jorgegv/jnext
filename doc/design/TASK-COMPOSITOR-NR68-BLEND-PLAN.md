# Task — Compositor NR 0x68 Blend-Mode SKIP-Reduction Plan

Plan authored 2026-04-23 as part of the ULA Phase 4 closure. Captures
the 3 rows re-homed from `test/ula/ula_test.cpp` §12 that test
NR 0x68 blend-mode and ULA-disable wiring at the compositor layer.

## Starting state

- `compositor_test` currently **114/0/0** (clean zero-skip).
- **This plan reopens the Compositor suite** by adding 3 new rows.
  Accept the reopen per the "finish ULA_test" directive — the new
  rows test real missing functionality, not theatre.
- NR 0x68 bits 6:5 (blend mode) and bit 7 (ULA disable) are forwarded
  to the Renderer/Compositor today via `emulator.cpp` but the actual
  render-pipeline consumption is unverified. The ULA-side setter→
  getter pair would be tautological.

## Rows inherited from ULA plan

| Row ID | Scope | VHDL cite |
|---|---|---|
| S12.02 | NR 0x68 bit 7 enable wired into render pipeline | `zxnext.vhd:5445` |
| S12.03 | NR 0x68 bit 7 toggle observable in rendered output | `zxnext.vhd:5445` |
| S12.04 | NR 0x68 blend-mode bits 6:5 actually affect compositing | `zxnext.vhd:5445` |

## Approach

- **Phase 0 — verify**: Confirm via VHDL read that the blend-mode bits
  6:5 encode the 4 compositing modes the compositor layers (ULA +
  tilemap + Layer 2 + sprites) must respect:
  - `00` = normal (no stencil; layers composite per priority).
  - `01` = ULA/tilemap stencil.
  - `10` = Layer 2 forced.
  - `11` = reserved.
- **Phase 1 — test**: Write 3 render-path rows in
  `test/compositor/compositor_test.cpp` exercising the 3 cases with
  known stimulus + expected frame-buffer output. Each row's expected
  value derives from the VHDL composite priority at
  `zxnext.vhd:5445-5475` (or wherever the compositor stencil logic
  lives in the VHDL top-level).
- **Phase 2 — fix any drift**: if the Compositor implementation in
  `src/video/renderer.cpp` diverges from VHDL for any of the 3 modes,
  fix in this phase.
- **Phase 3 — merge**: critic review + merge. Compositor suite goes
  from 114/0/0 to 117/0/0 (or 117/0/X if any of the rows legitimately
  fail a deeper integration gate).

## Scope + risk

- Small: 3 rows, each testing a well-defined mode. If existing
  compositor tests already exercise the general pipeline, the new
  rows are incremental.
- **Screenshot-regression risk**: if any of the 3 modes is mis-wired
  today and a game/demo relies on it, fixing will change rendered
  output. Re-run regression + spot-check screenshots.

## Open questions

- Is `renderer_.set_stencil_mode()` (currently wired to NR 0x68 bit 0,
  not bits 6:5) a naming artefact or a real second stencil path? Read
  VHDL for clarity.

## Status: **UDIS-01/02 CLOSED 2026-04-24**, UDIS-03 pending own plan

- **UDIS-01/02** re-homed to
  `test/compositor/compositor_integration_test.cpp` (UDIS-INT group,
  2/2/0/0). Required a small Renderer fidelity fix: per-scanline
  `ula_enabled_per_line_` snapshot (parallels the existing
  `fallback_per_line_` array) so a Copper MOVE to NR 0x68 mid-frame
  flips transparency only for rows that follow the toggle — matches
  VHDL zxnext.vhd:7103 per-pixel sampling of `ula_en_2`.
  - `src/video/renderer.{h,cpp}` — array + snapshot accessors + render-
    loop consumption of the per-line snapshot.
  - `src/core/emulator.cpp` — `init_ula_enabled_per_line()` at frame
    start + `snapshot_ula_enabled_for_line()` on scanline boundary
    (parallel to the fallback/border snapshots).
  - Compositor suite: 117/114/0/3 → 115/114/0/1.
  - Compositor (integration): new 2/2/0/0 suite.
- **UDIS-03** (NR 0x68 blend-mode bits 6:5 visible end-to-end) gets its
  own plan doc: `doc/design/TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md`.
  User-visible driver: `CSpect3_1_0_0/Beast/beast.nex` — Layer-2 sky
  gradient leaks 48K ULA attribute cells because jnext treats every
  `ula_blend_mode` variant as "00" (ULA-as-mix source) instead of
  honouring the "01"/"10"/"11" variants per VHDL 7141-7178.
