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

## Current status

Task 3 SKIP-reduction plan (`doc/design/TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md`) landed 2026-04-23 Phase 0 → 4. Measured on main post-closure:

- **`ula_test.cpp`: 113 pass + 29 skip (total 113 live rows)**, `123/48/0/75 → 113/84/0/29`. 10 Phase-0 rows migrated from `skip()` to `// G:` source comments (unobservable-at-this-abstraction, now marked `missing` in the traceability matrix).
- **New companion suite `test/ula/ula_integration_test.cpp`: 6/6/0/0** — covers NR 0x26/0x27 scroll (INT-SCROLL-01/02/03), NR 0x43/0x42 ULAnext (INT-ULANEXT-01), port 0xFF3B ULA+ (INT-ULAPLUS-01), standard-mode alt-file (INT-STANDARD-ALT-01).
- **Phase-2 waves landed**: Wave A scroll (§9, 9 rows), Wave B ULAnext (§6, 13 rows), Wave C ULA+ (§7, 7 rows), Wave D hi-colour + shadow + border_clr_tmx + clip_y2 (§5/§8, 4 rows), Wave E line-interrupt (§14, 3 rows).
- **Fails**: **S13.14** (C-class emulator bug) — `frame_done` does not flip at the 69888 T-state boundary on 48K. VHDL `zxula_timing.vhd` says it should. Retained as a failing `check()` regression witness per process manual §3; Emulator Bug backlog item 4. Do NOT convert to skip.
- **29 remaining skips are all F-blocked** to named subsystem plans (re-homed by Phase 0 with explicit owner strings): Emulator floating-bus (§10, 5 rows), ContentionModel (§11, 12 rows), Compositor NR 0x68 blend-mode (§12, 3 rows), VideoTiming per-machine + int-position (§13, 4 + §14, 3 = 7 rows), Emulator/MMU shadow-screen routing (§15, 2 rows).

