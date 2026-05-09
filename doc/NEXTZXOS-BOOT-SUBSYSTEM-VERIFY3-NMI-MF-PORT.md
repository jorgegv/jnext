# Pass-3 Blind Verification Re-Audit — NMI + Multiface + NextREG + Port

**Date**: 2026-05-09
**Auditor**: ULTRATHINK pass-3 blind re-audit (post 2× prior passes)
**Worktree**: `.claude/worktrees/task2-verify3-nmi-mf-port`
**Branch**: `task2/verify3-nmi-mf-port`
**Base HEAD**: `7747202`

---

## Verdict

**Audit did NOT converge** — found **3 new class-(a) bugs** that
both prior passes missed. All three involve NR readback / event-capture
gates that the C++ implements unconditionally where VHDL applies a
specific gate. Tests still 37/37 pass after fixes (no regressions).

| # | Class | Subsystem | Title |
|---|-------|-----------|-------|
| 1 | (a) | NextREG | NR 0x02 bit 7 (`nr_02_bus_reset`) not captured / not surfaced on readback |
| 2 | (a) | NextREG / Multiface | NR 0x0A bits 7:6 (`nr_0a_mf_type`) readback ignored config_mode write gate |
| 3 | (a) | NextREG / Iotrap | NR 0xDA / NR 0xD9 iotrap event capture not gated on `nmi_accept_cause` |

Three additional **class-(b) deviations** identified but NOT fixed (documented
in this report under "Class-(b) findings"). One class-(c) cosmetic finding.

---

## Methodology

### Worktree setup

```bash
git -C /home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify3-nmi-mf-port \
    branch --show-current   # → task2/verify3-nmi-mf-port
```

Base = `7747202` (the post-pass-2 aggregate). No prior verify reports read
(blind constraint).

### Files audited

Primary subsystem source files:

- `src/peripheral/nmi_source.{cpp,h}` (567 + 348 lines)
- `src/peripheral/multiface.{cpp,h}` (386 + 260 lines)
- `src/port/nextreg.{cpp,h}` (214 + 103 lines)
- `src/port/port_dispatch.{cpp,h}` (102 + 68 lines)

Cross-cutting (NR handler installation, NMI consumption):

- `src/core/emulator.cpp` (~6300 lines — focused on lines 1525-1700,
  2350-2470, 2855-3060, 4500-4900 NMI/MF/iotrap blocks)
- `src/core/emulator.h` (~840 lines)
- `src/cpu/z80_cpu.cpp` (NMI servicing path lines 390-410)

### VHDL oracle

- `cores/zxnext/src/zxnext.vhd` (7595 lines) — primary reference.
  Lines exhaustively cross-checked: 932-963 (NMI signal decls), 1095
  (`nr_02_bus_reset` decl), 1099-1133 (NR 0x03/05/06/0A/10 power-on
  defaults), 1226-1230 (NR 0x82-85 enable defaults), 1253-1265
  (NR C2/C3/C4/D8/D9/DA decls), 1306 (`nr_02_reset_type` decl),
  1730-1739 (reset wiring + NR 0x02 reset_type FSM), 2050-2170
  (NMI pipeline + arbiter + FSM + outputs), 2382-2430 (port-enable
  fanouts), 2598-2616 (port_xffd / port_mf_enable decode), 2730-2733
  (port_mf_*_rd/wr strobes), 2848-2912 (mf_a_0066 + automap_nmi),
  3306-3322 (port_e7 sd_swap), 3820-3898 (NR 0x02 sw-NMI strobes +
  iotrap cause/write capture), 4111-4310 (DivMMC retn + Multiface
  inst), 4926-5111 (synchronous reset block), 5052-5068 (port-enable
  reset gating), 5117-5170 (NR 0x02/03/05/06 write decode), 5191-5198
  (NR 0x0A write decode), 5500-5522 (NR 82-89 write decode), 5878-6300
  (full NR readback mux), 6340-6371 (hotkey + nr_02_soft_reset/hard_reset
  derivation).

