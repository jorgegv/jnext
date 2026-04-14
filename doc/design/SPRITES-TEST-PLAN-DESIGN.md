# Sprites Subsystem — VHDL-Derived Compliance Test Plan

> **Plan Rebuilt 2026-04-14 — Prior 48/48 status RETRACTED.**
>
> The previous revision of this plan claimed 48/48 passing but was flagged
> during the Task 4 audit as **coverage theatre**. Concrete defects in the
> retracted plan/implementation pair:
>
> - Multiple rows asserted the literal constant `true` (CL-01, CL-02, CL-03,
>   CL-04, RST-04, RST-05), so they passed regardless of emulator behaviour.
> - Several "write X, read X back" rows verified the test harness setter, not
>   hardware state derivable from VHDL.
> - **Zero** rendering assertions: no row ever compared a line-buffer pixel or
>   framebuffer ARGB against a VHDL-derived expected value. Anchor/relative
>   composition, 4bpp vs 8bpp decoding, mirroring, rotation, scaling, clip
>   window, sprite-over-border, per-line limit and collision flags were all
>   described in prose but never exercised end-to-end.
> - The transparency rows (TR-03/TR-04) said "compare full byte" / "compare
>   nibble" but the implementation never rendered a pixel, so the compare path
>   in `spr_line_we` (sprites.vhd:971) was not covered at all.
>
> All expected values below are re-derived from the authoritative VHDL at
> `ZX_Spectrum_Next_FPGA/cores/zxnext/src/video/sprites.vhd` (cited by line).
> The implementation phase (separate branch, not this document) must ban
> tautological assertions: no `x == x`, no literal `true`, no
> `a == b || a != b`, no "does not crash" rows.

## Purpose

The ZX Spectrum Next hardware sprite engine (`sprites.vhd`) implements 128
sprites × 64 patterns with 4bpp/8bpp modes, anchor/relative composition,
independent or inherited transforms (mirror/rotate/scale), palette offsets,
per-pixel transparency by palette index, a 320×1 double-buffered line
buffer, a 4-state FSM that caps per-line sprite rendering, sprite-sprite
collision detection, and a clip window that changes meaning under
sprite-over-border mode. JNEXT's Sprites subsystem must match this VHDL
exactly. This plan derives every expected value from a specific VHDL line.

## VHDL Oracle — Entity and Key Constants

### Entity `sprites` (`sprites.vhd:35`)

Inputs observed by the tests below:

| Signal | Line | Role |
|--------|-----:|------|
| `clock_master_i`, `clock_pixel_i` | 38–40 | Clock domains |
| `reset_i` | 41 | Power-on reset |
| `zero_on_top_i` | 42 | NR 0x15 bit 6, lower sprite priority |
| `border_clip_en_i` | 43 | NR 0x15 bit 5, enables clip in over-border mode |
| `over_border_i` | 44 | NR 0x15 bit 1, sprites drawn over border |
| `hcounter_i`, `vcounter_i` | 45–46 | 9-bit beam position |
| `transp_colour_i` | 47 | NR 0x4B, sprite transparent index |
| `port57_w_en_s` / `port5B_w_en_s` / `port303b_{r,w}_en_s` | 51–54 | CPU port strobes |
| `mirror_*_i` | 60–65 | NextREG 0x34 mirror path |
| `clip_{x1,x2,y1,y2}_i` | 74–77 | NR 0x19 clip window |

Outputs: `rgb_o(7:0)` plus `pixel_en_o` (sprites.vhd:69–70).
`rgb_o <= video_line_rgb_load(7 downto 0)` (sprites.vhd:1068) — the sprite
engine emits an 8-bit palette **index**, not ARGB. Palette lookup happens
in the palette LUT **outside** sprites.vhd (external to this subsystem), so
rendering assertions in this plan target the 8-bit sprite palette index at
the (hcounter, vcounter) position, not final ARGB.

### Constants (`sprites.vhd:86–94`)

| Constant | Value | Line |
|----------|------:|-----:|
| `SPRITE_SIZE_BITS` | 4 | 86 |
| `SPRITE_SIZE` | 16 | 87 |
| `TOTAL_SPRITES_BITS` | 7 | 90 |
| `TOTAL_SPRITES` | 128 | 91 |
| `TOTAL_PATTERN_BITS` | 6 | 93 |
| `TOTAL_PATTERNS` | 64 | 94 |
| Pattern RAM | 16 KiB (14-bit addr) | component `sdpbram_16k_8`, 125–139 |
| Line buffer | 320 × 9 bits | component `spram_320_9`, 112–122 |

### Attribute bytes (from VHDL comments at `sprites.vhd:325, 352, 381, 409, 437`)

| Byte | Bits | Field | VHDL citation |
|------|------|-------|---------------|
| 0 | 7:0 | X position low | 325 |
| 1 | 7:0 | Y position low | 353 |
| 2 | 7:4 | palette offset | 381 |
| 2 | 3 | X mirror | 381 |
| 2 | 2 | Y mirror | 381 |
| 2 | 1 | rotate | 381 |
| 2 | 0 | X MSB (bit 8) | 381 |
| 3 | 7 | visible | 409 |
| 3 | 6 | 5th-byte enable | 409 |
| 3 | 5:0 | pattern index N5:N0 | 409 |
| 4 | 7 | H (4bpp enable) | 437 |
| 4 | 6 | Pattern index bit N6 | 437 |
| 4 | 5 | relative type (0=individual, 1=unified) | 437 |
| 4 | 4:3 | X scale (00..11 = 1×..8×) | 437 |
| 4 | 2:1 | Y scale (00..11 = 1×..8×) | 437 |
| 4 | 0 | Y MSB (bit 8) | 437 |

### I/O ports

| Port | Dir | Function | Line |
|------|-----|----------|-----:|
| 0x57 | W | Attribute write, auto-increment | 639–667 |
| 0x5B | W | Pattern byte write, auto-increment | 728–744 |
| 0x303B | W | Set sprite index + pattern index | 655–657, 735–736 |
| 0x303B | R | Status register (collision + overtime) | 746–748, 979–995 |

