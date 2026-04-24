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

## Status: **BLOCKED** (2026-04-24 re-audit)

Re-audit on 2026-04-24 (agent task-compositor-nr68-blend) found all 3
rows genuinely blocked on emulator-side work that exceeds a 3-skip
flip:

- **UDIS-01** — bit 7 IS already wired (src/video/renderer.cpp:83). The
  end-to-end observation requires calling `Renderer::render_frame()`,
  which needs `Mmu+Ram+PaletteManager+Layer2+SpriteEngine+Tilemap`.
  Widening `compositor_test`'s CMake linkage to match
  `ula_integration_test` crosses the subsystem/integration boundary.
  Cleaner landing: a new `compositor_integration_test.cpp` in a
  follow-up session.
- **UDIS-02** — requires a functional Copper MOVE to toggle NR 0x68
  mid-scanline. Copper is intentionally stubbed pending the NMI
  plumbing plan. Blocker is Copper, not Compositor.
- **UDIS-03** — NR 0x68 bits 6:5 (`ula_blend_mode`) feed
  `mix_rgb / mix_top / mix_bot` at VHDL 7141-7178. Those signals are
  only consumed by NR 0x15 `layer_priorities_2 = "110"/"111"`
  (VHDL 7286-7356). The emulator has no blend-mode 110/111
  implementation — see Group BL in `compositor_test.cpp` and
  `renderer.cpp:~259` (falls back to SLU for modes 6/7). UDIS-03 is a
  user-visible consequence of that same gap; folds into the BL
  backlog rather than needing a local `Renderer::set_blend_mode`
  wiring.

Skip reasons in `test/compositor/compositor_test.cpp` refreshed to
cite the precise blockers. The original "F-UDIS-RENDER" tag was
imprecise — the 3 rows have 3 different root causes.

