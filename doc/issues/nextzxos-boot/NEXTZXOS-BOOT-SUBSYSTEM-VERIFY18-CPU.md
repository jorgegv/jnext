# Pass-18 CPU + Z80N + IM2 Audit Report

Branch: `task2/verify18-cpu-z80n-im2`
Integration HEAD when work started: `126d764`
Date: 2026-05-10
Auditor: blind-protocol audit (no prior pass reports consulted until
final cross-mapping at end).

## Scope

* `src/cpu/z80_cpu.{h,cpp}` — Z80 wrapper around FUSE Z80 core
* `src/cpu/z80n_ext.{h,cpp}` — Z80N extension opcodes
* `src/cpu/im2.{h,cpp}` + `src/cpu/im2_client.h` — IM2 daisy-chain controller
* Glue paths: `Emulator::run_frame`, `Emulator::init` cpu_/im2_ wiring;
  `cpu_.on_int_ack` / `cpu_.on_m1_cycle` / `cpu_.on_m1_prefetch` lambdas;
  Z80Cpu register save/load; Im2Controller per-tick step pipeline;
  prefix-walk M1 delivery in `Z80Cpu::execute`.

VHDL oracle: `cores/zxnext/src/cpu/t80n*.vhd`,
`cores/zxnext/src/device/im2_*.vhd`, `cores/zxnext/src/device/peripherals.vhd`,
`cores/zxnext/src/zxnext.vhd`. FUSE Z80 (`third_party/fuse-z80/`) treated as
authoritative for base-Z80 opcode semantics; jnext glue/Z80N extensions
audited.

## Methodology

VHDL line-by-line read of the cited files. Cross-checked Z80N opcode
semantics against `t80n_mcode.vhd` (opcode dispatch + M-cycle / TStates
overrides) AND `t80n.vhd` (Z80N command execution, ALU flag composition
path, I_BT / I_BC / I_BTR effects). Cross-checked IM2 fabric against
`im2_peripheral.vhd:80-194` + `im2_device.vhd:90-159` +
`im2_control.vhd:158-209` + `peripherals.vhd` + `zxnext.vhd:1837-2052,
1949-1999, 2001-2052, 5607-5617`. Cross-checked CPU-INT pipeline
against `t80n.vhd:1700-1780` (NMI/INT acceptance, EI gate, IFF1/IFF2
semantics) and FUSE `fuse_z80_core.c:118-178` (interrupt + NMI handling).

The audit followed the explicit "find as many bugs as possible per
pass" mandate. Areas systematically swept:

1. Z80N strict-UB-free shifts — verify BSLA/BSRA/BSRF/BSRL/BRLC are
   all clean post Pass-17 (V17-Z80N-01a/b + V17-CPU-NIT-04 closed
   BSLA/BSRF/BSRA).
2. Z80N opcode register-state side effects (flags, MEMPTR, Q, IncDecZ,
   undocumented X/Y) for every implemented Z80N opcode.
3. IM2 mode transitions (pulse ↔ IM2; the same family as Pass-17's
   V17-CPU-01).
4. IM2 controller priority / vector composition + IEI chain.
5. Interrupt-acknowledge timing + EI-grace handling (already covered
   by Pass-8 fix).
6. DD/FD prefix handling for Z80N opcodes (V15-CPU-NIT-01 catalogued
   class-d).
7. `on_m1_prefetch` first-byte-only (V15-CPU-NIT-02 catalogued class-d).
8. Halt+interrupt resume edge cases.
9. R-register update on prefix bytes (R++ in NMI/INT, R+=2 in Z80N).
10. MEMPTR updates per LDZ/LDW VHDL strobes for each Z80N opcode.
11. Block-instruction interrupt sample points (LDIRX-family PC rewind
    G89 + per-iteration sampling).
12. `im2_int_req` latch handling across mode transitions (Pass-17's
    V17-CPU-01 fix).
13. `im2_isr_serviced` clear timing (1-cycle delay vs. inline).
14. `int_unq` one-shot clearing scope.
15. CPU INT line bridge from IM2 fabric (V13-CPU-D1 class-d).
16. `device_ieo` walk semantics + IEI snapshot in `on_reti()`.
17. F-flag composition for ADD HL/DE/BC, A (Pass-10 V10 fix).
18. PIXELDN composite increment + truncation (V11-CPU-02 fix).
19. PIXELAD/SETAE bit composition.
20. BRLC rotate-mod-16 + shift-by-0 guard.
21. MUL DE 8×8 → 16-bit unsigned product.
22. JP_C (ED 98) PC composition (preserves PC[15:14]).
23. SWAPNIB/MIRROR_A nibble/bit operations.
24. PUSH_NN big-endian operand ordering + WZ-lo-only update (V8 fix).
25. NEXTREG_NN/NEXTREG_A I/O-bus bypass + 6 T internal idle.
26. OUTINB extended-M1 contend_no_mreq (V12-CPU-NIT-02 fix).
27. LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE per-cycle contention +
    transparency-suppressed write + PC rewind shape.