### NextREGs (handled in `zxnext.vhd`; all values below are what `sprites` expects on its inputs)

| Reg | Field | Role | VHDL line |
|-----|-------|------|-----------|
| 0x09 bit 4 | `mirror_tie_i` | Tie NR 0x34 mirror to 0x303B sprite select | 653–654 |
| 0x15 bit 6 | `zero_on_top_i` | Low-index sprite wins overlap | 972 |
| 0x15 bit 5 | `border_clip_en_i` | Apply clip in over-border mode | 1044–1054 |
| 0x15 bit 1 | `over_border_i` | Allow rendering outside 256×192 ULA window | 1043–1060 |
| 0x15 bit 0 | sprite enable (external) | Gates `pixel_en_o` downstream; not driven from sprites.vhd | compositor |
| 0x19 | clip window (4 auto-inc writes) | `clip_x1_i..clip_y2_i` | 74–77, 1050–1059 |
| 0x34 | mirror attribute write | `mirror_*_i` | 594–612 |
| 0x4B | sprite transparent index | `transp_colour_i` | 47, 971 |

Reset defaults for the clip window (from the Next TRM / NR 0x19 default,
verified against VHDL which treats `clip_{x1,y1}=0, x2=0xFF, y2=0xBF` as a
plain register file; the defaults are applied by NextReg, not by
sprites.vhd): `x1=0x00, x2=0xFF, y1=0x00, y2=0xBF`.

## Retractions (from prior 48/48 plan)

The rows below are **retracted** because their implementation asserted a
literal constant or verified only the test harness. They must be replaced
by the new rows in the groups listed.

| Retracted ID | Defect | Replaced by |
|--------------|--------|-------------|
| CL-01 | `check(..., true)` — no clip semantics verified | G6.CL-* (rendering-based) |
| CL-02 | `check(..., true)` — setters only | G6.CL-* |
| CL-03 | `check(..., true)` — setter only, wrong group | G11.OB-* |
| CL-04 | `check(..., true)` — setter, misnamed as clip | G7.PR-* |
| RST-04 | `check(..., true)` — no getter, no assertion | G14.RST-* (mirror-num readback) |
| RST-05 | `check(..., true)` — same | G14.RST-* |
| TR-03 / TR-04 (old) | Described compare but never rendered | G8.TR-* (pixel-level) |
| PR-01..PR-04 (old) | Described zero_on_top but never rendered | G7.PR-* |
| LP-01..LP-11 (old) | Layer priority belongs to Compositor plan | moved out |
| AN-01..AN-07 (old) | Anchor latching never observed via rendered pixels | G12.AN-* |
| RE-01..RE-11 (old) | Relative composition never rendered | G12.RE-* |
| RT-01..RT-06 (old) | Type 0 vs 1 never rendered | G12.RT-* |
| RP-01..RP-04 (old) | Relative pattern never rendered | G12.RP-* |
| SC-01..SC-11 (old) | Scale only verified attr bits, not pixel span | G10.SC-* |
| MI-01..MI-05, RO-01..RO-05 (old) | Mirror/rotate only verified flag bits | G10.MI-*, G10.RO-* |
| OT-01..OT-04, CO-01..CO-05 (old) | Overtime and collision never triggered | G13.OT-*, G13.CO-* |
| SR-01..SR-04 (old) | Status register never saw a real event | G13.SR-* |
| SM-01..SM-07 (old) | FSM state inspected directly; should be observed via outputs | folded into G10–G13 |

Layer priority (old Group 8, LP-01..LP-11) is fully removed from this plan
and belongs to `COMPOSITOR-TEST-PLAN-DESIGN.md`. Sprites.vhd does **not**
compute layer priority; it only emits `rgb_o` and `pixel_en_o`.

## Oracle Helpers (derived formulas, all with VHDL citations)

These are the numeric formulas the test oracle will use to compute expected
pixel values. Every test row below references one of them.

**OF1 — 9-bit sprite position (`sprites.vhd:796–799`):**
```
spr_cur_x = sprite_attr_2(0) & sprite_attr_0           -- 9 bits
spr_y8    = '0' if sprite_attr_3(6)='0' else spr_cur_attr_4(0)
spr_cur_y = spr_y8 & sprite_attr_1                     -- 9 bits
```

**OF2 — 8bpp pattern address, no transforms (`sprites.vhd:816, 817–820`):**
```
addr_start = spr_rel_pattern(6:1) & y_index & x_index  -- 14 bits
            -- y_index = spr_y_offset(3:0)             (line 811, no ymirror)
            -- x_index = 0 initially                   (line 814, no xmirror)
delta = +1 per pixel while no mirror, no rotate
```

**OF3 — X mirror / rotate delta table (`sprites.vhd:817–820`):**

| xmirror_eff | rotate | delta |
|-------------|--------|-------|
| 0 | 0 | +0x001 |
| 1 | 0 | -0x001 (0x3FFF) |
| 0 | 1 | +0x010 |
| 1 | 1 | -0x010 (0x3FF0) |

where `spr_x_mirr_eff = sprite_attr_2(3) XOR sprite_attr_2(1)`
(sprites.vhd:813). Y mirror complements `spr_y_offset(3:0)` (line 811).

**OF4 — Y-scale shift (`sprites.vhd:807–810`):**

| attr4(2:1) | effective y_offset | sprite height |
|------------|--------------------|---------------|
| 00 | raw | 16 |
| 01 | raw >> 1 (arith) | 32 |
| 10 | raw >> 2 | 64 |
| 11 | raw >> 3 | 128 |

Only active if `sprite_attr_3(6)='1'` (5th byte present). Else forced 1×.

**OF5 — X-scale pattern advance delta (`sprites.vhd:907–915`):**

| attr4(4:3) | `spr_width_count_delta` | pattern advances every N pixels |
|------------|-------------------------|---------------------------------|
| 00 | 8 | 1 |
| 01 | 4 | 2 |
| 10 | 2 | 4 |
| 11 | 1 | 8 |

**OF6 — X wrap mask (`sprites.vhd:919–927`):**

