# ULA Video Compliance Test Plan

VHDL-derived compliance test plan for the ULA video subsystem of the JNEXT
ZX Spectrum Next emulator. All expected behaviour is taken directly from the
FPGA VHDL sources (`zxula.vhd`, `zxula_timing.vhd`, `zxnext.vhd`), not from
the C++ implementation.

## Purpose

The ULA is the most fundamental display component: it renders the classic
256x192 pixel area, the border, Timex hi-res/hi-colour modes, and drives
contention and floating bus behaviour. Incorrect ULA behaviour is visible in
virtually every program. This test plan defines the checks needed to verify
VHDL-faithful implementation across all ULA operating modes.

## Scope

| Area                         | VHDL Source                | Section |
|------------------------------|----------------------------|---------|
| Screen address calculation   | `zxula.vhd` lines 218-263  | 1       |
| Attribute rendering          | `zxula.vhd` lines 410-558  | 2       |
| Border colour                | `zxula.vhd` lines 414-419  | 3       |
| Flash timing                 | `zxula.vhd` lines 470-481  | 4       |
| Timex hi-res / hi-colour     | `zxula.vhd` lines 384-393  | 5       |
| ULAnext mode                 | `zxula.vhd` lines 492-529  | 6       |
| ULA+ mode                    | `zxula.vhd` lines 531-541  | 7       |
| Clip windows                 | `zxula.vhd` line 562       | 8       |
| Pixel scrolling              | `zxula.vhd` lines 193-216  | 9       |
| Floating bus                 | `zxula.vhd` lines 308-345  | 10      |
| Contention timing            | `zxula.vhd` lines 578-601  | 11      |
| ULA disable (NR 0x68)        | `zxnext.vhd` line 5445     | 12      |
| Timing constants             | `zxula_timing.vhd`         | 13      |
| Frame interrupt               | `zxula_timing.vhd` 547-559 | 14      |
| Shadow screen                | `zxnext.vhd` line 4453     | 15      |

## Architecture

### Test approach

Each section below defines a set of deterministic test cases. Tests will be
implemented as headless emulator runs with programmatic VRAM setup and
register configuration, verifying the output pixel stream (or contention
T-state counts) against VHDL-derived expected values.

Two complementary test strategies:

1. **Unit-level tests** -- Direct C++ tests that configure the ULA subsystem
   registers and VRAM contents, step the video pipeline, and compare output
   pixel indices against hand-computed expected values from the VHDL.

2. **Screenshot regression tests** -- Headless runs of small Z80 programs
   that exercise specific ULA features, compared against reference PNGs
   generated from known-correct behaviour.

### File layout

```
test/
  ula_video/
    ula_video_test.cpp          # Unit-level test runner
    test_cases.h                # Test case data structures
  ula_video_programs/           # Z80 test programs for screenshot tests
    border_colours.asm
    flash_timing.asm
    timex_hires.asm
    ...
doc/design/
  ULA-VIDEO-TEST-PLAN-DESIGN.md # This document
```

## Section 1: Screen Address Calculation

### VHDL reference

The ULA generates 14-bit VRAM addresses. For pixel bytes:

```
vram_a = screen_mode(0) & py(7:6) & py(2:0) & py(5:3) & px(7:3)
```

For attribute bytes (when `screen_mode(1) = '0'`):

```
vram_a = screen_mode(0) & "110" & py(7:3) & px(7:3)
```

The classic ZX Spectrum screen address layout: pixel address bits 12-8 are
`{py[7:6], py[2:0]}`, bits 7-5 are `py[5:3]`, bits 4-0 are `px[7:3]`.
Attribute address has bits 12-10 = `"110"`, bits 9-5 = `py[7:3]`, bits 4-0 =
`px[7:3]`.

Bit 13 (`screen_mode(0)`) selects the alternate display file (Timex mode or
shadow screen).

### Test cases (~12 tests)

