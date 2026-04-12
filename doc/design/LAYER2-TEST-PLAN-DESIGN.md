# Layer 2 VHDL-Derived Compliance Test Plan

Systematic compliance test plan for the Layer 2 bitmap display subsystem,
derived exclusively from the VHDL sources (`layer2.vhd` and `zxnext.vhd`).
Tests verify that the JNEXT emulator matches the hardware behaviour defined
in the FPGA reference implementation.

## Purpose

Layer 2 is the ZX Spectrum Next's primary bitmap display layer. It supports
three resolutions, two pixel depths, hardware scrolling, palette selection,
clip windowing, bank selection, and participates in the 6-mode SLU priority
compositor. This plan covers all of these features with traceable references
to VHDL signal names and line-level behaviour.

## VHDL Reference Summary

### Source files

- `cores/zxnext/src/video/layer2.vhd` -- Layer 2 pixel generation
- `cores/zxnext/src/zxnext.vhd` -- NextREG wiring, palette lookup, compositor

### Key NextREGs

| NextREG | Signal(s) | Function |
|---------|-----------|----------|
| 0x12 | `nr_12_layer2_active_bank` | Active bank (7 bits, default `0001000` = bank 8) |
| 0x13 | `nr_13_layer2_shadow_bank` | Shadow bank (7 bits, default `0001011` = bank 11) |
| 0x14 | `nr_14_global_transparent_rgb` | Global transparent colour (default 0xE3) |
| 0x15[4:2] | `nr_15_layer_priority` | SLU layer priority (3 bits) |
| 0x16 | `nr_16_layer2_scrollx` | Scroll X low 8 bits |
| 0x17 | `nr_17_layer2_scrolly` | Scroll Y (8 bits) |
| 0x18 | `nr_18_layer2_clip_{x1,x2,y1,y2}` | Clip window (4 writes, auto-incrementing index) |
| 0x1C[0] | -- | Reset clip index to 0 |
| 0x43[2] | `nr_43_active_layer2_palette` | Active palette select (0 or 1) |
| 0x69[7] | -- | Layer 2 enable via NextREG (writes `port_123b_layer2_en`) |
| 0x70[5:4] | `nr_70_layer2_resolution` | Resolution: 00=256x192, 01=320x256, 1X=640x256 |
| 0x70[3:0] | `nr_70_layer2_palette_offset` | Palette offset (added to pixel high nibble) |
| 0x71[0] | `nr_71_layer2_scrollx_msb` | Scroll X bit 8 |

### Port 0x123B

| Bit | Signal | Function |
|-----|--------|----------|
| 1 | `port_123b_layer2_en` | Layer 2 enable |
| 0 | `port_123b_layer2_map_wr_en` | Map L2 bank for CPU writes |
| 2 | `port_123b_layer2_map_rd_en` | Map L2 bank for CPU reads |
| 3 | `port_123b_layer2_map_shadow` | Select shadow bank for mapping |
| 7:6 | `port_123b_layer2_map_segment` | Memory segment (0-3) for mapping |
| 4 (set) | `port_123b_layer2_offset` | Bank offset (bits 2:0) |

### Reset Defaults

From VHDL reset block:
- `nr_12_layer2_active_bank` = `0001000` (bank 8)
- `nr_13_layer2_shadow_bank` = `0001011` (bank 11)
- `nr_16_layer2_scrollx` = 0x00
- `nr_17_layer2_scrolly` = 0x00
- `nr_18_layer2_clip_x1` = 0x00
- `nr_18_layer2_clip_x2` = 0xFF
- `nr_18_layer2_clip_y1` = 0x00
- `nr_18_layer2_clip_y2` = 0xBF
- `nr_18_layer2_clip_idx` = 0
- `nr_70_layer2_resolution` = "00" (256x192)
- `nr_70_layer2_palette_offset` = 0x0
- `nr_71_layer2_scrollx_msb` = 0
- `port_123b_layer2_en` = 0 (disabled)
- `nr_43_active_layer2_palette` = 0
- `nr_14_global_transparent_rgb` = 0xE3

