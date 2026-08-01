# 3.3 Video

Video is the largest subsystem — about 12 000 lines across eight modules in
`src/video/`, plus the part of `src/core/emulator.cpp` that feeds it. It produces
one thing: a 640×256 ARGB8888 framebuffer, built one scanline at a time by
`Renderer::render_frame` at the end of every emulated frame (see
[The video pipeline](../02-architecture/04-the-video-pipeline.md)).

## Raster counters and their clock domains

Confusing these has cost this project real bugs. Three counters exist, at three
rates:

| Counter | Rate | Lives in |
|---|---|---|
| master cycle | 28 MHz | `Clock`, `MachineTiming` in `src/core/emulator_config.h` |
| `hc` / `vc` raw frame counters | 7 MHz pixel clock | `VideoTiming` in `src/video/timing.h` |
| `hc_ula` / `cvc` ULA pixel counters | 7 MHz, shifted origin | derived from `VideoTiming` |

One pixel tick is 4 master cycles; one 3.5 MHz T-state is 2 pixel ticks. The
per-machine limits (`c_max_hc`, `c_max_vc`, `c_min_hactive`, `c_min_vactive`, the
interrupt position) are VHDL constants held by `VideoTiming`, keyed on the
NR 0x03 *timing* axis, not the machine personality.

`hc_ula` is not `hc`: it is a registered counter reset one tick after `hc`
matches `c_min_hactive - 12`, so `hc_ula == 0` at raw `hc == c_min_hactive - 11`
(`VideoTiming::hc_ula_zero_raw_hc`), and `cvc` counts lines in *that* counter's
frame. The Copper compares its `WAIT` against `hc_ula`/`cvc`, so
`Emulator::tick_copper_for_master_cycles` rebases the master-cycle position onto
that pair before stepping. Passing the raw master-cycle offset through instead —
wrong by a factor of four in scale *and* in origin — made every non-trivial
`WAIT` fire early. That was a real defect here, not a hypothetical.

The other conversion that matters everywhere is `framebuffer_row = vc -
VideoTiming::vblank_top()`. `vblank_top` is `min_vactive - 32` and is
**per-machine** (32 for the Next family, 48 for Pentagon timing, 8 for the 60 Hz
branches); the constant 32 is right only by coincidence.
`Emulator::schedule_frame_events` queues one `SCANLINE` event per line plus a
`VSYNC`; `Emulator::on_scanline(line)` is the hook all per-line work hangs off.

## The ULA

`src/video/ula.{h,cpp}` renders one framebuffer row at a time via
`Ula::render_scanline`. Border rows go to `render_border_line`; display rows
dispatch on the Timex screen mode last written to port 0xFF: `STANDARD`,
`STANDARD_1` (the 0x6000 screen) and `HI_COLOUR` double each source bit into two
adjacent framebuffer cells, while `HI_RES` emits a true 512-pixel line from
screens 0 and 1 byte-interleaved. The ULA also owns ULA scroll (NR 0x26/0x27,
NR 0x68 b2), ULAnext (NR 0x42/0x43), ULA+ (ports 0xBF3B/0xFF3B), the
shadow-screen bank selector, the flash counter, and the active-palette selector
bits for *all four* palettes.

Two things a reader expects here and will not find. **The clip window**:
`Ula::render_scanline` emits an unclipped line and `Renderer::apply_ula_clip`
masks it, because in hardware NR 0x1A feeds a compositor-stage signal — Layer 2,
tilemap and sprites each clip themselves instead. **The floating bus**:
`Emulator::floating_bus_read` is the port-0xFF read mux, with its NR 0x08 b2
Timex arm, NR 0x82 b0 gate, and the per-machine gate that delivers ULA content
only under 48K/128K timing; `ula_floating_bus_active_arm` derives the byte from
the current raster position, and the +3's separate latch lives on the `Mmu`.

## LoRes is not a layer

`src/video/lores.{h,cpp}` models the `lores` VHDL entity as pure functions plus
its registers (NR 0x15 b7, 0x32, 0x33, 0x6A). It **substitutes its pixel for the
ULA's, inside the ULA slot** — `Renderer::apply_lores` writes into the
already-rendered ULA scanline, and there is deliberately no fifth layer buffer.
The LoRes byte indexes the *ULA* palette in the bank NR 0x43 b1 selects, and is
clipped by the ULA's window because hardware wires it there. Both 8-bit LoRes and
Radastan modes fetch from bank 5 directly.

## Layer 2, tilemap, sprites

| Layer | File | Modes | Notes |
|---|---|---|---|
| Layer 2 | `src/video/layer2.*` | 256×192 8bpp, 320×256 8bpp, 640×256 4bpp (NR 0x70) | reads physical RAM banks directly, not through the MMU; clip NR 0x18, scroll NR 0x16/0x17/0x71, banks NR 0x12/0x13; per-pixel priority promotion from palette bit 15 |
| Tilemap | `src/video/tilemap.*` | 40×32 and 80×32, 4bpp patterns + text mode | map and definitions in bank 5 or the dedicated bank-7 BRAM; emits the per-pixel `tm_pixel_below` and `tm_pixel_textmode` flags the compositor needs |
| Sprites | `src/video/sprites.*` | 128 sprites, 16×16, 4/8-bit, ×1/2/4/8 scale, anchored composites | ports 0x303B/0x57/0x5B, 16 KB pattern RAM, clip NR 0x19 |

