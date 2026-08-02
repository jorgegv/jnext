# 3.3 Video

The picture a ZX Spectrum Next puts on screen is composed, not drawn. Four
generators run in parallel — the classic ULA screen, a Layer 2 bitmap, a tilemap
and a sprite engine — each offering a pixel for every position on the display,
and a hardware compositor chooses between them using a programmable priority
order, per-layer transparency and a pair of blend modes. A fifth mode, LoRes,
does not add a layer at all: it substitutes chunky pixels into the ULA's slot.
All of it is under software control at scanline granularity, and through the
Copper at a position *within* a scanline, which is what makes raster effects on
this machine so much richer than on a 48K Spectrum.

That makes video the largest subsystem in JNEXT — about 10 200 lines across
eight modules in `src/video/`, plus the part of `src/core/emulator.cpp` that
feeds it. It produces exactly one artefact: a 640×256 ARGB8888 framebuffer,
built one scanline at a time by `Renderer::render_frame` at the end of every
emulated frame (see
[The video pipeline](../02-architecture/04-the-video-pipeline.md)).

The consequence of that last sentence shapes most of this page. Hardware draws
the picture *while* the CPU runs, so a register written half-way down the screen
affects only the rows below it. JNEXT runs the whole frame first and renders
afterwards, so a naive renderer would see nothing but end-of-frame register
values and every mid-screen change would vanish. A large part of the video code
therefore exists to record what each register was at each row, so that the
deferred render can reconstruct a picture that changed while it was being drawn.

## Raster counters and their clock domains

Confusing these has cost this project real bugs. Three counters exist, at three
rates:

| Counter | Rate | Lives in |
|---|---|---|
| master cycle | 28 MHz | `Clock`, `MachineTiming` in `src/core/emulator_config.h` |
| `hc` / `vc` raw frame counters | 7 MHz pixel clock | `VideoTiming` in `src/video/timing.h` |
| `hc_ula` / `cvc` ULA pixel counters | 7 MHz, shifted origin | derived from `VideoTiming` |

One pixel tick is 4 master cycles, and one 3.5 MHz T-state is 2 pixel ticks. The
per-machine limits — `c_max_hc`, `c_max_vc`, `c_min_hactive`, `c_min_vactive`
and the interrupt position — are VHDL constants held by `VideoTiming`, keyed on
the NR 0x03 *timing* axis rather than on the machine personality.

`hc_ula` is not `hc`. It is a registered counter, reset one tick after `hc`
matches `c_min_hactive - 12`, so `hc_ula == 0` at raw `hc == c_min_hactive - 11`
(`VideoTiming::hc_ula_zero_raw_hc`), and `cvc` counts lines in *that* counter's
frame. The Copper compares its `WAIT` against `hc_ula`/`cvc`, which is why
`Emulator::tick_copper_for_master_cycles` rebases the master-cycle position onto
that pair before stepping it. Passing the raw master-cycle offset through
instead — wrong by a factor of four in scale *and* wrong in origin — made every
non-trivial `WAIT` fire early. That was a real defect here, not a hypothetical
one.

The other conversion that matters everywhere is
`framebuffer_row = vc - VideoTiming::vblank_top()`. `vblank_top` is
`min_vactive - 32` and it is **per-machine** — 32 for the Next family, 48 for
Pentagon timing, 8 for the 60 Hz branches — so the constant 32 is right only by
coincidence. `Emulator::schedule_frame_events` queues one `SCANLINE` event per
line plus a `VSYNC`, and `Emulator::on_scanline(line)` is the hook all per-line
work hangs off.

## The ULA

The ULA is the original Spectrum display generator, and on the Next it is still
the layer everything else is composited around: 256×192 pixels with the familiar
attribute cells, a border, and the Timex extensions that predate the Next
itself. The Next adds hardware scrolling and a choice of palettes on top of it.

`src/video/ula.{h,cpp}` renders one framebuffer row at a time through
`Ula::render_scanline`. Border rows go to `render_border_line`; display rows
dispatch on the Timex screen mode last written to port 0xFF. `STANDARD`,
`STANDARD_1` (the 0x6000 screen) and `HI_COLOUR` each double every source bit
into two adjacent framebuffer cells, while `HI_RES` emits a true 512-pixel line
from screens 0 and 1 byte-interleaved. The ULA also owns ULA scroll (NR 0x26 /
0x27, NR 0x68 b2), ULAnext (NR 0x42/0x43), ULA+ (ports 0xBF3B/0xFF3B), the
shadow-screen bank selector, the flash counter, and the active-palette selector
bits for *all four* palettes.

