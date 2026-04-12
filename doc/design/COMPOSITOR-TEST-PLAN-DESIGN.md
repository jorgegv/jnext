# Compositor Compliance Test Plan

VHDL-derived compliance test plan for the video compositor / layer mixing
subsystem of the JNEXT emulator, covering layer priority modes, transparency
detection, fallback colour, palette lookup, blend modes, stencil mode, and
the complete SLU (Sprite/Layer2/ULA) ordering logic.

## Purpose

Validate that the emulator's compositor matches the VHDL behaviour defined in
`zxnext.vhd` (lines ~6730-7415), specifically the video pipeline stages,
transparency comparisons, layer priority selection, blend/stencil modes,
and the layer2 priority promotion bit.

## Authoritative VHDL Source

`zxnext.vhd`:
- Lines 6730-6832: Pipeline stage 0 --- per-line parameter capture
- Lines 6834-6934: Pipeline stage 0.5 --- pixel data holding
- Lines 6936-7005: Pipeline stage 1 --- palette lookup (dual-port RAM)
- Lines 7006-7089: Pipeline stage 1.5 --- parameter propagation
- Lines 7091-7356: Pipeline stage 2 --- transparency, SLU ordering, blend
- Lines 7358-7415: Pipeline stage 3 --- output delay and blanking

## Architecture

### Video Pipeline Overview

The compositor operates as a multi-stage pipeline synchronized to different
clock domains (CLK_7, CLK_14, CLK_28). Pixel data flows through:

1. **Stage 0**: Capture per-line parameters (clip windows, layer enables, priority, transparency colour, fallback colour)
2. **Stage 1**: Palette lookup via dual-port RAM (ULA/TM share one RAM, L2/Sprite share another)
3. **Stage 2**: Transparency comparison, layer ordering, blend/stencil computation
4. **Stage 3**: Output delay (4 CLK_7 cycles) with blanking

### Layer Sources

| Layer | Pixel Source | Palette RAM | Transparent When |
|-------|-------------|-------------|-----------------|
| ULA | `ula_pixel_1` | ULA/TM shared | RGB matches global transparent OR clipped |
| Tilemap | `tm_pixel_1` | ULA/TM shared | pixel_en=0 OR (textmode AND RGB matches transparent) OR tm_en=0 |
| Layer 2 | `layer2_pixel_1` | L2/Sprite shared | RGB matches global transparent OR pixel_en=0 |
| Sprites | `sprite_pixel_1` | L2/Sprite shared | pixel_en=0 |
| LoRes | `lores_pixel_1` | ULA/TM shared (replaces ULA) | pixel_en=0 AND lores_en=0 |

## Test Case Catalog

### 1. Transparency Detection

From `zxnext.vhd` lines 7100-7123:

**ULA transparency** (line 7100):
```
ula_mix_transparent <= '1' when (ula_rgb_2(8:1) = transparent_rgb_2) or (ula_clipped_2 = '1')
```
Comparison uses upper 8 bits of the 9-bit palette output against the 8-bit
global transparent colour. The LSB (bit 0) is not compared.

**ULA final transparency** (line 7103):
```
ula_transparent <= '1' when (ula_mix_transparent = '1') or (ula_en_2 = '0')
```
ULA is also transparent when ULA is disabled via NR 0x68 bit 7.

**Tilemap transparency** (line 7109):
```
tm_transparent <= '1' when (tm_pixel_en_2 = '0') or 
    (tm_pixel_textmode_2 = '1' and tm_rgb_2(8:1) = transparent_rgb_2) or (tm_en_2 = '0')
```
In text mode, transparency is checked against palette output. In non-text
(attribute) mode, `pixel_en` determines transparency (opaque if tile pixel set).

**Sprite transparency** (line 7118):
```
sprite_transparent <= not sprite_pixel_en_2
```
Sprites use their own transparent index (NR 0x4B) checked before palette lookup.