| # | Test | py | px | Expected pixel addr | Expected attr addr |
|---|------|----|----|--------------------|--------------------|
| 1 | Top-left pixel | 0 | 0 | 0x0000 | 0x1800 |
| 2 | First char row, col 1 | 0 | 8 | 0x0001 | 0x1801 |
| 3 | Pixel row 1 in char row 0 | 1 | 0 | 0x0100 | 0x1800 |
| 4 | Pixel row 7 in char row 0 | 7 | 0 | 0x0700 | 0x1800 |
| 5 | Char row 1, pixel row 0 | 8 | 0 | 0x0020 | 0x1820 |
| 6 | Third of screen (py=64) | 64 | 0 | 0x0800 | 0x1900 |
| 7 | Bottom-right pixel | 191 | 248 | 0x17FF | 0x1AFF |
| 8 | Alternate display file (mode(0)=1) | 0 | 0 | 0x2000 | 0x3800 |
| 9 | Middle of screen (py=96, px=128) | 96 | 128 | 0x0890 | 0x1990 |
| 10| Wrap within third (py=63) | 63 | 0 | 0x07E0 | 0x18E0 |
| 11| Second third start+1 row | 65 | 0 | 0x0900 | 0x1900 |
| 12| Last pixel row of last char | 191 | 0 | 0x17E0 | 0x1AE0 |

### Verification

For each (py, px) pair, compute the 14-bit address using the VHDL formulas
and compare against the emulator's address generation.

## Section 2: Attribute Rendering (Standard ULA)

### VHDL reference

Standard ULA pixel output (lines 543-554):

```vhdl
ula_pixel(7 downto 3) <= "000" & not pixel_en & attr_active(6);
if pixel_en = '1' then
   ula_pixel(2 downto 0) <= attr_active(2 downto 0);   -- ink
else
   ula_pixel(2 downto 0) <= attr_active(5 downto 3);   -- paper
end if;
```

The 8-bit `ula_pixel` output encodes: bits 7-3 = `{0,0,0, NOT_pixel, bright}`,
bits 2-0 = ink colour (if pixel set) or paper colour (if pixel clear).

This yields a palette index where:
- Bit 4 = 0 for ink (foreground), 1 for paper (background)
- Bit 3 = bright flag from attribute bit 6
- Bits 2-0 = colour from attribute

### Test cases (~10 tests)

| # | Test | Pixel bit | Attr byte | Expected ula_pixel |
|---|------|-----------|-----------|--------------------|
| 1 | Ink, no bright, colour 0 | 1 | 0x00 | 0x00 |
| 2 | Paper, no bright, colour 0 | 0 | 0x00 | 0x10 |
| 3 | Ink, bright, red (2) | 1 | 0x42 | 0x0A |
| 4 | Paper, bright, green (4) | 0 | 0x60 | 0x1C |
| 5 | Ink white, no bright | 1 | 0x07 | 0x07 |
| 6 | Paper white, bright | 0 | 0x78 | 0x1F |
| 7 | Ink cyan (5), bright | 1 | 0x45 | 0x0D |
| 8 | Flash bit set, no bright, ink | 1 | 0x87 | depends on flash_cnt |
| 9 | Full white on black, bright | 1 | 0x47 | 0x0F |
| 10| Border pixel (border_active_d=1) | - | - | border_clr value |

### Verification

For each combination of pixel_en and attribute byte, compute the expected
8-bit palette index and compare against emulator output.

## Section 3: Border Colour

### VHDL reference

Border colour is encoded as (lines 418-419):

```vhdl
border_clr <= "00" & i_port_fe_border & i_port_fe_border;
```

The 3-bit border colour from port 0xFE bits 2-0 is duplicated into bits 5-3
and 2-0, with bits 7-6 = "00".

Border is active when `border_active = i_phc(8) OR border_active_v`, where
`border_active_v = i_vc(8) OR (i_vc(7) AND i_vc(6))` (lines 414-415).

This means border is active when:
- vc >= 192 (bit 8 set, or bits 7+6 both set = 192..255)
- OR phc >= 256 (bit 8 set)

Timex mode border colour (line 419):
```vhdl
border_clr_tmx <= "01" & (not i_port_ff_reg(5 downto 3)) & i_port_ff_reg(5 downto 3);
```

Pentagon-specific: border colour is updated every half-pixel cycle during
border area (lines 443-449).

### Test cases (~8 tests)

