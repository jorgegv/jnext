# Tilemap VHDL-Derived Compliance Test Plan

Systematic compliance test plan for the tilemap subsystem of the JNEXT ZX
Spectrum Next emulator, derived exclusively from the authoritative VHDL sources
(`tilemap.vhd` and `zxnext.vhd`).

## Purpose

The tilemap is one of the Next's most complex video subsystems: it supports
two column modes, two colour depths, scrolling, tile transforms (mirror,
rotate), text mode, transparency, palette selection, clip windowing, and a
configurable layer-priority ("below ULA") mode. This plan defines black-box
tests that verify emulator behaviour against VHDL-derived specifications,
without reference to the C++ implementation.

## Current status

Rewrite in Phase 2 per-row idiom merged on main 2026-04-15 (`task1-wave3-tilemap`).
Measured on main post-merge (commit `a3e1196`):

- **69 plan rows total**, 51 check + 18 skip.
- **38/51 live pass (74.5%)**, 13 fail, 18 skip.
- **SPECIAL**: this rewrite **replaced the control-bit swap "anti-tests"** (TM-40/41/42/43/50/51/52/53/71/104) with VHDL-correct assertions that now FAIL against the current emulator, exposing the `src/video/tilemap.cpp:38-42` bit-assignment swap (2-bit misalignment with dropped bit 3). Reviewer confirmed VHDL `tilemap.vhd:189-195` says bit 6=mode, bit 5=strip_flags, bit 3=textmode; C++ has text_mode_=bit 5 and force_attr_=bit 4. Once the Emulator Bug backlog item for the control-bit swap lands, these 10 rows flip from fail to pass.
- **Fails (C-class legitimate emulator bugs, 13 rows)**:
  - **Control-bit swap family (10 rows)**: TM-40, TM-41, TM-42, TM-43, TM-50, TM-51, TM-52, TM-53, TM-71, TM-104 — existing Emulator Bug backlog item 8.
  - **NEW — TM-15**: rotate transform swaps X/Y (VHDL `tilemap.vhd:323-324`) — not implemented.
  - **NEW — TM-30**: `control(1)=1` enables 9-bit tile index (512 mode, VHDL `tilemap.vhd:194`) — not implemented.
  - **NEW — TM-31**: in 512 mode, `attr(0)` becomes tile bit 8 (VHDL `tilemap.vhd:393`) — not implemented.
- **Skips**: 18 rows — all live outside `Tilemap`'s public API: compositor RGB transparency (TM-44/93/94), PaletteManager routing (TM-100/101/102), clip enforcement in compositor (TM-110..116), below/ULA merge (TM-123), stencil (TM-130/131), disabled-tilemap below logic (TM-140/141).

## Authoritative VHDL Sources

| File | Role |
|------|------|
| `video/tilemap.vhd` | Tilemap pixel loader, tile fetch state machine, transforms, pixel output |
| `zxnext.vhd` | NextREG wiring, reset defaults, palette routing, compositor logic |

## NextREG Map (Tilemap-Related)

Derived from VHDL `nr_wr_reg` write handler and `port_253b_dat` read handler.

| NextREG | Name | VHDL Signals | Description |
|---------|------|-------------|-------------|
| 0x1B | TM Clip Window | `nr_1b_tm_clip_{x1,x2,y1,y2}` | Clip window, 4 writes cycling via `nr_1b_tm_clip_idx` |
| 0x1C | Clip Index Reset | bit 3 resets `nr_1b_tm_clip_idx` to 0 | Reset tilemap clip index |
| 0x2F | TM Scroll X MSB | `nr_30_tm_scrollx(9:8)` | Bits 1:0 = scroll X bits 9:8 |
| 0x30 | TM Scroll X LSB | `nr_30_tm_scrollx(7:0)` | Scroll X bits 7:0 |
| 0x31 | TM Scroll Y | `nr_31_tm_scrolly` | Scroll Y (8-bit, wraps at 256) |
| 0x4C | TM Transparent Index | `nr_4c_tm_transparent_index` | Bits 3:0 = 4-bit transparent colour index |
| 0x6B | TM Control | `nr_6b_tm_en`, `nr_6b_tm_control(6:0)` | Enable + control flags |
| 0x6C | TM Default Attr | `nr_6c_tm_default_attr` | Default tile flags when flags stripped |
| 0x6E | TM Map Base | `nr_6e_tilemap_base_7`, `nr_6e_tilemap_base(5:0)` | Tilemap base address |
| 0x6F | TM Tile Base | `nr_6f_tilemap_tiles_7`, `nr_6f_tilemap_tiles(5:0)` | Tile definitions base address |

