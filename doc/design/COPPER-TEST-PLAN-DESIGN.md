# Copper Coprocessor Compliance Test Plan

> **Plan Rebuilt 2026-04-14.** The previous revision of this document reported
> 69/69 passing and was flagged as coverage theatre during the Task 4 critical
> review: mode `10` semantics were stated incorrectly, the self-modifying
> Copper cases (EDG-03/EDG-04) were never actually exercised, NextREG
> arbitration was stubbed, and several rows were tautological (asserting the
> stimulus back to itself). **The prior pass count is hereby retracted.** This
> plan is rebuilt from scratch against the FPGA VHDL sources; every expected
> value is cited back to a specific VHDL file and line, and every row that
> could not be derived unambiguously from the VHDL is listed in the
> "Open Questions" section rather than hidden behind a green tick.
>
> Retractions:
> - Old **CTL-06 "Mode 10 behaviour"** described mode `10` as "treated as
>   not-00 so copper runs but no loop restart" with an implicit assumption
>   that it behaves identically to mode `01` minus the restart. That is
>   **half right and misleading**: mode `10` also does **not** reset
>   `copper_list_addr` on entry, because the "reset on mode-change" branch in
>   `copper.vhd:74` triggers only on `"01"` or `"11"`. Mode `10` therefore
>   *resumes* from whatever address the Copper was last parked at. See new
>   CTL-06a/b/c for the corrected behaviour.
> - Old **EDG-03 / EDG-04** (self-modifying copper) were row entries with no
>   corresponding runner logic. Reinstated as MUT-01..MUT-04 with concrete
>   stimulus and VHDL citations.
> - Old **ARB-01..ARB-05** were stubs that never forced a simultaneous CPU+
>   Copper write. Rewritten as ARB-01..ARB-06 with explicit one-cycle
>   contention scenarios and the exact priority rule from `zxnext.vhd:4775`.
> - Old plan claimed an instruction memory of 1024 entries in one place and
>   implied 2048 elsewhere; the correct figures are **1024 16-bit
>   instructions = 2048 bytes**. The byte pointer `nr_copper_addr` is
>   11 bits (`zxnext.vhd:1194`), the instruction address `copper_list_addr`
>   is 10 bits (`copper.vhd:38, 48`).

## Purpose

The Copper is a simple display-synchronized coprocessor that executes a list
of up to **1024** 16-bit instructions from dedicated dual-port RAM
(`copper.vhd:38`, `copper_list_addr` is 10 bits). It supports two instruction
kinds (MOVE and WAIT) and four start modes selected by `NR 0x62` bits 7..6.
Its NextREG writes are arbitrated against the CPU with Copper having strictly
higher priority (`zxnext.vhd:4775-4777`). Correct emulation requires exact
instruction decoding, cycle-level advancement, RAM-pointer auto-increment on
CPU uploads, vblank-relative restart in mode `11`, and resume-without-reset
in mode `10`.

All references below are to the FPGA sources under
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.

## VHDL Source Summary

### Entity: `copper` (`device/copper.vhd`)

**Ports (`copper.vhd:28-44`):**

| Signal                | Dir | Width | Purpose                                |
|-----------------------|-----|-------|----------------------------------------|
| `clock_i`             | in  | 1     | 28 MHz clock                           |
| `reset_i`             | in  | 1     | Synchronous reset                      |
| `copper_en_i`         | in  | 2     | Start mode (`nr_62_copper_mode`)       |
| `hcount_i`            | in  | 9     | ULA horizontal count                   |
| `vcount_i`            | in  | 9     | **Copper** vertical count `cvc`, not raw `vc_ula` |
| `copper_list_addr_o`  | out | 10    | Instruction RAM read address           |
| `copper_list_data_i`  | in  | 16    | Instruction RAM read data              |
| `copper_dout_o`       | out | 1     | Write pulse (asserts one clock per MOVE) |
| `copper_data_o`       | out | 15    | `{reg[6:0], data[7:0]}` of MOVE        |

`vcount_i` is wired to `cvc` (`zxnext.vhd:3950`), which is the offset copper
vertical counter produced by `zxula_timing.vhd` — see §"Copper vertical
offset" below. This means all WAIT vpos comparisons and the mode-`11` restart
trigger operate on `cvc`, **not** the raw ULA vcount.

### Instruction format (16 bits)

Decoded in `copper.vhd:92-108`.

| Bit 15 | Bits 14..9       | Bits 8..0            | Type | Action                                       |
|--------|------------------|----------------------|------|----------------------------------------------|
| `0`    | `reg[6:0]`       | `data[7:0]`          | MOVE | Write `data` to NextREG `{0, reg[6:0]}`      |
| `1`    | `hpos[5:0]`      | `vpos[8:0]`          | WAIT | Block until `vcount == vpos` AND `hcount >= (hpos*8) + 12` |

- **MOVE NOP** — when `copper_list_data_i(14 downto 8) = "0000000"` (i.e. MOVE
  to register `0x00`), the write-pulse is suppressed (`copper.vhd:104-106`)
  but the address still advances (`copper.vhd:108`). Register `0x00`
  (Machine ID) cannot be written by the Copper, by design.
- **WAIT** uses a 6-bit `hpos` field (`copper.vhd:94`:
  `copper_list_data_i(14 downto 9)&"000"`), giving 64 distinct horizontal
  positions at 8-pixel granularity, with an added `+12` offset.
- **WAIT match is equality on vpos, `>=` on hpos** — a WAIT cannot be
  "missed" on hpos alone; it can be missed (and then blocks until the next
  frame) on vpos.

### State machine (`copper.vhd:54-120`)

Per positive edge of `clock_i`:

1. **Reset (`copper.vhd:60-65`)**: `copper_list_addr_s <= 0`,
   `copper_dout_s <= 0`, `copper_data_o <= 0`.
2. **Mode-change branch (`copper.vhd:70-78`)**:
   - If `last_state_s /= copper_en_i`, latch new mode.
   - **Only if new mode is `"01"` or `"11"`**, reset
     `copper_list_addr_s` to 0 (`copper.vhd:74-76`).
   - Always clear `copper_dout_s` on mode change (`copper.vhd:78`).
   - **Mode `"10"` is explicitly absent from the reset list** — entering
     mode `10` does NOT reset the address; the Copper resumes from the
     current `copper_list_addr_s`.
3. **Vblank restart branch (`copper.vhd:80-83`)**:
   - Only when `copper_en_i = "11"`, if `vcount_i = 0` AND `hcount_i = 0`,
     reset `copper_list_addr_s` to 0 and clear `copper_dout_s`.
   - The comparison is against the **copper** vcount `cvc`, not the raw ULA
     vcount (`zxnext.vhd:3950`). Combined with the vertical offset
     (`zxula_timing.vhd:462`), "vcount = 0" occurs `c_max_vc - cu_offset + 1`
     lines after start-of-active-display, not at the top of the field.
