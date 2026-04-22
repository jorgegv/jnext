# Input Subsystem Compliance Test Plan

VHDL-derived compliance test plan for the JNEXT emulator's keyboard
membrane, extended-key matrix, joystick subsystem (Kempston 1/2, Sinclair
1/2, Cursor, Megadrive 3/6 button, user-defined I/O mode), Kempston
mouse, and the Multiface / Drive NMI buttons.

## Current status (2026-04-22, after Task 3 Input SKIP-reduction plan)

- `test/input/input_test.cpp`: **139 / 133 / 0 / 6** (pass/fail/skip = 133/0/6).
  - 6 remaining skips are all `IOMODE-05/06/07/08/09/10` — UART pin-7
    routing modes 10 and 11 of NR 0x0B. F-skip blocked on the future
    UART+I2C subsystem plan.
- `test/input/input_int_integration_test.cpp` (NEW): **7 / 5 / 0 / 2**.
  - Hosts the 7 port-0xFE-byte-assembly rows re-homed from
    `input_test.cpp` (KBD-22/23, FE-01..05).
  - 2 honest skips: FE-04 (issue-2 MIC^EAR — needs MIC/EAR analog
    feedback wiring), FE-05 (expansion-bus AND — jnext does not model
    expansion bus).

**Plan-doc inconsistency note (2026-04-22)**: §3.7 (Sinclair 1 + Sinclair
2 row labels) had the keymaps swapped relative to the authoritative
`ram/init/keyjoy_64_6.coe` oracle. Three independent sources agree
(COE, VHDL `zxnext.vhd:3429-3438` mode comment table, FUSE
`peripherals/joystick.c`). Test rows updated to match COE; the §3.7
plan text itself is queued for a follow-up edit.

For the full SKIP-reduction execution log see
[doc/design/TASK3-INPUT-SKIP-REDUCTION-PLAN.md](../design/TASK3-INPUT-SKIP-REDUCTION-PLAN.md).

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

### 1.4 NR 0xB2 — MD 6-button extras (`zxnext.vhd` 6215)

```
port_253b_dat <= i_JOY_RIGHT(10 downto 8) & i_JOY_RIGHT(11)
              &  i_JOY_LEFT(10 downto 8)  & i_JOY_LEFT(11);
```

VHDL concatenation is MSB-first, so `A(10 downto 8)` contributes bits
(10),(9),(8) in that order. Combined with the signal layout from
`zxnext.vhd` 3441-3442 (`MODE X Z Y START A C B U D L R` at positions
11..0, i.e. X=10, Z=9, Y=8, MODE=11), NR 0xB2 decodes as:

```
bit  7     6     5     4          3     2     1     0
     R.X   R.Z   R.Y   R.MODE     L.X   L.Z   L.Y   L.MODE
     R(10) R(9)  R(8)  R(11)      L(10) L(9)  L(8)  L(11)
```

Both nibbles are symmetric: top = right connector, bottom = left.

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

Reset: `joy_iomode_pin7 <= '1'` at `zxnext.vhd:3515-3516`; NR 0x0B
bits come from the reset block at `zxnext.vhd:4939-4941`
(`nr_0b_joy_iomode_en := '0'`, `nr_0b_joy_iomode := "00"`,
`nr_0b_joy_iomode_0 := '1'`).

### 1.9 Kempston mouse (`zxnext.vhd` 2668-2670, 3543-3561)

```
port_fbdf (0xFBDF) <= i_MOUSE_X                               -- line 3546
port_ffdf (0xFFDF) <= i_MOUSE_Y                               -- line 3553
port_fadf (0xFADF) <= i_MOUSE_WHEEL & '1' & !BTN2 & !BTN0 & !BTN1   -- line 3560
```

`i_MOUSE_WHEEL` is a 4-bit vector (`zxnext.vhd` 104), so the port
byte layout is:

```
bit  7..4          3   2      1      0
     wheel[3..0]   1   !BTN2  !BTN0  !BTN1
```

Decoded only when `port_mouse_io_en = 1` (line 2668). NR 0x0A bit 3
(`nr_0a_mouse_button_reverse`, reset default 0 at line 1127) swaps
left/right in the host adapter — verify separately from the port
decode. NR 0x0A bits 1:0 (`nr_0a_mouse_dpi`, reset default "01" at
line 1128) select the host-side DPI divisor and are observable only
through the rate at which `i_MOUSE_X/Y` increment per physical
movement unit; this is a host-harness property, so it is covered by
MOUSE-10 as a rate assertion rather than a VHDL-bit assertion.

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