- `cores/zxnext/src/device/multiface.vhd` (197 lines) — completely
  re-verified against `Multiface::clock_edge_()`.

### Audit technique

For each class-(a) candidate, three orthogonal angles:

1. **Direct C++→VHDL line-by-line comparison** — every assignment to
   the corresponding signal in the VHDL synchronous process compared
   to its C++ counterpart, gates and order included.
2. **Boundary-input sweep** — write 0x00 / 0x80 / 0x7F / 0xFF / boundary
   values to each NR with config_mode=1 vs config_mode=0, FSM states
   IDLE/FETCH vs HOLD/END. Find divergence.
3. **Reset-type sweep** — what does power-on, hard reset, soft reset
   each do to the field per VHDL? Compare to C++ `init()` / `reset()`
   / `soft_reset()` paths.

### Out-of-scope guardrail

Did NOT read `doc/NEXTZXOS-BOOT-SUBSYSTEM-{ANALYSIS,VERIFY}-*.md` per
the blind-audit constraint. All findings derived independently from VHDL.

---

## Class-(a) findings (FIXED)

### Finding 1 — NR 0x02 bit 7 (`nr_02_bus_reset`) latch missing

**VHDL authority**: `cores/zxnext/src/zxnext.vhd`

- Signal decl: line 1095 — `signal nr_02_bus_reset : std_logic := '0';`
  (initializer only, NO reset clause anywhere — survives both hard +
  soft reset).
- Write capture: line 5119 — `nr_02_bus_reset <= nr_wr_dat(7);` (every
  NR 0x02 write captures bit 7 verbatim).
- Read composition: line 5891 — `port_253b_dat <= nr_02_bus_reset & "00"
  & nr_02_iotrap & nr_02_generate_mf_nmi & nr_02_generate_divmmc_nmi
  & nr_02_reset_type(1 downto 0);` — bit 7 of the NR 0x02 readback IS
  `nr_02_bus_reset`.
- Output usage: line 1579 — `o_RESET_PERIPHERAL <= nr_02_bus_reset;`
  (peripheral / ESP / expansion-bus reset signal — drives a top-level
  pin in real hardware).

**Pre-fix C++ behaviour** (`src/core/emulator.cpp:1605-1621`):

- The NR 0x02 read handler composed bits 4 (iotrap) + 3 + 2 + 1:0
  (NmiSource) but explicitly returned **zero** for bit 7. The
  pre-existing comment acknowledged the gap: *"Bit 7 (bus_reset) is
  not yet modelled in jnext."*
- The NR 0x02 write handler did NOT capture bit 7 anywhere.
- Net effect: firmware reading NR 0x02 always saw bit 7 = 0 even after
  writing bit 7 = 1 — a clear specification deviation observable to
  any NR 0x02 round-trip test.

**Fix** (commit at top of branch):

- Added `bool nr_02_bus_reset_ = false;` to `Emulator` (`emulator.h`).
- NR 0x02 write handler captures `(v & 0x80) != 0` into the latch.
- NR 0x02 read handler ORs `0x80` into the result when the latch is set.
- `init()` does NOT reset the field (matches VHDL: no reset clause).
- Save/load state appended at end-of-snapshot with EOF tolerance.

**Test impact**: 37/37 ctest still pass. No NR 0x02 readback test
existed for bit 7 — the regression was completely silent. Added
detection coverage to verify3 corpus would require a new test row
(out-of-scope for this audit; recommended follow-up).

### Finding 2 — NR 0x0A bits 7:6 (`nr_0a_mf_type`) readback ignored config_mode gate

**VHDL authority**: `cores/zxnext/src/zxnext.vhd`

- Write decode (lines 5191-5198):
  ```vhdl
  when X"0A" =>
     if nr_03_config_mode = '1' then
        nr_0a_mf_type <= nr_wr_dat(7 downto 6);
        nr_0a_sd_swap <= nr_wr_dat(5);
     end if;
     nr_0a_divmmc_automap_en <= nr_wr_dat(4);
     nr_0a_mouse_button_reverse <= nr_wr_dat(3);
     nr_0a_mouse_dpi <= nr_wr_dat(1 downto 0);
  ```
  Note bits 7:6 (mf_type) and bit 5 (sd_swap) are **gated on
  `nr_03_config_mode='1'`**. Bits 4, 3, 1:0 commit unconditionally.

