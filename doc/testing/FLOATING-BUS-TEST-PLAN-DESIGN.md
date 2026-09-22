# Emulator Floating Bus Compliance Test Plan

VHDL-derived compliance test plan for the ZX Spectrum Next emulator's
floating-bus read paths. Two ports expose the floating bus: port 0xFF
(48K/128K timing) and port 0x0FFD (+3 timing). The port-0xFF read mux
lives on `Emulator::floating_bus_read` rather than on the `Ula` class,
because the VHDL gating is assembled at the top level of `zxnext.vhd`
(NR 0x08 `ff_rd_en`, per-machine timing selection, Timex `port_ff_reg`
override) around the `o_ula_floating_bus` signal exported from
`zxula.vhd`. Port 0x0FFD on +3 is a separate decode that reads
`port_p3_floating_bus_dat` (itself derived from `ula_floating_bus`
with the `port_7ffd_locked` gate). This plan owns the 5 rows re-homed
from §10 of the ULA plan plus a small set of VHDL-justified neighbours
that the Emulator-level surface now makes observable.

## Purpose

The "floating bus" is a historically load-bearing ZX Spectrum read:
during active display the ULA drives the last fetched VRAM byte onto
the bus, and older software uses that for raster-timed effects and
tape-loader hardware detection. On the Next FPGA two ports are wired
as floating-bus surfaces, and the visible behaviour is the composition
of several gates across `zxula.vhd` + `zxnext.vhd`:

1. The ULA captures VRAM bytes at specific `hc` phases and exports
   `o_ula_floating_bus` (`zxula.vhd:308-345` + `:573`). On +3 timing,
   `zxula.vhd:573` ORs bit 0 with 1 (`or i_timing_p3`) and in the
   border/disabled fallback substitutes `i_p3_floating_bus` for the
   48K/128K `X"FF"`.
2. `zxnext.vhd:4513` wires `ula_floating_bus` to port 0xFF only on
   48K/128K timing; on +3 / Pentagon / Next-base, port 0xFF is hard-
   forced to `X"FF"`.
3. `zxnext.vhd:4517` wires `ula_floating_bus` to the `+3`-specific
   `port_p3_floating_bus_dat` shadow, which in turn reaches the bus
   only via port 0x0FFD (decoded at `zxnext.vhd:2589` with the
   `p3_timing_hw_en` + `port_p3_floating_bus_io_en` gates). Line 4517
   substitutes `X"FF"` when `port_7ffd_locked = '1'`.
4. `zxnext.vhd:2813` gates port 0xFF: if NR 0x08 bit 2 is set AND
   `port_ff_io_en` is asserted, port 0xFF returns the Timex
   `port_ff_reg` instead of the floating-bus byte. `port_ff_rd` itself
   is decoded unconditionally at `zxnext.vhd:2713`
   (`port_ff_rd <= iord and port_ff`); `port_ff_io_en` gates the write
   (`:2714`) and the Timex arm of the read mux (`:2813`), not the
   decode.

Because the observable output is the top-level composition, the tests
in this plan drive port-0xFF and port-0x0FFD reads on a full
`Emulator` fixture. They are NOT `Ula` unit tests — the Ula plan
explicitly re-homes those 5 rows here (see
`doc/design/TASK-FLOATING-BUS-PLAN.md`).

All expected values in this plan are derived from the VHDL sources at
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.
The VHDL-as-oracle rule and the pass/fail/skip taxonomy follow
`doc/testing/UNIT-TEST-PLAN-EXECUTION.md`.

## Current status

- **0 rows live.** No `test/floating_bus/` suite exists today.
- 5 rows inherited from the ULA Phase-4 re-home
  (`doc/design/TASK-FLOATING-BUS-PLAN.md`, landed 2026-04-23):
  FB-01 (= S10.01), FB-03 (= S10.05), FB-04 (= S10.06),
  FB-06 (= S10.07), FB-07 (= S10.08).
- Production target under test: `Emulator::floating_bus_read`
  (declaration `src/core/emulator.h:293`; definition
  `src/core/emulator.cpp:2651-2700`; call site
  `src/core/emulator.cpp:173` — wired as `port_.set_default_read`
  so any unmapped port read, including 0xFF, falls through to it).
- Audit status: Phase 0 not executed. The existing C++ uses a
  FUSE/ZesarUX-style `tstate_in_line % 8` model that needs to be
  walked through `zxula.vhd:308-345` before un-skipping every row.
- **Known buggy implementation.** The existing
  `Emulator::floating_bus_read` (`src/core/emulator.cpp` around lines
  2651-2700; the comment at line 2654 falsely claims universal VHDL
  behaviour) returns VRAM-derived bytes on ALL machine types, which
  `zxnext.vhd:4513` explicitly forbids on +3/Pentagon/Next for port
  0xFF. Rows FB-4A/FB-4B/FB-4C will fail against today's code — they
  are legitimate emulator-bug witnesses and Phase 1 un-skipping of
  those rows is expected to motivate a fix.

## Scope

| Area                                                             | VHDL source                                   | Section |
|------------------------------------------------------------------|-----------------------------------------------|---------|
| Border-phase read returns 0xFF (48K/128K)                        | `zxula.vhd:312-316` + `:573`                  | 1       |
| Active-display capture (hc phases 0x9/B/D/F)                     | `zxula.vhd:319-340`                           | 2       |
| +3 port 0xFF hard-forced to 0xFF                                 | `zxnext.vhd:4513`                             | 3       |
| +3 port 0x0FFD: bit-0 force (active display)                     | `zxula.vhd:573` + `zxnext.vhd:4517`           | 3       |
| +3 port 0x0FFD: border fallback via `p3_floating_bus_dat`        | `zxnext.vhd:4498-4509` + `:4517` + `zxula.vhd:573` | 3  |
| +3 port 0x0FFD: `port_7ffd_locked` gate                          | `zxnext.vhd:4517`                             | 3       |
| +3 port 0x0FFD: `p3_timing_hw_en` / `port_p3_floating_bus_io_en` decode gate | `zxnext.vhd:2589`                 | 3       |
| Per-machine ULA-vs-0xFF selection (port 0xFF)                    | `zxnext.vhd:4513`                             | 4       |
| Port 0xFF read path (default-handler route)                      | `src/core/emulator.cpp:173, 2651-2700`        | 5       |
| NR 0x08 `ff_rd_en` Timex override                                | `zxnext.vhd:2813` + `:5180`                   | 6       |
| `port_ff_io_en` gate (Timex arm only; decode is unconditional)   | `zxnext.vhd:2397`, `:2713`, `:2813`           | 6       |

## Architecture

### Test approach

Each test row constructs a full `Emulator` fixture at a specific
machine timing, arranges VRAM contents and raster position, then
performs a port 0xFF read OR a port 0x0FFD read (only on +3) and
compares the byte against the VHDL-derived expected value. Raster
positioning uses the emulator's existing T-state bookkeeping
(`clock_`, `frame_cycle_`, `timing_.tstates_per_line`) — no
synthetic Ula state is injected.

Three classes of check coexist:

1. **Border / timing / gating rows** assert a scalar expected byte
   (0xFF, a VRAM byte, or `port_ff_reg`) at a deterministic
   `(line, tstate_in_line)` coordinate or register configuration.
2. **hc-phase rows** sweep `tstate_in_line % 8` inside an active
   display line and assert which phase returns VRAM data vs. 0xFF.
   These mirror the `hc(3:0)` case in `zxula.vhd:319-340` as
   observed through the 8T-granular Emulator model.
3. **+3 port-0x0FFD rows** exercise the separate `port_p3_float`
   decode (`zxnext.vhd:2589`) and the `port_p3_floating_bus_dat`
   mux (`zxnext.vhd:4517`). These rows target the +3 machine
   timing and use an `IN A,(0x0FFD)`-style stimulus, which is a
   different port from 0xFF.

### File layout

Per TASK-FLOATING-BUS-PLAN Phase 1 option A (new suite), not an
extension of `test/port/` or `test/ula/`:

```
test/
  floating_bus/
    floating_bus_test.cpp      # All rows; full Emulator fixture
    CMakeLists.txt             # Registers floating_bus_test with CTest
doc/testing/
  FLOATING-BUS-TEST-PLAN-DESIGN.md   # This document
doc/design/
  TASK-FLOATING-BUS-PLAN.md          # Re-home manifest (existing)
```

A new suite is preferred over extending `test/port/` because the
stimulus requires raster-phase control (placing the CPU at a known
`tstate_in_line % 8` during active display), which is not the
concern of the port-dispatch suite. Both port 0xFF (all machines)
and port 0x0FFD (+3 only) rows live in the same suite; the
stimulus driver picks the IN port and the machine type per row.

## Section 1: Border-phase read returns 0xFF

### VHDL reference

`zxula.vhd:312-316`:

```vhdl
if border_active_ula = '1' then
   floating_bus_r <= X"FF";
   floating_bus_en <= '0';
else
   case i_hc(3 downto 0) is
      ...
```

`zxula.vhd:416`: `border_active_ula <= i_hc(8) or border_active_v;`
`zxula.vhd:414`: `border_active_v <= i_vc(8) or (i_vc(7) and i_vc(6));`
`zxula.vhd:573`:

```vhdl
o_ula_floating_bus <=
      (floating_bus_r(7 downto 1) & (floating_bus_r(0) or i_timing_p3))
         when (border_active_ula = '0' and floating_bus_en = '1')
   else i_p3_floating_bus when i_timing_p3 = '1'
   else X"FF";
```

During any border region (`border_active_ula = '1'`), `floating_bus_r`
is held at 0xFF and `floating_bus_en = '0'`. Line 573's first arm is
disabled (`floating_bus_en = 0`), so on 48K/128K timing the final
`else X"FF"` arm delivers the byte. This is the row that the Ula plan
filed as S10.01.

### Test rows

| Row ID | Machine | Position | Stimulus | Expected | VHDL cite |
|--------|---------|----------|----------|----------|-----------|
| FB-01  | 48K     | Line in V-border (`line < 64` or `line >= 256`) | port 0xFF read | 0xFF | `zxula.vhd:312-316,414,573` |
| FB-02  | 48K     | Display line 36, read after the reload of `hc_ula` 267 (`267 & 15 = 0xB`, an attribute reload were it inside the display); the attribute byte it would fetch is seeded | port 0xFF read | 0xFF | `zxula.vhd:316,414-416,573` |

FB-01 is the S10.01 re-home. FB-02 is a VHDL-justified neighbour:
`border_active_ula` ORs `i_hc(8)` with the V-border, so horizontal
blanking inside the vertical active window must also read 0xFF,
whatever the reload schedule of Section 2 says for that `hc(3:0)`.
Positions are in the ULA's own counters (Section 10).

## Section 2: Active-display capture phases

### VHDL reference

`zxula.vhd:319-340`:

```vhdl
case i_hc(3 downto 0) is
   when X"1" =>
      floating_bus_r <= X"FF";
      floating_bus_en <= '0';
   when X"9" =>
      floating_bus_r <= i_ula_vram_d;
      floating_bus_en <= '1';
   when X"B" =>
      floating_bus_r <= i_ula_vram_d;
   when X"D" =>
      floating_bus_r <= i_ula_vram_d;
   when X"F" =>
      floating_bus_r <= i_ula_vram_d;
   when others => null;
end case;
```

Per the 16-phase `hc` cycle, phase 0x1 resets the register + disables,
phase 0x9 reloads from VRAM and enables, phases 0xB/0xD/0xF reload
while `floating_bus_en` stays asserted. The other phases preserve
the register (bus keeps its last captured byte). Line 573 gates the
output on `floating_bus_en = '1'` (first arm); while disabled the
final `else X"FF"` applies on 48K/128K timing.

The reload happens on the **falling** edge of `CLK_7`, half-way
through the `hc_ula` count, and the byte on `i_ula_vram_d` at counts
9/B/D/F is the fetch set up two counts earlier (`vram_a` on the rising
edges ending counts 7/9/B/D, `zxula.vhd:224-258`): pixel(2k),
attribute(2k), pixel(2k+1), attribute(2k+1) of the 16-count block k.
A read at the start of count `h` therefore sees the reload of count
`h-1`, and count 0 of a block still holds the previous block's
attribute(2k+1).