Two things a reader expects to find here are somewhere else. The **clip window**
is one: `Ula::render_scanline` emits an unclipped line and
`Renderer::apply_ula_clip` masks it afterwards, because in hardware NR 0x1A
feeds a compositor-stage signal — Layer 2, tilemap and sprites each clip
themselves instead. The **floating bus** is the other:
`Emulator::floating_bus_read` is the port-0xFF read mux, with its NR 0x08 b2
Timex arm, its NR 0x82 b0 gate, and the per-machine gate that delivers ULA
content only under 48K/128K timing. `ula_floating_bus_active_arm` derives the
byte from the current raster position, and the +3's separate latch lives on the
`Mmu`.

## LoRes is not a layer

LoRes trades resolution for colour freedom: 128×96 chunky pixels, each carrying
its own palette index, which is exactly what the ULA's attribute cells prevent.
Radastan mode is the 4-bit variant of the same idea. Neither is a new plane in
the display — the hardware feeds these pixels into the ULA's own slot in the
compositor, and the rest of the machine cannot tell the difference.

`src/video/lores.{h,cpp}` models the `lores` VHDL entity as pure functions plus
its registers (NR 0x15 b7, 0x32, 0x33, 0x6A), and JNEXT keeps the substitution
literal: `Renderer::apply_lores` writes into the already-rendered ULA scanline,
and there is deliberately no fifth layer buffer. The LoRes byte indexes the
*ULA* palette in the bank NR 0x43 b1 selects, and is clipped by the ULA's window
because that is where hardware wires it. Both 8-bit LoRes and Radastan fetch
from bank 5 directly.

## Layer 2, tilemap, sprites

These three are the Next's own additions, and each removes a specific piece of
work a Spectrum program used to do in software.

**Layer 2** is a linear bitmap: every pixel carries its own palette index in a
straight run of RAM, so it has none of the ULA's one-colour-pair-per-cell
restriction, and it scrolls in hardware on both axes. NR 0x70 selects between
three geometries, listed below. Because the frame can be larger than a 16 KB
bank and is fetched by the display hardware rather than by the CPU, `Layer2`
reads physical RAM banks directly instead of going through the MMU.

**Tilemap** is a character display in the modern sense. A 40×32 or 80×32 grid of
cells names 8×8 patterns held once in RAM as 4-bit-per-pixel definitions, so a
screenful of graphics costs a screenful of *indices*; the hardware scrolls the
whole grid (NR 0x2F/0x30/0x31) without anything being copied. Each cell normally
carries an attribute byte alongside its index — palette offset in the top
nibble, X mirror, Y mirror, rotate, and a per-tile "ULA over tilemap" bit — and
NR 0x6B can strip those flags, extend the palette offset for text mode, or
repurpose the low bit as a ninth tile-index bit for 512-tile mode.

**Sprites** are drawn by the display hardware as the raster passes, so moving an
object costs a few attribute writes rather than a blit and a background restore.
There are 128 of them, 16×16 pixels, at 4 or 8 bits per pixel, with mirroring,
rotation, ×1/2/4/8 scaling, and anchor-plus-relative composites for objects
bigger than one sprite.

| Layer | File | Modes | Notes |
|---|---|---|---|
| Layer 2 | `src/video/layer2.*` | 256×192 8bpp, 320×256 8bpp, 640×256 4bpp (NR 0x70) | reads physical RAM banks directly, not through the MMU; clip NR 0x18, scroll NR 0x16/0x17/0x71, banks NR 0x12/0x13; per-pixel priority promotion from palette bit 15 |
| Tilemap | `src/video/tilemap.*` | 40×32 and 80×32, 4bpp patterns + text mode | map and definitions in bank 5 or the dedicated bank-7 BRAM; emits the per-pixel `tm_pixel_below` and `tm_pixel_textmode` flags the compositor needs |
| Sprites | `src/video/sprites.*` | 128 sprites, 16×16, 4/8-bit, ×1/2/4/8 scale, anchored composites | ports 0x303B/0x57/0x5B, 16 KB pattern RAM, clip NR 0x19 |

Each of the four clip windows is a single NextREG written as a rotating 4-value
cycle — NR 0x18 Layer 2, NR 0x19 sprites, NR 0x1A ULA (and LoRes), NR 0x1B
tilemap — with the rotating index held by `Emulator` rather than by the layer.

Sprite collision and the per-line-budget overtime flag (port 0x303B) are
computed *inside* `SpriteEngine::render_scanline` and are software-visible, so
they cannot be skipped along with the pixels. A frame whose render the frontend
drops still runs `Renderer::run_sprite_side_effects`: the same pipeline, with
the pixels discarded.