| attr4(4:3) | `spr_cur_x_wrap` | max horizontal extent |
|------------|------------------|-----------------------|
| 00 | 11111 | 16 px |
| 01 | 11110 | 32 px |
| 10 | 11100 | 64 px |
| 11 | 11000 | 128 px |

**OF7 — 4bpp nibble selection and pattern address (`sprites.vhd:962–968`):**
```
spr_pat_addr = spr_cur_pattern_addr                          if H=0
             = addr(13:8) & spr_cur_4bit(0) & addr(7:1)       if H=1
spr_nibble_data = spr_pat_data(7:4) if addr(0)=0 else spr_pat_data(3:0)
spr_line_data_s(7:0) =
    (spr_pat_data(7:4) + paloff) & spr_pat_data(3:0)   if 8bpp
    paloff & spr_nibble_data                            if 4bpp
```

Key subtlety captured in row G3.PA-03: in **8bpp** mode the palette offset
is added only to the **upper nibble** of the pattern byte. In **4bpp** mode
the palette offset **replaces** the upper nibble.

**OF8 — Transparency compare (`sprites.vhd:971`):**
```
write_pixel = (state = S_PROCESS) AND hcount_valid AND
              ( (8bpp  AND spr_pat_data(7:0) /= transp_colour_i)
             OR (4bpp  AND spr_nibble_data    /= transp_colour_i(3:0)) )
```

**Critical property:** the compare is against the 8-bit **palette index**
(`spr_pat_data` or `spr_nibble_data`), **not** the ARGB output of the
palette LUT. Two palette slots that happen to render the same RGB are
**not** both transparent. This is the classic "alpha-vs-index" pitfall.

**OF9 — Line-buffer priority / collision (`sprites.vhd:969, 972, 991`):**
```
spr_line_data_s(8) = '1'                            -- marker bit
spr_line_we_s = spr_line_we AND (zero_on_top=0 OR spr_line_data_o(8)=0)
collision_bit = status_reg_s(0) OR (spr_line_data_o(8) AND spr_line_we)
```

So `zero_on_top=1` makes the first sprite (lowest index) that hits a pixel
win; `zero_on_top=0` lets later sprites overwrite. Collision fires whenever
any two sprites compute a non-transparent pixel at the same X, regardless
of `zero_on_top`.

**OF10 — Relative sprite derivation (`sprites.vhd:756, 760–786`):**

A sprite is "relative" iff `attr3(6)=1 AND attr4(7:6)="01"`
(sprites.vhd:756). The anchor's latched state (`anchor_*`, sprites.vhd:898–
949) is used to rewrite the relative sprite's effective attributes:

```
spr_rel_x0 = attr0 (no anchor rotate) | attr1 (anchor rotate)  -- 760–761
spr_rel_y0 = attr1 (no anchor rotate) | attr0 (anchor rotate)
spr_rel_x1 = x0 or (-x0)  if (anchor_rotate XOR anchor_xmirror) -- 762
spr_rel_y1 = y0 or (-y0)  if  anchor_ymirror                    -- 763
spr_rel_x2 = x1 shifted left by anchor_xscale (0/1/2/3)         -- 764–767
spr_rel_y2 = y1 shifted left by anchor_yscale                   -- 768–771
spr_rel_x3 = anchor_x + x2                                      -- 772
spr_rel_y3 = anchor_y + y2                                      -- 773

paloff (rel) = attr2(7:4)                     if attr2(0)=0     -- 775
             = anchor_paloff + attr2(7:4)      if attr2(0)=1

pattern (rel) = (attr3(5:0) & N6) + anchor_pattern if attr4(0)=1 -- 803
              = (attr3(5:0) & N6)                  otherwise
```

Relative type 0 (`anchor_rel_type='0'`) passes relative's own mirror/rotate
through; type 1 XOR-combines them with the anchor's and inherits the
anchor's scale (sprites.vhd:782–786, 929–949).

**OF11 — Clip window semantics (`sprites.vhd:1043–1060`):**

```
over_border=1, border_clip_en=0: (x_s,x_e,y_s,y_e) = (0, 319, 0, 255)
over_border=1, border_clip_en=1: (clip_x1 & '0', clip_x2 & '1',
                                   '0' & clip_y1, '0' & clip_y2)
over_border=0: x_s = ({0,clip_x1(7:5)}+1) & clip_x1(4:0)     -- non-linear!
               x_e = ({0,clip_x2(7:5)}+1) & clip_x2(4:0)
               y_s / y_e likewise
```

Row G6.CL-06 specifically asserts the **non-linear** top-three-bits
increment so this path is actually covered.

**OF12 — Per-line budget / overtime (`sprites.vhd:977, 841–866`):**

`sprites_overtime='1'` iff `state_s /= S_IDLE` at the instant
`line_reset_re="01"`. The FSM takes one `clock_master_i` cycle per
S_QUALIFY transition and one per pixel in S_PROCESS (sprites.vhd:826–866,
951–957). There is **no** fixed "max N sprites per line" constant; the
limit emerges from available cycles per scanline. Row G13.OT-* uses a
scanline populated with the maximum-width case (128 sprites, all visible,
all 16px wide, all on the same Y) as the VHDL-authoritative stress input
and asserts that `status_reg(1)` becomes 1. The exact cycle budget is an
**open question** (see bottom of this doc) because it depends on pixel
clock and vcounter wiring outside sprites.vhd.

## Test Case Catalog

Each row is: **ID · Title · Preconditions · Stimulus · Expected · VHDL cite**.

