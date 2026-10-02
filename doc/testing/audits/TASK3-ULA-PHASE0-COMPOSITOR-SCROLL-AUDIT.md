# Task 3 — ULA Video Phase 0 — Compositor scroll audit

Authored 2026-04-23 as the secondary deliverable of Phase 0. Addresses
the plan's §"Risks + open questions" Q1: "Verify the compositor does
not also apply scroll (double-offset bug)."

## Method

Grep-only audit of `/tmp/jnext-ula0/src/` for `scroll_x`, `scroll_y`,
`ula_scroll`, `nr_0x26`, `nr_0x27`, `0x26`, and `0x27` references —
everywhere EXCEPT `src/video/ula*`. No production code was touched.

## Findings

**The compositor (i.e. `src/video/renderer.cpp` / `renderer.h`, the
only module that mixes ULA with Layer 2, Tilemap, and Sprites into a
final scanline) contains ZERO scroll references and ZERO references to
NR 0x26 or NR 0x27.** The renderer calls
`ula_.render_scanline(ula_line_.data(), row, mmu)` and then composites
the returned line into the output with no x/y translation applied on
top. There is no separate "compositor" module distinct from the
renderer.

All scroll logic in the codebase is per-layer and owned by the layer
itself:

- `src/video/layer2.cpp` — NR 0x16/0x17/0x71 fold an internal
  `scroll_x_` / `scroll_y_` pair at Layer-2 render time.
- `src/video/tilemap.cpp` + `tilemap.h` — NR 0x2F/0x30/0x31 fold an
  internal `scroll_x_` / `scroll_y_` pair at Tilemap render time, with
  per-line snapshots.
- `src/core/emulator.cpp` — wires the Layer-2 and Tilemap NextREG
  write-handlers to those per-layer setters.

The only `scroll` hit inside `src/video/ula.h` is a doc-comment about
a possible future "scrolled or scaled" hi-res view — unrelated to NR
0x26/0x27 hardware scroll.

No hit anywhere in `src/port/nextreg*.cpp` or `src/core/emulator.cpp`
for the literals `0x26` or `0x27` (nor any ULA-scroll setter wiring).
Confirms the plan's starting observation that NR 0x26 / NR 0x27 are
completely unwired today.

## Implication for Wave A

Wave A has a clean runway. When it lands `Ula::set_scroll_x_*`,
`Ula::set_scroll_y`, `Ula::set_fine_scroll_x` and folds them at
`Ula::render_scanline` time (per zxula.vhd:193-216), the renderer
will NOT double-offset the resulting line. No guard or flag is
required in the renderer. The only wiring work is the Phase 1
NextREG write-handler for NR 0x26 / 0x27 (plus the NR 0x68 bit-2
forward) dispatching into the Ula setters.