4. **Execution branch (`copper.vhd:85-110`)** — when `copper_en_i /= "00"`:
   - If the previous cycle set `copper_dout_s = 1` (a MOVE write pulse),
     clear it this cycle — the write pulse is exactly one clock wide
     (`copper.vhd:87-89`).
   - Else if `copper_list_data_i(15) = '1'` (WAIT):
     - If `vcount_i = unsigned(copper_list_data_i(8 downto 0))` AND
       `hcount_i >= unsigned(copper_list_data_i(14 downto 9)&"000") + 12`,
       advance `copper_list_addr_s`. Otherwise stall. Always clear
       `copper_dout_s` (`copper.vhd:92-98`).
   - Else (MOVE):
     - Drive `copper_data_o <= copper_list_data_i(14 downto 0)`.
     - If `reg[6:0] /= "0000000"` set `copper_dout_s <= 1`.
     - Advance `copper_list_addr_s` unconditionally
       (`copper.vhd:100-108`).
5. **Stopped branch (`copper.vhd:112-114`)** — when `copper_en_i = "00"`
   and not a mode-change cycle: `copper_dout_s <= 0`. The address is **not**
   reset; a subsequent entry to mode `10` will resume from there.

**Implication for the address counter**: `copper_list_addr_s` is
`std_logic_vector(9 downto 0)` and is incremented with `+ 1`
(`copper.vhd:48, 95, 108`). It wraps 1023 → 0 with no special handling.

### Start modes (`NR 0x62` bits 7..6) — the authoritative table

Re-derived from `copper.vhd:70-85`. This replaces the old, incorrect CTL-06
description.

| Mode bits | Name (plan)               | Mode-change (entry) resets addr? | Vblank restart? | Runs?   | VHDL lines              |
|-----------|---------------------------|----------------------------------|-----------------|---------|--------------------------|
| `00`      | Stop                      | —                                | —               | No      | `copper.vhd:85, 112-114` |
| `01`      | Reset-and-start           | Yes                              | No              | Yes     | `copper.vhd:74-76, 85`   |
| `10`      | Start (resume, no reset)  | **No**                           | No              | Yes     | `copper.vhd:74-76, 85`   |
| `11`      | Reset-and-start-on-vblank | Yes                              | Yes (`cvc=0, hcount=0`) | Yes | `copper.vhd:74-76, 80-83` |

**Key correction for mode `10`**: when the CPU writes `NR 0x62` transitioning
Copper from some other mode into `10`, the state machine enters the
mode-change branch at `copper.vhd:70`. But the inner `if` at line 74 only
rewrites the address if the new state is `"01"` or `"11"`. So on entry to
`10` the address is **kept as-is** and the Copper will resume executing from
wherever it was previously parked. `copper_dout_s` is still cleared
(`copper.vhd:78`). There is no vblank-restart path for `10` either
(`copper.vhd:80` checks only `"11"`). Mode `10` is therefore a single-shot
*resume* — useful for pausing via mode `00` and continuing later without
losing position.

### Instruction RAM (`zxnext.vhd:3959-3999`)

Two dual-port RAMs (`dpram2`): `copper_inst_msb_ram` (bits 15..8) and
`copper_inst_lsb_ram` (bits 7..0), each **1024 × 8**. CPU side is addressed
by `nr_copper_addr(10 downto 1)` — a 10-bit instruction index
(`zxnext.vhd:3968, 3989`). Copper side is addressed by `copper_instr_addr`
(`zxnext.vhd:3973, 3994`).

Total instruction memory is **1024 instructions = 2048 bytes**. The CPU byte
pointer `nr_copper_addr` is 11 bits (`zxnext.vhd:1194`); bit 0 selects
MSB vs LSB byte.

Write-enable logic (`zxnext.vhd:3977, 3998`):

```
copper_msb_we = nr_copper_we AND (
    (nr_copper_write_8 = '0' AND nr_copper_addr(0) = '1')   -- 16-bit mode: MSB stored, written on odd
 OR (nr_copper_write_8 = '1' AND nr_copper_addr(0) = '0')   -- 8-bit  mode: MSB written on even
)
copper_lsb_we = nr_copper_we AND nr_copper_addr(0) = '1'    -- LSB always written on odd byte
```

Data sources:
- `copper_msb_dat <= nr_wr_dat when nr_copper_write_8 = '1' else nr_copper_data_stored`
  (`zxnext.vhd:3978`).
- `copper_lsb_dat <= nr_wr_dat` (`zxnext.vhd:3999`).

### CPU-side NextREG interface

| NextREG | Write action                                                                 | VHDL                         |
|---------|------------------------------------------------------------------------------|------------------------------|
| `0x60`  | 8-bit upload: caches on even byte only when using the write path that uses `nr_copper_data_stored`; actual MSB RAM write occurs on even byte via `copper_msb_we` rule; LSB RAM write occurs on odd byte. Each `NR 0x60` write auto-increments `nr_copper_addr` by 1. | `zxnext.vhd:4884-4886, 5419-5424` |
| `0x61`  | `nr_copper_addr(7 downto 0) <= nr_wr_dat`. Preserves bits 10..8.             | `zxnext.vhd:5427`            |
| `0x62`  | `nr_62_copper_mode <= nr_wr_dat(7 downto 6)`; `nr_copper_addr(10 downto 8) <= nr_wr_dat(2 downto 0)`. | `zxnext.vhd:5430-5431` |
| `0x63`  | 16-bit upload: even byte caches `nr_wr_dat` in `nr_copper_data_stored`; odd byte triggers joint MSB+LSB write (MSB from the cached byte, LSB from current). Auto-increments `nr_copper_addr` by 1 per byte. | `zxnext.vhd:4887, 5434-5437` |
| `0x64`  | `nr_64_copper_offset <= nr_wr_dat`.                                          | `zxnext.vhd:5442`            |

**Read-back** (`zxnext.vhd:6084` for NR 0x61, `6086-6087` for NR 0x62, `6089-6090` for NR 0x64):

| NextREG | Read value                                                              |
|---------|-------------------------------------------------------------------------|
| `0x61`  | `nr_copper_addr(7 downto 0)`                                            |
| `0x62`  | `nr_62_copper_mode & "000" & nr_copper_addr(10 downto 8)`               |
| `0x64`  | `nr_64_copper_offset`                                                   |

### Copper vertical offset (`NR 0x64`, `zxula_timing.vhd:457-472`)

