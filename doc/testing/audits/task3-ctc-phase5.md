# Subsystem CTC+Interrupts post-un-skip audit (Phase 5)

**Date**: 2026-04-21
**Scope**: post-state audit of `test/ctc/ctc_test.cpp` and new companion `test/ctc_interrupts/ctc_interrupts_test.cpp` following the Task 3 CTC+Interrupts SKIP-reduction plan (Phases 0 → 5 all landed on main).
**Baseline snapshot**: preserved at `doc/testing/audits/task3-ctc.md` (2026-04-15 state: 150/44/0/106 with 3 lazy-skip trio, 1 false-fail that was actually a good-fail).
**Plan doc (authoritative)**: `doc/design/TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md`.

## Summary counts

### `test/ctc/ctc_test.cpp`

| Phase              | Total | Pass | Fail | Skip |
|--------------------|------:|-----:|-----:|-----:|
| Baseline (04-15)   |   150 |   44 |    0 |  106 |
| Post Phase 0       |   135 |   44 |    0 |   91 |
| Post Phase 3 (now) |   133 |  128 |    0 |    5 |
| Session delta      |   −17 |  +84 |    0 | −101 |

Runtime total dropped from 150 to 133 because 17 plan rows migrated
from `check()`/`skip()` into source-level comments (Phase 0 triage
plus Phase 3 re-home placeholders). The 17 rows are NOT dropped — 10
live as passes in the new companion suite, 2 re-home to the
emulator/input layer, and 5 are B/D/E-class unobservables merged with
neighbouring rows that cover the same outcome.

### `test/ctc_interrupts/ctc_interrupts_test.cpp` (new, Phase 3c)

| Total | Pass | Fail | Skip |
|------:|-----:|-----:|-----:|
|    10 |   10 |    0 |    0 |

### Aggregate unit-test impact

| Phase             | Unit total | Pass | Fail | Skip |
|-------------------|-----------:|-----:|-----:|-----:|
| Session start     |       3229 | 2792 |    0 |  437 |
| Session end       |       3222 | 2886 |    0 |  336 |
| Delta             |         −7 |  +94 |    0 | −101 |

Regression (`bash test/regression.sh`): **34/0/0** unchanged throughout.
FUSE Z80: **1356/1356** unchanged throughout.

## Work landed (commit traceability)

Plan doc committed `0d8c056` (2026-04-21). 13 agent-authored commits +
7 merge commits + 4 doc/dashboard commits landed on main the same
day:

| Phase | Key commits |
|-------|-------------|
| 0 (triage) | `b7843d6` code, `3bd4d35` merge, `0b44ee4` dashboard |
| 1 (scaffold) | `0e08a91` code, `ccea277` merge |
| 2 Wave 1 (A + B + D) | `99415ff` A, `37834e0` D, `0155c18` B; merges `0395f1b`, `fca0389`, `043e035` |
| 2 Wave 2 (C + E + H) | `989c49e` C, `c705fe9` E, `bdf32a7` H; merges `c167154`, `165bebd`, `54e4432` |
| 2 Wave 3 (F + G) | `698b10b` F, `5ce4aaa` G; merges `12f3af7`, `2db02c0` |
| 3a wire-up | `fb72964` `cpu_.on_int_ack` + `im2_.tick()` |
| 3b un-skip | `fb1f38a` + `7216ea3` (critic-fix: ULA-INT-04/06 reason strings) |
| 3c new suite | `87fb998` `ctc_interrupts_test.cpp` |
| 3 merge into main | `a397422` |
| 5 dashboard | `0336c20` |

All 3 Phase-2 waves and all 3 Phase-3 sub-phases passed through
independent critic review before merge (per `feedback_never_self_review.md`).

## Major src/ additions

- `src/cpu/im2.h` 28 → ~171 lines, `src/cpu/im2.cpp` 45 → ~800 lines.
  Full `Im2Controller` replacing the priority-mask stub. Covers
  VHDL `device/im2_control.vhd` (RETI/RETN/IM-mode decoder, DMA
  delay), `device/im2_device.vhd` (S_0/S_REQ/S_ACK/S_ISR per device),
  `device/im2_peripheral.vhd` (edge detect, int_status, int_unq,
  im2_int_req latch), `device/peripherals.vhd` (daisy chain with
  IEI→IEO propagation).
- `src/cpu/im2_client.h` — new header-only mixin (~25 lines) for
  peripherals to register themselves with the controller.
- `src/cpu/z80_cpu.{h,cpp}` — added `std::function<uint8_t()> on_int_ack`
  callback + vector resolution in `execute()`. Opt-in; byte-identical
  to legacy behaviour when null.
- `src/peripheral/ctc.{h,cpp}` — added `Ctc::get_int_enable()` readback
  accessor (folded in from CTC-NR-02 un-skip during Agent B).
