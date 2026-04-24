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
| FB-02  | 48K     | V-active line (`line ∈ [64, 256)`), H-blank (`tstate_in_line = 150`, after the 128-T pixel fetch window) | port 0xFF read | 0xFF | `zxula.vhd:316,416,573` + host `src/core/emulator.cpp:2671` |

FB-01 is the S10.01 re-home. FB-02 is a VHDL-justified neighbour:
`border_active_ula` ORs `i_hc(8)` with the V-border, so horizontal
blanking inside the vertical active window must also read 0xFF. The
emulator models this via `tstate_in_line` (0..227 on 48K, one host
sample per T-state; one VHDL `hc` step ≈ 0.5 T-state); the H-blank
condition (`tstate_in_line >= 128`) is enforced at
`src/core/emulator.cpp:2671`.

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

`Emulator::floating_bus_read` models this at 8T granularity
(T-state % 8). A phase-0x1 (reset) is represented by the
"default: return 0xFF" arm on `tstate_in_line % 8 in {0,1,6,7}`; the
VRAM-return arms cover `% 8 in {2,3,4,5}`.

### Test rows

| Row ID | Machine | Position | Stimulus | Expected | VHDL cite |
|--------|---------|----------|----------|----------|-----------|
| FB-2A  | 48K | Active display, T-phase 0x9 equivalent (`tstate_in_line % 8 = 2`) | Fill VRAM bank 5 at the expected pixel address; port 0xFF read | VRAM byte at `pixel_addr` | `zxula.vhd:325-327` |
| FB-2B  | 48K | Active display, T-phase 0xB equivalent (`% 8 = 3`) | Fill VRAM attr bank; port 0xFF read | VRAM byte at `attr_addr` | `zxula.vhd:329-330` |
| FB-2C  | 48K | Active display, T-phase 0xD equivalent (`% 8 = 4`) | Fill VRAM; port 0xFF read | VRAM byte at `pixel_addr + 1` (next pixel column) | `zxula.vhd:332-333` |
| FB-2D  | 48K | Active display, T-phase 0xF equivalent (`% 8 = 5`) | Fill VRAM; port 0xFF read | VRAM byte at `attr_addr + 1` (next attribute column) | `zxula.vhd:335-336` |
| FB-2E  | 48K | Active display, reset/idle phase (host `% 8 = 0`, candidate mapping for VHDL `hc(3:0) = 0x1`) | port 0xFF read; VRAM irrelevant | 0xFF (reset/idle phase) | `zxula.vhd:321-323,573` |
| FB-2F  | 48K | Scanline < 64 (vc < min_vactive) | port 0xFF read | 0xFF (above active display window) | `zxula.vhd:414-416,573` |

FB-2A..FB-2F expand the 3 rows (S10.02/03/04) that were G-commented
out of the ULA plan as "internal, end-to-end by §1/§2". At the Ula
abstraction they were unobservable; at the Emulator-level floating-bus
surface they are direct public-API checks.

Caveat tracked in §Open questions: the VHDL's phase schedule is
`hc(3:0) ∈ {0x9, 0xB, 0xD, 0xF}` (a 16-cycle window), while the C++
model uses an 8T cycle. The Phase-0 audit must verify whether
`tstate_in_line % 8` correctly witnesses the 4 VRAM-returning phases,
or whether the VHDL phase count needs an `hc(3:0) / 2` translation.
Until that is resolved these rows may expose a real emulator bug
rather than a plan bug; that is the honest outcome per
UNIT-TEST-PLAN-EXECUTION.md §2.

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
| FB-3A  | +3 | 0x0FFD | Border; `port_7ffd_locked = 1` | 0xFF (`port_p3_floating_bus_dat` forced to `X"FF"` when locked) | `zxnext.vhd:4517` |
| FB-3B  | +3 | 0x0FFD | Active display; `port_p3_floating_bus_io_en = 0` (bit 4 of `internal_port_enable` cleared) | 0x00 (decode `port_p3_float = 0` → `port_p3_float_rd` never asserts → wired-or `else X"00"` arm at `zxnext.vhd:2814`) | `zxnext.vhd:2403, 2589, 2814` |
| FB-3C  | 48K | 0x0FFD | Active display, any phase (any `port_7ffd_locked`) | 0x00 (decode gated by `p3_timing_hw_en` → port 0x0FFD not decoded on 48K; `port_p3_float_rd_dat = X"00"`) | `zxnext.vhd:2589, 2814` |
| FB-3D  | 128K | 0x0FFD | Active display, any phase | 0x00 (same reason as FB-3C; port 0x0FFD is not floating-bus-reactive on 128K) | `zxnext.vhd:2589, 2814` |
| FB-3E  | Pentagon | 0x0FFD | Active display, any phase | 0x00 (`p3_timing_hw_en = 0` on Pentagon) | `zxnext.vhd:2589, 2814` |
| FB-3F  | Next | 0x0FFD | Active display, any phase | 0x00 (`p3_timing_hw_en = 0` on Next-base) | `zxnext.vhd:2589, 2814` |