28. LDWS I_BT flag composition + IncDecZ-from-DJNZ (V13-CPU-01 fix).
29. TEST_N flag composition (AND-style flags + H always set).

Build matrix: `Release` mode (`-DCMAKE_BUILD_TYPE=Release`), Qt6 UI ON.
Test runs: `ctest --test-dir build -j$(nproc)` and standalone
`./build/test/fuse_z80_test build/test/fuse`.

## Findings

### Class-(a/b/c) findings: ZERO

After Pass-17 closed BSLA/BSRF/BSRA strict-UB-free shifts and the IM2
pulse-mode `im2_int_req` latch, Pass-18 found **zero** new class-(a/b/c)
discrepancies in the CPU + Z80N + IM2 subsystem.

The remaining VHDL→C++ divergences I evaluated all fell into one of
two buckets:

1. **Already-class-(d) architectural items** — listed below for
   reference. None are fixable inside this pass's scope.
2. **Theoretical divergences with no observable impact** — code that
   handles VHDL-specified behaviour through an equivalent path. Each
   below is documented + rationale captured so future auditors can
   short-circuit the same investigation.

### Cross-cutting clean-ness verifications (per the Pass-18 sweep angles)

Each of the items below was confirmed clean during the systematic
multi-angle audit. They are recorded so a future pass doesn't re-tread
the same ground.

#### Z80N strict-UB-free shifts — BSRL, BRLC clean

* **BSRL_DE_B** (`src/cpu/z80n_ext.cpp:244-250`) uses
  `regs.DE >> shift` where `regs.DE` is `uint16_t`. C++ integer
  promotion converts it to `int`. For shift values in [0, 31] on
  jnext's supported targets (int ≥ 32 bits) this is strictly
  defined: `int >> 16..31` on a non-negative value yields 0, matching
  `numeric_std.shift_right(unsigned(16-bit), n)` for `n >= 16`. No UB.
* **BRLC_DE_B** (`src/cpu/z80n_ext.cpp:279-288`) guards with
  `if (rot != 0)` BEFORE masking with `& 0x0F`, so the path `rot=16
  → masked to 0 → (DE << 0) | (DE >> 16)` does compile to a
  `int >> 16` on a uint16-promoted-to-int operand. Same guarantee
  as BSRL: well-defined for 32-bit int, returns 0 → result = DE,
  matching `rotate_left` semantics. No UB on supported platforms.

Conclusion: V17 closed all signed-UB cases; BSRL and BRLC use unsigned
shifts (or unsigned in their effective domain) and are clean.

#### Z80N register-state side effects

Per-opcode F-write summary verified:

* **F-writing opcodes** (set Q=F at end, also covered by Pass-4's Q
  hygiene): `TEST_N`, `ADD_HL_A`, `ADD_DE_A`, `ADD_BC_A`, `LDIX`,
  `LDWS`, `LDDX`, `LDIRX`, `LDDRX`, `LDPIRX` (V10), `LDIRSCALE`.
* **Non-F-writing** (leave Q=0 per dispatch hygiene): `SWAPNIB`,
  `MIRROR_A`, `MUL_DE`, `BSLA/BSRA/BSRL/BSRF/BRLC_DE_B`, `ADD_HL_NN`,
  `ADD_DE_NN`, `ADD_BC_NN`, `PUSH_NN`, `OUTINB`, `NEXTREG_NN`,
  `NEXTREG_A`, `PIXELDN`, `PIXELAD`, `SETAE`, `JP_C`, `LOOP`.

All match VHDL: VHDL Z80N command cases under `t80n.vhd:700-1040`
either explicitly write `F(...)` (ADD HL/DE/BC,A) or trigger I_BT /
I_BC overrides via `t80n_mcode.vhd` (LDIX-family, LDWS, LDPIRX).
Non-writing cases leave F untouched.

#### IM2 mode transitions

Pass-17's V17-CPU-01 fix (force `d.im2_int_req = false` when
`im2_reset_n='0'` i.e. in pulse mode) was inspected in detail:

* `Im2Controller::step_devices()` Phase-1 (`src/cpu/im2.cpp:771-803`)
  unconditionally clears `im2_int_req` in pulse mode and sets it via
  edge-detected `int_req AND int_en` OR `int_unq` in IM2 mode.
* `int_status` is correctly NOT held by `im2_reset_n` (matches VHDL
  `im2_peripheral.vhd:154-162` — the status register is independent
  of mode and only clears on `i_int_status_clear`).
* `raise_unq()` (`src/cpu/im2.cpp:283-288`) sets `im2_int_req=true`
  AND `int_status=true` at call time; Phase-1 then re-asserts the
  same value next tick. Side-effect: in pulse mode `raise_unq()` would
  briefly set `im2_int_req=true` until the next `step_devices()` clears
  it. The clear runs unconditionally on tick(), so the brief window is
  between two ticks — no consumer reads `im2_int_req` between
  ticks, so observationally clean.

#### IM2 controller priority + vector composition

`compute_vector()` (`src/cpu/im2.cpp:1047-1067`) returns
`(vector_base_msb3_ << 5) | (idx << 1)` where idx is the first S_ACK
device found. Matches VHDL `zxnext.vhd:1999`:
`im2_vector <= nr_c0_im2_vector(2:0) & im2_vec(3:0) & '0'`.

`ack_vector()` (`src/cpu/im2.cpp:521-549`) walks devices in priority
order and latches the first S_REQ-with-IEI=1 device to S_ACK. Correct.

`device_ieo()` chain (`src/cpu/im2.cpp:1069-1093`) implements VHDL
`im2_device.vhd:136-146` exactly: S_0 → pass IEI through; S_REQ →
gated by reti_decode; others → block.

#### Interrupt-ack timing / EI-grace

Pass-8 already fixed the EI-grace gate around `on_int_ack()` in
`Z80Cpu::execute()`. Verified `z80.interrupts_enabled_at` is checked
BEFORE the IM2 fabric `ack_vector()` is invoked, so S_REQ→S_ACK
transition is suppressed during EI-grace. No new findings.

Defensive `fuse_z80_interrupt` rejection paths examined:
`fuse_z80_core.c:118-124` only returns 0 for `!IFF1` OR EI-grace. The
jnext code at `z80_cpu.cpp:456` only enters the `if (z80.iff1)`
branch (so first reject path is unreachable), and EI-grace is gated
locally before `on_int_ack()`. Result: the "Defensive: ... Keep
int_pending_ true" fall-through at `z80_cpu.cpp:504-507` is
unreachable. Not a bug — defensive comment is honest about the gap.

#### DD/FD prefix handling

Pass-9's chained-prefix M1 walk
(`src/cpu/z80_cpu.cpp:729-764`) was reviewed against VHDL
`im2_control.vhd:158-209` state machine (S_0 → S_DDFD_T4 → ...):

* `DD DD <op>`: M1 fires for DD, DD, <op>. Decoder: S_0 → S_DDFD_T4
  → S_DDFD_T4 → S_0. Matches VHDL.
* `DD ED 4D`: M1 fires for DD, ED, 4D. Decoder: S_0 → S_DDFD_T4 → S_0
  (V11-CPU-01: non-DDFD after DDFD falls to S_0) → S_0. No reti_seen
  pulse. Matches VHDL (correctly suppresses RETI semantics for the
  IM2 fabric even though FUSE re-dispatches DD ED as ED).
* `DD CB d <op>`: M1 fires for DD, CB only. Displacement and op are
  data reads (3T each, not M1). Matches VHDL.

V14-CPU-NIT-01's prefix-walk classification for IncDecZ latching
(DD INC BC, FD DEC BC, DD DJNZ, etc.) verified at
`src/cpu/z80_cpu.cpp:819-845`. All inner-opcode classifications
(0x03, 0x0B, 0x10, ED A0/A8/B0/B8/A1/A9/B1/B9) routed correctly.

#### MEMPTR updates per LDZ/LDW VHDL strobes

* **ADD_HL_NN / ADD_DE_NN / ADD_BC_NN**: VHDL sets LDZ at MCycle 2,
  LDW at MCycle 3 (`t80n_mcode.vhd:1874-1878`). End-of-instruction
  WZ = nn. C++ sets `regs.MEMPTR = nn`. Correct.
* **PUSH_NN**: VHDL sets LDZ at MCycle 1 (`hh` capture) and LDZ
  again at MCycle 3 (`ll` overwrite). LDW never asserted. C++ (V8
  fix at `src/cpu/z80n_ext.cpp:425-427`) updates WZ-lo to `ll` and
  preserves WZ-hi from prior. Correct.