### NextREG 0x6B Control Bits (VHDL `control_i`)

From `tilemap.vhd` lines 189-195:

| Bit | VHDL Signal | Meaning |
|-----|-------------|---------|
| 7 | `nr_6b_tm_en` | Tilemap enable (1=on) |
| 6 | `mode_i` | Column mode: 0=40x32, 1=80x32 |
| 5 | `strip_flags_i` | 1=eliminate flags from tilemap entries, use default_flags |
| 4 | (palette select) | Tilemap palette select (0 or 1), used in compositor |
| 3 | `textmode_i` | 1=text mode (7-bit palette offset, no mirror/rotate) |
| 2 | (reserved) | Not implemented (was split_addr) |
| 1 | `mode_512_i` | 1=512 tile mode |
| 0 | `tm_on_top_i` | 1=tilemap always on top of ULA |

### NextREG 0x6C Default Attribute Bits

When `strip_flags_i=1`, `tm_tilemap_1` receives `default_flags_i` instead of
memory data. The attribute byte format:

| Bits | Meaning |
|------|---------|
| 7:4 | Palette offset (4 bits) |
| 3 | X-mirror |
| 2 | Y-mirror |
| 1 | Rotate |
| 0 | ULA-over-tilemap (per-tile, OR'd with `mode_512_i`, AND'd with NOT `tm_on_top_i`) |

## Reset Defaults (from VHDL)

| Register | Reset Value | Notes |
|----------|-------------|-------|
| `nr_6b_tm_en` | 0 | Tilemap disabled |
| `nr_6b_tm_control` | 0x00 | All control bits zero |
| `nr_6c_tm_default_attr` | 0x00 | No palette offset, no transforms |
| `nr_6e_tilemap_base` | 0b0_101100 = bank 5, offset 0x2C | Map at 0x6C00 in bank 5 |
| `nr_6e_tilemap_base_7` | 0 | Bank 5 (not 7) |
| `nr_6f_tilemap_tiles` | 0b0_001100 = bank 5, offset 0x0C | Tiles at 0x4C00 in bank 5 (note: actually offset into 16K) |
| `nr_6f_tilemap_tiles_7` | 0 | Bank 5 (not 7) |
| `nr_4c_tm_transparent_index` | 0xF | Transparent colour = 15 |
| `nr_1b_tm_clip_{x1,x2,y1,y2}` | 0x00, 0x9F, 0x00, 0xFF | Full visible area |
| `nr_30_tm_scrollx` | 0x000 | No horizontal scroll |
| `nr_31_tm_scrolly` | 0x00 | No vertical scroll |

## Test Architecture

### Approach

Each test is a headless emulator run that:
1. Writes specific NextREG values to configure the tilemap
2. Fills tilemap and tile definition memory with known patterns
3. Captures a screenshot after rendering
4. Compares against a reference image (pixel-perfect)

Tests use `--headless --machine next` with the standard delayed-screenshot
mechanism.

### Test Data Layout

Test programs are z88dk NEX files placed in `demo/` or a dedicated
`test/tilemap/` directory. Each test program:
- Sets up NextREGs via `OUT (0x243B), reg` / `OUT (0x253B), val`
- Fills VRAM tile map and tile definition areas
- Enters a wait loop

### File Layout

```
test/
  tilemap/
    test_tm_*.nex           # Test NEX programs (one per test group)
    references/             # Reference PNG screenshots
doc/design/
  TILEMAP-TEST-PLAN-DESIGN.md   # This document
```

## Test Case Catalog

### Group 1: Basic Enable/Disable and Reset Defaults

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-01 | Tilemap disabled by default | `nr_6b_tm_en` resets to 0 | No tilemap pixels visible at boot |
| TM-02 | Enable tilemap | Set 0x6B bit 7 | Tilemap pixels appear |
| TM-03 | Disable tilemap | Clear 0x6B bit 7 | Tilemap pixels disappear, ULA shows through |
| TM-04 | Reset defaults readback | Read all TM NextREGs | Values match VHDL reset table above |

### Group 2: 40-Column Mode (8-bit tiles)

