# Per-scanline display state — coverage audit

**Date**: 2026-04-25
**Driver**: TASK-PER-SCANLINE-PALETTE-PLAN.md landed today; the
log-tagged-with-current-line + rewind + replay pattern it introduced
generalises to every other piece of mid-frame-mutable state the
renderer reads. This doc is the living priority list of what's
covered, what isn't, and what each missing item would cost.

## The pattern (one-paragraph recap)

Anything the renderer reads at frame end that can be mutated
mid-frame by Z80 / Copper / IRQ handlers is at risk of "last value
wins" collapse. The fix is the palette pattern:

1. Each mutation is logged tagged with `current_line_` (set by
   `Emulator::on_scanline(N)`).
2. At frame start a baseline snapshot of the relevant state is taken.
3. At render time the baseline is rewound and log entries are replayed
   line-by-line in `Renderer::render_frame`'s row loop.

The pattern wants: a single chokepoint for mutations (so logging is
local to one writer), small per-frame change volume (so the log cap
isn't hit by normal demos), and a baseline that can be memcpy'd in
constant time per frame.

## Relationship to VideoTiming work

Different axis — see TASK-VIDEOTIMING-EXPANSION-PLAN.md /
VIDEOTIMING-TEST-PLAN-DESIGN.md. VideoTiming is **when**
`on_scanline(N)` ticks fire (per-machine timing geometry, 50/60 Hz,
etc.). This audit is **what gets replayed** at each tick. Both layers
need to be accurate for high-fidelity demos like Nirvana.

## What's covered today

| State | Mechanism | Owner | Driver |
|---|---|---|---|
| Border colour (port 0xFE) | `border_per_line_` snapshot | Ula | core |
| Fallback colour (NR 0x4A) | `fallback_per_line_` snapshot | Renderer | copper_demo |
| ULA enabled (NR 0x68 bit 7) | `ula_enabled_per_line_` snapshot | Renderer | UDIS-01/02 |
| Tilemap scroll X/Y (NR 0x2F/0x30/0x31) | `scroll_x/y_per_line_` snapshot | Tilemap | core |
| **Palette (NR 0x40/0x41/0x44)** | **change-log + replay (32 KB cap)** | PaletteManager | beast.nex |

## Categories of missing coverage

### Category A — Control registers consumed by the renderer

Pure log-pattern clones of the palette work. Each is a small task
(≈1-2 h including tests) once a demo motivates it.

| Register | Consumer | Why it matters | Driver candidate | Cost |
|---|---|---|---|---|
| ~~**NR 0x16 / 0x17 / 0x71** Layer 2 X/Y scroll~~ | ~~`Layer2::set_scroll_*`~~ | **DONE 2026-04-26** — Beast.nex bottom-band parallax (5-strip Copper writes at scanlines 163/165/169/173/179, progressively higher speeds via `Beast/scroll.asm`). Pattern: `Layer2::start_frame/set_current_line/rewind_to_baseline/apply_changes_for_line` mirrors the palette path. Test: `layer2_test` G10 (10 rows). | beast.nex (live) | DONE |
| **NR 0x68** other bits | [emulator.cpp:823](src/core/emulator.cpp#L823) | bit 7 done; bit 0 (ULA fine-X), bits 6:5 (blend mode UDIS-03), bit 3 (ULA+ en gate) all renderer-relevant | UDIS variants, ULA+ split-screen demos | S |
| **NR 0x15** sprite/LoRes/priority bits | [emulator.cpp:434](src/core/emulator.cpp#L434) | beast actually toggles `0x80`/`0x01` mid-frame for what looks like a Copper-driven layer split; impact unverified | beast.nex (cosmetic?), parallax.nex | S |
| **NR 0x14** global transparency | [emulator.cpp:322](src/core/emulator.cpp#L322) | Mid-frame transparency change → sky-vs-foreground colour-key effects | unknown | S |
| **NR 0x4B / 0x4C** sprite / tilemap transparency index | [emulator.cpp:360,365](src/core/emulator.cpp#L360-L365) | Mid-frame transparency-index swap for layer-mask effects | unknown | S |
| **NR 0x18 / 0x19 / 0x1A / 0x1B** clip windows | [emulator.cpp:445+](src/core/emulator.cpp) (rotating 4-write) | Split-screen / picture-in-picture by re-clipping a layer mid-frame | TBD | M (rotating index complicates the log) |
| **NR 0x6B** tilemap control (mode bits) | tilemap handler | Mid-frame tilemap mode flip (40↔80, tm-on-top toggle) | TBD | S |
| **NR 0x70** Layer 2 mode (256 / 320 / 640) | layer2 handler | Per-line L2 mode change → mixed-resolution screens | TBD | M (changes width path) |
| **NR 0x12 / 0x13** Layer 2 active bank | layer2 handler | Page-flipping per-line for double-buffered scroll. Parallax does this per-frame; per-line is exotic. | TBD | S |
| **NR 0x43 bits 1-3** + **NR 0x6B bit 4** active palette select | PaletteManager | Mid-frame palette-bank flip independent of palette CONTENT writes | TBD | S |
| **port 0xFF** Timex screen mode | Ula | Mid-frame mode switch (STANDARD / HI_COLOUR / HI_RES) | TBD | M (mode change reroutes the render path) |
| **NR 0x26 / 0x27** ULA scroll | Ula | Mid-frame ULA scroll for non-square-tile effects | TBD | S |

Cost legend: **S** ≈ 1-2 h log-pattern clone. **M** ≈ half-day, touches
the renderer's per-mode dispatch.

### Category B — Memory written mid-frame to the same address (Nirvana-class)

The renderer reads ULA pixel/attribute bytes from physical bank 5/7
at fixed addresses derived from `(row, col)`. **Nirvana**, **BIFROST*2**,
**multicolour** demos rewrite the SAME attribute byte multiple times per
frame, timed to the beam, so each scanline inside a character cell
gets a different value. Frame-end render sees only the last → all 8
rows in the cell render with that one value.

This is structurally harder than Category A:

- The "log" is not NextREG writes — it's RAM writes to a specific
  address range. Hooking `Ram::write` is an architectural change
  (adds a callback or watch-range mechanism).
- Two viable approaches:
  - **Sparse hook**: instrument `Ram::write` for bank-5/7 attribute
    range (0x5800-0x5AFF + 0x7800-0x7AFF in CPU space, mapped to
    physical pages 10/14). Log `(line, addr, value)`. Static cap
    ~16K entries × 8 B = 128 KB.
  - **Per-line attribute snapshot**: 768 B × 312 lines = 240 KB
    static. Simpler but always-on cost.
- Pixel-byte rewrites (rarer than attr) need the same treatment;
  scope balloons fast.

| Driver | Status | Cost |
|---|---|---|
| Nirvana (attr multiplexer, 6144 unique attrs) | Not yet a target | M-L |
| BIFROST*2 (256-colour layer-2-via-ULA) | Not yet a target | M-L |
| Multicolour (per-2-line attrs) | Not yet a target | M-L |
| Pixel-rewrite mid-frame border effects | Rare in Next demos | L |

### Category C — Internal subsystem state read at render

| State | Why it matters | Driver | Cost |
|---|---|---|---|
| **Sprite attributes** (port 0x57 / NR 0x75-0x79 → SpriteEngine internal) | Sprite-multiplexing: rewrite the same slot's X/Y mid-frame so it draws two visually-distinct sprites. Frame-end render sees only the final attrs. | TBD (no current demo target) | M |
| **Layer 2 enable / write-paging** (port 0x123B) | Mid-frame enable toggle for "L2 only on rows 100-150" effects. | TBD | S |
| **Sprite patterns** (port 0x5B uploads) | Mid-frame pattern reload for animation effects beyond the sprite cap | Exotic | M |

## Recommended priority ordering

By **likelihood of being needed** based on demos we already care about:

1. ~~L2 X/Y scroll (NR 0x16 / 0x17 / 0x71)~~ **DONE 2026-04-26** —
   Beast.nex bottom-band perspective parallax. Same shape as palette
   work; ~1 h end-to-end including tests + audit doc closure.
2. **NR 0x68 other bits** — UDIS-03 already partially closed; bit 0
   (fine-X) is the obvious next gap if any demo uses fine-X mid-frame.
3. **NR 0x15** — beast Copper toggles this. Visual impact unverified;
   re-snap beast with NR 0x15 logged before deciding to flip.
4. Wait for a real demo driver before tackling anything else in
   Category A — see "By demo, not by completeness" below.
5. Category B (Nirvana / BIFROST*2 / multicolour) — only when a
   specific demo is the target. Architectural change to `Ram::write`
   wants a clear motivation.
6. Category C — defer; sprite-multiplexing is a known limitation but
   unblocked by no current target.

## By demo, not by completeness

The palette work was the right size because beast existed as the
concrete driver. Without a demo to verify against, "implement
per-scanline X" is guesswork — there's no oracle for whether the
output looks right beyond "it doesn't collapse to one value".

When picking up an item from this list:

1. Identify the demo or effect that motivates it.
2. Capture a known-good reference (CSpect screenshot, real-hardware
   capture, or a hand-traced expected output).
3. Use the palette pattern as the template. Memory budget should be
   computed from the worst case in the plan doc, statically allocated.
4. Add bare unit + renderer-level tests in the relevant `_test.cpp`
   suite.
5. Independent code review.
6. Re-snap the driver demo + run unit + regression.

## Out of scope for this audit

- **T-state-accurate mid-scanline mutation**. True Nirvana fidelity
  needs the Z80 to write attributes "just ahead of the beam" at sub-
  scanline precision. Per-scanline replay is a major step but won't
  catch effects that depend on the beam being at column X within
  scanline N. A future architectural change (interleave emulation
  and rendering at scanline or T-state granularity) would be needed.
- **Hardware quirks** (floating bus contents, ULA snow effects).
  Live in their own subsystem audits.
- **Per-frame state** that's already correct because it doesn't
  change mid-frame in any known demo (active palette bank,
  background sample buffer, etc.).

## Status

PENDING — no item picked up. This document opens the queue.