## Test Architecture

Tests are visual (screenshot-based) Z80 programs that set up NextREGs,
populate Layer 2 VRAM via port 0x123B memory mapping, and produce
deterministic output compared against reference screenshots.

### Test harness

Each test program:
1. Disables ULA display (`nr_68` bit 7 = 0) to isolate Layer 2
2. Sets Layer 2 NextREGs under test
3. Maps Layer 2 banks via port 0x123B and fills VRAM with test patterns
4. Enables Layer 2 via port 0x123B bit 1 or NextREG 0x69 bit 7
5. Halts; emulator captures screenshot after rendering stabilizes

### File layout

```
test/
  layer2/
    test_l2_256x192_basic.asm       # Basic 256x192 fill
    test_l2_320x256_basic.asm       # Basic 320x256 fill
    test_l2_640x256_basic.asm       # Basic 640x256x4 fill
    test_l2_scroll.asm              # Scroll X/Y tests
    test_l2_clip.asm                # Clip window tests
    test_l2_palette.asm             # Palette offset + select
    test_l2_transparency.asm        # Transparency tests
    test_l2_bank.asm                # Bank selection tests
    test_l2_priority.asm            # Layer priority / promotion
    ...
  layer2-references/
    *.png                           # Reference screenshots
```

## Test Case Catalog

### Group 1: Resolution Modes

#### 1.1 -- 256x192 (8-bit) basic fill

**VHDL basis**: `i_resolution = "00"`, `layer2_wide_res = '0'`

- Address formula: `addr = (y(7:0) & x(7:0))` -- i.e. `y * 256 + x`
- Pixel is 8-bit index into Layer 2 palette
- Display area: 256 columns x 192 rows
- Valid pixel: `hc_eff(8) = '0'` (columns 0..255) AND `vc_eff(8) = '0' AND vc_eff(7:6) /= "11"` (rows 0..191)
- Uses `i_phc` / `i_pvc` (standard coordinates)
- Memory: 3 x 16K banks = 48K (256 * 192 = 49152 bytes)

| Test | Description | Expected |
|------|-------------|----------|
| 1.1.1 | Fill entire 256x192 with colour ramp (y as colour) | Horizontal bands |
| 1.1.2 | Fill with x as colour | Vertical bands |
| 1.1.3 | Verify pixel at (0,0) and (255,191) | Corner pixels correct |
| 1.1.4 | Row 192+ is not displayed (write garbage) | No artefacts below line 191 |

#### 1.2 -- 320x256 (8-bit) basic fill

**VHDL basis**: `i_resolution = "01"`, `layer2_wide_res = '1'`

- Address formula: `addr = (x(8:0) & y(7:0))` -- i.e. `x * 256 + y` (column-major)
- Pixel is 8-bit index into Layer 2 palette
- Display area: 320 columns x 256 rows
- Valid pixel: `hc_eff(8) = '0' OR hc_eff(7:6) = "00"` (columns 0..319) AND `vc_eff(8) = '0'` (rows 0..255)
- Uses `i_whc` / `i_wvc` (wide coordinates)
- Memory: 5 x 16K banks = 80K (320 * 256 = 81920 bytes)
- Clip coordinates: `clip_x1 = i_clip_x1 & '0'`, `clip_x2 = i_clip_x2 & '1'` (doubled + low bit)

| Test | Description | Expected |
|------|-------------|----------|
| 1.2.1 | Fill 320x256 with colour ramp | Full-area coloured image |
| 1.2.2 | Verify column-major addressing (x*256+y) | Correct pixel placement |
| 1.2.3 | Pixel at (319, 255) is visible | Bottom-right corner correct |
| 1.2.4 | Column 320+ not displayed | No artefacts past right edge |

