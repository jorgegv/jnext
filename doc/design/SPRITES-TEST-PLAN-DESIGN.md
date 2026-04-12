# Sprites Subsystem — VHDL-Derived Compliance Test Plan

Systematic compliance test plan for the sprites subsystem of the JNEXT ZX
Spectrum Next emulator, derived exclusively from the authoritative VHDL source
(`sprites.vhd` and `zxnext.vhd`).

## Purpose

The ZX Spectrum Next sprite engine supports 128 hardware sprites with 64
patterns, 4-bit and 8-bit colour modes, anchor/relative sprite hierarchies,
scaling, rotation, mirroring, clip windows, and sprite-over-border rendering.
This test plan verifies the emulator matches the VHDL behaviour across all these
features.

## Architecture

### VHDL Constants (from `sprites.vhd`)

| Constant             | Value | Notes                          |
|----------------------|------:|--------------------------------|
| SPRITE_SIZE_BITS     |     4 | Sprites are 16x16 pixels       |
| SPRITE_SIZE          |    16 | 2^4                            |
| TOTAL_SPRITES_BITS   |     7 | 128 sprites total               |
| TOTAL_SPRITES        |   128 | 2^7                            |
| TOTAL_PATTERN_BITS   |     6 | 64 different patterns           |
| TOTAL_PATTERNS       |    64 | 2^6                            |
| Pattern memory       |  16KB | 14-bit address space            |
| Line buffer          |   320 | 9-bit entries (8-bit + flag)    |

### Attribute Bytes (5 per sprite)

From the VHDL memory declarations and comments:

| Attr | Bits    | Field                                                   |
|------|---------|---------------------------------------------------------|
| 0    | 7:0     | X position (low 8 bits)                                 |
| 1    | 7:0     | Y position (low 8 bits)                                 |
| 2    | 7:4     | Palette offset                                          |
| 2    | 3       | X mirror                                                |
| 2    | 2       | Y mirror                                                |
| 2    | 1       | Rotate flag                                             |
| 2    | 0       | X MSB (bit 8)                                           |
| 3    | 7       | Visible flag                                            |
| 3    | 6       | Fifth attribute byte follows (enables extended mode)    |
| 3    | 5:0     | Pattern name (N5:N0), index into pattern memory 0-63   |
| 4    | 7       | 4-bit pattern mode (H flag) — only if attr3 bit6=1     |
| 4    | 6       | Pattern name bit 6 (N6) — only if H=1                  |
| 4    | 5       | Relative type (composite type) — only if attr3 bit6=1  |
| 4    | 4:3     | X scale (00=1x, 01=2x, 10=4x, 11=8x)                  |
| 4    | 2:1     | Y scale (00=1x, 01=2x, 10=4x, 11=8x)                  |
| 4    | 0       | Y MSB (bit 8) — only if attr3 bit6=1                   |

### I/O Ports

| Port   | Dir | Function                                        |
|--------|-----|-------------------------------------------------|
| 0x57   | W   | Write sprite attributes (auto-incrementing)     |
| 0x5B   | W   | Write sprite pattern data (auto-incrementing)   |
| 0x303B | W   | Set current sprite index (attr + pattern)       |
| 0x303B | R   | Read sprite status register                     |

### NextREGs

| Reg  | Bits  | Field                          | Reset    |
|------|-------|--------------------------------|----------|
| 0x09 | 4     | Sprite/IO tie                  | 0        |
| 0x15 | 6     | Sprite priority (zero on top)  | 0        |
| 0x15 | 5     | Sprite border clip enable      | 0        |
| 0x15 | 4:2   | Layer priority                 | 000      |
| 0x15 | 1     | Sprite over border enable      | 0        |
| 0x15 | 0     | Sprite enable                  | 0        |
| 0x19 | -     | Sprite clip window (4 writes)  | 0,FF,0,BF |
| 0x34 | -     | Sprite attribute mirror        | -        |
| 0x43 | 3     | Active sprite palette          | 0        |
| 0x4B | 7:0   | Sprite transparent index       | 0xE3     |