| # | Test | port_fe_border | Expected border_clr |
|---|------|---------------|---------------------|
| 1 | Black border | 0 | 0x00 |
| 2 | Blue border | 1 | 0x09 |
| 3 | Red border | 2 | 0x12 |
| 4 | White border | 7 | 0x3F |
| 5 | Green border | 4 | 0x24 |
| 6 | Timex border, port_ff(5:3)=0 | - | 0x70 |
| 7 | Timex border, port_ff(5:3)=7 | - | 0x47 |
| 8 | Border active region boundaries | - | verify at vc=191/192, phc=255/256 |

## Section 4: Flash Timing

### VHDL reference

Flash counter (lines 474-481):

```vhdl
process (i_CLK_7)
begin
   if rising_edge(i_CLK_7) then
      if i_hc = ('0' & X"00") and i_vc = ('0' & X"00") then
         flash_cnt <= flash_cnt + 1;
      end if;
   end if;
end process;
```

The 5-bit flash counter increments once per frame (when hc=0 and vc=0).
Bit 4 controls the flash state, so the flash period is 32 frames (16 on,
16 off).

Flash effect in pixel generation (line 470):

```vhdl
pixel_en <= (shift_reg(15) xor (attr_active(7) and flash_cnt(4) and
            (not i_ulanext_en) and not i_ulap_en)) and not border_active_d;
```

Flash XORs the pixel bit with `attr(7) AND flash_cnt(4)`, but ONLY when
neither ULAnext nor ULA+ is enabled.

### Test cases (~6 tests)

| # | Test | Expected |
|---|------|----------|
| 1 | Flash period = 32 frames | Counter wraps every 32 frames |
| 2 | Flash attr bit=0: no inversion | Pixel unchanged regardless of counter |
| 3 | Flash attr bit=1, counter bit4=0 | Pixel not inverted |
| 4 | Flash attr bit=1, counter bit4=1 | Pixel inverted (ink<->paper swap) |
| 5 | Flash disabled in ULAnext mode | attr(7) flash ignored |
| 6 | Flash disabled in ULA+ mode | attr(7) flash ignored |

## Section 5: Timex Hi-Res and Hi-Colour Modes

### VHDL reference

Screen mode is taken from `port_ff_reg(2:0)` (via `port_ff_screen_mode`),
but forced to "000" when shadow screen is enabled (`i_ula_shadow_en = '1'`,
line 191).

Mode bits:
- `screen_mode(0)` = alternate display file select (bit 13 of VRAM address)
- `screen_mode(1)` = hi-colour mode (attribute bytes replaced by second pixel file)
- `screen_mode(2)` = hi-res mode (512-pixel wide, pixel doubling in shift register)

**Hi-res mode** (`screen_mode(2) = '1'`): The shift register interleaves pixel
and "attribute" bytes to form a 32-bit wide shift register (line 389):

```vhdl
shift_reg_32 <= shift_pbyte(15:8) & shift_abyte(15:8) &
                shift_pbyte(7:0) & shift_abyte(7:0);
```

**Normal mode** (`screen_mode(2) = '0'`): Each pixel bit is doubled to fill
the 14 MHz shift register (lines 390-393), creating the standard 256-pixel
display at 7 MHz effective resolution.

**Hi-colour mode** (`screen_mode(1) = '1'`): Instead of reading attribute
bytes from the attribute area (0x1800-0x1AFF), the second read fetches from
VRAM bank 1 (`vram_a(13) = '1'`), giving per-pixel-row colour attributes.

### Test cases (~8 tests)

| # | Test | Mode | Expected |
|---|------|------|----------|
| 1 | Standard mode (000) | Normal 256x192 | Standard pixel/attr layout |
| 2 | Alt display file (001) | mode(0)=1 | Addr bit 13 = 1 |
| 3 | Hi-colour mode (010) | mode(1)=1 | Attr reads from bank 1 pixel layout |
| 4 | Hi-colour + alt file (011) | Both | Bank 1 attrs, alt pixel file |
| 5 | Hi-res mode (100) | mode(2)=1 | 512 pixels wide, interleaved bytes |
| 6 | Hi-res uses timex border colour | mode(2)=1 | border_clr_tmx instead of border_clr |
| 7 | Shadow screen forces mode "000" | shadow_en=1 | Timex modes disabled |
| 8 | Hi-res attr_reg uses border_clr_tmx | mode(2)=1 | attr_reg loaded with border_clr_tmx |

## Section 6: ULAnext Mode