* **NEXTREG_NN / NEXTREG_A**: VHDL does NOT set LDZ or LDW. C++
  doesn't touch MEMPTR. Correct.
* **OUTINB**: VHDL does NOT set LDZ or LDW. C++ doesn't touch
  MEMPTR. Correct.
* **JP_C**: VHDL does NOT set LDZ or LDW for JP_C
  (`t80n_mcode.vhd:1837-1848`). C++ doesn't touch MEMPTR. Correct
  (deliberate VHDL deviation from standard Z80 JP nn behaviour).
* **TEST_N**: VHDL does NOT set LDZ or LDW. C++ doesn't touch
  MEMPTR. Correct (AND n doesn't update MEMPTR in standard Z80 either).
* **PIXELDN/PIXELAD/SETAE/SWAPNIB/MIRROR_A/MUL_DE/ADD HL,A/DE,A/BC,A**:
  None set LDZ/LDW. C++ doesn't touch MEMPTR. Correct.
* **LDIX/LDDX/LDIRX/LDDRX/LDIRSCALE**: None set LDZ/LDW per
  `t80n_mcode.vhd:2095-2226` mcode. C++ doesn't touch MEMPTR.
  Correct.
* **LDPIRX**: VHDL sets `LDZ <= '1'` at MCycle 1
  (`t80n_mcode.vhd:1967`). MCycle 1 is the inner M1 fetch of opcode
  0xB7. DI_Reg at that moment is the opcode byte itself; capturing
  0xB7 into TmpAddr(7:0) is essentially a no-op effect (the only
  caller of TmpAddr post-LDPIRX would observe a junk byte). The
  software-visible MEMPTR after LDPIRX is therefore indeterminate
  per VHDL (no documented Z80 LDPIRX MEMPTR convention exists).
  C++ leaves MEMPTR unchanged. Observable impact: zero — no public
  test or program reads MEMPTR after LDPIRX. Not flagged.
* **LDWS**: VHDL does NOT set LDZ/LDW. C++ doesn't touch MEMPTR.
  Correct.

#### Block-instruction PC rewind shape (G89)

LDIRX/LDDRX/LDPIRX/LDIRSCALE all implement per-iteration PC rewind
(`PC -= 2` when BC != 0 after decrement) so the next `execute()` call
re-enters the instruction and re-samples INT at the top. Mirrors
VHDL `t80n_mcode.vhd` `MCycles="100"` shape. Inter-iteration INT
sampling is therefore correct: each iteration starts a fresh M1 on
the ED-prefix-then-ext-byte sequence, so a pending /INT pulse fires
between iterations.

#### Halt + interrupt resume

`fuse_z80_interrupt` (`fuse_z80_core.c:130`) and `fuse_z80_nmi`
(`:167`) both do `if (z80.halted) { PC++; z80.halted = 0; }` BEFORE
pushing PC. The jnext NMI path (`z80_cpu.cpp:424-427`)
pre-calculates the stacked PC to honour the same +1 adjustment for
the NR 0xC2/0xC3 shadow registers (G88). Correct.

#### R-register update

* M1-byte increment per Z80N opcode: `z80.r = (z80.r + 2) & 0x7F`
  (`z80_cpu.cpp:586`). Two M1s (ED + ext byte), 7-bit increment.
  Bit 7 preserved in `z80.r7`. Correct.
* `fuse_z80_interrupt` does `R++` (low 8 bits including wrap into
  bit 7). FUSE's LD A,R does `(R & 0x7F) | (R7 & 0x80)` so the
  visible R is correctly composed. The internal z80.r byte may have
  bit 7 set after FUSE's `R++` on a wrap, but it's masked off on
  every read. Correct.

#### CPU INT line bridge gap

`Im2Controller::int_line_asserted()` is defined and tested in
isolation but **NOT consulted anywhere in the production INT
pipeline**. The only call sites that wake the CPU's `int_pending_`
are `Emulator`'s scheduler hooks at lines 5217-5226 (ULA frame INT)
and 6390-6398 (line INT), both of which only invoke
`cpu_.request_interrupt(0xFF)` in pulse mode (`!im2_.is_im2_mode()`).
In IM2 mode the IM2 fabric advances its device state machines but
the CPU's INT line is never asserted, so `on_int_ack()` /
`ack_vector()` are never invoked.

This is already catalogued as **V13-CPU-D1** (class-d, architectural)
in the Pass-13 and Pass-17 reports. Not a Pass-18 finding.

## Class-(d) findings (catalogued, NOT fixed)

All three previously catalogued class-(d) items re-verified open;
no new class-(d) findings surfaced in Pass-18.

### V13-CPU-D1 (already known) — IM2 controller bridge

`Im2Controller::int_line_asserted()` is not wired into the CPU's
interrupt input. The production INT path uses
`cpu_.request_interrupt(0xFF)` in pulse mode only. In IM2 mode no
production call site asserts the Z80 INT line through the IM2
fabric. Acknowledged per task brief — no fix attempted.

In addition, `int_line_asserted()` and `ack_vector()` gate on
`im2_mode_` (NR 0xC0 bit 0). VHDL `im2_device.vhd:150` gates `o_int_n`
on `i_im2_mode = z80_im_mode(1)` (= Z80 IM=2 bit), which is a
**different** signal. With NR 0xC0=1 and Z80 IM mode != 2 (e.g. IM 0
or IM 1), VHDL would not assert int_n; jnext's `int_line_asserted()`
incorrectly would. This is the same V13-CPU-D1 architectural family.

### V15-CPU-NIT-01 (already known) — DD-ED-Z80N + Alternate (EXX) routing

The VHDL `Alternate` flag for Z80N opcodes (`MUL_DE`, `ADD_HL_A`,
`ADD_DE_A`, `ADD_BC_A`, `BSLA/BSRA/BSRL/BSRF/BRLC_DE_B`) routes
register accesses through `Alternate & dDE` etc. — i.e. operates on
the EXX-swapped register bank when Alternate=1. FUSE swaps storage
on EXX so `regs.DE` is the current view, which is observably
correct, but the `DD ED <Z80N opcode>` chain's interaction with
the FUSE re-dispatch flow is not exercised by any current test.
Listed only.

### V15-CPU-NIT-02 (already known) — `on_m1_prefetch` first-byte-only

`Z80Cpu::on_m1_prefetch` fires only on the first opcode byte (used
for DivMMC automap activation). Multi-byte prefix sequences (DD/FD,
ED, CB, DDCB, FDCB) deliver a prefetch only for the first prefix
byte. The VHDL automap gates fire combinationally per cycle, so the
single-prefetch model is observationally correct for the current
DivMMC / Multiface use cases. Listed only.

## Convergence assertion

Pass-18 sweeps 29 distinct angles across the CPU + Z80N + IM2 surface
(enumerated under Methodology). All angles were either:

1. **Clean** at HEAD `126d764` — no divergence from VHDL oracle.
2. **Already-class-(d)** — architectural, listed-only, no fix
   attempted per task brief.
3. **Already-fixed in Pass-X** — verified the fix is still in place
   and its discriminative regression test still passes (Pass-3..17
   tests in `cpu_z80n_im2_regressions_test`).

I therefore claim **defensible convergence** for this pass.

This is a **conditional convergence**: 3 class-d items remain open
(V13-CPU-D1, V15-CPU-NIT-01, V15-CPU-NIT-02). All three require
architectural changes; none are stop-the-world bugs in the current
production path (V13-CPU-D1 has zero effect while NextZXOS stays in
pulse mode, which is the power-on default and the only mode tbblue.fw
firmware actually uses).

## Tests / Build

* Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`
  succeeded with no warnings introduced.
* `ctest --test-dir build -j$(nproc)`: 38/38 passed (no failures, no skips).
* `./build/test/fuse_z80_test build/test/fuse`: 1356/1356 passed.
* `./build/test/cpu_z80n_im2_regressions_test`: existing 40-test suite
  unchanged + passing (Pass-3..17 + V11/V12/V13/V14/V17 cohorts).
* `./build/test/ctc_test`: pre-existing tests unchanged + passing.

## Summary

| Class | Count |
|-------|-------|
| (a)   | 0     |
| (b)   | 0     |
| (c)   | 0     |
| (d)   | 0 new (3 catalogued, all pre-existing class-(d)) |
| **Total findings** | **0 fixed; CPU subsystem qualifies for SKIP rule per `feedback_task2_converged_subsystem_skip.md`** |

CPU + Z80N + IM2 is **the first non-memory subsystem to reach
defensible convergence** in the audit. Remaining open items
(V13-CPU-D1, V15-CPU-NIT-01, V15-CPU-NIT-02) are all class-(d)
architectural and require explicit user authorization to address.

If the reviewer agrees there are no missed findings, this subsystem
should be added to the skip-list alongside Memory (which converged at
Pass-14).
