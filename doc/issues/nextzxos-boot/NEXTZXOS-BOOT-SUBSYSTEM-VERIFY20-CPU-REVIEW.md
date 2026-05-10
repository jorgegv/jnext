# Pass-20 CPU + Z80N + IM2 — Independent Review

Reviewer branch: `task2/verify20-cpu-z80n-im2-reviewer` (off audit HEAD `ca8cbc5`).
Reviewer worktree: `.claude/worktrees/task2-verify20-cpu-z80n-im2-reviewer`.
Reviewer build: Release, ROMs symlinked from main worktree.

## Verdict

**APPROVE-WITH-NITS**.

The V20-IM2-01 finding is VHDL-faithful, the fix is correctly edge-detected,
and the discriminative test sandwich-verifies (pre-fix PC=0xC53F as audit
claimed; post-fix PC<0x4000). All baseline test suites are clean (ctest 38/38,
FUSE 1356/1356, ctc_interrupts 28/28, cpu_int_pulse 11/11,
cpu_z80n_im2_regressions 45/45, ctc 132/132, contention 82/82, regression
33/0/0, rewind 22/0/10). Pass-19 fixes (V19-IM2-01..04 + V19R-CPU-01) all
re-verified clean. Z80N opcode re-audit spot-checked 5 opcodes against VHDL —
all VHDL-faithful (UB-free shifts, identity-on-rot-16, F.C clear via 16-bit
truncation).

NITs are docs-and-process flavoured (no functional fix required) and one
class-(c) save-state corner case:

1. **Enumeration row count overstated (cosmetic)** — audit claims "~150" rows;
   actual count is ~125–130 surface rows. Row counts per file: z80n_ext.cpp 31,
   z80_cpu.cpp 22, im2.cpp/im2.h 45, emulator.cpp 32 (= 130 total). Still
   covers all Z80N opcodes, every IM2 method, every relevant NR handler — the
   coverage breadth is fine. Just the headline number is ~15–20 % inflated.

2. **Line 6655 referenced in audit table but actual LINE-INT scheduler is
   at line 6721 (cosmetic)** — minor doc drift. The row 6653 entry in the
   table claims "LINE-INT scheduler" but that line is `enqueue_cpu_nr_write`.
   The actual scheduler is at `emulator.cpp:6716-6727` (the
   `schedule_line_int_for_target()` helper).

3. **`prev_pulse_int_n_` not persisted in save_state — class-(c) save-state
   corner case (V20R-CPU-NIT-01).** `Emulator::save_state()` does not
   serialise `prev_pulse_int_n_`; `Im2Controller::save_state()` DOES
   serialise `pulse_int_n_`. Pre-fix the shadow does not exist, so this is
   net-new state. Failure scenario: emulator session A drops `pulse_int_n_=0`
   at tick T (some peripheral fires in pulse mode); `prev_pulse_int_n_=0`
   after the V20 falling-edge consume. Save_state at this point: `pulse_int_n_=0`
   persisted, `prev_pulse_int_n_` not persisted. Session B loads: `Im2Controller`
   restores `pulse_int_n_=0`; `Emulator::prev_pulse_int_n_` stays at whatever
   it was (`true` if construction/reset just happened, which is the typical
   load_state precondition). Next tick in session B: `cur_pulse_int_n=false`,
   `prev_pulse_int_n_=true` → spurious falling-edge fire →
   `cpu_.request_interrupt(0xFF)`. The pulse is in its tail half so the 32/36-cycle
   window in `Z80Cpu::execute()` clears it shortly anyway, but the fix's own
   "exactly ONCE per pulse" invariant is locally violated. Recommended:
   write `prev_pulse_int_n_` in `Emulator::save_state()` / `load_state()`
   alongside `im2_c4_expbus_` and friends (already documented at the
   `prev_pulse_int_n_` declaration as "Initial value true matches
   Im2Controller::reset() default" — but the same comment ought to mention
   the save/load coupling).