### VHDL reference

ULAnext is enabled by `nr_43_ulanext_en` (NextREG 0x43 bit 0). The format
register is `nr_42_ulanext_format` (NextREG 0x42, reset default 0x07).

When ULAnext is active (lines 492-529):

**Border**: palette index = `paper_base_index(7:3) & attr_active(5:3)`.
If format = 0xFF, `ula_select_bgnd = '1'` (transparent border).

**Ink** (pixel_en=1): `ula_pixel = attr_active AND ulanext_format`.

**Paper** (pixel_en=0): The format byte determines how many attribute bits
are ink vs paper. A lookup selects the paper palette index:

| Format | Paper pixel output |
|--------|-------------------|
| 0x01 | `1 & attr(7:1)` -- 1 ink bit, 7 paper bits |
| 0x03 | `10 & attr(7:2)` -- 2 ink bits, 6 paper bits |
| 0x07 | `100 & attr(7:3)` -- 3 ink bits (default), 5 paper bits |
| 0x0F | `1000 & attr(7:4)` -- 4 ink bits, 4 paper bits |
| 0x1F | `10000 & attr(7:5)` -- 5 ink bits, 3 paper bits |
| 0x3F | `100000 & attr(7:6)` -- 6 ink bits, 2 paper bits |
| 0x7F | `1000000 & attr(7)` -- 7 ink bits, 1 paper bit |
| 0xFF | `ula_select_bgnd = '1'` -- 8 ink bits, transparent paper |
| other | `ula_select_bgnd = '1'` -- any non-standard format = transparent |

Where `paper_base_index = 0x80` ("10000000").

### Test cases (~12 tests)

| # | Test | Format | Pixel | Attr | Expected |
|---|------|--------|-------|------|----------|
| 1 | Ink, format 0x07 | 0x07 | 1 | 0xFF | 0x07 |
| 2 | Paper, format 0x07 | 0x07 | 0 | 0xFF | 0x9F |
| 3 | Ink, format 0x0F | 0x0F | 1 | 0xAB | 0x0B |
| 4 | Paper, format 0x0F | 0x0F | 0 | 0xAB | 0x8A |
| 5 | Ink, format 0xFF | 0xFF | 1 | 0x42 | 0x42 |
| 6 | Paper, format 0xFF | 0xFF | 0 | 0x42 | bgnd (transparent) |
| 7 | Border, format 0x07 | 0x07 | - | attr(5:3)=5 | 0x85 |
| 8 | Border, format 0xFF | 0xFF | - | - | bgnd (transparent) |
| 9 | Ink, format 0x01 | 0x01 | 1 | 0xFE | 0x00 |
| 10| Paper, format 0x01 | 0x01 | 0 | 0xFE | 0xFF |
| 11| Ink, format 0x3F | 0x3F | 1 | 0xC3 | 0x03 |
| 12| Non-standard format (e.g. 0x05) | 0x05 | 0 | any | bgnd (transparent) |

## Section 7: ULA+ Mode

### VHDL reference

ULA+ is enabled via port 0xFF3B (`port_ff3b_ulap_en`). When active (lines
531-541):

```vhdl
ula_pixel(7 downto 3) <= "11" & attr_active(7 downto 6) & (screen_mode_r(2) or not pixel_en);
if pixel_en = '1' then
   ula_pixel(2 downto 0) <= attr_active(2 downto 0);   -- ink
else
   ula_pixel(2 downto 0) <= attr_active(5 downto 3);   -- paper
end if;
```

Palette index format:
- Bits 7-6 = "11" (ULA+ palette base)
- Bits 5-4 = attribute bits 7-6 (palette group)
- Bit 3 = `screen_mode(2) OR NOT pixel_en` (1 for paper or hi-res mode)
- Bits 2-0 = ink colour (pixel on) or paper colour (pixel off)

### Test cases (~6 tests)

| # | Test | Pixel | Attr | Mode | Expected |
|---|------|-------|------|------|----------|
| 1 | Ink, group 0 | 1 | 0x07 | normal | 0xC7 |
| 2 | Paper, group 0 | 0 | 0x38 | normal | 0xCF |
| 3 | Ink, group 3 | 1 | 0xC2 | normal | 0xF2 |
| 4 | Paper, group 3 | 0 | 0xF8 | normal | 0xFF |
| 5 | Hi-res forces bit 3 high | 1 | 0x07 | hires | 0xCF |
| 6 | Flash bit NOT used (attr bit 7 = palette group) | - | 0x80 | normal | group 2 |