### Group 1 — Attribute Port 0x57 & Mirror Path Side-Effects

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G1.AT-01 | 4-byte write auto-skips to next sprite attr0 | Reset; set sprite idx=0 via 0x303B | Write 4 bytes to 0x57 with `attr3(6)=0` on the 4th | After 4th write, `attr_index = 1 & "000"` (next sprite, byte 0) | 639–664 |
| G1.AT-02 | 5-byte write advances through attr4 | Set sprite idx=0 | Write 5 bytes with `attr3(6)=1` on the 4th | After 5th write, `attr_index = 1 & "000"` | 639–664 |
| G1.AT-03 | 0x303B sets `attr_index = d(6:0) & "000"` | Reset | Write 0x7F to 0x303B | `attr_index = 0x3F8` and `cpu_sprite_q=0x7F` | 655–657 |
| G1.AT-04 | 0x303B sets `pattern_index = d(5:0)&d(7)&"0000000"` | Reset | Write 0xC1 to 0x303B | `pattern_index(13:7)=0x01 & 1` = bit-exact per formula | 735–736 |
| G1.AT-05 | Attr2 bitfields readable as (paloff, xm, ym, rot, xmsb) | — | Write attr2=0xAF | Fields split per 381: paloff=0xA, xm=1, ym=1, rot=1, xmsb=1 | 381 |
| G1.AT-06 | Attr4 bitfields (H, N6, type, xscale, yscale, ymsb) | attr3(6)=1 | Write attr4=0xED | H=1, N6=1, type=1, xs=01, ys=10, ymsb=1 | 437 |
| G1.AT-07 | Sprite 127 is the last slot | — | 0x303B ← 0x7F; write attrs | attr1/2/3/4 land in slot 127 | 655 |
| G1.AT-08 | Attr write via NR 0x34 mirror path | `mirror_we_i=1`, `mirror_index_i=010` | Write mirror_data=0x5A | sprite_attr_2 at `mirror_sprite_q` slot = 0x5A | 704–715 |
| G1.AT-09 | Mirror `index="111"` sets sprite number | `mirror_we=1, idx=111, data=0x05` | — | `mirror_sprite_q = 0x05`, `mirror_num_change='1'` | 600–602 |
| G1.AT-10 | `mirror_inc_i` increments within 7 bits | mirror_sprite_q=0x7F | Pulse `mirror_inc_i` | mirror_sprite_q(6:0) wraps to 0 | 603–605 |
| G1.AT-11 | `mirror_tie_i=1` syncs attr_index to mirror number | Tie=1 | Mirror num ← 0x10 | attr_index becomes 0x10 & "000" | 653–654 |
| G1.AT-12 | Mirror write takes priority over pending CPU write | Both latched same cycle | `mirror_served=1` blocks `cpu_served` | attr0_we driven from mirror, not cpu | 704–715 |

### Group 2 — Pattern Port 0x5B and Pattern RAM

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G2.PL-01 | 256-byte pattern upload targets bytes 0..255 of pattern 0 | 0x303B ← 0x00 | 256 writes to 0x5B | pattern RAM `[0..255]` equals upload | 728–744 |
| G2.PL-02 | Last pattern (63) writable | 0x303B ← 0x3F | 256 writes | RAM `[63*256..63*256+255]` = upload | 728–744 |
| G2.PL-03 | Auto-increment crosses pattern boundary | 0x303B ← 0x00 | 512 writes | pattern 0 full + pattern 1 full, no wrap to 0 | 738, 645 |
| G2.PL-04 | `pattern_index(7)` half-pattern offset for 4bpp | 0x303B ← 0x80 | 128 writes | RAM `[0*256+128 .. 0*256+255]` = upload | 736 |
| G2.PL-05 | 14-bit pattern address does not spill above 0x3FFF | 0x303B ← 0x3F, `d(7)=1` | 128 writes | Final `pattern_index=0x3FFF` (not wrap to 0) | 738 |

### Group 3 — Pixel Decoding (the rendering oracle)

All rows in this group compare the 8-bit palette-index **pixel** written to
the active line buffer at position `(hcounter, vcounter)`, not the final
ARGB value. The test harness must drive `clock_master_i`,
`clock_master_180o_i`, `clock_pixel_i`, `hcounter_i`, `vcounter_i`
consistent with the VHDL FSM for one full scanline.

| ID | Title | Preconditions | Stimulus | Expected pixel | VHDL |
|----|-------|---------------|----------|----------------|------|
| G3.PX-01 | 8bpp opaque pixel, paloff=0, no mirror/rotate/scale | pattern0[0]=0x42, transp=0xE3 | sprite(x=0,y=0) visible, attr3(6)=0 | line_buf[0] low byte = 0x42 | 968, 971 |
| G3.PX-02 | 8bpp paloff applies to upper nibble only | pattern0[0]=0x15, paloff=0x3 | render at x=0 | low byte = ((0x1+0x3)<<4) \| 0x5 = 0x45 | 968 |
| G3.PX-03 | 8bpp paloff upper nibble wraps mod 16 | pattern0[0]=0xF5, paloff=0x2 | x=0 | ((0xF+0x2) & 0xF)<<4 \| 0x5 = 0x15 | 968 |
| G3.PX-04 | 4bpp (H=1), even addr, upper nibble selected | pattern0[0]=0x73, paloff=0x4 | attr4(7)=1, x=0 | paloff<<4 \| 0x7 = 0x47 | 967–968 |
| G3.PX-05 | 4bpp, odd addr, lower nibble selected | pattern0[0]=0x73 | x=1 (addr(0)=1) | paloff<<4 \| 0x3 = 0x43 | 967 |
| G3.PX-06 | 4bpp addr remap: `pat_addr_b = addr(13:8) & n6 & addr(7:1)` | N6=1, pattern selects high half | render | correct byte from 4bpp half-pattern | 962–964 |
| G3.TR-01 | 8bpp transparent pixel (full byte) not written | pattern0[0]=0xE3, transp=0xE3 | render x=0 | line_buf[0] unchanged (bit 8 stays 0) | 971 |
| G3.TR-02 | 4bpp transparent nibble not written, other nibble of same byte still writes | pattern byte=0x3A, transp=0x03 → low nibble=3 | render x=0 (upper→A) then x=1 (lower→3) | line_buf[0] has 0xpA, line_buf[1] unchanged | 971 |
| G3.TR-03 | Transparent compare is on palette **index**, not ARGB | Set palette so entries 0x04 and 0xFC map to same RGB; transp=0x04 | pattern byte=0xFC | pixel still **written** — compare is index-based | 971 |
| G3.TR-04 | 8bpp paloff change does not make the transparency check compare the *post-add* value | pattern=0x10, paloff=0xF (post-add upper=0xFF), transp=0xFF | render | pixel written — VHDL compares `spr_pat_data` pre-add | 971, 968 |
| G3.PA-01 | 4bpp replaces upper nibble with paloff | 4bpp, nibble=0x5, paloff=0xC | — | pixel = 0xC5 | 968 |
| G3.PA-02 | Line buffer bit 8 set on any sprite write | Any opaque write | — | `spr_line_data_s(8)='1'` | 969 |

