# Independent Review — Task 2 NMI / Multiface / NextREG / Port

**Date**: 2026-05-09
**Reviewer branch**: `task2/nmi-mf-port-reviewer`
**Reviewing commit**: `c1d7998` (single fix commit on top of base `ac56cff`)
**VHDL oracle**: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
**Original report**: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-NMI-MF-PORT.md`

---

## Verdict

**APPROVE-WITH-NITS**

All five reported findings (NMI-1, NMI-2, NMI-3, NMI-4, NR-2) are independently
re-verified against VHDL and the corresponding fixes are VHDL-faithful. Tests
pass on this worktree (36/36 ctest, 1356/1356 FUSE Z80, 58 NMI rows: 52 PASS /
0 FAIL / 6 SKIP). The work is ready to merge, but I am calling out four
documentation gaps and four missing test rows that are clearly within the
spirit of CLAUDE.md ("when a bug is fixed... make sure there are enough test
cases in that subsystem's test plan to fully test the fixed new code"). I also
identified one **adjacent latent bug** the agent missed (`nr_02_pending_*`
clear in `config_mode` block of `recompute_()`) and one ungated
`nr_da_iotrap_cause_` write that diverges from VHDL line 3871. None of the
nits or adjacent findings invalidate the agent's work; they are queued for
follow-up.

| Bucket | Count |
|--------|-------|
| Confirmed findings | 5 (NMI-1, NMI-2, NMI-3, NMI-4, NR-2) |
| Disputed findings | 0 |
| Added findings | 2 (R-1: `nr_02_pending_*` clear in `config_mode`; R-2: `nr_da_iotrap_cause_` set lacks `nmi_accept_cause` gate) |
| Test plan gaps | 4 (test-plan stale; missing regression rows for NMI-1 ExpBus selector, NMI-3 second-NMI, NR-2 slot-2-5 $FF) |
| Documentation gaps | 3 stale comments (`nr_02_read` header, `recompute_()` line 361, `nmi_source.h:55,193`) |

---

## Per-finding reassessment

### NMI-1 — `HOLD→END` selector ignores ExpBus arm — **CONFIRMED**

VHDL line 2118 (verified with `Read` of `zxnext.vhd:2118`):

```vhdl
nmi_hold <= mf_nmi_hold     when nmi_mf     = '1'
       else divmmc_nmi_hold when nmi_divmmc = '1'
       else nmi_assert_expbus;
```

Pre-fix code at `nmi_source.cpp:362`:
```cpp
const bool hold = nmi_mf_ ? mf_nmi_hold_ : divmmc_nmi_hold_;
```

This collapses three arms to two; for an ExpBus-latched NMI, the cpp would
read `divmmc_nmi_hold_` (typically idle false) and fall through to End
prematurely whenever `nmi_mf_=0` and the actual selector should have been
`nmi_assert_expbus()`.

Fix at `nmi_source.cpp:385-405` properly cascades MF → DivMMC → ExpBus and
calls `nmi_assert_expbus()` on the third arm. **VHDL-faithful.** Note that
`nmi_assert_expbus()` is the *combinational* signal (line 2089), not the
latched `nmi_expbus_`, which matches the VHDL exactly (line 2118 uses
`nmi_assert_expbus`, not `nmi_expbus`).

**Nit**: there is no unit test row that exercises the new ExpBus arm —
`set_expbus_nmi_n(false)` followed by Hold-state and verifying the FSM does
NOT advance to End while the pin remains asserted. Recommended add row in
`test/nmi/nmi_test.cpp` (e.g. GATE-09).

### NMI-2 — NR 0x02 readback latch clear path — **CONFIRMED**

VHDL lines 3840-3851 (verified):
```vhdl
elsif nmi_gen_nr_mf = '1' and nmi_accept_cause = '1' then
   nr_02_generate_mf_nmi <= '1';
elsif nr_02_we = '1' and nr_wr_dat(3) = '0' then
   nr_02_generate_mf_nmi <= '0';