## Test Case Catalog

### Group 1: Pattern Loading (8-bit mode)

Tests for writing pattern data via port 0x5B with 8-bit (non-4-bit) patterns.
Pattern memory is 16KB (14-bit address). Each 8-bit pattern is 256 bytes
(16x16). The pattern index auto-increments after each byte write.

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| PL-01 | Write 256 bytes to pattern 0                 | Pattern 0 memory contains correct data              |
| PL-02 | Write to pattern 63 (last)                   | Last valid pattern slot populated correctly          |
| PL-03 | Auto-increment across pattern boundary       | Index wraps from pattern N byte 255 to pattern N+1  |
| PL-04 | Set sprite index via 0x303B, write patterns  | Pattern index set from cpu_d_i(5:0) & cpu_d_i(7)    |
| PL-05 | Port 0x303B bit 7 maps to pattern index(7)   | Half-pattern offset for 4-bit addressing            |

### Group 2: Pattern Loading (4-bit mode)

4-bit patterns use half the memory per pattern (128 bytes for 16x16). The
pattern address remapping is: `addr(13:8) & N6 & addr(7:1)` where N6 is from
attr4 bit 6.

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| P4-01 | Load 4-bit pattern, upper nibble selection   | `spr_pat_data(7:4)` when `addr(0)=0`               |
| P4-02 | Load 4-bit pattern, lower nibble selection   | `spr_pat_data(3:0)` when `addr(0)=1`               |
| P4-03 | 4-bit pixel output with palette offset       | Output = `paloff(3:0) & nibble(3:0)`                |
| P4-04 | 8-bit pixel output with palette offset       | Output = `(pat(7:4)+paloff) & pat(3:0)`             |
| P4-05 | 4-bit transparency check against low nibble  | Compare nibble vs `transp_colour_i(3:0)` only       |
| P4-06 | 8-bit transparency check against full byte   | Compare full byte vs `transp_colour_i(7:0)`         |

### Group 3: Attribute Registers via Port 0x57

The attribute index auto-increments with each write to port 0x57. The index
is `sprite_num(6:0) & attr_sub(2:0)`. It skips from attr 3 to the next
sprite (incrementing by sprite number) when attr3 bit6=0 (no 5th byte).

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| AT-01 | Write 4 attrs (no 5th byte), auto-skip       | After writing attr3 with bit6=0, index jumps to next sprite attr0 |
| AT-02 | Write 5 attrs (with 5th byte), auto-advance  | After writing attr4, index advances to next sprite attr0 |
| AT-03 | Set sprite number via 0x303B                 | Attr index = `cpu_d_i(6:0) & "000"`                |
| AT-04 | Attr0 stores X low byte correctly            | sprite_attr_0 matches written value                |
| AT-05 | Attr1 stores Y low byte correctly            | sprite_attr_1 matches written value                |
| AT-06 | Attr2 stores palette/mirror/rotate/Xmsb      | Each bitfield independently verifiable              |
| AT-07 | Attr3 stores visible/has5th/pattern           | Visible, 5th-byte flag, name(5:0) correct          |
| AT-08 | Attr4 stores H/N6/type/Xscale/Yscale/Ymsb    | All extended fields correct                        |
| AT-09 | Write to sprite 127 (last sprite)            | Boundary: highest sprite number                    |
| AT-10 | Index wrap from sprite 127 to sprite 0       | Auto-increment wraps within 7-bit range            |

### Group 4: Attribute Registers via NextREG 0x34 (Mirror)