### Group 4 — Position, 9-bit Coordinates, Screen Edges

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G4.XY-01 | Sprite at (0,0) opaque fills [0..15] on line 0 | 16×16 fully-opaque pattern | draw line 0 | line_buf[0..15] all have bit8=1 | 796–799, 965 |
| G4.XY-02 | X MSB (attr2(0)=1) gives x=256+attr0 | attr0=0x20, attr2=0x01 | render on line 0 | pixels at 288..303 | 799 |
| G4.XY-03 | Y MSB requires 5th byte; else forced to 0 | attr3(6)=0, attr4(0)=1 | spr_cur_y | y8=0 regardless of attr4(0) | 796 |
| G4.XY-04 | Y MSB honored with 5th byte | attr3(6)=1, attr4(0)=1, attr1=0x00 | y=256, only visible on vcounter=256 | pixels on line 256 only | 796 |
| G4.XY-05 | x=319 renders last valid column | sprite x=319, 1px opaque pattern | — | line_buf[319] written; state exits when hcount_valid=0 | 822, 855–860 |
| G4.XY-06 | x=320 fully off-screen, x-wrap 1× (mask 11111) still renders via wrap-around | sprite x=320 | — | state stays in S_PROCESS until mask condition holds; no pixel on this line | 822, 855 |
| G4.XY-07 | 2× scale wrap-around, sprite starts at x=300 | attr4(4:3)=01 | — | 20 px drawn at 300..319, remainder absorbed by wrap mask 11110 | 919–927 |

### Group 5 — Visibility and 5th-Byte Gating

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G5.VIS-01 | `attr3(7)=1` and on-scanline ⇒ renders | — | render | pixels written | 917, 842 |
| G5.VIS-02 | `attr3(7)=0` ⇒ S_QUALIFY→S_QUALIFY (skipped) | — | — | no line buffer writes for that sprite | 842, 848–854 |
| G5.VIS-03 | Y not on this line ⇒ `spr_cur_yoff≠0` ⇒ skipped | sprite y=50, draw line 80 | — | skipped | 842, 918 |
| G5.VIS-04 | `spr_cur_hcount_valid=0` at entry and no x-wrap ⇒ no write | x=320, scale=1× | — | zero pixels written | 822, 855 |
| G5.VIS-05 | Invisible sprite still latches its anchor context for a later relative sprite? **NO** | anchor sprite has attr3(7)=0 | — | `anchor_vis=0`, subsequent relative sprite is also invisible | 917, 784 |

### Group 6 — Clip Window

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G6.CL-01 | Reset defaults {0,0xFF,0,0xBF} pass full display window | over_border=0 | render fully within | all pixels emitted on `pixel_en_o` | 1055–1060 |
| G6.CL-02 | Non-over-border x transform `(({0,x1(7:5)}+1) & x1(4:0))` | clip_x1=0x1F | — | x_s_v = 0x3F (not 0x1F) — tests the +1 on top 3 bits | 1055 |
| G6.CL-03 | Non-over-border x2 transform same formula | clip_x2=0xE0 | — | x_e_v = (0x7 +1)<<5 \| 0 = 0x100 (=256) | 1056 |
| G6.CL-04 | Over-border, clip_en=0 ⇒ full 320×256 | over_border=1, clip_en=0 | — | (0,319,0,255) | 1044–1048 |
| G6.CL-05 | Over-border, clip_en=1 ⇒ (x1*2, x2*2+1, y1, y2) | clip_x1=0x40, clip_x2=0x80 | — | x_s=0x80, x_e=0x101 | 1049–1053 |
| G6.CL-06 | Sprite pixel suppressed when `(h,v)` outside (x_s..x_e, y_s..y_e) | sprite at x=10, clip_x1 set so x_s=32 | — | `pixel_en_o=0` for x<32 | 1067 |
| G6.CL-07 | Sprite pixel emitted when inside clip and non-zero line-buf bit 8 | — | — | `pixel_en_o=1` | 1067 |

### Group 7 — Priority (`zero_on_top`) at the Sprite Boundary

This group does **not** test layer composition — that belongs to the
Compositor plan. It only verifies the NR 0x15 bit 6 effect on the
sprite-internal line buffer.

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G7.PR-01 | `zero_on_top=0`: higher-index sprite wins overlap | sprites 0 and 1 both at x=50, different opaque colours | render | line_buf[50..65] low byte = sprite 1's colour | 972 |
| G7.PR-02 | `zero_on_top=1`: lower-index sprite wins | same as PR-01 | render | line_buf[50..65] = sprite 0's colour | 972 |
| G7.PR-03 | bit 8 of line-buffer entry cleared each scanline by video path | render line N, then N+1 | — | on line N+1, first-to-hit wins even when z_on_top=1 | 1023–1033 |
| G7.PR-04 | Collision flag set regardless of `zero_on_top` | overlap two sprites, read 0x303B | — | status bit 0 = 1 | 991 |

### Group 8 — Transparency (integrated with rendering)

Covered by G3.TR-01..TR-04. This group header exists only so the audit
trail maps 1:1 to the old TR-* IDs.