## Section 8: Clip Windows

### VHDL reference

ULA clip window is set by NextREG 0x1A (4 writes cycling x1, x2, y1, y2).
Reset defaults: x1=0x00, x2=0xFF, y1=0x00, y2=0xBF.

Clipping logic (line 562):

```vhdl
o_ula_clipped <= '0' when (i_phc >= i_ula_clip_x1 and i_phc <= i_ula_clip_x2
   and i_vc >= i_ula_clip_y1 and i_vc <= i_ula_clip_y2) or border_active = '1' else '1';
```

Key observations:
- Clip coordinates use `phc` (practical horizontal count, 0-255 for display)
  and `vc` (vertical count, 0-191 for display area).
- Border area is NEVER clipped (`or border_active = '1'`).
- y2 values >= 0xC0 are clamped to 0xBF in `zxnext.vhd` (lines 6779-6782).

### Test cases (~8 tests)

| # | Test | Clip window | Position | Expected clipped |
|---|------|-------------|----------|-----------------|
| 1 | Default window, inside | (0,255,0,191) | (128,96) | 0 (visible) |
| 2 | Narrow window, inside | (64,192,32,160) | (128,96) | 0 (visible) |
| 3 | Narrow window, outside left | (64,192,32,160) | (32,96) | 1 (clipped) |
| 4 | Narrow window, outside right | (64,192,32,160) | (200,96) | 1 (clipped) |
| 5 | Narrow window, outside top | (64,192,32,160) | (128,16) | 1 (clipped) |
| 6 | Narrow window, outside bottom | (64,192,32,160) | (128,180) | 1 (clipped) |
| 7 | Border area: never clipped | any | border region | 0 (visible) |
| 8 | y2 >= 0xC0 clamped to 0xBF | (0,255,0,0xFF) | (128,191) | 0 (visible) |

## Section 9: Pixel Scrolling

### VHDL reference

Scroll registers: `nr_26_ula_scrollx` (NextREG 0x26), `nr_27_ula_scrolly`
(NextREG 0x27), `nr_68_ula_fine_scroll_x` (NextREG 0x68 bit 2). Reset
defaults: all zero.

**Vertical scroll** (lines 193-207): `py_s = vc + scroll_y`. Then py is
wrapped to stay within 0-191:

```
if py_s(8:7) = "11":       py = {NOT py_s(7), py_s(6:0)}   -- 192-255 wraps
elsif py_s(8)='1' or py_s(7:6)="11":  py = {py_s(7:6)+1, py_s(5:0)}
else:                       py = py_s(7:0)
```

This implements modulo-192 wrapping: values 192-255 wrap back, values >= 256
also wrap.

**Horizontal scroll** (line 199):
```
px = fine_scroll_x & (hc(7:3) + scroll_x(7:3)) & scroll_x(2:0)
```

Coarse scroll (bits 7:3) is added to the column counter. Fine scroll (bits
2:0) provides sub-character offset. `fine_scroll_x` (bit 8 of px) enables
half-pixel precision.

The shift register is pre-shifted by the scroll amount (line 395):
```vhdl
shift_reg_ld <= shift_left(shift_reg_32, scroll_amount);
```

### Test cases (~10 tests)

| # | Test | scroll_x | scroll_y | Expected |
|---|------|----------|----------|----------|
| 1 | No scroll | 0 | 0 | Normal display |
| 2 | Scroll Y by 1 | 0 | 1 | Display shifted up 1 pixel |
| 3 | Scroll Y by 191 | 0 | 191 | Display shifted up 191 (= down 1) |
| 4 | Scroll Y wraps at 192 | 0 | 192 | Same as no scroll |
| 5 | Scroll X by 8 (1 char) | 8 | 0 | Display shifted left 1 char |
| 6 | Scroll X by 1 (fine) | 1 | 0 | Fine sub-char scroll |
| 7 | Scroll X by 255 | 255 | 0 | Maximum scroll |
| 8 | Fine scroll X enabled | 0 (fine=1) | 0 | Half-pixel offset |
| 9 | Combined X+Y scroll | 16 | 32 | Both axes scrolled |
| 10| Y scroll wraps mid-third | 0 | 100 | Wraps across screen thirds correctly |