```
process (i_CLK_7)
   if rising_edge(i_CLK_7) then
      if ula_max_hc = '1' then
         if ula_min_vactive = '1' then
            cvc <= unsigned ('0' & i_cu_offset);      -- line 462
         elsif cvc = c_max_vc then
            cvc <= (others => '0');                    -- line 464
         else
            cvc <= cvc + 1;                            -- line 466
```

So at the start of active display (`ula_min_vactive='1'`), `cvc` is
**loaded with the offset**, not zero. It then counts up, wraps at
`c_max_vc`, and keeps ticking. The value fed to `copper.vcount_i` is `cvc`
(`zxnext.vhd:3950`). Consequences:
- A WAIT for `vpos = N` triggers on the scanline where `cvc = N`, not
  on ULA line `N`.
- Mode `11` vblank restart fires when `cvc = 0`, which is not the top of
  the field: it's the line where `cvc` wraps from `c_max_vc` back to `0`
  (or, if `i_cu_offset = 0`, the start of active display itself, because
  the line 462 reload writes 0 and then `hc = 0` arrives inside that line).

### NextREG write arbitration (`zxnext.vhd:4706-4777`)

```
copper_requester <= copper_dout_en;                                  -- 4709
-- rising-edge detect
copper_req        <= '1'  when copper_requester='1' and copper_requester_d='0'  -- 4729
copper_nr_reg     <= '0' & copper_dout(14 downto 8);                  -- 4731  (bit 7 forced to 0)
copper_nr_dat     <= copper_dout(7 downto 0);                         -- 4732
-- cpu_req is cleared only when copper_req = 0:
elsif cpu_req = '1' and copper_req = '0' then                         -- 4769
nr_wr_en  <= copper_req or cpu_req;                                   -- 4775
nr_wr_reg <= copper_nr_reg when copper_req='1' else cpu_nr_reg;       -- 4776
nr_wr_dat <= copper_nr_dat when copper_req='1' else cpu_nr_dat;       -- 4777
```

Rules:
- Copper request is edge-triggered from `copper_dout_en` going 0→1
  (`zxnext.vhd:4729`). It's therefore one cycle wide *at minimum*.
- When `copper_req = 1`, the CPU write path is not advanced
  (`zxnext.vhd:4769`) — the CPU waits until `copper_req` clears.
- `nr_wr_reg`/`nr_wr_dat` multiplexers give Copper unconditional priority
  (`zxnext.vhd:4776-4777`).
- Register number from Copper is masked to 7 bits by the `'0' &` prepend
  (`zxnext.vhd:4731`). Copper MOVE cannot write `NR 0x80..0xFF`.
- `nmi_cu_02_we` special-cases a Copper write to `NR 0x02`
  (`zxnext.vhd:3830`), driving the NMI generators for MF and DivMMC
  (`zxnext.vhd:3832-3833`).

### Reset state (`zxnext.vhd:5020-5024`, `copper.vhd:60-65`)

| Signal                 | Reset value |
|------------------------|-------------|
| `nr_62_copper_mode`    | `"00"`      |
| `nr_copper_addr`       | all zeros (11 bits) |
| `nr_copper_data_stored`| `0x00`      |
| `nr_64_copper_offset`  | `0x00`      |
| `copper_list_addr_s`   | `0`         |
| `copper_dout_s`        | `'0'`       |
| `copper_data_o`        | `(others => '0')` |

## Test Harness Requirements

The test runner must be able to:

1. Directly poke `(hcount, vcount)` pairs into the Copper through a mock
   ULA timing source, *and* run the real ULA timing to verify cross-module
   integration. Unit tests MUST use the mock; at least two integration
   smoke tests MUST use the real timing.
2. Write `NR 0x60..0x64` as the CPU and observe both the RAM contents (via
   `copper_instr_data` mirror) and `nr_copper_addr`.
3. Observe `copper_dout_en`, `copper_data_o`, `nr_wr_en`, `nr_wr_reg`,
   `nr_wr_dat`, and the chosen source (Copper vs CPU) each cycle.
4. Schedule a CPU `NR` write to land on the *exact* cycle a Copper MOVE
   asserts `copper_req`, for arbitration tests.
5. Run single-stepped at the 28 MHz Copper clock; no "advance to next
   frame" shortcut is acceptable for timing-sensitive tests.

Any test that cannot satisfy its VHDL-citation requirement from the above
APIs MUST be moved to the Open Questions section, not silently weakened.

## Tautology Ban

The following patterns are disallowed and any existing test matching them
is retracted:

- "Write `X` to `NR 0x62`, read back `NR 0x62`, assert the mode bits equal
  `X >> 6`" — this exercises only the storage register, not the Copper.
  (Acceptable only as part of RAM-BK-01, where the test is explicitly a
  read-back of the CPU-visible register.)
- "Program a MOVE to register `R` with value `V`, observe a NextREG write
  to register `R` with value `V`" — only counts when the expected trigger
  cycle and `copper_list_addr` transition are also asserted.
- "Set mode=00, verify Copper does nothing" without observing that
  `copper_list_addr_s` is frozen at its previous value (not zero).
- "Verify NR 0x61/0x62 auto-increment", when only the CPU-visible pointer
  is checked — the test must also verify that the *next* Copper fetch
  returns the freshly-written instruction.

## Test Case Catalog

Columns: **ID**, **Title**, **Pre**, **Stimulus**, **Expected**, **VHDL**.
"Pre" describes pre-conditions beyond default reset; "Stimulus" lists CPU/
clock actions in order; "Expected" lists the observable assertions that
decide pass/fail; "VHDL" cites the authoritative lines.

### Group 1 — Instruction RAM upload (CPU-side)