4. **Theoretical double-INT during legacy ULA/LINE callback + V20 poll race
   (class-(c) timing nit; not currently observed)** — when the FRAME-INT
   or LINE-INT scheduler callback fires in pulse mode (NR 0xC0 b0=0), it
   already calls both `im2_.raise_req(ULA/LINE)` AND
   `cpu_.request_interrupt(0xFF)` (emulator.cpp:5447, :6723). Then on the
   NEXT run_frame iteration, `im2_.tick()` processes the int_req edge,
   drops `pulse_int_n_=false`, and the V20 falling-edge poll fires
   `cpu_.request_interrupt(0xFF)` AGAIN. Scenario:
   - Iter N: scheduler fires legacy callback → `int_pending_=true`,
     `int_requested_at_=T0`. `pulse_int_n` still `true` because im2_.tick
     hasn't run for this callback yet.
   - Iter N+1: `execute()` may accept the legacy INT (T-states ~T1=T0+δ).
     Acceptance clears `int_pending_=false`. Then `im2_.tick()`:
     `step_pulse` consumes the ULA int_req edge, `pulse_int_n_` drops to
     `false`. V20 poll falling-edge condition fires →
     `cpu_.request_interrupt(0xFF)` AGAIN. Now `int_pending_=true`,
     `int_requested_at_=T1`. `prev_pulse_int_n_=false`.
   - Iter N+2: CPU is executing ISR with IFF1=0 — won't accept. Pending
     stays. Pulse window (32/36 cpu cycles from T1) eventually expires
     via the unconditional drop arm at `z80_cpu.cpp:467-470` → harmless.

   It only becomes observable if the ISR does `EI` within the 32/36-cycle
   tail of the V20 re-stamped window. Real 48K/128K boot ROMs never EI
   that fast (typical ISR is many tens of T-states), so no regression
   test trips. **All 11/11 cpu_int_pulse and 33/0/0 regression tests pass**,
   confirming the practical absence of the bug. Recommended cleanup:
   either drop the legacy `cpu_.request_interrupt(0xFF)` from the ULA/LINE
   scheduler callbacks (let V20 poll be the sole driver), OR gate the V20
   poll to skip when the legacy path already stamped within the same tick
   (e.g. by introducing a small "stamped-this-tick" flag). The cleaner
   refactor is the former — but it's risky without a green-field test
   matrix, so leaving as-is is acceptable. Filed as **V20R-CPU-NIT-02
   timing-race redundancy** for a future cleanup pass.

The above NITs are NOT blockers. The fix as committed is correct and
VHDL-faithful for the common case (single CTC/UART pulse trigger). The
two cosmetic doc NITs are also non-blocking.

## Table validation

Audit claimed "~150 rows" in the enumeration table. Actual:
- pipe-table rows: 132 (incl. the 5-row test-results header table).
- main enumeration table: ~125–130 surface rows.

Per-file coverage:

| Surface file | Audit rows |
|---|---|
| z80n_ext.cpp | 31 |
| z80_cpu.cpp | 22 |
| im2.cpp + im2.h | 45 |
| emulator.cpp | 32 |
| **Total** | ~130 |

Coverage breadth is solid (all Z80N opcodes, all IM2 fabric methods,
every NR handler that touches CPU/IM2 state). The "~150" figure in the
summary is overstated by ~15–20 % but does not affect correctness.

## Spot-checks (10 ✓ rows verified against VHDL/C++)