NextREG mirror provides an alternative path to write sprite attributes.
Priority: mirror writes take precedence over CPU port writes when both arrive
simultaneously.

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| MR-01 | Write attrs 0-4 via NextREG mirror            | Same result as port 0x57                          |
| MR-02 | Set sprite number via mirror index "111"       | `mirror_sprite_q` updated to specified number     |
| MR-03 | Mirror auto-increment sprite number            | When `mirror_inc_i=1`, sprite number increments   |
| MR-04 | Mirror/IO tie (NR 0x09 bit 4)                 | When tied, attr index follows mirror changes      |
| MR-05 | Mirror priority over CPU port                  | `mirror_served` blocks `cpu_served`               |

### Group 5: Sprite Visibility

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| VIS-01 | Sprite with visible=1 renders                | Pixel output present in line buffer                |
| VIS-02 | Sprite with visible=0 does not render         | No pixel written to line buffer                   |
| VIS-03 | Sprite enable (NR 0x15 bit 0) = 0            | All sprites disabled globally                     |
| VIS-04 | Sprite enable = 1, individual visible = 1     | Sprite renders                                    |
| VIS-05 | Y position off-screen (no scanline match)     | Sprite not drawn on current line                  |
| VIS-06 | X position off-screen (>= 320)               | `spr_cur_hcount_valid` = 0, no write              |

### Group 6: Clip Window

The clip window behaves differently based on `over_border_i`:

**Over border mode (`over_border_i=1`, NR 0x15 bit 1):**
- If `border_clip_en=0`: full 320x256 area (0..319, 0..255)
- If `border_clip_en=1`: `x1*2..x2*2+1, y1..y2`

**Non-over-border mode (`over_border_i=0`):**
- X: `((0,x1(7:5))+1) & x1(4:0)` to `((0,x2(7:5))+1) & x2(4:0)`
- Y: `((0,y1(7:5))+1) & y1(4:0)` to `((0,y2(7:5))+1) & y2(4:0)`

Reset defaults: x1=0x00, x2=0xFF, y1=0x00, y2=0xBF.

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| CL-01 | Default clip window, sprite fully inside       | Sprite renders all pixels                         |
| CL-02 | Set narrow clip window, sprite partially inside| Only pixels within clip bounds appear             |
| CL-03 | Sprite fully outside clip window               | No pixels rendered                                |
| CL-04 | Over-border mode with border_clip_en=0         | Full 320x256 clip area                            |
| CL-05 | Over-border mode with border_clip_en=1         | Clip coordinates doubled for X: `x1*2..x2*2+1`   |
| CL-06 | Non-over-border clip coordinate transform      | Top 3 bits incremented, lower 5 bits preserved    |
| CL-07 | Clip window write cycling (NR 0x19)            | 4 sequential writes cycle x1,x2,y1,y2 then wrap  |
| CL-08 | Reset clip index (NR 0x1C bit 1)               | Sprite clip index resets to 0                     |

### Group 7: Sprite Ordering and Priority

Sprites are processed from index 0 to 127. Each sprite writes its pixel to
the line buffer. The `zero_on_top_i` signal (NR 0x15 bit 6) controls whether
lower-numbered sprites overwrite higher-numbered ones.

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| PR-01 | Two overlapping sprites, zero_on_top=0        | Higher-numbered sprite's pixel wins (last write)  |
| PR-02 | Two overlapping sprites, zero_on_top=1        | Lower-numbered sprite's pixel wins                |
| PR-03 | zero_on_top mechanism via line buffer bit 8    | `spr_line_we_s` blocked if `zero_on_top=1` and `spr_line_data_o(8)=1` |
| PR-04 | First sprite pixel always writes (bit8=0)      | Line buffer bit 8 starts at 0 (cleared each line)|

### Group 8: Layer Priority (Compositor)

NR 0x15 bits 4:2 set layer priority order. The VHDL defines 8 modes:

| Value | Order        | Notes                                     |
|-------|-------------|-------------------------------------------|
| 000   | S L U       | Sprites on top                            |
| 001   | L S U       | Layer2 on top                             |
| 010   | S U L       | Sprites on top, ULA middle                |
| 011   | L U S       | Layer2 on top, sprites behind ULA         |
| 100   | U S L       | ULA on top                                |
| 101   | U L S       | ULA on top, sprites behind all            |
| 110   | (U|T)S(T|U)(B+L)  | Mix mode with additive blend       |
| 111   | (U|T)S(T|U)(B+L-5)| Mix mode with subtractive blend    |