- Read composition (line 5912):
  ```vhdl
  when X"0A" =>
     port_253b_dat <= nr_0a_mf_type & nr_0a_sd_swap
                    & nr_0a_divmmc_automap_en & nr_0a_mouse_button_reverse
                    & '0' & nr_0a_mouse_dpi;
  ```
  Bits 7:6 of the readback ARE `nr_0a_mf_type` (the gated FF), NOT the
  raw last-written byte.

**Pre-fix C++ behaviour** (`src/core/emulator.cpp:908-916`):

- Write handler at 883-899 correctly gates `multiface_.set_mode(...)`
  and `spi_.set_sd_swap(...)` on `nextreg_.nr_03_config_mode()`. So the
  authoritative state is correct.
- Read handler sourced bits 7:6 from `cached(0x0A) & 0xC0` — the **raw
  last-written byte**, NOT the gated authoritative state.
- `nextreg_.write(reg, val)` stores the raw byte in `regs_[]` regardless
  of any handler-side gating (`NextReg::write` returns the
  write_handler's return value verbatim, but the write_handler returns
  `v` unchanged).
- Net effect: firmware writes NR 0x0A = 0xC0 outside config_mode →
  VHDL keeps mf_type unchanged, NR 0x0A readback returns the unchanged
  mf_type; C++ stores 0xC0 in cached, NR 0x0A readback returns 0xC0
  for bits 7:6 — diverging.

**Fix**:

- Read handler now sources bits 7:6 from `multiface_.mf_type()` (the
  authoritative gated state owned by the Multiface subsystem). Same
  pattern as bit 2 (`nr_06_ps2_mode_`) and bit 5 (`spi_.sd_swap()`),
  bit 4 (`divmmc_.nr_0a_4_enable()`), bit 3 (`mouse_.button_reverse()`),
  bits 1:0 (`mouse_.dpi()`) — all already use authoritative accessors.
- The bits 7:6 source was the only divergence; bit 5 was already
  correct (`spi_.sd_swap()`).

**Test impact**: 37/37 ctest still pass. Existing tests don't write NR
0x0A bits 7:6 outside config_mode then read back, so the divergence
was not previously detectable. Recommended follow-up: add a
`nextreg_integration_test` row that writes NR 0x0A = 0xC0 with
`nr_03_config_mode = 0`, asserts readback bits 7:6 unchanged from prior.

### Finding 3 — NR 0xDA / NR 0xD9 iotrap capture missing `nmi_accept_cause` gate

**VHDL authority**: `cores/zxnext/src/zxnext.vhd:3866-3898`

- NR 0xDA (`nr_da_iotrap_cause`) clocked process:
  ```vhdl
  process (i_CLK_28)
  begin
     if rising_edge(i_CLK_28) then
        if reset = '1' then
           nr_da_iotrap_cause <= (others => '0');
        elsif nmi_gen_iotrap = '1' and nmi_accept_cause = '1' then  -- <<<
           if    port_2ffd_rd = '1' then nr_da_iotrap_cause <= "01";
           elsif port_3ffd_rd = '1' then nr_da_iotrap_cause <= "10";
           else                          nr_da_iotrap_cause <= "11";
           end if;
        elsif nr_02_we = '1' and nr_wr_dat(4) = '0' then
           nr_da_iotrap_cause <= (others => '0');
        end if;
     end if;
  end process;
  ```

- NR 0xD9 (`nr_d9_iotrap_write`) clocked process:
  ```vhdl
  elsif port_3ffd_wr = '1' and nmi_accept_cause = '1' then  -- <<<
     nr_d9_iotrap_write <= cpu_do;
  ```

- `nmi_accept_cause` (line 2164):
  ```vhdl
  nmi_accept_cause <= '1' when nmi_state = S_NMI_IDLE
                            or nmi_state = S_NMI_FETCH
                          else '0';
  ```