- `src/core/emulator.cpp` — NR 0xC0/C4/C5/C6/C8/C9/CA/0x20 handlers
  rewired through `Im2Controller`; ULA/line interrupt scheduler
  routes through `raise_req(DevIdx::*)`; DMA int per-frame hook at
  `run_frame()` top; CTC `on_interrupt` callback migrated + new
  `joy_iomode_pin7` extension; `cpu_.on_int_ack` installed in
  `Emulator` ctor; `im2_.tick()` called per-instruction.

## Remaining 5 skips in `ctc_test.cpp` — row-by-row rationale

### CTC-NR-04 — NR 0xC5 vs port-write overlap (review-later)

- **Skip message**: `"NR 0xC5 vs port-write overlap — cycle-accurate bus arbitration, review later"` (ctc_test.cpp:868).
- **VHDL fact**: `zxnext.vhd` `nr_c5_we` must not overlap `i_iowr`
  because `control_reg` is a single-ported register updated by
  whichever strobe is active.
- **Why it stays**: the constraint is about cycle-level bus
  arbitration. jnext serialises both write paths at the C++ call
  level; both writes land, no cycle-level overlap is constructible.
  This is category-G (behavioural simplification) adjacent to
  category-F (upstream gap).
- **Disposition**: user-deferred 2026-04-21; candidate for the future
  `WONT`-taxonomy sweep once the Requirements DB lands
  (`reference_requirements_db_proposal.md`).

### NR-C0-02 — NR 0xC0 bit 3 stackless NMI (NMI-blocked F-keep)

- **Skip message**: `"NR 0xC0 stackless_nmi — blocked on NMI subsystem (see memory/project_nmi_fragmented_status.md)"` (ctc_test.cpp:1980).
- **VHDL fact**: `zxnext.vhd:5598` stores `stackless_nmi`; `zxnext.vhd:2052-2083` implements the stackless vector handoff on NMI.
- **Why it stays**: NMI in jnext is fragmented. The Z80 NMI line and
  DivMMC NMI consumer are wired, but the NMI source/edge generator
  (NR 0x02 strobe, Multiface source, NMI-button input) is missing
  entirely. Without a way to drive an NMI, the stackless bit cannot
  be observed end-to-end.
- **Disposition**: category-F (genuine upstream gap). Unblocks when a
  dedicated NMI subsystem lands.

### DMA-04 — NMI → DMA delay (NMI-blocked F-keep)

- **Skip message**: `"NMI-driven DMA delay — blocked on NMI subsystem (see memory/project_nmi_fragmented_status.md)"` (ctc_test.cpp:2455).
- **VHDL fact**: `zxnext.vhd:2007` latches `im2_dma_delay` on NMI
  when `nr_cc_dma_int_en_0_7 = 1`.
- **Why it stays**: same NMI subsystem absence as NR-C0-02.
- **Disposition**: category-F. Unblocks with the same NMI work.

### ULA-INT-04 — Line interrupt at cvc match (re-home candidate)

- **Skip message**: `"line interrupt at cvc match — candidate re-home to ctc_interrupts_test.cpp"` (ctc_test.cpp:1885).
- **VHDL fact**: `zxnext.vhd` line interrupt pulse fires when the ULA
  vertical counter matches the configured line register.
- **Why it stays**: Agent G's branch wires `line_int_pulse` into the
  fabric correctly, but observing the match requires driving the ULA
  line counter through real frame timing, which needs a full
  `Emulator` fixture rather than the bare `Im2Controller` + stubs
  used in `ctc_test.cpp`. Phase 3b left this as `skip()` with a
  "candidate re-home" reason string (clarified by Phase 3b critic
  fix `7216ea3`).
- **Disposition**: follow-up backlog item — re-home to
  `test/ctc_interrupts/ctc_interrupts_test.cpp` using the existing ULA
  step-to-line helper. Post-move, `ctc_test.cpp` reaches the target
  3-skip envelope (the original Section 12 triage expectation).

### ULA-INT-06 — Line 0 → c_max_vc wrap (re-home candidate)

- **Skip message**: `"line 0 → c_max_vc wrap — candidate re-home to ctc_interrupts_test.cpp"` (ctc_test.cpp:1892).
- **VHDL fact**: `zxnext.vhd` line interrupt wraps when the
  configured line register is `0`: the pulse fires at `c_max_vc`, not
  at `vc=0`.
- **Why it stays**: same reason as ULA-INT-04; needs a live
  `c_max_vc` observable from the `Ula` class which is only available
  inside `Emulator`.
- **Disposition**: same re-home follow-up as ULA-INT-04.

## 17 non-skip / non-check rows — classification