Update (2026-07-25, GH #96/#97): `ula_test.cpp` is **116 pass / 0 fail / 0 skip**. §6 gained 8 rows — S6.16-S6.19 pin the display-row border-strip ULAnext encoder routing fixed for GH #96 (STANDARD + HI_COLOUR, colour indexing and the format-0xFF NR $4A fallback), and S6.20-S6.23 give the four LR-140 select_bgnd fallback-mux replica sites (scrolled / hi-colour / hi-res / TMX border) one mutation-verified discriminative row each (GH #97).

Update (2026-07-25, GH #103): `ula_test.cpp` is **119 pass / 0 fail / 0 skip**. §6 gained 3 rows — S6.24-S6.26 pin the FULL top/bottom border rows of `render_border_line`'s non-TMX branch routing through the ULAnext border cycle (`zxula.vhd:494-504`; `border_active_v` at `:414-415` makes no row/strip distinction), closing residual (a) of the GH #96 review note.

Update (2026-07-25, GH #104): `ula_test.cpp` is **122 pass / 0 fail / 0 skip**. §7 gained 3 rows — S7.07-S7.09 pin the STANDARD/HI_COLOUR border (in-row strips AND full rows) routing through the ULA+ encoder (`zxula.vhd:535-540` with `border_active_d=1` → slot low6 = 0x08 | border), closing residual (b) of the GH #96 review note. Both GH #96 residuals are now closed.

Update (2026-09-21, GH #258): `ula_integration_test.cpp` gains INT-ULAPLUS-06/07 (16 rows) — port 0xFF3B palette-mode writes, end to end through a CPU program and `run_frame`; see §7.

Update (2026-09-22, GH #263 follow-up): `ula_integration_test.cpp` gains INT-BORDER-RST-01 (17 rows) — the border after power-on and a hard reset is black; see §3.

Update (2026-09-23, GH #70): §6 gains S6.27-S6.30 — the 16-colour std-ULA
table `PaletteManager::reset()` seeds, and the invariant that constrains it. S6.14 pinned the 0x20-0xFF *repeat*
as a relation and deliberately did not duplicate the colour table, so the
table itself was unpinned and was wrong: six non-bright chromatics at RGB333
level 6 and bright magenta at 0x1C7, one step brighter than the machine on
eight of the sixteen entries. The oracle is the boot chain, since there is no
hardware default at all (`palette_utm`, `zxnext.vhd:6960-6965`, is a `dpram2`
with no `init_file_g`, so the palette RAM powers up all-zero —
`dpram2.vhd:41-46,63-80`): NextZXOS's 0xAA-terminated 16-byte table in
`enNextZX.rom` at bank offset 0x1626, streamed through NR 0x41 into all 256
entries of both ULA banks, byte-identical to `nexload.asm:728-730`
`DefaultPalette`. Widening is `zxnext.vhd:4919`. **S6.30 pins why entry 11 is
0xE7 and not the 0xE3 a "pure" bright magenta would be**: `zxnext.vhd:7100`
compares `ula_rgb_2(8 downto 1)` against NR 0x14, which resets to 0xE3
(`zxnext.vhd:4946`), so an entry whose source byte is 0xE3 renders
TRANSPARENT — jnext's old 0x1C7 was exactly that, and every bright-magenta
ULA pixel was see-through.  It hid because NR 0x4A resets to 0xE3 too
(`zxnext.vhd:5014`) and painted the identical colour underneath; under a NEX,
which sets NR 0x4A = 0, those pixels were simply black.  The row states the
invariant (no default entry may collide with the reset transparency index)
rather than the value, so any future edit to the table is caught.  Not a
fully faithful end state: a NEX <= V1.2 sees neither table on hardware, because
`nexload.asm:396-399` repaints all 256 entries before entry and jnext models
no such sweep — the colours now coincide, the sweep does not.

See `doc/testing/audits/task3-ula-phase4.md` for full per-wave critic verdicts and backlog items.

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
| Debug render entry points    | `zxnext.vhd` 6558/6670     | 16      |

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

### Integration row — the border after a reset

`port_fe_reg` is cleared by the core's one `reset` wire (zxnext.vhd:3587-3593)
and drives `port_fe_border` (:3601-3605), so every reset — power-on, hard and
soft — leaves a black border until software writes port 0xFE. jnext forced it
to white from its phase-1 skeleton (a placeholder with no hardware basis) until
the GH #263 follow-up. The soft-reset case is SRST-10 in the compositor plan.

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| INT-BORDER-RST-01 | Power-on and a hard reset leave the border black | Fresh Next, CPU parked on DI; HALT, one frame; OUT (0xFE) 5, one frame; hard reset (`emulator_cold_boot()`), parked again, one frame | Border register 0 and column 0 black on every row after power-on and after the hard reset; cyan after the OUT | zxnext.vhd:3587-3593,3601-3605; zxula.vhd:543-553 |

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
| 7a (S5.07a) | Shadow screen MASKS, does not clear, the mode | port 0xFF = hi-res, shadow on then off | Hi-res mode back (`port_ff_reg` untouched, zxnext.vhd:3610-3624) |
| 8 | Hi-res attr_reg uses border_clr_tmx | mode(2)=1 | attr_reg loaded with border_clr_tmx |
| 9 (S5.10) | Hi-res renders at 512 px wide (mode=100) | render_scanline emits 512 distinct pixel slots (one per `shift_reg_32` bit). skip — F-G104-RENDER (see G104) |
| 10 (S5.11) | Hi-res border uses 6-bit `border_clr_tmx` field (mode=100) | `border_clr_tmx == "01" & (not port_ff(5:3)) & port_ff(5:3)` — 6 bits, NOT (port_ff>>3)&0x07. skip — F-G105-PALGRP (see G105) |

### §5-PSL — Per-scanline port-0xFF Timex screen-mode replay (G07)

`Ula::screen_mode_reg_` is overwritten on every port-0xFF write; mid-frame
STANDARD↔HI_COLOUR↔HI_RES splits collapse to last-write. Add
`Ula::log_port_ff_write` change-log + `set_current_line` +
`apply_changes_for_line` mirroring the PaletteManager / Layer2 precedent
(`PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A).

VHDL: `zxula.vhd:259-266` (screen_mode bits 2:0 from `port_ff_reg(2:0)`),
`zxnext.vhd:2397, 2713, 2813` (port-0xFF write fan-out).

| ID         | Test                                                                                                       | Expected                                                                                  | Status |
|------------|------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------|--------|
| S5-PSL.01  | Two port-0xFF writes mid-frame at lines L1 < L2 captured at correct scanline boundaries                    | log records both `(line, value)` ordered; replay at L1 sees v1, replay at L2 sees v2     | skip (F-G07-TIMEXMODE, see G07) |
| S5-PSL.02  | Render at line L produces STANDARD pixels for L < split, HI_COLOUR pixels for L >= split                   | Lines below split use attribute-byte path; lines >= split use second pixel-file path     | skip (F-G07-TIMEXMODE, see G07) |
| S5-PSL.03  | Mid-frame HI_RES→STANDARD switch at line L: lines >= L revert to 256-px attribute path                     | renderer's mode dispatch flips at the next-line boundary, not at frame end                | skip (F-G07-TIMEXMODE, see G07) |
| S5-PSL.04  | `Ula::start_frame()` rewinds the per-scanline change-log; line-0 baseline reflects last-frame closing value | First-line render of frame N uses last-write-of-frame-(N-1) as baseline                  | skip (F-G07-TIMEXMODE, see G07) |
| S5-PSL.05  | Save-state snapshot includes the per-scanline change-log; round-trip preserves split rendering             | `Ula::save_state` and `restore_state` round-trip; replay produces identical scanline output | skip (F-G07-TIMEXMODE, see G07) |

> **Width-resolution gap (G104).** The current renderer at
> `src/video/ula.cpp:646+` (with the comment at lines 635-640 documenting
> the 256-pixel approximation) discards every alternate hi-res pixel
> from the VHDL `shift_reg_32` lane interleaving at `zxula.vhd:389-395`.
> S5.10 stays SKIP until the renderer doubles its hi-res output stride
> and the compositor accepts a 512-wide framebuffer in hi-res mode.
> Cross-link: G18 (screenshot scaling) is orthogonal — G104 is the
> emit-side fix.

> **6-bit border-group encoding gap (G105).** VHDL `zxula.vhd:419`
> emits `border_clr_tmx <= "01" & (not port_ff(5:3)) & port_ff(5:3)`
> — a 6-bit composite where the leading "01" is the palette-base
> nibble and the inverted-then-concatenated paper bits drive the
> alternate-quadrant entry. `src/video/ula.cpp:710-712` truncates to
> `(screen_mode_reg_ >> 3) & 0x07`, dropping the inverted-half. The
> visible difference is borderline-only on the legacy 16-entry palette
> (G105 is "cheap once G102/G103 land") but breaks ULAnext/ULA+
> palette-quadrant entries that differ from the 0x00-0x07 row. S5.11
> stays SKIP until the palette store widens (shared with G102/G103).

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

### Test cases (29 tests)

Status (2026-09-23): all 29 `check()` — all pass. (S6.13 is reserved, see the
coverage-gap note below; the two rows added for the ULA palette reset content
took S6.14/S6.15; S6.16-S6.19 pin the GH #96 display-row border-strip
encoder routing and S6.20-S6.23 give the four LR-140 select_bgnd fallback
mux replicas one discriminative row each, GH #97; S6.24-S6.26 pin the GH
#103 full-border-row routing through the non-TMX `render_border_line`
branch; S6.27-S6.30 pin the GH #70 default ULA colour table itself — the
boot chain's 16 bytes, their equivalence with the live NR 0x41 write path,
the rendered ARGB, and the transparency-index invariant behind entry 11.)

| # | Row ID | Test | Format | Pixel | Attr | Expected | Status |
|---|------|------|--------|-------|------|----------|--------|
| 1 | S6.01 | Ink, format 0x07 | 0x07 | 1 | 0xFF | 0x07 | pass |
| 2 | S6.02 | Paper, format 0x07 | 0x07 | 0 | 0xFF | 0x9F | pass |
| 3 | S6.03 | Ink, format 0x0F | 0x0F | 1 | 0xAB | 0x0B | pass |
| 4 | S6.04 | Paper, format 0x0F | 0x0F | 0 | 0xAB | 0x8A | pass |
| 5 | S6.05 | Ink, format 0xFF | 0xFF | 1 | 0x42 | 0x42 | pass |
| 6 | S6.06 | Paper, format 0xFF | 0xFF | 0 | 0x42 | bgnd (transparent) | pass |
| 7 | S6.07 | Border, format 0x07 | 0x07 | - | attr(5:3)=5 | 0x85 | pass |
| 8 | S6.08 | Border, format 0xFF | 0xFF | - | - | bgnd (transparent) | pass |
| 9 | S6.09 | Ink, format 0x01 | 0x01 | 1 | 0xFE | 0x00 | pass |
| 10| S6.10 | Paper, format 0x01 | 0x01 | 0 | 0xFE | 0xFF | pass |
| 11| S6.11 | Ink, format 0x3F | 0x3F | 1 | 0xC3 | 0x03 | pass |
| 12| S6.12 | Non-standard format (e.g. 0x05) | 0x05 | 0 | any | bgnd (transparent) | pass |
| 13| S6.14 | ULA palette reset content, indices 0x20-0xFF | - | - | - | entry i == entry i & 0x0F, both banks (16-colour repeat) | pass |
| 14| S6.15 | Unwritten ULAnext paper/border render (0x89 / 0x81) | 0x07 | 0 | 0x4E | colours 9 and 1 of that repeat, not RRRGGGBB ramp entries | pass |
| 15| S6.16 | STANDARD display-row border strips, ULAnext (GH #96) — zxula.vhd:494-504,:418 | 0x07 | - | border=3 | strips index ULA palette entry 0x80\|border (0x83), not std paper 0x13 | pass |
| 16| S6.17 | STANDARD display-row border strips, format 0xFF (GH #96) — zxula.vhd:500-502 + zxnext.vhd:6987-6991 | 0xFF | - | border=2 | strips take the NR $4A fallback (select_bgnd) | pass |
| 17| S6.18 | HI_COLOUR display-row border strips, ULAnext (GH #96) — zxula.vhd:494-504,:418 | 0x07 | - | border=5 | strips index entry 0x80\|border (0x85), not std paper 0x15 | pass |
| 18| S6.19 | HI_COLOUR display-row border strips, format 0xFF (GH #96) — zxula.vhd:500-502 + zxnext.vhd:6987-6991 | 0xFF | - | border=1 | strips take the NR $4A fallback (select_bgnd) | pass |
| 19| S6.20 | Scrolled-path paper select_bgnd consumer (GH #97) — zxula.vhd:525,:199 + zxnext.vhd:6987-6991 | 0x05 | 0 | scroll_x=8 | display paper cells take the NR $4A fallback via the per-pixel path | pass |
| 20| S6.21 | HI_COLOUR-path paper select_bgnd consumer (GH #97) — zxula.vhd:525 + zxnext.vhd:6987-6991 | 0x05 | 0 | mode 010 | display paper cells take the NR $4A fallback | pass |
| 21| S6.22 | HI_RES-path paper select_bgnd consumer (GH #97) — zxula.vhd:525,:419,:426-427 + zxnext.vhd:6987-6991 | 0x05 | 0 | mode 110 | display paper cells take the NR $4A fallback | pass |
| 22| S6.23 | TMX border-row select_bgnd consumer (GH #97) — zxula.vhd:500-502 + zxnext.vhd:6987-6991 | 0xFF | - | mode 110, top border row | full border row takes the NR $4A fallback | pass |
| 23| S6.24 | STANDARD full top-border row, ULAnext (GH #103) — zxula.vhd:494-504,:414-415,:418 | 0x07 | - | border=3 | full row indexes ULA palette entry 0x80\|border (0x83), not std paper 0x13 | pass |
| 24| S6.25 | STANDARD full bottom-border row, format 0xFF (GH #103) — zxula.vhd:500-502 + zxnext.vhd:6987-6991 | 0xFF | - | border=2 | full row takes the NR $4A fallback (select_bgnd) | pass |
| 25| S6.26 | HI_COLOUR full top-border row, ULAnext (GH #103) — zxula.vhd:494-504,:414-415,:426 | 0x07 | - | mode 010, border=5 | full row indexes entry 0x80\|border (0x85), not std paper 0x15 | pass |
| 26| S6.27 | Default ULA colour table (GH #70) — zxnext.vhd:4919 + zxnext.vhd:6960-6965 / dpram2.vhd:41-46,63-80 | - | - | `reset()` | the 16 std-ULA colours at 0x00-0x1F, both banks, are the boot chain's bytes widened by B0=(B1 or B0) — non-bright chromatics at level 5 | pass |
| 27| S6.28 | Seeding equals the firmware's NR 0x41 write path (GH #70) — zxnext.vhd:4919 | - | - | replay 16 bytes x16 per bank | all 256 entries of both banks bit-identical to `reset()` | pass |
| 28| S6.29 | Rendered default colours are the firmware's (GH #70) — zxnext.vhd:4919 | - | - | `ula_colour()` | non-bright white 0xFFB6B6B6 not 0xFFDBDBDB; bright magenta 0x1CF not 0x1C7 (0xE3 is the NR 0x14 transparency index); no level-6 component anywhere | pass |
| 29| S6.30 | No default entry collides with the reset transparency index (GH #70) — zxnext.vhd:7100 + zxnext.vhd:4946 | - | - | `reset()` + NR 0x14 | no ULA entry has `rgb333 >> 1 == 0xE3`; that is why the boot chain spends a green step on entry 11 (0xE7), and why the old 0x1C7 rendered bright magenta TRANSPARENT | pass |

Integration coverage: **INT-ULANEXT-01** in `ula_integration_test.cpp` — enables NR 0x43 bit 0, sets NR 0x42=0x0F, verifies the rendered paper index matches the lookup at `zxula.vhd:503-515`.

**Coverage gap (Wave B critic, non-blocking)**: the ULA+ 0x7F format encoder path is coded in src/ but not exercised by any test row in this section. **S6.13 stays reserved for it.**

**Known residual (GH #96 review, 2026-07-25) — RESOLVED**: two border sites bypassed the encoder dispatch and stayed on the std-ULA path unconditionally. (a) `render_border_line`'s non-TMX branch (full top/bottom border rows in STANDARD/HI_COLOUR modes) did not route through the ULAnext border cycle (`zxula.vhd:494-504` applies there identically) — **FIXED (GH #103, rows S6.24-S6.26)**. (b) the display-row border strips did not route through the ULA+ encoder (`zxula.vhd:531-541` with `border_active_d=1`) — GH #96 scoped only the ULAnext strips — **FIXED (GH #104, rows S7.07-S7.09 in §7)**.

**S6.14 / S6.15 (2026-07-22)** pin the content `PaletteManager::reset()` leaves
in ULA palette indices `0x20-0xFF` — the region only the ULAnext and LoRes
encoders can reach. `reset()` models the **post-firmware** palette, not the VHDL
power-on state: on hardware `palette_utm` (`zxnext.vhd:6960-6965`) is a `dpram2`
with no `init_file_g`, so it powers up all-zero (`dpram2.vhd:41-46,63-80`) and
tbblue.fw fills all 256 entries of both banks with the 16 colours repeated —
but `--load` injects the program at frame 0 (`src/main.cpp:748`), so the
firmware never runs and `reset()` is the only thing standing in for it. The
rows exist because the region previously held an invented RRRGGGBB-identity
ramp, which NextSIDplayer.nex (ULAnext, NR 0x43 bit 0 = 1, NR 0x42 = 0x07,
writes only the Layer 2 palette) rendered as bright red/orange under `--load`
while rendering correctly when launched from the NextZXOS browser.

> **Runtime renderer integration (G102, see
> `doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md`).** Encoder rows
> S6.01-S6.12 verify the pure ULAnext lookup; the renderer at
> `src/video/ula.cpp:386-423` never invokes it because `kUlaPalette` is
> 16-entry and `render_display_line/_hicolour/_hires` short-circuit on
> the legacy 16-colour table. New integration row **INT-ULANEXT-02**
> in `test/ula/ula_integration_test.cpp` is registered as
> `skip("INT-ULANEXT-02", "F-G102-RUNTIME …")` until the palette store
> widens to 256×2 banks and the renderer routes per the VHDL
> `zxula.vhd:485-528 + zxnext.vhd:6981`.

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

### Test cases (~9 tests)

Status (Wave C, 2026-04-23): all 6 `check()` — all pass. Update (2026-07-25,
GH #104): +3 rows S7.07-S7.09 — STANDARD/HI_COLOUR border (in-row strips and
full top/bottom rows) routes through the ULA+ encoder; the non-TMX border attr
`border_clr = "00" & border & border` (zxula.vhd:418) with `border_active_d=1`
encodes ULA+ slot low6 = 0x08 | border (zxula.vhd:535-540; border_active_v at
:414-415 makes no row/strip distinction). All 9 pass.

| # | Row ID | Test | Pixel | Attr | Mode | Expected | Status |
|---|------|------|-------|------|------|----------|--------|
| 1 | S7.01 | Ink, group 0 | 1 | 0x07 | normal | 0xC7 | pass |
| 2 | S7.02 | Paper, group 0 | 0 | 0x38 | normal | 0xCF | pass |
| 3 | S7.03 | Ink, group 3 | 1 | 0xC2 | normal | 0xF2 | pass |
| 4 | S7.04 | Paper, group 3 | 0 | 0xF8 | normal | 0xFF | pass |
| 5 | S7.05 | Hi-res forces bit 3 high | 1 | 0x07 | hires | 0xCF | pass |
| 6 | S7.06 | Flash bit NOT used (attr bit 7 = palette group) | - | 0x80 | normal | group 2 | pass |
| 7 | S7.07 | STANDARD display-row border strips, ULA+ (GH #104) — zxula.vhd:535-540,:418 | - | border=3 | normal | strips index ULA+ slot 0x08\|border (0x0B), not std paper 0x13 | pass |
| 8 | S7.08 | HI_COLOUR display-row border strips, ULA+ (GH #104) — zxula.vhd:535-540,:418 | - | border=5 | mode 010 | strips index slot 0x08\|border (0x0D), not std paper 0x15 | pass |
| 9 | S7.09 | Full top-border row, ULA+ (GH #104) — zxula.vhd:535-540,:414-415,:418 | - | border=6 | normal | full row indexes slot 0x08\|border (0x0E), not std paper 0x16 | pass |

Integration coverage: **INT-ULAPLUS-01** in `ula_integration_test.cpp` — enables port 0xFF3B and verifies palette-group-3 indices in a rendered row. S4.06 (flash disabled in ULA+ mode) also flipped to pass in Wave C.

> **Runtime palette-index path (G103).** Encoder rows S7.01-S7.06 verify
> the `"11" & attr(7:6) & …` lookup. `src/core/emulator.cpp:1937-1941`
> only forwards the top-2 mode bits to `set_ulap_mode` — the low-6-bit
> `port_bf3b_ulap_index` write per VHDL `zxnext.vhd:4525-4538` is
> silently discarded; ULA+ programs cannot drive their 64-entry
> palette window. New integration row **INT-ULAPLUS-03** in
> `test/ula/ula_integration_test.cpp` registered as
> `skip("INT-ULAPLUS-03", "F-G103-RUNTIME …")` until the index latch is
> wired and the palette store widens (shared with G102/G105).

### Integration rows — port 0xFF3B palette writes (GH #258)

With port 0xBF3B in the palette group (bits 7:6 = "00"), a port 0xFF3B write
is the third CPU requester of the NextREG write bus (`zxnext.vhd:4741-4745`):
register X"FF", byte reordered from the ULA+ GGGRRRBB format to the palette's
RRRGGGBB (`cpu_do(4:2) & cpu_do(7:5) & cpu_do(1:0)`). It then behaves exactly
as an NR 0xFF write. jnext dropped it until GH #258 — the handler implemented
only the mode "01" enable — so ULA+ software using the standard port protocol
showed the default palette. Both rows run a Z80 program in a fresh machine for
one production `run_frame` and check one display column of the rendered frame
(attr 0x07 paper = ULA+ slot 8).

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| INT-ULAPLUS-06 | Port 0xFF3B palette-mode write reaches the screen | CPU enables ULA+, selects index 8, OUTs 0xE0 at frame start; polls NR 0x1F to cvc 100, OUTs 0x03 then 0x1F without re-selecting; polls NR 0x1F to cvc 150, OUTs 0x1C | Green rows 32..131, magenta 132..181, red 182..223 — the reorder, no index auto-increment, the 0x243B select latch untouched, each write from its own row | zxnext.vhd:4532-4535,4597-4598,4741-4745,4919,6957-6958 |
| INT-ULAPLUS-07 | Port 0xFF3B writes the palette only in mode 00 with the port decoded | Slot 8 = 0xE0 in mode 00; then 0xFF3B writes of red-ish bytes in modes 01, 10, 11 and in mode 00 with NR 0x85 b0 = 0; finally IN (0xFF3B) in mode 00 | Green in every display row (ULA+ stays on); IN returns 0xE0 | zxnext.vhd:2439,2686,4548-4549,4563,4741-4745 |

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

Status (Wave A, USER PRIORITY, 2026-04-23): row 1 (S9.01) reclassified to G-comment (no-scroll baseline covered by §1/§2); rows 2-10 flipped to live `check()` — all pass.

| # | Row ID | Test | scroll_x | scroll_y | Expected | Status |
|---|------|------|----------|----------|----------|--------|
| 2 | S9.02 | Scroll Y by 1 | 0 | 1 | Display shifted up 1 pixel | pass |
| 3 | S9.03 | Scroll Y by 191 | 0 | 191 | Display shifted up 191 (= down 1) | pass |
| 4 | S9.04 | Scroll Y wraps at 192 | 0 | 192 | Same as no scroll | pass |
| 5 | S9.05 | Scroll X by 8 (1 char) | 8 | 0 | Display shifted left 1 char | pass |
| 6 | S9.06 | Scroll X by 1 (fine) | 1 | 0 | Fine sub-char scroll | pass |
| 7 | S9.07 | Scroll X by 255 | 255 | 0 | Maximum scroll | pass |
| 8 | S9.08 | Fine scroll X enabled | 0 (fine=1) | 0 | Half-pixel offset | pass |
| 9 | S9.09 | Combined X+Y scroll | 16 | 32 | Both axes scrolled | pass |
| 10| S9.10 | Y scroll wraps mid-third | 0 | 100 | Wraps across screen thirds correctly | pass |

Integration coverage: **INT-SCROLL-01** (NR 0x26 coarse X), **INT-SCROLL-02** (NR 0x27 Y), **INT-SCROLL-03** (NR 0x68 bit 2 fine X) in `ula_integration_test.cpp`. This was the user-priority cluster that motivated the plan.

### §9-PSL — Per-scanline NR 0x26 / NR 0x27 ULA scroll replay (G08)

`Ula` reads `nr_26_ula_scrollx_` / `nr_27_ula_scrolly_` once at frame
end. Mid-frame Copper writes coalesce to last-write. Same Copper-driven
raster-split pattern that drives Beast.nex's NR 0x16/0x17 stripes
applies here too. Mirror the `PaletteManager` / `Layer2` change-log + add
`Ula::set_current_line` + `Ula::apply_changes_for_line`. Per-frame
coverage (S9.02..S9.10) is already live; per-scanline was deliberately
out-of-scope at ULA-plan closure 2026-04-23.

VHDL: `zxula.vhd:193-216` (vertical scroll: `py_s = vc + scroll_y`),
`zxula.vhd:199` (horizontal coarse + fine scroll arithmetic).

| ID        | Test                                                                                | Expected                                                                                | Status |
|-----------|-------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------|--------|
| S9-PSL.01 | Two NR 0x26 writes at scanlines L1 < L2 captured separately                         | log retains both `(line, value)`; replay at L1 sees v1, replay at L2 sees v2            | skip (F-G08-ULASCROLL, see G08) |
| S9-PSL.02 | Mid-frame NR 0x27 (scroll Y) split renders top/bottom regions with different scroll | Lines below split use baseline; lines >= split use the post-write value                 | skip (F-G08-ULASCROLL, see G08) |
| S9-PSL.03 | Mid-frame NR 0x26 fine-scroll (NR 0x68 b2) flip at line L                           | Coarse-scroll bits unaffected; bit-7 fine-enable only flips below split                  | skip (F-G08-ULASCROLL, see G08) |
| S9-PSL.04 | `Ula::start_frame()` rewinds NR 0x26 / NR 0x27 change-log; line-0 baseline correct  | First-line render of frame N uses last-write-of-frame-(N-1) as baseline                  | skip (F-G08-ULASCROLL, see G08) |

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

### GH #196 Phase 1.3 — extra-coverage table folded (2026-08-01)

`TRACEABILITY-MATRIX.md`'s ULA Video section carried a 26-row "Extra coverage
(not in plan)" table (GH #192 lineage). All 26 were independently re-verified:

- **`S13.14`** (frame_done flips exactly at 69888 T-states, 48K) was folded
  into the main table as a normal pass row. It is genuinely live in
  `test/ula/ula_test.cpp:3031` and currently passes (`ula_test` reports
  `Total: 122 Passed: 122 Failed: 0 Skipped: 0`). Its surrounding comment used
  to read "KNOWN FAIL — Task 2 item 4" — stale: the underlying
  `VideoTiming::advance` frame-boundary bug it described has since been fixed
  by other work and nobody updated the comment. The comment was corrected to
  describe it as a regression witness against reintroducing that bug, not a
  known failure.
- The other 25 (`S2.11`; `S13.09`-`S13.13`; `SR.01`-`SR.07`; `SD.01`-`SD.08`;
  `S03P.01`-`S03P.04`) are **not** asserted anywhere in `ula_test.cpp` or
  `ula_integration_test.cpp` today and were dropped. 24 of them (all except
  `S2.11`) are confirmed rewrite orphans — `git log -S'"<ID>"' -- test/ula/`
  shows exactly 2 historical commits each (implemented, later removed).
  `S2.11` is never-implemented — its literal only ever appears in 3 doc
  commits (matrix/plan-doc prose), never in test source. None of the 25 is
  resurrected elsewhere under a different ID with the same assertion, except
  that `S13.10`-`S13.13` (display left=128 / top=64 / width=256 / height=192)
  are already exercised today as a single combined assertion by the live
  `S13.04` check (`VideoTiming::DISPLAY_LEFT/TOP/W/H`, `ula_test.cpp:3004`).

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

Status (Wave E 2026-04-23, with post-closure walkback same day): rows 4-6 were initially flipped to live `check()` (all passing) but walked back to `// G:` source comments post-closure — they validated `VideoTiming` interrupt-class logic with no production consumer (the Emulator scheduler reads local fields directly, never `VideoTiming::*`). Rows 1-3 remain `skip()` F-blocked on VideoTiming per-machine int-position exposure. The VHDL-derived expected values are preserved in the surrounding source comments for future resurrection if the VideoTiming production-wiring backlog item ever lands.

| # | Row ID | Test | Machine | Expected | Status |
|---|------|------|---------|----------|--------|
| 4 | ~~S14.04~~ | ~~Interrupt disabled~~ | ~~inten_ula_n=1~~ | ~~No interrupt pulse~~ | **RETIRED 2026-09-24 (GH #201)** — covered live by `ULA-INT-02` |
| 5 | ~~S14.05~~ | ~~Line interrupt fires~~ | ~~line=10~~ | ~~Fires when cvc=9, hc_ula=255~~ | **RETIRED 2026-09-24 (GH #201)** — covered live by `VT-GH257-01/02/03` + `VT-GH257-05` |
| 6 | ~~S14.06~~ | ~~Line interrupt 0 = last line~~ | ~~line=0~~ | ~~Fires at cvc=max_vc~~ | **RETIRED 2026-09-24 (GH #201)** — covered live by `VT-GH257-04` |

**Why the 2026-04-23 walkback rationale no longer applies, and why these
rows are retired rather than resurrected.** The walkback comment in
`ula_test.cpp` says the `VideoTiming` interrupt state is "test-only dead
code" because "no production code path writes to those setters or reads
those counters". The first half of that is no longer true: `Emulator`
writes `set_interrupt_enable()` at `emulator.cpp:397, 3164, 4241, 4901,
12158`, writes `set_line_interrupt_enable()` / `set_line_interrupt_target()`
from the NR 0x22 / NR 0x23 / port 0xFF handlers, reads
`line_interrupt_enable()` in `reschedule_line_interrupt()` at `:11264`,
and calls `video_timing_.advance()` every frame at `:9919`. The second
half is still true — `ula_int_pulse_count()` / `line_int_pulse_count()`
have no production consumer — so a row written against those counters
would still be measuring an output nothing depends on.

The resolution is that the three CLAIMS are all asserted, at the
production tier, by rows that already exist. Each was opened and its
stimulus read before this retirement was written:

- **S14.04** (`inten_ula_n=1` → no pulse) → `ULA-INT-02` in
  `test/ctc_interrupts/ctc_interrupts_test.cpp`: writes NR 0x22 bit 2 on
  a live `Emulator`, runs a frame, asserts NR 0xC8 bit 0 stays clear.
  `ULA-INT-01` is the discriminator (same fixture, interrupts enabled,
  bit 0 set). The full VHDL trace, walked for this retirement rather than
  assumed: `nr_22_we` → `port_ff_reg(6) <= nr_wr_dat(2)` (`zxnext.vhd:3619-3620`,
  i.e. NR 0x22 bit 2) → `port_ff_interrupt_disable <= port_ff_reg(6)`
  (`:3635`) → `i_inten_ula_n => port_ff_interrupt_disable` (`:6750`, the
  `zxula_timing` port map) → `if (i_inten_ula_n = '0') and (hc = c_int_h)
  and (vc = c_int_v) then int_ula <= '1'` (`zxula_timing.vhd:551`). So the
  row's `inten_ula_n = 1` IS NR 0x22 bit 2 set, and `ULA-INT-02` drives
  exactly that.
- **S14.05** (target N fires at `cvc = N-1`, `hc_ula = 255`) →
  `VT-GH257-01/02/03` in `test/videotiming/videotiming_test.cpp` pin the
  `int_line_num <= i_int_line - 1` map (`zxula_timing.vhd:566-570`) on
  Next, 48K and Pentagon timing, each discriminatively: dropping the `-1`
  moves the Pentagon answer from 515792 to 517584. `VT-GH257-05` proves
  the running emulator raises it in the instruction crossing `hc_ula` 255,
  not raw hc 0.
- **S14.06** (target 0 → last line) → `VT-GH257-04`: `set_line_interrupt_target(0)`
  gives 116432, reachable only if `int_line_num` is `c_max_vc` = 310; a
  literal 0 would give 118256 (`zxula_timing.vhd:566-567`).

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
| 3 | Shadow disables Timex modes | 1 | screen_mode forced to "000" — a mask on the ULA's input (zxula.vhd:191); `port_ff_reg` keeps its value (zxnext.vhd:3610-3624), so the mode returns with the shadow screen off (S5.07 / S5.07a, GH #265 follow-up: jnext used to clear the stored mode bits) |
| 4 | Shadow bit toggles display | toggle | Correct screen content shown |

## Section 16: NR 0xFF palette write side-channel (G150)

### VHDL reference

`zxnext.vhd:6957`:

```vhdl
nr_ulatm_we <= (nr_palette_we and not (nr_43_palette_write_select(1)
                                       xor sel(0)))
               or nr_ff_we;
```

NR 0xFF writes commit a value derived from `nr_wr_dat` into the
ULA/TM palette RAM at address `(0, sel(2), 1, 1, port_bf3b_ulap_index)`
— i.e., the bf3b-indexed slot of the ULA palette. This is the legacy
ULA+ palette-poke side-channel: software that does not raise
`port_ff3b_ulap_en` can still drive the palette by writing NR 0xFF.

`src/port/nextreg.cpp` has no write_handler registered for NR 0xFF;
`regs_[0xFF]` is raw storage only.

### Test cases

| # | Row ID | Test | Expected | Status |
|---|--------|------|----------|--------|
| 1 | S16.01 | NR 0xFF write commits ULA palette entry at the slot indexed by `port_bf3b_ulap_index` (zxnext.vhd:6957) | Palette[bf3b_index] reads back the value written | skip (F-G150-NRFF) |

### Skip-reason taxonomy extension

| Reason code   | Semantics                                                                                                | Rows  |
|---------------|----------------------------------------------------------------------------------------------------------|-------|
| `F-G150-NRFF` | Add NR 0xFF write_handler that derives the value from `nr_wr_dat` and pokes the bf3b-indexed palette slot. Blocked on G102/G103 palette-store widening. | S16.01 |
| `F-G07-TIMEXMODE` | Per-scanline port-0xFF Timex screen-mode replay absent. Add `Ula::log_port_ff_write` change-log + `Ula::set_current_line` + `Ula::apply_changes_for_line` mirroring the PaletteManager / Layer2 precedent. | S5-PSL.01..05 |
| `F-G08-ULASCROLL` | Per-scanline NR 0x26 / NR 0x27 ULA-scroll replay absent. Mirror `PaletteManager` / `Layer2` change-log + `Ula::set_current_line` + `Ula::apply_changes_for_line`. | S9-PSL.01..04 |
| `F-G10-PALSEL`    | Per-scanline NR 0x43 b1-3 + NR 0x6B b4 active-palette **selector** replay absent. Distinct from palette **content** path (already live). | S17.01..04 |

## Section 17: Per-scanline active-palette select (G10)

### VHDL reference

`zxnext.vhd:6957` writes NR 0x43 b1-3 into the palette-write/active-select
field. `zxnext.vhd:3614+` writes NR 0x6B b4 into the tilemap-palette
selector. Both lanes feed the per-scanline active-bank decision in the
renderer. The PaletteManager change-log already exists for palette
**content**; this section pins its **selector** sibling.

| ID     | Test                                                                                                  | Expected                                                                                            | Status |
|--------|-------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------|--------|
| S17.01 | Two NR 0x43 b1-3 writes mid-frame at lines L1 < L2 captured separately                                | `PaletteManager::log_active_select(line, bits)` records both; replay at L1 picks bank-A, L2 bank-B | skip (F-G10-PALSEL, see G10) |
| S17.02 | Mid-frame NR 0x6B b4 flip at line L re-routes tilemap palette select for lines >= L                   | Tilemap renderer reads `tilemap_palette_select` value-2 for lines >= L; lines < L use value-1     | skip (F-G10-PALSEL, see G10) |
| S17.03 | NR 0x43 b1-3 selector and NR 0x6B b4 are independent — flipping one does not perturb the other         | Two change-log streams; cross-write does not race                                                   | skip (F-G10-PALSEL, see G10) |
| S17.04 | `PaletteManager::start_frame()` rewinds the selector change-log; line-0 baseline reflects last-frame   | First-line render of frame N uses last-write-of-frame-(N-1) selector                               | skip (F-G10-PALSEL, see G10) |

## Section 18: `.SCR` screen dump (GH #18)

### VHDL reference

`Ula::screen_dump()` copies the ULA's own storage for a `.SCR` screenshot, so
the only thing it can get wrong is WHICH bank and WHICH address window — and
both are hardware decisions with an oracle.

`zxnext.vhd:6649-6656` — `ula_bank_do <= vram_bank5_do1 when ula_vram_shadow =
'0' else vram_bank7_do`: bank choice is the port 0x7FFD bit-3 shadow bit alone,
independent of the Timex screen mode. `zxula.vhd:191` — `screen_mode_s <=
i_port_ff_reg(2 downto 0) when i_ula_shadow_en = '0' else "000"`: shadow masks
the mode, so a shadow dump is always the classic layout. `zxula.vhd:218` — the
mode field's bit 0 is the alt-file select (pixels 0x6000 / attrs 0x7800).
`zxula.vhd:235/245` fetch both 6144-byte planes in the Timex modes and
`zxula.vhd:389` interleaves them in hi-res, which is why those modes dump
12288 bytes rather than 6912: the second plane is a whole plane, and 768
attribute bytes cannot stand in for it.

The FILE side — which extension selects which format, the auto-generated name,
and reporting a write that did not happen — has no VHDL counterpart and lives
in `screenshot_test` (`test/platform/screenshot_test.cpp`), tombstoned in the
matrix accordingly.

| ID     | Test                                                                                        | Expected                                                                 | Status |
|--------|---------------------------------------------------------------------------------------------|--------------------------------------------------------------------------|--------|
| S18.01 | STANDARD mode, markers at bank-5 +0x0000 / +0x17FF / +0x1800 / +0x1AFF                       | 6912 bytes: 6144 pixels from +0x0000, then 768 attributes from +0x1800   | pass |
| S18.02 | Port 0xFF mode bits = 001 (Timex alt file), markers at +0x2000 / +0x37FF / +0x3800 / +0x3AFF | 6912 bytes from +0x2000 / +0x3800; the primary screen does not appear    | pass |
| S18.03 | Port 0xFF mode bits = 010 (hi-colour), markers at both plane boundaries                      | 12288 bytes: plane 0 from +0x0000, then plane 1 from +0x2000             | pass |
| S18.04 | Port 0xFF mode bits = 110 (hi-res), markers at both plane bases                              | 12288 bytes, same two planes — decoded by a separate arm from S18.03     | pass |
| S18.05 | Shadow-screen enabled, distinct markers in bank 5 and bank 7                                 | The dump is bank 7's; no bank-5 byte appears                             | pass |
| S18.06 | Hi-colour selected in port 0xFF AND shadow enabled                                           | 6912 bytes (mode masked to "000"), port 0xFF still reads 0x02            | pass |

## Total Test Count

| Section | Area | Tests |
|---------|------|------:|
| 1 | Screen address calculation | 12 |
| 2 | Attribute rendering | 10 |
| 3 | Border colour | 8 |
| 4 | Flash timing | 6 |
| 5 | Timex hi-res/hi-colour | 10 (+S5.10/S5.11 for G104/G105; +S5-PSL.01..05 for G07) |
| 6 | ULAnext mode | 12 (+INT-ULANEXT-02 in integration suite) |
| 7 | ULA+ mode | 6 (+INT-ULAPLUS-03 in integration suite) |
| 8 | Clip windows | 8 |
| 9 | Pixel scrolling | 10 (+S9-PSL.01..04 for G08) |
| 10 | Floating bus | 8 |
| 11 | Contention timing | 12 |
| 12 | ULA disable | 4 |
| 13 | Timing constants | 8 |
| 14 | Frame interrupt | 6 |
| 15 | Shadow screen | 4 |
| 16 | NR 0xFF palette side-channel | 1 (G150) |
| 17 | Per-scanline active-palette select | 4 (G10) |
| | **Total** | **~138** |

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


## Section 16: Debug render entry points (Task 22a, 2026-07-12)

`Ula::render_scanline_bank(dst, row, mmu, use_bank7)` renders one scanline
with the VRAM bank **pinned**, instead of following the live port-0x7FFD b3
shadow selector that `render_scanline` uses.  It exists for the debugger's two
ULA views, which must each show *their* bank whichever one the running program
has selected:

* `use_bank7=false` → bank 5, the dedicated 16K dual-port VRAM
  (VHDL `bank5_ram` dpram2, `zxnext.vhd:6558`).
* `use_bank7=true`  → bank 7, the dedicated 8K BRAM
  (VHDL `bank7_ram` dpram2, `zxnext.vhd:6670`).

It delegates to `render_scanline`, so the Timex screen mode (port 0xFF), ULA
scroll, active-palette selector and per-line border snapshot all apply exactly
as in the live compositor path — the entry point it replaced open-coded a
STANDARD-mode-only decode, so a Timex hi-colour / hi-res program's shadow
screen was decoded with the wrong layout.  It saves and restores the live bank
selector, so it never disturbs emulation state.
`Ula::vram_bank7()` exposes that selector for the restore assertion.

> **The `DVP-*` rows below are asserted in `test/debugger/video_panel_test.cpp`,
> which the traceability matrix deliberately does not trace** — it is a
> GUI-gated debugger-panel suite, tombstoned in `%NO_MATRIX_SECTION` as
> "debugger panel RENDERING; the hardware it displays is traced in
> `## Compositor`/`## Layer2`/`## ULA Video`". Their IDs are therefore struck
> through: left live, the generator emitted every one of them as a `missing`
> row of this subsystem, i.e. a coverage gap that does not exist. The `Status`
> column below is the record, and `video_panel_test` is the proof.
> (GH #196 phase 4.2 — all 34 struck rows verified asserted there, none of
> them anywhere else.)

| ID       | Test                                                            | Status |
|----------|-----------------------------------------------------------------|--------|
| ~~DVP-03~~   | bank-5 view shows bank 5 while the shadow screen is selected     | PASS   |
| ~~DVP-03b~~  | bank-5 view shows bank 5 while the primary screen is selected    | PASS   |
| ~~DVP-04~~   | bank-7 view shows bank 7 while the primary screen is selected    | PASS   |
| ~~DVP-04a~~  | bank-7 view shows bank 7 while the shadow screen is selected     | PASS   |
| ~~DVP-04c~~ | the live bank selector is restored after the debug render     | PASS   |
| ~~DVP-04d~~ | …and the debug render leaves it exactly as it found it        | PASS   |
| ~~DVP-12~~   | the debug views apply the NR 0x1A clip window                    | PASS   |
| ~~DVP-12a~~  | …keeping display cells inside the window                         | PASS   |
| ~~DVP-12b~~  | …and clipping the border strips when clip_x1>0 / clip_x2<255     | PASS   |

### ULA clip window in the debug path

The ULA is the only layer whose clip window is applied by the **compositor**
rather than inside its own `render_scanline`: in VHDL the clip is a
compositor-stage signal (`zxnext.vhd:7104` — `ula_clipped` is OR'd into
`ula_transparent`), whereas Layer 2 / Tilemap / Sprites each gate their own
pixel output.  `Renderer::apply_ula_clip(line, row)` was factored out of
`render_frame` (verbatim, no behavioural change) so the debugger's ULA views
can apply the identical mask — otherwise the ULA view would be the only layer
view showing content the compositor suppresses.

Hosted in `test/debugger/video_panel_test.cpp` (`debugger_video_panel_test`),
alongside the rest of the panel-vs-compositor parity rows, because the entry
point exists solely for the debugger and the discriminating fixture is the
panel itself.

## Section 17: The composite "All layers" debug view (Task 36, 2026-07-12)

`Renderer::render_row(out, row, mmu, ram, palette, layer2, sprites, tilemap)`
is `render_frame`'s per-row body, lifted out **verbatim** (no behavioural
change — proven by a 0-pixel-diff regression run and a byte-identical
`--delayed-screenshot-layers all` capture of `sonic.nex`).  It renders every
layer for one framebuffer row and composites it: NR 0x15 priority, per-layer
transparency, the ULA/tilemap merge, Layer 2 priority promotion, the blend
modes, the stencil, the border, and the **NR 0x4A fallback colour**.

The debugger's "All layers" tab (leftmost, selected by default) renders through
it, so the panel and the live output cannot drift.  Two properties make a
second, hand-rolled compositor in the debugger unacceptable:

1. **The fallback colour belongs to no layer.**  The compositor emits it
   wherever *every* layer is transparent (VHDL `zxnext.vhd:7218-7352` — each
   priority mux starts at the fallback and is only overwritten by an opaque
   layer).  `sonic.nex` is the motivating case: it writes NR 0x68 = 0x80 (ULA
   off) and NR 0x4A = 0x13, leaves Layer 2 empty, and its entire sky is that
   fallback (`0x13` in RRRGGGBB = `#0092FF`).  No per-layer view can ever show
   it — which is why the layer panels do not visibly add up to the picture on
   screen, and why this view had to exist.
2. **A debug view must not perturb the machine it inspects.**  The caller owns
   the per-scanline change-log replay; `render_row` advances no cursor and does
   not touch the once-per-frame ULA flash counter.  The panel wraps it in the
   same rewind → apply-per-row → flush round trip `render_frame` performs.

> **The `DVP-*` rows below are asserted in `test/debugger/video_panel_test.cpp`,
> which the traceability matrix deliberately does not trace** — it is a
> GUI-gated debugger-panel suite, tombstoned in `%NO_MATRIX_SECTION` as
> "debugger panel RENDERING; the hardware it displays is traced in
> `## Compositor`/`## Layer2`/`## ULA Video`". Their IDs are therefore struck
> through: left live, the generator emitted every one of them as a `missing`
> row of this subsystem, i.e. a coverage gap that does not exist. The `Status`
> column below is the record, and `video_panel_test` is the proof.
> (GH #196 phase 4.2 — all 34 struck rows verified asserted there, none of
> them anywhere else.)

| ID       | Test                                                            | Status |
|----------|-----------------------------------------------------------------|--------|
| ~~DVP-13~~   | "All layers" view is pixel-for-pixel the emulator's framebuffer  | PASS   |
| ~~DVP-13a~~  | premise: ULA + Layer 2 + tilemap + sprite all reach that frame   | PASS   |
| ~~DVP-13b~~  | the composite view is FB_WIDTH × FB_HEIGHT (640 × 256)           | PASS   |
| ~~DVP-14~~   | NR 0x4A fallback shows where EVERY layer is transparent          | PASS   |
| ~~DVP-14a~~  | premise: NR 0x4A = 0x13 really is #0092FF (sonic.nex's sky)      | PASS   |
| ~~DVP-14b~~  | an opaque tilemap pixel still composites over the fallback       | PASS   |
| ~~DVP-14c~~  | …and so does the sprite                                          | PASS   |
| ~~DVP-14d~~  | the fallback appears in NO per-layer view — only the composite   | PASS   |
| ~~DVP-15~~   | composite honours the raster cut-off (row+1 = unrendered)        | PASS   |
| ~~DVP-15a~~  | …and every row below the raster is the placeholder               | PASS   |
| ~~DVP-16~~   | compositing for the panel leaves every live register untouched   | PASS   |
| ~~DVP-16a~~  | …and does not clobber the tilemap per-line scroll snapshots      | PASS   |
| ~~DVP-16b~~  | premise: the vblank writes really did move the live registers    | PASS   |
| ~~DVP-16c~~  | …including VBLANK-tagged writes, which only the flush replays    | PASS   |
| ~~DVP-17~~   | "All layers" is the leftmost tab and selected by default         | PASS   |
| ~~DVP-17a~~  | …and the per-layer tabs still follow it in order                 | PASS   |
| ~~DVP-18~~   | Background view shows the NR 0x4A fallback colour                | PASS   |
| ~~DVP-18a~~  | premise: the two fallback colours differ                         | PASS   |
| ~~DVP-18d~~  | the view reads the PER-LINE snapshot, not the live NR 0x4A       | PASS   |
| ~~DVP-18b1~~ | premise: a real Copper MOVE reached NR 0x4A mid-frame            | PASS   |
| ~~DVP-18b~~  | **real Copper program** MOVEs NR 0x4A → band split in the view   | PASS   |
| ~~DVP-18c~~  | …and the composite AND the emulator framebuffer agree, row-wise  | PASS   |
| ~~DVP-19~~   | Background view honours the raster cut-off                       | PASS   |
| ~~DVP-19a~~  | …and rendering it preserves NR 0x4A and its per-line snapshots   | PASS   |
| ~~DVP-20~~   | "Background" is the RIGHTMOST tab (and not the selected one)     | PASS   |

### The Background view (NR 0x4A) — why it is per-scanline

`VideoLayerView::Layer::BACKGROUND` is the sequel to DVP-14: the fallback colour
is on screen but in **no layer**, so the rightmost "Background" tab makes it
directly inspectable — the answer to "where does sonic.nex's blue sky come
from?" becomes a thing you can look at rather than something you must deduce.

It reads `Renderer::fallback_for_line(row)` — the very byte `render_row` feeds
`rrrgggbb_to_argb` for that row — and paints the row with it, inside the same
`replay_rewind → replay_line(row) → replay_restore` round trip every other view
uses, honouring the same raster cut-off.  It is **per-line and not a flat
swatch** because the Copper can MOVE NR 0x4A mid-frame to paint a gradient down
the raster (that is exactly why `fallback_per_line_[]` exists in the renderer);
the live `fallback_colour()` is only the frame's last value.  The title carries
the live register (`Background colour (NR 0x4A = $13)`), reusing the existing
per-view title idiom rather than inventing a widget.

**Which row proves what — read this before trusting the table.**

* **DVP-18 / DVP-18d** drive `Renderer::snapshot_fallback_for_line()` *directly*.
  They simulate the snapshot half of `Emulator::on_scanline` without running the
  emulator, and therefore prove only that the **view** reads the per-line array
  instead of the live register.  They say nothing about how that array is
  filled.  They are labelled accordingly and must not be read as Copper
  coverage.
* **DVP-18b / DVP-18b1 / DVP-18c** are the Copper integration, done for real:
  the Z80 is parked on a HALT, Copper bytecode
  (`WAIT vpos=100` → `MOVE NR 0x4A,$E0` → `WAIT vpos=511`) is assembled and
  uploaded through NR 0x60/0x61/0x62, `Emulator::run_frame()` executes it, the
  MOVE reaches NR 0x4A through the real NextReg dispatch, `on_scanline`
  snapshots it, and the band split is asserted in the panel **and** in the
  emulator's own framebuffer.  Same fixture idiom as UDIS-02 in
  `test/compositor/compositor_integration_test.cpp`, which is this repo's
  established proof of the Copper → NextReg path.

Discriminative evidence: swapping `fallback_for_line(row)` for the live
`fallback_colour()` (a flat swatch) fails DVP-18, DVP-18d, DVP-18b and DVP-18c.
Removing the `NR 0x62 = 0xC0` that starts the Copper fails DVP-18b1, DVP-18b and
DVP-18c — i.e. those rows genuinely depend on the Copper executing, not on any
hand-poked register.

Note that BACKGROUND is deliberately **absent** from DVP-14d's "the fallback
appears in no per-layer view" loop: it is not a layer view, it *is* the
fallback.  Adding it there would make DVP-14d self-contradictory.

### Why DVP-16c needs a VBLANK-tagged write — and the DVP-06 blind spot

A state-preservation row built only from *mid-frame* writes cannot see a panel
that rewinds and replays but forgets to `flush_remaining_changes()`: replaying
rows 0..255 happens to walk every visible-line entry back to where it was.  It
is the entries tagged at line >= `FB_HEIGHT` that only the final flush replays
— exactly the `tilemap_demo` class of bug the renderer's flush comment
describes (at NR 0x07 >= 0x02 the whole setup lands in vblank).  DVP-16c plants
one and pins it; removing `replay_restore()` from the panel fails DVP-16c and
nothing else.

**This means the Task-22a group DVP-06 (`DVP-NOMUT`) has a documented blind
spot**: every write it makes is tagged at a visible scanline, so it stays GREEN
against a panel that has lost its `replay_restore()` call (verified by mutation
during Task 36).  Do not read DVP-06 as covering that bug class — DVP-16c is
what guards it, and because `replay_restore()` has a single call site shared by
every `VideoLayerView::Layer` case, DVP-16c covers all of the views, not just
the composite.  DVP-06 remains valid for what it *was* written for (the
`Tilemap::render_scanline_debug` snapshot-clobbering defect).  Both facts are
recorded in the header comment of `test/debugger/video_panel_test.cpp`.

Hosted in `test/debugger/video_panel_test.cpp` (`debugger_video_panel_test`),
with the other panel-vs-compositor parity rows.

## Planned rows carried over from the traceability matrix (GH #196)

These rows were recorded only in `TRACEABILITY-MATRIX.md`, which is now a
generated artifact and can no longer hold a claim of its own. They are
planned and NOT implemented, so they are recorded here — the one place the
generator reads planned rows from — and the matrix emits them as `missing`,
which is what they are.

| ID | Description | VHDL file:line |
|----|-------------|----------------|
| ~~S13.03~~ | ~~Pentagon frame length~~ | **IMPLEMENTED 2026-09-24 (GH #201)** — live `check()` in `ula_test.cpp` §13; the matrix now reads its description and citation from the source, so it no longer belongs in this planned-only table. The source comment that had retired it ("VideoTiming no longer initialises any Pentagon-specific `c_max_hc`/`c_max_vc`") was stale: only the standalone `MachineType::Pentagon` enum went away in Wave 0.3, not Pentagon timing, which `init_timing(MachineTimingMode::TimingPentagon)` still provides. |
| ~~S14.04~~ | ~~Interrupt disabled~~ | **RETIRED 2026-09-24 (GH #201)** — see Section 14 above; covered live by `ULA-INT-02` (`ctc_interrupts_test.cpp`). |
| ~~S14.05~~ | ~~Line interrupt fires~~ | **RETIRED 2026-09-24 (GH #201)** — see Section 14 above; covered live by `VT-GH257-01/02/03` + `VT-GH257-05` (`videotiming_test.cpp`). |
| ~~S14.06~~ | ~~Line interrupt 0 = last line~~ | **RETIRED 2026-09-24 (GH #201)** — see Section 14 above; covered live by `VT-GH257-04` (`videotiming_test.cpp`). |

## Coverage notes (moved from the traceability matrix, GH #196)

The matrix is a generated artifact now and carries no prose of its own; it
links here instead. These notes were written alongside the rows they explain.

Task 3 SKIP-reduction plan (`doc/design/TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md`) landed 2026-04-23 Phase 0 → 4 (final state after post-closure walkback + NR 0x68 bit 3 follow-up). `ula_test.cpp` moved from `123/48/0/75` to `110/81/0/29`; 13 rows migrated from `skip()`/`check()` to `// G:` source comments (status `missing` below) — 10 Phase-0 unobservable-at-this-abstraction reclassifications plus the 3 Wave E rows (S14.04/05/06) walked back post-closure because they validated `VideoTiming` interrupt-class logic with no production consumer. 33 rows flipped from `skip()` to live `check()` passes via five parallel Phase-2 waves. 7 integration rows now live as passes in the companion suite `test/ula/ula_integration_test.cpp` (7/7/0/0 — scroll, ULA+, ULAnext, alt-file, NR 0x68 bit 3 ungated ulap_en). Remaining 29 skips are all F-blocked to named subsystem plans: Emulator floating-bus (5), ContentionModel (12), Compositor NR 0x68 blend-mode (3), VideoTiming per-machine + int-position (7), Emulator/MMU shadow-screen routing (2). See `doc/testing/audits/task3-ula-phase4.md` for row-by-row rationale.
Created 2026-04-23 (commit `08a4296`, renamed and merged at `94ccaf3`) to host end-to-end integration coverage of the Phase-2 flips that require the full `Emulator` fixture (NR 0x26/0x27 scroll composition, port 0xFF3B ULA+ palette, NR 0x43 ULAnext, and HI_COLOUR vs alt-file discrimination). Runtime: `Total:    6  Passed:    6  Failed:    0  Skipped:    0`. No skips; each row is a live pass.