So both `nr_da_iotrap_cause` and `nr_d9_iotrap_write` update on a trap
event ONLY when the NMI FSM is in IDLE or FETCH. While the FSM is in
HOLD or END, an iotrap-port access does NOT update either field —
the trap-cause stays latched at whatever caused entry into the current
NMI handler (or zero), and the captured-write byte stays at the
pre-NMI value.

**Pre-fix C++ behaviour** (`src/core/emulator.cpp:2380-2412`):

The three iotrap port handlers (0x2FFD READ, 0x3FFD READ, 0x3FFD WRITE)
all updated `nr_da_iotrap_cause_` and (for 0x3FFD WRITE) `nr_d9_iotrap_write_`
unconditionally on every trapped access — gated only on `nr_d8_io_trap_fdc_en_`,
NOT on the FSM state.

Concrete divergence scenario:

1. NR 0xD8 ← 1 (enable iotrap).
2. NR 0x06 ← 0x08 (enable MF NMI).
3. CPU writes 0x3FFD with byte X → trap fires, FSM IDLE→FETCH,
   `nr_da_iotrap_cause = "11"`, `nr_d9_iotrap_write = X`.
4. Z80 takes NMI, fetches 0x0066 → FSM FETCH→HOLD.
5. NMI handler running: CPU writes 0x3FFD with byte Y.
   - VHDL: FSM in HOLD, `nmi_accept_cause = 0`, fields stay at "11" / X.
   - C++ pre-fix: fields update to "11" / Y — wrong.

**Fix**:

- Added `Emulator::nmi_accept_cause_()` private const helper that
  returns true iff `nmi_source_.state()` is `Idle` or `Fetch`.
- Wrapped each `nr_da_iotrap_cause_` / `nr_d9_iotrap_write_` capture
  inside `if (nmi_accept_cause_()) { ... }`.
- The `nmi_source_.strobe_iotrap()` call is OUTSIDE the gate (the
  combinational `nmi_gen_iotrap` line in VHDL fires on every trapped
  access regardless of FSM state — only the cause-latch update is
  gated).

**Test impact**: 37/37 ctest still pass. Existing tests don't exercise
the in-NMI iotrap re-trigger path. Recommended follow-up: add test row
"FSM in HOLD: 0x3FFD WRITE does NOT change `nr_da_iotrap_cause_`".

---

## Class-(b) findings (NOT fixed — known approximations / scope-limited)

### B-1 — NR 0x86-0x88 reset gate

**VHDL** (zxnext.vhd:5061-5068):
```vhdl
if nr_89_bus_port_reset_type = '0' then
   nr_86_bus_port_enable <= (others => '1');
   nr_87_bus_port_enable <= (others => '1');
   nr_88_bus_port_enable <= (others => '1');
   nr_89_bus_port_enable <= (others => '1');
end if;
```

Note `nr_89` reset_type is at bit 7 of NR 0x89, default '1' (line 1235),
so by default the reset block is **NOT** entered for NR 0x86-0x89.

**C++** (`src/port/nextreg.cpp:72-80`): unconditionally sets
`regs_[0x86] = regs_[0x87] = regs_[0x88] = 0xFF` on every reset; sets
`regs_[0x89] = 0x8F`.

The pre-existing comment acknowledges the deviation: *"jnext applies
unconditionally — same approximation as NR 0x82-0x84"*. Class-(b)
deviation; expansion-bus emulation not modelled, so functionally
invisible.

### B-2 — NR 0x80 reset preserves lower nibble

**VHDL** (zxnext.vhd:2185-2186):
```vhdl
if reset = '1' then
   nr_80_expbus(7 downto 4) <= nr_80_expbus(3 downto 0);
```

On reset, the upper nibble is replaced by the lower nibble (the lower
nibble survives reset, encoding the reset-defaults for the upper nibble).

**C++**: `regs_[0x80]` is reset to 0 (no special handling). Class-(b)
deviation; expansion-bus emulation not modelled.

### B-3 — NextReg standalone cold-init for NR 0x82-0x84