### Group 9 — Mirroring and Rotation

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G9.MI-01 | Plain sprite, pattern byte 0 renders at x=0, byte 15 at x=15 | unique byte per column | render | line_buf[i] holds byte `i` | 811–820, 904 |
| G9.MI-02 | X-mirror flips columns: byte 15 at x=0, byte 0 at x=15 | attr2(3)=1 | render | reversed | 813, 817–820 |
| G9.MI-03 | Y-mirror on row 0 reads pattern row 15 | attr2(2)=1 | draw vcounter=sprite_y | y_index = NOT 0 = 15 | 811 |
| G9.MI-04 | Both mirrors | attr2(3)=attr2(2)=1 | — | fully rotated 180° | 811, 813 |
| G9.RO-01 | Rotate swaps row/col in address | attr2(1)=1 | render | addr = `pat & x_index & y_index` | 816 |
| G9.RO-02 | `x_mirr_eff = xmirror XOR rotate` | attr2(3)=0, attr2(1)=1 | — | effective x-mirror=1 | 813 |
| G9.RO-03 | Rotate + x-mirror produces delta = -16 (0x3FF0) | attr2(3)=1, attr2(1)=1 | — | pattern address advances by -16 per pixel | 817 |
| G9.RO-04 | Rotate without mirror: delta = +16 | attr2(1)=1, attr2(3)=0 | — | +16 | 819 |

### Group 10 — Scaling

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G10.SC-01 | X 1× renders 16 px, advances addr every pixel | — | — | 16 pixels with distinct pattern bytes | 907–908, 954–956 |
| G10.SC-02 | X 2× renders 32 px, each byte repeated twice | attr4(4:3)=01 | — | delta=4 ⇒ bit 3 of width count flips every 2 px | 909–910 |
| G10.SC-03 | X 4× renders 64 px, each byte×4 | attr4(4:3)=10 | — | delta=2 | 911–912 |
| G10.SC-04 | X 8× renders 128 px, each byte×8 | attr4(4:3)=11 | — | delta=1 | 913–914 |
| G10.SC-05 | Y 2× shows row 0 on 2 consecutive scanlines | attr4(2:1)=01 | vcounter=y, y+1 | both read y_index=0 | 808 |
| G10.SC-06 | Y 4× repeats 4× | attr4(2:1)=10 | — | arith shift by 2 | 809 |
| G10.SC-07 | Y 8× repeats 8× | attr4(2:1)=11 | — | arith shift by 3 | 810 |
| G10.SC-08 | 5th byte absent ⇒ scale forced 1× regardless of attr4 bits | attr3(6)=0 | — | delta=8 and no Y shift | 907, 919 |
| G10.SC-09 | Combined X=4×, Y=2× covers 64×32 rectangle | attr4=…Type0… | — | 64 distinct X pixels across 32 scanlines | 807–915 |
| G10.SC-10 | X wrap mask for 2× is 11110 | sprite x=300, 2× | render | stops when `hcount(8:4) AND 11110 = 11110` | 921 |

### Group 11 — Sprite Over Border

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G11.OB-01 | `over_border=0`: sprite at y=200 not emitted (clip via non-linear transform puts y_e ≤ 191) | default clip, y=200 | — | `pixel_en_o=0` | 1055–1067 |
| G11.OB-02 | `over_border=1, border_clip_en=0`: sprite at y=200 emitted | — | — | `pixel_en_o=1` | 1044–1048, 1067 |
| G11.OB-03 | `over_border=1, border_clip_en=1`: sprite at y=200, clip_y2=0xBF | — | — | emitted only for y≤0xBF=191 ⇒ not emitted | 1049–1053 |
| G11.OB-04 | `pixel_en_o` also requires `vcounter < 224` when `over_border_s=0` | over_border=0, y=223 | — | not emitted | 1067 |

### Group 12 — Anchor and Relative Composition

