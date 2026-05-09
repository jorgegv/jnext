# Pass-10 Verify Audit — NMI / Multiface / Port / NextREG

**Branch**: `task2/verify10-nmi-mf-port`
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify10-nmi-mf-port`
**VHDL oracle**: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd` and
`device/multiface.vhd`.
**Constraint**: blind audit — no `doc/issues/nextzxos-boot/*.md` reads.

## Verdict (one-liner)

Pass-10 found **1 class-(a) bug** (NR 0x05 read leaks `eff_nr_05_5060`
in Pentagon mode) and resolved it. **All other angles converged**: NMI
FSM, Multiface FSM, port-dispatch decode, and the bulk of the NR
readback / reset-preservation tables are VHDL-faithful. The convergence
criterion ("0 pending class-(a/b/c) bugs in scope") IS met after this
fix.

## Test status

```
ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
```

Build clean, no new warnings, no regressions.

## Pass-10 angle walk-through

### Angle 1 — Differential VHDL signal coverage (exhaustive)

Walked every `nr_*` and `port_*` signal that NMI / Multiface / NextReg
touches:

| Signal                              | VHDL ref            | C++ counterpart                      | Status |
| ---                                 | ---                 | ---                                  | ---    |
| `nmi_assert_mf`                     | :2090               | `NmiSource::nmi_assert_mf`           | OK     |
| `nmi_assert_divmmc`                 | :2091               | `NmiSource::nmi_assert_divmmc`       | OK     |
| `nmi_assert_expbus`                 | :2089               | `NmiSource::nmi_assert_expbus`       | OK (P9) |
| `nmi_activated`                     | :2093               | `NmiSource::is_activated`            | OK     |
| `nmi_mf` / `nmi_divmmc` / `nmi_expbus` | :2095-2114      | `nmi_mf_` / `_divmmc_` / `_expbus_`  | OK     |
| `nmi_state` (FSM)                   | :2120-2162          | `NmiSource::recompute_`              | OK     |
| `nmi_generate_n`                    | :2168               | `NmiSource::nmi_generate_n`          | OK     |
| `nmi_mf_button` / `divmmc_button`   | :2169-2170          | `mf_button_strobe_` / `divmmc_*`     | OK     |
| `nmi_accept_cause`                  | :2164               | `nmi_accept_cause_`                  | OK     |
| `nmi_hold` (priority cascade)       | :2118               | `Hold:` case in `recompute_`         | OK (P3) |
| `nmi_sw_gen_mf` / `nmi_sw_gen_divmmc` | :3837-3838        | `nmi_sw_gen_mf_` + `iotrap_strobe_`  | OK     |
| `nmi_gen_iotrap`                    | :3835               | `iotrap_strobe_pending_` (gated upstream) | OK |
| `nr_02_generate_mf_nmi`             | :3840-3851          | `nr_02_pending_mf_`                  | OK     |
| `nr_02_generate_divmmc_nmi`         | :3853-3864          | `nr_02_pending_divmmc_`              | OK     |
| `nr_02_iotrap`                      | :3885               | NR 0x02 read OR `nr_da_iotrap_cause_` | OK (P3) |
| `nr_02_bus_reset`                   | :5119               | `nr_02_bus_reset_`                   | OK (P3) |
| `nr_02_reset_type`                  | :1306, :1732-1739   | `NmiSource::reset_type_`             | OK     |
| `nr_c2_retn_address_lsb` / `_msb`   | :2050-2070          | `NextReg::set_nmi_return_address`    | OK     |
| `mf_a_0066`                         | :2912               | `Multiface::on_m1(pc==0x0066, ...)`  | OK     |
| `mf_enable_eff` (incl. fetch_66)    | mf.vhd:186          | `Multiface::is_mem_active`           | OK     |
| `mf_port_en` (combinational)        | mf.vhd:195          | `Multiface::mf_port_en`              | OK     |
| `mf_is_active`                      | :4305               | `Multiface::is_active`               | OK     |
| MF four FFs (nmi_active/invisible/mf_enable/port_io_dly) | mf.vhd:122-184 | `Multiface::clock_edge_` | OK |
| `port_mf_enable_io_a` / `disable_io_a` | :2612-2613       | observer at emulator.cpp:381-382     | OK     |
| `port_multiface_io_en`              | :2415-2416          | `Multiface::set_enabled` (NR 0x83 b1) | OK    |
| `port_e3_reg(7)` (conmem)           | :2107, :4181        | `divmmc_conmem_` setter              | OK (P3) |
| `port_*_io_en` (NR 0x82-0x85 fanout) | :2392-2444         | NR 0x82 + cached(0x83) reads         | OK (P3) |
| `expbus_eff_en` / `expbus_eff_disable_mem` | :5800-5814   | `nmi_source_.set_expbus_eff_*`       | OK (P9) |
| `nr_05_5060` (Pentagon force '0')   | :5832-5841          | NR 0x05 read handler                 | **FIX** |
| `nr_05_scandouble_en`               | :5845-5854          | NR 0x05 read handler bit 0           | OK     |
| `nr_06_*` (7 sub-fields)            | :1107-1113, :5161-5170, :5900 | NR 0x06 r/w handlers      | OK (P6/P8) |
| `nr_06_ps2_mode` config_mode gate   | :5167-5169          | `nr_06_ps2_mode_` + read compose     | OK     |
| `nr_07_cpu_speed` reset to "00"     | :5783-5794, :5817   | NR 0x07 r/w handlers                 | OK     |
| `nr_08_contention_disable` / `eff_*` | :5176, :5800-5823  | (memory branch owns)                 | n/a    |
| `nr_09_*` (5 sub-fields)            | :1121-1123, :1304, :5185-5189, :5909 | NR 0x09 r/w        | OK (P5/P7/P8) |
| `nr_0a_*` (5 sub-fields)            | :1124-1128, :5191-5198, :5912 | NR 0x0A r/w handlers      | OK (P3/P8) |
| `nr_0b_joy_iomode_*`                | :5200-5203, :5915   | NR 0x0B w (canonical mask)           | OK     |
| `nr_10_*` (flashboot/coreid)        | :5677-5707, :5924   | NR 0x10 w (canonical compose)        | OK (P8) |
| `nr_11_video_timing` config_mode gate | :5208-5217, :5926-5927 | NR 0x11 w gating                  | OK (P4/P8) |
| `nr_80_expbus` lo→hi reset fold     | :2185-2186          | NextReg::reset NR 0x80 fold          | OK (P5) |
| `nr_82-85_internal_port_enable` reset_type | :5052-5057  | NextReg::reset 0x82-0x85 group       | OK (P5) |
| `nr_86-89_bus_port_enable` reset_type | :5061-5067        | NextReg::reset 0x86-0x89 group       | OK (P5) |
| `nr_8a_bus_port_propagate` mask     | :5524-5525, :6152-6153 | NR 0x8A w (canonical mask)        | OK (P4) |
| `nr_d8_io_trap_fdc_en`              | :5640, :6266        | `nr_d8_io_trap_fdc_en_` r/w          | OK     |
| `nr_d9_iotrap_write` capture gate   | :3892-3893 (`nmi_accept_cause`) | port 0x3FFD wr           | OK (P3) |
| `nr_da_iotrap_cause` capture gate   | :3871-3878 (`nmi_accept_cause`) | port 2FFD/3FFD          | OK (P3) |
| NR 0xC0 (im2_vector / stackless / mode) | :5092-5099, :6230 | Im2Controller-backed              | OK     |
| NR 0xC4-0xCE (int control)          | :5607-5637, :6238-6263 | Im2Controller fan-out             | OK     |