| ID       | Title                                 | Pre                          | Stimulus                                                            | Expected                                                                                                        | VHDL |
|----------|---------------------------------------|------------------------------|---------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------|------|
| RAM-8-01 | `NR 0x60` two-byte upload             | `nr_copper_addr=0`, mode=`00`| Write `NR 0x60 <= 0xAB`; write `NR 0x60 <= 0xCD`                    | After 1st write: MSB RAM[0]=0xAB, `nr_copper_addr=1`. After 2nd: LSB RAM[0]=0xCD, `nr_copper_addr=2`. Instr[0]=0xABCD. | `zxnext.vhd:3977, 3978, 3998, 5419-5424` |
| RAM-8-02 | `NR 0x60` upload starting odd         | `nr_copper_addr=1`, mode=`00`| Write `NR 0x60 <= 0x11`; write `NR 0x60 <= 0x22`                    | 1st: LSB RAM[0]=0x11 (instr addr 0), addr=2. 2nd: MSB RAM[1]=0x22, addr=3. (Start-odd skews byte/instruction pairing.) | `zxnext.vhd:3977, 3998` |
| RAM-16-01| `NR 0x63` two-byte upload             | `nr_copper_addr=0`, mode=`00`| Write `NR 0x63 <= 0xAB`; write `NR 0x63 <= 0xCD`                    | 1st: `nr_copper_data_stored=0xAB`, no RAM write, addr=1. 2nd: MSB RAM[0]=0xAB, LSB RAM[0]=0xCD, addr=2. Instr[0]=0xABCD. | `zxnext.vhd:3977, 3978, 3998, 5434-5437` |
| RAM-P-01 | `NR 0x61` sets low byte               | `nr_copper_addr=0x123`       | Write `NR 0x61 <= 0x7F`                                             | `nr_copper_addr=0x17F`; high bits 10..8 unchanged.                                                              | `zxnext.vhd:5427`       |
| RAM-P-02 | `NR 0x62` sets mode and addr hi       | `nr_copper_addr=0x0FF`, mode=`00` | Write `NR 0x62 <= 0x43` (binary `01_000_011`)                  | `nr_62_copper_mode="01"`, `nr_copper_addr(10..8)="011"` → `0x3FF`.                                               | `zxnext.vhd:5430-5431`  |
| RAM-P-03 | `NR 0x61` then `NR 0x62` addressing   | `nr_copper_addr=0`           | `NR 0x61 <= 0xAA`; `NR 0x62 <= 0xC5` (`11_000_101`)                  | `nr_62_copper_mode="11"`, `nr_copper_addr = 0x5AA`.                                                             | `zxnext.vhd:5427, 5430-5431` |
| RAM-AI-01| Auto-increment over 4 bytes           | `nr_copper_addr=0`, mode=`00`| Four `NR 0x60` writes `0x11 0x22 0x33 0x44`                         | `nr_copper_addr=4`; instr[0]=0x1122, instr[1]=0x3344.                                                           | `zxnext.vhd:5419-5424`  |
| RAM-AI-02| Byte pointer wraps at 0x7FF → 0x000   | `nr_copper_addr=0x7FF`, mode=`00` | Two `NR 0x60` writes `0xEE 0xFF`                                | After 1st: addr=0x000 (11-bit wrap), LSB RAM[1023]=0xEE. After 2nd: addr=0x001, MSB RAM[0]=0xFF.                 | `zxnext.vhd:5424, 1194` |
| RAM-AI-03| Full RAM fill                         | `nr_copper_addr=0`, mode=`00`| 2048 `NR 0x60` writes, byte `i & 0xFF`                              | `nr_copper_addr=0x000` (wrapped back); instr[1023]=0xFEFF.                                                      | `zxnext.vhd:5424, 3977, 3998` |
| RAM-MIX-01| `nr_copper_write_8` latch across 0x60/0x63 mix | fresh reset: `nr_copper_write_8=0`, `nr_copper_addr=0`, `nr_copper_data_stored=0x00`, mode=`00` | Write `NR 0x63 <= 0xA1`; `NR 0x63 <= 0xB2`; `NR 0x60 <= 0xC3`; `NR 0x63 <= 0xD4` | 1) 0x63<=0xA1: `write_8` stays `0`, `addr(0)=0` → `data_stored<=0xA1`, no RAM write, `addr=1`. 2) 0x63<=0xB2: `write_8=0`, `addr(0)=1` → `msb_we=1` (16-bit path), `lsb_we=1`; `copper_msb_dat=data_stored=0xA1`; `MSB RAM[0]=0xA1`, `LSB RAM[0]=0xB2`; `data_stored` unchanged (0xA1); `addr=2`. Instr[0]=`0xA1B2`. 3) 0x60<=0xC3: sets `nr_copper_write_8<='1'` (line 4885, latched); `addr(0)=0` → `data_stored<=0xC3`; `msb_we=(1 AND 0)=1`, `copper_msb_dat=nr_wr_dat=0xC3`; `MSB RAM[1]=0xC3`; `lsb_we=0`; `addr=3`. 4) 0x63<=0xD4: `write_8` **stays `1`** (0x63 case at line 4887 does **not** clear it — only reset does; lines 5423/5439 that would have cleared it are commented out); `addr(0)=1` → line 5434 `addr(0)=0` condition false, `data_stored` unchanged (0xC3); `msb_we=(1 AND 1)=0 OR (0 AND 1)=0 → 0`; `lsb_we=1`; `LSB RAM[1]=0xD4`; `addr=4`. Final: Instr[0]=`0xA1B2`, Instr[1]=`0xC3D4`, `nr_copper_addr=4`, `nr_copper_data_stored=0xC3`, `nr_copper_write_8=1`. **Captures the VHDL quirk that `nr_copper_write_8` latches at `'1'` on the first NR 0x60 write and is not cleared by subsequent NR 0x63 writes — so post-0x60 writes to 0x63 behave as byte-mode, not 16-bit-pair mode.** | `zxnext.vhd:3977, 3978, 3998, 4833, 4883-4887, 5418-5437` |
| RAM-BK-01| Read-back `NR 0x61`/`NR 0x62`/`NR 0x64`| fresh reset                 | `NR 0x61 <= 0x5A`; `NR 0x62 <= 0x86`; `NR 0x64 <= 0x40`; read each  | `NR 0x61` → `0x5A`; `NR 0x62` → `0x86` (mode=`10`, hi=`110` — **note mode=`10`**); `NR 0x64` → `0x40`.           | `zxnext.vhd:6084` (NR 0x61), `6086-6087` (NR 0x62), `6089-6090` (NR 0x64) |

### Group 2 — MOVE instruction execution

All tests in this group run with mode=`01` and a mock timing source parked
at `(hcount=12, vcount=0)` so that any WAIT 0,0 would trivially match; each
test starts the Copper from `copper_list_addr=0` via the mode-change reset
(`copper.vhd:74-76`).