```

The FSM signal `nmi_state` is **NOT** in this process's input set — there is
no auto-clear at `S_NMI_END`. Pre-fix cpp behavior (auto-clear in `case End`
of `recompute_()`) was a non-VHDL invention.

Agent's fix:
* Gates the SET path on `accept = (state in {Idle, Fetch})` = `nmi_accept_cause`.
* Adds the explicit CLEAR path on bit-write-low.
* Removes the `case End` auto-clear.
* Updates test row NR02-05 to verify the new (correct) semantics.

All four moves match VHDL. **VHDL-faithful.**

**Subtle race check**: VHDL's clear path is ungated by `nmi_accept_cause`; the
agent's code likewise does NOT gate the clear on `accept` (line 130 / 137 of
`nmi_source.cpp`). Correct.

**Nit**: The `nr_02_read()` header comment at `nmi_source.cpp:148-149` still
says `auto-clear @END`. **Stale documentation.** Should be updated to mirror
VHDL semantics (write-back-clear).

### NMI-3 — FSM stuck in End forever — **CONFIRMED (critical)**

`grep observe_cpu_wr src/` returns only the definition site and one stale
header comment — there is **no caller** in `Emulator` or anywhere else. Z80
core does not surface `wr_n` through callbacks. The original FSM had no
End→Idle transition path because:
1. `observe_cpu_wr` was the only mover and was never called.
2. `observe_retn` is documented as a no-op (`nmi_source.cpp:307-312`).
3. `case End` in pre-fix `recompute_()` cleared latches but did NOT advance
   `state_`.

Consequence: first NMI works (Idle→Fetch→Hold→End). Second NMI fails: FSM
sticks in End, latch is set by `nmi_assert_*`, next `recompute_()` re-enters
`case End`, clears the latch, never advances. Bug confirmed.

Agent's fix advances `state_ = State::Idle` at the bottom of `case End`, on
the same recompute pass that clears the priority latches. This is a 1-tick
End → 1-tick Idle progression that diverges from VHDL only by the
sub-instruction `cpu_wr_n` rising-edge timing.

**Faithfulness check**: in VHDL, End→Idle waits for `cpu_wr_n='1'`, which is
true except during the Z80 write-cycle data phase. Practically, this happens
within a few T-states of entering End. With our coarse per-instruction tick
(`tick(master_cycles)` ignoring `master_cycles`), the agent's "advance on
the next recompute" approximation is the correct behavioral mapping.

**Concern (raised but not blocking)**: there is **no regression test** for the
critical bug just fixed. The agent's NR02-05 update verifies the readback
latch survives End, but does not verify that the FSM accepts a second
software-NMI strobe. Recommended add row e.g. NR02-09: drive two
sequential NR 0x02 bit-3 writes through Idle→Fetch→Hold→End→Idle and assert
that the second NMI fires. Per CLAUDE.md "when a bug is fixed... make sure
there are enough test cases", this is a clear miss.

**Documentation nits** (non-blocking):
* `nmi_source.h:55` claims `observe_cpu_wr drives END -> IDLE` — stale.
* `nmi_source.h:193` repeats the same misleading claim.
* `nmi_source.cpp:361` comment in `recompute_()` says `END -> IDLE is driven
  by observe_cpu_wr() rising edge` — stale.

### NMI-4 — NR 0x02 readback bit 4 (iotrap) not composed — **CONFIRMED**

VHDL line 5891 (verified):
```vhdl
port_253b_dat <= nr_02_bus_reset & "00" & nr_02_iotrap & nr_02_generate_mf_nmi
                 & nr_02_generate_divmmc_nmi & nr_02_reset_type(1 downto 0);
```

VHDL line 3885: `nr_02_iotrap <= nr_da_iotrap_cause(1) or nr_da_iotrap_cause(0);`.

The Emulator NR 0x02 read handler at pre-fix `emulator.cpp` returned only
`NmiSource::nr_02_read()`. `nr_02_read()` itself does not compose bit 4
(its comment correctly disclaims that bit 4 belongs to Emulator). The
`nr_da_iotrap_cause_` shadow is tracked in `Emulator::nr_da_iotrap_cause_`
and visible at `emulator.cpp:1610`, but its bit-4 contribution to NR 0x02
was missing. Bug confirmed.

Fix at `emulator.cpp:1609-1611`:
```cpp
uint8_t v = nmi_source_.nr_02_read();
if ((nr_da_iotrap_cause_ & 0x03) != 0) v |= 0x10;
return v;
```

This is the OR of `nr_da_iotrap_cause_(1)` and `nr_da_iotrap_cause_(0)`,
which matches VHDL line 3885 exactly. **VHDL-faithful.**

**Adjacent finding R-2 (NEW, not in agent's list)**: `nr_da_iotrap_cause_` is
SET unconditionally on iotrap port accesses at `emulator.cpp:2370,2382,2392`,
but VHDL line 3871 gates the SET on `nmi_gen_iotrap='1' AND nmi_accept_cause='1'`.
If an iotrap port access fires while the FSM is in HOLD or END (e.g., a
nested iotrap mid-NMI-handler), jnext will overwrite `nr_da_iotrap_cause_`,
whereas VHDL retains the original cause. This is a latent divergence — no
current test or boot path exercises it, but it is the same class of bug as
NMI-2 and worth fixing in a follow-up. Concrete fix: gate the SET on
`nmi_source_.state() == NmiSource::State::Idle || NmiSource::State::Fetch`.

### NR-2 — NR 0x52..0x55 `$FF` silently remapped — **CONFIRMED (latent)**

VHDL lines 4607-4699 (verified), specifically:
```vhdl
elsif nr_mmu_we = '1' then
   case nr_mmu is
      when "010"  => MMU2 <= nr_wr_dat;   -- verbatim store
      when "011"  => MMU3 <= nr_wr_dat;
      when "100"  => MMU4 <= nr_wr_dat;
      when "101"  => MMU5 <= nr_wr_dat;
      ...
   end case;
