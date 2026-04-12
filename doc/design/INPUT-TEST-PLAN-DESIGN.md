# Input Subsystem Compliance Test Plan

VHDL-derived compliance test plan for the keyboard membrane scanning,
extended keys, PS/2 keyboard mapping, and joystick subsystem (Kempston,
Sinclair, Cursor, MD protocol, I/O mode) of the JNEXT emulator.

## Purpose

Validate that the emulator's input subsystem matches the VHDL behaviour
defined in `membrane.vhd`, `ps2_keyb.vhd`, `md6_joystick_connector_x2.vhd`,
and the keyboard/joystick section of `zxnext.vhd` (lines ~3426-3562).

## Authoritative VHDL Sources

| File | Subsystem |
|------|-----------|
| `input/membrane/membrane.vhd` | 8x7 keyboard membrane scan, half-row read, extended key columns |
| `input/keyboard/ps2_keyb.vhd` | PS/2 scancode to 8x7 matrix mapping, shift counting, keymap RAM |
| `input/md6_joystick_connector_x2.vhd` | Sega MD 3/6-button protocol, left/right connector, I/O mode |
| `zxnext.vhd` (3426-3562) | Joystick mode selection, Kempston/Sinclair/Cursor/MD routing, port 0x1F/0x37 |

## Architecture

### Test Approach

Unit tests that simulate keyboard state and joystick state, then verify that
I/O port reads return the correct values. No real PS/2 or GPIO interaction is
needed --- the tests inject key state into the membrane matrix and joystick
vectors, then read the appropriate ports.

### Test Categories

1. **Keyboard half-row scanning** (port 0xFE read)
2. **Extended keys** (extra columns 5-6, NextREG 0xB0/0xB1)
3. **Joystick mode selection** (NextREG 0x05)
4. **Kempston joystick** (port 0x1F, port 0x37)
5. **Sinclair joystick** (keyboard rows)
6. **Cursor joystick** (keyboard rows)
7. **MD 6-button joystick** (port 0x1F/0x37 extended bits)
8. **Joystick I/O mode** (NextREG 0x0B)

## Test Case Catalog

### 1. Keyboard Half-Row Scanning (Port 0xFE Read)

The ZX Spectrum keyboard is an 8x5 matrix. Port 0xFE is read with the
address bus high byte (A15-A8) selecting which rows are active (active low).
The returned data is the AND of all selected rows' column states (bits 4:0,
active low).

From `membrane.vhd` lines 242-251:
- Each row `r[n]` contributes its 5-bit column data when `i_rows(n) = '0'`
- Otherwise the row contributes `11111` (all keys up)
- Final result: `o_cols = r0 AND r1 AND r2 AND r3 AND r4 AND r5 AND r6 AND r7`

From `zxnext.vhd` line 3449: `keyrow <= cpu_a(15 downto 8)` on falling CPU clock edge.

From `zxnext.vhd` line 3459: `port_fe_dat_0 <= '1' & (i_AUDIO_EAR or port_fe_ear) & '1' & i_KBD_COL`

| Test | A15-A8 | Key pressed | Expected D4:D0 | Notes |
|------|--------|-------------|-----------------|-------|
| KBD-01 | 0xFE | None | 0x1F (all 1s) | Row 0 selected, no keys |
| KBD-02 | 0xFE | CAPS SHIFT | 0x1E | Row 0, bit 0 = 0 |
| KBD-03 | 0xFE | Z | 0x1D | Row 0, bit 1 = 0 |
| KBD-04 | 0xFE | X | 0x1B | Row 0, bit 2 = 0 |
| KBD-05 | 0xFE | C | 0x17 | Row 0, bit 3 = 0 |
| KBD-06 | 0xFE | V | 0x0F | Row 0, bit 4 = 0 |
| KBD-07 | 0x7F | SPACE | 0x1E | Row 7, bit 0 = 0 |
| KBD-08 | 0x7F | SYM SHIFT | 0x1D | Row 7, bit 1 = 0 |
| KBD-09 | 0xFE | CS + Z (both) | 0x1C | Row 0, bits 0,1 = 0 |
| KBD-10 | 0x00 | CS (row 0) + SPACE (row 7) | Both bits 0 = 0 | All rows selected |
| KBD-11 | 0xFB | Q | 0x1E | Row 2 (A10), bit 0 = 0 |
| KBD-12 | 0xFF | Any key | 0x1F | No rows selected |
| KBD-13 | 0xFE & 0x7F | CS + SYM | Bits ANDed | Two rows simultaneously |