| ID     | Title                          | Pre                                  | Stimulus                                                                  | Expected                                                                                                         | VHDL |
|--------|--------------------------------|--------------------------------------|---------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------|------|
| MOV-01 | MOVE NR 0x40 = 0x55            | Instr[0]=`MOVE 0x40,0x55` = `0x4055` | Step 2 clocks after mode write                                            | Cycle A: `copper_data_o=0x4055`, `copper_dout_s=1`, addr advances 0→1. Cycle B: `copper_dout_s=0` (pulse cleared). | `copper.vhd:100-108, 87-89` |
| MOV-02 | MOVE to reg 0x7F               | Instr[0]=`0x7F33`                    | Step 2 clocks                                                             | `copper_data_o=0x7F33`, Copper requests `nr_wr_reg=0x7F` (bit 7 cleared by the prepend).                          | `copper.vhd:100-108`; `zxnext.vhd:4731` |
| MOV-03 | MOVE NOP suppresses pulse      | Instr[0]=`0x00AA` (reg=0)            | Step 2 clocks                                                             | `copper_dout_s` stays 0; no `copper_req` ever rises; addr still advances 0→1; `copper_data_o=0x00AA`.             | `copper.vhd:104-108` |
| MOV-04 | Two consecutive MOVEs          | Instr[0]=`0x4011`, Instr[1]=`0x4122` | Step 4 clocks                                                             | c0: out=0x4011,dout=1,addr=1. c1: dout=0. c2: out=0x4122,dout=1,addr=2. c3: dout=0. Two distinct `copper_req` pulses. | `copper.vhd:85-110` |
| MOV-05 | MOVE pulse is exactly 1 clock  | Instr[0]=`0x4055`                    | Step 10 clocks with no further instructions (program is `MOVE` then WAIT unreachable) | `copper_dout_s=1` on exactly one cycle.                                                                         | `copper.vhd:87-89` |
| MOV-06 | MOVE then WAIT pipeline        | Instr[0]=`0x4055`, Instr[1]=`0x8000` (WAIT 0,0) | Step 4 clocks with hcount/vcount held at match                  | c0: MOVE, dout=1,addr=1. c1: dout=0 (pulse clear). c2: WAIT matches, addr=2. (MOVE's clear-pulse cycle defers WAIT's evaluation.) | `copper.vhd:87-89, 92-97` |
| MOV-07 | MOVE output width              | Instr[0]=`0x7FFF`                    | Step 2 clocks                                                             | `copper_data_o = "111111111111111"` (15 bits); Copper-side `nr_wr_reg=0x7F`, `nr_wr_dat=0xFF`.                   | `copper.vhd:42, 102`; `zxnext.vhd:4731-4732` |

### Group 3 — WAIT instruction

| ID     | Title                             | Pre                                              | Stimulus                                                            | Expected                                                                                                         | VHDL |
|--------|-----------------------------------|--------------------------------------------------|---------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------|------|
| WAI-01 | WAIT (0,0) matches at hcount=12   | Instr[0]=`0x8000`; mock cvc=0, hcount stepped 0→20 | Step clocks with hcount 0,1,...                                     | addr stays 0 while hcount<12; advances 0→1 on the first cycle hcount≥12 AND cvc=0.                                | `copper.vhd:92-96` |
| WAI-02 | hpos threshold `hpos*8+12`        | Instr[0]=WAIT hpos=10 vpos=100 (`0x9464`); cvc=100 | hcount stepped upward                                              | advance on first cycle with hcount ≥ 10*8+12 = 92.                                                               | `copper.vhd:94` |
| WAI-03 | hpos=63 max                       | Instr[0]=WAIT hpos=63 vpos=0 (`0xFE00`); cvc=0   | hcount stepped                                                      | advance on first cycle hcount ≥ 63*8+12 = 516. Since `hcount` is 9-bit (max 511), this WAIT **never fires** within a single scanline — documented as a program error, see EDG-02. | `copper.vhd:94`; `copper.vhd:35` |
| WAI-04 | vpos mismatch stalls              | Instr[0]=WAIT (0,100) = `0x8064`; cvc held at 99 | 1000 clocks with hcount sweeping                                    | addr stays 0 throughout.                                                                                         | `copper.vhd:94` |
| WAI-05 | vpos equality, not >=             | Instr[0]=WAIT (0,100); cvc held at 101           | 1000 clocks                                                         | addr stays 0 — equality required, not `>=`.                                                                      | `copper.vhd:94` |
| WAI-06 | hcount >= once matched            | Instr[0]=WAIT hpos=5 vpos=10 (`0x0A0A`→`0x8A0A`); cvc=10 | hcount=52 (exact); then hcount=60                             | advance at hcount=52; if stimulus is restarted at hcount=60 instead, it still advances.                           | `copper.vhd:94` |
| WAI-07 | WAIT then MOVE                    | Instr[0]=WAIT (0,50)=`0x8032`, Instr[1]=`0x4077` | cvc stepped 0..51 with hcount=12                                    | addr stays 0 while cvc<50. On cvc=50 hcount≥12, addr 0→1. Next cycle MOVE executes (`copper_data_o=0x4077`, dout=1). | `copper.vhd:85-110` |
| WAI-08 | Multiple WAITs                    | Instr[0]=WAIT(0,50), Instr[1]=WAIT(0,100), Instr[2]=`0x40AA` | stepped timing                                              | MOVE fires only on the cycle after cvc=100 hcount≥12, never before.                                              | `copper.vhd:85-110` |
| WAI-09 | WAIT for line 0 edge case         | Instr[0]=`0x8000`; offset=0                      | run real ULA timing for 1 frame                                     | Copper advances on the first cvc=0 line when hcount reaches 12 (remember cvc=offset at start-of-active, see OFS-*). | `copper.vhd:94`; `zxula_timing.vhd:462` |
| WAI-10 | Impossible WAIT, run-once         | mode=`01`, Instr[0]=WAIT vpos=500 hpos=63 (unreachable), Instr[1]=MOVE | run 5 frames                                                | addr stays 0 forever; MOVE never fires; `copper_req` never rises.                                                | `copper.vhd:85-96` |
| WAI-11 | Missed-line WAIT in Run mode      | mode=`01`, cvc already past 50, Instr[0]=WAIT(0,50), Instr[1]=MOVE | run 2 frames                                           | addr stays 0 **indefinitely** — run mode does not restart at vblank. (Contrast CTL-07.)                          | `copper.vhd:80-83, 94` |
| WAI-12 | Missed-line WAIT in Loop mode     | mode=`11`, Instr[0]=WAIT(0,50), Instr[1]=MOVE; start cvc past 50 | run 2 frames                                            | First frame: WAIT never matches. Vblank restart (cvc=0, hcount=0) resets addr to 0. Next frame: WAIT matches on cvc=50, MOVE fires. | `copper.vhd:80-83` |

### Group 4 — Start modes (corrected)