Special case in modes 011, 100, 101: border pixels do not obscure sprites
(`ula_border_2=1 and tm_transparent=1 and sprite_transparent=0` bypasses ULA).

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| LP-01 | Priority 000 (SLU): sprite visible over L2    | Sprite pixel wins when all layers opaque          |
| LP-02 | Priority 001 (LSU): L2 covers sprite           | Layer2 pixel wins when all opaque                 |
| LP-03 | Priority 010 (SUL): sprite over ULA, under L2  | Correct ordering with transparent combinations    |
| LP-04 | Priority 011 (LUS): sprite behind ULA           | Sprite only visible when L2 and ULA transparent   |
| LP-05 | Priority 100 (USL): ULA covers sprite           | ULA on top                                        |
| LP-06 | Priority 101 (ULS): sprite behind everything     | Sprite only when ULA+L2 transparent              |
| LP-07 | Border bypass in mode 011                       | Sprite shows through border even when ULA "opaque"|
| LP-08 | Border bypass in mode 100                       | Same bypass logic                                 |
| LP-09 | Border bypass in mode 101                       | Same bypass logic                                 |
| LP-10 | Mix mode 110: sprite between mix layers          | Sprite renders between top and bottom mix layers  |
| LP-11 | Mix mode 111: subtractive blend                  | Correct clamping (min 0, max 7 per channel)       |

### Group 9: Transparency

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| TR-01 | Default transparent index (0xE3)              | Pixels matching 0xE3 not written to line buffer   |
| TR-02 | Custom transparent index via NR 0x4B          | Changed index respected                           |
| TR-03 | 8-bit mode: full byte comparison               | `spr_pat_data(7:0) != transp_colour_i`            |
| TR-04 | 4-bit mode: nibble comparison                  | `spr_nibble_data != transp_colour_i(3:0)`         |
| TR-05 | Non-transparent pixel writes to line buffer    | `spr_line_we=1`                                   |
| TR-06 | Transparent pixel skips line buffer write       | `spr_line_we=0`                                   |

### Group 10: X/Y Position and 9-bit Coordinates

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| XY-01 | Sprite at X=0, Y=0                           | Top-left corner, renders on scanline 0            |
| XY-02 | Sprite with X MSB (attr2 bit 0) set          | Full 9-bit X = `{attr2(0), attr0(7:0)}`           |
| XY-03 | Sprite with Y MSB (attr4 bit 0) set          | Full 9-bit Y = `{attr4(0), attr1(7:0)}` (only with 5th attr byte) |
| XY-04 | Y MSB is 0 when attr3 bit 6 = 0              | 4-byte mode forces Y to 8-bit range              |
| XY-05 | Sprite at X=319 (rightmost visible)           | Renders; X=320 would be `hcount_valid=0`          |
| XY-06 | Sprite wrapping around X boundary              | X wrap-around allowed per `spr_cur_x_wrap` mask   |

### Group 11: Mirroring

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| MI-01 | No mirror: normal pixel order                 | Left-to-right, top-to-bottom                      |
| MI-02 | X mirror (attr2 bit 3)                        | Horizontal flip: pattern addr delta = -1 (no rotate) or -16 (rotate) |
| MI-03 | Y mirror (attr2 bit 2)                        | Y index complemented: `not(spr_y_offset(3:0))`   |
| MI-04 | Both X and Y mirror                           | Combined flip                                     |
| MI-05 | X mirror effective = xmirror XOR rotate        | Rotation inverts the X mirror sense               |

### Group 12: Rotation