### 5 B/D/E-merged comments (unobservable at this abstraction)

| ID         | Category | Covered by | Reason |
|------------|----------|------------|--------|
| CTC-CW-11  | **D** (structurally unreachable) | CTC-SM-* family | API-level `write()` is discrete; VHDL `iowr` rising-edge strobe is an internal pipeline signal with no behavioural consequence at this abstraction. |
| IM2C-13    | **B** (VHDL-internal clock edge) | IM2C-10/11/12 | `im_mode` update on falling CLK_CPU edge vs rising — single-threaded tick emulator cannot distinguish; the observable final `im_mode` value is already asserted. |
| PULSE-09   | **B** (VHDL-internal bus pin) | PULSE-08 | `o_BUS_INT_n` is an internal pin at the top-level wiring boundary; already covered by PULSE-08 which tests the same `pulse_int_n AND im2_int_n` composition at the controller output. |
| IM2W-09    | **B** (cross-domain pipeline) | IM2W-03 | `isr_serviced` edge detection across clock domains is purely a pipeline stage; already covered outcome-equivalent by IM2W-03 (latch clear is observable). |
| NR-C5-02   | **E** (redundant) | CTC-NR-02 | Explicitly tagged "duplicate" in the pre-existing test file — identical assertion to CTC-NR-02 which is now passing. |

### 10 re-home placeholders → `test/ctc_interrupts/ctc_interrupts_test.cpp`

Now live as passes in the new companion suite (see TRACEABILITY-MATRIX §"Companion integration suite"):

| Plan ID    | Integration home                                                         |
|------------|--------------------------------------------------------------------------|
| ULA-INT-01 | Section 12 — ULA interrupt at HC/VC match                                |
| ULA-INT-02 | Section 12 — Port 0xFF bit 6 disable (via NR 0x22 bit 2 workaround)      |
| ULA-INT-03 | Section 12 — ula_int_en NOT-of-disable mirror                            |
| ULA-INT-05 | Section 12 — NR 0x22 line_interrupt_en                                   |
| NR-C0-04   | Section 13 — NR 0xC0 read-byte composition                               |
| NR-C4-02   | Section 13 — NR 0xC4 line int enable write bit                           |
| NR-C4-03   | Section 13 — NR 0xC4 read-byte composition                               |
| NR-C6-02   | Section 13 — NR 0xC6 read-byte composition                               |
| ISC-09     | Section 14 — Legacy NR 0x20 read mixed-status                            |
| ISC-10     | Section 14 — Legacy NR 0x22 read reset-state invariant (missing read handler) |

### 2 JOY re-home → emulator/input layer

| Plan ID | Reason |
|---------|--------|
| JOY-01  | `ctc_zc_to(3) → joy_iomode_pin7` toggle is wired in `Emulator::Emulator()` (Agent H `bdf32a7`), but the observable is the keyboard-row input bit, which belongs in a keyboard/input integration test rather than the CTC+Interrupts suite. |
| JOY-02  | Same — the NR 0x0B `joy_iomode_0` guard is a read-only condition at `Emulator` level; re-homes alongside JOY-01. |

## 84 un-skipped rows — section-grouped coverage

Every row below flipped from `skip()` to `check()` during Phase 3b
(commit `fb1f38a`). Each is now a live pass against the new
`Im2Controller` + `Im2Client` fabric.

| Section (rows)          | Count | Un-skipped by agent |
|-------------------------|------:|---------------------|
| 6 CTC-NR (CTC-NR-02)    |     1 | B (folded-in `Ctc::get_int_enable()`) |
| 7 IM2C (01..12, 14)     |    13 | A (RETI/RETN/IM decoder) |
| 8 IM2D (01..11)         |    11 | B (device SM) |
| 8 IM2D (12)             |     1 | F (dma_int integration) |
| 9 IM2P (01..10)         |    10 | B (daisy chain + IEI/IEO) |
| 10 PULSE (01..08)       |     8 | C (pulse fabric + machine timing) |
| 11 IM2W (01..08)        |     8 | D (wrapper edge detect + int_unq) |
| 12 ULA-INT (07, 08)     |     2 | B (priority ordering facts) |
| 12 ULA-INT (09)         |     1 | C (ULA EXCEPTION map) |
| 13 NR-C (01, 03)        |     2 | E (NR 0xC0 handler) |
| 13 NR-C4-01             |     1 | E |
| 13 NR-C5-01             |     1 | E |
| 13 NR-C6-01             |     1 | E |
| 13 NR-C8-01             |     1 | E |
| 13 NR-C9-01             |     1 | E |
| 13 NR-CA-01             |     1 | E |
| 13 NR-CC/CD/CE-01       |     3 | F (DMA int enable masks) |
| 14 ISC (01..07)         |     7 | E (NR C8/C9/CA clear plumbing) |
| 14 ISC-08               |     1 | D (re-set race under pending clear) |
| 15 DMA (01, 02, 03, 05, 06) | 5 | F |
| 16 UNQ (01, 02, 03)     |     3 | E (NR 0x20 int_unq) |
| 16 UNQ (04, 05)         |     2 | D (wrapper bypass + status set) |
| **Total**               |  **84** | |