| ID       | Title                                        | Pre                                                        | Stimulus                                                              | Expected                                                                                                         | VHDL |
|----------|----------------------------------------------|------------------------------------------------------------|-----------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------|------|
| CTL-00   | Reset → mode `00` is idle                    | hard reset                                                 | step 1000 clocks                                                      | `copper_list_addr_s=0`, `copper_dout_s=0` throughout.                                                            | `copper.vhd:60-65, 112-114` |
| CTL-01   | `00` freezes but does not reset              | Run some instructions to push `copper_list_addr_s` to 5, then `NR 0x62 <= 0x00` | step 1000 clocks                                           | `copper_list_addr_s` stays at **5**, not 0. `copper_dout_s=0`.                                                   | `copper.vhd:70-78, 112-114` |
| CTL-02   | `01` resets addr on entry from `00`          | addr=5, mode=`00`                                          | `NR 0x62 <= 0x40` (mode `01`)                                         | Next cycle addr=0, execution begins from instr[0].                                                               | `copper.vhd:74-76`      |
| CTL-03   | `11` resets addr on entry from `00`          | addr=5, mode=`00`                                          | `NR 0x62 <= 0xC0`                                                     | Next cycle addr=0, execution begins.                                                                             | `copper.vhd:74-76`      |
| CTL-04   | `01` does **not** loop                       | mode=`01`, short all-MOVE program, run through             | run 5 frames                                                          | Each MOVE fires exactly once, then Copper stalls reading past end-of-program data (see EDG-01 for wrap behaviour); no vblank restart. | `copper.vhd:80-83`      |
| CTL-05   | `11` loops at `cvc=0, hcount=0`              | mode=`11`, `Instr[0]=MOVE 0x4055`, rest are NOPs           | run 3 frames                                                          | `copper_req` for `NR 0x40 = 0x55` fires **once per frame**, on the cycle immediately after the cvc=0 restart.     | `copper.vhd:80-83`      |
| CTL-06a  | **Mode `10` does NOT reset addr on entry**   | Run mode=`01` until `copper_list_addr_s=5`, then `NR 0x62 <= 0x00` (stops with addr=5), then `NR 0x62 <= 0x80` (mode `10`) | step 2 clocks                                     | addr is **5** on the entry cycle (mode-change branch does not touch addr for `10`). Next cycle the Copper fetches instr[5] and executes it. This is the corrected behaviour that retracts old CTL-06. | `copper.vhd:70-78`      |
| CTL-06b  | **Mode `10` does NOT restart at vblank**     | Run mode=`10`, small program; let Copper finish            | run multiple frames                                                   | `copper_req` does not re-fire on subsequent frames; `copper_list_addr_s` keeps advancing (or wrapping at 1024) without a restart. | `copper.vhd:80-83`      |
| CTL-06c  | Mode `10` resumes after pause                | mode=`01`, run to addr=3 mid-WAIT, `NR 0x62 <= 0x00`, wait 100 clocks, `NR 0x62 <= 0x80` | step clocks with WAIT now matching                       | Copper resumes the WAIT at addr=3, advances to 4, continues. addr=0 is **never** observed during the resume.     | `copper.vhd:70-85`      |
| CTL-07   | Mode change clears pending MOVE pulse        | mode=`01`, Instr[0]=MOVE; stimulate so `copper_dout_s=1` is set, then on the very next cycle write `NR 0x62 <= 0x00` | observe                                          | `copper_dout_s` goes to 0 on the mode-change cycle; no `copper_req` rising edge beyond the one already latched. | `copper.vhd:78`         |
| CTL-08   | Same-mode rewrite does not reset addr        | mode=`01`, addr=7                                          | `NR 0x62 <= 0x40` again (same mode bits)                              | `last_state_s == copper_en_i` already, so the mode-change branch is not entered; addr stays at 7.                | `copper.vhd:70`         |
| CTL-09   | Mode `01` → `11` mid-execution               | mode=`01`, addr=5                                          | `NR 0x62 <= 0xC0`                                                     | Mode-change branch resets addr to 0 (new state is `11`). Also now subject to vblank restart.                     | `copper.vhd:74-76, 80-83` |
| CTL-10   | Mode `11` → `10` mid-execution               | mode=`11`, addr=5                                          | `NR 0x62 <= 0x80`                                                     | Mode-change branch runs (states differ) but the inner reset-to-0 does **not** fire (new state is `10`). addr stays 5. `copper_dout_s` cleared. | `copper.vhd:70-78` |

### Group 5 — Timing and throughput

| ID     | Title                                | Pre                                                 | Stimulus                                             | Expected                                                                                               | VHDL |
|--------|--------------------------------------|-----------------------------------------------------|------------------------------------------------------|--------------------------------------------------------------------------------------------------------|------|
| TIM-01 | MOVE is 2 Copper clocks              | Instr[0]=MOVE                                       | step 4 clocks                                        | Cycle A: addr 0→1, dout=1. Cycle B: dout=0. Cycle C: addr 1→2 (next instruction fetch).                 | `copper.vhd:87-89, 100-108` |
| TIM-02 | WAIT stall is 1 clock per no-match   | Instr[0]=WAIT mismatch                              | step 10 clocks                                       | addr stays 0 every cycle; `copper_dout_s` stays 0.                                                     | `copper.vhd:92-98` |
| TIM-03 | 10 consecutive MOVEs take 20 clocks  | Instr[0..9]=distinct MOVEs, Instr[10]=WAIT impossible | step 21 clocks                                     | Exactly 10 `copper_req` pulses observed over clocks 0..19; clock 20 begins the impossible WAIT stall.  | `copper.vhd:85-110` |
| TIM-04 | WAIT then MOVE pipeline              | see MOV-06                                          | as MOV-06                                            | WAIT match cycle advances addr; MOVE executes on the very next clock, no dead cycle in between.        | `copper.vhd:85-110` |
| TIM-05 | Dual-port instr fetch available      | Write instr[0] via `NR 0x60`, then immediately set mode=`01` | step 2 clocks                              | On the cycle where Copper enters "run", `copper_list_data_i` already reflects the freshly-written byte pair. Documents that our emulator collapses the pos/neg edge timing correctly. | `zxnext.vhd:3959-3998` |

### Group 6 — Copper vertical offset (`NR 0x64`)

| ID     | Title                          | Pre                              | Stimulus                                                  | Expected                                                                                                    | VHDL |
|--------|--------------------------------|----------------------------------|-----------------------------------------------------------|-------------------------------------------------------------------------------------------------------------|------|
| OFS-01 | Default offset = 0             | hard reset                       | run 1 frame with real ULA timing; sample `cvc` at start-of-active | `cvc=0` at the first `ula_min_vactive`.                                                               | `zxnext.vhd:5024`; `zxula_timing.vhd:462` |
| OFS-02 | Non-zero offset loads `cvc`    | `NR 0x64 <= 0x20`                | run 1 frame                                              | `cvc=0x20` at start-of-active.                                                                              | `zxula_timing.vhd:462` |
| OFS-03 | WAIT resolves on offset cvc    | offset=10, mode=`01`, Instr[0]=WAIT(0,10), Instr[1]=MOVE | 1 frame                                      | MOVE fires on the **first** active scanline (because cvc is already 10 there), not line 10.                  | `zxula_timing.vhd:462`; `copper.vhd:94` |
| OFS-04 | Offset read-back               | `NR 0x64 <= 0x80`, read          | —                                                         | returns `0x80`.                                                                                              | `zxnext.vhd:6090`      |
| OFS-05 | Offset reset                    | hard reset                       | read `NR 0x64`                                           | returns `0x00`.                                                                                              | `zxnext.vhd:5024`      |
| OFS-06 | `cvc` wraps at `c_max_vc`      | offset=0, mode=`11`, Instr[0]=MOVE, rest NOP | run until `cvc = c_max_vc`                    | On the line after `c_max_vc`, cvc wraps to 0. Mode-`11` restart fires (hcount=0 on that line), MOVE re-executes. | `zxula_timing.vhd:463-464`; `copper.vhd:80-83` |