Rotation swaps X and Y axes in the pattern address calculation.

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| RO-01 | No rotation: addr = `pattern & y_index & x_index` | Normal row-major layout                        |
| RO-02 | Rotation (attr2 bit 1): addr = `pattern & x_index & y_index` | Column-major layout                  |
| RO-03 | Rotation + X mirror: delta = -16              | Pattern traversed in reverse column order         |
| RO-04 | Rotation + no X mirror: delta = +16           | Pattern traversed in forward column order         |
| RO-05 | Rotation + Y mirror                           | Y index complement applied to swapped axis        |

### Group 13: Scaling

X scale (attr4 bits 4:3) and Y scale (attr4 bits 2:1) each support 1x, 2x,
4x, 8x. Scaling is only available when the 5th attribute byte is present
(attr3 bit 6 = 1). When attr3 bit 6 = 0, scale defaults to 1x.

X scaling uses a fractional counter (`spr_width_count_delta`):

| Scale | Delta | Effect                        |
|-------|-------|-------------------------------|
| 1x    | 8     | Advance pattern every pixel   |
| 2x    | 4     | Advance pattern every 2 pixels|
| 4x    | 2     | Advance pattern every 4 pixels|
| 8x    | 1     | Advance pattern every 8 pixels|

Y scaling divides `spr_y_offset_raw` via arithmetic right shift:

| Scale | Shift | Effective sprite height       |
|-------|-------|-------------------------------|
| 1x    | 0     | 16 pixels                     |
| 2x    | 1     | 32 pixels                     |
| 4x    | 2     | 64 pixels                     |
| 8x    | 3     | 128 pixels                    |

X wrap mask for scaled sprites:

| Scale | Wrap mask | Max X span |
|-------|-----------|------------|
| 1x    | 11111     | 16 pixels  |
| 2x    | 11110     | 32 pixels  |
| 4x    | 11100     | 64 pixels  |
| 8x    | 11000     | 128 pixels |

| ID    | Test                                         | Verify                                             |
|-------|----------------------------------------------|-----------------------------------------------------|
| SC-01 | X scale 1x: pattern advances every pixel      | `delta=8`, 16 pixels wide                         |
| SC-02 | X scale 2x: every 2 pixels                    | `delta=4`, 32 pixels wide                         |
| SC-03 | X scale 4x: every 4 pixels                    | `delta=2`, 64 pixels wide                         |
| SC-04 | X scale 8x: every 8 pixels                    | `delta=1`, 128 pixels wide                        |
| SC-05 | Y scale 1x: 16 scanlines                      | Y offset unshifted                                |
| SC-06 | Y scale 2x: 32 scanlines                      | Y offset >> 1 (arithmetic)                        |
| SC-07 | Y scale 4x: 64 scanlines                      | Y offset >> 2 (arithmetic)                        |
| SC-08 | Y scale 8x: 128 scanlines                     | Y offset >> 3 (arithmetic)                        |
| SC-09 | No 5th byte (attr3 bit6=0): forced 1x         | Scale bits ignored, delta=8, no Y shift           |
| SC-10 | Combined X=4x, Y=2x                           | 64 wide, 32 tall                                  |
| SC-11 | X wrap-around with 2x scale                    | Wrap mask = 11110                                 |

### Group 14: Anchor Sprites

An anchor sprite is a non-relative sprite (attr3 bit6=1, attr4 bits 7:6 != 01).
It establishes the reference frame for subsequent relative sprites.

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| AN-01 | Anchor stores position                        | `anchor_x`, `anchor_y` set from sprite X,Y       |
| AN-02 | Anchor stores pattern number                  | `anchor_pattern` set from `spr_rel_pattern`       |
| AN-03 | Anchor stores palette offset                  | `anchor_paloff` from attr2(7:4)                   |
| AN-04 | Anchor with type=1 (attr4 bit5) stores transforms | `anchor_rotate`, `anchor_xmirror`, `anchor_ymirror`, `anchor_xscale`, `anchor_yscale` stored |
| AN-05 | Anchor with type=0: transforms zeroed          | Mirror/rotate/scale reset to identity             |
| AN-06 | Anchor visibility propagates                  | `anchor_vis` = attr3 bit 7                        |
| AN-07 | Non-anchor (attr3 bit6=0): no anchor update    | Previous anchor values preserved                  |