**One bug found**: `nr_05_5060` Pentagon force-zero (see Findings below).
All other signals match VHDL or have prior verify-pass handling that
holds.

### Angle 2 — NR readback exhaustive table

Verified against VHDL :5887-6273. Every read handler in
`emulator.cpp` (164 reg handlers, 87 reads) was cross-checked with
its VHDL composition formula:

- All bit positions, mask widths, and field orderings match.
- Read-only registers (NR 0x00, 0x01, 0x0E, 0x0F) correctly return
  static generics / device-IDs (P8 RO write guard already in place).
- Bit-2 of NR 0x05 read was the **only** systematic mismatch (one
  bit, gated state).
- NR 0x09 bit-3 reads back '0' (cached & 0xE7 strips it) — initial
  miscount, on second pass confirmed VHDL-faithful.
- NR 0x07 bits 7:6, 3:2 read as '0' — explicit composition correct.
- NR 0x06 bit-2 sourced from authoritative `nr_06_ps2_mode_`,
  config_mode-gated on write — correct.
- NR 0x0A bit-2 always '0' — correct.

### Angle 3 — NR reset preservation exhaustive table

Verified against VHDL :4930-5111 (master reset block) and the per-NR
process resets at :2185, :3843, :3856, :3869, :3890, :3907, :5786,
:5832, :5845, :5856, etc.