**Layer 2 transparency** (line 7121):
```
layer2_transparent <= '1' when (layer2_rgb_2(8:1) = transparent_rgb_2) or (layer2_pixel_en_2 = '0')
```

| Test | Layer | Pixel Colour | Global Transparent | Expected |
|------|-------|-------------|-------------------|----------|
| TR-01 | ULA | 0xE3 | 0xE3 | Transparent |
| TR-02 | ULA | 0xE4 | 0xE3 | Opaque |
| TR-03 | ULA | Any | Any (clipped) | Transparent |
| TR-04 | ULA | 0xE3 | 0xE3, ula_en=0 | Transparent (double) |
| TR-05 | ULA | 0x42 | 0xE3, ula_en=0 | Transparent (disabled) |
| TR-06 | TM text | 0xE3 | 0xE3 | Transparent |
| TR-07 | TM attr | pixel_en=0 | Any | Transparent |
| TR-08 | TM attr | pixel_en=1, RGB=0xE3 | 0xE3 | Opaque (attr mode) |
| TR-09 | TM | Any | Any, tm_en=0 | Transparent |
| TR-10 | Sprite | pixel_en=0 | N/A | Transparent |
| TR-11 | Sprite | pixel_en=1 | Any | Opaque (always) |
| TR-12 | L2 | 0xE3 | 0xE3 | Transparent |
| TR-13 | L2 | pixel_en=0 | Any | Transparent |
| TR-14 | L2 | 0x42 | 0xE3 | Opaque |

### 2. Fallback Colour

From `zxnext.vhd` line 7214:
```
rgb_out_2 <= fallback_rgb_2 & (fallback_rgb_2(1) or fallback_rgb_2(0))
```

The fallback colour (NR 0x4A, default 0xE3) is used when ALL layers are
transparent. The 9-bit RGB is constructed by extending bit 0 from
`fallback(1) OR fallback(0)`.

Also from line 6990: When ULA selects background (`ula_select_bgnd_1 = '1'`):
```
ula_rgb_1 <= fallback_rgb_1 & (fallback_rgb_1(1) or fallback_rgb_1(0))
```

| Test | Scenario | Expected RGB |
|------|----------|-------------|
| FB-01 | All layers transparent, fallback=0xE3 | 0xE3 extended to 9-bit |
| FB-02 | All layers transparent, fallback=0x00 | 0x000 |
| FB-03 | All layers transparent, fallback=0x4A | 0x4A & bit0=1 (0x095) |
| FB-04 | ULA background (no pixel data) | Fallback colour |
| FB-05 | One layer opaque | That layer's colour, not fallback |
| FB-06 | Fallback=0x01 | 9-bit = 0x001 & 1 = 0x003 |
| FB-07 | Reset default | 0xE3 (magenta) |

### 3. Layer Priority Modes (NR 0x15 bits 4:2)

From `zxnext.vhd` lines 7205-7354. Six standard modes plus two blend modes:

| Code | Order | Description |
|------|-------|-------------|
| 000 | S L U | Sprites top, L2 middle, ULA bottom |
| 001 | L S U | L2 top, sprites middle, ULA bottom |
| 010 | S U L | Sprites top, ULA middle, L2 bottom |
| 011 | L U S | L2 top, ULA middle, sprites bottom |
| 100 | U S L | ULA top, sprites middle, L2 bottom |
| 101 | U L S | ULA top, L2 middle, sprites bottom |
| 110 | (U|T)S(T|U)(B+L) | Blend: L2+ULA additive, clamped |
| 111 | (U|T)S(T|U)(B+L-5) | Blend: L2+ULA subtractive |

For modes 000-101, the first non-transparent layer wins. Layer2 priority
bit can promote L2 to top in modes 000, 010, 100, 101.