### 2. Extended Keys (Columns 5-6)

The membrane has 7 columns (0-6). Columns 5-6 are "extended" keys mapped from
the physical Next keyboard. From `membrane.vhd` lines 159-227:

| Row | Column 6 | Column 5 |
|-----|----------|----------|
| 0 | UP | EXTEND |
| 1 | GRAPH | CAPS LOCK |
| 2 | INV VIDEO | TRUE VIDEO |
| 3 | EDIT | BREAK |
| 4 | " | ; |
| 5 | . | , |
| 6 | RIGHT | DELETE |
| 7 | DOWN | LEFT |

Extended keys are mapped into the standard 5-column matrix by modifying
specific rows (membrane.vhd lines 236-241):
- Row 0 bit 0: ANDed with extended_keys CAP SHIFT equivalent
- Row 3 bits 4:0: ANDed with extended_keys bits 5:1
- Row 4 bits 4:0: ANDed with extended_keys bits 10:6
- Row 5 bits 1:0: ANDed with extended_keys bits 12:11
- Row 7 bits 3:0: ANDed with extended_keys bits 16:13

From `zxnext.vhd` line 6206-6212, NextREG 0xB0 and 0xB1 read extended key state:
- 0xB0: `;  "  ,  .  UP  DOWN  LEFT  RIGHT`
- 0xB1: `DELETE  EDIT  BREAK  INV  TRU  GRAPH  CAPSLOCK  EXTEND`

| Test | Scenario | Expected |
|------|----------|----------|
| EXT-01 | Press UP (row 0 col 6), read 0xB0 | Bit 3 set |
| EXT-02 | Press DELETE (row 6 col 5), read 0xB1 | Bit 7 set |
| EXT-03 | Press EDIT (row 3 col 6), read 0xB1 | Bit 6 set |
| EXT-04 | Extended key affects standard matrix row 3 | Row 3 modified |
| EXT-05 | cancel_extended_entries = 1 | Standard matrix unmodified |
| EXT-06 | Multiple extended keys simultaneously | Correct compound state |

### 3. Joystick Mode Selection (NextREG 0x05)

From `zxnext.vhd` lines 3429-3438 and 5156-5158:

```
nr_05_joy0 <= nr_wr_dat(3) & nr_wr_dat(7 downto 6)
nr_05_joy1 <= nr_wr_dat(1) & nr_wr_dat(5 downto 4)
```

Modes (3-bit):
| Code | Mode | Port/Mapping |
|------|------|--------------|
| 000 | Sinclair 2 | Keys 67890 |
| 001 | Kempston 1 | Port 0x1F |
| 010 | Cursor | Keys 56780 |
| 011 | Sinclair 1 | Keys 12345 |
| 100 | Kempston 2 | Port 0x37 |
| 101 | MD 1 | Port 0x1F (with bits 7:6) |
| 110 | MD 2 | Port 0x37 (with bits 7:6) |
| 111 | I/O Mode | Both connectors in I/O mode |

| Test | Scenario | Expected |
|------|----------|----------|
| JMODE-01 | Write 0x05 = 0x41, read 0x05 | joy0=001 (Kempston1), joy1=000 (Sinclair2) |
| JMODE-02 | Write 0x05 = 0xC9, read 0x05 | joy0=101 (MD1), joy1=010 (Cursor) |
| JMODE-03 | Write 0x05 with bit 3=1 | joy0 bit 2 set |
| JMODE-04 | Reset defaults | Both = 000 (Sinclair 2) |

### 4. Kempston Joystick (Ports 0x1F, 0x37)

From `zxnext.vhd` lines 3470-3507:

Port 0x1F data = `joyL_1f OR joyR_1f` (left and right connector ORed).
Port 0x37 data = `joyL_37 OR joyR_37`.