#### 1.3 -- 640x256 (4-bit) basic fill

**VHDL basis**: `i_resolution(1) = '1'`, `layer2_wide_res = '1'`, `layer2_hires_qq = '1'`

- Each byte holds 2 pixels: high nibble first (`sc(1) = '0'`), low nibble second (`sc(1) = '1'`)
- 4-bit pixel value: `"0000" & pixel_nibble` -- only lower 4 bits of palette index used
- Address formula: same as 320x256 column-major (addr = x(8:0) & y(7:0)) but x covers half the pixels per byte
- Display area: 640 columns x 256 rows
- Memory: 5 x 16K banks = 80K (same physical layout as 320x256)
- Palette offset applies to high nibble of resulting 8-bit index

| Test | Description | Expected |
|------|-------------|----------|
| 1.3.1 | Fill 640x256 with alternating nibbles | Checkerboard at half resolution |
| 1.3.2 | Verify high nibble renders first (left pixel) | Pixel ordering correct |
| 1.3.3 | Verify only 16 colours available per offset | Index wraps within 4 bits |
| 1.3.4 | Full 640-pixel wide horizontal line | Spans entire width |

### Group 2: Scrolling

**VHDL basis**: `layer2_scroll_x_q` (9-bit), `layer2_scroll_y_q` (8-bit)

Scroll X: `x_pre = hc_eff + scroll_x` with wrap correction per resolution.
Scroll Y: `y_pre = vc_eff + scroll_y` with wrap correction per resolution.

#### 2.1 -- 256x192 scroll wrapping

- X wraps at 256: correction adds 0 (no explicit wrap needed, 8-bit truncation)
- Y wraps at 192: when `y_pre(8) = '0' AND y_pre(7:6) = "11"` (>= 192), `y(7:6) += 1` which wraps 192->0, 193->1, etc.

| Test | Description | Expected |
|------|-------------|----------|
| 2.1.1 | Scroll X=128, distinct left/right halves | Halves swapped |
| 2.1.2 | Scroll Y=96, distinct top/bottom halves | Halves swapped |
| 2.1.3 | Scroll X=255, Y=191 (maximum) | Wrapped by 1 pixel each axis |
| 2.1.4 | Scroll X bit 8 = 1 (nr_71 bit 0) | 9-bit scroll in 256 mode is mod 256 but changes phase |

#### 2.2 -- 320x256 scroll wrapping

- X wraps at 320: when `x_pre(9) = '1' OR (x_pre(8) = '1' AND x_pre(7:6) /= "00")` (>= 320), `x(8:6) += 3` (adds 192, effectively subtracts 320 mod 512)
- Y wraps at 256: natural 8-bit wrap (no correction needed)

| Test | Description | Expected |
|------|-------------|----------|
| 2.2.1 | Scroll X=160 in 320x256 mode | Left/right halves swapped |
| 2.2.2 | Scroll Y=128 in 320x256 mode | Top/bottom halves swapped |
| 2.2.3 | Scroll X=319 | Single-pixel offset wrap |
| 2.2.4 | Scroll X MSB set (nr_71) for values > 255 | 9-bit X scroll works |

#### 2.3 -- 640x256 scroll wrapping

- Same X/Y wrap logic as 320x256 (same `layer2_wide_res = '1'`)
- Scroll X affects the byte address, not the sub-pixel nibble

| Test | Description | Expected |
|------|-------------|----------|
| 2.3.1 | Scroll X=160 in 640x256 mode | Correct byte-level wrap |
| 2.3.2 | Scroll Y=128 in 640x256 mode | Top/bottom swap |

### Group 3: Clip Window

**VHDL basis**: `nr_18_layer2_clip_{x1,x2,y1,y2}`, `layer2_clip_en` signal