Per `zxnext.vhd` 5157-5158, `nr_05_joy0 = D3 & D7 & D6` and
`nr_05_joy1 = D1 & D5 & D4`. VHDL `&` is MSB-first, so for `joy0` the
3-bit code reads `{D3, D7, D6}` high-to-low, and for `joy1` it reads
`{D1, D5, D4}` high-to-low.

| | Stimulus | Expected (old) | Expected (VHDL) |
|---|---|---|---|
| JMODE-02 (retracted) | Write NR 0x05 = 0xC9 | joy0=101 (MD1), joy1=010 (Cursor) | joy0=111 (I/O mode), joy1=000 (Sinclair 2) |
| JMODE-02 (corrected) | Write NR 0x05 = 0x68 = 0b0110_1000 | joy0 = {D3,D7,D6} = {1,0,1} = 101 (MD1); joy1 = {D1,D5,D4} = {0,1,0} = 010 (Cursor) | same |

Derivation of 0x68: to get joy0=101 we need D3=1, D7=0, D6=1; to get
joy1=010 we need D1=0, D5=1, D4=0. Don't-care bits D2, D0 are chosen
as 0. The byte is `0 1 1 0 1 0 0 0 = 0x68`. The retracted 0xC9 row is
kept as JMODE-02r so the I/O-mode + Sinclair-2 decode is also covered.

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

Each row below is fully derived: for every byte, the expected
`(joy0, joy1)` is what `{D3,D7,D6}` and `{D1,D5,D4}` compute to per
`zxnext.vhd` 5157-5158. No "check" sidebars, no guessing.

| ID | Stimulus | joy0 = {D3,D7,D6} | joy1 = {D1,D5,D4} | Expected `(joy0, joy1)` | Cite |
|----|----------|-------------------|-------------------|-------------------------|------|
| JMODE-01  | NR 0x05 = 0x00 = 0b0000_0000 | {0,0,0}=000 | {0,0,0}=000 | (000 Sinclair 2, 000 Sinclair 2) | 5157-5158 |
| JMODE-02  | NR 0x05 = 0x68 = 0b0110_1000 | {1,0,1}=101 | {0,1,0}=010 | (101 MD 1, 010 Cursor) | 5157-5158 |
| JMODE-02r | NR 0x05 = 0xC9 = 0b1100_1001 | {1,1,1}=111 | {0,0,0}=000 | (111 I/O mode, 000 Sinclair 2) | 5157-5158 |
| JMODE-03  | NR 0x05 = 0x40 = 0b0100_0000 | {0,0,1}=001 | {0,0,0}=000 | (001 Kempston 1, 000 Sinclair 2) | 5157-5158 |
| JMODE-04  | NR 0x05 = 0x08 = 0b0000_1000 | {1,0,0}=100 | {0,0,0}=000 | (100 Kempston 2, 000 Sinclair 2) | 5157-5158 |
| JMODE-05  | NR 0x05 = 0x88 = 0b1000_1000 | {1,1,0}=110 | {0,0,0}=000 | (110 MD 2, 000 Sinclair 2) | 5157-5158 |
| JMODE-06  | NR 0x05 = 0x22 = 0b0010_0010 | {0,0,0}=000 | {1,1,0}=110 | (000 Sinclair 2, 110 MD 2) | 5157-5158 |
| JMODE-07  | NR 0x05 = 0x30 = 0b0011_0000 | {0,0,0}=000 | {0,1,1}=011 | (000 Sinclair 2, 011 Sinclair 1) | 5157-5158 |
| JMODE-08  | Power-on, read joystick mode | — | — | (001 Kempston 1, 000 Sinclair 2) — signal-declaration defaults | 1105-1106 |

JMODE-08 cites the signal-declaration line numbers because the soft
reset block at `zxnext.vhd` 4926-4942 does not clear `nr_05_joy0` or
`nr_05_joy1`. Their only defaults are the initialisers at
`zxnext.vhd:1105` (`nr_05_joy0 := "001"`) and `zxnext.vhd:1106`
(`nr_05_joy1 := "000"`), so the cold-boot mode is Kempston 1 on the
left connector and Sinclair 2 on the right — not "(000, 000)" as the
retracted plan said.

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

Per §1.4 the NR 0xB2 byte is
`{R.X, R.Z, R.Y, R.MODE, L.X, L.Z, L.Y, L.MODE}` at bit positions
`{7, 6, 5, 4, 3, 2, 1, 0}`. Each row drives exactly one bit of
`i_JOY_LEFT` or `i_JOY_RIGHT` and asserts the mapped NR 0xB2 bit.

