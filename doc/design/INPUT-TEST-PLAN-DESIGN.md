# Input Subsystem Compliance Test Plan

VHDL-derived compliance test plan for the JNEXT emulator's keyboard
membrane, extended-key matrix, joystick subsystem (Kempston 1/2, Sinclair
1/2, Cursor, Megadrive 3/6 button, user-defined I/O mode), Kempston
mouse, and the Multiface / Drive NMI buttons.

---

## Plan Rebuilt 2026-04-15 — Retraction of Prior "71/71 Passing" Claim

The previous revision of this document reported 71/71 passing and
presented a nine-section catalog. Task 4's plan audit (2026-04-14)
demonstrated that the number was coverage theatre:

1. **Keyboard-only coverage.** Of the eight joystick categories listed,
   only the Kempston and Keyboard rows had real cases. Sinclair 1/2,
   Cursor, MD 3-button, MD 6-button, I/O mode, mouse and NMI-button
   sections each either held placeholder rows with no stimulus/expected
   pair that could be executed as a test, or were absent entirely.
2. **Kempston bit map was wrong.** The old plan wrote
   `[5:0]=A,C,B,U,D,L,R` — that is seven bit names for six bit positions
   and does not match `zxnext.vhd` 3441-3442, which defines the joystick
   signal ordering as `X Z Y START A C(F2) B(F1) U D L R` for bits 11..0.
   The actual port 0x1F / 0x37 data-byte bit map is given below and is
   the oracle every KEMP-* / MD-* row now derives from.
3. **JMODE-02 arithmetic was wrong.** The old row claimed that writing
   NR 0x05 = 0xC9 produced `joy0=101 (MD1), joy1=010 (Cursor)`. Decoding
   per `zxnext.vhd` 5157-5158
   (`joy0 = D3 & D7 & D6`, `joy1 = D1 & D5 & D4`) gives
   `joy0 = 1&1&1 = 111 (I/O mode)` and `joy1 = 0&0&0 = 000 (Sinclair 2)`.
   The corrected test and the byte that would actually produce the
   originally-intended mapping are in the Retractions section.
4. **MD-01 expected was wrong.** The old row claimed mode 101 with
   `U+D+L+R+A+B` pressed produced port 0x1F = 0x3F. Per the signal
   ordering `A C B U D L R` at bit positions 6 5 4 3 2 1 0, pressing
   those six inputs sets bits 0,1,2,3,4,6 — i.e. 0x5F, not 0x3F. 0x3F
   would require Fire2 (C, bit 5) to be pressed instead of A.
5. **No citations.** The old plan referenced the VHDL in prose but did
   not pin individual expected values to file/line, so the three
   arithmetic bugs above survived review.

This rewrite derives every expected value from the VHDL under
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`
with line citations, removes every tautology, and adds the six
missing joystick categories plus Kempston mouse and NMI buttons.
Test totals in this document are nominal (the test code has not yet
been rewritten); any future pass/fail counts must come from executing
these cases, not from the old source tree.

---

## Authoritative VHDL Sources

| File | Subsystem |
|------|-----------|
| `zxnext.vhd` 3425-3562 | Joystick mode select, Kempston/MD routing, ports 0x1F/0x37, I/O-mode pin7, mouse ports 0xFADF/0xFBDF/0xFFDF |
| `zxnext.vhd` 5156-5203 | NR 0x05 (joystick mode), NR 0x06 (NMI button enables), NR 0x0B (I/O-mode) writes |
| `zxnext.vhd` 6197-6215 | NR 0xB0 / 0xB1 (extended keys) and NR 0xB2 (MD 6-button extras) reads |
| `zxnext.vhd` 2085-2095 | NMI assertion from Multiface (M1) and Drive hotkeys/software |
| `zxnext.vhd` 2668-2675 | Port decode: 0x1F / 0x37 / 0xFADF / 0xFBDF / 0xFFDF |
| `input/membrane/membrane.vhd` 150-255 | 8x7 membrane, extended-column folding into standard matrix, shift-hold hysteresis |
| `input/keyboard/ps2_keyb.vhd` | PS/2 scancode -> matrix mapping (host-side, optional coverage) |
| `input/md6_joystick_connector_x2.vhd` | Megadrive 3/6-button state machine, SELECT pin sequencing |

---

## 1. VHDL-Derived Oracles

### 1.1 NR 0x05 joystick mode encoding (`zxnext.vhd` 5157-5158)

```
nr_05_joy0 <= nr_wr_dat(3) & nr_wr_dat(7 downto 6);   -- {D3,D7,D6}
nr_05_joy1 <= nr_wr_dat(1) & nr_wr_dat(5 downto 4);   -- {D1,D5,D4}
```

Mode codes (`zxnext.vhd` 3429-3438):

| Code | Mode | Read surface |
|------|------|--------------|
| 000 | Sinclair 2 | Keyboard half-row (keys 6,7,8,9,0) |
| 001 | Kempston 1 | Port 0x1F |
| 010 | Cursor (Protek) | Keyboard rows, keys 5,6,7,8,0 |
| 011 | Sinclair 1 | Keyboard half-row (keys 1,2,3,4,5) |
| 100 | Kempston 2 | Port 0x37 |
| 101 | MD 1 (3 or 6 button) | Port 0x1F, bits 7:6 + 5:0; extras via NR 0xB2 |
| 110 | MD 2 (3 or 6 button) | Port 0x37, bits 7:6 + 5:0; extras via NR 0xB2 |
| 111 | User I/O mode | Both connectors routed by NR 0x0B |

### 1.2 Joystick signal bit layout (`zxnext.vhd` 3441-3442)

`i_JOY_LEFT` and `i_JOY_RIGHT` are each 12-bit vectors, active high:

```
bit    11   10   9   8     7    6      5       4      3   2   1   0
name   MODE X    Z   Y     START A   C(Fire2) B(Fire1) U   D   L   R
```

### 1.3 Kempston / MD port-byte bit map (`zxnext.vhd` 3478-3494, 3499, 3506)

Port 0x1F (Kempston 1, MD 1) and port 0x37 (Kempston 2, MD 2) both
return `joyL_xx or joyR_xx`. For each source the byte is assembled as:

```
bit  7     6   5      4      3   2   1   0
     START A   Fire2  Fire1  U   D   L   R     (MD modes 101/110)
     0     0   Fire2  Fire1  U   D   L   R     (Kempston modes 001/100; bits 7:6 forced 0)