### Group 7 — NextREG write arbitration

All tests in this group require the harness to schedule a CPU `NR` write on
the *same 28 MHz clock* that the Copper issues its MOVE write-pulse.

| ID     | Title                                  | Pre                                        | Stimulus                                                                          | Expected                                                                                                       | VHDL |
|--------|----------------------------------------|--------------------------------------------|-----------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------|------|
| ARB-01 | Copper wins simultaneous write         | mode=`01`, Instr[0]=MOVE 0x40,0x55; schedule CPU `out (NR) 0x40,0xAA` on the same cycle | step 1 clock                                                                 | `nr_wr_en=1`, `nr_wr_reg=0x40`, `nr_wr_dat=0x55` (Copper). `cpu_req` not cleared.                              | `zxnext.vhd:4775-4777, 4769` |
| ARB-02 | CPU write deferred until Copper clears | As ARB-01                                  | step 2 clocks                                                                     | Cycle A: as ARB-01. Cycle B: `copper_req=0`, `nr_wr_en=1`, `nr_wr_reg=0x40`, `nr_wr_dat=0xAA` (deferred CPU).   | `zxnext.vhd:4769, 4775-4777` |
| ARB-03 | Copper priority on different registers | CPU wants `NR 0x07 <= 0x10`, Copper wants `NR 0x40 <= 0x55` same cycle | step 2 clocks                                                       | Cycle A: Copper `NR 0x40`. Cycle B: CPU `NR 0x07`.                                                             | `zxnext.vhd:4769-4777` |
| ARB-04 | Copper reg width masked to 7 bits      | Instr[0]=MOVE with reg bits=`1111111` (0x7F)| step 2 clocks                                                                    | `copper_nr_reg = 0x7F` (bit 7 always 0 per the prepend). Copper cannot address `NR 0x80..0xFF`.                | `zxnext.vhd:4731`      |
| ARB-05 | No Copper request when stopped         | mode=`00`                                  | CPU writes `NR 0x40 <= 0xAA`                                                     | `copper_req` never rises; CPU write completes with 0-cycle stall.                                              | `zxnext.vhd:4709`; `copper.vhd:112-114` |
| ARB-06 | Copper write to `NR 0x02` triggers NMI signals | mode=`01`, Instr[0]=MOVE to `NR 0x02` with data=0x08 (bit 3) | step 2 clocks                                                         | `nmi_cu_02_we = 1` on the cycle `copper_req=1` with `copper_nr_reg=0x02`; `nmi_gen_nr_mf = 1`.                  | `zxnext.vhd:3830-3832` |

### Group 8 — Self-modifying Copper (reinstated)

These tests replace the old EDG-03/EDG-04 stubs.

| ID     | Title                                                | Pre                                                                                                   | Stimulus                                                          | Expected                                                                                                       | VHDL |
|--------|------------------------------------------------------|-------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------|------|
| MUT-01 | Copper writes `NR 0x62` to stop itself               | mode=`01`, Instr[0]=`MOVE 0x62,0x00`, Instr[1]=`MOVE 0x40,0xFF`                                       | step 8 clocks                                                     | Cycle A: MOVE writes `nr_62_copper_mode <= "00"`. Subsequent clocks: mode-change branch triggers, `copper_dout_s` cleared, `copper_list_addr_s` held at 1 (no reset because new mode is `00`). MOVE-`NR 0x40` at addr 1 **never** fires. | `zxnext.vhd:5430`; `copper.vhd:70-78, 112-114` |
| MUT-02 | Copper writes `NR 0x62` to switch itself to mode `10`| mode=`01`, Instr[0]=`MOVE 0x62,0x80`, Instr[1]=`MOVE 0x40,0xAA`                                       | step 8 clocks                                                     | MOVE updates mode to `"10"`. Mode-change branch fires (states differ) but the reset-to-0 inner `if` does not. `copper_list_addr_s` stays at 1. Next fetch is instr[1]; MOVE fires. | `copper.vhd:70-78`     |
| MUT-03 | Copper writes its own addr-hi via `NR 0x62`          | mode=`01`, Instr[0]=`MOVE 0x62,0x41` (mode=`01`, addr_hi=`001`), Instr[1..]=filler                     | step 4 clocks                                                     | CPU-side `nr_copper_addr(10..8)` becomes `001` (byte pointer jumps to 0x100); Copper-side `copper_list_addr_s` is **not** affected (it is a separate counter updated only by the Copper state machine). Verifies the two pointers are independent. | `zxnext.vhd:5430-5431`; `copper.vhd:48` |
| MUT-04 | Copper writes RAM via `NR 0x60` inside a MOVE        | mode=`01`, `nr_copper_addr=0x010` pre-set, Instr[0]=`MOVE 0x60,0xAB`                                  | step 4 clocks                                                     | On the MOVE-pulse cycle, `nr_wr_reg=0x60`, `nr_wr_dat=0xAB`. Because `nr_copper_write_8=1` and byte addr bit 0 = 0, MSB RAM[8] receives `0xAB`; `nr_copper_addr` auto-increments to `0x011`. Copper self-modifies its own instruction RAM live. | `zxnext.vhd:3977, 3978, 4884-4886, 5419-5424` |

### Group 9 — Edge cases and corners

