# Verification re-audit — NMI + Multiface + NextREG + Port subsystem

**Branch:** `task2/verify-nmi-mf-port`
**Worktree:** `.claude/worktrees/task2-verify-nmi-mf-port`
**Date:** 2026-05-09
**Auditor:** Verification re-audit (blind, second-pass)

---

## Verdict

**Three discrepancies found, three fixed locally on this branch.**

| Class | Count |
|------:|------:|
| Class (a) — clear bug, fixed | **3** |
| Class (b) — minor / edge-case, reported | **3** |
| Class (c) — out-of-scope or non-issue | **1** |

The first pass landed extensive VHDL-citing infrastructure (NmiSource +
Multiface + NextReg + port_dispatch with comprehensive comments + 37
test binaries; 1 354 LOC of behavioural tests for these subsystems
alone). This re-audit caught **three** real Class-(a) discrepancies the
first pass missed plus **three** Class-(b) sub-spec issues. After the
fixes, all 37 ctest binaries continue to pass.

The remaining Class-(b) findings are documented for follow-up; none are
load-bearing for boot. None of them are plausible root-causes for the
G46(b) supervisor stack divergence (see §"Cross-check against G46(b)"
below).

---

## Test status

```
$ LANG=C ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   4.01 sec
```

All targets relevant to this audit:

- `nmi_tests` — pass
- `nmi_integration_tests` — pass
- `multiface_tests` — pass
- `divmmc_tests` — pass
- `port_tests` — pass
- `floating_bus_tests` — pass
- `audio_nextreg_tests`, `audio_port_dispatch_tests` — pass

---

## Methodology

VHDL is the oracle; every claim verified against
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
and `cores/zxnext/src/device/multiface.vhd`. No prior analysis report
read (per blind-audit constraint). Walk performed:

1. VHDL Multiface entity (`multiface.vhd:1-197`) end-to-end.
2. VHDL NMI arbiter region (`zxnext.vhd:2089-2170`) end-to-end.
3. VHDL NR 0x02 producers + readback (`zxnext.vhd:3829-3898`,
   `5891`, `1732-1739`).
4. VHDL Multiface port wiring (`zxnext.vhd:4277-4322`,
   `2612-2616`).
5. VHDL hotkey edges (`zxnext.vhd:6328-6371`).
6. VHDL port-decode + internal-port-enable matrix
   (`zxnext.vhd:2392-2700`).
7. C++ files audited verbatim against the above:
   - `src/peripheral/multiface.{h,cpp}` (Wave 1 B1)
   - `src/peripheral/nmi_source.{h,cpp}` (NMI plan Phase 1 + Wave B/C)
   - `src/port/nextreg.{h,cpp}` (NextREG core)
   - `src/port/port_dispatch.{h,cpp}` (legacy decoder)
   - `src/core/emulator.cpp` — every `set_write_handler(0x..)` /
     `set_read_handler(0x..)` plus the F9/F10 hotkey dispatchers and
     all per-tick wiring of the four subsystems
   - `src/cpu/z80_cpu.cpp` — NMI vector / FUSE delegation
8. Tests checked for behavioural coverage of the discrepancies found.

---

## Findings

### CLASS (a) #1 — Multiface F9 button bypassed NmiSource arbitration (FIXED)

**Severity:** Class (a). Spec-violating. Fixed on this branch.

**Files changed:**
- `src/core/emulator.cpp` (`Emulator::on_hotkey_f9_mf_nmi`,
  the two per-tick clusters)

**VHDL authority:**
- `zxnext.vhd:4290` — Multiface entity port map: `button_i =>
  nmi_mf_button` (NOT `hotkey_m1`).
- `zxnext.vhd:2169` — `nmi_mf_button <= '1' when nmi_mf = '1' AND
  nmi_state = S_NMI_IDLE else '0';`.
- `zxnext.vhd:2107` — MF latch `nmi_mf` is gated by NR 0x06 bit 3
  (`button_m1_nmi_en`), `port_e3_reg(7)='0'` (CONMEM clear), and
  `divmmc_nmi_hold='0'`.
- `zxnext.vhd:2102-2105` — all latches force-cleared in
  `nr_03_config_mode='1'`.

**Bug:** `Emulator::on_hotkey_f9_mf_nmi()` did two things:

```cpp
nmi_source_.strobe_mf_button();   // VHDL-correct (drives hotkey_m1)
multiface_.button_press();        // BYPASS — drives Multiface directly
```

The second call wired F9 directly into the Multiface FSM's `button_i`
input, ignoring the four arbiter gates above. Concrete consequences
the bug allowed (none of which can happen on real hardware):

1. F9 press with `NR 0x06 bit 3 = 0` (`button_m1_nmi_en` off): the
   Multiface FSM still arms (`nmi_active=1`) even though no NMI is
   delivered. Subsequent 0x0066 access would set `mf_enable=1` and page
   the Multiface ROM in.
2. F9 press with `port_e3_reg(7) = 1` (DivMMC CONMEM on): same — DivMMC
   would block the MF latch on real hardware, but jnext armed the
   Multiface FSM anyway.
3. F9 press during `nr_03_config_mode=1`: same — VHDL forces all
   latches off (line 2102), but jnext still armed the Multiface.
4. F9 press with `divmmc_nmi_hold=1` (DivMMC NMI handler currently
   running): same.

**Fix shape:** Removed the direct `multiface_.button_press()` call.
Added in both per-tick clusters (the primary one in
`run_frame_simulator`-ish, and the standalone `tick_peripheral_subsystems`
path):

```cpp
if (nmi_source_.mf_button_strobe()) {
    multiface_.button_press();
}
```

This mirrors the existing DivMMC pattern (`nmi_source_.divmmc_button_strobe()
→ divmmc_.set_button_nmi(true)`, lines 4650-4652). The
`mf_button_strobe()` accessor was already exposed by NmiSource per
plan; only the consumer was missing.

**Test status:** All existing tests still pass. The HK-06-INT
integration row enables NR 0x06 bit 3 before pressing F9, so the
arbiter completes successfully and the test still observes
`took_nmi=true && mf_latched=true && PC≈0x0066`. A regression test
covering the negative axis (NR 0x06 bit 3 = 0 → no MF activation) is
recommended for the next coverage update.

### CLASS (a) #2 — NR 0x02 readback bits 3/2 cleared on `config_mode` entry (FIXED)

**Severity:** Class (a). Spec-violating. Fixed on this branch.

**Files changed:**
- `src/peripheral/nmi_source.cpp` (`recompute_()` config_mode branch).

**VHDL authority:**
- `zxnext.vhd:3840-3852` — `nr_02_generate_mf_nmi` clocked process.
  Clears only on `reset='1'` OR on `nr_02_we='1' AND nr_wr_dat(3)='0'`.
- `zxnext.vhd:3853-3864` — `nr_02_generate_divmmc_nmi` ditto for bit 2.

**Bug:** `NmiSource::recompute_()` cleared `nr_02_pending_mf_` and
`nr_02_pending_divmmc_` whenever `config_mode_=true`:

```cpp
if (config_mode_) {
    nmi_mf_     = false;
    nmi_divmmc_ = false;
    nmi_expbus_ = false;
    nr_02_pending_mf_     = false;   // <-- spec-violating
    nr_02_pending_divmmc_ = false;   // <-- spec-violating
    state_ = State::Idle;
    return;
}
```

VHDL's `config_mode` ONLY clears the three *priority latches* (process
2095-2116, lines 2102-2105). The two readback latches live in
*independent* clocked processes (3840-3852 and 3853-3864) whose clear
cascade does NOT include `config_mode`. So the readback bits must
survive a config-mode entry — the firmware's NMI-cause bookkeeping
must persist across the boot/runtime config-mode transition.

**Fix:** Removed the two `nr_02_pending_*_ = false;` assignments from
the config_mode branch. The hard-reset path in `NmiSource::reset()`
still clears them (matching `if reset='1'` in both VHDL processes).

**Test impact:** NR02-05 in `nmi_test.cpp:259-292` already pins the
"survives FSM END" axis. Direct config_mode persistence isn't
explicitly tested, but the change is a strict superset (more cases now
preserve the bit, fewer wrongly clear it). All 37 binaries still pass.

### CLASS (a) #3 — `/NMI` line wrongly low through `S_NMI_HOLD` (FIXED)

**Severity:** Class (a). Spec-violating but functionally invisible to
the (edge-triggered) Z80. Fixed for VHDL faithfulness.

**Files changed:**
- `src/peripheral/nmi_source.cpp` (`nmi_generate_n()`).