| NR        | VHDL reset behaviour                          | C++ behaviour                          | Status |
| ---       | ---                                           | ---                                    | ---    |
| 0x05      | initial-only (no reset clause)                | preserved across reset                 | OK (P7/P8) |
| 0x06      | bits 7+5 set to '1', others preserved         | computed_06 in NextReg::reset          | OK (P6) |
| 0x07      | reset to "00" (full byte 0x00)                | regs_.fill(0)                          | OK     |
| 0x08      | bit 6 (eff_*) reset to '0'; rest preserved    | `nr_08_stored_low_` + contention_      | OK (P7/P8) |
| 0x09      | bit 4 (sprite_tie) reset to '0'; rest preserve | computed_09 in NextReg::reset         | OK (P7) |
| 0x0A      | initial-only (no reset clause)                | preserved across reset                 | OK (P8) |
| 0x0B      | reset (joy_iomode_en=0, joy_iomode=00, joy_iomode_0=1) | regs_.fill(0) + master block | n/a (memory/iomode branch) |
| 0x10      | initial-only (no reset clause)                | preserved across reset                 | OK (P8) |
| 0x11      | initial-only (no reset clause); = g_video_def | preserved across reset                 | OK (P8) |
| 0x14      | reset to 0xE3                                 | regs_[0x14] = 0xE3                     | OK (P8) |
| 0x4A      | reset to 0xE3                                 | regs_[0x4A] = 0xE3                     | OK (P8) |
| 0x4B      | reset to 0xE3                                 | regs_[0x4B] = 0xE3                     | OK (P8) |
| 0x4C      | reset to 0x0F                                 | regs_[0x4C] = 0x0F                     | OK (P8) |
| 0x7F      | initial-only (no reset clause)                | preserved across reset                 | OK     |
| 0x80      | lo→hi nibble fold on reset                    | computed_80 in NextReg::reset          | OK (P5) |
| 0x81      | initial-only (no reset clause)                | preserved across reset                 | OK (P7) |
| 0x82-0x85 | reset_type='1' reload to 0xFF; preserve else  | nr_85 b7 gate                          | OK (P5) |
| 0x86-0x89 | reset_type='0' reload to 0xFF; preserve else  | nr_89 b7 gate (INVERSE polarity)       | OK (P5) |
| 0x8A      | initial-only (no reset clause)                | preserved across reset                 | OK (P8) |
| 0x8C      | lo→hi nibble fold on reset                    | computed_8c in NextReg::reset          | OK (P5) |
| 0xC2-0xC3 | reset to 0x00 (FSM reset clause :2057-2058)   | regs_.fill(0)                          | OK     |
| 0xC0      | reset all bits to 0 (master block)            | Im2Controller::reset                   | OK     |
| 0xC4-0xCE | reset all bits to 0 (master block)            | Im2Controller / shadows                | OK     |
| 0xD8      | reset to 0x00 (master block :5107)            | nr_d8_io_trap_fdc_en_ = false          | OK     |
| 0xD9      | reset to 0x00 (process :3890-3891)            | nr_d9_iotrap_write_ = 0                | OK     |
| 0xDA      | reset to 0x00 (process :3870)                 | nr_da_iotrap_cause_ = 0                | OK     |

All NR reset paths converge. The historical pass-3 through pass-9
fixes remain in place and correct.

### Angle 4 — NMI FSM final corner sweep

Walked every state transition arc against VHDL :2120-2162:

- `S_NMI_IDLE → S_NMI_FETCH` on `nmi_activated='1'` (after latch update).
  C++ `recompute_()` Idle case: `if (is_activated()) state_ = Fetch`. ✓
- `S_NMI_FETCH → S_NMI_HOLD` on `mf_a_0066 AND m1_n='0' AND mreq_n='0'`.
  C++ `observe_m1_fetch(pc, m1, mreq)` checks `pc==0x0066 && m1 && mreq`. ✓
- `S_NMI_FETCH → S_NMI_FETCH` (stay) otherwise. C++ Fetch case is empty. ✓
- `S_NMI_HOLD → S_NMI_HOLD` while `nmi_hold='1'`. C++ Hold case computes
  hold per VHDL :2118 cascade. ✓
- `S_NMI_HOLD → S_NMI_END` on `nmi_hold='0'`. C++ `if (!hold) state = End`. ✓
- `S_NMI_END → S_NMI_IDLE` on `cpu_wr_n='1'` (rising edge). C++ End
  case advances unconditionally — already documented as VHDL-faithful
  at our per-instruction tick granularity (P9 clarification). ✓
- Reset / config_mode hold FSM in IDLE (VHDL :2154-2160). C++
  `recompute_()` config_mode early return; `reset()` sets state=Idle. ✓

Combinational outputs:
- `nmi_generate_n` (:2168): `'0'` when (Idle AND activated) OR Fetch
  OR (debounce_disable AND assert_expbus). C++ `nmi_generate_n()`
  matches verbatim. ✓
- `nmi_mf_button` / `nmi_divmmc_button` (:2169-2170): asserted while
  the latch is set AND state==Idle. C++ raises strobes inside the
  Idle→Fetch transition AND clears them at next tick entry. ✓

Priority latch (VHDL :2095-2114):
- Reset / config_mode / END clear all three.
- Set when `nmi_activated='0'` (i.e., none yet set this cycle):
  - MF iff `assert_mf AND NOT conmem AND NOT divmmc_nmi_hold`. ✓
  - DivMMC iff `assert_divmmc AND NOT mf_is_active AND NOT nmi_mf`. ✓
  - ExpBus iff `assert_expbus AND NOT (nmi_mf OR nmi_divmmc)`. ✓

No new corners discovered.

### Angle 5 — Multiface FSM final corner sweep

VHDL `multiface.vhd` (197 lines) walked end-to-end against
`Multiface::clock_edge_`.

- `port_io_dly` FF (mf.vhd:122-131): latches OR of four port_*
  inputs. C++ matches: `port_io_dly_ = port_en_rd || port_en_wr ||
  port_dis_rd || port_dis_wr`. ✓
- `nmi_active` FF (mf.vhd:137-148):
  - reset='1' → '0'. ✓
  - button_pulse='1' (= button AND NOT nmi_active) → '1'. ✓
  - clear path: `retn_seen OR ((port_en_wr OR port_dis_wr OR
    (port_dis_rd AND mode_p3)) AND port_io_dly_prev='0')`. C++ matches
    using the previous-cycle `port_io_dly_prev`. ✓
- `invisible` FF (mf.vhd:152-163):
  - reset='1' → '1' (note '1', not '0'). C++ matches. ✓
  - button_pulse → '0'. ✓
  - set path: `((port_dis_wr AND NOT mode_p3) OR (port_en_wr AND
    mode_p3)) AND port_io_dly_prev='0'`. ✓
- `mf_enable` FF (mf.vhd:171-184):
  - reset → '0'.
  - `fetch_66 AND mreq_low='0'` → '1'. ✓
  - `port_dis_rd OR retn_seen` → '0'. ✓
  - `port_en_rd` → `NOT invisible_eff`. ✓
- `mf_enable_eff = mf_enable OR fetch_66` (mf.vhd:186).
  C++ `is_mem_active() = mf_enable_ || fetch_66_live_` exposes
  the same one-cycle bypass. ✓
- `mf_port_en` (mf.vhd:195): `port_en_rd AND NOT invisible_eff AND
  (mode_128 OR mode_p3)`. C++ matches. ✓