Clip coordinate interpretation depends on resolution:
- 256x192: `clip_x1 = '0' & x1`, `clip_x2 = '0' & x2` (raw 8-bit values, range 0..255)
- 320x256 / 640x256: `clip_x1 = x1 & '0'`, `clip_x2 = x2 & '1'` (doubled values, range 0..511)

Clip Y is always raw 8-bit: `clip_y1 = y1`, `clip_y2 = y2`.

A pixel is visible when:
`hc_eff >= clip_x1 AND hc_eff <= clip_x2 AND vc_eff >= clip_y1 AND vc_eff <= clip_y2 AND hc_valid AND vc_valid`

#### 3.1 -- Auto-incrementing clip index

**VHDL basis**: `nr_18_layer2_clip_idx` cycles 00->01->10->11->00

| Test | Description | Expected |
|------|-------------|----------|
| 3.1.1 | Write 4 values to NR 0x18, verify all 4 stored | Read-back matches |
| 3.1.2 | Write 5th value wraps to x1 | Index auto-wraps |
| 3.1.3 | Reset index via NR 0x1C bit 0 | Index returns to 0 |

#### 3.2 -- 256x192 clip window

| Test | Description | Expected |
|------|-------------|----------|
| 3.2.1 | Default clip (0,0)-(255,191): full display | Entire area visible |
| 3.2.2 | Clip to centre 128x96 window | Only centre quadrant visible |
| 3.2.3 | Clip x1=x2 (single column) | One-pixel-wide vertical stripe |
| 3.2.4 | Clip y1=y2 (single row) | One-pixel-tall horizontal stripe |
| 3.2.5 | Clip x1 > x2 (inverted) | No pixels visible |

#### 3.3 -- 320x256 clip window

| Test | Description | Expected |
|------|-------------|----------|
| 3.3.1 | Default clip in 320x256 | Full 320x256 visible |
| 3.3.2 | Clip register value 80 -> effective x = 160 | Doubled interpretation |
| 3.3.3 | Clip to left half (x2=159) | Only left 160 columns |

#### 3.4 -- 640x256 clip window

| Test | Description | Expected |
|------|-------------|----------|
| 3.4.1 | Same clip register interpretation as 320x256 | Doubled X coordinates |
| 3.4.2 | Clip restricts 640-wide display correctly | Right portion clipped |

### Group 4: Palette

**VHDL basis**: `layer2_palette_offset_q` (4-bit), `layer2_palette_select_{0,1}`, palette RAM lookup

#### 4.1 -- Palette offset

The palette offset is added to the HIGH nibble of the pixel value:
`layer2_pixel = (pixel_pre(7:4) + palette_offset) & pixel_pre(3:0)`

This shifts the 256-entry palette in blocks of 16.

| Test | Description | Expected |
|------|-------------|----------|
| 4.1.1 | Offset=0: pixel 0x00 maps to palette entry 0x00 | Identity mapping |
| 4.1.2 | Offset=1: pixel 0x00 maps to palette entry 0x10 | Shifted by 16 |
| 4.1.3 | Offset=15: pixel 0x00 maps to palette entry 0xF0 | Maximum shift |
| 4.1.4 | Offset wraps: offset=15, pixel=0x1n maps to 0x0n | 4-bit add wraps |

#### 4.2 -- 4-bit mode palette offset

In 640x256 mode, pixel value is `0000 & nibble`, so palette offset applies to upper nibble of `00nn_nnnn` -> effectively the entire upper nibble comes from offset alone.

| Test | Description | Expected |
|------|-------------|----------|
| 4.2.1 | 640x256, offset=0, nibble=5 -> palette entry 0x05 | Low nibble only |
| 4.2.2 | 640x256, offset=3, nibble=5 -> palette entry 0x35 | Offset sets high nibble |

#### 4.3 -- Palette selection

**VHDL basis**: `nr_43_active_layer2_palette` selects palette 0 or 1 via `layer2_palette_select_1`