`PaletteManager` (`src/video/palette.*`) holds all four palettes in two banks
each, RGB333 internally with an ARGB8888 cache rebuilt on write, plus the ULA+
region aliased at ULA indices 0xC0-0xFF. See
[Ports and NextREG](05-ports-and-nextreg.md) for the register plumbing and
[Peripherals](06-peripherals.md) for the Copper.

## The compositor

`Renderer::render_row` is the whole per-row pipeline: clear the layer buffers,
render each layer, substitute LoRes, apply the NR 0x68 b7 blank and the ULA
clip, then call `composite_scanline`. That last step dispatches once per row on
the NR 0x15 priority into a `composite_scanline_mode<PRIO>` specialisation, so
the per-pixel loop that follows carries no branch on the mode at all.

The per-pixel logic follows the VHDL compositor point for point: per-layer
transparency (an NR 0x14 RGB compare, the layer's own enable, the clip result),
the ULA/tilemap merge with its `tm_pixel_below` and stencil (NR 0x68 b0)
variants, Layer 2 priority promotion, blend modes 6 and 7 with their NR 0x68
b6:5 source selection, the border exception, and the NR 0x4A fallback colour
wherever *every* layer is transparent. `Renderer::LayerMask` — what
`--delayed-screenshot-layers` drives — forces a layer transparent at that input;
it is host-side debug state, which is why `reset()` and `save_state()`
deliberately ignore it.

## Per-scanline state: snapshots and change logs

This is where the deferred-rendering problem from the top of the page is paid
for. Splitting the screen mid-frame is the standard trick of the machine: change
the palette at row 96 for a sky gradient, change Layer 2's scroll each row for
parallax, change the layer priority so a HUD sits above the playfield. All of
those are register writes that land while the beam is somewhere specific, and a
renderer that only sees the final register values would show one flat frame. Two
mechanisms prevent that.

**Snapshot arrays** store one value per framebuffer row, sampled in
`Emulator::on_scanline`. They cover the NR 0x4A fallback colour, ULA enable
(NR 0x68 b7), stencil and blend mode (NR 0x68 b0 / b6:5), NR 0x14 transparent
RGB, the NR 0x1A ULA clip window, the LoRes register set, the ULA border colour,
and tilemap scroll plus fetch state (NR 0x6C/0x6E/0x6F). All but tilemap scroll
snapshot the *previous* row, because by the time `on_scanline(N)` fires the
Copper has finished line N-1; tilemap scroll latches at the start of the current
row, as hardware does.

**Change logs** are the finer instrument: they record every write with a line
tag and replay it during render. Each owner exposes the same five calls —
`start_frame`, `set_current_line`, `rewind_to_baseline`, `apply_changes_for_line`
and `flush_remaining_changes` — so a new one costs no new protocol. The logs
are: palette contents (`PaletteManager`); Layer 2 scroll, clip, bank, enable and
NR 0x70 (five separate logs); sprite attributes and sprite patterns; the ULA's
port-0xFF screen mode, ULA scroll, and the NR 0x43 / NR 0x6B b4 active-palette
selectors; the tilemap's NR 0x6B; the `AttributeMux` on the `Mmu`
(Nirvana-class mid-frame attribute rewrites, resolved per *column* rather than
per row); and NR 0x15 layer priority plus sprite enable on the `Renderer`
itself.

Two details there are load-bearing. Writes tagged during vblank never match a
visible row, so `flush_remaining_changes` must drain them or they are lost
forever — the next frame's baseline would not contain them. And writes that
arrive before the display starts are coalesced to row 0 rather than given a
sentinel tag, because a sentinel at the head of the log stalls the cursor walk.

A few registers are still read live, at frame granularity: NR 0x4B/0x4C
transparency indices, the sprite and tilemap clip windows, and NR 0x68 b3 (ULA+
enable — `Ula` has the per-line snapshot API, but nothing calls it). The
coverage is demand-driven by design: a register gets a log when a real program
turns out to need one.

## Accepted limitation: mid-line writes apply from the start of the row

Both mechanisms are row-granular, so a register write that physically lands
part-way through a scanline is applied from the **beginning** of that row.
Hardware samples those latches per pixel and would change only the pixels after
the write. The residual error is therefore **bounded to at most one row, and is
one-directional** — an effect can appear early, never late. Closing it properly
would need either sub-row granularity or the cycle-accurate rendering refactor,
both of which have been assessed and declined; this is a known modelling
limitation rather than a bug. `AttributeMux` is the one place the finer
granularity was genuinely needed, and it resolves per column.