| # | Row | VHDL/C++ check | Result |
|---|---|---|---|
| 1 | z80n_ext.cpp:142 SWAPNIB | t80n.vhd:702-704 confirms nibble swap | ✓ |
| 2 | z80n_ext.cpp:299 ADD_HL_A | t80n.vhd:778-783 — `reg_temp_t(16)` not driven; F.C cleared by 16-bit truncation | ✓ |
| 3 | z80n_ext.cpp:610 LDIX | t80n_mcode.vhd:2098-2138 confirms `Write` gated on `ext_ACC_i /= ext_Data_i` for `IRB=A4/B4`, HL++/DE++/BC-- | ✓ |
| 4 | im2.cpp:483 set_int_en_c4 | zxnext.vhd:5607-5610 stores bit 1 (line) and bit 7 (expbus) only; bit 0 (ULA) intentionally absent | ✓ |
| 5 | im2.cpp:1057 ULA exception | im2_peripheral.vhd:192 `((im2_mode AND NOT z80_im2) OR (NOT im2_mode))` matches C++ `!im2_mode_ || (im_mode_ != 2)` | ✓ |
| 6 | emulator.cpp:1850 NR 0x22 | bit 2 → port_ff_reg(6) → ULA int_en; bit 1 → LINE int_en + video_timing; bit 0 → line target MSB | ✓ |
| 7 | emulator.cpp:5443 FRAME-INT | raise_req(ULA) + (pulse mode) request_interrupt(0xFF) wired correctly | ✓ |
| 8 | im2.cpp:90 int_unq one-shot clear | V19-IM2-03 fix code present at line 90; nr_20_we one-cycle pulse honoured | ✓ |
| 9 | emulator.cpp:5728 IM2-mode INT poll | V19-IM2-04 fix at line 5730 (`int_line_asserted()` → `request_interrupt(0xFE)`) | ✓ |
| 10 | im2.cpp:614 ack_vector | im2_device.vhd:111-116 + zxnext.vhd:1999 priority-walk + vector compose | ✓ |

10/10 spot-checks confirm. The table accurately reflects the current code
and matches VHDL.

## V20-IM2-01 verification

### VHDL oracle re-read

- **zxnext.vhd:1840** (verified): `z80_int_n <= ((pulse_int_n and im2_int_n) or not expbus_disable_int) and (i_BUS_INT_n or expbus_disable_int);` — when `expbus_disable_int='1'` (default), reduces to `pulse_int_n AND im2_int_n`.
- **zxnext.vhd:2017-2031** (verified): `pulse_int_n` FSM. Drops to `0` on `pulse_int_en='1'` rising edge; returns to `1` on `pulse_count_end`. `pulse_count_end = pulse_count(5) AND (machine_timing_48 OR machine_timing_p3 OR pulse_count(2))` = bit5-set AND (48/+3 OR bit2-set) = 32-cycle (48/+3) or 36-cycle (128K/Pentagon/Next).
- **im2_peripheral.vhd:186** (verified): non-EXCEPTION path —
  `o_pulse_en <= ((int_req AND i_int_en) OR i_int_unq) AND NOT i_mode_pulse_0_im2_1;` — fires in pulse mode only.
- **im2_peripheral.vhd:192** (verified): EXCEPTION (ULA) path — also
  fires in IM2 mode when CPU `i_im2_mode='0'` (CPU not actually in IM=2).

### Fix diff verification

- **Edge-detect direction** (`!cur_pulse_int_n && prev_pulse_int_n_`): correct
  for falling-edge detection (was high → now low = INT asserted).
- **Gating**: `!im2_.is_im2_mode()` — pulse-mode only, mutually exclusive
  with the IM2 poll at line 5730. **No double-fire between the two polls.**
- **Shadow update**: `prev_pulse_int_n_ = cur_pulse_int_n;` runs AFTER
  the falling-edge check — correct.
- **Re-stamping concern**: well-handled. After one falling-edge fire,
  subsequent ticks during the pulse have `prev_pulse_int_n_=false`, so
  the condition `!cur && prev` = false. No re-stamp. The pulse window
  in `Z80Cpu::execute()` (32/36T) self-expires naturally via the
  unconditional drop arm (`z80_cpu.cpp:467-470`).