| ID | PRE | Stimulus (one bit high) | NR 0xB2 read | Cite |
|----|-----|------------------------|--------------|------|
| MD6-01 | joy0=101 | `i_JOY_LEFT(11)` (L.MODE)  | bit 0 = 1 | 6215 + 3442 |
| MD6-02 | joy0=101 | `i_JOY_LEFT(8)`  (L.Y)     | bit 1 = 1 | 6215 + 3442 |
| MD6-03 | joy0=101 | `i_JOY_LEFT(9)`  (L.Z)     | bit 2 = 1 | 6215 + 3442 |
| MD6-04 | joy0=101 | `i_JOY_LEFT(10)` (L.X)     | bit 3 = 1 | 6215 + 3442 |
| MD6-05 | joy1=110 | `i_JOY_RIGHT(11)` (R.MODE) | bit 4 = 1 | 6215 + 3442 |
| MD6-06 | joy1=110 | `i_JOY_RIGHT(8)`  (R.Y)    | bit 5 = 1 | 6215 + 3442 |
| MD6-07 | joy1=110 | `i_JOY_RIGHT(9)`  (R.Z)    | bit 6 = 1 | 6215 + 3442 |
| MD6-08 | joy1=110 | `i_JOY_RIGHT(10)` (R.X)    | bit 7 = 1 | 6215 + 3442 |
| MD6-09 | both MD | all of `JOY_LEFT(11..8)` and `JOY_RIGHT(11..8)` high | 0xFF | 6215 |
| MD6-10 | joy0=001 (Kempston, not MD), `i_JOY_LEFT(10)=1` | bit 3 of NR 0xB2 = 1 (VHDL at 6215 reads the raw vector unconditionally, no mode gating) | 6215 |

Derivation of the left-nibble mapping (mirrored for the right): line
6215 builds the byte as `R(10) R(9) R(8) R(11) L(10) L(9) L(8) L(11)`
from MSB to LSB because `A & B & C` in VHDL places `A` at the
most-significant position. That fixes the left nibble at
`L.X=bit 3, L.Z=bit 2, L.Y=bit 1, L.MODE=bit 0`. Any prior row
claiming "L.X → bit 1" was reading the slice upside-down.

#### 3.6a.1 MD6 SELECT-pin state machine (MD6-11a..MD6-11i)

Coverage of `md6_joystick_connector_x2.vhd`. The state machine at
lines 66-117 of that file cycles `state(3 downto 0)` through an
8-phase sequence (bit 0 = connector, 0=left / 1=right, driven to
`o_joy_select` at line 117; bit 3:1 = sub-phase). Six-button detect
happens at sub-phase `100` (lines 157-161) by checking
`i_joy_1_n or i_joy_2_n`, and the extras (MODE X Y Z) are latched at
sub-phase `101` (lines 163-171) into bits `(11 downto 8)` with
ordering `joy_4_n & joy_3_n & joy_1_n & joy_2_n` (i.e. MODE=!joy_4_n,
X=!joy_3_n, Z=!joy_1_n, Y=!joy_2_n after the final `not` at lines
192-193). Eight sub-phases * 2 connectors = 16 distinct `state(3:0)`
values; only the `0000`, `0010/011x`, `0011/011x`, `0100/100x`, and
`0101/101x` sub-phases mutate state, so the test walks just those.

Stimuli drive `i_joy_1_n..i_joy_9_n` and `i_CLK_EN` at the expected
cadence so the state machine advances one sub-phase per row.