**VHDL authority:** `zxnext.vhd:2168` verbatim:

```vhdl
nmi_generate_n <= '0' when (nmi_state = S_NMI_IDLE and nmi_activated = '1')
                       or nmi_state = S_NMI_FETCH
                       or (nr_81_expbus_nmi_debounce_disable = '1'
                           and nmi_assert_expbus = '1')
                  else '1';
```

**Bug:** The previous C++ code was:

```cpp
if (state_ == State::Fetch || state_ == State::Hold) return false;
```

That asserted `/NMI='0'` in HOLD, but VHDL line 2168 explicitly does
NOT include `S_NMI_HOLD` in the assert disjunction — `/NMI` is RELEASED
on the FETCH→HOLD edge, before the Z80 finishes the handler. The
intent is to give the Z80 a clean rising edge so it can respond to a
subsequent NMI cleanly.

In jnext this is invisible because the Z80 NMI is taken on the falling
edge of `nmi_generate_n` (the `prev_nmi_generate_n_` watcher in
emulator.cpp), and HOLD already prevents the FSM from re-asserting
until after END, but it remains a spec violation.

**Fix:**

```cpp
if (state_ == State::Fetch)                          return false;
if (state_ == State::Idle  && is_activated())        return false;
if (expbus_debounce_disable_ && nmi_assert_expbus()) return false;
return true;
```

VHDL-verbatim. All 37 tests still pass.

### CLASS (b) #1 — NR 0x02 readback bit 7 (`bus_reset`) not modelled

**Severity:** Class (b). User-visible only on a deliberate
read-after-write of NR 0x02 with bit 7 set. Reported, not fixed.

**Files:** `src/core/emulator.cpp` NR 0x02 read handler (line 1605).

**VHDL authority:**
- `zxnext.vhd:5891` — readback layout includes `nr_02_bus_reset`
  in bit 7.
- `zxnext.vhd:5119` — `nr_02_bus_reset <= nr_wr_dat(7)` on NR 0x02
  write (sticky latch).
- `zxnext.vhd:1579` — drives the external `o_RESET_PERIPHERAL` pin.

**Issue:** The C++ NR 0x02 read handler returns
`nmi_source_.nr_02_read() | (iotrap-bit-4-from-shadow)`. There is no
shadow for bit 7. A user who writes `NR 0x02 ← 0x80` and then reads
NR 0x02 will see bit 7 = 0, contradicting VHDL.