The L2/sprite palette RAM address is: `{is_sprite, palette_select, pixel(7:0)}`

| Test | Description | Expected |
|------|-------------|----------|
| 4.3.1 | Palette 0 selected (NR 0x43 bit 2 = 0) | Colours from palette 0 |
| 4.3.2 | Palette 1 selected (NR 0x43 bit 2 = 1) | Colours from palette 1 |
| 4.3.3 | Switch palette mid-frame (via Copper) | Top/bottom use different palettes |

### Group 5: Transparency

**VHDL basis**: `layer2_transparent` signal in compositor

A Layer 2 pixel is transparent when:
`(layer2_rgb_2(8:1) = transparent_rgb_2) OR (layer2_pixel_en_2 = '0')`

The comparison is against the **palette RGB output** (top 8 bits of the 9-bit RGB), NOT the pixel index. The global transparent colour is NR 0x14 (default 0xE3).

| Test | Description | Expected |
|------|-------------|----------|
| 5.1 | Pixel index 0xE3 with default palette (identity) is transparent | ULA/fallback shows through |
| 5.2 | Pixel index 0xE3 with remapped palette (non-E3 RGB) is opaque | Palette output differs from transparent colour |
| 5.3 | Pixel index 0x00 with palette mapping to RGB 0xE3 is transparent | Any index producing transparent RGB is transparent |
| 5.4 | Change NR 0x14 to 0x00, pixel 0xE3 becomes opaque | Custom transparent colour |
| 5.5 | Clipped pixel (outside clip window) is transparent | `layer2_pixel_en_2 = 0` |
| 5.6 | Layer 2 disabled: all pixels transparent | `layer2_en_q = 0` -> `layer2_en = 0` |
| 5.7 | Fallback colour (NR 0x4A) visible when all layers transparent | VHDL default fallback |

### Group 6: Bank Selection

**VHDL basis**: `nr_12_layer2_active_bank` (7-bit), bank address computation

#### 6.1 -- Active bank

Bank address computation in VHDL:
```
layer2_bank_eff = ((0 & active_bank(6:4)) + 1) & active_bank(3:0)
layer2_addr_eff = (bank_eff + (00000 & addr(16:14))) & addr(13:0)
```

The `+1` on `active_bank(6:4)` shifts from 16K-bank numbering to SRAM page numbering. The address bits 16:14 select which of the (up to 6) 16K pages within the resolution's memory footprint.

| Test | Description | Expected |
|------|-------------|----------|
| 6.1.1 | Default bank 8: display from bank 8-10 (256x192) | Correct 48K region |
| 6.1.2 | Bank 12: display from bank 12-14 | Different memory visible |
| 6.1.3 | Bank 8 in 320x256 mode: 5 banks (8-12) | 80K addressed correctly |
| 6.1.4 | SRAM address bit 21 = 1 disables pixel | Out-of-range bank forced off |

#### 6.2 -- Shadow bank

**VHDL basis**: `port_123b_layer2_map_shadow` selects between active/shadow bank for CPU mapping

| Test | Description | Expected |
|------|-------------|----------|
| 6.2.1 | Shadow bank (NR 0x13, default 11): map and write | Data visible when active bank switched |
| 6.2.2 | Display always uses NR 0x12 (active), not shadow | Writing to shadow does not affect display |

#### 6.3 -- Port 0x123B memory mapping

**VHDL basis**: `port_123b_layer2_map_{wr_en,rd_en}`, `port_123b_layer2_map_segment`

| Test | Description | Expected |
|------|-------------|----------|
| 6.3.1 | Bit 0 = 1: CPU writes to segment 0 go to L2 bank | VRAM populated |
| 6.3.2 | Bit 2 = 1: CPU reads from L2 bank | Read-back matches |
| 6.3.3 | Segment = 01 (0x4000-0x7FFF): maps second 16K | Bank offset 1 |
| 6.3.4 | Segment = 11: maps based on A15:A14 | CPU address selects page |
| 6.3.5 | Bank offset (bit 4 mode, bits 2:0) | Offset shifts page selection |