(84 un-skip + 5 remaining skips + 5 B/D/E comments + 10 re-home placeholders + 2 JOY re-home = 106 = original skip count. Accounting closes.)

## Audit methodology notes

- **VHDL source**: all citations verified against
  `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/device/{im2_control,im2_device,im2_peripheral,peripherals,ctc,ctc_chan}.vhd`
  and `zxnext.vhd`.
- **Test files**: `test/ctc/ctc_test.cpp` (2662 lines post-Phase-3),
  `test/ctc_interrupts/ctc_interrupts_test.cpp` (441 lines, new).
- **Plan file**: `doc/design/TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md` (Section A triage table is the row-by-row canonical disposition).
- **Execution state**: 128 check() pass + 5 skip() + 10 cross-file pass = 143 live assertions for the 128+15 rows that still have runtime representation; 17 comment rows are static source documentation of the disposition.
- **False-pass audit (re-check)**: zero. No row encodes buggy C++ as oracle; all 84 un-skipped rows assert the VHDL behaviour implemented by the Phase-2 agents, critiqued independently before merge.
- **False-fail audit (re-check)**: zero. The CTC-CH-01 "known fail" from the 2026-04-15 audit (ch3→ch0 wrap in `Ctc::handle_zc_to()`) was fixed before the Phase-0 triage (now passing); no new fails were introduced by the session.
- **Lazy-skip audit**: the 2026-04-15 audit flagged CTC-CW-11, CTC-NR-02, CTC-NR-04 as the "lazy-skip trio". Of these:
  - CTC-NR-02 was **un-skipped** (Agent B added `Ctc::get_int_enable()`).
  - CTC-CW-11 became a **D-comment** (structurally unreachable at this abstraction).
  - CTC-NR-04 **stays skip** with user-deferred review-later disposition.
- **New-state skip audit**: the 5 remaining skips each carry an explicit one-line reason string (verified by Phase 3b critic pass `7216ea3` which tightened ULA-INT-04/06 wording to reflect re-home candidate status).

## Flagged follow-up backlog (non-blocking)

1. **Re-home ULA-INT-04 and ULA-INT-06** to `ctc_interrupts_test.cpp` (small; brings `ctc_test.cpp` to `131/128/0/3`, matching the original ≤3 skip envelope).
2. **Port 0xFF bit 6 write-path doesn't mirror to `ula_int_disabled_`** — VHDL `zxnext.vhd:3615-3620` drives `port_ff_reg(6)` from `OUT 0xFF` writes. ULA-INT-02 in the integration suite currently uses the NR 0x22 bit 2 mirror as a workaround.
3. **NR 0x22 read handler missing** — `NextReg::read()` fallback returns raw `regs_[0x22]`, not the VHDL composed byte (`zxnext.vhd:5991-5992`). ISC-10 asserts only the reset-state invariant.
4. **Legacy `im2_.raise(Im2Level::DMA)` cleanup** — left in place at `emulator.cpp:1333-1335` for save-state wire-format stability (DMA is a "victim" not a fabric source per VHDL). Schedule for a future cleanup wave.
5. **NMI subsystem** (per `memory/project_nmi_fragmented_status.md`) — unblocks NR-C0-02 + DMA-04 in `ctc_test.cpp`, Copper ARB-06 in `copper_test.cpp`, and is a prerequisite for Task 8 Multiface.
6. **Alignment notes from Phase 3b critic** (observationally equivalent today, re-visit if CPU timing changes):
   - `ULA EXCEPTION` gate uses `(im_mode_ != 2)` via Agent A's decoder latch, not the Z80's live IM mode. Likely equivalent once the CPU decodes the `IM 2` instruction.
   - `dma_delay_ctrl_` is held between `on_m1_cycle()` calls; `tick()` reads it mid-stream. VHDL has purely combinational `o_dma_delay`. Our model collapses edges.

---

**End audit. No critical defects. Recommendations:**

1. Re-home ULA-INT-04/06 next session to close out `ctc_test.cpp` at the original 3-skip envelope.
2. Promote CTC-NR-04 to `WONT` once the Requirements DB lands and the WONT-sweep runs.
3. NMI-subsystem work is the single follow-up that would clear both remaining F-keep skips (NR-C0-02 + DMA-04) plus Copper ARB-06 plus unblock Task 8 (Multiface).