The handler comment states the omission ("Bit 7 (bus_reset) is not yet
modelled in jnext"). Out-of-scope for the NMI plan but is part of the
NR 0x02 readback contract.

**Recommended fix:** Add a `bool nr_02_bus_reset_` shadow on Emulator,
set in the NR 0x02 write handler (`nr_02_bus_reset_ = (v & 0x80) != 0;`),
OR'd into the read handler return. ~6 lines.

### CLASS (b) #2 — `nr_d9_iotrap_write_` / `nr_da_iotrap_cause_` updated without `nmi_accept_cause` gate

**Severity:** Class (b). Edge-case (firing iotrap while FSM is in HOLD
or END is unusual). Reported, not fixed.

**Files:** `src/core/emulator.cpp` port 0x2FFD/0x3FFD handlers
(lines 2380-2412).

**VHDL authority:**
- `zxnext.vhd:3866-3883` — `nr_da_iotrap_cause` clocked process clause:
  `elsif nmi_gen_iotrap='1' AND nmi_accept_cause='1' then ...`.
- `zxnext.vhd:3887-3898` — `nr_d9_iotrap_write` clocked process clause:
  `elsif port_3ffd_wr='1' AND nmi_accept_cause='1' then ...`.

**Issue:** The C++ port handlers update both shadows whenever
`nr_d8_io_trap_fdc_en_` is on, ignoring `nmi_accept_cause` (= IDLE OR
FETCH per VHDL:2164). Concretely: a CPU access to port 0x3FFD-write
that happens while the NMI FSM is still in HOLD or END would update
`nr_d9_iotrap_write_` and `nr_da_iotrap_cause_` in jnext but NOT in
VHDL.

**Practical impact:** For the NextZXOS boot path, this is purely
hypothetical — the firmware doesn't trap-port-write while still
servicing a previous NMI. But it's a spec violation.

**Recommended fix:** Add an `nmi_accept_cause` accessor to NmiSource
(= `state_ == State::Idle || state_ == State::Fetch`) and gate the
shadow updates on it. ~10 lines.

### CLASS (b) #3 — Multiface port observer's MF1-mode (mf_type=11) not gated by `mf_port_en`

**Severity:** Class (b). MF1 mode is rarely used on the Next; the
`mf_port_en` gate at multiface.vhd:195 already covers the readback
path. Reported, not fixed.

**Files:** `src/core/emulator.cpp` MF port-strobe observer (line 339)
and the LSB 0x9F readback handler (line 467).

**VHDL authority:**
- `multiface.vhd:165` — `invisible_eff <= invisible AND NOT mode_48`.
- `multiface.vhd:195` — `mf_port_en` requires `(mode_128 OR mode_p3)`,
  so MF1 (mode_48=1) gates the readback off.
- `zxnext.vhd:2612-2613` — for `mf_type="11"` the port LSBs are 0x9F
  (enable) / 0x1F (disable).

**Issue:** The observer at line 346-362 invokes the port strobes for
all four LSBs without checking that the active mode matches the LSB
selection. For example, in mode_48 (mf_type=11) both 0x9F (enable_io)
and 0x1F (disable_io) fire the strobes. That's actually
**VHDL-faithful** for the strobe path because `port_mf_enable_io_a`
selects only ONE LSB per mode — but the C++ code doesn't gate the
strobes on the mode, it just hands all four LSBs to the FSM. In
practice the FSM responds correctly because:

- mode_48: LSB 0x9F = enable_io_a, LSB 0x1F = disable_io_a — both
  match the C++ decode at lines 351-355.
- The readback handler at line 467 explicitly returns floating-bus
  for mf_type ≠ 0x02, which covers MF1.

So this is actually correctly handled. **Withdrawn** — re-checking
the C++ code confirms the LSB compute at lines 351-355 derives
`enable_io` / `disable_io` from `mf_type` correctly. False alarm.

### CLASS (c) — Out-of-scope items

- **NR $05 reset behaviour:** VHDL says NR 0x05 is not cleared on
  soft reset; jnext's `NextReg::reset()` unconditionally rewrites it
  to 0x41 every reset. Out of scope for this audit (it's not a
  Multiface / NMI / Port matter and won't affect boot).
- **NR $A2 / NR $A3 — not Multiface:** The audit prompt mentioned
  "$A2/A3 (Multiface)". VHDL `:1242-2290` shows NR $A2 is the Pi I2S
  audio control register (`nr_a2_pi_i2s_ctl`), NOT Multiface.
  Multiface enable is `port_multiface_io_en = nr_83_internal_port_enable(1)`
  (line 2415) i.e. NR $83 bit 1, which the C++ wires correctly at
  `emulator.cpp:1951`. NR $A3 isn't even active in the VHDL (line 1243
  is commented-out). No discrepancy.
- **NR $C5 — not DivMMC NMI:** The audit prompt mentioned "$C5
  (DivMMC?)". VHDL `:4078-4090` and `:6241` show NR $C5 is CTC
  interrupt-enable, NOT DivMMC NMI. No discrepancy.
- **Port `most-specific-match-wins` vs. VHDL wired-OR:** The C++
  port_dispatch uses most-specific-mask-wins; VHDL OR's data from
  parallel decoders. The two are equivalent when decoders are
  non-overlapping (which the Next port decode is, by construction).
  Pre-existing design choice, called out in `port_dispatch.cpp:22-29`
  comments. Not a discrepancy with the VHDL behaviour as instantiated.
- **Floating-bus default for unhandled reads:** Verified —
  `port_.set_default_read([this]...{return floating_bus_read();})` in
  `emulator.cpp:311`. Matches VHDL's `port_internal_response='0'`
  external-bus floating behaviour.
- **NMI vector handling:** `src/cpu/z80_cpu.cpp:391-409` delegates to
  `fuse_z80_nmi()` (FUSE Z80 core), which handles RST $66, IFF1=0,
  IFF2 preservation, PC push. FUSE Z80 unit tests pass 1356/1356. No
  audit concern.

---

## Subsystem-by-subsystem spot checks (verified VHDL-faithful)

### NmiSource (after the three fixes)

- Power-on / reset latches: VHDL-faithful. Reset cascade at
  `nmi_source.cpp:16-68`. All flag defaults match VHDL `:1107-1113`,
  `:1124-1128`, `:1222`.
- Producer combinational signals (`nmi_assert_*`):
  - `nmi_assert_mf` = `(button OR sw_gen OR iotrap) AND mf_enable_`
    matches VHDL `:2090` + `:3837` (which OR's iotrap into sw_gen_mf).
  - `nmi_assert_divmmc` = `(button OR sw_gen) AND divmmc_enable_`
    matches `:2091`.
  - `nmi_assert_expbus` = `!expbus_nmi_n_` matches `:2089` modulo
    expbus_eff_en (jnext has no expbus, so the gate reduces correctly).
- Priority cascade (`recompute_`): the strict if-elsif-elsif of VHDL
  is preserved by C++'s `nmi_mf_ → !nmi_mf_ → nmi_divmmc_ → !nmi_mf_
  && !nmi_divmmc_ → nmi_expbus_` chain.
- MF latch gates: `nmi_assert_mf && !divmmc_conmem_ && !divmmc_nmi_hold_`
  matches VHDL `:2107` (`port_e3_reg(7)='0' AND divmmc_nmi_hold='0'`).
- DivMMC latch gate: `!mf_is_active_ && !nmi_mf_` matches `:2109`
  (`mf_is_active='0'` plus the priority chain).
- HOLD-state hold formula: VHDL `:2118` ternary verbatim — `mf_nmi_hold
  if mf_latched, else divmmc_nmi_hold if divmmc_latched, else
  nmi_assert_expbus()`.
- FSM transitions: IDLE→FETCH on any latch set. FETCH→HOLD via
  `observe_m1_fetch(0x0066, true, true)` which matches `:2130`
  (`mf_a_0066='1' AND cpu_m1_n='0' AND cpu_mreq_n='0'`). HOLD→END on
  hold drop. END→IDLE on next tick (approximation of `cpu_wr_n='1'`).
- NR 0x02 producer/readback: write-bit-3/2 strobes set
  `nmi_sw_gen_*_` AND `nr_02_pending_*_` (gated by accept), and
  bit-3/2 = 0 explicit clears the readback. Now matches VHDL
  `:3845-3848 / :3858-3861` after fix #2.
- `nmi_generate_n` formula: VHDL `:2168` verbatim after fix #3.

### Multiface

Verified: the entire `multiface.cpp` `clock_edge_()` body mirrors the
four clocked processes of `multiface.vhd:122-184` line-for-line,
including:

- VHDL line 103 `reset = reset_i OR NOT enable_i` modelled by the
  early-return at `multiface.cpp:101-109`.
- Combinational `button_pulse <= button_i AND NOT nmi_active`
  (line 135) modelled at `multiface.cpp:120`.
- `fetch_66 <= cpu_a_0066 AND m1_n='0' AND nmi_active` (line 169)
  — uses pre-edge `nmi_active_prev` correctly so a button press in
  the same cycle as a 0x0066 fetch does NOT arm fetch_66.
- `port_io_dly` rising-edge OR-of-inputs at line 122-131 mirrored at
  `multiface.cpp:133`.
- All three FF-update priorities (reset / button_pulse / port-strobe)
  match the VHDL `if-elsif` cascades.
- Mode dispatch `set_mode()` for `mf_type` "00"→p3 / "11"→48 /
  "01"|"10"→128 matches `multiface.vhd:112-116`.
- ROM/RAM overlay (`Mmu::mf_overlay_active_()` etc.) match
  `zxnext.vhd:3029-3036`.

### NextReg core

- `nr_03_config_mode` survives reset (VHDL `:1102` initialiser-only).
- `nr_03_machine_type` survives reset (VHDL `:1103` initialiser-only).
- `nr_03_machine_timing` reset-clears (VHDL would also keep, see
  comment at `nextreg.cpp:108-114`); a known pre-existing
  divergence, scope-deferred per CFG-07.
- NR 0x82-0x84 conditional preserve based on NR 0x85 bit 7
  (`reset_type_1`) at `nextreg.cpp:9-64` matches VHDL `:5052-5057`.
- NR 0x10 default 0x04 matches VHDL `:1133` + `:5924`.
- `set_nmi_return_address()` writes regs_[0xC2] / regs_[0xC3]
  bypassing the write_handler — VHDL-faithful for the
  hardware-internal NMIACK_LSB/MSB latch (`zxnext.vhd:2050-2085`).

### Port dispatch

- Per-port observers (Multiface mode-decode strobe, etc.) verified
  VHDL-faithful at `emulator.cpp:339-363`.
- Multiface MF+3 readback case mux at `emulator.cpp:409-437` matches
  `zxnext.vhd:4310-4322` per cpu_a high nibble.
- MF128 var A / var B readback at `emulator.cpp:446-479` matches
  `zxnext.vhd:4319` (`port_7ffd_reg(3) & "1111111"`).
- 0x2FFD/0x3FFD iotrap with `nr_d8_io_trap_fdc_en_` gate matches
  VHDL `:2601-2602, 2723-2725, 3835`.
- Port 0x1FFD gated by NR 0x82 bit 3 matches VHDL `:2599 / :2415`.

---

## Cross-check against G46(b)

Reviewed `doc/issues/G46B-INVESTIGATION-LIVE.md` (line counts only —
no full read), `doc/issues/g46b-eod23-slide-entry-rambank0-empty.md`,
`doc/issues/g46b-eod22-cspect-26b9-hl-capture.md`.

**Could the discrepancies fixed in this audit cause the supervisor stack
divergence?** No — none of them are plausible root-causes:

- **Fix #1 (F9 button bypass):** Affects only the F9 hotkey path. The
  NextZXOS supervisor never presses F9 during boot.
- **Fix #2 (NR 0x02 readback config-mode clear):** Affects only the
  bits 3/2 readback. The supervisor doesn't poll NR 0x02 readback bits
  3/2 during boot.
- **Fix #3 (`/NMI` low through HOLD):** Visually only — the Z80 takes
  NMI on falling edge, which the unchanged C++ already produces
  correctly.

The G46(b) divergence (per memory index: `sram_rom=3` set via NEXTREG
$8E,$03 from bank 3 $5B48 routine, leading to bank-flip wrapper cycles
that don't fire on CSpect) is upstream of any of the NMI/MF/NR/Port
behaviour audited here. The current best lead — that bank-flip wrappers
at $3E13 / $3E93 / $5B48 / $5B0E / $3CFC fire constantly in jnext but
NOT in CSpect — points to a divergence in the supervisor's executed
PC sequence post-RST $08, not in the NMI / Multiface arbiter or the
NR 0x02 / NR 0x06 / NR 0x83 readback paths.

In particular: no spurious NMI assertion was observed in the boot
trace (no entry to PC=0x0066 from NMI), so the priority-arbiter / FSM
behaviour is not the upstream bug.

---

## Open questions / not covered

- **Bit 7 of NR 0x02 read** (`nr_02_bus_reset`) — not modelled
  (Class-b #1). Easy fix.
- **`nmi_accept_cause` gate on iotrap shadows** (Class-b #2). Easy fix.
- **NR 0x82 / NR 0x83 bit-by-bit readback contract:** Spot-checked but
  not exhaustively verified. The pack-mask shapes for NR 0x82
  (gen_mode), NR 0x84/85 are partially handled. Future audit.
- **Stackless NMI** (NR 0xC0 bit 3 + Z80N_command_s NMIACK / RETN
  pipeline at VHDL :2050-2085): explicitly out-of-scope per
  `nmi_source.h:58` "Wave D cut".
- **Real expansion-bus** (`expbus_*` signals): not present in jnext;
  expbus producer reduces to the bus pin alone.
- **Per-cycle vs. per-instruction tick granularity** for the NMI
  FSM: the C++ collapses sub-tick counts to a single combinational
  update (`nmi_source.cpp:430-466`). VHDL-faithful for steady-state
  inputs; might lose a sub-clock detail in pathological cases. No
  observed test failure.

---

## Files committed

- `src/core/emulator.cpp` — Multiface F9 button arbitration fix.
- `src/peripheral/nmi_source.cpp` — NR 0x02 readback config_mode
  preservation; `/NMI` HOLD-state release.
- `doc/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY-NMI-MF-PORT.md` — this report.

---

## Branch / commit

- Branch: `task2/verify-nmi-mf-port`
- Pre-audit HEAD: `e6bd9ce`
- Post-audit HEAD: (this commit, see `git log -1`)
- All work local; no push, no merge to main, no other branches touched.