Every row renders at least one pixel and compares it; none of them stop at
"attribute bit set".

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G12.AN-01 | Sprite with `attr4(7:6)≠"01"` and attr3(6)=1 is an anchor; latches x,y,paloff,pattern | anchor type=0 | next S_QUALIFY | anchor_x=spr_cur_x etc. | 929–936 |
| G12.AN-02 | Anchor type=1 additionally latches rotate/mirror/scale | attr4(5)=1 | — | anchor_{rotate,xmirror,ymirror,xscale,yscale} set | 937–942 |
| G12.AN-03 | Anchor type=0 zeroes latched transforms | attr4(5)=0 | — | anchor_{rotate,xm,ym,xscale,yscale}=0 | 943–948 |
| G12.AN-04 | 4-byte (attr3(6)=0) sprite does **not** update anchor | — | — | previous anchor_* preserved | 929 |
| G12.AN-05 | `anchor_vis` is `attr3(7)` of anchor | — | — | invisible anchor ⇒ all its relatives invisible | 932, 784 |
| G12.RE-01 | Relative with no transforms renders at `anchor_pos + (signed attr0, attr1)` | anchor type=0 at (100,100), rel attr0=10, attr1=5 | render | pixel at (110, 105) | 760–773 |
| G12.RE-02 | Relative inherits visibility: anchor invisible ⇒ relative invisible | anchor attr3(7)=0 | — | zero pixels for relative | 784 |
| G12.RE-03 | Relative palette: attr2(0)=0 ⇒ direct paloff | — | — | paloff = rel's attr2(7:4) | 775 |
| G12.RE-04 | Relative palette: attr2(0)=1 ⇒ `anchor_paloff + attr2(7:4)` (mod 16) | — | — | verified sum | 775 |
| G12.RE-05 | Anchor rotate swaps rel's offset axes (x0↔y0) | anchor type=1 rotate=1 | rel attr0=dx, attr1=dy | pixel at `anchor + (dy,dx)` | 760–761 |
| G12.RE-06 | Anchor xmirror XOR rotate negates rel X offset | type=1, xmirror=1, rotate=0 | rel attr0=+10 | pixel at `anchor_x - 10` | 762 |
| G12.RE-07 | Anchor ymirror negates rel Y offset | type=1, ymirror=1 | rel attr1=+4 | pixel at `anchor_y - 4` | 763 |
| G12.RE-08 | Anchor xscale=01 doubles rel X offset (shift left 1) | type=1, xscale=01 | rel attr0=+5 | pixel at `anchor_x + 10` | 764–765 |
| G12.RE-09 | Anchor yscale=10 quadruples rel Y offset | type=1, yscale=10 | rel attr1=+3 | pixel at `anchor_y + 12` | 770 |
| G12.RE-10 | Anchor xscale=11 × 8 | — | rel attr0=+2 | `+16` | 767 |
| G12.RT-01 | Type 0 relative: own mirror/rotate used directly | anchor type=0 | rel attr2 xmirror=1 | relative mirrored but anchor not | 782–783 |
| G12.RT-02 | Type 1 relative: `mirror = anchor XOR rel` | anchor xmirror=1, rel xmirror=1 | — | effective xmirror=0 | 783 |
| G12.RT-03 | Type 1 relative: `rotate = anchor XOR rel` | anchor rot=1, rel rot=0 | — | effective rotate=1 | 783 |
| G12.RT-04 | Type 1 relative scale from anchor, not relative | anchor xscale=10 (4×), rel xscale=00 | — | effective xscale=10 | 786 |
| G12.RP-01 | Rel pattern without add (attr4(0)=0): uses own name | rel attr3(5:0)=0x05 | — | pattern 0x05 | 803–804 |
| G12.RP-02 | Rel pattern with add (attr4(0)=1): anchor_pattern + rel pattern | anchor_pattern=0x03, rel=0x05 | — | pattern 0x08 | 803 |
| G12.RP-03 | Rel pattern with N6 bit (from rel's attr4(6) AND anchor_h) | anchor H=1, rel attr4(6)=1 | — | N6=1 in formed pattern | 802 |
| G12.RP-04 | 4bpp relative inherits H from anchor (`anchor_h`) | anchor H=1 | — | relative's `spr_rel_attr_4(7) = anchor_h` | 785 |

**Negative/boundary rows:**

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G12.NG-01 | Relative sprite with no prior anchor ⇒ `anchor_*` all zero (power-on) | fresh reset | first sprite in list is relative | renders at (0,0) with paloff=attr2(7:4) and scale 1× | 893–897, 766–773 |
| G12.NG-02 | Two consecutive anchors: second replaces first | anchor0 at (100,100), anchor1 at (50,50), then rel | rel renders relative to anchor1 | pixel near (50,50) | 929 |
| G12.NG-03 | Invisible anchor between visible anchor and relative leaves the visible anchor intact | anchor0 visible (attr3(6)=1), then 4-byte sprite (no anchor update), then rel | rel uses anchor0 | — | 929 |

### Group 13 — Status Register, Collision, Per-Line Limit

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G13.CO-01 | No overlap ⇒ collision bit stays 0 | two non-overlapping sprites | render, read 0x303B | status bit 0 = 0 | 991 |
| G13.CO-02 | Two opaque sprites overlap ⇒ bit 0 = 1 | overlap at x=50 | render, read | 0x01 | 991 |
| G13.CO-03 | Collision fires even when `zero_on_top=1` blocks the write | z_on_top=1, both opaque | — | bit 0 still 1 (fires on `spr_line_we AND line_data_o(8)`) | 991 |
| G13.CO-04 | Transparent pixel does not count (spr_line_we=0) | second sprite transparent at overlap | — | bit 0 = 0 | 971, 991 |
| G13.CO-05 | Read of 0x303B clears status | after CO-02 | read again | 0x00 | 986–988 |
| G13.CO-06 | Collision bit is sticky across frames until read | overlap on frame N, no overlap on frame N+1 | read on frame N+2 | still 0x01 | 986–991 |
| G13.OT-01 | Few sprites ⇒ `state_s` returns to S_IDLE before next `line_reset_re="01"` ⇒ bit 1 = 0 | 4 small sprites | render line, read | 0x00 | 977 |
| G13.OT-02 | 128 visible anchors all on same Y, 1× scale ⇒ overtime | — | render, read | bit 1 = 1 | 977 |
| G13.OT-03 | Overtime bit independent of collision bit | stress without overlap | read | 0x02 | 977, 991 |
| G13.OT-04 | Both flags can accumulate | stress + overlap | read | 0x03 | 990–991 |
| G13.SR-01 | Status bits 7:2 always 0 | any | read | `status & 0xFC = 0` | 975–995 |
| G13.SR-02 | Read captures then clears in same cycle | — | — | returned value non-zero, next read 0 | 986–988 |
| G13.SR-03 | Status bits update via OR while unread | trigger two overlaps in one frame | — | bit 0 stays 1 | 991 |

### Group 14 — Reset Defaults (observable)

The prior RST-04 and RST-05 asserted `true`; the new rows observe real
VHDL-driven state via outputs.

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G14.RST-01 | `anchor_vis` cleared on reset | — | inspect via first relative sprite after reset | relative invisible | 888, 784 |
| G14.RST-02 | `spr_cur_index` reset to 0 | — | first sprite processed on first line after reset is index 0 | observed via FSM path | 876, 898 |
| G14.RST-03 | `status_reg_s` and `status_reg_read` zeroed | — | read 0x303B after reset | 0x00 | 982–984 |
| G14.RST-04 | `mirror_sprite_q` zeroed | — | `mirror_num_o` after reset | 0x00 | 598–599, 614 |
| G14.RST-05 | `line_buf_sel` starts at 0 | — | first line writes to linebuf0 when `line_buf_sel=0` | 534–550 |
| G14.RST-06 | `attr_index` and `pattern_index` zeroed | — | write to 0x57 without any 0x303B ⇒ lands in sprite 0 attr 0 | 651–652, 731–732 |

### Group 15 — Boundary / Negative

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| G15.NG-01 | Pattern index 64..255 inaccessible via attr3 | attr3 is 6 bits | — | only `attr3(5:0)` reaches pattern select, N6 from attr4 is needed for >63 | 804 |
| G15.NG-02 | Sprite fully off-screen (x=500, y=500) produces zero writes | x from attr2(0)&attr0, y from attr4(0)&attr1 | — | state walks through S_QUALIFY→next immediately | 842 |
| G15.NG-03 | Sprite at `(x=0, y=0)` with `attr3(7)=1, attr3(6)=0` (no 5th byte) ⇒ forces 1× scale and y_msb=0 | — | — | sprite still renders normally | 796, 907, 919 |
| G15.NG-04 | Palette offset wrap: `paloff=0xF, pat(7:4)=0x1` ⇒ (0xF+0x1)&0xF = 0 | — | — | low byte = 0x0N | 968 |
| G15.NG-05 | Zero-size pattern (all bytes = transp colour) ⇒ zero pixels written but FSM still advances to next sprite | — | — | collision bit stays 0, no OT | 971 |
| G15.NG-06 | Relative sprite whose computed `spr_rel_x3(8)=1` but attr3(6)=0 — **impossible** because relatives require attr3(6)=1; document as unreachable | — | — | — | 756 |
| G15.NG-07 | Negative offset wraps in 9-bit arithmetic: anchor_x=5, rel x0=0xF0 (signed −16) ⇒ pixel at x=`(5-16) mod 512 = 0x1F5` (off-screen) | — | — | no pixel, FSM qualifies out | 762, 772 |

## Test Implementation Strategy

The goal is a **headless unit harness** that instantiates the emulator's
`SpriteEngine` with a mock line-buffer, drives it one scanline at a time,
and exposes:

1. `upload_pattern(index, bytes[256])` → writes via 0x5B path.
2. `set_attrs(sprite_idx, bytes[5])` → writes via 0x57 path or mirror path
   (both modes tested in Group 1).
3. `render_scanline(vcounter) → LineBuffer[320]` where each entry is
   `{bit8_marker, low8_pixel}`.
4. `render_pixel(hcounter, vcounter) → {enable, pixel8}` matching the
   `rgb_o`/`pixel_en_o` combinational contract.
5. `read_status_303B() → uint8_t` with the same "read clears" side effect.

Every test row above has a concrete expected pixel or register value.
There is no `check(..., true)`. No row tests "does not crash".

### Ban list (enforced at code review)

- `check(id, true)` — hard fail.
- `check(id, a == a)` — hard fail.
- `check(id, a == b || a != b)` — hard fail.
- Rows whose only stimulus is calling a setter and whose only assertion is
  reading the same setter back — hard fail. Every row must cross a VHDL
  boundary (pattern RAM, line buffer, FSM state observable via outputs, or
  status register).

### Test counts (target)

| Group | Rows |
|-------|-----:|
| G1 Attr port 0x57 / mirror | 12 |
| G2 Pattern port 0x5B | 5 |
| G3 Pixel decoding + transparency | 12 |
| G4 Position / 9-bit / edges | 7 |
| G5 Visibility | 5 |
| G6 Clip window | 7 |
| G7 Priority (zero_on_top) | 4 |
| G8 (folded into G3) | 0 |
| G9 Mirror / rotate | 8 |
| G10 Scaling | 10 |
| G11 Over border | 4 |
| G12 Anchor / relative / type / pattern / negative | 22 |
| G13 Status / collision / overtime | 13 |
| G14 Reset defaults | 6 |
| G15 Boundary / negative | 7 |
| **Total** | **122** |

There is no honest target of "100% pass" until every row above has been
implemented against the VHDL oracle. Until then, the published status
line must read "`N/122` passing, `M` VHDL-derived, `K` stub". A stub row
counts as 0 in the N column and must carry a `TODO(sprites): stub` marker
in code.

## Open Questions

These are the VHDL ambiguities identified while deriving the plan. They
must be resolved (in the implementation phase) by additional reading of
`zxnext.vhd` or the Next TRM before the corresponding rows can be finalised.

1. **Per-line cycle budget for OT-02.** `sprites.vhd` does not define how
   many `clock_master_i` cycles fall between two successive
   `line_reset_re="01"` pulses — that is set in `zxnext.vhd` by the pixel
   clock / vcount divider. The exact minimum number of on-line sprites that
   produces `sprites_overtime='1'` therefore cannot be stated from
   sprites.vhd alone. The test row asserts the "128 max-width opaque
   sprites" case, which is the VHDL worst case, but a tighter bound needs
   top-level analysis.
2. **Does `spr_pat_re` gate pattern reads?** Declared at line 172 but never
   driven in the file. Either dead signal or connected in a coregen wrapper.
   If dead, remove from any oracle comments.
3. **Line buffer clearing cadence.** Lines 1020–1033 show the video path
   writes zeros after reading the pixel on `clock_pixel_i`. Whether this
   clears **bit 8** as well (the priority marker) needs confirmation via
   `spram_320_9` width: it's declared 9 bits (line 112), so `video_line_data_s
   = (others => '0')` at line 1033 clears all 9 bits. Therefore bit 8 is
   cleared each scanline — rows G7.PR-03 and G13.CO-06 depend on this.
4. **Mirror write priority edge.** `mirror_served` blocks `cpu_served` in
   the same cycle (line 705), but when both arrive within the same
   `io_port_re` window it is not 100% clear from VHDL whether the CPU write
   is lost or deferred. Rows G1.AT-12 assume "lost" (consistent with
   `cpu_request` being cleared only on `cpu_served=1`, lines 691–702).
5. **Exact semantics of `anchor_rel_type`'s type-0 passthrough for
   `spr_rel_attr_2(3:1)`.** Line 782 uses `sprite_attr_2(3 downto 1)` as
   the raw pass-through, while line 783 for type 1 uses the XOR'd version;
   G12.RT-01 assumes type 0 ignores the `spr_rel_xm/ym` wires entirely for
   the output attribute 2 bits 3:1. Needs waveform confirmation.

## File Layout

```
test/
  sprites/
    sprites_test.cpp           # C++ unit harness (REWRITE IN FOLLOWING PHASE)
    oracles/
      vhdl_formulas.h          # OF1..OF12 encoded as pure functions
      pattern_fixtures.h       # deterministic patterns with unique bytes
doc/design/
  SPRITES-TEST-PLAN-DESIGN.md  # this document
```

## How to Run (post-implementation)

```bash
cmake --build build -j$(nproc)
./build/test/sprites_test
```

Until every row is implemented, the test runner MUST print both the
`passed / total` and the `implemented / total` counts. A row that is
`stub` prints `[STUB]` and does not count as passed.