end if;
```

There is **no** `$FF`-special-case for any slot (0..7) on NR-write. The
VHDL behavior is verbatim store. The "$FF means legacy auto-paging" is a
firmware convention realized by the surrounding `port_memory_change_dly`
re-derivation logic, which only activates for slots 0/1 (legacy ROM-area)
and 6/7 (legacy RAM-area). For slots 2-5 there is no auto-derivation.

Pre-fix `emulator.cpp:1378-1382`:
```cpp
} else {
   // Slots 2-5: keep prior fallback behavior
   mmu_.map_rom(i, 0);
}
```

`Mmu::map_rom(slot, 0)` calls `Mmu::map_rom_physical(slot, 0)` which
performs three actions:
1. Sets `slots_[slot] = 0`.
2. Sets `read_only_[slot] = true`.
3. Sets `read_ptr_[slot] = ROM page 0`.

**Triple divergence from VHDL**: not just a remap to page 0 (as the agent's
note describes), but also makes the slot read-only AND points reads at the
ROM area instead of leaving the slot unmapped. On a hypothetical supervisor
write `NR $54, $FF`, jnext would silently start serving ROM page 0 from
slot 4 and dropping writes — vastly worse than VHDL's "store $FF, slot
becomes inactive (`sram_pre_active=0`), reads return floating bus".

Fix at `emulator.cpp:1393`:
```cpp
mmu_.set_page(i, 0xFF);
```

Decoded against the jnext MMU at `Mmu::rebuild_ptr` (`mmu.cpp:149-185`):
* `slots_[slot] = 0xFF` and `read_only_[slot] = false` → first branch:
  `if (page == 0xFF || read_only_[slot])` matches.
* Inside that branch: `read_only_[slot]=false` → second sub-branch →
  `read_ptr_=nullptr, write_ptr_=nullptr`.
* Reads via `read()` return `0xFF` for null `read_ptr_` (the floating-bus
  approximation).
* Writes via `write()` are dropped on null `write_ptr_`.

This matches VHDL's "page $FF resolves to `mmu_A21_A13(8)='1'` →
`sram_pre_active=0` → SRAM inactive on read/write" semantics for slots 2-5.
**VHDL-faithful.**

**Slot 0/1 sanity check (asymmetry concern raised by reviewer mission)**:
For slots 0/1, agent already routes `engage_legacy_rom_paging()`. This is
correct because slot 0/1 in VHDL has the elaborate auto-derivation at
lines 4636-4646 (set MMU0/MMU1 to `0x00,0x01` if `port_eff7_reg_3=1` or
`profi+dffd_reg(4)=1`, else `0xFF,0xFF`). The agent's slot-0/1 path is
unchanged from `8242098`.

For slots 6/7, agent routes `engage_legacy_ram_paging()`. Correct (auto-
derivation lines 4677-4680).

For slots 2-5, the new `set_page(i, 0xFF)` is the right choice because no
auto-derivation exists.

**Test plan gap**: `test/nextreg/nextreg_test.cpp` exercises NR 0x52 with
`0x20` (line 453) but no row writes `$FF` to slot 2-5 and verifies (a) the
read-back of `nr_mmu_[2]` returns `$FF`, (b) memory reads from `0x4000` return
`0xFF` (floating bus), (c) memory writes to `0x4000` are dropped. Recommended
add row `MMU2-FF-FALLBACK` (or equivalent).

---

## Test verification

```
LANG=C cmake -B build -DENABLE_QT_UI=ON   → clean (0.3s configure)
LANG=C cmake --build build -j$(nproc)     → clean (links jnext)
LANG=C ctest --test-dir build             → 100% tests passed, 0 tests failed out of 36
./build/test/fuse_z80_test build/test/fuse → 1356/1356 PASS
./build/test/nmi_test                     → 58 rows: 52 PASS / 0 FAIL / 6 SKIP
```

* `nmi_tests` (Phase 1+A+B+C+E): 30 row groups, all PASS / SKIP.
* `nmi_integration_tests`: PASS.
* `multiface_tests`: PASS.
* `nextreg_tests` / `nextreg_integration_tests`: PASS.
* `port_tests`: PASS.

Test count and pass-rate match agent's report. **Tests verified.**

---

## Coverage gaps

These are **outside the agent's stated scope** but flagged per CLAUDE.md
"enough test cases" rule.

### Missing regression test rows

1. **NMI-1 ExpBus HOLD selector**. Drive `set_expbus_nmi_n(false)` →
   `nmi_assert_expbus=true` → `nmi_expbus_=true`, advance to Hold. While in
   Hold, `set_divmmc_nmi_hold(true)` should NOT block End advance (because
   the agent's fix correctly selects `nmi_assert_expbus()` on the third
   arm); BUT also asserting `expbus_nmi_n=false` should hold the FSM in
   Hold (because `nmi_assert_expbus()` returns true). Without this row, the
   NMI-1 fix has no targeted regression coverage.

2. **NMI-3 second-NMI fires after End**. Test sequence:
   `set_mf_enable(true)` → `nr_02_write(0x08)` → `tick(1)` (Idle→Fetch) →
   `observe_m1_fetch(0x0066, true, true)` → `tick(1)` (Hold) → `tick(1)`
   (End→Idle) → `nr_02_write(0x08)` again → `tick(1)` → assert `nmi_mf()`
   is true again. The pre-fix code would have failed this row.

3. **NR-2 slot 2-5 `$FF` fallback**. NextReg test that writes `$FF` to NR
   0x52..0x55 and verifies (a) read-back of NR 0x52..0x55 = `$FF`, (b)
   physical read at slot-2..5 address ranges returns `0xFF` (floating bus),
   (c) physical write is dropped. Optionally compare against pre-fix
   behavior to demonstrate divergence with VHDL.

4. **NMI-2 readback latch survives End**. The agent updated NR02-05 to
   verify this, which is the right test. **No additional row needed for
   NMI-2 itself.**

### Test plan staleness

`doc/testing/NMI-PIPELINE-TEST-PLAN-DESIGN.md` contains stale references to
the pre-fix semantics:

* Line 30: `auto-clear on FSM END.` — should be `clear on bit-write-low`.
* Line 68: row table claims `bits 3/2 auto-cleared by FSM` — should be
  `bits 3/2 cleared on bit-write-low`.
* Line 96: `cleared together on FSM S_NMI_END` — only the priority latches
  are cleared on END, NOT the readback latches.
* Line 116: `latches clear here` — qualify as "priority latches only".
* Line 158-159: claims `mf_pending / divmmc_pending bits auto-clear when
  the FSM transitions through S_NMI_END`. Wrong now.
* Line 197: NR02-05 row description still says `auto-clear on FSM END`.
* Line 389: `auto-clear` blurb in NR02 group summary.

The agent updated the *test code* (NR02-05) to match the new semantics but
not the *test plan*. CLAUDE.md mandates plan updates when the interface or
the bug semantics change. **Plan should be updated.**

### Documentation staleness in source

* `src/peripheral/nmi_source.h:55` (header doc on observers) says
  `observe_cpu_wr(wr_n) drives END -> IDLE on the rising edge` — no longer
  true.
* `src/peripheral/nmi_source.h:193` repeats the claim.
* `src/peripheral/nmi_source.cpp:148-149` — `nr_02_read()` comment still
  says `auto-clear @END` for bits 3/2.
* `src/peripheral/nmi_source.cpp:361` — `END -> IDLE is driven by
  observe_cpu_wr() rising edge` — no longer true.

These are doc-only nits; they don't affect runtime behavior but are
misleading for future readers and add maintenance debt.

---

## Adjacent latent bugs (not in agent's scope; surfaced during review)

### R-1 — `recompute_()` config_mode block clears `nr_02_pending_*`

Per VHDL line 1730 (`reset <= i_RESET`) the `reset` signal is the hardware
reset pin only; `nr_03_config_mode` does **not** trigger `reset='1'`. The
NR 0x02 readback latches at lines 3840-3864 are clocked but **only**
cleared on `reset='1'` or on bit-write-low. They are NOT cleared on
config_mode entry.

`nmi_source.cpp:319-330`:
```cpp
if (config_mode_) {
    nmi_mf_     = false;
    nmi_divmmc_ = false;
    nmi_expbus_ = false;
    nr_02_pending_mf_     = false;       // <-- WRONG per VHDL
    nr_02_pending_divmmc_ = false;       // <-- WRONG per VHDL
    state_ = State::Idle;
    return;
}
```

The priority-latch clears (`nmi_mf_, nmi_divmmc_, nmi_expbus_`) and
state-reset are correct (VHDL lines 2102-2105 explicitly clear those on
`nr_03_config_mode='1'`). The two `nr_02_pending_*` clears are not.

**Severity**: latent — same class as NMI-2 (which the agent fixed). Should
be addressed in a follow-up commit; should be added to the agent's report
as NMI-5 or noted as related to NMI-2.

### R-2 — `nr_da_iotrap_cause_` SET is ungated by `nmi_accept_cause`

VHDL lines 3866-3878:
```vhdl
elsif nmi_gen_iotrap = '1' and nmi_accept_cause = '1' then
   if port_2ffd_rd = '1' then
      nr_da_iotrap_cause <= "01";
   ...