In `NextReg::reset()` (nextreg.cpp:7-65), the reset_type-1 condition
is read FROM `regs_[0x85]`. On the very first call (cold standalone
construction), `regs_[0x85] = 0`, so `reset_type_1 = false`, and
`regs_[0x82..0x84]` remain at their default-constructed value of 0
— NOT 0xFF as VHDL would have on power-on.

In the integrated `Emulator` path this is hidden because `init()` calls
`nextreg_.reset()` AFTER the `NextReg` constructor already ran one
reset; the second call sees `regs_[0x85] = 0x8F` and correctly fills
0x82-0x84 with 0xFF. Hence the existing test RST-08 passes.

A standalone use of `NextReg` would observe `read(0x82) = 0` instead
of 0xFF. **Class-(b)** — incidentally works in the integrated path;
defensive fix would seed `regs_[0x85] = 0x8F` in the constructor's
member initializer or run reset() twice in the constructor.

### B-4 — NR 0x06 b3, b4, b6, b2, b1:0 reset behavior

**VHDL** synchronous reset block (zxnext.vhd:4926-5111) only resets
`nr_06_hotkey_cpu_speed_en` and `nr_06_hotkey_5060_en` (lines 4932-4933).
The other NR 0x06 bits are not in the reset block, so their values
persist across both hard and soft reset.

**C++** `Emulator::init()` clears all NR 0x06-tracked subsystem state
on every reset (`nr_06_button_m1_nmi_en_ = false;`, `divmmc_enable_ =
false;`, etc.) and `regs_[0x06]` is cleared then re-seeded to 0xA0.

This is **debatable as a class-(a) bug**: per VHDL strict reading, F1
hard-reset should NOT clear NR 0x06 b3/b4. But the existing test
HK-09-INT (`test/nmi/nmi_integration_test.cpp:451-477`) explicitly
expects mf_enable to be cleared on F1, and cites VHDL :1109-1110 (the
power-on initializers) as authority — conflating power-on with
hard-reset.

Held as **class-(b) test-codified approximation**. Fix would require
both code and test plan changes — out of pass-3 verify-audit scope.

---

## Class-(c) findings (cosmetic / informational)

### C-1 — Vestigial Emulator::nmi_assert_mf() / nmi_assert_divmmc()

`emulator.h:399-402` retains `Emulator::nmi_assert_mf()` /
`nmi_assert_divmmc()` legacy methods that compose
`nr_06_button_m1_nmi_en_` with `test_hotkey_m1_` etc. — used only by
`test/input/input_test.cpp` test rows. The actual NMI gate is owned by
`NmiSource::nmi_assert_mf()`. Vestigial duplication; not removed (test
rewrite out of scope).

---

## Spot checks (sampling sweeps that found no new issues)

### NMI FSM transitions

All 4 transitions of `NmiSource::recompute_()` traced against VHDL
`nmi_state_t` (lines 2120-2162):

- IDLE → FETCH on `nmi_activated = '1'` (any latch set). C++ matches.
- FETCH → HOLD on `mf_a_0066 AND m1 AND mreq` (= PC=0x0066 + M1 + MREQ
  per `mf_a_0066 = port_00xx_msb AND port_66_lsb`). C++ matches.
- HOLD → END on `nmi_hold = 0`. The `nmi_hold` mux (line 2118):
  `mf_nmi_hold` if `nmi_mf=1` else `divmmc_nmi_hold` if `nmi_divmmc=1`
  else `nmi_assert_expbus`. C++ matches verbatim.