### Group 15: Relative Sprites

A relative sprite has attr3 bit6=1 and attr4 bits 7:6 = "01". Its position,
palette, and optionally its transforms are derived from the most recent anchor.

**Position calculation (from VHDL):**
1. If anchor has rotation: swap X/Y offsets (`spr_rel_x0/y0`)
2. Apply anchor's X mirror: negate X if `anchor_rotate XOR anchor_xmirror`
3. Apply anchor's Y mirror: negate Y if `anchor_ymirror`
4. Apply anchor's scale to offsets (shift left by 0/1/2/3)
5. Add anchor position

**Type 0 (individual transforms):**
- Palette offset: directly from attr2(7:4) if attr2(0)=0, else anchor_paloff + attr2(7:4)
- Mirror/rotate/scale from relative sprite's own attributes

**Type 1 (unified/composite transforms):**
- Palette offset: same as type 0
- Mirror = anchor XOR relative
- Rotate = anchor XOR relative
- Scale = anchor's scale (overrides relative)

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| RE-01 | Relative sprite offset from anchor (no transforms) | Position = anchor + signed offset              |
| RE-02 | Relative sprite inherits anchor visibility     | `anchor_vis AND sprite_attr_3(7)`                 |
| RE-03 | Anchor invisible: relative invisible           | Both hidden                                       |
| RE-04 | Relative palette offset (attr2 bit0=0)         | Direct palette offset                             |
| RE-05 | Relative palette offset (attr2 bit0=1)         | Anchor paloff + relative paloff                   |
| RE-06 | Anchor with rotation: X/Y offsets swapped      | Offset X reads from attr1, Y from attr0           |
| RE-07 | Anchor with X mirror: negate X offset          | `rotate XOR xmirror` controls negation            |
| RE-08 | Anchor with Y mirror: negate Y offset          | Y offset negated                                  |
| RE-09 | Anchor 2x X scale: offset doubled              | Offset shifted left by 1                          |
| RE-10 | Anchor 4x Y scale: offset quadrupled          | Offset shifted left by 2                          |
| RE-11 | Anchor 8x scale: offset multiplied by 8        | Offset shifted left by 3                          |

### Group 16: Relative Sprite Type 0 vs Type 1

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| RT-01 | Type 0: relative has own mirror/rotate/scale   | attr2 bits 3,2,1 and attr4 bits 4:3, 2:1 used directly |
| RT-02 | Type 1: mirror = anchor XOR relative           | `anchor_xmirror XOR spr_rel_xm`                  |
| RT-03 | Type 1: rotate = anchor XOR relative           | `anchor_rotate XOR attr2(1)`                      |
| RT-04 | Type 1: scale from anchor (not relative)       | `anchor_xscale`, `anchor_yscale` override         |
| RT-05 | Type 0: X mirror effective (with rotation)     | `spr_rel_xm` = attr2(3) when no anchor rotate, else attr2(2) XOR attr2(1) |
| RT-06 | Type 0: Y mirror effective (with rotation)     | `spr_rel_ym` = attr2(2) when no anchor rotate, else attr2(3) XOR attr2(1) |

### Group 17: Relative Sprite Pattern Number

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| RP-01 | Relative pattern with N6 bit                   | Pattern = `attr3(5:0) & (attr4(6) AND H)`        |
| RP-02 | Pattern + anchor (attr4 bit0=1)                | `spr_rel_pattern + anchor_pattern`                |
| RP-03 | Pattern without anchor add (attr4 bit0=0)      | `spr_rel_pattern` used directly                   |
| RP-04 | 4-bit relative sprite                          | H=1, pattern address remapped for 4-bit          |

### Group 18: Per-Line Sprite Limit (Overtime)