### Group 7: Layer Priority and Promotion

**VHDL basis**: `nr_15_layer_priority` (3 bits), `layer2_priority` signal, compositor process

#### 7.1 -- SLU ordering

Six priority modes from NR 0x15 bits 4:2:

| Test | Priority | Order | Description |
|------|----------|-------|-------------|
| 7.1.1 | 000 | SLU | Sprites on top, then L2, then ULA |
| 7.1.2 | 001 | LSU | L2 on top (always, even without priority bit) |
| 7.1.3 | 010 | SUL | Sprites, ULA, then L2 at bottom |
| 7.1.4 | 011 | LUS | L2 on top, ULA middle, sprites bottom |
| 7.1.5 | 100 | USL | ULA on top, sprites, then L2 |
| 7.1.6 | 101 | ULS | ULA on top, L2 middle, sprites bottom |

Each test places a coloured rectangle on L2, ULA, and sprites at overlapping positions and verifies stacking order.

#### 7.2 -- Layer 2 priority promotion

**VHDL basis**: `layer2_priority_2` from palette bit 15 (priority bit)

When a Layer 2 pixel has its priority bit set in the palette entry, `layer2_priority = '1'` promotes it above all other layers in modes 000, 010, 100, 101, 110, 111.

| Test | Description | Expected |
|------|-------------|----------|
| 7.2.1 | Priority bit set in SLU mode (000) | L2 pixel above sprites |
| 7.2.2 | Priority bit set in SUL mode (010) | L2 pixel above sprites and ULA |
| 7.2.3 | Priority bit NOT set in LSU mode (001) | L2 already on top, no difference |
| 7.2.4 | Priority bit with transparent pixel | Priority ignored (transparent overrides) |

#### 7.3 -- Blend modes (modes 110 and 111)

**VHDL basis**: `layer_priorities_2` values "110" and "111"

Mode 110: L2 + mix colour, clamped to 7 per channel.
Mode 111: L2 + mix - 5, clamped to 0..7 per channel.

| Test | Description | Expected |
|------|-------------|----------|
| 7.3.1 | Mode 110: L2 RGB(4,4,4) + ULA RGB(4,4,4) -> clamped (7,7,7) | White output |
| 7.3.2 | Mode 110: L2 RGB(1,1,1) + ULA RGB(1,1,1) -> (2,2,2) | Additive blend |
| 7.3.3 | Mode 111: L2 RGB(4,4,4) + ULA RGB(4,4,4) -> (3,3,3) | Subtractive-5 blend |
| 7.3.4 | Mode 111: L2 RGB(1,1,1) + ULA RGB(1,1,1) = 2 <= 4 -> (0,0,0) | Clamped to zero |

### Group 8: Enable / Disable

**VHDL basis**: `port_123b_layer2_en`, `nr_69_we` -> `port_123b_layer2_en`

| Test | Description | Expected |
|------|-------------|----------|
| 8.1 | Enable via port 0x123B bit 1 | Layer 2 visible |
| 8.2 | Enable via NR 0x69 bit 7 | Layer 2 visible |
| 8.3 | Disable: no L2 pixels rendered | Only ULA/fallback visible |
| 8.4 | Enable/disable mid-frame (via Copper) | Top half on, bottom half off |

### Group 9: Address Generation Details

**VHDL basis**: Detailed address and coordinate arithmetic in `layer2.vhd`

#### 9.1 -- One-pixel-ahead generation

`hc_eff = hc + 1` -- address is generated one pixel ahead of the current position.

| Test | Description | Expected |
|------|-------------|----------|
| 9.1.1 | First visible pixel reads from x=1 (not x=0) | Pixel alignment correct |

#### 9.2 -- Memory layout verification