FB-03 is the re-homed S10.05: corrected expected value per
`zxnext.vhd:4513` (port 0xFF forces 0xFF on +3). The bit-0 force
exercised by the original row lives on port 0x0FFD and is now
FB-03a. FB-04 is the re-homed S10.06: corrected expected value
per `zxnext.vhd:4513`. The `p3_floating_bus_dat` behaviour exercised
by the original row is on port 0x0FFD and is FB-04a. FB-3A is the
`port_7ffd_locked` gate, now correctly targeting port 0x0FFD.
FB-3B covers the `port_p3_floating_bus_io_en` AND-term of the
port-0x0FFD decode (`zxnext.vhd:2589`). FB-3C/3D/3E/3F are
VHDL-justified neighbours: they lock down that port 0x0FFD is NOT a
floating-bus surface on 48K/128K/Pentagon/Next because the
`p3_timing_hw_en` decode AND-term at `zxnext.vhd:2589` blocks the
decode altogether.

Note: the read-value `0x00` in FB-3B..FB-3F is the wired-or default
at `zxnext.vhd:2814` (`X"00"` when `port_p3_float_rd = '0'`). In
practice any further fallback in the host dispatcher may replace
this with an open-bus 0xFF; Phase 0 will walk
`Port::default_read` vs. the `port_internal_rd_response` mux at
`zxnext.vhd:2803-2806` and lock down whether 0x00 or 0xFF is the
faithful value — the rows' expectation will follow the VHDL.

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
| FB-4B  | Pentagon | Active display, any phase | 0xFF (machine-timing gate forces 0xFF regardless of ULA) | `zxnext.vhd:4513` |
| FB-4C  | Next (default) | Active display, any phase | 0xFF (not 48K/128K, so ULA bus not wired to port 0xFF) | `zxnext.vhd:4513` |

All three are VHDL-justified neighbours of the 5 re-homed rows. They
protect against a regression where a future refactor might apply the
48K floating-bus logic uniformly across machine types.

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
| FB-5A  | 48K | CPU executes `IN A,(0xFF)` in active-display capture phase; NR 0x08 bit 2 = 0 (reset default) | Matching VRAM byte | `zxnext.vhd:2713, 2813` + host `emulator.cpp:2651-2700` |

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
| 3 | +3 port 0xFF vs port 0x0FFD           | 10 |
| 4 | Per-machine selection (port 0xFF)     | 3  |
| 5 | Port 0xFF read wiring                 | 2  |
| 6 | NR 0x08 override + gate               | 3  |
| | **Total** | **26** |

Of the 26: **5 are the re-homed ULA rows** (FB-01, FB-03, FB-04,
FB-06, FB-07 — note FB-03 and FB-04 were re-scoped to the correct
+3 port-0xFF expected value per `zxnext.vhd:4513`) and **21 are
VHDL-justified neighbours** (FB-02, FB-2A..FB-2F, FB-03a, FB-04a,
FB-3A..FB-3F, FB-4A..FB-4C, FB-5A, FB-6A, FB-6B). Every neighbour
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
| S10.06 | FB-04 (port 0xFF on +3 → 0xFF, corrected) + FB-04a (port 0x0FFD border fallback) |
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

2. **hc-phase vs. 8T-model fidelity.** The VHDL `hc` counter runs on
   `i_CLK_7` (7 MHz) inside `zxula.vhd:308`, cycling through 16 `hc`
   values per 8-T-state character window — i.e. one `hc` step is
   roughly 0.5 T-state wide. The floating-bus capture schedule is
   `hc(3:0) ∈ {0x1, 0x9, 0xB, 0xD, 0xF}`, with `0x1` being the
   reset/idle phase and `0x9..0xF` the four VRAM-capture phases. In
   the 16-hc window these capture phases land in the **second half**
   of the character window (values 9/11/13/15 of 16). The emulator
   models this at 8T granularity via `tstate_in_line % 8` and
   currently asserts VRAM on `% 8 ∈ {2,3,4,5}` — i.e. the **first
   half** of each 8T window (T-states 2/3/4/5 of 8). Whether that
   mapping is VHDL-faithful is not obvious: if the 16-hc schedule
   translates 1:2 to T-states (one hc = 0.5T, so hc 9/11/13/15 map
   to half-T offsets 4.5/5.5/6.5/7.5), the capture-returning phases
   should fall in `% 8 ∈ {4,5,6,7}` (the second half), not
   `{2,3,4,5}`. Phase 0 must walk one full 16-hc window through
   `zxula.vhd:319-340` + `zxula_timing.vhd` + the emulator's
   `floating_bus_read` (`src/core/emulator.cpp:2651-2700`) to
   resolve this and adjust either the plan expected values or the
   host mapping. FB-2A..FB-2E will flush any mismatch out as real
   fails rather than plan bugs.

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
