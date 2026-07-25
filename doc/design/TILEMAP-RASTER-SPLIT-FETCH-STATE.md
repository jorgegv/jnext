# Tilemap raster-split fetch state

## Problem

The tilemap renderer historically sampled NR `$6C`, `$6E`, and `$6F` only
once: whatever values were live when the completed frame was composed were
used for every scanline.

TX-1696 exposes the error. During gameplay it selects the HUD map and tile
definitions before the visible area, switches to the playfield pair around
raw raster line 78, then restores the HUD pair near line 272. On JNext, the
last pair selected painted the whole frame. Depending on interrupt timing,
the result was either HUD tiles repeated as green bands over the playfield or
playfield tiles covering the HUD.

The game is used only as a local compatibility witness. No game data or
fixture is included.

## Hardware authority

In the official `tilemap.vhd`:

- `tm_map_base_i` and `tm_tile_base_i` are live entity inputs
  (`tilemap.vhd:57-58`);
- both inputs are latched whenever the fetch state machine enters `S_IDLE`
  (`tilemap.vhd:345-354`), i.e. for subsequent tile fetches rather than once
  per video frame;
- the selected base contributes directly to every external tile/map memory
  address (`tilemap.vhd:400,403`);
- when attributes are stripped, `default_flags_i` is consumed during the
  tile-attribute fetch (`tilemap.vhd:362-367`).

JNext renders a completed frame scanline-by-scanline rather than simulating
the tile fetcher at master-clock resolution. Its established approximation
for tilemap scroll is therefore also appropriate for these fetch inputs:
snapshot the values at the start of each scanline and render that line from
the snapshot.

## Design

`Tilemap` keeps per-line snapshots of:

- decoded map base (NR `$6E`);
- decoded tile-definition base (NR `$6F`);
- default tile attribute (NR `$6C`).

`Emulator::begin_new_frame()` seeds every line from the frame-start values.
`Emulator::on_scanline()` captures the values live at the start of each
visible framebuffer row, alongside the existing tilemap X/Y scroll capture.
`Tilemap::render_scanline()` uses the row's captured fetch state.

The register readback and live state remain unchanged. The arrays are
transient render history, so they are not serialized; after reset or snapshot
load the live registers remain authoritative until the next frame initializes
the snapshots.

## Test strategy

`tilemap_fetch_split_test` uses only synthesized RAM and palette contents.
Three focused rows independently change the map base, tile-definition base,
and default attribute between two scanlines. Each row requires the first line
to retain the old value and the second to use the new value.

A fourth full-Emulator row installs a Copper program through the real NextREG
ports, waits to raster position 100, switches NR `$6E`, and renders a complete
frame. It checks every visible tilemap row, requiring exactly one colour
transition at `cvc + Renderer::DISP_Y + 1`. This proves that the frame-start
initialization and `on_scanline()` capture are wired into the production
Copper/NextREG/render path, rather than testing the `Tilemap` helpers in
isolation.

The rows fail against the old final-value-only renderer and pass with the
per-line snapshots. TX-1696 is then launched from NextZXOS as a local
end-to-end witness: one frame must contain both its fixed HUD and scrolling
terrain without the former green bands.

## Deliberate limit

A register write in the middle of a scanline takes effect at tile-fetch
granularity on the FPGA. JNext applies it from the following scanline, matching
the existing tilemap-scroll approximation. The FPGA prefetches one character
ahead horizontally (`tilemap.vhd:229`) but derives vertical position directly
from the current counter (`tilemap.vhd:326`), so scanline capture preserves the
vertical raster contract. Column-accurate tilemap register replay is outside
this fix.