The 40x32 mode uses 8x8 pixel tiles, 40 columns, 32 rows. Each tilemap entry
is 2 bytes: tile index (byte 0) + attribute flags (byte 1).

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-10 | 40-col basic display | `mode_i=0` | 40 columns x 32 rows of 8x8 tiles rendered |
| TM-11 | 40-col tile index range | `tm_tilemap_0` = tile index | All 256 tile indices addressable (0-255) |
| TM-12 | 40-col attribute palette offset | `tm_tilemap_1(7:4)` | Palette offset shifts colour by 16 * offset |
| TM-13 | 40-col X-mirror | `tm_tilemap_1(3)` XOR `tm_tilemap_1(1)` | Tile horizontally flipped |
| TM-14 | 40-col Y-mirror | `tm_tilemap_1(2)` | Tile vertically flipped |
| TM-15 | 40-col rotation | `tm_tilemap_1(1)` | Tile rotated 90 degrees (X/Y swapped) |
| TM-16 | 40-col rotation + X-mirror | Combined transforms | Rotation inverts effective X-mirror |
| TM-17 | 40-col ULA-over-tile flag | `tm_tilemap_1(0)` | Per-tile ULA-over-tilemap when tm_on_top=0 |

VHDL transform logic (line 320-324):
- `effective_x_mirror = tilemap_1(3) XOR tilemap_1(1)` -- rotation inverts x-mirror
- `effective_x = abs_x XOR effective_x_mirror`
- `effective_y = abs_y XOR tilemap_1(2)` -- y-mirror
- `transformed_x = effective_x when rotate=0 else effective_y`
- `transformed_y = effective_y when rotate=0 else effective_x`

### Group 3: 80-Column Mode (4-bit tiles)

The 80x32 mode uses 8x8 pixel tiles at half horizontal resolution (4 pixels
per character cell on screen), 80 columns, 32 rows.

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-20 | 80-col basic display | `mode_i=1` | 80 columns visible, characters half-width |
| TM-21 | 80-col tile attributes | Same attribute format | Palette, mirror, rotate work in 80-col |
| TM-22 | 80-col pixel selection | `hcount_effsub` logic differs | Correct pixel doubling/selection in 80-col |

### Group 4: 512-Tile Mode

When `mode_512_i=1`, tile index is extended to 9 bits using attribute bit 0.

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-30 | 512-tile mode enable | `mode_512_i=1` | 512 tiles accessible |
| TM-31 | 512-tile index construction | `tm_mem_addr_pix_sub_sub = (mode_512 AND tilemap_1(0)) & tilemap_0` | Bit 0 of attr becomes tile index bit 8 |
| TM-32 | 512-tile ULA-over interaction | `pixel_wdata(8) = (tilemap_1(0) OR mode_512) AND NOT tm_on_top` | In 512 mode, "below" is always set (unless tm_on_top) |

VHDL (line 388): `tm_tilemap_pixel_wdata(8) <= (tm_tilemap_1(0) or mode_512_q) and not tm_on_top_q`

This means: in 512-tile mode, the per-tile ULA-over bit is always 1 (OR'd
with mode_512), BUT is forced to 0 when tilemap-on-top is enabled.

### Group 5: Text Mode

When `textmode_i=1`, the tile data is treated as 1bpp (monochrome). The
palette offset extends to 7 bits (attribute bits 7:1), and mirror/rotate are
disabled.

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-40 | Text mode enable | `textmode_i=1` via 0x6B bit 3 | 1bpp rendering, 7-bit palette offset |
| TM-41 | Text mode pixel extraction | `shift_left(mem_data, abs_x(2:0))` then bit 7 | Each pixel is 1 bit from tile data |
| TM-42 | Text mode palette construction | `tilemap_1(7:1) & shifted_bit` | 7-bit palette offset + pixel bit = 8-bit palette index |
| TM-43 | Text mode no transforms | Transforms not applied in textmode | Mirror/rotate bits ignored; repurposed as palette |
| TM-44 | Text mode transparency | `pixel_textmode_s` + global transparent RGB | Text-mode transparency uses RGB comparison, not index |

VHDL text-mode pixel (lines 385-386):
```
tm_tilemap_pixel_data_textmode_shift <= shift_left(mem_data, abs_x(2:0))
tm_tilemap_pixel_data_textmode <= tilemap_1(7:1) & shifted_bit(7)
```

VHDL text-mode transparency (line 7109):
```
tm_transparent <= '1' when (tm_pixel_en_2='0') or
   (tm_pixel_textmode_2='1' and tm_rgb_2(8:1)=transparent_rgb_2) or
   (tm_en_2='0')
```