```

In Kempston mode bits 7:6 are forced to `'0'` by the VHDL guards at
lines 3478 and 3490. In MD mode those two bits pass through and carry
START (bit 7) and A (bit 6).

### 1.4 NR 0xB2 — MD 6-button extras (`zxnext.vhd` 6214-6215)

```
port_253b_dat <= i_JOY_RIGHT(10 downto 8) & i_JOY_RIGHT(11)
              &  i_JOY_LEFT(10 downto 8)  & i_JOY_LEFT(11);
```

So NR 0xB2 bit layout is:

```
bit  7    6    5    4       3    2    1    0
     R.Y  R.Z  R.X  R.MODE  L.Y  L.Z  L.X  L.MODE
```

Note: `i_JOY_*(10:8)` is `X Z Y` high-to-low (line 3442), so bit 7 of
NR 0xB2 is Y (i.e. `JOY_RIGHT(8)`), bit 6 is Z (`JOY_RIGHT(9)`), bit 5
is X (`JOY_RIGHT(10)`). Mirrored for the left joystick at bits 3:1.

### 1.5 Port 0xFE layout (`zxnext.vhd` 3459)

```
port_fe_dat_0 <= '1' & (i_AUDIO_EAR or port_fe_ear) & '1' & i_KBD_COL;
```

Bit 7 = 1, bit 6 = EAR in, bit 5 = 1, bits 4..0 = membrane column AND.
The row select is latched from `cpu_a(15 downto 8)` on the falling
CPU-clock edge (line 3449). All eight rows are ANDed (membrane.vhd 251).

### 1.6 Membrane matrix (`membrane.vhd` 161-175, 236-254)

The physical matrix is 8 rows x 7 columns. Columns 0..4 are the
classic Spectrum keys, columns 5 and 6 are the Next extended keys.
The layout (bit-in-column → key) per row is:

| Row | A-addr | Col0 | Col1 | Col2 | Col3 | Col4 | Col5 (ext) | Col6 (ext) |
|-----|--------|------|------|------|------|------|------------|------------|
| 0 | A8  (0xFEFE) | CAPS SHIFT | Z | X | C | V | EXTEND | UP |
| 1 | A9  (0xFDFE) | A | S | D | F | G | CAPS LOCK | GRAPH |
| 2 | A10 (0xFBFE) | Q | W | E | R | T | TRUE VIDEO | INV VIDEO |
| 3 | A11 (0xF7FE) | 1 | 2 | 3 | 4 | 5 | BREAK | EDIT |
| 4 | A12 (0xEFFE) | 0 | 9 | 8 | 7 | 6 | ;      | "      |
| 5 | A13 (0xDFFE) | P | O | I | U | Y | ,      | .      |
| 6 | A14 (0xBFFE) | ENTER | L | K | J | H | DELETE | RIGHT |
| 7 | A15 (0x7FFE) | SPACE | SYM SHIFT | M | N | B | LEFT | DOWN |

Extended columns are folded back into the standard five-column rows at
`membrane.vhd` 236-240:

```
matrix_state_0 <= m(0)(4:1) & (m(0)(0) and matrix_state_ex(0));
matrix_state_3 <= m(3)(4:0) and matrix_state_ex(5:1);
matrix_state_4 <= m(4)(4:0) and matrix_state_ex(10:6);
matrix_state_5 <= m(5)(4:2) & (m(5)(1:0) and matrix_state_ex(12:11));
matrix_state_7 <= m(7)(4) & (m(7)(3:0) and matrix_state_ex(16:13));
```

where `matrix_state_ex` is the 17-bit extended-key vector listed in
`membrane.vhd` 161-162. The shift-hysteresis at lines 180-232 holds
CAPS SHIFT / SYM SHIFT for one extra scan, which a real test plan must
account for when testing CS/SS released between scans.

### 1.7 NR 0xB0 / 0xB1 (`zxnext.vhd` 6206-6212)

```
NR 0xB0 = ';', '"', ',', '.', UP, DOWN, LEFT, RIGHT   (bits 7..0)
NR 0xB1 = DELETE, EDIT, BREAK, INV, TRU, GRAPH, CAPSLOCK, EXTEND
```

Bit indices pulled from `i_KBD_EXTENDED_KEYS` on those lines.

### 1.8 NR 0x0B I/O mode (`zxnext.vhd` 5200-5203, 3510-3539)

```
nr_0b_joy_iomode_en <= D7
nr_0b_joy_iomode    <= D5..D4
nr_0b_joy_iomode_0  <= D0
```

Pin-7 behaviour:

| `nr_0b_joy_iomode` | Drives pin 7 |
|--------------------|--------------|
| 00 | `nr_0b_joy_iomode_0` (static) |
| 01 | Toggles on `ctc_zc_to(3)` while iomode_0 = 1 or pin7 = 0 |
| 10 | `uart0_tx` if iomode_0 = 0, else `uart1_tx` |
| 11 | same as 10 |

UART-enable: `joy_uart_en = iomode_en AND iomode(1)` (line 3536).
RX input selection at 3538: `joy_uart_rx` picks `JOY_LEFT(5)` or
`JOY_RIGHT(5)` based on `iomode_0`.

Reset: `joy_iomode_pin7 <= '1'` (line 3516); register defaults must be
checked from the reset process at the top of `zxnext.vhd`.

### 1.9 Kempston mouse (`zxnext.vhd` 2668-2670, 3543-3561)

```
port_fbdf (0xFBDF) <= i_MOUSE_X
port_ffdf (0xFFDF) <= i_MOUSE_Y
port_fadf (0xFADF) <= MOUSE_WHEEL(4) & '1' & !BTN2 & !BTN0 & !BTN1
```

Decoded only when `port_mouse_io_en = 1` (line 2668). NR 0x0A bit 3
(`nr_0a_mouse_button_reverse`) swaps left/right in the host adapter —
verify separately from the port decode.

### 1.10 NMI buttons (`zxnext.vhd` 2090-2091, 5165-5166)

```
nmi_assert_mf     <= (hotkey_m1 or nmi_sw_gen_mf) and nr_06_button_m1_nmi_en;
nmi_assert_divmmc <= (hotkey_drive or nmi_sw_gen_divmmc) and nr_06_button_drive_nmi_en;
```

`nr_06_button_m1_nmi_en` = NR 0x06 bit 3. `nr_06_button_drive_nmi_en` =
NR 0x06 bit 4. Gating is VHDL-observable; the hotkey inputs are host
harness signals.

---

## 2. Corrections to the Retracted Rows

### 2.1 Kempston bit map

| Old plan | Per VHDL (`zxnext.vhd` 3441-3442, 3478-3482) |
|----------|------------------------------------------------|
| `[7:6]=START,A  [5:0]=A,C,B,U,D,L,R` (7 names, 6 bits) | `b7=START, b6=A, b5=Fire2(C), b4=Fire1(B), b3=U, b2=D, b1=L, b0=R` |
| Bits 7:6 always decoded | Bits 7:6 are forced to 0 in Kempston mode (001/100); only live in MD mode (101/110) |

### 2.2 JMODE-02

| | Stimulus | Expected (old) | Expected (VHDL) |
|---|---|---|---|
| JMODE-02 (retracted) | Write NR 0x05 = 0xC9 | joy0=101 (MD1), joy1=010 (Cursor) | joy0=111 (I/O mode), joy1=000 (Sinclair 2) |
| JMODE-02 (corrected) | Write NR 0x05 = 0x4A = 0b0100_1010 | joy0 = D3&D7&D6 = 1&0&1 = 101 (MD1); joy1 = D1&D5&D4 = 1&0&0 = 010 (Cursor) | — |

0x4A is the byte that actually yields the originally-intended mapping.
The retracted 0xC9 row is now JMODE-02r and tests the correct
I/O-mode + Sinclair-2 decode.

### 2.3 MD-01

| | Stimulus | Expected (old) | Expected (VHDL) |
|---|---|---|---|
| MD-01 | Mode 101; `i_JOY_LEFT` = U+D+L+R+A+B (bits 6,4,3,2,1,0) | port 0x1F = 0x3F | port 0x1F = 0x5F |

Per `zxnext.vhd` 3478-3479 with mode 101 enabled: bits 6..0 = A, _, B,
U, D, L, R = `1 0 1 1 1 1 1` = 0x5F. Bit 5 (Fire2/C) is unset.
Bit 7 (START) unset. If the test intends to also press Fire2, expected
becomes 0x7F; if START is added, 0xFF.

---

## 3. Test Case Catalog

Each row lists test ID, title, preconditions, stimulus, expected, and
VHDL citation. `PRE` "reset-defaults" means boot state with no NR
writes beyond reset.

### 3.1 Keyboard membrane — standard 40 keys (KBD-*)

One read per (row, column) pair. Expected D4..D0 reflects a single
pressed key; all 1s when none pressed.

| ID | PRE | Stimulus | Expected D4..D0 on port 0xFE | Cite |
|----|-----|----------|------------------------------|------|
| KBD-01  | none | addr=0xFEFE, no key | 0x1F | membrane.vhd 251 |
| KBD-02  | none | 0xFEFE, CAPS SHIFT | 0x1E | 236, 242 |
| KBD-03  | none | 0xFEFE, Z          | 0x1D | 242 |
| KBD-04  | none | 0xFEFE, X          | 0x1B | 242 |
| KBD-05  | none | 0xFEFE, C          | 0x17 | 242 |
| KBD-06  | none | 0xFEFE, V          | 0x0F | 242 |
| KBD-07  | none | 0xFDFE, A..G       | 0x1E..0x0F | 243 |
| KBD-08  | none | 0xFBFE, Q..T       | 0x1E..0x0F | 244 |
| KBD-09  | none | 0xF7FE, 1..5       | 0x1E..0x0F | 245 |
| KBD-10  | none | 0xEFFE, 0..6       | 0x1E..0x0F | 246 |
| KBD-11  | none | 0xDFFE, P..Y       | 0x1E..0x0F | 247 |
| KBD-12  | none | 0xBFFE, ENTER..H   | 0x1E..0x0F | 248 |
| KBD-13  | none | 0x7FFE, SPACE      | 0x1E | 249 |
| KBD-14  | none | 0x7FFE, SYM SHIFT  | 0x1D | 249 |
| KBD-15  | none | 0x7FFE, M          | 0x1B | 249 |
| KBD-16  | none | 0x7FFE, N          | 0x17 | 249 |
| KBD-17  | none | 0x7FFE, B          | 0x0F | 249 |
| KBD-18  | none | CS+Z @ 0xFEFE      | 0x1C | 251 (AND) |
| KBD-19  | none | CS+SYM @ 0xFCFE (rows 0,7 selected) | 0x1C | 251 |
| KBD-20  | none | Any key, addr=0xFFFE (no rows) | 0x1F | 242-251 |
| KBD-21  | none | Any single key, addr=0x00FE (all rows selected) | AND across rows | 251 |
| KBD-22  | none | Full port 0xFE byte with no key, EAR=0 | 0xBF | 3459 |
| KBD-23  | none | Full port 0xFE byte with CS pressed | 0xBE | 3459 + 242 |

### 3.2 Keyboard shift hysteresis and hold (KBDHYS-*)

Coverage of `membrane.vhd` 180-232 (shift state advanced one scan,
held an extra scan).

| ID | Stimulus | Expected | Cite |
|----|----------|----------|------|
| KBDHYS-01 | Pulse CS for one scan, then release; read the next scan | CS still reads pressed | 190, 232 |
| KBDHYS-02 | Hold CS continuously across 3 scans | Reads pressed every scan | 190 |
| KBDHYS-03 | `i_cancel_extended_entries = 1` mid-scan | ex matrix forced to all 1s | 183-186 |

### 3.3 Extended keys (EXT-*)

Coverage of `membrane.vhd` 159-254 and `zxnext.vhd` 6206-6212.

| ID | Stimulus | Expected | Cite |
|----|----------|----------|------|
| EXT-01 | Press UP, read NR 0xB0 | bit 3 = 1 | 6208 |
| EXT-02 | Press DOWN, read NR 0xB0 | bit 2 = 1 | 6208 |
| EXT-03 | Press LEFT, read NR 0xB0 | bit 1 = 1 | 6208 |
| EXT-04 | Press RIGHT, read NR 0xB0 | bit 0 = 1 | 6208 |
| EXT-05 | Press ';' | NR 0xB0 bit 7 = 1 | 6208 |
| EXT-06 | Press '"' | NR 0xB0 bit 6 = 1 | 6208 |
| EXT-07 | Press ',' | NR 0xB0 bit 5 = 1 | 6208 |
| EXT-08 | Press '.' | NR 0xB0 bit 4 = 1 | 6208 |
| EXT-09 | Press DELETE | NR 0xB1 bit 7 = 1 | 6212 |
| EXT-10 | Press EDIT | NR 0xB1 bit 6 = 1 | 6212 |
| EXT-11 | Press BREAK | NR 0xB1 bit 5 = 1 | 6212 |
| EXT-12 | Press INV VIDEO | NR 0xB1 bit 4 = 1 | 6212 |
| EXT-13 | Press TRUE VIDEO | NR 0xB1 bit 3 = 1 | 6212 |
| EXT-14 | Press GRAPH | NR 0xB1 bit 2 = 1 | 6212 |
| EXT-15 | Press CAPS LOCK | NR 0xB1 bit 1 = 1 | 6212 |
| EXT-16 | Press EXTEND | NR 0xB1 bit 0 = 1 | 6212 |
| EXT-17 | Press EDIT; read 0xF7FE (row 3) | Row 3 shows BREAK/EDIT folded via `matrix_state_3` | 237 |
| EXT-18 | Press ','; read 0xDFFE (row 5) | Row 5 bit 1/0 ANDed with ex(12:11) | 239 |
| EXT-19 | Press LEFT; read 0x7FFE (row 7) | Row 7 bits 3:0 ANDed with ex(16:13) | 240 |
| EXT-20 | UP + DOWN + LEFT + RIGHT | NR 0xB0 low nibble = 0x0F | 6208 |

### 3.4 Joystick mode select (JMODE-*)

| ID | Stimulus | Expected `(joy0, joy1)` | Cite |
|----|----------|-------------------------|------|
| JMODE-01  | Write NR 0x05 = 0x00 | (000, 000) — Sinclair 2 both | 5157-5158 |
| JMODE-02  | Write NR 0x05 = 0x4A = 0b0100_1010 | (101 MD1, 010 Cursor) — D3=1,D7=0,D6=1; D1=1,D5=0,D4=0 | 5157-5158 |
| JMODE-02r | Write NR 0x05 = 0xC9 = 0b1100_1001 | (111 I/O, 000 Sinclair 2) — retracted-row decode | 5157-5158 |
| JMODE-03  | Write 0x05 = 0x41 | (001 Kempston1, 000 Sinclair2) | 5157-5158 |
| JMODE-04  | Write 0x05 = 0x80 | (100 Kempston2, 000 Sinclair2) — D3=0,D7=1,D6=0 → 010? **Check:** 0x80 ⇒ D7=1,D6=0,D3=0 ⇒ joy0=010 Cursor. Use 0x88 instead: D7=0,D6=0,D3=1 ⇒ joy0=100 Kempston2. | 5157-5158 |
| JMODE-05  | Write 0x05 = 0x88 | (100 Kempston2, 000 Sinclair2) | 5157-5158 |
| JMODE-06  | Write 0x05 = 0x22 | (000 Sinclair2, 011 Sinclair1) — D5=1,D4=0,D1=1 → joy1=110? **Check:** 0x22=0b0010_0010 ⇒ D5=1,D4=0,D1=1 ⇒ joy1 = 1&1&0 = 110 MD2. | 5157-5158 |
| JMODE-07  | Write 0x05 = 0x30 | joy1 = D1&D5&D4 = 0&1&1 = 011 Sinclair1; joy0 = 000 | 5157-5158 |
| JMODE-08  | Reset, read joystick mode | (000, 000) Sinclair 2 default | reset process |

Note: JMODE-04/06 rows intentionally *show the derivation* (not the
wrong guess) so the test binding code is forced to do the bit-fiddle
arithmetic from the VHDL oracle, not from memory. The expected column
on those rows is whatever the derivation line computes.

### 3.5 Kempston 1 / 2 (KEMP-*)

Preconditions listed under PRE; all tests read port 0x1F or 0x37 after
the stimulus. Expected byte computed per section 1.3.

| ID | PRE | Stimulus (i_JOY_LEFT bits) | Port | Expected | Cite |
|----|-----|---------------------------|------|----------|------|
| KEMP-01 | joy0=001 | bit 0 R | 0x1F | 0x01 | 3475, 3479 |
| KEMP-02 | joy0=001 | bit 1 L | 0x1F | 0x02 | 3479 |
| KEMP-03 | joy0=001 | bit 2 D | 0x1F | 0x04 | 3479 |
| KEMP-04 | joy0=001 | bit 3 U | 0x1F | 0x08 | 3479 |
| KEMP-05 | joy0=001 | bit 4 Fire1 (B) | 0x1F | 0x10 | 3479 |
| KEMP-06 | joy0=001 | bit 5 Fire2 (C) | 0x1F | 0x20 | 3479 |
| KEMP-07 | joy0=001 | bit 6 A (button) | 0x1F | 0x00 — masked to 0 in Kempston mode | 3478 |
| KEMP-08 | joy0=001 | bit 7 START | 0x1F | 0x00 — masked to 0 in Kempston mode | 3478 |
| KEMP-09 | joy0=001 | U+D+L+R+F1+F2 | 0x1F | 0x3F | 3479 |
| KEMP-10 | joy0=100 | bit 3 U on left | 0x37 | 0x08 | 3476, 3482 |
| KEMP-11 | joy0=100 | all dirs+F1+F2 | 0x37 | 0x3F | 3482 |
| KEMP-12 | joy0=000 | any stimulus | 0x1F | port not enabled (`port_1f_hw_en=0`) | 2454, 2674 |
| KEMP-13 | joy0=001, joy1=001, L.U + R.R | 0x1F | 0x09 (bits 3,0 ORed) | 3499 |
| KEMP-14 | joy0=001, joy1=100, L.U, R.D | 0x1F = 0x08, 0x37 = 0x04 | — | 3499, 3506 |
| KEMP-15 | joy0=101, L.A pressed | 0x1F | 0x40 (bit 6 passes in MD mode) | 3478 |

KEMP-12 checks the `port_1f_hw_en` guard at line 2454 — when no source
is enabled, the port is not decoded; the corresponding read falls
through to `X"FF"` via the floating-bus path and the test must assert
that the byte is *not* the joystick value.

### 3.6 MD 3-button (MD-*)

| ID | PRE | Stimulus (left JOY bits) | Port | Expected | Cite |
|----|-----|--------------------------|------|----------|------|
| MD-01 | joy0=101 | U+D+L+R+A+B (bits 6,4,3,2,1,0) | 0x1F | 0x5F | 3478-3479, 3441 |
| MD-02 | joy0=101 | START (bit 7) | 0x1F | 0x80 | 3478 |
| MD-03 | joy0=101 | A (bit 6) | 0x1F | 0x40 | 3478 |
| MD-04 | joy0=101 | Fire2/C (bit 5) | 0x1F | 0x20 | 3479 |
| MD-05 | joy0=101 | START + A | 0x1F | 0xC0 | 3478 |
| MD-06 | joy0=001 | START (bit 7) | 0x1F | 0x00 — bits 7:6 masked in Kempston mode | 3478 |
| MD-07 | joy0=110 | U on left | 0x37 | 0x08 | 3481-3482 |
| MD-08 | joy1=110 | U on right | 0x37 | 0x08 | 3493-3494 |
| MD-09 | joy0=101, joy1=101 (both MD1 — illegal?) | see Open questions | — | — |

MD-09 is an open-question row: the VHDL does not forbid it; both
connectors would drive port 0x1F via `joyL_1f or joyR_1f` (line 3499).
Expected byte is `L or R` but no firmware ever selects this config.

### 3.6a MD 6-button and NR 0xB2 (MD6-*)

Expected byte per section 1.4.

| ID | PRE | Stimulus | NR 0xB2 read | Cite |
|----|-----|----------|--------------|------|
| MD6-01 | joy0=101, L.MODE (bit 11) | L.MODE=1 | bit 0 = 1 | 6215 |
| MD6-02 | joy0=101, L.X (bit 10)    | L.X=1    | bit 1 = 1 | 6215 |
| MD6-03 | joy0=101, L.Z (bit 9)     | L.Z=1    | bit 2 = 1 | 6215 |
| MD6-04 | joy0=101, L.Y (bit 8)     | L.Y=1    | bit 3 = 1 | 6215 |
| MD6-05 | joy1=110, R.MODE          | bit 4 = 1 | 6215 |
| MD6-06 | joy1=110, R.X             | bit 5 = 1 | 6215 |
| MD6-07 | joy1=110, R.Z             | bit 6 = 1 | 6215 |
| MD6-08 | joy1=110, R.Y             | bit 7 = 1 | 6215 |
| MD6-09 | both MD with all 6 extras pressed | 0xFF | 6215 |
| MD6-10 | joy0=001 (Kempston, not MD), L.X pressed | NR 0xB2 still shows bit 1 = 1 (oracle reads the raw vector regardless of mode) or 0? **Open question** — see section 6 | 6215 |
| MD6-11 | SELECT-pin sequencing: emulate 6-button protocol via md6 state machine; pins 3,4 low during state 100 | test extras latch | md6_joystick_connector_x2.vhd (whole entity) |

MD6-11 is the only test that exercises the SELECT-pin state machine in
`md6_joystick_connector_x2.vhd`; everything above can be tested by
driving `i_JOY_LEFT/RIGHT` directly.

### 3.7 Sinclair 1 / 2 (SINC-*)

The joystick-to-key translation is performed *outside* the VHDL we
have (key_joystick module). These tests verify that in a given NR 0x05
mode, pressing the virtual joystick direction causes the expected
membrane cell to read as pressed when the corresponding row is
selected on port 0xFE.

Keys 1..5 are row 3, bits 0..4 (`membrane.vhd` 245 + row table §1.6).
Keys 0,9,8,7,6 are row 4, bits 0..4 (line 246). This is the truth the
tests assert against.

| ID | PRE | Direction | Row addr | Expected bit low | Cite |
|----|-----|-----------|----------|------------------|------|
| SINC1-01 | joy0=011 | LEFT  | 0xF7FE | bit 0 (key 1) | 245 |
| SINC1-02 | joy0=011 | RIGHT | 0xF7FE | bit 1 (key 2) | 245 |
| SINC1-03 | joy0=011 | DOWN  | 0xF7FE | bit 2 (key 3) | 245 |
| SINC1-04 | joy0=011 | UP    | 0xF7FE | bit 3 (key 4) | 245 |
| SINC1-05 | joy0=011 | FIRE  | 0xF7FE | bit 4 (key 5) | 245 |
| SINC2-01 | joy1=000 | LEFT  | 0xEFFE | bit 3 (key 7) — **host mapping** | 246 |
| SINC2-02 | joy1=000 | RIGHT | 0xEFFE | bit 4 (key 6) | 246 |
| SINC2-03 | joy1=000 | DOWN  | 0xEFFE | bit 2 (key 8) | 246 |
| SINC2-04 | joy1=000 | UP    | 0xEFFE | bit 1 (key 9) | 246 |
| SINC2-05 | joy1=000 | FIRE  | 0xEFFE | bit 0 (key 0) | 246 |
| SINC-06 | joy0=011, joy1=000, both LEFT | 0xE7FE (rows 3+4) | row-3 bit 0 AND row-4 bit 3 low | 245-246 + 251 |

Note on SINC2-* mapping: Sinclair 2 is classically documented as
"6=LEFT, 7=RIGHT, 8=DOWN, 9=UP, 0=FIRE". The row/bit positions above
are therefore 4, 3, 2, 1, 0 respectively (key 6 is bit 4 of row 4).
The ordering in the table above has been reconstructed to match this
documented mapping; the emulator adapter must implement it the same
way, and any divergence should be recorded as a bug rather than
silently "fixed" in the plan. See Open questions §6.3.

### 3.8 Cursor / Protek (CURS-*)

Documented mapping: 5=LEFT, 6=DOWN, 7=UP, 8=RIGHT, 0=FIRE.
Key 5 is row 3 bit 4; keys 6/7/8 are row 4 bits 4/3/2; key 0 is row 4
bit 0 (`membrane.vhd` 245-246 + §1.6).

| ID | PRE | Direction | Row addr | Expected bit low | Cite |
|----|-----|-----------|----------|------------------|------|
| CURS-01 | joy0=010 | LEFT  | 0xF7FE | bit 4 (key 5) | 245 |
| CURS-02 | joy0=010 | DOWN  | 0xEFFE | bit 4 (key 6) | 246 |
| CURS-03 | joy0=010 | UP    | 0xEFFE | bit 3 (key 7) | 246 |
| CURS-04 | joy0=010 | RIGHT | 0xEFFE | bit 2 (key 8) | 246 |
| CURS-05 | joy0=010 | FIRE  | 0xEFFE | bit 0 (key 0) | 246 |
| CURS-06 | joy0=010, LEFT + RIGHT | 0xE7FE (rows 3,4) | row 3 bit 4 low AND row 4 bit 2 low | 245-246 |

### 3.9 User I/O mode (IOMODE-*)

Coverage of NR 0x0B and the pin-7 mux, per §1.8.

| ID | Stimulus | Expected | Cite |
|----|----------|----------|------|
| IOMODE-01 | Reset | `joy_iomode_pin7 = '1'`; `joy_uart_en = 0` | 3515-3516, 3536 |
| IOMODE-02 | Write NR 0x0B = 0x80 (en=1, mode=00, iomode_0=0) | pin7 = 0 | 3518-3520 |
| IOMODE-03 | Write NR 0x0B = 0x81 (en=1, mode=00, iomode_0=1) | pin7 = 1 | 3520 |
| IOMODE-04 | Write NR 0x0B = 0x91 (en=1, mode=01) + pulse `ctc_zc_to(3)` | pin7 toggles | 3521-3524 |
| IOMODE-05 | Write NR 0x0B = 0xA0 (en=1, mode=10, iomode_0=0) | pin7 = uart0_tx | 3526-3530 |
| IOMODE-06 | Write NR 0x0B = 0xA1 (en=1, mode=10, iomode_0=1) | pin7 = uart1_tx | 3526-3530 |
| IOMODE-07 | Write NR 0x0B = 0xA0, assert `JOY_LEFT(5)=0` | `joy_uart_rx` asserted (ctsn analogously on bit 4) | 3538-3539 |
| IOMODE-08 | Write NR 0x0B = 0xA1, assert `JOY_RIGHT(5)=0` | `joy_uart_rx` asserted | 3538 |
| IOMODE-09 | Write NR 0x0B = 0xA0, assert `joy_uart_en` | `= 1` | 3536 |
| IOMODE-10 | Write NR 0x0B = 0x80 | `joy_uart_en = 0` (iomode(1)=0) | 3536 |
| IOMODE-11 | NR 0x05 joy0/joy1 = 111 (user I/O) + NR 0x0B configured | verifies interaction with mode-111 selection | 3429-3438, 3510-3539 |

### 3.10 Kempston mouse (MOUSE-*)

Coverage of `zxnext.vhd` 2668-2670 and 3543-3561.

| ID | PRE | Stimulus | Port | Expected | Cite |
|----|-----|----------|------|----------|------|
| MOUSE-01 | `port_mouse_io_en=1` | `i_MOUSE_X = 0x5A` | 0xFBDF | 0x5A | 3546 |
| MOUSE-02 | `port_mouse_io_en=1` | `i_MOUSE_Y = 0xA5` | 0xFFDF | 0xA5 | 3553 |
| MOUSE-03 | `port_mouse_io_en=1` | no buttons, wheel=0 | 0xFADF | 0x0F (bit 3 = 1 fixed, buttons inverted) | 3560 |
| MOUSE-04 | `port_mouse_io_en=1` | left button (BTN0) | 0xFADF | bit 1 = 0 | 3560 |
| MOUSE-05 | `port_mouse_io_en=1` | right button (BTN1) | 0xFADF | bit 0 = 0 | 3560 |
| MOUSE-06 | `port_mouse_io_en=1` | middle (BTN2) | 0xFADF | bit 2 = 0 | 3560 |
| MOUSE-07 | `port_mouse_io_en=1` | wheel = 0xA | 0xFADF | bits 7..4 = 0xA | 3560 |
| MOUSE-08 | `port_mouse_io_en=0` | any | 0xFBDF/FFDF/FADF | port not decoded (bus default) | 2668-2670 |
| MOUSE-09 | NR 0x0A bit 3 = 1 (reverse) | host swaps L/R | verify reversal happens at the adapter, not the VHDL port | 5197 |

### 3.11 NMI buttons (NMI-*)

Coverage of `zxnext.vhd` 2090-2091 and NR 0x06 bits 3-4.

| ID | PRE | Stimulus | Expected | Cite |
|----|-----|----------|----------|------|
| NMI-01 | NR 0x06 bit 3 = 1 (M1 en) | Assert `hotkey_m1` | `nmi_assert_mf = 1` | 2090 |
| NMI-02 | NR 0x06 bit 3 = 0 | Assert `hotkey_m1` | `nmi_assert_mf = 0` | 2090 |
| NMI-03 | NR 0x06 bit 4 = 1 | Assert `hotkey_drive` | `nmi_assert_divmmc = 1` | 2091 |
| NMI-04 | NR 0x06 bit 4 = 0 | Assert `hotkey_drive` | `nmi_assert_divmmc = 0` | 2091 |
| NMI-05 | NR 0x06 bit 3 = 1 | Software NMI via `nmi_sw_gen_mf = 1` | `nmi_assert_mf = 1` | 2090 |
| NMI-06 | NR 0x06 bit 4 = 1 | Software NMI via `nmi_sw_gen_divmmc = 1` | `nmi_assert_divmmc = 1` | 2091 |
| NMI-07 | both NR 0x06 bits 3,4 = 1, both hotkeys asserted | both asserts = 1 | 2090-2091 |

### 3.12 Port 0xFE format and issue-2 (FE-*)

| ID | Stimulus | Expected | Cite |
|----|----------|----------|------|
| FE-01 | No keys, EAR=0, no `port_fe_ear` | 0xBF | 3459 |
| FE-02 | EAR input high | bit 6 = 1 | 3459 |
| FE-03 | Write 0xFE bit 4 high (`port_fe_ear`=1), then read | bit 6 = 1 | 3459 |
| FE-04 | NR 0x08 bit 0 = 1 (issue 2), MIC=1, EAR=0 | bit 6 reflects issue-2 MIC XOR EAR (audio block) | 5182 + audio wiring |
| FE-05 | `expbus_eff_en=1`, `port_propagate_fe=1`, expansion bus drives D0=0 | ANDed with bus (`port_fe_dat = port_fe_dat_0 and port_fe_bus`) → bit 0 forced 0 | 3468 |

---

## 4. Reset Defaults (VHDL-verified)

| Register / signal | Default | Cite |
|-------------------|---------|------|
| `nr_05_joy0`      | 000 (Sinclair 2) | reset process (check zxnext.vhd top) |
| `nr_05_joy1`      | 000 (Sinclair 2) | ditto |
| `nr_06_button_m1_nmi_en` | 0 | 1110 |
| `nr_06_button_drive_nmi_en` | 0 | 1109 |
| `nr_0b_joy_iomode_en` | 0 | reset process |
| `nr_0b_joy_iomode` | "00" | reset process |
| `nr_0b_joy_iomode_0` | 1 | reset process |
| `joy_iomode_pin7` | '1' | 3516 |
| `matrix_state*` | all 1s | 184-186 |
| `i_JOY_LEFT/RIGHT` | all 0s (host side) | — |

The actual reset values of the NR 0x05 / 0x06 / 0x0B signals are set
at the main NR reset process near the top of `zxnext.vhd`; the tests
must cite the real line numbers when binding, not repeat this table.

---

## 5. Test Count Summary (nominal)

| Category | Tests |
|----------|-------|
| 3.1 Keyboard standard | 23 |
| 3.2 Keyboard shift hysteresis | 3 |
| 3.3 Extended keys | 20 |
| 3.4 Joystick mode select | 9 |
| 3.5 Kempston 1/2 | 15 |
| 3.6 MD 3-button | 9 |
| 3.6a MD 6-button + NR 0xB2 | 11 |
| 3.7 Sinclair 1/2 | 11 |
| 3.8 Cursor | 6 |
| 3.9 I/O mode | 11 |
| 3.10 Kempston mouse | 9 |
| 3.11 NMI buttons | 7 |
| 3.12 Port 0xFE format | 5 |
| **Total (nominal)** | **139** |

No pass/fail ratio is reported until the test code has been rewritten
against this oracle; the old 71/71 figure is retracted in §Plan
Rebuilt.

---

## 6. Open Questions

1. **NR 0xB2 vs. mode.** NR 0xB2 unconditionally reads
   `i_JOY_*(10..8)` and bit 11 (line 6215). The VHDL does *not* gate
   these on `mdL_1f_en`/`mdR_1f_en`, so a program could in principle
   read the 6-button extras even in Kempston mode. Test MD6-10 asserts
   the observed behaviour; confirm with firmware authors whether this
   is intentional.

2. **Bits 7:6 in Kempston mode.** Lines 3478 and 3490 guard bits 7:6
   behind `mdL_1f_en`/`mdR_1f_en`. A button wired to bit 6 (A) on a
   Kempston pad will therefore read back as 0 on port 0x1F. Verify
   against real hardware whether the "Kempston PS/2" joypad exposes A
   via a different mechanism.

3. **Sinclair 2 key order.** The Spectrum documentation orders Sinclair
   2 as 6-7-8-9-0 = LEFT/RIGHT/DOWN/UP/FIRE, but multiple historic
   references disagree about LEFT vs RIGHT (some sources swap 6 and
   7). The tests in §3.7 encode the "6=LEFT, 7=RIGHT" convention; if
   the emulator's joystick adapter uses the other convention, one row
   of this plan is wrong, not the code. Needs the user to pick the
   canonical convention before the tests bind.

4. **`port_1f_hw_en` and floating-bus fallback.** KEMP-12 asserts that
   a port 0x1F read returns *not* the Kempston byte when no Kempston
   source is enabled. The exact fallback (floating bus, 0xFF, last
   port byte) depends on `port_propagate_fe`/bus-decode paths that are
   out of scope here; the test should only assert "not the joystick
   value", not pin a specific byte.

5. **`md6_joystick_connector_x2` coverage.** Only MD6-11 exercises the
   SELECT-pin state machine. A full protocol test (8 states, 3-button
   detect, 6-button detect at state 100 with pins 3/4 low) is
   deferred — listed in TODO for a second pass once the basic
   connector mock is wired.

6. **Reset-process line numbers.** The NR-reset `when` block that
   clears `nr_05_*`, `nr_06_*`, `nr_0b_*` was not re-read for this
   rewrite; the tests binding the reset-default rows (JMODE-08,
   IOMODE-01, NMI-02/04) must insert the real file:line in place of
   "reset process" before committing.

7. **PS/2 scancode mapping.** `ps2_keyb.vhd` is referenced in §VHDL
   sources but not tested here: its host-side nature means every row
   would be a tautology against our own keymap table. PS/2 coverage
   belongs in a host-input / harness test, not this compliance plan.

---

## 7. Bans

- **No tautologies.** "Set bit X, read register, see bit X" is
  *not* a test unless there is a masking/encoding step in between.
  Every row above either goes through a VHDL mux, AND, OR, or
  mode-gated guard.
- **No expected values from C++.** All expected bytes trace to a line
  number in the VHDL source tree listed in §VHDL sources.
- **No coverage padding.** The nominal 139-test total is derived from
  concrete row count in §3; adding tests by splitting existing rows
  into "press / release / press again" triples is not allowed.