## Section 10: Floating Bus

### VHDL reference

Floating bus captures VRAM data during active display (lines 308-345):

During border: `floating_bus_r = 0xFF`, `floating_bus_en = 0`.

During active display, data is captured at specific `hc` phases:
- `hc(3:0) = 0x1`: reset to 0xFF, disable
- `hc(3:0) = 0x9`: capture VRAM data, enable
- `hc(3:0) = 0xB, 0xD, 0xF`: capture VRAM data (enable stays set)

Output logic (line 573):
```vhdl
o_ula_floating_bus <= (floating_bus_r(7:1) & (floating_bus_r(0) or i_timing_p3))
   when (border_active_ula = '0' and floating_bus_en = '1')
   else i_p3_floating_bus when i_timing_p3 = '1'
   else X"FF";
```

Key points:
- On +3 timing: bit 0 is forced to 1 in the ULA floating bus value
- On +3 timing: fallback is `p3_floating_bus_dat` (captured from contended
  memory accesses) instead of 0xFF
- On 48K/128K timing: fallback is 0xFF
- Port 0xFF read returns the floating bus value on 48K/128K timing, or the
  Timex register value if `nr_08_port_ff_rd_en` is set (line 2813 in zxnext.vhd)

### Test cases (~8 tests)

| # | Test | Timing | Phase | Expected |
|---|------|--------|-------|----------|
| 1 | Border region, 48K | 48K | any | 0xFF |
| 2 | Active display, phase 0x9 | 48K | data capture | VRAM byte |
| 3 | Active display, phase 0xB | 48K | attr capture | VRAM byte |
| 4 | Active display, phase 0x1 | 48K | reset phase | 0xFF |
| 5 | +3 timing, bit 0 forced | +3 | active | bit 0 OR'd to 1 |
| 6 | +3 timing, border fallback | +3 | border | p3_floating_bus_dat |
| 7 | Port 0xFF read, ff_rd_en=0 | 48K | - | floating bus value |
| 8 | Port 0xFF read, ff_rd_en=1 | 48K | - | Timex register value |

## Section 11: Contention Timing

### VHDL reference

Contention is controlled by a wait signal (lines 582-583):

```vhdl
hc_adj <= i_hc(3:0) + 1;
wait_s <= '1' when ((hc_adj(3:2) /= "00") or
   (hc_adj(3:1) = "000" and i_timing_p3 = '1'))
   and i_hc(8) = '0' and border_active_v = '0'
   and i_contention_en = '1' else '0';
```

This means contention occurs when:
- `hc_adj(3:2) != "00"` -- phases 3-14 of each 16-cycle group
  (or additionally phase 1-2 for +3 timing)
- In display area vertically (`border_active_v = '0'`, i.e. vc < 192)
- In display area horizontally (`hc(8) = '0'`, i.e. hc < 256)
- Contention is enabled (`i_contention_en = '1'`)

Contention is enabled only when (from zxnext.vhd line 4481):
- `eff_nr_08_contention_disable` is NOT set
- NOT pentagon timing
- CPU speed is 3.5 MHz (both speed bits = 0)

**48K/128K contention** (line 595): Uses edge-detected MREQ/IORQ signals.
Active when:
- Memory access to contended page AND MREQ was high last cycle
- OR I/O to contended port AND IORQ just went low
- AND NOT +3 timing

**Memory contention pages** (lines 4489-4493):
- 48K: only bank 5
- 128K: odd banks (1,3,5,7)
- +3: banks >= 4

**Port contention** (line 4496):
- Any even port (A0=0), OR port 0x7FFD range, OR ULA+ ports (0xBF3B, 0xFF3B)

**+3 contention** (line 600): Uses WAIT_n instead of clock stretching. Active
when MREQ is active to a contended page.

### Test cases (~12 tests)