Bit mapping (active high): `[7:6]=START,A  [5:0]=A,C,B,U,D,L,R`

Enable logic (lines 3472-3488):
- Left on 0x1F: `joyL_1f_en = (nr_05_joy0 == 001) or (nr_05_joy0 == 101)`
- Left on 0x37: `joyL_37_en = (nr_05_joy0 == 100) or (nr_05_joy0 == 110)`
- Same pattern for right connector using `nr_05_joy1`

MD bits 7:6 only enabled when mode is MD (101 or 110).

| Test | Mode | Joystick | Port | Expected |
|------|------|----------|------|----------|
| KEMP-01 | joy0=001 | Left UP | 0x1F | Bit 3 set |
| KEMP-02 | joy0=001 | Left RIGHT | 0x1F | Bit 0 set |
| KEMP-03 | joy0=001 | Left FIRE | 0x1F | Bit 4 set |
| KEMP-04 | joy0=100 | Left UP | 0x37 | Bit 3 set |
| KEMP-05 | joy0=000 | Left UP | 0x1F | 0x00 (Sinclair mode, no Kempston) |
| KEMP-06 | joy0=001, joy1=001 | Both UP | 0x1F | Both ORed |
| KEMP-07 | joy0=001 | All directions + fire | 0x1F | 0x1F |
| KEMP-08 | No joystick hw enabled | Any | 0x1F | Port not decoded |

### 5. MD 6-Button Joystick

From `md6_joystick_connector_x2.vhd`:

Output bit mapping: `MODE X Z Y START A C B U D L R` (bits 11:0, active high).

The MD protocol uses a state machine with 8 states per connector read cycle.
6-button detection occurs when pins 3 and 4 are both low during state "100".

From `zxnext.vhd` lines 3472-3494:
- MD on port 0x1F (mode 101): bits 7:6 from `i_JOY_LEFT(7:6)`, bits 5:0 from `i_JOY_LEFT(5:0)`
- MD on port 0x37 (mode 110): same pattern

NextREG 0xB2 reads extra MD buttons (lines 6214-6215):
`i_JOY_RIGHT(10:8) & i_JOY_RIGHT(11) & i_JOY_LEFT(10:8) & i_JOY_LEFT(11)`

| Test | Scenario | Expected |
|------|----------|----------|
| MD-01 | Mode 101, 3-button: U+D+L+R+A+B | Port 0x1F = 0x3F |
| MD-02 | Mode 101, START+A | Port 0x1F bits 7:6 set |
| MD-03 | Mode 101, 6-button: X+Y+Z+MODE | NR 0xB2 bits 3:0 set |
| MD-04 | Mode 001 (not MD), press START | Port 0x1F bits 7:6 = 0 |
| MD-05 | Mode 110, right connector | Port 0x37 correct |
| MD-06 | Reset clears all joystick state | All outputs 0 |

### 6. Sinclair and Cursor Joystick (Keyboard-Mapped)

Sinclair joysticks map to keyboard rows. The mapping is not done in `zxnext.vhd`
directly; instead, the key_joystick module (external to the VHDL we have)
injects keypresses into the membrane matrix. The mode selection determines
which keyboard half-row the joystick buttons are mapped to.

Standard Sinclair/Cursor mappings:

| Mode | Name | Keys | Row | Bits (R L D U F) |
|------|------|------|-----|------------------|
| 000 | Sinclair 2 | 6 7 8 9 0 | Row 4 (67890) | Various |
| 011 | Sinclair 1 | 1 2 3 4 5 | Row 3 (12345) | Various |
| 010 | Cursor | 5 6 7 8 0 | Rows 3,4 | 5=L, 6=D, 7=U, 8=R, 0=F |