Note: standard mode uses `pixel_en` (which checks 4-bit index against
`transp_colour_i`), but text mode compares the post-palette RGB against the
global transparent RGB.

### Group 6: Strip Flags Mode

When `strip_flags_i=1`, tilemap entries are single-byte (tile index only).
Attribute flags come from NextREG 0x6C (default attribute).

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-50 | Strip flags mode | `strip_flags_i=1` via 0x6B bit 5 | Tilemap entries are 1 byte each |
| TM-51 | Default attr applied | `tm_tilemap_1 <= default_flags_i` when strip | All tiles use 0x6C attribute |
| TM-52 | Strip flags + 40-col | Entries packed tighter (1 byte per tile) | 40-col map fits in half the space |
| TM-53 | Strip flags + 80-col | Same with 80-col mode | 80-col map with stripped flags |

VHDL (line 363-366): In `S_READ_TILE_1` state, when `strip_flags_q=0`,
tilemap_1 is read from memory; when `strip_flags_q=1`, tilemap_1 is assigned
from `default_flags_i`.

### Group 7: Tile Map Address Calculation

The tilemap address calculation determines where tile indices/attributes are
read from in the 16K VRAM bank.

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-60 | Map base address (bank 5) | `nr_6e_tilemap_base_7=0`, offset | Tiles read from correct bank 5 address |
| TM-61 | Map base address (bank 7) | `nr_6e_tilemap_base_7=1` | Map read from bank 7 |
| TM-62 | Tile def base (bank 5) | `nr_6f_tilemap_tiles_7=0`, offset | Tile pixels from bank 5 |
| TM-63 | Tile def base (bank 7) | `nr_6f_tilemap_tiles_7=1` | Tile pixels from bank 7 |
| TM-64 | Address offset computation | `addr_sub(13:8) + offset(5:0)` concatenated with `addr_sub(7:0)` | Upper 6 bits added, lower 8 passed through |
| TM-65 | Tile address with/without flags | `S_READ_TILE_0`: `tile_sub_sub & '0'` vs `tile_sub_sub` | With flags: entries are 2 bytes (LSB selects byte); without: 1 byte |

VHDL tile map address (lines 395-398):
```
tm_mem_addr_tile_sub <=
  '0' & tile_sub_sub & '0'  when READ_TILE_0 and strip=0   -- even byte (tile index)
  '0' & tile_sub_sub & '1'  when strip=0                    -- odd byte (flags)
  "00" & tile_sub_sub        when strip=1                    -- single byte (tile index only)
```

### Group 8: Tile Pixel Address Calculation

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-70 | Standard pixel address | `(tile_9bit & transformed_y & transformed_x(2:1))` | 4-bit pixels, 2 pixels per byte, row-major |
| TM-71 | Text mode pixel address | `("00" & tile_9bit & abs_y(2:0))` | 1bpp: 1 byte per row, 8 pixels per byte |
| TM-72 | Pixel nibble selection | `transformed_x(0)` selects high/low nibble | x(0)=0 -> bits 7:4, x(0)=1 -> bits 3:0 |

VHDL standard pixel address (line 394):
```
tm_mem_addr_pix_sub <= (pix_sub_sub & transformed_y & transformed_x(2:1))
```

VHDL text mode pixel address:
```
tm_mem_addr_pix_sub <= ("00" & pix_sub_sub & abs_y(2:0))  -- when textmode
```

VHDL nibble selection (line 383):
```
pixel_data(3:0) <= mem_data(7:4) when transformed_x(0)='0' else mem_data(3:0)
```

### Group 9: Scroll Offsets

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-80 | X scroll basic | `tm_scroll_x_i` 10-bit | Tilemap shifts left by scroll amount |
| TM-81 | X scroll wrap at 320 (40-col) | `tm_x_correction` logic | X wraps at 320 pixels (40 tiles * 8) |
| TM-82 | X scroll wrap at 640 (80-col) | `tm_x_correction` logic | X wraps at 640 pixels (80 tiles * 8) |
| TM-83 | Y scroll basic | `tm_scroll_y_i` 8-bit | Tilemap shifts up by scroll amount |
| TM-84 | Y scroll wrap at 256 | `tm_abs_y_s` = `scroll_y + vcounter` (8-bit add, wraps) | Y wraps at 256 pixels (32 tiles * 8) |
| TM-85 | Per-line scroll update | Scroll is sampled at `S_IDLE` per tile fetch | Scroll can change per scanline via copper |