```

Note the `AND nmi_accept_cause` gate. jnext's port handlers at
`emulator.cpp:2370, 2382, 2392` do not check this gate:
```cpp
if (nr_d8_io_trap_fdc_en_) {
    nmi_source_.strobe_iotrap();
    nr_da_iotrap_cause_ = 0x01;          // <-- ungated, not VHDL-faithful
    return 0xFF;
}
```

**Impact**: a nested iotrap fired while the FSM is in HOLD/END would
overwrite the original cause in jnext but not on real hardware.

**Severity**: latent — no current test or boot path exercises nested
iotraps. Worth fixing in the same follow-up commit as R-1 because both are
the same class of "missing `nmi_accept_cause` gate".

---

## Code quality

### Style

* `nmi_source.cpp` follows the project's existing comment style (VHDL line
  refs inline in code). Comments are detailed and traceable. Quality is
  high.
* `emulator.cpp` NR 0x02 read handler comment is comprehensive and matches
  the VHDL bit layout.

### Const-correctness

* `NmiSource::nr_02_read() const` is correctly `const` — the readback path
  does not mutate state. ✓

### Branching / dead code

* `observe_cpu_wr` retained as no-op with `prev_wr_n_ = wr_n` only — a
  forward-compatibility hook. Harmless; the `prev_wr_n_` member is used
  nowhere else now (it was tracking the rising edge previously). Could be
  removed entirely along with the wr_n tracking, but the agent's choice to
  keep it for future Z80-bus-callback plumbing is defensible.

### No new logging spam

The agent did not introduce any debug/probe logging. ✓

### Commit message

The commit message is detailed, follows the project's convention (terse but
insightful), references VHDL lines, lists test status. The escape `\$FF`
in the subject line is a side-effect of how the user copied the report
through some intermediate tooling — minor cosmetic only. ✓

### No spurious churn

Diffstat:
```
src/peripheral/nmi_source.cpp:    96 lines changed (mostly comments and
                                  semantic adjustments)