| ID     | Title                                    | Pre                                                   | Stimulus                          | Expected                                                                                                                                   | VHDL |
|--------|------------------------------------------|-------------------------------------------------------|-----------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------|------|
| EDG-01 | Instruction address wraps at 1024        | mode=`01`, all 1024 instr = NOP MOVE (reg=0)          | run enough clocks for 1024 MOVEs  | `copper_list_addr_s` sequence `0,1,...,1023,0,1,...`. 10-bit wrap is silent (`+1` on a 10-bit vector).                                      | `copper.vhd:48, 108` |
| EDG-02 | Empty program (first slot WAIT impossible)| mode=`01`, Instr[0]=WAIT vpos=500 hpos=63 (unreachable)| run 5 frames                    | addr stays 0; no Copper requests.                                                                                                          | `copper.vhd:92-96` |
| EDG-03 | Program at max size                      | mode=`01`, 1023 NOP MOVEs then Instr[1023]=MOVE NR 0x40,0xEE | run long enough                 | `copper_req` for NR 0x40 fires exactly once, on the cycle after addr reaches 1023.                                                          | `copper.vhd:108`    |
| EDG-04 | Copper stopped mid-MOVE pulse            | As CTL-07 but timing-accurate                         | —                                 | `copper_dout_s` cleared on the mode-change cycle; but the `copper_req` that was already latched by `copper_requester_d=0→1` on the *previous* cycle still causes one NextREG write. (Documents that a Copper MOVE that has already issued its edge cannot be aborted mid-flight.) | `zxnext.vhd:4709, 4729`; `copper.vhd:78` |
| EDG-05 | Mode `11` restart coincident with MOVE   | mode=`11`, Instr[0]=MOVE; arrange timing so the restart hits on the same cycle a pending MOVE pulse is being asserted | —                                                                            | Mode-`11` restart does `copper_list_addr_s<=0; copper_dout_s<=0` (`copper.vhd:82-83`), and the restart branch is checked **before** the execution branch, so the MOVE pulse is suppressed that cycle. First MOVE fires on the cycle after restart. | `copper.vhd:80-83` |
| EDG-06 | WAIT hpos=0 matches at hcount=12         | Instr[0]=WAIT(0,0)                                    | sweep hcount 0..20               | advance occurs on the first clock with hcount≥12. Explicitly tests the `+12` offset constant.                                              | `copper.vhd:94`    |
| EDG-07 | All-WAIT program in Run mode             | mode=`01`, 1024 WAIT instructions all impossible      | run 5 frames                     | addr stays 0 the whole time; no `copper_req`.                                                                                              | `copper.vhd:92-98` |
| EDG-08 | All-NOP program                           | mode=`01`, 1024 MOVE reg=0 instructions               | run 2100 clocks                  | No `copper_req` ever. `copper_list_addr_s` wraps 0→1023→0. `copper_dout_s` stays 0 throughout.                                             | `copper.vhd:104-108` |
| EDG-09 | Rapid mode toggling                      | mode=`00`→`01`→`00`→`11`→`10` in 5 consecutive clocks, all before any instruction fetch completes | observe                          | Each `01` and `11` entry resets addr to 0. The final `10` does not reset. `copper_dout_s` cleared on every transition.                     | `copper.vhd:70-78` |

### Group 10 — Reset

| ID     | Title                | Stimulus    | Expected                                                                                                 | VHDL                |
|--------|----------------------|-------------|----------------------------------------------------------------------------------------------------------|---------------------|
| RST-01 | Copper hard reset    | assert reset | `copper_list_addr_s=0`, `copper_dout_s=0`, `copper_data_o=0`.                                           | `copper.vhd:60-65`  |
| RST-02 | NR state reset       | assert reset | `nr_62_copper_mode="00"`, `nr_copper_addr=0`, `nr_copper_data_stored=0x00`, `nr_64_copper_offset=0x00`. | `zxnext.vhd:5020-5024` |
| RST-03 | `last_state_s` reset | assert reset, then `NR 0x62 <= 0x00` | On a hard reset `last_state_s` is `"00"` (VHDL initializer `copper.vhd:50`). Writing `NR 0x62 <= 0x00` is therefore a no-op: the mode-change branch is not entered. Verifies initial-value semantics. | `copper.vhd:50, 70` |

## Test Count Summary

| Group                         | Tests |
|-------------------------------|------:|
| 1. RAM upload / addressing    |    10 |
| 2. MOVE execution             |     7 |
| 3. WAIT execution             |    12 |
| 4. Start modes (incl. CTL-06a/b/c for mode 10) | 13 |
| 5. Timing / throughput        |     5 |
| 6. Vertical offset            |     6 |
| 7. Arbitration                |     6 |
| 8. Self-modifying Copper      |     4 |
| 9. Edge cases                 |     9 |
| 10. Reset                     |     3 |
| **Total**                     | **75** |

**No pass-count is claimed in this plan.** Pass/fail numbers belong in the
runner output, not the design doc.

## Open Questions (VHDL ambiguities)

1. **Mode `10` + CPU `NR 0x61`/`NR 0x62` addr writes while running** —
   `nr_copper_addr` is the *CPU byte pointer*, and `copper_list_addr_s` is
   the *Copper instruction pointer*. The former drives `addr_a_i` of the
   RAMs (`zxnext.vhd:3968, 3989`), the latter drives `addr_b_i`
   (`zxnext.vhd:3973, 3994`). It is clear from the VHDL that CPU pointer
   changes do **not** move the Copper's fetch pointer, but the plan should
   additionally check whether any part of `zxnext.vhd` synchronises them at
   mode-change time. Current reading: no, they are independent. MUT-03
   encodes this as an assertion. Flagging for reviewer confirmation.
2. **Exact timing of the rising-edge `copper_req` capture vs. Copper
   mode-change** — `copper_requester_d` is a 1-cycle delay
   (`zxnext.vhd:4717`). If the Copper MOVE cycle and the mode-change cycle
   are the same clock, does the already-latched `copper_req` survive? EDG-04
   asserts "yes" based on a straight reading of `zxnext.vhd:4726-4734`, but
   this should be reviewed.
3. **`c_max_vc` exact value** — `zxula_timing.vhd` defines it per timing
   model (50/60 Hz, HDMI). OFS-06 needs the value for the specific timing
   mode under test; the test harness must read `c_max_vc` via the same
   constant, not hard-code a number.
4. **`+12` constant semantics** — `copper.vhd:94` adds 12 to the computed
   threshold. This is pixels (ULA clock ticks), not Copper clocks. Harness
   must clearly distinguish `hcount` (ULA, 9 bits, ticks at ULA clock) from
   Copper's own 28 MHz clock. WAI-01/EDG-06 pin this down.
5. **`ula_min_vactive` timing** — `zxula_timing.vhd:461-462` reloads `cvc`
   with `i_cu_offset` at start-of-active-display, not at start-of-frame.
   OFS-* must use real timing to verify, because the mock cannot replicate
   the `ula_min_vactive` pulse correctly without replicating the full
   timing constants.
6. **Copper MOVE to `NR 0x60` self-write — address auto-increment** —
   MUT-04 assumes that routing the Copper data through the NextREG write
   path activates the exact same auto-increment logic
   (`zxnext.vhd:5424`). This is consistent with `nr_wr_reg=0x60` entering
   the same `case` branch, but should be re-verified when running MUT-04
   end-to-end.

## File Layout (unchanged from previous plan)

```
test/
  copper/
    copper_test.cpp
    test_cases.h
  CMakeLists.txt
doc/design/
  COPPER-TEST-PLAN-DESIGN.md   # this document
```

## How to Run

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
./build/test/copper_test
bash test/regression.sh
```