VHDL X-scroll correction (lines 312-316):
```
correction = "1100" when sum >= 1280
           = "0001" when sum >= 960 and mode=40col
           = "0110" when sum >= 640
           = "1011" when sum >= 320 and mode=40col
           = "0000" otherwise
```

The correction is applied to bits 9:6, effectively performing modular
arithmetic for the tilemap's virtual width:
- 40-col: wraps at 320 (40 * 8)
- 80-col: wraps at 640 (80 * 8)

VHDL Y computation (lines 326-328):
```
abs_y_s = scroll_y + vcounter(7:0)     -- 8-bit add wraps at 256
abs_y_mult_sub = (abs_y_s(7:3) & "00") + ("000" & abs_y_s(7:3))   -- *5
abs_y_mult = '0' & mult_sub   when mode=40col
           = mult_sub & '0'   when mode=80col                      -- *10 for 80-col
```

### Group 10: Transparency

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-90 | Standard transparency index | `transp_colour_i` = `nr_4c_tm_transparent_index` | 4-bit pixel matching transp index is transparent |
| TM-91 | Default transparency (0xF) | Reset value is 0xF | Pixel value 15 is transparent by default |
| TM-92 | Custom transparency index | Write 0x4C with different value | Custom index makes those pixels transparent |
| TM-93 | Text mode transparency (RGB) | `tm_pixel_textmode_2=1 and tm_rgb_2(8:1)=transparent_rgb_2` | Text mode compares post-palette RGB, not index |
| TM-94 | Text mode vs standard path | `pixel_en_f` logic | Standard: index check; text: always enabled if pixel_en_s, then RGB check |

VHDL transparency enable (lines 427-429):
```
pixel_en_standard_s <= pixel_en_s and (pixel(3:0) /= transp_colour)
pixel_en_f <= (pixel_en_standard and not textmode) or (pixel_en_s and textmode)
```

Standard mode: pixel is enabled only if 4-bit colour differs from transparent
index. Text mode: pixel is always enabled (transparency handled later in
compositor via RGB comparison).

### Group 11: Palette Selection

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-100 | Palette select 0 | `nr_6b_tm_control(4)=0` | Tilemap uses first palette (offset 0 in ULA/TM palette) |
| TM-101 | Palette select 1 | `nr_6b_tm_control(4)=1` | Tilemap uses second palette (offset 256) |
| TM-102 | Palette routing | `ulatm_pixel_1 = '1' & tm_palette_select & tm_pixel` | Palette address = {1, palette_sel, 8-bit pixel} |
| TM-103 | Standard pixel composition | `pixel_data = {attr(7:4), nibble}` | 4-bit palette offset + 4-bit pixel = 8-bit index |
| TM-104 | Text mode pixel composition | `pixel_data = {attr(7:1), bit}` | 7-bit palette offset + 1-bit pixel = 8-bit index |

VHDL palette routing (line 6981):
```
ulatm_pixel_1 <= ('1' & tm_palette_select_1 & tm_pixel_1) when sc(0)='1'
```

The ULA/TM shared palette has 1K entries: bit 9 selects ULA(0) vs TM(1),
bit 8 selects palette 0/1, bits 7:0 are the pixel value.

### Group 12: Clip Window

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-110 | Default clip (full area) | x1=0, x2=0x9F, y1=0, y2=0xFF | Entire 320x256 visible |
| TM-111 | Custom clip window | Write 0x1B four times | Only pixels within window are enabled |
| TM-112 | Clip X coordinates | `xsv = clip_x1 & '0'`, `xev = clip_x2 & '1'` | X range is clip_x1*2 to clip_x2*2+1 |
| TM-113 | Clip Y coordinates | `ysv = clip_y1`, `yev = clip_y2` | Y range is clip_y1 to clip_y2 |
| TM-114 | Clip index cycling | `nr_1b_tm_clip_idx` increments mod 4 | Successive writes to 0x1B cycle x1,x2,y1,y2 |
| TM-115 | Clip index reset | Write 0x1C bit 3 | Resets clip index to 0 without changing clip values |
| TM-116 | Clip readback | Read 0x1B returns current indexed value | Reads cycle through x1,x2,y1,y2 |

VHDL clip enable (line 424):
```
pixel_en_s <= '1' when hcounter < 320 and vcounter(8)='0'
              and hcounter >= xsv and hcounter <= xev
              and vcounter >= ysv and vcounter <= yev
```

Where `xsv = clip_x1 & '0'` and `xev = clip_x2 & '1'` (clip values are
doubled for the 320-pixel-wide coordinate space, with LSB=0 for start and
LSB=1 for end).

