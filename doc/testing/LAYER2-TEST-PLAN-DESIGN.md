# Layer 2 VHDL-Derived Compliance Test Plan

> **Plan Rebuilt 2026-04-14 — Retraction Notice**
>
> The previous revision of this document reported "61/61 tests passing" for the
> Layer 2 subsystem. That status is **retracted**. The Task 4 critical review of
> the Step-2 test plans identified the Layer 2 plan (along with several others)
> as **coverage theatre**: the happy-path tests it shipped all passed, but the
> plan left large areas of VHDL-specified behaviour unverified and in several
> places asserted things that could not fail (tautologies / non-stimuli).
>
> Concretely, the retracted plan:
>
> - Never exercised the `layer2_bank_eff` computation in VHDL layer2.vhd:172
>   (the `+1` lift on `active_bank(6:4)`), so the transform from NR 0x12
>   bank-numbering to SRAM-page-numbering was never checked.
> - Had **zero** coverage of port 0x123B, even though that port is the sole
>   mechanism for CPU-side VRAM population and for the runtime Layer 2 enable.
>   Bits 0/2/3/4/6/7 of that port were all untested.
> - Stated that palette-RGB-based transparency was the mechanism but wrote tests
>   that could not distinguish an implementation that checks the **palette
>   index** from one that checks the **palette RGB output** — so the historical
>   "checked-on-wrong-field" bug class this subsystem has already hit once could
>   not have been caught by the test suite that allegedly passed.
> - Did not cover the NR 0x4A fallback colour interaction, the `layer2_en_qq`
>   one-pixel latency, or the clip auto-increment wrap to index 0 on the 5th
>   write.
> - Described several tests in prose ("stacking order correct", "pixel alignment
>   correct") without any mechanical expected value, making the checks
>   effectively `x == x`.
>
> This rewrite re-derives every expected value from an explicit VHDL citation
> (file + line). No expected value in this plan is justified by reference to the
> JNEXT C++ implementation. Test rows that are currently tautological in the
> corresponding test code (if any) must be removed before this plan is re-marked
> "passing"; see the Retractions section at the bottom.
>
> **Current status (2026-04-15): test code rewritten and merged to main. Honest
> pass rate: 89/95 live, 44 rows deferred to integration tier. The 6 failures are
> all driven by a single Task 3 emulator bug — `src/video/layer2.cpp:52-61`
> `compute_ram_addr()` omits the VHDL `+1` bank transform at `layer2.vhd:172`.
> Tests are the specification; leave failing until Task 3 fixes the emulator.**

Systematic compliance test plan for the Layer 2 bitmap display subsystem,
derived exclusively from the VHDL sources (`layer2.vhd` and the Layer 2 wiring
in `zxnext.vhd`). Tests verify that the JNEXT emulator matches the hardware
behaviour defined in the FPGA reference implementation.

## Purpose

Layer 2 is the ZX Spectrum Next's primary bitmap display layer. It supports
three resolutions, two pixel depths (8-bit and 4-bit), 9-bit hardware
scrolling with resolution-dependent wrap, dual palette selection with a
palette offset, a clip window, active/shadow bank selection, CPU-side VRAM
mapping through port 0x123B, and a transparency check that fires on the
palette RGB output. This plan covers all of those features with traceable
references to VHDL signal names and line-level behaviour.

## VHDL Oracle

### Source files

- `cores/zxnext/src/video/layer2.vhd` (216 lines) — Layer 2 pixel generation
- `cores/zxnext/src/zxnext.vhd` — NextREG decode, port 0x123B, palette lookup,
  transparency check, compositor

Paths are relative to the FPGA worktree
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.

### NextREG map

| NextREG | VHDL signal | Function | Citation |
|---------|-------------|----------|----------|
| 0x12 | `nr_12_layer2_active_bank` (7 bits) | Active L2 base bank | zxnext.vhd:1135, 5220 |
| 0x13 | `nr_13_layer2_shadow_bank` (7 bits) | Shadow L2 base bank (CPU-map only) | zxnext.vhd:1136, 5223 |
| 0x14 | `nr_14_global_transparent_rgb` | Global transparent colour (RRRGGGBB) | zxnext.vhd:1137, 5226 |
| 0x15[4:2] | `nr_15_layer_priority` | SLU priority mode (3 bits) | zxnext.vhd:1141, 5232 |
| 0x16 | `nr_16_layer2_scrollx` | Scroll X low 8 bits | zxnext.vhd:1144, 5237 |
| 0x17 | `nr_17_layer2_scrolly` | Scroll Y (8 bits) | zxnext.vhd:1145, 5240 |
| 0x18 | `nr_18_layer2_clip_x1/x2/y1/y2` | Clip register, auto-indexed | zxnext.vhd:1146-1150, 5243 |
| 0x1C[0] | — | Reset L2 clip index to 0 | zxnext.vhd:5278-5281 |
| 0x43[2] | `nr_43_active_layer2_palette` | L2 palette select (0 or 1) | zxnext.vhd:1187, 5392 |
| 0x4A | `nr_4a_fallback_rgb` | Fallback colour when all layers transparent | zxnext.vhd:1191, 5014, 5407 |
| 0x69[7] | via `nr_69_we` | Writes `port_123b_layer2_en` | zxnext.vhd:3924-3925 |
| 0x70[5:4] | `nr_70_layer2_resolution` | 00=256x192, 01=320x256, 1X=640x256 | zxnext.vhd:1213, 5476 |
| 0x70[3:0] | `nr_70_layer2_palette_offset` | 4-bit palette offset | zxnext.vhd:1214, 5477 |
| 0x71[0] | `nr_71_layer2_scrollx_msb` | Scroll X bit 8 | zxnext.vhd:1215, 5480 |

### Port 0x123B bit map

From zxnext.vhd:3914-3922 and the read-back formatter at zxnext.vhd:3933:

| Bit | Signal | Function |
|-----|--------|----------|
| 0 | `port_123b_layer2_map_wr_en` | Enable L2 bank mapping for CPU writes |
| 1 | `port_123b_layer2_en` | Layer 2 display enable |
| 2 | `port_123b_layer2_map_rd_en` | Enable L2 bank mapping for CPU reads |
| 3 | `port_123b_layer2_map_shadow` | 0=active (NR 0x12), 1=shadow (NR 0x13) mapped |
| 4 (set in write path) | `port_123b_layer2_offset` | When bit 4 set, bits 2:0 latch as offset |
| 7:6 | `port_123b_layer2_map_segment` | Memory segment, 11 = use CPU A15:A14 |

Port read returns `segment & "00" & shadow & rd_en & en & wr_en`
(zxnext.vhd:3933). Note that bit 4 is **not** reflected in the read-back —
it is a write-only control line selecting which field the low 3 bits update.

### Reset defaults

Reset block at zxnext.vhd:4943-5050 and 3908-3913:

| Field | Value | Citation |
|-------|-------|----------|
| `nr_12_layer2_active_bank` | `"0001000"` (bank 8) | zxnext.vhd:4943 |
| `nr_13_layer2_shadow_bank` | `"0001011"` (bank 11) | zxnext.vhd:4944 |
| `nr_14_global_transparent_rgb` | `0xE3` | zxnext.vhd:4946 |
| `nr_15_layer_priority` | `"000"` (SLU) | zxnext.vhd:4951 |
| `nr_16_layer2_scrollx` | `0x00` | zxnext.vhd:4955 |
| `nr_17_layer2_scrolly` | `0x00` | zxnext.vhd:4957 |
| `nr_18_layer2_clip_x1` | `0x00` | zxnext.vhd:4959 |
| `nr_18_layer2_clip_x2` | `0xFF` | zxnext.vhd:4960 |
| `nr_18_layer2_clip_y1` | `0x00` | zxnext.vhd:4961 |
| `nr_18_layer2_clip_y2` | `0xBF` | zxnext.vhd:4962 |
| `nr_18_layer2_clip_idx` | `"00"` | zxnext.vhd:4963 |
| `nr_43_active_layer2_palette` | `'0'` | zxnext.vhd:5007 |
| `nr_4a_fallback_rgb` | `0xE3` | zxnext.vhd:5014 |
| `nr_70_layer2_resolution` | `"00"` | zxnext.vhd:5047 |
| `nr_70_layer2_palette_offset` | `"0000"` | zxnext.vhd:5048 |
| `nr_71_layer2_scrollx_msb` | `'0'` | zxnext.vhd:5050 |
| `port_123b_layer2_en` | `'0'` | zxnext.vhd:3908 |
| `port_123b_layer2_map_wr_en` | `'0'` | zxnext.vhd:3909 |
| `port_123b_layer2_map_rd_en` | `'0'` | zxnext.vhd:3910 |
| `port_123b_layer2_map_shadow` | `'0'` | zxnext.vhd:3911 |
| `port_123b_layer2_map_segment` | `"00"` | zxnext.vhd:3912 |
| `port_123b_layer2_offset` | `"000"` | zxnext.vhd:3913 |

Note that on a cold reset Layer 2 is **disabled**, the clip covers the full
default 256x192 area (`(0,0)-(255,191)`), and the default transparent colour
equals the default fallback colour, both `0xE3`.

### Key VHDL identities

These are the exact expressions reproduced from layer2.vhd and zxnext.vhd;
expected-value columns further down reference them by short name.

- **Wide flag** (layer2.vhd:145): `layer2_wide_res = '0'` iff resolution `"00"`.
- **Lookahead** (layer2.vhd:147-150): `hc_eff = hc + 1`, where `hc` is `i_phc`
  (narrow) or `i_whc` (wide).
- **Scroll X** (layer2.vhd:152-154):
  `x_pre = hc_eff + scroll_x` (10-bit);
  for wide res, if `x_pre >= 320` (decoded as `x_pre(9)='1' OR
  (x_pre(8)='1' AND x_pre(7:6) /= "00")`) then `x(8:6) := x_pre(8:6) + 3`;
  else `x(8:6) := x_pre(8:6)`. Low 6 bits pass through.
- **Scroll Y** (layer2.vhd:156-158):
  `y_pre = vc_eff + scroll_y`;
  for narrow res only, if `y_pre(8)='0' AND y_pre(7:6)="11"` (>= 192 and <256)
  then `y(7:6) := y_pre(7:6) + 1`; else `y(7:6) := y_pre(7:6)`. Low 6 bits
  pass through.
- **Address** (layer2.vhd:160):
  narrow: `layer2_addr = '0' & y(7:0) & x(7:0)` (row-major, 256 bytes/row);
  wide: `layer2_addr = x(8:0) & y(7:0)` (column-major, 256 bytes/column).
- **hc_valid** (layer2.vhd:164): narrow valid iff `hc_eff(8)='0'`; wide valid
  iff `hc_eff(8)='0' OR hc_eff(7:6)="00"` (i.e. 0..319 or 384..511 mod 512,
  covering the 320-col window).
- **vc_valid** (layer2.vhd:165): wide valid iff `vc_eff(8)='0'` (0..255);
  narrow valid iff `vc_eff(8)='0' AND vc_eff(7:6)/="11"` (0..191).
- **Clip compare** (layer2.vhd:167): `layer2_clip_en = 1` iff
  `hc_eff ∈ [clip_x1_q, clip_x2_q]` **and** `vc_eff ∈ [clip_y1_q, clip_y2_q]`
  **and** `hc_valid` **and** `vc_valid`. Clip comparisons are inclusive.
- **Clip X latching** (layer2.vhd:129-135): narrow latches `clip_x1_q = '0'&x1`
  and `clip_x2_q = '0'&x2`; wide latches `clip_x1_q = x1&'0'` (left-shift
  with 0 in the LSB) and `clip_x2_q = x2&'1'` (left-shift with 1 in the LSB).
  So in wide mode an odd number of columns at each clip edge is impossible —
  the clip always starts on an even column and ends on an odd column.
- **Bank** (layer2.vhd:172-173):
  `layer2_bank_eff = (('0' & active_bank(6:4)) + 1) & active_bank(3:0)`
  (8-bit);
  `layer2_addr_eff = (layer2_bank_eff + ("00000" & addr(16:14))) & addr(13:0)`.
  The `+ 1` on the top 3 bits is the key transform: NR 0x12 is numbered in
  16K "layer-2 banks" while `layer2_bank_eff` is the physical SRAM 16K page.
  With default `active_bank = "0001000"` (bank 8), `layer2_bank_eff` =
  `(000+1) & 1000` = `"00011000"` = 24. Since `layer2_sram_addr` is
  bits 20:0 of `layer2_addr_eff` and bit 21 must be 0 for visibility, the
  maximum legal `active_bank(6:4)` is `110` (anything with the top bit set
  lands at bit 21 and kills the pixel).
- **Out-of-range guard** (layer2.vhd:175):
  `layer2_en = '1'` iff `layer2_en_q = '1' AND layer2_clip_en = '1' AND
  layer2_addr_eff(21) = '0'`.
- **One-pixel latency** (layer2.vhd:197-198): `layer2_en_qq` and
  `layer2_hires_qq` lag `layer2_en` / `layer2_resolution_q(1)` by one `CLK_7`.
- **4-bit unpack** (layer2.vhd:202): in hires mode (`layer2_hires_qq='1'`) the
  high nibble of the byte is the left pixel (`i_sc(1)='0'`) and the low nibble
  is the right pixel (`i_sc(1)='1'`). The effective pixel value is
  `"0000" & nibble` — only the low 4 bits of the 8-bit pixel index can be
  nonzero before the palette offset is added.
- **Palette offset** (layer2.vhd:203):
  `layer2_pixel = (pixel_pre(7:4) + palette_offset_q) & pixel_pre(3:0)`. The
  4-bit add wraps: offset 15 + high nibble 1 → high nibble 0.
- **Palette lookup address** (zxnext.vhd:6827, 4229):
  `layer2_pixel_en = layer2_en_qq` from layer2.vhd, and the RAM lookup is
  indexed by `{is_sprite=0, layer2_palette_select, layer2_pixel(7:0)}`, so
  NR 0x43[2] picks one of two 256-entry L2 palettes.
- **Transparency** (zxnext.vhd:7121):
  `layer2_transparent = '1' when (layer2_rgb_2(8 downto 1) = transparent_rgb_2)
  or (layer2_pixel_en_2 = '0')`. **Critical:** the comparison is on
  `layer2_rgb_2(8:1)` — the top 8 bits of the 9-bit palette RGB output — not
  on the 8-bit palette index. `transparent_rgb_2` is a pipelined copy of
  `nr_14_global_transparent_rgb` (zxnext.vhd:7078, 7121).
- **Fallback** (zxnext.vhd:6823, 5014): `fallback_rgb_0 <= nr_4a_fallback_rgb`;
  reset value `0xE3`. The fallback is emitted by the compositor when every
  layer comes back transparent.
- **Priority promotion bit** (zxnext.vhd:7050):
  `layer2_priority_2 <= layer2_prgb_1(9)` — i.e. bit 9 of the 10-bit palette
  word flags "priority L2 pixel" and is gated off when the pixel itself is
  transparent (zxnext.vhd:7123).
- **NR 0x69 routing** (zxnext.vhd:3924-3925): a write to NR 0x69 sets
  `port_123b_layer2_en <= nr_wr_dat(7)`. This gives two independent paths to
  toggle Layer 2 visibility, both landing on the same flop.
- **CPU-map bank source** (zxnext.vhd:2968):
  `layer2_active_bank <= nr_12_layer2_active_bank when
  port_123b_layer2_map_shadow='0' else nr_13_layer2_shadow_bank`. Display
  generation always uses NR 0x12 (layer2.vhd port `i_layer2_active_bank` is
  wired directly to `nr_12_layer2_active_bank` at zxnext.vhd:4223); the
  shadow bank affects **only** CPU-side mapping.

## Test Architecture

### Harness

Layer 2 tests are Z80 programs loaded via the NEX loader, executed headlessly,
and compared against reference screenshots. Each test:

1. Disables the ULA display (NR 0x68 bit 7 = 0) and sprites (NR 0x15 bit 0 = 0)
   unless the test is specifically about layer interaction.
2. Sets the NR / port 0x123B values under test.
3. Maps Layer 2 VRAM through port 0x123B and writes a deterministic pattern.
4. Enables Layer 2 (via port 0x123B bit 1 **or** NR 0x69 bit 7 depending on
   the test).
5. Halts; the emulator screenshots after the frame has stabilised.

Every expected value below must be realisable as either a pixel value in a
reference screenshot, a NextREG read-back through port 0x253B, or a memory
read-back through the mapped Layer 2 segment.

### File layout

```
test/
  layer2/
    test_l2_<group>_<id>.asm
  layer2-references/
    test_l2_<group>_<id>.png
```

### Column conventions

All tables below use:

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |

- **Preconditions** lists every non-default NR/port written before stimulus;
  anything not listed is at reset default.
- **Stimulus** is the single action under test.
- **Expected** is a mechanical, checkable output (pixel value, read-back byte,
  address, or RGB).
- **VHDL cite** references the line the expected value comes from.

## Test Case Catalog

### Group 1 — Reset defaults (read-back only)

No VRAM or stimulus needed; just reset the machine and read NRs back through
port 0x253B / NR 0x18-style auto-indexed reads.

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G1-01 | NR 0x12 default | cold reset | read NR 0x12 | `0x08` | zxnext.vhd:4943 |
| G1-02 | NR 0x13 default | cold reset | read NR 0x13 | `0x0B` | zxnext.vhd:4944 |
| G1-03 | NR 0x14 default | cold reset | read NR 0x14 | `0xE3` | zxnext.vhd:4946 |
| G1-04 | NR 0x16 default | cold reset | read NR 0x16 | `0x00` | zxnext.vhd:4955 |
| G1-05 | NR 0x17 default | cold reset | read NR 0x17 | `0x00` | zxnext.vhd:4957 |
| G1-06 | NR 0x18 defaults | cold reset | 4 × read NR 0x18 | `0x00,0xFF,0x00,0xBF` | zxnext.vhd:4959-4962 |
| G1-07 | NR 0x43[2] default | cold reset | read NR 0x43, mask 0x04 | `0` | zxnext.vhd:5007 |
| G1-08 | NR 0x4A default | cold reset | read NR 0x4A | `0xE3` | zxnext.vhd:5014 |
| G1-09 | NR 0x70 default | cold reset | read NR 0x70 | `0x00` | zxnext.vhd:5047-5048 |
| G1-10 | NR 0x71[0] default | cold reset | read NR 0x71, mask 0x01 | `0` | zxnext.vhd:5050 |
| G1-11 | port 0x123B default | cold reset | `IN A,(0x123B)` | low 4 bits all 0, bits 7:4 = 0 | zxnext.vhd:3908-3913, 3933 |
| G1-12 | Layer 2 off after reset | cold reset | enable ULA with known colour, screenshot | full screen = ULA colour, no L2 | zxnext.vhd:3908, layer2.vhd:175 |

### Group 2 — Resolution modes and address generation

**VHDL basis:** layer2.vhd:145-160. Every test in this group fills VRAM with
a pattern chosen so that the **byte written** and the **pixel displayed at a
given screen coordinate** uniquely identify the address formula.

Pattern A (narrow, 256x192): write byte `(y XOR x)` at offset `y*256 + x` for
0 ≤ y < 192, 0 ≤ x < 256.
Pattern B (wide 8-bit, 320x256): write byte `(y XOR x)` at offset `x*256 + y`
for 0 ≤ x < 320, 0 ≤ y < 256.
Pattern C (wide 4-bit, 640x256): write high nibble = `x & 0x0F`, low nibble =
`(x+1) & 0x0F` at offset `(x/2)*256 + y`.

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G2-01 | 256x192 row-major address | NR 0x70=0x00, L2 enabled, Pattern A, palette identity | sample pixels (0,0),(1,0),(0,1),(255,191) | palette indices `0,1,1,254` (= y XOR x) | layer2.vhd:160 narrow |
| G2-02 | 256x192 row pitch = 256 | as G2-01 | sample pixel (0,1) vs (0,0) | byte offset differs by exactly 256 (decoded as colour 1) | layer2.vhd:160 |
| G2-03 | 256x192 y≥192 invisible | NR 0x70=0x00, L2 enabled, VRAM filled with 0xFF for y≥192 only, elsewhere 0x00 | screenshot | ULA fallback shows through in the non-existent area (no L2 pixel promoted there) | layer2.vhd:165 vc_valid narrow |
| G2-04 | 256x192 x wraparound at 256 is impossible (no stimulus route) | n/a | document | narrow `hc_valid` already kills x≥256; no test | layer2.vhd:164 |
| G2-05 | 320x256 column-major address | NR 0x70=0x10, NR 0x14=0x00, L2 enabled, Pattern B, palette identity | sample (0,0),(0,1),(1,0),(319,255) | palette indices `0,1,1,254` | layer2.vhd:160 wide |
| G2-06 | 320x256 column pitch = 256 | as G2-05 | (1,0) − (0,0) address delta | 256 bytes | layer2.vhd:160 |
| G2-07 | 320x256 x in [320,383] invisible | NR 0x70=0x10, L2 enabled, VRAM at notional `x∈[320,383]` written 0xAA | screenshot | those columns are not rasterised (off-screen) | layer2.vhd:164 wide `hc_eff(7:6)="00"` split |
| G2-08 | 320x256 y=255 visible | NR 0x70=0x10, L2 enabled, bottom row 0xC3 | sample (0,255) | palette index 0xC3 rendered | layer2.vhd:165 wide |
| G2-09 | 640x256 high nibble = left pixel | NR 0x70=0x20, L2 enabled, first byte of col 0 = 0x5A | sample (0,0),(1,0) | indices `0x05,0x0A` (high then low nibble) | layer2.vhd:202 |
| G2-10 | 640x256 only 4-bit index pre-offset | NR 0x70=0x20, offset=0, palette index >=16 should be impossible without offset | inspect full image histogram | only palette entries 0..15 appear | layer2.vhd:202-203 |
| G2-11 | 640x256 shares 320 column layout | NR 0x70=0x20, L2 enabled, Pattern C, palette identity | (2,0),(3,0) derive from byte at `(1*256+0)` | bytes at `(x/2)*256 + y` | layer2.vhd:160 wide |
| G2-12 | Lookahead one pixel | NR 0x70=0x00, L2 enabled, column 0 = `0xAA`, column 1 = `0x55`, all others 0x00 | sample visible col 0 | `0x55` (generator reads x=1 when rasterising x=0) | layer2.vhd:148 `hc_eff = hc + 1` |

Note on G2-12: the VHDL generates the address one pixel ahead. JNEXT's
renderer may pre-fetch differently, but the on-screen position of a given
byte must match the VHDL identity. If the emulator samples "in phase" the
expected pixel at the leftmost visible column is still the byte that the
VHDL address formula labels as column 1 of that row, because `hc_eff = hc+1`
is part of the address computation itself (not a pipeline artefact of the
display side). This test is the only one that can detect an off-by-one in
the address pipeline.

### Group 3 — Scrolling

**VHDL basis:** layer2.vhd:152-158. All wrap arithmetic below is the literal
VHDL.

Pattern D: fill the left half with `0x11` and the right half with `0x22` (for
X-scroll tests). Pattern E: top half `0x11`, bottom half `0x22` (for Y-scroll).
Assume palette identity so index = rendered colour byte.

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G3-01 | 256x192 scroll X=128 | NR 0x70=0x00, Pattern D, NR 0x16=128, NR 0x71=0x00 | screenshot | col 0 = `0x22`, col 127 = `0x22`, col 128 = `0x11`, col 255 = `0x11` | layer2.vhd:152-154 narrow |
| G3-02 | 256x192 scroll X=255 | NR 0x70=0x00, Pattern D, NR 0x16=255 | col 0 | right-half byte `0x22` (single-pixel shift of the left half off the right edge) | layer2.vhd:152 |
| G3-03 | 256x192 scroll Y wrap from 192 | NR 0x70=0x00, Pattern E, NR 0x17=191 | row 0 | `y_pre = 191+0 = 191`; `y_pre(7:6)="10"`, not "11", so no +1; row 0 reads source row 191 = `0x22` | layer2.vhd:156-158 narrow |
| G3-04 | 256x192 scroll Y wrap from 193 | NR 0x70=0x00, Pattern E, NR 0x17=193 | row 0 | `y_pre = 193`, `y_pre(7:6)="11"` → y(7:6)+1 = "00", so y=1 → row 1 of source = `0x11` | layer2.vhd:157 |
| G3-05 | 256x192 scroll Y=96 | NR 0x70=0x00, Pattern E, NR 0x17=96 | row 0 vs row 95 | row 0 sources y=96 (`0x22`); row 95 sources y=191 (`0x22`); row 96 sources y=0 (`0x11`) via the +1 wrap branch | layer2.vhd:157 |
| G3-06 | Scroll X MSB (nr_71[0]) in 256 mode | NR 0x70=0x00, Pattern D, NR 0x16=0, NR 0x71=0x01 | col 0 | scroll_x = 256, `hc_eff + 256` → x(8)=1 but addr takes only x(7:0); visible col 0 = source col 0 (`0x11`) — **same as no scroll**, because narrow addr uses `x(7:0)` only | layer2.vhd:160 narrow |
| G3-07 | 320x256 scroll X=160 | NR 0x70=0x10, Pattern D (wide, left/right at x=160), NR 0x16=160 | col 0 | `0x22` (right half); col 159 = `0x22`; col 160 = `0x11` | layer2.vhd:152-154 wide |
| G3-08 | 320x256 scroll X=319 | NR 0x70=0x10, Pattern D wide, NR 0x16=63, NR 0x71=0x01 (scroll_x=319) | col 0 | source col 319 | layer2.vhd:152-154 |
| G3-09 | 320x256 scroll X wrap arithmetic | NR 0x70=0x10, Pattern D wide, NR 0x16=200, NR 0x71=0x00 | `hc_eff=120` | `x_pre = 120+200 = 320 = 10'b0101000000`; wide=1, inner condition `x_pre(9)='0' AND (x_pre(8)='0' OR x_pre(7:6)="00")` is false (x_pre(8)='1', x_pre(7:6)="01"), so wrap branch fires: `x(8:6) = "101" + "011" = "1000"` truncated to 3 bits = `"000"`; `x(5:0) = "000000"`; x=0 → source col 0 = **left half `0x11`** | layer2.vhd:153 |
| G3-10 | 320x256 scroll Y=128 | NR 0x70=0x10, Pattern E wide (top/bot at y=128), NR 0x17=128 | row 0 | source row 128 = `0x22`; row 128 wraps to row 0 = `0x11` (natural 8-bit wrap, wide branch does not add +1) | layer2.vhd:157 wide |
| G3-11 | 640x256 scroll X=160 byte-level | NR 0x70=0x20, Pattern C but with left/right halves of nibbles, NR 0x16=160 | (0,0) | nibble at byte `(x+80).y=0` — verify byte-granularity byte-lane scroll | layer2.vhd:152-154, 202 |
| G3-12 | Negative path: 320x256 scroll X wrap branch skipped when x_pre<320 | NR 0x70=0x10, Pattern D wide, NR 0x16=100 | col 0 | `x_pre=100`, wrap branch does NOT fire, x=100 → source col 100 (left half, `0x11`) | layer2.vhd:153 |

### Group 4 — Clip window

**VHDL basis:** layer2.vhd:129-135 (X latch rule), 137-138 (Y raw), 164-167
(compare), 5243-5280 (auto-index and NR 0x1C reset).

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G4-01a | Auto-index advances — slot 0 observable | cold reset | write `0x11` to NR 0x18 (idx 0→1); write NR 0x1C=0x01 (idx→0); read NR 0x18 once | `0x11` (slot 0 = x1 holds the value written at idx 0) | zxnext.vhd:5243-5249, 5278-5281, 5948-5952 |
| G4-01b | Auto-index advances — slot 1 observable | cold reset | write `0x11,0x22` to NR 0x18 (idx 0→1→2); write NR 0x1C=0x01 (idx→0); write dummy `0x00` (idx 0→1); read NR 0x18 once | `0x22` (slot 1 = x2 was set by the second write at idx 1; dummy write advanced idx back to 1) | zxnext.vhd:5243-5249 |
| G4-01c | Auto-index advances — slot 2 observable | cold reset | write `0x11,0x22,0x33` to NR 0x18 (idx ends at 3); write NR 0x1C=0x01 (idx→0); write dummy `0x00,0x00` (idx 0→1→2); read NR 0x18 once | `0x33` (slot 2 = y1 holds the value from the third original write) | zxnext.vhd:5243-5249 |
| G4-01d | Auto-index advances — slot 3 observable and wraps | cold reset | write `0x11,0x22,0x33,0x44` to NR 0x18 (idx 0→1→2→3→0 wraps); write NR 0x1C=0x01 (idx→0); write dummy `0x00,0x00,0x00` (idx→3); read NR 0x18 once | `0x44` (slot 3 = y2, confirms both that `idx+1` was a 2-bit wrap and that the fourth write landed at idx 3) | zxnext.vhd:5243-5249 |
| G4-02 | Auto-index wraps at 4 | cold reset | write `0x11,0x22,0x33,0x44,0x55` to NR 0x18; read back x1 | `0x55` (5th write landed in slot 0 again) | zxnext.vhd:5249 (`idx + 1`), 2-bit rollover |
| G4-03 | NR 0x1C[0] resets L2 clip index | after G4-02 (idx now at 1) | write NR 0x1C = 0x01; read NR 0x18 once | `0x55` (index was reset to 0, first read returns x1) | zxnext.vhd:5278-5281 |
| G4-04 | NR 0x1C[0]=0 leaves L2 index alone | cold reset; seed slots: write `0x10,0x20,0x30,0x40` to NR 0x18 then `NR 0x1C=0x01`; then write `0x11,0x22` to NR 0x18 so L2 idx=2 (slots x1=0x11, x2=0x22, y1=0x30, y2=0x40) | write NR 0x1C=0x02 (sprite-only reset, bit0=0); write `0xAA` to NR 0x18; then `NR 0x1C=0x01` followed by two dummy writes `0x00,0x00` to step idx to 2; read NR 0x18 once | `0xAA` — the `0xAA` write landed in slot 2 (y1), proving L2 idx was NOT reset by NR 0x1C=0x02 and was still at 2 when the write happened. If bit0 of NR 0x1C had (incorrectly) reset L2 idx, the `0xAA` would have landed in slot 0 and the read would return `0x30` (the y1 left over from seeding). | zxnext.vhd:5278-5281 (four independent `if` guards; only bit0 touches L2 idx) |
| G4-05 | 256x192 default clip covers full area | reset, L2 enabled, fill with `0x5A` | screenshot | entire 256x192 area = palette[0x5A]; pixel at (0,0) and (255,191) both visible | layer2.vhd:167, zxnext.vhd:4959-4962 |
| G4-06 | 256x192 clip to centre 64x64 | reset, L2 enabled, fill `0x5A`, NR 0x18 writes `96,159,64,127` | screenshot | pixels inside `[96..159]×[64..127]` = L2, outside = ULA/fallback | layer2.vhd:167 (inclusive) |
| G4-07 | 256x192 clip x1==x2 single column | reset, L2 enabled, fill `0x5A`, clip `100,100,0,191` | screenshot | exactly one vertical column (x=100) is L2 | layer2.vhd:167 inclusive |
| G4-08 | 256x192 clip x1 > x2 → empty | reset, L2 enabled, fill `0x5A`, clip `100,50,0,191` | screenshot | no L2 pixels anywhere (compare `hc_eff >= clip_x1 AND hc_eff <= clip_x2` both false) | layer2.vhd:167 |
| G4-09 | 320x256 clip X is doubled | NR 0x70=0x10, L2 enabled, fill `0x5A`, clip x1=50, x2=99 | screenshot | visible L2 is columns 100..199 inclusive (effective `clip_x1_q = 100`, `clip_x2_q = 199`) | layer2.vhd:133-134 (`x1&'0'`, `x2&'1'`) |
| G4-10 | 320x256 clip Y is not doubled | NR 0x70=0x10, L2 enabled, fill `0x5A`, clip y1=50, y2=99 | screenshot | visible L2 rows = 50..99 inclusive | layer2.vhd:137-138 |
| G4-11 | 320x256 clip `x1=0,x2=0` gives 2-pixel-wide strip | NR 0x70=0x10, fill `0x5A`, clip `0,0,0,255` | screenshot | visible L2 columns = {0,1} only (because `clip_x1_q=0`, `clip_x2_q=1`) | layer2.vhd:133-134 |
| G4-12 | 640x256 clip uses same doubling as 320 | NR 0x70=0x20, fill nibble pattern, clip x1=10, x2=19 | screenshot | L2 visible columns 20..39 inclusive | layer2.vhd:133-134 (applies whenever `i_resolution /= "00"`) |
| G4-13 | Clip is inclusive on both edges | NR 0x70=0x00, L2 enabled, fill `0x5A`, clip `10,20,30,40` | sample (10,30) and (20,40) | both are L2 visible; (9,30) and (21,30) are not | layer2.vhd:167 `>=` / `<=` |

### Group 5 — Palette offset, selection, and 4-bit mode

**VHDL basis:** layer2.vhd:203 (offset add), zxnext.vhd:6827 (palette select),
layer2.vhd:202 (nibble unpack).

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G5-01 | Offset 0 identity | NR 0x70[3:0]=0, L2 enabled, palette[0x00]=blue, palette[0x10]=red, VRAM byte=0x00 | sample a pixel | blue | layer2.vhd:203 |
| G5-02 | Offset 1 shifts high nibble | NR 0x70[3:0]=1, same palette, VRAM byte=0x00 | sample | red (palette[0x10]) | layer2.vhd:203 |
| G5-03 | Offset 15, high nibble 0 | NR 0x70[3:0]=15, palette[0xF5]=green, VRAM byte=0x05 | sample | green | layer2.vhd:203 |
| G5-04 | Offset 15, high nibble 1 → wraps to 0 | NR 0x70[3:0]=15, palette[0x05]=yellow, VRAM byte=0x15 | sample | yellow (0x1+0xF=0x0 in 4 bits, then low nibble 0x5) | layer2.vhd:203 4-bit add |
| G5-05 | 4-bit mode high nibble is pre-offset zero | NR 0x70=0x20, offset=0, palette[0x05]=green, byte=0x50 | left pixel | green (high nibble → pixel `0x05`) | layer2.vhd:202-203 |
| G5-06 | 4-bit mode offset shifts into upper nibble | NR 0x70=0x23 (offset=3), palette[0x35]=cyan, byte=0x50 | left pixel | cyan (pixel_pre=0x05, +0x30 = 0x35) | layer2.vhd:202-203 |
| G5-07 | 4-bit mode low nibble is right pixel | NR 0x70=0x20, offset=0, palette[0x0A]=magenta, byte=0x5A | right pixel (x=1) | magenta | layer2.vhd:202 `i_sc(1)='1'` branch |
| G5-08 | Palette 0 vs Palette 1 | NR 0x43[2]=0, palette0[0x40]=red, palette1[0x40]=blue, VRAM byte=0x40, L2 enabled | screenshot | red; then flip NR 0x43[2]=1 → blue | zxnext.vhd:6827, 5392 |
| G5-09 | Palette select does not affect sprite/ula palette | as G5-08 + ULA showing palette[0x40] | toggle NR 0x43[2] | ULA colour unchanged; L2 colour toggles | zxnext.vhd:6827 (is_sprite=0 only for L2) |

### Group 6 — Transparency (critical — historical bug class)

**VHDL basis:** zxnext.vhd:7121. The comparison is on
`layer2_rgb_2(8:1)`, i.e. the palette RGB output top 8 bits, not on the
palette index. Tests G6-01 and G6-02 together form the oracle that
distinguishes a correct implementation from the historical
"compare on palette index" bug class.

For these tests "palette identity" means palette entry N has 9-bit RGB
`"0" & N` (top 8 bits = N, with bit 0 always 0). Under palette identity,
pixel index and palette RGB top 8 bits coincide, so they cannot be used to
distinguish the two bug forms — the distinguishing cases explicitly break
identity.

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G6-01 | Index ≠ 0xE3, RGB = 0xE3 → transparent (would catch "index check" bug) | NR 0x14=0xE3, L2 palette: palette[0x40] has top-8-bit RGB 0xE3; palette[0xE3] has RGB 0x00; fill VRAM with 0x40 | screenshot | L2 pixel is transparent — ULA / fallback 0xE3 shows through | zxnext.vhd:7121 |
| G6-02 | Index = 0xE3, RGB ≠ 0xE3 → opaque (would catch "index check" bug) | NR 0x14=0xE3, L2 palette[0xE3]=RGB 0x40, fill VRAM with 0xE3 | screenshot | L2 pixel visible with palette colour 0x40 | zxnext.vhd:7121 |
| G6-03 | Identity palette, default NR 0x14 | reset, palette identity, VRAM=0xE3 | screenshot | transparent (both interpretations agree here, but confirms identity path) | zxnext.vhd:7121, 4946 |
| G6-04 | Change NR 0x14 to 0x00 | palette identity, VRAM=0xE3 | write NR 0x14=0x00; screenshot | 0xE3 now opaque; 0x00 pixels now transparent | zxnext.vhd:5226, 7121 |
| G6-05 | Clip outside ⇒ transparent regardless of colour | L2 enabled, fill `0x5A` (opaque colour), clip `0,0,0,0` | sample (100,100) | transparent (`layer2_pixel_en_2 = 0`) | layer2.vhd:167, 175; zxnext.vhd:7121 |
| G6-06 | L2 disabled ⇒ all transparent | reset, fill VRAM, do NOT enable L2 | screenshot | ULA / fallback only | layer2.vhd:175 `layer2_en_q='0'` ⇒ `layer2_en=0` |
| G6-07 | Fallback 0xE3 visible when every layer transparent | reset, all layers disabled, NR 0x4A=0xE3 | screenshot | uniform 0xE3 | zxnext.vhd:5014, 6823 |
| G6-08 | Fallback colour follows NR 0x4A write | reset, all layers disabled, write NR 0x4A=0x1C | screenshot | uniform 0x1C | zxnext.vhd:5407 |
| G6-09 | Priority bit gated by transparency | NR 0x15[4:2]=000, palette[0x80] has priority bit 9=1 and top-8 RGB = 0xE3 (transparent) | place 0x80 pixel over a sprite | sprite shows (priority ignored, zxnext.vhd:7123) | zxnext.vhd:7121-7123 |

### Group 7 — Bank selection and port 0x123B mapping

**VHDL basis:** layer2.vhd:172-175, zxnext.vhd:2966-2968, 3908-3933.

For these tests, VRAM is populated by mapping the L2 bank into a CPU segment
through port 0x123B and writing bytes to that segment. The display side
always sources from NR 0x12 regardless of the `shadow` flag.

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G7-01 | Bank `+1` transform on default bank | reset; NR 0x12=0x08 (default) | enable L2, write known byte at L2 offset 0 via CPU-map; screenshot | display sources from SRAM 16K page 24 (0x60000) — `(0+1)&1000 = "00011000"` | layer2.vhd:172 |
| G7-02 | Bank `+1` transform, nonzero high 3 bits | NR 0x12 = `"0011000"` = 0x18 | enable L2, write pattern | `layer2_bank_eff = (001+1)&1000 = "00101000"` = 40; display sources from SRAM page 40 | layer2.vhd:172 |
| G7-03 | Bank `+1` transform, max legal | NR 0x12 = `"1101000"` = 0x68 | enable L2, write pattern | `layer2_bank_eff = (110+1)&1000 = "01111000"` = 120; visible (bit 21 = 0) | layer2.vhd:172-175 |
| G7-04 | Out-of-range bank → no pixel | NR 0x12 = `"1111000"` | enable L2, fill VRAM, screenshot | no L2 pixel anywhere: `(111+1)&1000 = "10001000"` → addr bit 21 = 1 → `layer2_en=0` | layer2.vhd:173-175 |
| G7-05 | Address bits 16:14 select 16K page within 48K | NR 0x70=0x00, NR 0x12=0x08, map segment 0 with offset 0,1,2 and write distinct bytes; screenshot | rows 0..63 come from page 24, rows 64..127 from page 25, rows 128..191 from page 26 | layer2.vhd:173 |
| G7-06 | 320x256 uses 5 pages | NR 0x70=0x10, NR 0x12=0x08, fill all 5 pages with distinct marker bytes | screenshot | all 5 column-major stripes appear in the expected screen regions | layer2.vhd:160 wide + 173 |
| G7-07 | Port 0x123B bit 0 enables CPU writes | reset, port 0x123B ← 0x01 (bit0=1, bit2=0) | write byte to segment 0 (0x0000-0x3FFF), then set bit0=0 and bit2=1 to read | readback = written byte; display not enabled | zxnext.vhd:3917, 3025 |
| G7-08 | Port 0x123B bit 2 enables CPU reads | after G7-07 setup | toggle bit2=1 with known VRAM | read returns stored L2 byte, not regular memory | zxnext.vhd:3918, 3025 |
| G7-09 | Port 0x123B bit 1 enables display | port 0x123B ← 0x02 (bit1=1) | screenshot | L2 visible | zxnext.vhd:3916, 4211 |
| G7-10 | Port 0x123B bit 1 and NR 0x69 bit 7 target same flop | port 0x123B ← 0x02 then NR 0x69 ← 0x00 | screenshot | L2 disabled (NR 0x69 write clobbered the enable) | zxnext.vhd:3924-3925 |
| G7-11 | Port 0x123B bit 3 selects shadow bank for mapping only | NR 0x12=0x08, NR 0x13=0x0B, port 0x123B ← 0x09 (bit0=1, bit3=1) | write marker byte into shadow (bank 11), disable bit3 and display | display shows bank 8 content, not the marker (proves display ignores shadow) | zxnext.vhd:2968, 4223 |
| G7-12 | Shadow bank data becomes visible after NR 0x12 rewrite | continuing G7-11 | set NR 0x12 ← 0x0B | display now shows the marker | layer2.vhd:172 (bank is flopped on `CLK_7`) |
| G7-13 | Port 0x123B bits 7:6 select segment | port 0x123B ← 0x41 (segment=01, wr_en) | write to 0x4000 | data lands in L2 page `+1` | zxnext.vhd:2966-2967, 3920 |
| G7-14 | Port 0x123B segment=11 ⇒ A15:A14 selects page | port 0x123B ← 0xC1 (segment=11, wr_en) | write to 0x0000 and 0xC000 | 0x0000 → page offset 0, 0xC000 → page offset 3 | zxnext.vhd:2966 |
| G7-15 | Port 0x123B bit 4 (offset latch) | port 0x123B ← 0x11 (bit4=1, bits2:0=001) | write to 0x4000 segment | `port_123b_layer2_offset = "001"`, subsequent writes shifted one 16K page | zxnext.vhd:3922, 2967 |
| G7-16 | Port 0x123B read-back formatting | port 0x123B ← 0xC9 | IN A,(0x123B) | bit layout `segment=11, bits 5:4 = 00, shadow=1, rd_en=0, en=0, wr_en=1` → `0xC9` (note bit 4 always reads 0) | zxnext.vhd:3933 |

### Group 8 — Layer priority interactions at the Layer 2 boundary

This group tests **only** the edges at which Layer 2 participates: the cases
that must hold specifically because of Layer 2 state. Full 6-mode compositor
behaviour (including tilemap/sprite/ULA interplay that is orthogonal to L2)
belongs in the Compositor test plan.

**VHDL basis:** zxnext.vhd:7050, 7121-7123, 7216.

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G8-01 | NR 0x15 priority SLU with L2 opaque over ULA | NR 0x15[4:2]=000, L2 enabled, palette idx 0x40 opaque, ULA colour 0x70 | screenshot | L2 colour 0x40 wins | zxnext.vhd:7216 case SLU |
| G8-02 | L2 transparent ⇒ ULA shows through in SLU | NR 0x15[4:2]=000, L2 palette gives transparent RGB at idx 0x40 | screenshot | ULA colour 0x70 visible | zxnext.vhd:7121-7122 |
| G8-03 | L2 priority bit promotes over sprite | NR 0x15[4:2]=000, palette word for idx 0x80 has bit9=1 and opaque RGB, sprite active over same pixel | screenshot | L2 colour, not sprite | zxnext.vhd:7050, 7123 |
| G8-04 | Priority bit suppressed when L2 pixel transparent | as G6-09 | screenshot | sprite visible (priority gated) | zxnext.vhd:7123 |
| G8-05 | `layer2_rgb` zeroed when transparent | L2 transparent, other layer also transparent | screenshot | fallback 0xE3 (not a stale L2 value) | zxnext.vhd:7122 |

### Group 9 — Negative / boundary / retracted-tautology cases

These cases exist specifically to catch the failure modes identified in the
retraction notice. None of them appeared in the old plan.

| ID | Title | Preconditions | Stimulus | Expected | VHDL cite |
|----|-------|---------------|----------|----------|-----------|
| G9-01 | Disable then re-enable via NR 0x69 | L2 enabled via port 0x123B, then NR 0x69 ← 0x00 | screenshot | L2 disappears (proves NR 0x69 path wires through) | zxnext.vhd:3924 |
| G9-02 | Cold-reset port 0x123B read is 0x00 | cold reset | IN A,(0x123B) | `0x00` | zxnext.vhd:3908-3913, 3933 |
| G9-03 | Clip y1 > y2 empties display | NR 0x18 writes `0,255,200,100` | screenshot | no L2 pixels | layer2.vhd:167 |
| G9-04 | Scroll X with wide branch NOT fired | see G3-12 | — | — | — |
| G9-05 | Wide mode clip `x2=0xFF` ⇒ effective 511 | NR 0x70=0x10, clip x2=0xFF, x1=0 | all 320 columns visible | `clip_x2_q = 0xFF & '1' = 0x1FF = 511`, covers all columns 0..319 | layer2.vhd:134 |
| G9-06 | `hc_eff = hc + 1` cannot be detected as a pure scroll (non-test, explanatory) | — | documentation only | the +1 is folded into the address formula; G2-12 is the scroll-independent probe | layer2.vhd:148 |

## Test Case Summary

| Group | Area | Tests |
|-------|------|------:|
| 1 | Reset defaults (read-back only) | 12 |
| 2 | Resolution modes and address generation | 12 |
| 3 | Scrolling (narrow, wide, 4-bit, wrap branches, MSB) | 12 |
| 4 | Clip window (auto-index, NR 0x1C, coordinates, inclusivity, doubling) | 13 |
| 5 | Palette (offset wrap, 4-bit, palette select) | 9 |
| 6 | Transparency (palette-RGB check, NR 0x14, clip, disable, fallback, priority gate) | 9 |
| 7 | Bank selection and port 0x123B mapping | 16 |
| 8 | Layer priority interactions at the L2 boundary | 5 |
| 9 | Negative / boundary / retracted-tautology cases | 6 |
| | **Total** | **94** |

The prior plan listed ~91 tests and reported 61/61 passing, meaning at most
61 of the listed ~91 were ever realised as code; the gap (~30 unrealised
tests) was silently invisible. This rebuild deliberately renames, regroups,
and adds coverage so that the old pass count cannot be reconciled with the
new one without a test-code audit.

## Retractions

The following test categories from the previous plan are **removed** (or must
be re-proven mechanical before re-adding) because they were tautological,
redundant, or could not catch the bug class they claimed to cover:

- **Old 1.1.3** "Verify pixel at (0,0) and (255,191) — corner pixels correct."
  Expected column was prose, not a byte value. Replaced by **G2-01**.
- **Old 1.1.4** "Row 192+ not displayed — no artefacts below line 191."
  There is no visible "below 191" in narrow mode, so the assertion was a
  no-op in screenshot form. Replaced by **G2-03**, which positively proves
  `vc_valid` kills the pixel by requiring the *ULA fallback* to appear in
  the area where the L2 byte is 0xFF.
- **Old 1.3.2** "Verify high nibble renders first — pixel ordering correct."
  Pure prose; no expected byte. Replaced by **G2-09**.
- **Old 1.3.3** "Only 16 colours per offset — index wraps within 4 bits."
  The wrap doesn't happen within the nibble; this assertion was self-true
  by construction of the VHDL `"0000" & nibble`. Replaced by **G2-10**
  (histogram check over the full image).
- **Old 3.3.2** "Clip register 80 → effective x = 160 — doubled interpretation."
  Expected was a restatement of the stimulus. Replaced by **G4-09**, which
  ties the effective range to concrete on-screen columns.
- **Old 7.1.\*** six-row "stacking order correct" table. Each row's "expected"
  was `a on top of b on top of c`, which only restates the priority mode
  enum. Layer-2-independent priority cases moved to the Compositor plan;
  L2-specific interactions kept in **G8-01..G8-05** with concrete pixel
  expectations.
- **Old 7.2.1..7.2.3** "L2 pixel above sprites / no difference". The
  "no difference" row is the literal `x == x` anti-pattern. Dropped; the
  one useful case (priority bit gated by transparency) is captured in
  **G6-09** and **G8-04**.
- **Old 7.3.\*** blend modes. These are Compositor-plan territory
  (they exercise the mixer, not Layer 2). Moved out.
- **Old 8.1/8.2** "Enable via port 0x123B / via NR 0x69".
  Both wrote the flop and both "passed" without ever proving the two paths
  reach the same flop. Replaced by **G7-10** (cross-path clobber) and
  **G9-01** (NR 0x69 disable after port enable).
- **Old 9.1.1** "First visible pixel reads from x=1 — pixel alignment correct."
  Prose expected value. Replaced by **G2-12**, which uses a known two-byte
  pattern in column 0 and column 1 to prove the one-pixel lookahead.
- **Old 10.\*** reset-default rows — kept in spirit, but every row now cites
  the exact reset line in zxnext.vhd; see **G1-\***.

No `a == a || a != a` expressions were found in the prose, but several rows
were the screenshot equivalent: "stimulus X produces result described by X".
Those have been re-expressed as concrete pixel values or read-back bytes.

## Open questions (VHDL did not fully resolve)

1. **Pipeline alignment of `layer2_en_qq`** (layer2.vhd:197). `layer2_en`
   is captured one `CLK_7` later into `layer2_en_qq`, and `o_layer2_en`
   feeds the compositor directly. In combination with `hc_eff = hc + 1` in
   the address path, the exact screen column at which a clip-edge transition
   becomes visible depends on whether the emulator collapses the pipeline
   to a single render step. **G2-12** will detect a full-pixel shift; a
   half-pixel shift may or may not be observable depending on the
   emulator's rendering granularity. If G2-12 fails, investigate whether
   JNEXT's Layer 2 render step accounts for both the address lookahead and
   the `layer2_en_qq` latch.

2. **Port 0x123B bit 4 read-back.** The VHDL formatter at zxnext.vhd:3933
   does not place `port_123b_layer2_offset` into the read-back byte at all —
   bits 5:4 of the read byte are hardwired `"00"`. **G7-16** assumes this.
   If hardware in fact returns the offset here, the read-back expected value
   needs revision; this should be confirmed against a real Next or against
   the TBBLUE errata.

3. **Interaction of NR 0x12 writes mid-frame.** layer2.vhd:120 latches
   `layer2_active_bank_q` on every `CLK_7`, so a mid-frame NR 0x12 write
   takes effect from the next pixel. No test currently covers this timing,
   because the Copper / DMA-timed mid-frame write is Copper-plan territory.
   Flagged so the Copper plan owner knows the VHDL semantics.

4. **"Bit 21 disables pixel" test (G7-04).** The VHDL uses `layer2_addr_eff`
   as a 22-bit value and masks display when bit 21 is set (layer2.vhd:175).
   JNEXT's emulation may or may not model SRAM with 22-bit addressing; the
   test should still pass visually (no L2 pixel) but the underlying failure
   mode may be different (e.g. JNEXT might wrap the bank to a valid page
   instead). If G7-04 reports an L2 pixel, the first thing to check is
   whether the C++ bank computation even has a bit-21 check.

5. **Transparency on the 9-bit RGB**. zxnext.vhd:7121 compares
   `layer2_rgb_2(8:1)` against the 8-bit `transparent_rgb_2`. Bit 0 of the
   9-bit RGB is **dropped** in the comparison, meaning two palette entries
   whose 9-bit outputs differ only in bit 0 both match the same transparent
   value. No test currently exercises this last-bit drop; it would require a
   palette whose bit-0 LSB differs from NR 0x14's implied bit 0. Deferred
   as a subtle edge case; callout here so it is not forgotten.

6. **NR 0x14 vs NR 0x4A coincidence.** Both default to `0xE3`. Any test
   that wants to prove the fallback is "the fallback, not the L2 transparent
   passthrough" must deliberately change one of the two. **G6-07** and
   **G6-08** do this, but the reset-default case is intentionally ambiguous
   — no test distinguishes "L2 transparent and showing NR 0x4A" from
   "L2 transparent and showing stale NR 0x14" at reset. Resolved in spirit
   by G6-08 once NR 0x4A is changed.

7. **`layer2_en_qq` one-pixel latency not directly observed.**
   layer2.vhd:197 registers `layer2_en` into `layer2_en_qq` on CLK_7,
   and `o_layer2_en` (the compositor gate) is `layer2_en_qq`, not
   `layer2_en`. G2-12 proves the address-path lookahead (`hc_eff = hc+1`)
   but does not independently observe the enable-path latch. A targeted
   test would need a mid-line clip-edge transition and a sample exactly
   at the transition column; the expected visible edge is shifted by
   one CLK_7 pixel relative to the address-path edge. Deferred because
   the JNEXT renderer collapses the pipeline; flagged so a future
   cycle-accurate pass has a hook.

8. **NR 0x69 vs port 0x123B same-cycle collision.** zxnext.vhd:3914-3925
   is an `elsif` chain: if `port_123b_wr='1'` and `nr_69_we='1'` on the
   same rising edge, the port write wins and the NR 0x69 write to
   `port_123b_layer2_en` is dropped. G7-10 and G9-01 only exercise
   sequential writes. A same-cycle collision is essentially unreachable
   from Z80 software (two I/O cycles cannot end on the same CLK_28
   rising edge) and untestable from our harness; recorded here as the
   documented priority for anyone who later wires an internal stimulus
   path (Copper, DMA-to-IO) that could clash.
