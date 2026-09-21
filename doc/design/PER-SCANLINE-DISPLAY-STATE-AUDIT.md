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

### The pattern's resolution floor is one row (GH #170 — accepted)

Step 1 tags a write with the row it landed in, and step 3 replays it
**before** that row is rendered — so a write that physically lands
part-way through a line still colours the whole of that line. Hardware
does not: the video pipeline re-reads the NR latches every pixel cycle
(`zxnext.vhd:6825-6828` → `:6981` for the four palette selects), so only
the pixels after the write change. The residual error is **at most one
row**, and it is **early**: a write colours the whole of the row whose raw
line it executes in, for every consumer of the pattern. A Copper
`WAIT(line, h=0)` + MOVE completes at x = 32 of the 320-wide area, the start
of the 256-wide display (`zxula_timing.vhd:423-436,474-490`, `copper.vhd:94`),
and the GH #256 reporter's MAME capture shows the change from x ≈ 35 of that
row, so jnext colours that row's first ~35 pixels early.

Until GH #257 the tilemap lane — scroll (GH #16), fetch bases (GH #53) and
the NR 0x1B / NR 0x4C output-stage inputs (GH #256) — was snapshotted at the
START of each raw line instead, so it landed every Copper split one row
**late**, including the "WAIT at the end of the previous line" technique.
That convention had been adopted for GH #16, and was only right there
because the line interrupt fired at raw hc 0 of its line instead of at
hc_ula 255 (raw hc 380 on the Next timing, `zxula_timing.vhd:577`), ~380
pixels early, and NR 0x1E/0x1F's `cvc` stepped at raw hc 0 instead of at
hc_ula 0 (raw hc 125, `:457-470`). GH #257 fixed both and moved the tilemap
lane to the end-of-row point every other lane uses: GH #16's handler write
now lands at raw hc ~15-37 of the line it colours, before the tilemap's
first visible fetch.

Decision (2026-07-30): **documented, not fixed.** A per-scanline
renderer has no representation for "from column X of row N"; closing it
means sub-row log granularity or the cycle-accurate refactor declined
twice in EMULATOR-DESIGN-PLAN.md. A "round up when the write lands past
the visible span" heuristic is not VHDL behaviour either, needs the
horizontal position at each of the ~12 log sites (the tag is computed
once per scanline, in `Emulator::on_scanline`, not per write), and would
shift the row of every existing consumer across the whole demo corpus.
Full reasoning, and why `show512.nex` turned out **not** to be an
instance of this (its one-row artefact is a Copper WAIT `hc` domain
mismatch — jnext feeds raw master-cycles-into-line where VHDL feeds the
7 MHz `hc_ula` whose origin is `c_min_hactive - 12`), is in
EMULATOR-DESIGN-PLAN.md §6.

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
| Tilemap fetch state (NR 0x6C/0x6E/0x6F) | map/definition/default-attribute snapshots | Tilemap | TM-SPLIT-01..04 |
| **Palette (NR 0x40/0x41/0x44)** | **change-log + replay (32 KB cap)** | PaletteManager | beast.nex |
| NR 0x15 b4:2 layer priority + b0 sprite enable | `write_nr15` change-log + replay (1024 cap) — wired 2026-07-23 (GH #73; rows PSCAN-G02-01..05) | Renderer | beast.nex (0x80↔0x01 toggle) |
| NR 0x43 b1/b2/b3 active ULA / Layer 2 / sprite palette select | `Ula` palsel change-log + replay (1024 cap). Log built with the ULA lane; the **Layer 2 and sprite lanes were only CONSUMED on 2026-07-30** (GH #163) — before that nothing read them back and both rasterizers resolved colour through the live end-of-frame bank. `PaletteManager::{layer2_colour,layer2_rgb8,layer2_priority_high,sprite_colour}` now take the bank, and `Renderer::render_row` / the debugger video panel pass `Ula::get_active_{layer2,sprite}_palette()`. Rows PSCAN-G10-01..04, DVP-PALSEL-*. | Ula (log) + Renderer (consumption) | show512.nex (512-colour split field) |
| NR 0x6B b4 active tilemap palette select | `Ula` palsel6b change-log + replay (separate 1-bit log — `nr_6b_tm_control(4)` is a different latch from NR 0x43's 3-bit field). **CONSUMED on 2026-07-30** (GH #168, the third lane of GH #163): `Ula::get_active_tilemap_palette()` had eight test references and zero production callers, so `tilemap_colour(idx)` read the live end-of-frame `active_tm_second_`. `PaletteManager::tilemap_colour` now takes the bank, `Tilemap::render_scanline` / `render_scanline_debug` thread it, and `Renderer::render_row` / the debugger video panel pass the per-row replayed selector. Rows PSCAN-G10-05, DVP-PALSEL-TM*. | Ula (log) + Renderer (consumption) | none yet (found by inspection while fixing #163) |
| NR 0x4C tilemap transparency index + NR 0x1B tilemap clip | `Tilemap::snapshot_output_for_line`, taken beside the fetch snapshot (end of each row since GH #257; start of the row before), so a map/index split never mixes the two. Before GH #256 both were read at their end-of-frame value | Tilemap | GH #256 repro (Copper NR 0x6E + NR 0x4C split); rows TM-165, TM-SPLIT-05/06 |
| NR 0x19 sprite clip, NR 0x15 b6/b5/b1, NR 0x4B sprite transparency index | `SpriteEngine::snapshot_control_for_line`, end of each row (the row the attribute log tags a write with) | SpriteEngine | GH #256 audit; rows PSCAN-G04-02, PLRS-SPR-01..04 |
| ULA+ enable (NR 0x68 b3 / port 0xFF3B), ULAnext enable (NR 0x43 b0) and format (NR 0x42), shadow-screen bank (port 0x7FFD b3 / NR 0x69 b6) | `Ula::snapshot_control_for_line`, end of each row; `render_scanline` swaps the row's values in, `apply_lores` reads them per row. The G11 ULA+ array had existed with **no production caller** | Ula | GH #256 audit; rows PLRS-ULA-01..05 |
| NR 0x6B b7 as the compositor's stencil gate (`tm_en_2`) | `Renderer::snapshot_tm_enabled_for_line`, end of each row | Renderer | GH #256 audit; row PLRS-CMP-01 |
| NR 0xFF ULA+ palette poke | Palette change-log, like every other palette write. It used to write the live arrays unlogged, so `rewind_to_baseline()` **erased** it and the poke never reached the screen | PaletteManager | GH #256 audit; row PLRS-PAL-01 |

## Categories of missing coverage

### Category A — Control registers consumed by the renderer

Pure log-pattern clones of the palette work. Each is a small task
(≈1-2 h including tests) once a demo motivates it.

| Register | Consumer | Why it matters | Driver candidate | Cost |
|---|---|---|---|---|
| ~~**NR 0x16 / 0x17 / 0x71** Layer 2 X/Y scroll~~ | ~~`Layer2::set_scroll_*`~~ | **DONE 2026-04-26** — Beast.nex bottom-band parallax (5-strip Copper writes at scanlines 163/165/169/173/179, progressively higher speeds via `Beast/scroll.asm`). Pattern: `Layer2::start_frame/set_current_line/rewind_to_baseline/apply_changes_for_line` mirrors the palette path. Test: `layer2_test` G10 (10 rows). | beast.nex (live) | DONE |
| ~~**NR 0x68** other bits~~ | ~~NR 0x68 handler~~ | **DONE** — b0 stencil and b6:5 blend (G11 snapshots), b2 fine-X (G08 scroll log), b3 ULA+ enable (consumed per row since GH #256). | — | DONE |
| ~~**NR 0x15** sprite/LoRes/priority bits~~ | ~~NR 0x15 dispatcher~~ | **DONE 2026-07-23 (GH #73)** — b4:2 layer priority + b0 sprite enable via `Renderer::write_nr15` change-log (the class existed dormant since G02; wiring + vblank flush + per-line sprite render gate landed together). b7 was already per-line via the LoRes snapshot (GH #63); b6/b5/b1 remain frame-granularity SpriteEngine state. Rows PSCAN-G02-01..05. | beast.nex (0x80↔0x01 toggle) | DONE |
| ~~**NR 0x14** global transparency~~ | ~~NR 0x14 handler~~ | **DONE** (G04, Task 45) — `transparent_rgb_per_line_`. | — | DONE |
| ~~**NR 0x4B / 0x4C** sprite / tilemap transparency index~~ | ~~NR 0x4B / 0x4C handlers~~ | **DONE 2026-09-21 (GH #256)** — the reporter's Copper split of NR 0x4C collapsed to the frame's last index. | GH #256 repro | DONE |
| ~~**NR 0x18 / 0x19 / 0x1A / 0x1B** clip windows~~ | ~~clip handlers~~ | **DONE** — NR 0x18 (Layer 2 clip log), NR 0x1A (ULA clip snapshot), NR 0x19 / NR 0x1B (GH #256). Snapshotting the four effective values sidesteps the rotating index. | — | DONE |
| ~~**NR 0x6B** tilemap control (mode bits)~~ | ~~tilemap handler~~ | **DONE** (G06 change-log); the compositor's stencil-gate copy of b7 since GH #256. | — | DONE |
| ~~**NR 0x70** Layer 2 mode (256 / 320 / 640)~~ | ~~layer2 handler~~ | **DONE** — `Layer2` NR 0x70 change-log (resolution + palette offset). | — | DONE |
| ~~**NR 0x12 / 0x13** Layer 2 active bank~~ | ~~layer2 handler~~ | **DONE** (G09) — `Layer2` bank change-log. | — | DONE |
| ~~**NR 0x43 bits 1-3** + **NR 0x6B bit 4** active palette select~~ | ~~`Tilemap::render_scanline` → `PaletteManager::tilemap_colour`~~ | **DONE** — NR 0x43 b1 with the original G10 log, b2/b3 on 2026-07-30 (GH #163), NR 0x6B b4 on 2026-07-30 (GH #168). All four lanes are now consumed per scanline; see the two covered-table rows above. | — | DONE |
| ~~**port 0xFF** Timex screen mode~~ | ~~Ula~~ | **DONE** (G07) — `Ula` port-0xFF change-log. | — | DONE |
| ~~**NR 0x26 / 0x27** ULA scroll~~ | ~~Ula~~ | **DONE** (G08) — `Ula` scroll change-log. | — | DONE |

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
3. ~~**NR 0x15**~~ **DONE 2026-07-23 (GH #73)** — the dormant G02
   change-log class is wired (b4:2 priority + b0 sprite enable per
   line; vblank flush added; sprite render gate per-line). beast's
   `0x80`/`0x01` Copper toggle now resolves per scanline.
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

Category A is closed as far as the GH #256 audit reaches: it walked every
member each layer's `render_scanline`, `apply_lores` and the compositor read
at render time, and each NextREG / port value among them is now replayed or
snapshotted per row. Categories B (mid-frame
RAM writes — the ULA attribute plane excepted, G12) and C remain open. Since
GH #257 every lane follows the one end-of-row convention described under the
GH #170 heading above.