### Group 13: ULA-Over-Tilemap / Layer Priority

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-120 | Tilemap on top (default) | `tm_on_top_i=0` default | ULA shows through transparent TM pixels |
| TM-121 | Tilemap always on top | `tm_on_top_i=1` via 0x6B bit 0 | Tilemap covers ULA completely |
| TM-122 | Per-tile below flag | `pixel_below = (attr(0) OR mode_512) AND NOT tm_on_top` | Per-tile ULA priority |
| TM-123 | Below flag in compositor | `ulatm_rgb = tm_rgb when below=0 or ula_transparent, else ula_rgb` | ULA covers TM when below=1 and ULA non-transparent |
| TM-124 | tm_on_top overrides per-tile | `AND NOT tm_on_top` | When tm_on_top=1, below is forced to 0 |
| TM-125 | 512-mode forces below | `attr(0) OR mode_512` | In 512-tile mode, below is always set (unless tm_on_top) |

VHDL compositor (line 6863):
```
tm_pixel_below_1 <= (tm_pixel_below and tm_en_1a) or
                    ((not nr_6b_tm_control(0)) and not tm_en_1a)
```

When tilemap is disabled (`tm_en_1a=0`), below defaults to `NOT tm_on_top`:
if tm_on_top is 0, below=1, so ULA wins; if tm_on_top is 1, below=0.

VHDL compositor final (line 7116):
```
ulatm_rgb <= tm_rgb when (tm_transparent=0) and (below=0 or ula_transparent=1)
             else ula_rgb
```

### Group 14: Stencil Mode Interaction

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-130 | Stencil mode (ULA AND TM) | `nr_68_ula_stencil_mode=1` | Output = ULA_rgb AND TM_rgb |
| TM-131 | Stencil transparency | `stencil_transparent = ula_transparent OR tm_transparent` | Stencil is transparent if either layer is |

VHDL stencil (lines 7112-7113):
```
stencil_transparent <= ula_transparent OR tm_transparent
stencil_rgb <= (ula_rgb AND tm_rgb) when stencil_transparent=0
```

### Group 15: Tilemap Enable Interaction with Below

| ID | Test | VHDL Basis | Verify |
|----|------|-----------|--------|
| TM-140 | TM disabled, tm_on_top=0 | `tm_en_1a=0`, `nr_6b_tm_control(0)=0` | below=1, ULA shows through |
| TM-141 | TM disabled, tm_on_top=1 | `tm_en_1a=0`, `nr_6b_tm_control(0)=1` | below=0, but TM transparent, so ULA shows |

## Test Execution

### Building test programs

```bash
# Build tilemap test NEX files
make -C demo tilemap_tests
```

### Running tests

```bash
# Individual test
./build/jnext --headless --machine next \
    --load test/tilemap/test_tm_basic.nex \
    --delayed-screenshot /tmp/tm_test.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5

# Full regression suite (includes tilemap tests)
bash test/regression.sh
```

### Reference image comparison

Tests compare captured screenshots pixel-by-pixel against reference PNGs.
Any difference fails the test. References are generated once from a
verified-correct run and stored in `test/tilemap/references/`.

## Priority Order

Implementation priority (most impactful tests first):

1. **Groups 1-2**: Basic enable/disable and 40-column mode (foundation)
2. **Group 10**: Transparency (critical for layer compositing)
3. **Groups 9, 12**: Scrolling and clip window (common usage)
4. **Group 4**: 512-tile mode (used by NextZXOS)
5. **Group 3**: 80-column mode (text displays)
6. **Group 5**: Text mode (specialized)
7. **Groups 6-8**: Address calculations (internal correctness)
8. **Groups 11, 13-15**: Palette, layer priority, stencil (compositor integration)

## Estimated Test Count

| Group | Tests |
|-------|------:|
| 1: Enable/Disable/Defaults | 4 |
| 2: 40-col mode | 8 |
| 3: 80-col mode | 3 |
| 4: 512-tile mode | 3 |
| 5: Text mode | 5 |
| 6: Strip flags | 4 |
| 7: Map address | 6 |
| 8: Pixel address | 3 |
| 9: Scrolling | 6 |
| 10: Transparency | 5 |
| 11: Palette | 5 |
| 12: Clip window | 7 |
| 13: Layer priority | 6 |
| 14: Stencil | 2 |
| 15: Enable/below interaction | 2 |
| **Total** | **~67** |