| ID | Phase `state(3:0)` | `o_joy_select` | Purpose | Stimulus | Expected | Cite |
|----|---------------------|----------------|---------|----------|----------|------|
| MD6-11a | 0000 (left, sub=000)  | 0 | init clear   | any pad state | `joy_left_n <= (others=>'1')`, `joy_right_n <= (others=>'1')`, `joy_left_six_button_n <= '1'` | md6_joystick_connector_x2.vhd:135-139 |
| MD6-11b | 0100 (left, sub=010)  | 0 | bits 7:6 latch (L) | `i_joy_3_n=0, i_joy_4_n=0, i_joy_9_n=0, i_joy_6_n=1` | `joy_left_n(7 downto 6) <= "01"` → final `o_joy_left(7)='1'` (START), `(6)='0'` | 141-144 |
| MD6-11c | 0110 (left, sub=011)  | 0 | bits 5:0 latch (L) | drive `i_joy_9_n/6_n/1_n/2_n/3_n/4_n` active-low to press `C,B,U,D,L,R` (line 121 builds `joy_raw` in this order) | `joy_left_n(5:0) <= joy_raw`, so `o_joy_left(5:0)` becomes `{C,B,U,D,L,R}` active-high | 121, 151-152 |
| MD6-11d | 1000 (left, sub=100)  | 0 | 6-button detect (L) | `i_joy_1_n=0, i_joy_2_n=0` | `joy_left_six_button_n <= '0'` (6-button pad) | 157-158 |
| MD6-11e | 1010 (left, sub=101)  | 0 | extras latch (L, 6-btn only) | `i_joy_4_n=0 (MODE), i_joy_3_n=1, i_joy_1_n=0 (Z), i_joy_2_n=0 (Y)` | `joy_left_n(11:8) <= "0100"` → `o_joy_left(11)='1'` MODE, `(10)='0'` X, `(9)='1'` Z, `(8)='1'` Y | 163-166 |
| MD6-11f | 0101 (right, sub=010) | 1 | bits 7:6 latch (R) | `i_joy_3_n=0, i_joy_4_n=0, i_joy_9_n=1, i_joy_6_n=0` | `joy_right_n(7:6) <= "10"` → `o_joy_right(7)='0'`, `(6)='1'` | 146-149 |
| MD6-11g | 0111 (right, sub=011) | 1 | bits 5:0 latch (R) | `joy_raw` all inactive (all `_n=1`) | `joy_right_n(5:0) <= "111111"` → `o_joy_right(5:0)="000000"` | 154-155 |
| MD6-11h | 1011 (right, sub=101) | 1 | extras latch (R) | `joy_right_six_button_n='0'`, `i_joy_4_n=1, i_joy_3_n=0, i_joy_1_n=0, i_joy_2_n=1` | `joy_right_n(11:8) <= "1001"` → `o_joy_right(11)='0'`, `(10)='1'` X, `(9)='1'` Z, `(8)='0'` Y | 168-171 |
| MD6-11i | 1000 (left, sub=100), 3-button pad | 0 | 3-button detect (L) | `i_joy_1_n=1 or i_joy_2_n=1` (a single-missing pin is enough) | `joy_left_six_button_n <= '1'`; the subsequent `101` sub-phase does NOT update `joy_left_n(11:8)` (line 164 guard), so bits 11:8 stay at their `0000` reset from sub-phase 0000 → `o_joy_left(11:8)="0000"` | 158, 163-166 |

The `state_rest` mask at line 100 ORs sub-phases where `state(8:4)` is
non-zero; only the 8 sub-phases enumerated above are "live". MD6-11a
must be issued first (or after a reset / `io_mode_change`) because
`joy_left_n`/`joy_right_n` are otherwise sticky across phases.

`io_mode_change` (line 96) and `i_reset` both force the state machine
to `"111110000"` (line 107) and clear the outputs (lines 185-186,
128-129). An extra row MD6-11j can be added to assert that toggling
`i_io_mode_en` mid-sequence aborts whatever latches were in progress;
this is tracked in Open Questions §6.5 rather than shipped here
because the test harness for `io_mode_change` is not yet specified.

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
| MOUSE-10 | `port_mouse_io_en=1` | `i_MOUSE_WHEEL = 0xF` (max), then 0x0 (wrap) | 0xFADF | bits 7..4 track `i_MOUSE_WHEEL` directly; VHDL at 3560 does no sign extension, so wrap is a pure 4-bit unsigned roll-over and any "signed wheel delta" semantics live in the host adapter | 104, 3560 |
| MOUSE-11 | `port_mouse_io_en=1`, `nr_0a_mouse_dpi = "00"` vs `"11"` | same physical motion | no visible change in `i_MOUSE_X/Y` at the VHDL port — DPI scaling is applied in the host mouse adapter before it drives `i_MOUSE_X/Y`, not inside `zxnext.vhd`. Open question §6.6. | 1128, 5198 |

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

Every entry below is pinned to a specific `zxnext.vhd` line. Entries
labelled "signal decl" come from the signal declaration block at
1100-1135 (the value after `:=`), which is the power-up value
because the soft-reset process at 4926-4942 does NOT clear these
particular signals. Entries labelled "reset process" come from the
`if reset = '1'` block inside that process.