| # | Test | Timing | Access | Expected |
|---|------|--------|--------|----------|
| 1 | 48K, bank 5 read, contention phase | 48K | memory | contended |
| 2 | 48K, bank 0 read | 48K | memory | NOT contended |
| 3 | 48K, non-contention phase (hc_adj 3:2 = "00") | 48K | memory | NOT contended |
| 4 | 48K, vc >= 192 (border) | 48K | memory | NOT contended |
| 5 | 48K, even port I/O | 48K | I/O | contended |
| 6 | 48K, odd port I/O | 48K | I/O | NOT contended |
| 7 | 128K, bank 1 read | 128K | memory | contended |
| 8 | 128K, bank 4 read | 128K | memory | NOT contended |
| 9 | +3, bank 4+ read | +3 | memory | contended (WAIT) |
| 10| +3, bank 0 read | +3 | memory | NOT contended |
| 11| Pentagon timing | Pentagon | any | NEVER contended |
| 12| CPU speed > 3.5 MHz | any | any | NEVER contended |

## Section 12: ULA Disable (NR 0x68)

### VHDL reference

NextREG 0x68 controls ULA behaviour:
- Bit 7: ULA disable (`nr_68_ula_en <= not nr_wr_dat(7)`)
- Bit 6-5: Blend mode
- Bit 4: Cancel extended keys
- Bit 2: Fine scroll X enable
- Bit 0: Stencil mode

Reset default: `nr_68_ula_en = '1'` (ULA enabled).

When ULA is disabled (`ula_en_0 = '0'`), the compositor should not include
ULA pixels in the final output. The ULA still generates pixels internally
but they are gated off in the compositor pipeline.

### Test cases (~4 tests)

| # | Test | NR 0x68 | Expected |
|---|------|---------|----------|
| 1 | ULA enabled (default) | bit7=0 | ULA pixels visible |
| 2 | ULA disabled | bit7=1 | ULA pixels suppressed |
| 3 | ULA disable + re-enable | toggle | Pixels return |
| 4 | Blend mode bits | bits 6-5 | Correct blend mode passed to compositor |

## Section 13: Timing Constants

### VHDL reference

From `zxula_timing.vhd`, the per-machine timing parameters:

| Parameter | 48K 50Hz | 128K 50Hz | +3 50Hz | Pentagon |
|-----------|----------|-----------|---------|----------|
| max_hc | 447 | 455 | 455 | 447 |
| max_vc | 311 | 310 | 310 | 319 |
| min_hactive | 128 | 136 | 136 | 128 |
| min_vactive | 64 | 64 | 64 | 80 |
| Pixels/line | 448 | 456 | 456 | 448 |
| Lines/frame | 312 | 311 | 311 | 320 |
| T-states/frame | 69888 | 70908 | 70908 | 71680 |

60Hz variants:
| Parameter | 48K 60Hz | 128K 60Hz |
|-----------|----------|-----------|
| max_vc | 263 | 263 |
| min_vactive | 40 | 40 |
| Lines/frame | 264 | 264 |

ULA horizontal counter: starts at 0 when `hc = min_hactive - 12`. This
gives the ULA a 12-cycle head start for prefetching display data.

ULA vertical counter: resets to 0 when `vc = min_vactive`.

Practical horizontal counter (phc): starts at -48 relative to the wide
horizontal active area.

### Test cases (~8 tests)

| # | Test | Machine | Expected |
|---|------|---------|----------|
| 1 | 48K frame length | 48K | 448 * 312 / 2 = 69888 T-states |
| 2 | 128K frame length | 128K | 456 * 311 / 2 = 70908 T-states |
| 3 | Pentagon frame length | Pentagon | 448 * 320 / 2 = 71680 T-states |
| 4 | Active display start 48K | 48K | hc=128, vc=64 |
| 5 | Active display start 128K | 128K | hc=136, vc=64 |
| 6 | Active display start Pentagon | Pentagon | hc=128, vc=80 |
| 7 | ULA hc resets correctly | all | hc_ula=0 at min_hactive-12 |
| 8 | 60Hz frame length | 48K 60Hz | 448 * 264 / 2 = 59136 T-states |

## Section 14: Frame Interrupt

### VHDL reference

ULA interrupt (lines 547-559):

```vhdl
if (i_inten_ula_n = '0') and (hc = c_int_h) and (vc = c_int_v) then
   int_ula <= '1';
else
   int_ula <= '0';
end if;
```