Each of the four clip windows is one NextREG written as a rotating 4-value cycle
— NR 0x18 Layer 2, NR 0x19 sprites, NR 0x1A ULA (and LoRes), NR 0x1B tilemap —
with the rotating index held by `Emulator`, not by the layer.

Sprite collision and per-line-budget overtime (port 0x303B) are computed *inside*
`SpriteEngine::render_scanline` and are software-visible, so a frame whose render
the frontend skips still runs `Renderer::run_sprite_side_effects` — the same
pipeline, pixels discarded.

`PaletteManager` (`src/video/palette.*`) holds all four palettes in two banks
each, RGB333 internally with an ARGB8888 cache rebuilt on write, plus the ULA+
region aliased at ULA indices 0xC0-0xFF. See
[Ports and NextREG](05-ports-and-nextreg.md) for the register plumbing and
[Peripherals](06-peripherals.md) for the Copper.

## The compositor

`Renderer::render_row` is the whole per-row pipeline: clear the layer buffers,
render each layer, substitute LoRes, apply the NR 0x68 b7 blank and the ULA clip,
then `composite_scanline` — which dispatches once per row on the NR 0x15 priority
into a `composite_scanline_mode<PRIO>` specialisation, so the per-pixel loop
carries no branch on the mode.

The per-pixel logic follows the VHDL compositor: per-layer transparency (an
NR 0x14 RGB compare, the layer's own enable, the clip result), the ULA/tilemap
merge with its `tm_pixel_below` and stencil (NR 0x68 b0) variants, Layer 2
priority promotion, the blend modes 6 and 7 with their NR 0x68 b6:5 source
selection, the border exception, and the NR 0x4A fallback colour wherever *every*
layer is transparent. `Renderer::LayerMask` (`--delayed-screenshot-layers`)
forces a layer transparent at that input; it is host-side debug state, so
`reset()` and `save_state()` deliberately ignore it.

## Per-scanline state: snapshots and change logs

**Rendering happens after the frame has already been emulated**, so a naive
renderer would see only end-of-frame register values and every raster split would
collapse. Two mechanisms prevent that.

**Snapshot arrays** store one value per framebuffer row, sampled in
`Emulator::on_scanline`: the NR 0x4A fallback colour, ULA enable (NR 0x68 b7),
stencil and blend mode (NR 0x68 b0 / b6:5), NR 0x14 transparent RGB, the NR 0x1A
ULA clip window, the LoRes register set, the ULA border colour, and tilemap
scroll plus fetch state (NR 0x6C/0x6E/0x6F). All but tilemap scroll snapshot the
*previous* row, because by the time `on_scanline(N)` fires the Copper has
finished line N-1; tilemap scroll latches at the start of the current row, as
hardware does.

**Change logs** record every write with a line tag, replayed during render. Each
owner exposes the same five calls — `start_frame`, `set_current_line`,
`rewind_to_baseline`, `apply_changes_for_line`, `flush_remaining_changes`. The
logs are: palette contents (`PaletteManager`); Layer 2 scroll, clip, bank, enable
and NR 0x70 (five separate logs); sprite attributes and sprite patterns; the
ULA's port-0xFF screen mode, ULA scroll, and the NR 0x43 / NR 0x6B b4
active-palette selectors; the tilemap's NR 0x6B; the `AttributeMux` on the `Mmu`
(Nirvana-class mid-frame attribute rewrites, resolved per *column* rather than
per row); and NR 0x15 layer priority + sprite enable on the `Renderer` itself.

Two details there are load-bearing. Writes tagged during vblank never match a
visible row, so `flush_remaining_changes` must drain them or they are lost
forever — the next frame's baseline would not contain them. And writes before the
display starts are coalesced to row 0 rather than given a sentinel tag, because a
sentinel at the head of the log stalls the cursor walk.

Still read live, at frame granularity: NR 0x4B/0x4C transparency indices, the
sprite and tilemap clip windows, and NR 0x68 b3 (ULA+ enable — `Ula` has the
per-line snapshot API but nothing calls it). The coverage is demand-driven: a
register gets a log when a real program needs one.

## Accepted limitation: mid-line writes apply from the start of the row

Both mechanisms are row-granular, so a register write that physically lands
part-way through a scanline is applied from the **beginning** of that row.
Hardware samples those latches per pixel and would change only the pixels after
the write. The residual error is therefore **bounded to at most one row and is
one-directional** — an effect can appear early, never late. Closing it properly
needs sub-row granularity or a cycle-accurate rendering refactor, both assessed
and declined; this is a known modelling limitation, not a bug. `AttributeMux` is
the one place the finer granularity was needed, and it resolves per column.