src/core/emulator.cpp:            35 lines changed (NR 0x02 read handler +
                                  NR 0x52-0x55 fallback)
test/nmi/nmi_test.cpp:            43 lines changed (NR02-05 rewrite)
doc/...-NMI-MF-PORT.md:           284 lines added (the report)
```

Tight, focused, no unrelated edits. ✓

---

## Summary

The agent's work is **technically correct, VHDL-faithful, and tested**. The
five fixes land on real bugs (one critical, three correctness-affecting,
one latent silent-corruption trap) and the cpp changes mirror the VHDL
processes verbatim. NMI-3 in particular is a load-bearing fix for any
NMI-driven workflow (Multiface button, NextZXOS NR 0x02 software-NMI
strobes, IO traps, ExpBus pin edges).

The nits are all about **completeness** (test rows, plan updates,
stale-comment cleanup) rather than correctness. The two adjacent latent
bugs (R-1, R-2) are clearly out of scope for the agent's task as written
but are obvious follow-ups in the same VHDL pattern (`nmi_accept_cause`
gating).

**Verdict: APPROVE-WITH-NITS.** Recommend follow-up issue or branch to:

1. Add NR02-09 (NMI-3 second-NMI regression) and GATE-09 (NMI-1 ExpBus
   selector) test rows.
2. Add MMU2-FF-FALLBACK row to NextReg tests.
3. Refresh `doc/testing/NMI-PIPELINE-TEST-PLAN-DESIGN.md` to remove all
   "auto-clear at S_NMI_END" claims for bits 3/2.
4. Update the four stale source-code comments listed under "Documentation
   staleness".
5. Fix R-1 (config_mode `nr_02_pending_*` clear) and R-2
   (`nr_da_iotrap_cause_` accept-gate) in a single follow-up commit.

None of these are blockers for merging the current work.