The sprite engine processes sprites sequentially. If processing hasn't
finished by the time the next scanline begins (`line_reset_re="01"` while
`state_s /= S_IDLE`), the overtime flag is set.

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| OT-01 | Few sprites: no overtime                       | Status bit 1 = 0                                  |
| OT-02 | Many sprites on one line: overtime triggered    | Status bit 1 = 1                                  |
| OT-03 | Status register read clears overtime flag       | After 0x303B read, status_reg_s resets to 0       |
| OT-04 | Status register latches until read              | Flag persists across frames until read            |

### Group 19: Collision Detection

A collision occurs when a sprite pixel is written to a line buffer position
that already has a sprite pixel (bit 8 = 1).

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| CO-01 | No overlap: no collision                       | Status bit 0 = 0                                  |
| CO-02 | Two sprites overlap: collision detected         | Status bit 0 = 1                                  |
| CO-03 | Collision flag latches until read               | `status_reg_s(0) OR (spr_line_data_o(8) AND spr_line_we)` |
| CO-04 | Reading 0x303B clears collision flag            | `status_reg_s <= (others => '0')` after read      |
| CO-05 | Status read returns latched value               | `status_reg_read` captures then clears source     |

### Group 20: Sprite Over Border

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| OB-01 | over_border=0: sprites only in display area    | Clip window coordinates use offset formula        |
| OB-02 | over_border=1: sprites render over border      | Full 320x256 area or clipped per clip window      |
| OB-03 | over_border=1 + clip_en=0: no clipping         | 0..319 x 0..255                                   |
| OB-04 | over_border=1 + clip_en=1: clipped             | `clip_x1*2..clip_x2*2+1, clip_y1..clip_y2`       |

### Group 21: Line Buffer and Double Buffering

The sprite engine uses two line buffers that alternate each scanline. One
buffer is filled by the sprite engine while the other is read for display.

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| LB-01 | Buffer swap on line_reset                      | `line_buf_sel` toggles on `line_reset_re = "01"`  |
| LB-02 | Display buffer cleared after read              | Write zeros to display buffer after pixel output  |
| LB-03 | Sprite buffer retains data until swap          | Pixels from current line available next line      |
| LB-04 | vcount for sprite processing = vcounter + 1    | Sprites rendered one line ahead                   |

### Group 22: Port 0x303B Read (Status Register)

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| SR-01 | Bits 7:2 always 0                              | Only bits 1 and 0 used                           |
| SR-02 | Read captures current status                   | `status_reg_read <= status_reg_s`                 |
| SR-03 | Read clears internal status                    | `status_reg_s <= 0` after read                    |
| SR-04 | Multiple events accumulate (OR)                | Both collision and overtime can be set            |

### Group 23: Palette Selection

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| PA-01 | Active sprite palette 0 (NR 0x43 bit 3 = 0)   | Sprite pixels pass through palette 0             |
| PA-02 | Active sprite palette 1 (NR 0x43 bit 3 = 1)   | Sprite pixels pass through palette 1             |
| PA-03 | 8-bit palette offset applied to upper nibble   | `(pat(7:4) + paloff) & pat(3:0)`                 |
| PA-04 | 4-bit palette offset replaces upper nibble     | `paloff(3:0) & nibble(3:0)`                      |

### Group 24: State Machine

The VHDL sprite engine uses a 4-state FSM: S_IDLE, S_START, S_QUALIFY, S_PROCESS.

| ID    | Test                                          | Verify                                            |
|-------|-----------------------------------------------|---------------------------------------------------|
| SM-01 | S_IDLE: no processing until line reset         | Engine waits for new scanline                     |
| SM-02 | S_START: initializes sprite index to 0         | `spr_cur_index <= 0`, `anchor_vis <= 0`           |
| SM-03 | S_QUALIFY: skip invisible/off-screen sprites   | Advances to next sprite immediately               |
| SM-04 | S_QUALIFY -> S_PROCESS: visible on-screen       | Begins pixel drawing                              |
| SM-05 | S_PROCESS: draws pixels until width exhausted   | Width counter reaches limit, returns to QUALIFY   |
| SM-06 | S_QUALIFY with index=0: transition to IDLE      | All 128 sprites processed (wrapped)              |
| SM-07 | Pattern address advances based on width delta   | `spr_cur_pattern_addr += delta` when bit3 flips  |