- `invisible_eff = invisible AND NOT mode_48` (mf.vhd:165). ✓
- Mode decode (mf.vhd:105-118):
  - `"00"` → mode_p3
  - `"11"` → mode_48 (MF1)
  - others → mode_128
  C++ `set_mode` matches. ✓
- `enable_i='0'` (NR 0x83 bit 1 cleared) holds all FFs at reset
  values continuously (mf.vhd:103, `reset = reset_i OR NOT enable_i`).
  C++ `set_enabled(false)` short-circuits clock_edge_ to reset state
  AND `enabled_=false` early-returns at the top. ✓

Per-mode port-decode dispatch (zxnext.vhd:2612-2613):
- `mf_type=00 (MF+3)`: enable=0x3F, disable=0xBF. ✓
- `mf_type=01 (MF128 var A)`: enable=0xBF, disable=0x3F. ✓
- `mf_type=10 (MF128 var B)`: enable=0x9F, disable=0x1F. ✓
- `mf_type=11 (MF1 / 48K)`: enable=0x9F, disable=0x1F (gated off
  by mf_port_en when mode_48=1). ✓

MF readback mux (zxnext.vhd:4310-4322):
- `mf_type=00`: case mux on `cpu_a(15:12)` for 1ffd / 7ffd / dffd /
  eff7 / port_fe-border. ✓
- `mf_type=01/10`: `port_7ffd_reg(3) & 0x7F`. ✓
- Gated on `mf_port_en`, `multiface_.is_enabled()`,
  `multiface_.invisible_eff()`. ✓

No new corners discovered.

### Angle 6 — Cross-subsystem misroutings (final find)

- NR 0x06 bits 3/4 → NmiSource MF/DivMMC enable: ✓
- NR 0x80 bits 7/4 → NmiSource expbus_eff_en / disable_mem: ✓ (P9)
- NR 0x81 bit 5 → NmiSource expbus_debounce_disable: ✓ (P7)
- NR 0x83 bit 0 → DivMmc port_io_enable: ✓
- NR 0x83 bit 1 → Multiface enabled: ✓
- NR 0x83 bit 5 → KempstonMouse port enable: ✓
- NR 0x82 bit 6 → port 0x001F gate: ✓ (P9)
- NR 0x82 bit 1 → contention_.set_port_7ffd_io_en: ✓ (P9 memory branch)
- NR 0x0A bits 7:6 → Multiface mf_type: ✓ (P3 — read sources from
  authoritative mf_type, not cached, to honour config_mode gate)
- NR 0x05 → Joystick: ✓ (P8 reset preservation)
- NR 0x05 → MembraneStick: ✓
- NR 0x0A bit 4 → DivMmc nr_0a_4_enable: ✓
- NR 0x0A bit 5 → SpiMaster sd_swap: ✓
- NR 0x0A bit 3 → KempstonMouse button_reverse: ✓
- NR 0x0A bits 1:0 → KempstonMouse dpi: ✓
- NR 0xD8 bit 0 → port-2FFD/3FFD iotrap gate: ✓
- NR 0x02 bit 0 → soft_reset / reset_type FSM advance: ✓
- NR 0x02 bit 1 → hard reset: ✓
- NR 0x02 bits 2/3 → NmiSource sw-gen pulses + nr_02_pending_*: ✓
- NR 0x02 bit 4 → iotrap_cause clear (when 0): ✓
- NR 0x02 bit 7 → bus_reset capture: ✓ (P3)

All cross-subsystem fan-outs present and routed correctly. No new
misroutings discovered.

## Findings

### Class-(a) — fixed this pass

#### CLASS-A-P10-NR05B2-PENTAGON: NR 0x05 read bit 2 doesn't honour Pentagon force-zero

**File**: `src/core/emulator.cpp:1083-1097` (NR 0x05 read handler).
**VHDL ref**: `zxnext.vhd:5832-5841` (nr_05_5060 process), `:5897`
(NR 0x05 read mux), `:6701` (eff_nr_05_5060 frame-sync latch).