| Test | Mode | Direction | Expected key |
|------|------|-----------|-------------|
| SINC-01 | 000 (Sinclair 2) | RIGHT | Key 6 |
| SINC-02 | 000 (Sinclair 2) | LEFT | Key 7 |
| SINC-03 | 000 (Sinclair 2) | DOWN | Key 8 |
| SINC-04 | 000 (Sinclair 2) | UP | Key 9 |
| SINC-05 | 000 (Sinclair 2) | FIRE | Key 0 |
| SINC-06 | 011 (Sinclair 1) | LEFT | Key 1 |
| SINC-07 | 011 (Sinclair 1) | RIGHT | Key 2 |
| SINC-08 | 011 (Sinclair 1) | DOWN | Key 3 |
| SINC-09 | 011 (Sinclair 1) | UP | Key 4 |
| SINC-10 | 011 (Sinclair 1) | FIRE | Key 5 |
| CURS-01 | 010 (Cursor) | LEFT | Key 5 |
| CURS-02 | 010 (Cursor) | DOWN | Key 6 |
| CURS-03 | 010 (Cursor) | UP | Key 7 |
| CURS-04 | 010 (Cursor) | RIGHT | Key 8 |
| CURS-05 | 010 (Cursor) | FIRE | Key 0 |

### 7. Joystick I/O Mode (NextREG 0x0B)

From `zxnext.vhd` lines 5200-5203 and 3510-3539:

```
nr_0b_joy_iomode_en <= nr_wr_dat(7)
nr_0b_joy_iomode <= nr_wr_dat(5 downto 4)
nr_0b_joy_iomode_0 <= nr_wr_dat(0)
```

When I/O mode is enabled (`nr_0b_joy_iomode_en = 1`):
- Pin 7 behaviour depends on `nr_0b_joy_iomode`:
  - "00": pin 7 = `nr_0b_joy_iomode_0`
  - "01": pin 7 toggles on CTC zero-cross
  - "1x": pin 7 = UART TX (uart0 if iomode_0=0, uart1 if iomode_0=1)
- UART mode enabled when `nr_0b_joy_iomode(1) = '1'`

Reset defaults: `iomode_en=0, iomode="00", iomode_0=1`

| Test | Scenario | Expected |
|------|----------|----------|
| IOMODE-01 | Write 0x0B = 0x80, read back | Bit 7 set, rest 0 except bit 0=1 (default) |
| IOMODE-02 | Reset values | 0x0B reads 0x01 |
| IOMODE-03 | UART mode (iomode=1x) | joy_iomode_uart_en = 1 |
| IOMODE-04 | iomode="00", iomode_0=0 | pin7 = 0 |
| IOMODE-05 | iomode="00", iomode_0=1 | pin7 = 1 |

### 8. Port 0xFE Format

From `zxnext.vhd` line 3459:

Bit 7: always 1
Bit 6: `i_AUDIO_EAR or port_fe_ear`
Bit 5: always 1
Bits 4:0: keyboard column data from membrane

With issue 2 keyboard mode (NR 0x08 bit 0): MIC output is XORed with EAR
input, matching the ZX Spectrum issue 2 hardware (line 6503).

| Test | Scenario | Expected |
|------|----------|----------|
| FE-01 | No keys, no EAR | 0xBF (bits 7,5 = 1, bit 6 = 0, bits 4:0 = 1F) |
| FE-02 | EAR input active | Bit 6 = 1 |
| FE-03 | port_fe_ear active (write 0xFE bit 4) | Bit 6 = 1 |
| FE-04 | Issue 2 mode, MIC XOR EAR | Correct XOR behaviour |

## Reset Defaults (from zxnext.vhd)

| Register | Default | Notes |
|----------|---------|-------|
| NR 0x05 (joy0, joy1) | 000, 000 | Both Sinclair 2 |
| NR 0x08 bit 0 (issue2) | 0 | Issue 3 keyboard |
| NR 0x0B (I/O mode) | en=0, mode="00", 0=1 | I/O mode disabled |
| Membrane matrix | All 1s | No keys pressed |
| Joystick outputs | All 0s | No buttons pressed |

## Test Count Summary

| Category | Tests |
|----------|-------|
| Keyboard half-row scanning | ~13 |
| Extended keys | ~6 |
| Joystick mode selection | ~4 |
| Kempston joystick | ~8 |
| MD 6-button joystick | ~6 |
| Sinclair/Cursor joystick | ~15 |
| Joystick I/O mode | ~5 |
| Port 0xFE format | ~4 |
| **Total** | **~61** |