## Test Implementation Strategy

### Approach: Z80 Assembly Test Programs

Each test group is implemented as a `.nex` or `.tap` program that:

1. Configures the sprite system via NextREGs and I/O ports
2. Loads patterns and attributes
3. Waits for a specific frame/scanline
4. Takes a screenshot or reads back status registers
5. Compares against expected output

### Screenshot Comparison

For visual tests (rendering, clipping, priorities), use the emulator's
`--delayed-screenshot` feature in headless mode and compare against
reference PNGs, following the same approach as the existing regression test
suite.

### Register-Level Tests

For non-visual tests (status register, auto-increment, index management),
a dedicated test runner can set up sprite state programmatically and verify
internal state without rendering.

### Recommended Priority

1. **High**: Groups 1-6 (pattern loading, attributes, visibility, clip) --
   foundation for all other tests
2. **High**: Groups 7-9 (priority, layer order, transparency) -- critical
   for correct display
3. **Medium**: Groups 10-13 (coordinates, mirroring, rotation, scaling) --
   transform correctness
4. **Medium**: Groups 14-17 (anchor/relative sprites) -- composite sprites
5. **Lower**: Groups 18-24 (overtime, collision, state machine internals) --
   edge cases and status

### Total Test Cases

| Group | Count |
|-------|------:|
| Pattern Loading (8-bit)       |  5 |
| Pattern Loading (4-bit)       |  6 |
| Attribute Registers (0x57)    | 10 |
| Attribute Registers (mirror)  |  5 |
| Visibility                    |  6 |
| Clip Window                   |  8 |
| Ordering/Priority             |  4 |
| Layer Priority                | 11 |
| Transparency                  |  6 |
| X/Y Position                  |  6 |
| Mirroring                     |  5 |
| Rotation                      |  5 |
| Scaling                       | 11 |
| Anchor Sprites                |  7 |
| Relative Sprites              | 11 |
| Relative Type 0 vs 1          |  6 |
| Relative Pattern Number       |  4 |
| Per-Line Limit (Overtime)     |  4 |
| Collision Detection           |  5 |
| Sprite Over Border            |  4 |
| Line Buffer                   |  4 |
| Status Register               |  4 |
| Palette Selection             |  4 |
| State Machine                 |  7 |
| **Total**                     | **~146** |

## File Layout

```
test/
  sprites/
    test_sprites_patterns.asm      # Groups 1-2
    test_sprites_attributes.asm    # Groups 3-4
    test_sprites_visibility.asm    # Group 5
    test_sprites_clip.asm          # Group 6
    test_sprites_priority.asm      # Groups 7-8
    test_sprites_transparency.asm  # Group 9
    test_sprites_transforms.asm    # Groups 10-13
    test_sprites_composite.asm     # Groups 14-17
    test_sprites_status.asm        # Groups 18-19, 22
    test_sprites_border.asm        # Group 20
    references/                    # Reference PNGs for screenshot tests
doc/design/
  SPRITES-TEST-PLAN-DESIGN.md     # This document
```

## How to Run

```bash
# Build all sprite test programs
make -C test/sprites all

# Run individual test in headless mode
./build/jnext --headless --machine-type next \
    --load test/sprites/test_sprites_patterns.nex \
    --delayed-screenshot /tmp/sprites_patterns.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5

# Compare against reference
diff <(sha256sum /tmp/sprites_patterns.png) \
     <(sha256sum test/sprites/references/sprites_patterns.png)

# Run full regression suite (includes sprite tests)
bash test/regression.sh
```