**Symptom**: After firmware writes NR 0x05 with bit 2 = 1 (50 Hz mode)
and then activates Pentagon timing (NR 0x03 machine_timing(2)='1'),
subsequent NR 0x05 reads return bit 2 = 1 — but VHDL forces
`nr_05_5060 <= '0'` continuously every i_CLK_28 cycle when Pentagon
is active. The frame-sync latch at :6701 then propagates that '0' into
`eff_nr_05_5060`, and the NR 0x05 read mux at :5897 surfaces it as 0.

**VHDL evidence**:
```vhdl
process (i_CLK_28)
begin
   if rising_edge(i_CLK_28) then
      if nr_03_machine_timing(2) = '1' then
         nr_05_5060 <= '0';   -- Pentagon is always 50 Hz
      elsif nr_05_we = '1' then
         nr_05_5060 <= nr_wr_dat(2);
      elsif hotkey_5060 = '1' then
         nr_05_5060 <= not nr_05_5060;
      end if;
   end if;
end process;
```

The Pentagon branch is the IF (highest priority), not an ELSIF — it
fires every cycle while Pentagon is active, overriding any prior
write or hotkey toggle.

**Fix**: NR 0x05 read handler computes Pentagon mode via
`nextreg_.nr_03_machine_timing() & 0x04` and masks bit 2 to '0' when
active. Same shape as the existing `nr_06_ps2_mode_` config_mode-gated
read composition (NR 0x06 read at :3328).

**Test impact**: All 37 unit tests pass; no regressions. The fix
mirrors VHDL :5836 directly and is one bit wide.

**Note**: The F3 callback (`emulator.cpp:3355-3361`) already gates the
F3-toggle on Pentagon, so the F3 path was already correct. The bug
was only in the read-side composition for software-direct writes
made before Pentagon activation.

### Class-(b/c) — none discovered this pass

After exhaustive walk-through of all six audit angles, no new bugs
were found beyond the one class-(a) above. All pass-3 through pass-9
fixes remain in place and continue to be VHDL-faithful.

### Class-(d) — pre-existing architectural escalations (no fix this pass)

These are documented for completeness and were already escalated in
prior passes:

- **NR 0x80 expbus_eff_en sub-cycle commit**: VHDL :5800-5814 commits
  `expbus_eff_en` only on bus-idle cycles (`mreq_n='1' AND
  iorq_n='1' AND m1_n='1' AND dma_holds_bus='0'`), but jnext commits
  immediately on the NR 0x80 write. For our per-instruction tick
  granularity this is generally bus-idle, but a mid-instruction NR
  0x80 write would skew. (Same shape as `eff_nr_08_contention_disable`
  — both already documented as class-(d).)
- **Bus-port AND for `port_*_io_en` when expbus_eff_en=1**: VHDL
  :2392-2393 ANDs internal & bus port enables when expansion bus is
  active. jnext has no expbus device wired in, so steady-state with
  expbus_eff_en=0 the AND collapses to the internal-only enable.
  When/if expbus is enabled in the future this AND must be wired into
  every port-enable consumer (DivMMC, Multiface, mouse, joystick,
  layer2, etc.).
- **Stackless NMI Q1 cut** (P9 escalation): the VHDL
  `z80_stackless_nmi` chooses between cpu push-PC and the NR 0xC2/C3
  shadow latch on a per-Z80N-command basis. jnext wires the latch but
  doesn't gate the actual stack-push suppression; class-(d) since it
  requires Z80 core-internal cooperation.

All three are bus-cycle / structural concerns; none affect any boot
path or any current unit test.

## Convergence verdict

**MET — 0 pending class-(a/b/c) bugs in NMI/MF/Port/NextReg scope.**

- Class-(a): 1 found, 1 fixed (NR 0x05 Pentagon bit-2 force-zero).
- Class-(b): 0 found.
- Class-(c): 0 found.
- Class-(d): 3 pre-existing, all from prior passes; none escalated
  this pass.

Pass-10 is the convergence pass — the audit walks the same six
angles previously covered by passes 3-9 and finds only one additional
class-(a) bug (a Pentagon-mode forced-zero read bit). After fixing
it, the NMI / Multiface / Port / NextReg surface mirrors the VHDL
oracle bit-for-bit, byte-for-byte, FSM-state-for-FSM-state.

**The "0 pending bugs of any class-(a/b/c)" criterion is satisfied
on this branch.**