Special rule for modes 011, 100, 101 (lines 7256, 7266, 7278): ULA is
treated as transparent when `ula_border_2='1' AND tm_transparent='1' AND
sprite_transparent='0'`. This prevents the ULA border colour from covering
sprites when U has higher priority.

| Test | Priority | Opaque Layers | Expected Top |
|------|----------|--------------|-------------|
| PRI-01 | 000 (SLU) | S,L,U all | Sprite |
| PRI-02 | 000 (SLU) | L,U only | Layer 2 |
| PRI-03 | 000 (SLU) | U only | ULA |
| PRI-04 | 000 (SLU) | None | Fallback |
| PRI-05 | 001 (LSU) | L,S,U all | Layer 2 |
| PRI-06 | 001 (LSU) | S,U only | Sprite |
| PRI-07 | 010 (SUL) | S,U,L all | Sprite |
| PRI-08 | 010 (SUL) | U,L only | ULA |
| PRI-09 | 010 (SUL) | L only | Layer 2 |
| PRI-10 | 011 (LUS) | L,U,S all | Layer 2 |
| PRI-11 | 011 (LUS) | U,S only | ULA (non-border) |
| PRI-12 | 011 (LUS) | U(border),S | Sprite (border exception) |
| PRI-13 | 100 (USL) | U,S,L all | ULA (non-border) |
| PRI-14 | 100 (USL) | U(border),S,L | Sprite (border exception) |
| PRI-15 | 101 (ULS) | U,L,S all | ULA (non-border) |
| PRI-16 | 101 (ULS) | U(border),L,S | Layer 2 (border exception) |

### 4. Layer2 Priority Promotion

From lines 7220, 7242, 7264, 7277, 7300, 7342:

`layer2_priority = layer2_priority_2 when layer2_transparent = '0' else '0'`

When the L2 priority bit is set (from palette entry bit 15) and L2 is
non-transparent, L2 is promoted to topmost regardless of SLU ordering.
This works in modes 000, 010, 100, 101, 110, 111 (not in 001 or 011 where
L2 is already on top).

| Test | Priority | L2 Priority Bit | Expected |
|------|----------|-----------------|----------|
| L2P-01 | 000 (SLU), S opaque | L2 priority=1 | Layer 2 wins |
| L2P-02 | 010 (SUL), S opaque | L2 priority=1 | Layer 2 wins |
| L2P-03 | 001 (LSU), L2 opaque | L2 priority=1 | Layer 2 (already top) |
| L2P-04 | 100 (USL), U opaque | L2 priority=1 | Layer 2 wins |
| L2P-05 | 000 (SLU), L2 transparent | L2 priority=1 | Priority forced to 0 |

### 5. Blend Modes (Priority 110 and 111)

From lines 7286-7353:

**Mode 110** (additive blend): For each channel, `result = L2 + ULA`, clamped to 7.

```
mixer_r_t = ('0' & layer2_rgb(8:6)) + ('0' & mix_rgb(8:6))
if mixer_r_t(3) = '1' then mixer_r_t = "0111"  -- clamp
```

Layer order in blend mode: L2_priority > mix_top > Sprite > mix_bot > L2_blend.

**Mode 111** (subtractive blend): `result = L2 + ULA - 5`, clamped to [0,7].

```
if mix_rgb_transparent = '0':
    if mixer <= 4: result = 0
    elsif mixer >= 12: result = 7
    else: result = mixer - 5
```

| Test | Mode | L2 RGB | ULA RGB | Expected Blend |
|------|------|--------|---------|---------------|
| BL-01 | 110 | R=3 | R=3 | R=6 (no clamp) |
| BL-02 | 110 | R=5 | R=5 | R=7 (clamped) |
| BL-03 | 110 | R=0 | R=0 | R=0 |
| BL-04 | 110 | All 7 | All 7 | All 7 (clamped) |
| BL-05 | 111 | R=5 | R=3 | R=3 (8-5=3) |
| BL-06 | 111 | R=2 | R=2 | R=0 (4<=4, clamped to 0) |
| BL-07 | 111 | R=7 | R=7 | R=7 (14>=12, clamped to 7) |
| BL-08 | 111 | R=3 | R=3 | R=1 (6-5=1) |
| BL-09 | 111 | ULA transparent | Any | No subtraction applied |
| BL-10 | 110 | L2 priority | Any | Blend result shown |