`Emulator::ula_floating_bus_active_arm` evaluates this in `hc_ula` /
`vc_ula` (GH #265 follow-up, see Section 10): one `hc_ula` count is 4
master cycles, half a CPU T-state at 3.5 MHz. The test helper
`set_fb_capture(dline, col, kind)` places a direct read at the start of
the count after the reload of `col`'s pixel or attribute.

### Test rows

| Row ID | Machine | Position | Stimulus | Expected | VHDL cite |
|--------|---------|----------|----------|----------|-----------|
| FB-2A  | 48K | Display line 36, after the `hc(3:0) = 9` reload of block 2 | Seed pixel (36, col 4); port 0xFF read | Pixel byte of column 4 | `zxula.vhd:325-327` |
| FB-2B  | 48K | Same line, after the `hc(3:0) = B` reload | Seed attribute (36, col 4); port 0xFF read | Attribute byte of column 4 | `zxula.vhd:329-330` |
| FB-2C  | 48K | Same line, after the `hc(3:0) = D` reload | Seed pixel (36, col 5); port 0xFF read | Pixel byte of column 5 | `zxula.vhd:332-333` |
| FB-2D  | 48K | Same line, after the `hc(3:0) = F` reload | Seed attribute (36, col 5); port 0xFF read | Attribute byte of column 5 | `zxula.vhd:335-336` |
| FB-2E  | 48K | Same line, count 16·2+5 (after the `hc(3:0) = 1` reset, before the next 9); all four bytes of the block seeded | port 0xFF read | 0xFF (reset/idle half) | `zxula.vhd:321-323,573` |
| FB-2F  | 48K | Raw line 50 (< `c_min_vactive` 64, vertical border), at the count where a displayed line would show a pixel | port 0xFF read | 0xFF (above active display window) | `zxula.vhd:414-416,573` |

FB-2A..FB-2F expand the 3 rows (S10.02/03/04) that were G-commented
out of the ULA plan as "internal, end-to-end by §1/§2". At the Ula
abstraction they were unobservable; at the Emulator-level floating-bus
surface they are direct public-API checks.

The `hc(3:0)`-vs-8T question this section used to carry as a caveat
is resolved: see Section 10 and Open question 2. These rows were
re-derived from the VHDL schedule in GH #265's follow-up; the old
stimulus (`tstate_in_line % 8` of the RAW line, `{2,3,4,5}` = VRAM)
sat 58 T (48K) / 62 T (128K) before the ULA's fetch window.

## Section 3: +3 floating-bus paths — port 0xFF vs port 0x0FFD

### VHDL reference

`zxula.vhd:573` (repeated for convenience):

```vhdl
o_ula_floating_bus <=
      (floating_bus_r(7 downto 1) & (floating_bus_r(0) or i_timing_p3))
         when (border_active_ula = '0' and floating_bus_en = '1')
   else i_p3_floating_bus when i_timing_p3 = '1'
   else X"FF";
```

`zxnext.vhd:4513`:

```vhdl
port_ff_dat_ula <= ula_floating_bus
   when (machine_timing_48 = '1' or machine_timing_128 = '1')
   else X"FF";
```

`zxnext.vhd:4517`:

```vhdl
port_p3_floating_bus_dat <= ula_floating_bus
   when port_7ffd_locked = '0' else X"FF";
```

`zxnext.vhd:2589` (port 0x0FFD decode):

```vhdl
port_p3_float <= '1' when cpu_a(15 downto 12) = "0000"
   and port_fd = '1'
   and p3_timing_hw_en = '1'
   and port_p3_floating_bus_io_en = '1' else '0';
```

Two +3-specific behaviours live in `zxula.vhd:573`:

1. **Bit-0 force to 1.** When `i_timing_p3 = '1'` AND the first arm
   is taken (active display, capture enabled), the low bit of the
   captured VRAM byte is ORed with 1. This is how +3 advertises
   "I am a +3" to detection code.
2. **Border/disabled fallback.** When the first arm is not taken
   (border or `floating_bus_en = 0`) AND `i_timing_p3 = '1'`, the
   output is `i_p3_floating_bus` (the last contended CPU r/w byte
   latched at `zxnext.vhd:4498-4509`), not the 48K/128K `X"FF"`.

Crucially, **port 0xFF on +3 does NOT observe either of these
behaviours**: `zxnext.vhd:4513` hard-forces port 0xFF → `X"FF"` on
+3/Pentagon/Next-base, bypassing `ula_floating_bus` entirely. The +3
floating bus is observable only through **port 0x0FFD**, where
`port_p3_floating_bus_dat` (line 4517) is wired by the
`port_p3_float_rd_dat` mux at `zxnext.vhd:2814`. Line 4517 further
forces `X"FF"` when `port_7ffd_locked = '1'`. The decode at
`zxnext.vhd:2589` gates port 0x0FFD on `p3_timing_hw_en` (48K/128K
timing cannot see port 0x0FFD at all) AND on
`port_p3_floating_bus_io_en` (internal port enable bit 4, see
`zxnext.vhd:2403`).

### Test rows

| Row ID | Machine | Port | Stimulus | Expected | VHDL cite |
|--------|---------|------|----------|----------|-----------|
| FB-03  | +3 | 0xFF | Active display, VRAM capture phase | 0xFF (port 0xFF hard-forced on +3) | `zxnext.vhd:4513` |
| FB-03a | +3 | 0x0FFD | Active display, VRAM capture phase; VRAM byte with bit 0 = 0; `port_7ffd_locked = 0` | Returned byte bit 0 forced to 1; bits 7:1 = VRAM byte | `zxula.vhd:573` + `zxnext.vhd:4517` |
| FB-04  | +3 | 0xFF | Border; last contended mem r/w wrote 0xA5 | 0xFF (port 0xFF hard-forced on +3, shadow does not reach port 0xFF) | `zxnext.vhd:4513` |
| FB-04a | +3 | 0x0FFD | Border; last contended mem r/w wrote 0xA5; `port_7ffd_locked = 0` | 0xA5 (from `ula_floating_bus` border arm via `i_p3_floating_bus` latch `p3_floating_bus_dat`) | `zxula.vhd:573` + `zxnext.vhd:4498-4509,4517` |
| FB-04b | +3 | 0x0FFD | ONE emulator, both sources seeded 0x42 (bit 0 clear); read at border, then in the active-display capture phase | Border 0x42 (raw second waveform), active 0x43 (first waveform's `or i_timing_p3`) — the bit-0 force is present in exactly one arm | `zxula.vhd:573` + `zxnext.vhd:4478,4498-4509,4517` |
| FB-3A  | +3 | 0x0FFD | Border; `port_7ffd_locked = 1` | 0xFF (`port_p3_floating_bus_dat` forced to `X"FF"` when locked) | `zxnext.vhd:4517` |
| FB-3B  | +3 | 0x0FFD | Active display; `port_p3_floating_bus_io_en = 0` (bit 4 of `internal_port_enable` cleared) | 0xFF (decode `port_p3_float = 0` → `port_p3_float_rd` never asserts → no `port_internal_rd_response` → cpu_di default `X"FF"`; GH #111) | `zxnext.vhd:2403, 2589, 2716, 2803-2806, 1877` |
| FB-3C  | 48K | 0x0FFD | Active display, any phase (any `port_7ffd_locked`) | 0xFF (decode gated by `p3_timing_hw_en` → port 0x0FFD not decoded on 48K → no read strobe → cpu_di default `X"FF"`; GH #111) | `zxnext.vhd:2589, 2716, 2803-2806, 1877` |
| FB-3D  | 128K | 0x0FFD | Active display, any phase | 0xFF (same reason as FB-3C; port 0x0FFD is not floating-bus-reactive on 128K) | `zxnext.vhd:2589, 2716, 2803-2806, 1877` |
| ~~FB-3E~~  | ~~Pentagon~~ | — | **RETIRED 2026-05-04** — the standalone Pentagon machine type was dropped (Wave 0.3 follow-up), so this row has no machine to run on. FB-3D (128K) and FB-3F (Next) still cover the decode-gate path. No `check()` row exists. | — | — |
| FB-3F  | Next | 0x0FFD | Border; latch seeded 0x42; fresh Next-base (`tim_sel` default = 011 = +3 timing) | 0x42 — the decode is **active** (`p3_timing_hw_en` follows `machine_timing`, and Next-base defaults to +3 timing), so the border waveform returns the raw latch. NOT 0xFF (which is what a blocked decode yields, GH #111). | `zxnext.vhd:2589, 1099` + `zxula.vhd:573` |

FB-03 is the re-homed S10.05: corrected expected value per
`zxnext.vhd:4513` (port 0xFF forces 0xFF on +3). The bit-0 force
exercised by the original row lives on port 0x0FFD and is now
FB-03a. FB-04 is the re-homed S10.06: corrected expected value
per `zxnext.vhd:4513`. The `p3_floating_bus_dat` behaviour exercised
by the original row is on port 0x0FFD and is FB-04a. FB-3A is the
`port_7ffd_locked` gate, now correctly targeting port 0x0FFD.
FB-3B covers the `port_p3_floating_bus_io_en` AND-term of the
port-0x0FFD decode (`zxnext.vhd:2589`). FB-3C/3D are VHDL-justified
neighbours: they lock down that port 0x0FFD is NOT a floating-bus
surface on 48K/128K, because the `p3_timing_hw_en` AND-term at
`zxnext.vhd:2589` blocks the decode altogether (blocked decode → no
read strobe → `cpu_di` default `X"FF"`, GH #111). **FB-3F is not one
of them**: `p3_timing_hw_en` mirrors `machine_timing`, and a
Next-base machine boots with `tim_sel = 011` (+3 timing,
`zxnext.vhd:1099`), so port 0x0FFD *is* decoded there — the row
asserts the decoded border-arm value and exists to rule the blocked
path OUT. FB-3E is retired (no Pentagon machine type since
2026-05-04).

### The bit-0 force is scoped to the active-display arm (GH #112)

`zxula.vhd:573` is a **conditional signal assignment** (VHDL LRM
§10.5.3) — an ordered priority chain of *independent* waveforms, not
one Boolean expression:

```vhdl
o_ula_floating_bus <=
     (floating_bus_r(7 downto 1) & (floating_bus_r(0) or i_timing_p3))
         when (border_active_ula = '0' and floating_bus_en = '1')
     else i_p3_floating_bus when i_timing_p3 = '1'
     else X"FF";
```

`or i_timing_p3` sits inside the parenthesised concatenation of the
**first** waveform. The second waveform is the bare signal
`i_p3_floating_bus`, wired 1:1 from `p3_floating_bus_dat`
(`zxnext.vhd:4478`); that signal's only driver (`:4498-4509`) latches
`cpu_di` / `cpu_do` verbatim. So on +3 the border arm returns the
last contended CPU bus byte **raw** — bit 0 included.

jnext forced bit 0 on both arms until GH #112. FB-04a could not
detect it (it seeds 0xA5, whose bit 0 is already set); **FB-04b** is
the discriminative contrast row, and FB-3X / FB-3F /
FIX-FB-EFFLOCK-01 were corrected from `latch | 0x01` to the raw
latch at the same time.

Note (resolved by GH #111, owner-approved rewrite 2026-07-26): the
blocked-decode read value is `0xFF`, NOT the `X"00"` at
`zxnext.vhd:2814`. That `X"00"` is only the wired-or contribution
into `port_rd_dat` (`:2837`); with the decode blocked the strobe
`port_p3_float_rd <= iord and port_p3_float` (`:2716`) never asserts,
no other strobe in the `port_internal_rd_response` OR-list
(`:2803-2806`) decodes 0x0FFD (port_7ffd decodes it on non-+3 timing
but is write-only), so the cpu_di IORQ mux falls through to its
unconditional `cpu_di <= X"FF"` default (`:1877`). FB-3B/3C/3D
originally asserted 0x00 (the misreading this note anticipated);
rewritten to 0xFF per two independent VHDL derivations (GH #109
thread + GH #111).

## Section 4: Per-machine ULA-vs-0xFF selection

### VHDL reference

`zxnext.vhd:4513`:

```vhdl
port_ff_dat_ula <= ula_floating_bus
   when (machine_timing_48 = '1' or machine_timing_128 = '1')
   else X"FF";
```

Only 48K and 128K timings deliver the ULA floating-bus byte onto port
0xFF. +3, Pentagon, and Next-base timings read 0xFF (before NR 0x08's
Timex override — see §6). Note that this gate is distinct from the
gate inside `zxula.vhd:573` — line 573 decides what the
`o_ula_floating_bus` signal exports, and line 4513 decides whether
that signal is wired to port 0xFF at all.

### Test rows

| Row ID | Machine | Stimulus | Expected | VHDL cite |
|--------|---------|----------|----------|-----------|
| FB-4A  | 128K | Active display, capture phase, VRAM byte `0x5A` | 0x5A (ULA floating bus reaches port 0xFF on 128K timing) | `zxnext.vhd:4513` |
| ~~FB-4B~~  | ~~Pentagon~~ | **RETIRED 2026-05-04** — the standalone Pentagon machine type was dropped (Wave 0.3 follow-up), so this row has no machine to run on. FB-4C (Next) still covers the same gate path (non-48K/128K timing → port 0xFF hard-forced 0xFF). No `check()` row exists. | — | — |
| FB-4C  | Next (default) | Active display, any phase | 0xFF (not 48K/128K, so ULA bus not wired to port 0xFF) | `zxnext.vhd:4513` |

FB-4A and FB-4C are VHDL-justified neighbours of the 5 re-homed rows.
They protect against a regression where a future refactor might apply
the 48K floating-bus logic uniformly across machine types. FB-4B is
retired (no Pentagon machine type since 2026-05-04).

## Section 5: Port 0xFF read path wiring

### VHDL reference

`zxnext.vhd:2713` (decode — unconditional):

```vhdl
port_ff_rd <= iord and port_ff;
```

`zxnext.vhd:2813` (read mux):

```vhdl
port_ff_rd_dat <=
      port_ff_dat_tmx
         when nr_08_port_ff_rd_en = '1' and port_ff_io_en = '1' and port_ff_rd = '1'
   else port_ff_dat_ula when port_ff_rd = '1'
   else X"00";
```

`zxnext.vhd:2714`: `port_ff_wr <= iowr and port_ff and port_ff_io_en;`
`zxnext.vhd:2397`: `port_ff_io_en <= internal_port_enable(0);`

Key wiring facts to avoid a common misreading:

- The **decode** `port_ff_rd` (`zxnext.vhd:2713`) is NOT gated by
  `port_ff_io_en`. It fires on every IN from any port with low byte
  0xFF, on every machine. `port_ff_io_en` gates the **write** decode
  (`:2714`) and the **Timex arm** of the read mux (`:2813`).
- When `port_ff_io_en = '0'` AND `nr_08_port_ff_rd_en = '1'`, the
  first arm of `port_ff_rd_dat` is disabled (the AND-term collapses),
  so the second arm (`port_ff_dat_ula`) wins and the ULA
  floating-bus byte still reaches the CPU.
- `port_ff_dat_ula` itself is gated per-machine at `zxnext.vhd:4513`:
  it carries `ula_floating_bus` on 48K/128K and `X"FF"` elsewhere.

### Host implementation

`Emulator::floating_bus_read` (`src/core/emulator.cpp:2651-2700`) is
the host-side implementation of `port_ff_dat_ula` for 48K/128K. It is
wired in at `src/core/emulator.cpp:173` as the port-dispatch default
read handler, so any unmapped port read falls through to it. Port
0xFF is unmapped on 48K/128K, so a CPU `IN A,(FF)` reaches
`floating_bus_read()`. This is deliberately not an `Ula::` method
because the final byte depends on NR 0x08 and machine timing, both of
which are outside the Ula's concern.

### Test rows

| Row ID | Machine | Stimulus | Expected | VHDL cite |
|--------|---------|----------|----------|-----------|
| FB-06  | 48K | CPU executes `IN A,(0xFF)` at border; NR 0x08 bit 2 = 0 (reset default) | 0xFF (via `Emulator::floating_bus_read`, through `port_ff_dat_ula`) | `zxnext.vhd:2713, 2813` + host `emulator.cpp:173` |
| FB-5A  | 48K | CPU executes `IN A,(0xFF)` (A = 0) starting at FUSE T 14328+224·36+16; NR 0x08 bit 2 = 0 (reset default) | Pixel byte of line 36 column 4 (FUSE 1.6 sweep) | `zxnext.vhd:2713, 2813`; `zxula.vhd:319-340,573` |

FB-06 is the S10.07 re-home. FB-5A confirms the wiring exercises the
active-display branch. Both rows go through the full port-dispatch
path (not a direct `floating_bus_read()` call) so any regression in
the `port_.set_default_read` binding is caught. NR 0x08 bit 2 = 0
is stated explicitly (even though it is the reset default, per
`zxnext.vhd:1118`) so each row is self-contained: with the bit set,
`port_ff_rd_dat` would take the Timex arm and the floating-bus
assertion would not hold.

## Section 6: NR 0x08 Timex override and `port_ff_io_en`

### VHDL reference

`zxnext.vhd:2813` (first-arm condition):

```vhdl
port_ff_rd_dat <=
      port_ff_dat_tmx
         when nr_08_port_ff_rd_en = '1' and port_ff_io_en = '1' and port_ff_rd = '1'
```

`zxnext.vhd:5180`: `nr_08_port_ff_rd_en <= nr_wr_dat(2);`
`zxnext.vhd:2397`: `port_ff_io_en <= internal_port_enable(0);`
`zxnext.vhd:3630`: `port_ff_dat_tmx <= port_ff_reg;`
`port_ff_reg` is the Timex screen-mode byte (set via port 0xFF
writes, `zxnext.vhd:3614-3622`).

When NR 0x08 bit 2 is set AND `port_ff_io_en = '1'` (i.e. the port is
enabled in `internal_port_enable(0)`), port 0xFF *reads* the Timex
register, not the floating bus. This allows software to read back
what it wrote via port 0xFF. `internal_port_enable(0)` is '1' by
default at boot (declared at `zxnext.vhd:1226`:
`nr_82_internal_port_enable : std_logic_vector(7 downto 0) := (others => '1');`,
and `internal_port_enable` draws its low byte from
`nr_82_internal_port_enable` — see `zxnext.vhd:2392`). This plan
asserts the gate structure rather than the enable-bit write path
(which belongs to the NextReg / port-enable plan).

Reset defaults: `nr_08_port_ff_rd_en := '0'` (`zxnext.vhd:1118`) so
the Timex arm is off at cold boot — floating bus wins.

### Test rows

| Row ID | Machine | Stimulus | Expected | VHDL cite |
|--------|---------|----------|----------|-----------|
| FB-07  | 48K | Write NR 0x08 = 0x04 (bit 2 set); write port 0xFF = 0x05 (Timex HiRes); `IN A,(0xFF)` at border | 0x05 (Timex register wins; NOT 0xFF floating bus) | `zxnext.vhd:2813,5180,3630` |
| FB-6A  | 48K | Reset state (NR 0x08 = 0x00); write port 0xFF = 0x05; `IN A,(0xFF)` at border | 0xFF (NR 0x08 gate not set → floating-bus path) | `zxnext.vhd:1118,2813,5180` |
| FB-6B  | 48K | NR 0x08 = 0x04; disable port 0xFF by clearing `internal_port_enable(0)`; `IN A,(0xFF)` at border | 0xFF (`port_ff_io_en=0` drops the Timex arm; floating-bus arm takes over) | `zxnext.vhd:2397,2813` |

FB-07 is the S10.08 re-home. FB-6A is a VHDL-justified neighbour that
pins the reset default — without it, a bug that leaves
`nr_08_port_ff_rd_en = '1'` at reset would silently break every other
row in this plan. FB-6B exercises the `port_ff_io_en` leg of line
2813's three-term AND that is not reachable from FB-07 alone.

## Section 8: GH #109 — floating bus is scoped to the LSB-0xFF decode

### VHDL reference

`zxnext.vhd:2571+2583` — `port_ff <= '1' when cpu_a(7 downto 0) = X"FF"`
(LSB-only decode: EVERY 0x??FF port is the port-0xFF mux, and ONLY
those). `zxnext.vhd:2803-2806` — `port_internal_rd_response` OR-list:
a port matching none of the listed read strobes produces no internal
response. `zxnext.vhd:1868-1878` — the cpu_di IORQ mux: with no
internal response and no expansion-bus device, `cpu_di <= X"FF"`,
unconditionally, in every machine timing.

Pre-fix, jnext wired `Emulator::floating_bus_read` (the port-0xFF
mux) as `PortDispatch`'s catch-all default for every unmatched port —
so in 48K/128K timing an undecoded port read leaked the ULA floating
bus, and under NextZXOS (Timex gates set) it leaked the last
port-0xFF write (#102 session-3 finding,
`doc/issues/g46b-102-tx1696-freeze-session3.md` §2). GH #109 moved
the mux onto a registered LSB-0xFF read handler and made the
dispatch default return 0xFF.

### Test rows

| Row ID | Machine | Stimulus | Expected | VHDL cite |
|--------|---------|----------|----------|-----------|
| FB-109-01 | 48K | Pixel capture of (36, col 4), VRAM seeded 0x5A; read undecoded port 0x40A7 | 0xFF (undecoded default; floating bus must NOT leak) | `zxnext.vhd:1877, 2583, 2803-2806` |
| FB-109-02 | 48K | Same geometry; read port 0x40FF (LSB 0xFF, high byte ≠ 0) | 0x5A (VRAM byte — LSB-only port_ff decode keeps ALL 0x??FF ports on the mux) | `zxnext.vhd:2571+2583, 2813, 4513`; `zxula.vhd:319-340, 573` |

FB-109-01 is the discriminator (fails pre-fix). FB-109-02 pins that
the narrowing did not shrink the mux below its true LSB-only scope.
The Next-mode/Timex-arm side of GH #109 lives in the port-dispatch
plan (rows GH109-01/02 in
`IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md`).

## Section 9: GH #265 — the CPU samples the bus at its I/O cycle

### VHDL reference

`floating_bus_r` reaches the CPU through combinational logic only:
`o_ula_floating_bus` (`zxula.vhd:573`) → `port_ff_dat_ula` /
`port_p3_floating_bus_dat` (`zxnext.vhd:4513, 4517`) → `port_rd_dat`
(`:2813-2814, 2837`) → `cpu_di` (`:1872-1873`). The T80 latches `cpu_di`
into `DI_Reg` on the falling edge of the I/O cycle's T3
(`t80na.vhd:214-222`); with `IOWait = 1` the I/O cycle is four clocks
(`t80n.vhd:1781-1782`), so the byte is the one on the bus 3.5 T-states into
it — after every contention stretch of the cycle (GH #265 follow-up: the
cycle is now charged clock by clock, see CONTENTION-TEST-PLAN-DESIGN.md
CT-IOC). jnext used to evaluate the raster at `clock_`, the START of the
instruction — 10.5 T-states early for `IN A,(n)` (M1 + operand read = 7 T
before the I/O cycle), 11.5 for `IN A,(C)` (two M1s).
`Emulator::io_read_sample_cycle()` supplies the latch instant.

The first version of these rows (GH #265) placed the IN on the old `T%8`
model of Section 2. The follow-up re-derived them from FUSE 1.6 (48K) and
from the VHDL (+3 port 0x0FFD, which FUSE does not model). FUSE, 48K,
`IN A,(0xFF)` with A = 0: an IN STARTING at INT-relative
`14328 + 224·L + 8·g + k` returns pixel(2g), attribute(2g), pixel(2g+1),
attribute(2g+1) of display line L for k = 0..3 and 0xFF for k = 4..7
(every T of the frame swept, no inconsistent sample). `IN A,(C)` reaches
its I/O cycle one T later, so it returns the k+1 byte.

### Test rows

Display line 36, block g = 2 (columns 4/5), FUSE's screen fill (pixel
`c | (y&1)<<5`, attribute `0x40 | c | (row&1)<<5`). Code at 0x8000
(bank 2) is uncontended.

| Row ID | Machine | Stimulus | Expected | Oracle / VHDL cite |
|--------|---------|----------|----------|--------------------|
| FB-GH265-01 | 48K | `IN A,(0xFF)` started at FUSE T 14328+224·36+17 (k = 1) | 0x44, attribute of column 4 (start-of-instruction sampling: 0xFF) | FUSE 1.6; `zxula.vhd:573`; `t80na.vhd:214-222` |
| FB-GH265-02 | 48K | `IN A,(C)`, BC = 0x00FF, started at k = 0 | 0x44 — one T later into its I/O cycle than `IN A,(n)`, so the attribute, not the pixel | FUSE 1.6; `zxula.vhd:573`; `t80na.vhd:214-222` |
| FB-GH265-03 | +3 | `IN A,(C)`, BC = 0x0FFD, started at raw (line 100, T 72); contended latch seeded 0xA4 | 0x43 — latch at master cycle 667 of the line = `hc_ula` count 41, `hc(3:0) = 9`, pixel of column 4 with bit 0 forced; start-of-instruction sampling: count 18, idle half, border arm → 0xA4 | `zxula.vhd:319-340,573`; `zxnext.vhd:4517`; `t80na.vhd:214-222` |

## Section 10: GH #265 follow-up — the ULA's own counters, Timex, scroll, shadow

### VHDL reference

The floating bus is a function of the ULA's `hc_ula` / `vc_ula`
counters — the same counters contention and NR 0x1E/0x1F use:
`hc_ula = 0` at raw hc `c_min_hactive - 11` (117 on 48K, 125 on
128K/+3), `vc_ula = 0` on raw line `c_min_vactive` (64)
(`zxula_timing.vhd:423-451`). The reload schedule is Section 2's. The
byte loaded is the one the ULA fetched (`zxula.vhd:191-258`):

- `screen_mode_s <= i_port_ff_reg(2:0)` unless the 128K shadow screen is
  on, then `"000"` (`:191`); the shadow screen fetches from bank 7
  (`zxnext.vhd:6649-6656`).
- `px(7:3) <= i_hc(7:3) + scroll_x(7:3)` (NR 0x26), `py <= vc + scroll_y`
  (NR 0x27) folded into 0..191 (`:192-209`).
- `addr_p = py(7:6) & py(2:0) & py(5:3)`, `addr_a = "110" & py(7:3)`
  (`:220-221`); pixel `screen_mode(0) & addr_p & px`; attribute
  `'1' & addr_p & px` in hi-colour/hi-res, else
  `screen_mode(0) & addr_a & px` (`:236-252`).

The model this replaces read "raw line 64..255 × raw T 0..127,
`T%8 ∈ {2,3,4,5}`" of jnext's frame — 58 T (48K) / 62 T (128K) before
the ULA's fetch — always from bank 5's standard layout, and divided the
frame position by the CONFIGURED 3.5 MHz divisor instead of working in
master cycles.

**FUSE ↔ jnext mapping.** FUSE's T counter is INT-relative, jnext's
counts from the raw frame top. jnext's memory contention, verified
against FUSE on every T of a frame, puts FUSE's first contended T
(14335 / 14361) on raw T 14396 / 14656 (the first `kPat48` stretch at
`hc_ula(3:1) = 010`, `zxula.vhd:582-583`), so FUSE T = raw T − 61 (48K)
/ − 295 (128K). The FUSE-derived rows place the instruction with that
mapping (`at_fuse_T()`), moving the clock and the contention counter
together as a running frame does.

**FUSE vs VHDL, where they differ.** (a) FUSE floats every odd 48K port;
the VHDL only the LSB-0xFF decode (`zxnext.vhd:2583`, Section 8) — jnext
follows the VHDL. (b) Three FUSE samples at T 69887 (the last T of the
frame) straddle FUSE's frame wrap and are not comparable. Everything
else in the FUSE sweeps (48K and 128K, `IN A,(0xFF)` / `IN A,(C)`,
contended and uncontended, shadow screen) matches jnext sample for
sample.

### Test rows

| Row ID | Machine | Stimulus | Expected | Oracle / VHDL cite |
|--------|---------|----------|----------|--------------------|
| FB-HC-48 | 48K | Direct reads after the reload of each `hc_ula` count 0..31, display line 36 | FF for `hc(3:0)` 1..8; pixel(2k) 9-10; attr(2k) 11-12; pixel(2k+1) 13-14; attr(2k+1) 15 and the next block's 0; FF at count 0 of the line | `zxula.vhd:319-340,573`; `zxula_timing.vhd:423-436` |
| FB-HC-128 | 128K | Same, `hc_ula` 0 at raw hc 125 | Same schedule | same |
| FB-FUSE-48-PHASE | 48K | `IN A,(0xFF)` at FUSE T 14328+224·36+16+k, k = 0..7 | P4 A4 P5 A5 FF FF FF FF | FUSE 1.6; `zxula.vhd:573` |
| FB-FUSE-128-PHASE | 128K | `IN A,(0xFF)` at FUSE T 14354+228·36+16+k | Same | FUSE 1.6 |
| FB-FUSE-48-EDGES | 48K | Window corners: first byte T 14328 (0x00) and T 14327 (FF); last byte T 14328+224·191+123 (0x7F) and the next group (FF); lines −1 and 192 (FF) | As listed | FUSE 1.6; `zxula.vhd:414-416,573` |
| FB-FUSE-128-EDGES | 128K | Same with base 14354, 228 T/line | As listed | FUSE 1.6 |
| FB-FUSE-128-CONT | 128K | `IN A,(0xFF)` with A = 0x40 (port 0x40FF, bank-5 page → C:1 ×4), start column x = 32..38 and 47 of line 36 | (T, byte) = (23..17, FF) for x = 32..38 — the stretches push the latch into the idle half — and (23, 0x4F) for x = 47 | FUSE 1.6; `zxula.vhd:573,587-595`; `t80na.vhd:214-222` |
| FB-SHD-01 | 128K | 0x7FFD = 0x18 (shadow screen); bank 7 filled with a distinct pattern; `IN A,(0xFF)` at 14354+228·36+16+k, k = 0..3 | 0x84 0xC4 0x85 0xC5 (bank 7), not bank 5's 0x04 0x44 0x05 0x45 | FUSE 1.6; `zxnext.vhd:6649-6656` |
| FB-SHD-02 | 128K | Shadow screen on and port 0xFF = 0x02 (Timex hi-colour), written in BOTH orders; direct attribute read (36, col 4); bank 7 pixel 0x91, attribute 0x93 | 0x93 in both orders — bank 7's standard attribute; the shadow screen forces `screen_mode_s = "000"` ("bank 7 only has 8k bram": the hi-colour address `0x2000 + pixel layout` would fold onto the pixel byte 0x91) | `zxula.vhd:191,246-252`; `zxnext.vhd:6649-6656` |
| FB-SHD-03 | 128K | Port 0xFF = 0x02, THEN shadow on (the order that used to lose the mode); NR 0x08 b2 read-back; shadow off; attribute read (36, col 4) with bank 5 hi-colour byte 0x95, standard attribute 0x96 | Read-back 0x02 while shadowed; 0x95 after — the shadow screen MASKS the mode, `port_ff_reg` is only written by reset / port 0xFF / NR 0x69 / 0x22 / 0xC4 | `zxula.vhd:191`; `zxnext.vhd:2813,3610-3624,3630` |
| FB-TMX-01 | 48K | Port 0xFF = 0x01; pixel read (36, 4) | 0x22 from the 0x6000 screen | `zxula.vhd:191,236-240` |
| FB-TMX-02 | 48K | Port 0xFF = 0x02 (hi-colour); attribute read (36, 4) | 0x44 from 0x6000 + pixel layout, not 0x5800 | `zxula.vhd:246-252` |
| FB-TMX-03 | 48K | Port 0xFF = 0x06 (hi-res); pixel then attribute read (36, 4) | 0x55 (screen 0 pixel), 0x66 (screen 1 pixel) | `zxula.vhd:236-252` |
| FB-SCR-01 | 48K | NR 0x26 = 0x10; pixel read (36, 4) | Column 6's byte 0x7A | `zxula.vhd:199` |
| FB-SCR-02 | 48K | NR 0x27 = 8; pixel read (36, 4) | Pixel line 44's byte 0x7B | `zxula.vhd:192,201-209` |
| FB-SCR-03 | 48K | NR 0x27 = 20; pixel read (180, 4): `py_s = 200` folds to 8 | Pixel line 8's byte 0x7C | `zxula.vhd:201-209` |
| FB-SCR-04 | 48K | NR 0x27 = 250; pixel read (150, 4): `py_s = 400`, `py_s(8:7) = "11"` folds to 16 | Pixel line 16's byte 0x7D | `zxula.vhd:201-203` |
| FB-SPD-01 | 48K | NR 0x07 = 3 (28 MHz); `IN A,(0xFF)` started 500 master cycles into raw line 100 (13 cycles with the two SRAM read waits, `zxnext.vhd:3171-3181`; latch in cycle 512 = count 10) | Pixel of column 0 on display line 36, 0x5E (the old model divided by the configured 3.5 MHz divisor) | `zxnext.vhd:4513`; `zxula.vhd:319-340,573`; `t80na.vhd:214-222` |
| FB-SPD-02 | 48K | 28 MHz; the same IN started at raw-line master cycle 493 (latch in 505 = 4·9+1 of the ULA line) and at 494 (latch in 506 = 4·9+2) | 0xFF, then 0x5E — the reload lands on CLK_7's falling edge, 2 master cycles into the count, and T3's falling edge at 28 MHz is half a cycle into its master cycle (assumes CLK_7 rising edges on master cycles ≡ 0 mod 4, as CT-GH183 / VT-GH265) | `zxula.vhd:308-340`; `t80na.vhd:214-222` |

## Reset defaults (VHDL-verified)

| Signal | Default | Cite | Kind |
|--------|---------|------|------|
| `nr_08_port_ff_rd_en` | '0' | `zxnext.vhd:1118` | signal decl |
| `port_ff_reg` | X"00" | `zxnext.vhd:3614` | reset process |
| `floating_bus_r` | X"FF" after any border phase (border process re-forces it on every border `i_CLK_7` tick) | `zxula.vhd:312-315` | border process |
| `floating_bus_en` | '0' after any border phase | `zxula.vhd:312-315` | border process |
| `nr_82_internal_port_enable` (incl. bit 0 → `port_ff_io_en`, bit 4 → `port_p3_floating_bus_io_en`) | X"FF" at boot (all '1') | `zxnext.vhd:1226, 2392, 2397, 2403` | signal decl |
| `port_7ffd_locked` | '0' at boot (firmware must explicitly lock banks) | see NextReg/MMU plans | cross-plan |

Note: `zxula.vhd` does NOT declare a power-up initial value for
`floating_bus_r` / `floating_bus_en`. They only become observably
`X"FF"` / `'0'` once the border process runs in a border region; the
rows above reflect that, and FB-01 is the correct row to pin the
reset-era behaviour (any read performed before a border has been
traversed is outside the scope of this plan).

FB-6A pins `nr_08_port_ff_rd_en` to '0' at reset; the others are
implicitly exercised by FB-01 (border 0xFF) and FB-5A (active-display
capture after reset).

## Test count summary (nominal)

| Section | Area | Rows |
|---------|------|-----:|
| 1 | Border-phase 0xFF                     | 2  |
| 2 | Active-display capture                | 6  |
| 3 | +3 port 0xFF vs port 0x0FFD           | 11 |
| 4 | Per-machine selection (port 0xFF)     | 3  |
| 5 | Port 0xFF read wiring                 | 2  |
| 6 | NR 0x08 override + gate               | 3  |
| 8 | GH #109 LSB-0xFF scope                | 2  |
| 9 | GH #265 I/O-cycle sampling            | 3  |
| 10 | GH #265 follow-up: ULA counters, Timex, scroll, shadow | 19 |
| | **Total** | **51** |

Nominal, i.e. as enumerated by this plan. Two of them (FB-3E, FB-4B)
are retired with no `check()` row, so 49 plan rows are live. The suite
also carries the FB-3X port-conflict neighbour, 3 Section-7
D3F-followup rows and 5 FB-HARNESS-NN smoke rows — see
`test/unit-tests.conf` for the pinned total the harness enforces.

Of the 26: **5 are the re-homed ULA rows** (FB-01, FB-03, FB-04,
FB-06, FB-07 — note FB-03 and FB-04 were re-scoped to the correct
+3 port-0xFF expected value per `zxnext.vhd:4513`) and **21 are
VHDL-justified neighbours** (FB-02, FB-2A..FB-2F, FB-03a, FB-04a,
FB-04b, FB-3A..FB-3F, FB-4A..FB-4C, FB-5A, FB-6A, FB-6B). Every neighbour
cites a VHDL line that was read during Phase 0 preparation and that
opens a distinct path in `zxula.vhd:573` / `zxnext.vhd:2589` /
`:2713` / `:2813` / `:4513-4517`.

## Row ID mapping (old → new)

| ULA plan ID (retired) | Floating-bus plan ID |
|-----------------------|----------------------|
| S10.01 | FB-01 |
| S10.02 | FB-2A / FB-2B / FB-2C / FB-2D / FB-2E (phase-split) |
| S10.03 | covered by FB-2B / FB-2D (attribute-phase) |
| S10.04 | FB-2E (reset phase) |
| S10.05 | FB-03 (port 0xFF on +3 → 0xFF, corrected) + FB-03a (port 0x0FFD bit-0 force) |
| S10.06 | FB-04 (port 0xFF on +3 → 0xFF, corrected) + FB-04a (port 0x0FFD border fallback) + FB-04b (border-vs-active bit-0 contrast, GH #112) |
| S10.07 | FB-06 |
| S10.08 | FB-07 |

S10.02/03/04 were G-commented in the ULA plan as unobservable at the
`Ula` abstraction. At the Emulator abstraction they are observable
(via raster-phase positioning) and have been promoted to live rows
FB-2A..FB-2E plus the neighbour rows FB-2F (above-active) and
FB-4A..FB-4C / FB-3A..FB-3F / FB-6A / FB-6B. S10.05 and S10.06
each split into two rows under option (b) — the +3 behaviour they
pinned is observable on port 0x0FFD, not port 0xFF — with the
retained FB-03 / FB-04 rows asserting the corrected port-0xFF
expected value (`X"FF"` per `zxnext.vhd:4513`).

## Open questions

1. **New test file vs. extend existing?** This plan picks a new
   `test/floating_bus/floating_bus_test.cpp` suite (per
   `TASK-FLOATING-BUS-PLAN.md` Phase 1 option A). Rationale: raster
   positioning and NR 0x08 gating are not the concern of `test/port/`
   (port-dispatch decoding) or `test/ula/` (Ula-internal signals).
   Confirm at Phase 1 kickoff whether a combined `test/emulator/`
   suite is preferred instead; the two approaches are mechanically
   identical.

2. **hc-phase vs. 8T-model fidelity — RESOLVED (GH #265 follow-up,
   2026-09-22).** The emulator now evaluates the floating bus in the
   ULA's own `hc_ula` / `vc_ula` counters, one count = 4 master cycles
   (half a T-state at 3.5 MHz), with the reload on the falling edge of
   each count per `zxula.vhd:319-340`. The old `tstate_in_line % 8 ∈
   {2,3,4,5}` window of the raw line was 58 T (48K) / 62 T (128K) early.
   FB-HC-48 / FB-HC-128 pin the schedule count by count and the FB-FUSE
   rows pin it against FUSE 1.6 (Section 10).

3. **+3 `p3_floating_bus_dat` stimulus.** FB-04a needs a prior
   contended memory access to seed `p3_floating_bus_dat`
   (`zxnext.vhd:4498-4509`). The harness has to execute a contended
   `LD A,(hl)` or similar with `mem_contend = '1'` on +3 timing
   before the `IN A,(0x0FFD)` read — verify the Emulator's +3
   contention wiring actually latches `cpu_di` into the shadow
   register. If it does not, FB-04a will fail as a downstream
   consequence of an unrelated contention bug; flag it there rather
   than here. (Note: the VHDL wiring is subtle — line 4517 reads
   `ula_floating_bus` directly, not `p3_floating_bus_dat`, into the
   port-0x0FFD shadow. The `p3_floating_bus_dat` register at
   `:4498-4509` is the source of `i_p3_floating_bus` fed into
   `zxula.vhd:573`'s second arm, which then re-enters line 4517 via
   `ula_floating_bus`. The net result on +3 border is
   `ula_floating_bus = p3_floating_bus_dat`.)

4. **Precedence of Phase 0 vs. NextZXOS boot debug.** Floating-bus
   reads are not heavily exercised by NextZXOS itself (mostly tape
   loaders and legacy 48K detection code), so this plan is not on
   the NextZXOS-boot critical path. Pick it up when a session has
   budget for a small Emulator-side audit.

## GH #196 Phase 1.3 — "Extra coverage" rows folded into the main table

The matrix's `## Floating Bus` section carried a second, 4-column
`### Extra coverage (not in plan)` table (no `Status` column) holding
6 rows: `FB-3X` and the 5 `FB-HARNESS-01..05` fixture-helper smoke
rows. These had simply never been migrated out of that ad-hoc scheme
(GH #192 lineage) into the normal 5-column table. Verified 2026-08-01:
all 6 have a live `check()` call at the cited `floating_bus_test.cpp`
line, and `./build/test/floating_bus_test` reports all 6 passing
(`FB-HARNESS 5/5`, `Total: 37 Passed: 37`). No duplicate of any of the
6 IDs exists elsewhere in the matrix. All 6 were folded into the main
table as normal `pass` rows (the `FB-HARNESS-*` rows keep a bare `—`
VHDL citation, since they test the test harness's own helpers, not
FPGA behaviour — same convention already used elsewhere in the matrix,
e.g. `V11-CPU-01-IM2-DDFD-ED-NO-RETI`), and the now-empty
`### Extra coverage (not in plan)` table was removed.

## Bans

- **No tautologies.** Every row asserts an expected byte that depends
  on a VHDL mux/AND/OR step between the stimulus and the output. "Set
  `port_ff_reg = 0x05`, NR 0x08 bit 2 set, read port 0xFF, expect
  0x05" is a legal assertion because `zxnext.vhd:2813`'s three-term
  AND is the hop between stimulus and expected.
- **No expected values from C++.** Every row cites `zxula.vhd` or
  `zxnext.vhd` by file:line. Host references (e.g. `emulator.cpp`
  line numbers in §5) document *where the test reaches in* — they do
  not supply expected values.
- **No coverage padding.** Splitting FB-2A..FB-2E into eight rows by
  individually asserting `tstate_in_line % 8 ∈ {0,1,2,3,4,5,6,7}` is
  not allowed — the schedule has four "return-VRAM" phases and four
  "return-0xFF" phases; each side is covered once per offset into
  the byte stream (pixel / attr / pixel+1 / attr+1 / reset).

## Coverage notes (moved from the traceability matrix, GH #196)

The matrix is a generated artifact now and carries no prose of its own; it
links here instead. These notes were written alongside the rows they explain.

Suite covers the two floating-bus surfaces the Next FPGA exposes: port 0xFF (48K/128K timing, ULA capture) and port 0x0FFD (+3 timing, contended-write latch with bit-0 force / `port_7ffd_locked` gate / NR 0x82 b4 `port_p3_floating_bus_io_en` decode). Plan §1-§6 = 26 plan rows (5 re-homed from ULA §10 + 21 VHDL-justified neighbours), plus 1 port-conflict neighbour FB-3X (Branch B reviewer note 2) and 5 FB-HARNESS-NN fixture-helper smoke rows. Closed 2026-04-25 at 32/32 pass / 0 fail / 0 skip via Branches A/B/C/D (commits `0ee05c5`, `8bcae9b`, `c43b201`, `42b52f0`).