| Test | Description | Expected |
|------|-------------|----------|
| 9.2.1 | 256x192: byte at offset 0 = pixel (0,0); offset 256 = pixel (0,1) | Row-major, 256 bytes/row |
| 9.2.2 | 320x256: byte at offset 0 = pixel (0,0); offset 1 = pixel (0,1) | Column-major, 256 bytes/column |
| 9.2.3 | 640x256: byte at offset 0 = pixels (0,0) and (1,0) | Two pixels per byte, high nibble first |

### Group 10: Reset Defaults

Verify all Layer 2 registers hold correct values after reset without any NextREG writes.

| Test | Description | Expected |
|------|-------------|----------|
| 10.1 | NR 0x12 reads 0x08 (bank 8) | Default active bank |
| 10.2 | NR 0x13 reads 0x0B (bank 11) | Default shadow bank |
| 10.3 | NR 0x16 reads 0x00 | No X scroll |
| 10.4 | NR 0x17 reads 0x00 | No Y scroll |
| 10.5 | NR 0x18 reads 0x00, 0xFF, 0x00, 0xBF | Default clip window |
| 10.6 | NR 0x70 reads 0x00 | 256x192, offset 0 |
| 10.7 | NR 0x71 reads 0x00 | Scroll X MSB = 0 |
| 10.8 | Layer 2 disabled at reset | No L2 pixels visible |

## Test Case Summary

| Group | Area | Tests |
|-------|------|------:|
| 1 | Resolution modes (256x192, 320x256, 640x256) | 16 |
| 2 | Scrolling (X/Y, all resolutions, wrap) | 10 |
| 3 | Clip window (index, coords, resolution scaling) | 12 |
| 4 | Palette (offset, 4-bit, selection) | 9 |
| 5 | Transparency | 7 |
| 6 | Bank selection (active, shadow, mapping) | 10 |
| 7 | Layer priority and promotion | 12 |
| 8 | Enable / disable | 4 |
| 9 | Address generation | 3 |
| 10 | Reset defaults | 8 |
| | **Total** | **~91** |

## Implementation Notes

### Test program conventions

- All test programs are Z80 assembly (z88dk `zcc` or standalone `z80asm`)
- Programs start at 0x8000 to avoid ROM area
- ULA display is disabled (NR 0x68 bit 7 = 0) and sprites disabled (NR 0x15 bit 0 = 0) unless testing layer interaction
- Layer 2 VRAM is populated via port 0x123B mapping (segment 0-2 for 48K, 0-4 for 80K)
- Each test halts after setup; emulator screenshot captures the result

### Screenshot comparison

Tests use the existing regression framework:
```bash
./build/jnext --headless --machine-type next \
    --load test/layer2/test_l2_xxx.nex \
    --delayed-screenshot /tmp/l2_test.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5
```

Reference screenshots are stored in `test/layer2-references/` and compared
pixel-for-pixel by the regression script.

### Priority for implementation

Recommended implementation order:
1. Group 10 (reset defaults) -- no VRAM setup needed, register read-back only
2. Group 1 (resolutions) -- foundational, validates address generation
3. Group 5 (transparency) -- validates palette-based transparency
4. Group 2 (scrolling) -- validates wrap arithmetic
5. Group 3 (clip window) -- validates clip coordinate scaling
6. Group 4 (palette) -- validates offset and selection
7. Group 6 (bank selection) -- validates memory mapping
8. Group 7 (layer priority) -- validates compositor integration
9. Group 8 (enable/disable)
10. Group 9 (address generation edge cases)

## How to Run

```bash
# Build test programs
make -C test/layer2 all

# Run all Layer 2 compliance tests
bash test/layer2-regression.sh

# Run individual test
./build/jnext --headless --machine-type next \
    --load test/layer2/test_l2_256x192_basic.nex \
    --delayed-screenshot /tmp/test.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5
```