| Register / signal | Default | Cite | Kind |
|-------------------|---------|------|------|
| `nr_05_joy0`      | "001" (Kempston 1) | `zxnext.vhd:1105` | signal decl |
| `nr_05_joy1`      | "000" (Sinclair 2) | `zxnext.vhd:1106` | signal decl |
| `nr_06_button_m1_nmi_en` | '0' | `zxnext.vhd:1110` | signal decl |
| `nr_06_button_drive_nmi_en` | '0' | `zxnext.vhd:1109` | signal decl |
| `nr_06_hotkey_cpu_speed_en` | '1' | `zxnext.vhd:4932` | reset process |
| `nr_06_hotkey_5060_en` | '1' | `zxnext.vhd:4933` | reset process |
| `nr_0a_mouse_button_reverse` | '0' | `zxnext.vhd:1127` | signal decl |
| `nr_0a_mouse_dpi` | "01" | `zxnext.vhd:1128` | signal decl |
| `nr_0b_joy_iomode_en` | '0' | `zxnext.vhd:4939` | reset process |
| `nr_0b_joy_iomode` | "00" | `zxnext.vhd:4940` | reset process |
| `nr_0b_joy_iomode_0` | '1' | `zxnext.vhd:4941` | reset process |
| `joy_iomode_pin7` | '1' | `zxnext.vhd:3515-3516` | reset process |
| `matrix_state*` | all 1s | `membrane.vhd:184-186` | reset process |
| `i_JOY_LEFT/RIGHT` | all 0s | host-driven | — |

Note on the cold-boot joystick mode: because the signal declarations
at `zxnext.vhd:1105-1106` give `nr_05_joy0 := "001"` and
`nr_05_joy1 := "000"`, a cold-booted Next is in (Kempston 1, Sinclair
2) mode, not "(Sinclair 2, Sinclair 2)". JMODE-08 asserts this
specific default and fails loudly if any reset path silently zeroes
`nr_05_joy0`.

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
| 3.6a MD 6-button + NR 0xB2 (incl. MD6-11a..i) | 19 |
| 3.7 Sinclair 1/2 | 11 |
| 3.8 Cursor | 6 |
| 3.9 I/O mode | 11 |
| 3.10 Kempston mouse | 11 |
| 3.11 NMI buttons | 7 |
| 3.12 Port 0xFE format | 5 |
| **Total (nominal)** | **149** |

No pass/fail ratio is reported until the test code has been rewritten
against this oracle; the old 71/71 figure is retracted in §Plan
Rebuilt.

**Current status (2026-04-15):** test code rewritten and merged to main.
Honest pass rate: **21/23 live, 126 skip** — the skips document plan rows
that cannot be exercised against the current C++ surface. 2 live failures
are legitimate Task 3 emulator bugs, both in `NextReg::reset()`: JMODE-08
expects NR 0x05 = 0x40 per `zxnext.vhd:1105-1106` + the soft-reset block
at `:4920-4945` that intentionally does NOT clear `nr_05_joy*`; IOMODE-01
expects NR 0x0B = 0x01 per `zxnext.vhd:4939-4941` + packing at `:5200-5203`.
`NextReg::reset()` currently zeroes the whole register file instead of
applying signal-declaration defaults.

**Task 3 implementation debt (summary of the 126 skipped rows):** keyboard
CS hysteresis + extended-column folding + NR 0xB0/0xB1 extended-key matrix;
entire joystick subsystem (NR 0x05 decoder, joy0/joy1 mux, port 0x1F/0x37
handlers, Kempston 1/2, MD 3-button, MD 6-button state machine + NR 0xB2,
Sinclair 1/2 and Cursor joy→key adapters); NR 0x0B user-I/O pin-7 mux +
UART routing; Kempston mouse ports 0xFADF/0xFBDF/0xFFDF + enable gate;
NR 0x06 NMI gating; port 0xFE byte assembly wrapper + NR 0x08 issue-2/3
select + expansion-bus AND.

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

5. **`md6_joystick_connector_x2` `io_mode_change` coverage.** Rows
   MD6-11a..MD6-11i walk the non-io-mode 8-phase state machine, but
   the `io_mode_change` path (line 96, 106, 184) that aborts the
   sequence and re-enters at state `"111110000"` is only exercised
   indirectly via IOMODE-*. A dedicated row MD6-11j that asserts
   "toggle `i_io_mode_en` mid-sub-phase-101 and verify `joy_left_n`
   returns to all-1s on the next `i_CLK_EN`" is listed here as TODO
   once the connector harness supports it.

6. **Mouse DPI scaling visibility.** `nr_0a_mouse_dpi` (default "01"
   at `zxnext.vhd:1128`, written at 5198) has no VHDL consumer inside
   `zxnext.vhd` — it is exposed on `o_MOUSE_CONTROL` at line 1599 and
   consumed by the host PS/2 mouse driver. MOUSE-11 asserts the
   absence of any in-core effect; whether the host adapter actually
   divides its increment counter by the DPI factor is out of scope
   for this VHDL-compliance plan.

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