Interrupt position per machine:
- 48K: hc=116, vc=0
- 128K: hc=128, vc=1
- +3: hc=126, vc=1
- Pentagon: hc=439, vc=319

Line interrupt (lines 562-583): Fires at `hc_ula = 255` when `cvc` matches
the target line. If target line is 0, the actual comparison is against
`max_vc` (i.e., fires at the end of the previous frame).

### Test cases (~6 tests)

| # | Test | Machine | Expected |
|---|------|---------|----------|
| 1 | 48K interrupt position | 48K | hc=116, vc=0 |
| 2 | 128K interrupt position | 128K | hc=128, vc=1 |
| 3 | Pentagon interrupt position | Pentagon | hc=439, vc=319 |
| 4 | Interrupt disabled | inten_ula_n=1 | No interrupt pulse |
| 5 | Line interrupt fires | line=10 | Fires when cvc=9, hc_ula=255 |
| 6 | Line interrupt 0 = last line | line=0 | Fires at cvc=max_vc |

## Section 15: Shadow Screen

### VHDL reference

Shadow screen is selected by `port_7ffd_shadow` (128K paging register bit 3).
This is passed to the ULA as `i_ula_shadow_en` (zxnext.vhd line 4453).

When shadow is enabled:
- `screen_mode_s` is forced to "000" (line 191), disabling Timex modes
- `ula_shadow` output is set to 1, which tells the VRAM controller to read
  from the shadow screen (bank 7 instead of bank 5)

### Test cases (~4 tests)

| # | Test | Shadow | Expected |
|---|------|--------|----------|
| 1 | Normal screen (shadow=0) | 0 | Reads from bank 5 |
| 2 | Shadow screen (shadow=1) | 1 | Reads from bank 7 |
| 3 | Shadow disables Timex modes | 1 | screen_mode forced to "000" |
| 4 | Shadow bit toggles display | toggle | Correct screen content shown |

## Total Test Count

| Section | Area | Tests |
|---------|------|------:|
| 1 | Screen address calculation | 12 |
| 2 | Attribute rendering | 10 |
| 3 | Border colour | 8 |
| 4 | Flash timing | 6 |
| 5 | Timex hi-res/hi-colour | 8 |
| 6 | ULAnext mode | 12 |
| 7 | ULA+ mode | 6 |
| 8 | Clip windows | 8 |
| 9 | Pixel scrolling | 10 |
| 10 | Floating bus | 8 |
| 11 | Contention timing | 12 |
| 12 | ULA disable | 4 |
| 13 | Timing constants | 8 |
| 14 | Frame interrupt | 6 |
| 15 | Shadow screen | 4 |
| | **Total** | **~122** |

## Implementation Notes

### NextREG register summary

| Register | Bits | Function | Reset |
|----------|------|----------|-------|
| NR 0x08 | bit 2 | Port 0xFF read enable | 0 |
| NR 0x1A | 8-bit x4 | ULA clip (x1,x2,y1,y2) | 0,255,0,191 |
| NR 0x26 | 8-bit | ULA scroll X | 0 |
| NR 0x27 | 8-bit | ULA scroll Y | 0 |
| NR 0x42 | 8-bit | ULAnext format | 0x07 |
| NR 0x43 | bit 0 | ULAnext enable | 0 |
| NR 0x68 | bit 7 | ULA disable (inverted) | 0 (enabled) |
| NR 0x68 | bit 2 | Fine scroll X | 0 |
| NR 0x68 | bits 6-5 | Blend mode | 0 |
| NR 0x68 | bit 0 | Stencil mode | 0 |
| Port 0xFE | bits 2-0 | Border colour | 0 |
| Port 0xFF | bits 5-0 | Timex screen mode | 0 |

### Priority of test sections

1. **Critical** (breaks basic display): Sections 1, 2, 3, 13
2. **High** (breaks compatibility): Sections 4, 11, 10, 14
3. **Medium** (Next-specific features): Sections 5, 6, 7, 8, 9, 12, 15

### Relationship to existing tests

The FUSE Z80 test suite (1356/1356) validates CPU timing but not ULA video
output. The screenshot regression tests provide end-to-end validation but
are not fine-grained enough to catch subtle timing or address calculation
errors. This test suite fills the gap with VHDL-derived deterministic checks.