- END → IDLE on `cpu_wr_n = '1'` (handler's last bus cycle complete).
  C++ collapses this to "advance on next tick" (acceptable approximation
  per the source comment; the in-flight Z80 write completes before the
  next NMI gets generated regardless).

### NR 0x02 readback bit composition

VHDL:5891 — bit 7 = `nr_02_bus_reset` (FIX-1 above), bits 6:5 = "00",
bit 4 = `nr_02_iotrap` = `nr_da_iotrap_cause(1) OR (0)`, bit 3 =
`nr_02_generate_mf_nmi`, bit 2 = `nr_02_generate_divmmc_nmi`, bits 1:0 =
`nr_02_reset_type(1:0)`.

After fix #1: C++ composes bit 7 from `nr_02_bus_reset_`, bit 4 from
`nr_da_iotrap_cause_`, bits 3+2 from `nmi_source_.nr_02_read()` (which
sources from FSM-driven `nr_02_pending_*`), bits 1:0 from
`reset_type_`. All six bits match VHDL.

### NR 0x02 reset_type FSM advance

VHDL:1732-1739 — `nr_02_reset_type <= '0' & rt(2) & (rt(1) OR rt(0))`
on `nr_02_soft_reset` rising edge. Sequence: 100 → 010 → 001 → 001
(saturates).

C++ `NmiSource::strobe_soft_reset()` (`nmi_source.cpp:169-180`):
verified the bit math by hand against all three transitions. Match.

### Multiface FSM (4 FFs + combinational)

Cross-checked `Multiface::clock_edge_()` (`multiface.cpp:88-204`) against
multiface.vhd lines 122-184 in full:

- `port_io_dly` FF (lines 122-131) — match.
- `nmi_active` FF priority cascade (lines 137-148) — match (button_pulse
  > clear-conditions, with `port_io_dly_prev` gate on the
  port_*_clear path).
- `invisible` FF priority cascade (lines 152-163) — match (button_pulse
  > set-condition, with mode_p3 polarity inversion in the set path).
- `mf_enable` FF priority cascade (lines 171-184) — match (fetch_66
  set > port_dis_rd OR retn_seen clear > port_en_rd → NOT invisible_eff).
- Combinational outputs `mf_enable_eff` (line 186), `mf_port_en_o`
  (line 195) — match, surfaced via `is_mem_active()` and
  `update_mf_port_en_()` respectively.

### NMI priority arbiter

VHDL:2107-2113:
```
if    nmi_assert_mf  AND port_e3_reg(7)=0 AND divmmc_nmi_hold=0 then nmi_mf <= '1';
elsif nmi_assert_divmmc AND mf_is_active=0 then nmi_divmmc <= '1';
elsif nmi_assert_expbus then nmi_expbus <= '1';
```

C++ `NmiSource::recompute_()` lines 354-365 — match (with the structural
note that C++ uses sequential `if`/`if`/`if` instead of `elsif`, but
each subsequent clause includes the appropriate `!nmi_mf_` /
`!nmi_divmmc_` guard so the priority is preserved).

### Multiface port-decode dispatch (NR 0x83 b1 + per-mode LSB table)

VHDL:2612-2616 + 2730-2733. Per-mode table (LSBs vary by `nr_0a_mf_type`):

| `mf_type` | mode    | enable_io | disable_io |
|-----------|---------|-----------|------------|
| `00`      | mode_p3 | 0x3F      | 0xBF       |
| `01`      | mode_128| 0xBF      | 0x3F       |
| `10`      | mode_128| 0x9F      | 0x1F       |
| `11`      | mode_48 | 0x9F      | 0x1F       |

C++ IO observer at `emulator.cpp:339-363` — decoded LSB table by hand
against VHDL ternary cascade. Match. The IO observer fires for both
reads and writes, gated on `multiface_.is_enabled()` (= NR 0x83 b1).

### NR 0x06 readback bit composition

VHDL:5900 — bit 7=cpu_speed_en, bit 6=speaker_beep, bit 5=5060_en,
bit 4=drive_nmi_en, bit 3=m1_nmi_en, bit 2=ps2_mode, bits 1:0=psg_mode.

C++ `emulator.cpp:2965-2971` — bits 7,6,5,4,3,1,0 from `cached(0x06)`,
bit 2 from authoritative `nr_06_ps2_mode_` flag. The cached byte has
all bits except bit 2 set unconditionally on write (no gates) so the
sourcing is correct. Match.

### NR 0x81 readback bit composition

VHDL:6126 — bit 7=`i_BUS_ROMCS_n` (constant 1, no expbus), bit 6=
ula_override, bit 5=debounce_disable, bit 4=clken, bit 3=fdc, bit 2='0',
bits 1:0=expbus_speed.

C++ `emulator.cpp:3050-3052` — `0x80 | (nr_81_ & 0x7B)`. Bit mask
0x7B = 0111_1011 keeps bits 6,5,4,3,1,0 and clears bit 2. Bit 7 forced
to 1. Match.

### NR 0xC2 / NR 0xC3 NMI return-address shadow

VHDL:2050-2085 — captured from `cpu_do` at NMIACK_LSB / NMIACK_MSB
cycles when `cpu_wr_n = '0'`.

C++ `Z80Cpu::handle_pending_interrupts` calls `on_nmi_servicing(saved_pc)`
which calls `NextReg::set_nmi_return_address(pc)` to write both bytes.
The C++ does both halves at once (during the NMI dispatch hook) instead
of separately at LSB / MSB cycles, but the end-state matches.

### F9 / F10 hotkey paths

VHDL:6348-6349:
- F9 (`hotkey_m1`) = raw edge, no upstream gate.
- F10 (`hotkey_drive`) = edge AND `port_divmmc_io_en` (NR 0x83 b0).

C++ `Emulator::on_hotkey_f9_mf_nmi` (`emulator.cpp:5012`) — strobes
`NmiSource::strobe_mf_button()` unconditionally; the NR 0x06 b3 gate
is honoured downstream in `nmi_assert_mf()`. Match.
C++ `Emulator::on_hotkey_f10_divmmc_nmi` (`emulator.cpp:5037`) — gates
on `divmmc_.port_io_enable()` BEFORE strobing. Match.

### F4 / F1 reset paths

VHDL:6370-6371:
- F4 → `nr_02_soft_reset` = `(hotkey_soft_reset AND NOT nr_03_config_mode)
  OR (nr_02_we AND nr_wr_dat(0))`.
- F1 → `nr_02_hard_reset` = `hotkey_hard_reset OR (nr_02_we AND
  nr_wr_dat(1))`.

C++ `Emulator::on_hotkey_f4_soft_reset` gates on
`nextreg_.nr_03_config_mode()` early-return. The NR 0x02 b0 path strobes
`nmi_source_.strobe_soft_reset()` BEFORE the host-side `soft_reset()`
(which goes through `init()` and resets `NmiSource` — but `reset_type_`
is intentionally preserved in `NmiSource::reset()` per the source
comment at lines 50-59). Match.

`Emulator::on_hotkey_f1_hard_reset` is unconditional; calls `reset()`.
NR 0x02 b1 same. Hard reset does NOT advance reset_type FSM (correct;
VHDL only advances on `nr_02_soft_reset`).

### NR 0x83 bit 1 (port_multiface_io_en)

VHDL:2415 — `port_multiface_io_en <= internal_port_enable(9);` (bit 9
= NR 0x83 bit 1). VHDL:4289 — multiface entity's `enable_i =
port_multiface_io_en`. VHDL:103 (multiface.vhd) — `reset = reset_i OR
NOT enable_i` — when NR 0x83 b1=0, all four MF FFs held in reset.

C++ `emulator.cpp:1946-1953` — NR 0x83 write handler calls
`multiface_.set_enabled((v & 0x02) != 0);`. Multiface::set_enabled at
`multiface.cpp:52-71` clears all FFs on enabled→disabled edge. Match.

### NR 0xD8 / NR 0xD9 / NR 0xDA reset

VHDL:5107 — `nr_d8_io_trap_fdc_en <= '0';` on reset.
VHDL:3870 — `nr_da_iotrap_cause <= "00";` on reset.
VHDL:3891 — `nr_d9_iotrap_write <= 0x00;` on reset.

C++ `emulator.cpp:138-143` — all three reset to 0 in `init()`. Match.

---

## Convergence assessment

This pass-3 audit found **3 new class-(a) bugs** that both prior passes
missed. The audit therefore did **NOT converge**.

The three bugs all follow the same pattern: an NR readback / event-
capture path that should mirror VHDL gating but in C++ is implemented
unconditionally. None of the existing test corpus exercised the gates
that diverge:

- Bug 1 (NR 0x02 bit 7): no NR 0x02 round-trip test for bit 7.
- Bug 2 (NR 0x0A bits 7:6): no NR 0x0A write-outside-config_mode test
  followed by readback assertion.
- Bug 3 (iotrap in HOLD): no test that triggers an iotrap during an
  in-flight MF NMI.

**Recommended follow-up audits** (pass-4):

1. Boundary tests for NR 0x02 bit 7 round-trip across reset.
2. Boundary tests for NR 0x0A bits 7:6 readback gate.
3. Boundary tests for in-NMI iotrap suppression (FSM in HOLD/END).
4. Class-(b) NR 0x06 reset behaviour debate — consult the user / project
   plan to decide whether to fix B-4 (current C++ resets b3/b4, VHDL
   preserves them); test HK-09-INT would need replanning.
5. Class-(b) NR 0x86-0x89 / NR 0x80 reset gates if expansion-bus
   emulation lands.

The audit's coverage spanned ~20% of the surface area (NMI pipeline +
iotrap + Multiface FSM + key NR readbacks). Other large NR clusters
(palette $40-$44, copper $60-$63, sprite $34-$3F, line int $22/$23,
DMA $C8-$CF int routing) were NOT audited — recommended for future
passes if the operator deems them in scope.

---

## Open questions

- **OQ-1**: Should jnext model the `o_RESET_PERIPHERAL` signal
  (NR 0x02 bit 7) end-to-end? Currently captured (post-fix) but no
  consumer. If/when expansion-bus / ESP / RTC peripheral reset is
  modelled, this latch becomes load-bearing.

- **OQ-2**: Class-(b) finding B-4 (NR 0x06 reset behaviour) is a
  test-vs-VHDL conflict. Strict VHDL-faithful would preserve b3/b4
  across hard reset. Convention preserves them only across soft reset.
  Awaiting user direction.

- **OQ-3**: Should NR 0x82-0x85 reset-type=1 logic on `NextReg::reset()`
  use a member initializer for `regs_[0x85] = 0x8F` to remove the
  fragile "two-reset-call" cold-boot dependency (B-3)?

---

## Test status

- Build: `cmake --build build -j$(nproc)` → 100% built.
- Tests: `ctest --test-dir build --output-on-failure` → **37/37 PASS**
  (post-fix). No regressions.
- Pre-fix: 37/37 PASS (baseline confirmed clean before any edits).

## Branch HEAD after fixes

Branch `task2/verify3-nmi-mf-port`, working tree initially clean at
`7747202`; three fixes applied as a single commit on top.

## Files modified

- `src/core/emulator.h` — added `nr_02_bus_reset_` field +
  `nmi_accept_cause_()` private helper.
- `src/core/emulator.cpp` — NR 0x02 write/read handler (capture +
  surface bit 7), NR 0x0A read handler (use `multiface_.mf_type()`),
  iotrap handlers at 0x2FFD/0x3FFD (gate on `nmi_accept_cause_()`),
  save/load state for `nr_02_bus_reset_`.

No new files created. No tests modified. No worktree push.

---

## Honest output

This is a **non-converged** audit. The pre-existing audit pass-1 +
pass-2 verdicts (which my blind constraint forbids me from reading)
apparently did not exhaustively check every NR readback bit composition
nor every event-capture gate. The three bugs found here are real
spec deviations with concrete VHDL line citations. None of the existing
ctest corpus catches them — they would silently corrupt firmware
state in obscure scenarios (iotrap during NMI handler; firmware
round-trip of `o_RESET_PERIPHERAL`; firmware that probes mf_type via
NR 0x0A readback after writing it outside config_mode).

The audit is **partial**: the 3 bugs found are confined to NR
readback / event-capture gates. Other unaudited surface area
(palette, copper, sprite NRs, etc.) may harbour similar gate-omission
bugs. Recommend a pass-4 audit covering the remaining NR clusters with
the same boundary-input + reset-sweep methodology.