### 6. ULA/Tilemap Blend Modes (NR 0x68 bits 6:5)

From lines 7139-7178. These control how ULA and tilemap interact before
entering the SLU priority selector:

| Blend Mode | Behaviour |
|------------|-----------|
| 00 | Normal: ULA for SLU mixing, TM above/below separate |
| 01 | mix_rgb forced transparent; top/bot from TM-below logic |
| 10 | ULA+TM combined into ula_final; TM removed from top/bot |
| 11 | TM as mix_rgb; ULA available as top/bot replacement |

The `mix_rgb` signal goes to the SLU selector as the "U" layer. `mix_top`
and `mix_bot` allow tilemap to float above or below in the SLU order.

| Test | Blend | TM Below | Expected |
|------|-------|----------|----------|
| UTB-01 | 00 | below=0 | TM in mix_top, ULA as mix_rgb |
| UTB-02 | 00 | below=1 | TM in mix_bot, ULA as mix_rgb |
| UTB-03 | 10 | N/A | Combined ULA+TM, no separate TM |
| UTB-04 | 11 | below=0 | TM as mix, ULA as top (if below=0) |
| UTB-05 | 01 | below=0 | TM as top, ULA as bot |
| UTB-06 | 01 | below=1 | ULA as top, TM as bot |

### 7. Stencil Mode (NR 0x68 bit 0)

From lines 7112-7113 and 7125-7137:

```
stencil_rgb <= (ula_rgb AND tm_rgb) when stencil_transparent = '0'
stencil_transparent <= ula_transparent OR tm_transparent
```

When both ULA and TM are enabled and stencil mode is on, the "U" layer
output is the bitwise AND of ULA and TM colours. Either being transparent
makes the stencil transparent.

| Test | ULA RGB | TM RGB | Expected |
|------|---------|--------|----------|
| STEN-01 | 0xFF (white) | 0xE0 (red) | 0xE0 (AND) |
| STEN-02 | 0xFF | transparent | Transparent |
| STEN-03 | transparent | 0xFF | Transparent |
| STEN-04 | 0x00 | 0xFF | 0x00 (AND = black) |
| STEN-05 | Stencil off | N/A | Normal ULA+TM behaviour |

### 8. Palette Lookup

From lines 6936-7053:

Two shared palette RAMs:
- **ULA/TM RAM** (1k x 16): Indexed by `{palette_select, pixel_data}`. ULA on sc(0)=0, TM on sc(0)=1.
- **L2/Sprite RAM** (1k x 16): Indexed by `{palette_select, pixel_data}`. L2 on sc(0)=0, Sprite on sc(0)=1.

The ULA path has special background handling (line 6987-6991): if `ula_select_bgnd_1 = '1'`
and `lores_pixel_en_1 = '0'`, the ULA uses fallback colour instead of palette.

| Test | Scenario | Expected |
|------|----------|----------|
| PAL-01 | ULA pixel with active palette | Correct palette colour |
| PAL-02 | ULA background pixel | Fallback colour |
| PAL-03 | LoRes overrides ULA pixel | LoRes colour from ULA palette |
| PAL-04 | Sprite pixel | Correct colour from L2/Sprite palette |
| PAL-05 | L2 pixel with palette select 0 | Palette 0 colour |
| PAL-06 | L2 pixel with palette select 1 | Palette 1 colour |
| PAL-07 | L2 priority bit from palette entry | Extracted from bit 15 |

### 9. Per-Line Parameter Capture