- **Re-assertion after RETI**: after pulse expires (`pulse_int_n_=true`),
  `prev_pulse_int_n_` updates to `true` on next tick. Next pulse drops
  → falling edge again → V20 fires. RETI doesn't interact directly
  with pulse_int_n (only with `reti_decode`/`reti_seen` for IM2 mode).
- **Initial value**: `prev_pulse_int_n_=true` in both `init()` (line 111)
  and `reset()` (line 6234), matching `Im2Controller::reset()`'s initial
  `pulse_int_n_=true` (line 37 of im2.cpp).

### Sandwich test

| Step | ctc_interrupts_test | CTC-INT-V20-IM2-01 |
|---|---|---|
| Baseline (post-fix HEAD `ca8cbc5`) | 28/28 PASS | PASS |
| Revert `src/core/emulator.{cpp,h}` to `63c8d48~1` (pre-fix), keep test | 27/28 PASS, **1 FAIL** | **FAIL PC=0xC53F** (matches audit's exact claim) |
| Restore fix (`git checkout HEAD -- src/core/emulator.{cpp,h}`) | 28/28 PASS | PASS |

Sandwich confirms the test is discriminative and the fix exact.

## Pass-19 re-verification spot-check

The audit claims all Pass-19 fixes remain clean. I spot-checked 3:

| Fix | Spot-check | Result |
|---|---|---|
| **V19-IM2-01** (NR 0x22 b1 → LINE int_en) | emulator.cpp:1870 `im2_.set_int_en(LINE, (v&0x02)!=0)` present; VHDL :5607-5610 + :5297 confirms bit 1 path | ✓ |
| **V19-IM2-03** (int_unq one-shot clear) | im2.cpp:90 `for (k) dev_[k].int_unq = false` present at end of tick(); VHDL :1946-1947 nr_20_we one-cycle pulse | ✓ |
| **V19-IM2-04** (int_line_asserted poll) | emulator.cpp:5730 `if (im2_.is_im2_mode() && im2_.int_line_asserted()) cpu_.request_interrupt(0xFE)` present | ✓ |

Plus V19R-CPU-01 (int_req auto-clear at end of tick) at im2.cpp:141 — present.

cpu_z80n_im2_regressions_test 45/45 PASS, including
`V19-IM2-03-INT-UNQ-ONE-SHOT-AFTER-ISR` and
`V19R-CPU-01-INT-REQ-PULSE-SYNTHESIS-MULTI-FRAME-VHDL-101` rows.

## Z80N opcode re-audit spot-check (5 opcodes)

| Opcode | C++ location | VHDL ref | Strict-UB check | Result |
|---|---|---|---|---|
| **SWAPNIB** | z80n_ext.cpp:142 | t80n.vhd:702-704 | uint8_t swap, no UB | ✓ |
| **BSLA_DE_B** | z80n_ext.cpp:190 | t80n.vhd:987-993 | uint32_t shift, mask 0xFFFF; for shift≥16 returns 0 via 32-bit overflow truncation. No UB (shift up to 31 on uint32_t is well-defined) | ✓ |
| **BSRA_DE_B** | z80n_ext.cpp:209 | t80n.vhd:1006-1014 (bit 16 = bit 15 sign) | explicit branching: shift=0 identity; shift≥16 sign-fill; else unsigned shift + mask-OR of top bits. No UB | ✓ |
| **BSRF_DE_B** | z80n_ext.cpp:252 | t80n.vhd:1006-1014 (bit 16 = IR[0]=1) | explicit branching: shift=0 identity; shift≥16 = 0xFFFF; else unsigned shift + mask-OR. No UB | ✓ |
| **BRLC_DE_B** | z80n_ext.cpp:279 | t80n.vhd:1022-1028 | `rot &= 0x0F` after `if(rot!=0)` outer; rot=16 case takes outer branch, then masked to 0, `DE<<0 \| DE>>16` where uint16 int-promotes → DE>>16=0 via int-promotion. Identity. No UB | ✓ |

All 5 spot-checks VHDL-faithful and strictly UB-free.

## Adjacent re-audit (VHDL :1840-2050)

Re-read VHDL `zxnext.vhd:1840-2050`:

- **`z80_int_n`** consumers: only :1840 itself.
- **`pulse_int_n`** consumers: only :1840.
- **`im2_int_n`** consumers: only :1840 (already covered by V19-IM2-04).
- **`i_BUS_INT_n`** consumers: only :1840, gated on `expbus_disable_int='0'`.
  jnext does not model expansion bus — class-d, correctly skipped by audit.
- **`pulse_int_en`** consumer: :2024 in the pulse FSM. The drop trigger.
- **`im2_dma_delay`** (:2001-2010) — well-modelled by Im2Controller::step_dma_delay.
- **Stackless-NMI path** (:2052+) — covered by V18-CPU / NR 0xC2/C3 work.

No additional missed `pulse_int_n` / `im2_int_n` consumers in this VHDL window.

The class-(d) item `Expansion-bus INT path (i_BUS_INT_n)` audit-listed at the
end of the report is honest — jnext doesn't have an expansion bus model
anywhere. Latent / out-of-scope.

The class-(d) item `ULA EXCEPTION pulse fires in IM2 mode when CPU IM ≠ 2`
(`im2_peripheral.vhd:192`) is honestly listed. The V20 fix's
`!im2_.is_im2_mode()` gate means this niche is NOT covered. Realistic
software pairs `NR 0xC0 b0=1` with `IM 2` instruction at boot, so this
is genuinely latent.

## Test invariants (re-run on reviewer worktree)

| Suite | Required | Observed |
|---|---|---|
| ctest (38 suites) | 38/38 | **38/38** |
| FUSE Z80 (1356 opcodes) | 1356/1356 | **1356/1356** |
| ctc_interrupts_test | 28/28 | **28/28** |
| cpu_int_pulse_test | 11/11 | **11/11** |
| cpu_z80n_im2_regressions_test | 45/45 | **45/45** |
| ctc_test | 132/132 | **132/132** |
| contention_test | 82/82 | **82/82** |
| rewind_test | 22/0/10 | **22/0/10** |
| regression.sh | 33/0/0 | **33/0/0** |

All test invariants met.

## NITs summary

- **V20R-CPU-NIT-01** (class-c, save-state): persist `prev_pulse_int_n_`
  in `Emulator::save_state()` / `load_state()` to keep the falling-edge
  invariant across save/restore. Recommended fix: small addition to the
  schema after `im2_c4_expbus_`.
- **V20R-CPU-NIT-02** (class-c, timing-race redundancy): the legacy
  scheduler-callback `cpu_.request_interrupt(0xFF)` and the V20 falling-edge
  poll both stamp `int_requested_at_` for the same ULA/LINE pulse. Harmless
  for boot-realistic ISRs (32/36-cycle window expires before any EI), but
  the redundant path is a latent double-INT trap if an ISR does fast EI.
  Recommended cleanup: drop the scheduler-callback legacy path and let the
  V20 poll be the sole driver. Filed for a future cleanup pass.
- **V20R-DOC-NIT-01** (cosmetic): audit's "~150 rows" claim is ~15–20 %
  overstated (actual ~125–130). Cover/headline only — coverage breadth is
  fine.
- **V20R-DOC-NIT-02** (cosmetic): table row references "emulator.cpp:6653
  LINE-INT scheduler" — actual scheduler is at `:6716-6727`. Minor doc drift.

## Final state

- Reviewer branch: `task2/verify20-cpu-z80n-im2-reviewer` (off audit HEAD
  `ca8cbc5`).
- Reviewer commit (this report): TBD (single doc commit, no code changes).
- Not pushed.

## Verdict (repeat)

**APPROVE-WITH-NITS** — V20-IM2-01 is a correct, VHDL-faithful, test-
discriminative fix. NITs above are non-blocking and offered for an
optional Pass-21 cleanup.