From lines 6730-6832: All compositor parameters are captured at the start of
each scanline, not mid-line. This includes:
- `layer_priorities_0` from NR 0x15
- `transparent_rgb_0` from NR 0x14
- `fallback_rgb_0` from NR 0x4A
- `ula_en_0`, `tm_en_0`, `sprite_en_0` from respective NRs
- `ula_stencil_mode_0`, `ula_blend_mode_0` from NR 0x68
- Clip window values
- Palette select values from NR 0x43

| Test | Scenario | Expected |
|------|----------|----------|
| LINE-01 | Change NR 0x15 mid-line | Old priority until next line |
| LINE-02 | Change NR 0x14 mid-line | Old transparent colour until next line |
| LINE-03 | Change NR 0x4A mid-line | Old fallback until next line |
| LINE-04 | Copper changes priority each line | Different priority per line |

### 10. Output Blanking

From lines 7395-7412:

```
if rgb_blank_n_6 = '1' then
    rgb_out_o <= rgb_out_6;
else
    rgb_out_o <= (others => '0');
end if;
```

During blanking, output is forced to black (all zeros).

| Test | Scenario | Expected |
|------|----------|----------|
| BLANK-01 | Active display area | Compositor output |
| BLANK-02 | Horizontal blanking | Black (0x000) |
| BLANK-03 | Vertical blanking | Black (0x000) |

### 11. Sprite Over Border

From NR 0x15 bit 1 (`nr_15_sprite_over_border_en`):

When enabled, sprites are visible over the border area. When disabled,
sprites are clipped to the display area.

| Test | Scenario | Expected |
|------|----------|----------|
| SOB-01 | Sprite in border, over_border=1 | Sprite visible |
| SOB-02 | Sprite in border, over_border=0 | Sprite clipped |

### 12. Tilemap Below Flag

From line 6863:
```
tm_pixel_below_1 <= (tm_pixel_below and tm_en_1a) or ((not nr_6b_tm_control(0)) and not tm_en_1a)
```

The tilemap `below` flag comes from per-tile attributes. When set, the tilemap
pixel is placed below ULA in the compositor rather than above.

| Test | Scenario | Expected |
|------|----------|----------|
| TMB-01 | TM below=0, blend_mode=00 | TM in mix_top |
| TMB-02 | TM below=1, blend_mode=00 | TM in mix_bot |
| TMB-03 | TM below=0, blend_mode=11 | ULA as bot |
| TMB-04 | TM below=1, blend_mode=11 | ULA as top |

## Reset Defaults (Compositor-Relevant)

| Parameter | Default | Source |
|-----------|---------|--------|
| Layer priority | 000 (SLU) | NR 0x15 bits 4:2 |
| Sprite enable | 0 | NR 0x15 bit 0 |
| Sprite over border | 0 | NR 0x15 bit 1 |
| ULA enable | 1 | NR 0x68 bit 7 (inverted) |
| Blend mode | 00 | NR 0x68 bits 6:5 |
| Stencil mode | 0 | NR 0x68 bit 0 |
| Tilemap enable | 0 | NR 0x6B bit 7 |
| Global transparent | 0xE3 | NR 0x14 |
| Fallback colour | 0xE3 | NR 0x4A |
| Sprite transparent idx | 0xE3 | NR 0x4B |
| TM transparent idx | 0x0F | NR 0x4C |

## Test Count Summary

| Category | Tests |
|----------|-------|
| Transparency detection | ~14 |
| Fallback colour | ~7 |
| Layer priority modes | ~16 |
| Layer2 priority promotion | ~5 |
| Blend modes (110/111) | ~10 |
| ULA/TM blend modes | ~6 |
| Stencil mode | ~5 |
| Palette lookup | ~7 |
| Per-line parameter capture | ~4 |
| Output blanking | ~3 |
| Sprite over border | ~2 |
| Tilemap below flag | ~4 |
| **Total** | **~83** |
