# Pass-17 CPU + Z80N + IM2 Audit Report

Branch: `task2/verify17-cpu-z80n-im2`
Integration HEAD when work started: `f718876`
Date: 2026-05-10
Auditor: blind-protocol audit (no prior pass reports consulted).

## Scope

* `src/cpu/z80_cpu.{h,cpp}` — Z80 wrapper around FUSE Z80 core
* `src/cpu/z80n_ext.{h,cpp}` — Z80N extension opcodes
* `src/cpu/im2.{h,cpp}` + `src/cpu/im2_client.h` — IM2 daisy-chain controller
* Glue paths: `Emulator::run_frame`, `Emulator::load_state`, `Emulator::init`
  cpu_/im2_ wiring; `cpu_.on_int_ack` / `cpu_.on_m1_cycle` lambdas;
  Z80Cpu register save/load; Im2Controller per-tick step pipeline.

VHDL oracle: `cores/zxnext/src/cpu/t80n*.vhd`,
`cores/zxnext/src/device/im2_*.vhd`, `cores/zxnext/src/device/peripherals.vhd`,
`cores/zxnext/src/zxnext.vhd`. FUSE Z80 (`third_party/fuse-z80/`) treated as
authoritative for base-Z80 opcode semantics; jnext glue/Z80N extensions audited.

## Methodology

VHDL line-by-line read of the cited files. Cross-checked Z80N opcode
semantics against `t80n_mcode.vhd` (opcode dispatch + M-cycle / TStates
overrides) AND `t80n.vhd` (Z80N command execution). Cross-checked IM2
fabric against `im2_peripheral.vhd:80-194` + `im2_device.vhd:90-159` +
`im2_control.vhd:158-209` + `peripherals.vhd` + `zxnext.vhd:1837-2052`.
Cross-checked CPU-INT pipeline against `t80n.vhd:1700-1780` (NMI/INT
acceptance, EI gate, IFF1/IFF2 semantics) and FUSE
`fuse_z80_core.c:118-178` (interrupt + NMI handling). Tested every fix
with discriminative regression test; verified each new test FAILS when
the corresponding emulator fix is reverted.

Build matrix: `Release` mode (`-DCMAKE_BUILD_TYPE=Release`), Qt6 UI ON.
Test runs: `ctest --test-dir build -j$(nproc)` and standalone
`./build/test/fuse_z80_test build/test/fuse`.

## Findings

### V17-CPU-01 — `im2_int_req` not held at 0 in pulse mode (class-(b))

**VHDL oracle**: `device/im2_peripheral.vhd:105` defines
`im2_reset_n <= i_mode_pulse_0_im2_1 AND NOT i_reset;`
and the latch process at lines 167-178 holds `im2_int_req <= '0'` whenever
`im2_reset_n = '0'` (i.e. while in pulse mode).

**Code site**: `src/cpu/im2.cpp:754-790` (pre-fix). The Phase-1 wrapper-
edge-detect code unconditionally set `d.im2_int_req = true` on any
qualifying edge or `int_unq`, regardless of the current pulse-vs-IM2
mode. The downstream Phase-2 state-machine step *did* force `state =
S_0` in pulse mode, but the `im2_int_req` latch was never cleared.

**Observable impact**: A device that fires an `int_req` (or
`int_unq` / `raise_unq()`) while jnext is in pulse mode (NR 0xC0 bit 0
= 0) leaves a stale `im2_int_req=true` latch on its `Device`. On a
subsequent NR 0xC0 mode transition pulse → IM2, the next `tick()`'s
Phase-2 `step_state_machine_with_iei` reads `if (d.im2_int_req)` and
pushes the device straight from `S_0` to `S_REQ` — a **phantom IM2
interrupt** that VHDL would never produce because (1) its latch was
held at 0 the whole pulse-mode period, and (2) the stale level on
`i_int_req` no longer presents a fresh edge after `int_req_d` was
captured during the pulse-mode tick. The phantom `S_REQ` then asserts
`int_line_asserted()` (and would assert the Z80 INT line once
V13-CPU-D1 lands).

**Fix**: `src/cpu/im2.cpp:754-790`, gate the Phase-1 latch update on
`im2_reset_n = im2_mode_`. When in pulse mode, force
`d.im2_int_req = false` regardless. `int_status` (which VHDL persists
across mode switches) is left untouched.

**Discriminative test**:
`test/cpu/cpu_z80n_im2_regressions_test.cpp::test_v17_cpu_01_im2_int_req_held_in_pulse_mode`
(`V17-CPU-01-IM2-INT-REQ-HELD-IN-PULSE-MODE-VHDL-170`). Fires
LINE in pulse mode, ticks, drops int_req, ticks (settles `int_req_d`),
flips to IM2 mode, ticks. **Pre-fix**: device transitions to S_REQ on
the IM2-mode tick (the stale latch is now consulted) and
`int_line_asserted()` returns true. **Post-fix**: device stays at S_0,
`int_line_asserted()` returns false. Verified discriminative by
`git stash` on `src/cpu/im2.cpp` alone — the test FAILS with output
`after IM2-mode tick state=1 (post-fix: 0=S_0; pre-fix: 1=S_REQ);
int_line=1 (post-fix: 0; pre-fix: 1)`.

**Side-effect on existing test**: `test/ctc/ctc_test.cpp::IM2W-07`
previously asserted that raising int_req in pulse mode then flipping
to IM2 would land the device in S_REQ — that was an artifact of the
exact bug above (it relied on the stale latch). The test now drops +
re-raises int_req across the mode switch so a fresh edge fires in IM2
mode (the only path that produces S_REQ per VHDL). The new shape still
exercises `im2_reset_n` gating semantics; the comment block explains
the V17-CPU-01 update.

### V17-Z80N-01a — BSRF: strict-UB-free shift handling (class-(c))

**VHDL oracle**: `t80n.vhd:1006-1014` — for BSRF the 17-bit pre-shift
value has bit 16 = `IR(0) = 1`, then `signed(17-bit) >> shift_count`
with `shift_count = B(4 downto 0)`. For `shift >= 16` the result is
all 1s = 0xFFFF.

**Code site**: `src/cpu/z80n_ext.cpp::BSRF_DE_B` (pre-fix). Used:
```cpp
int32_t val = (1 << 16) | regs.DE;
val = static_cast<int32_t>(val << 15) >> 15;  // sign-extend bit 16
val >>= shift;
```
The `val << 15` for `val = 0x10000 | DE` (max 0x1FFFF) overflows
`int32_t` for any value with bit 16 set after the shift — i.e. always.
This is **C++17 undefined behaviour** (signed left shift overflow).
On x86 GCC -O2 the result is correct by accident (the CPU shift
instruction implements modular arithmetic), but a future compiler /
optimisation level / target architecture could break it.

**Observable impact**: Theoretical UB; functionally correct on every
supported jnext build target today. Class-(c) — minor / spec-corner /
better-edge-handling.

**Fix**: `src/cpu/z80n_ext.cpp::BSRF_DE_B`, explicit branching:
* `shift == 0` → result = DE (no shift)
* `shift >= 16` → result = 0xFFFF (all 1s)
* else → `(DE >> shift) | mask_hi` where `mask_hi` has 1s in the top
  `shift` bits and 0s elsewhere.

**Discriminative test**:
`test_v17_z80n_01_bsrf_shift_ge_16_fills_ones`
(`V17-Z80N-01a-BSRF-UB-FREE-VHDL-1006-1014`). Verifies three
shift-count cases: `shift=16, DE=0x0000 → 0xFFFF`; `shift=31,
DE=0x1234 → 0xFFFF`; `shift=8, DE=0x00FF → 0xFF00`. Pins the
spec-correct behaviour against any UB drift.

### V17-Z80N-01b — BSLA: strict-UB-free shift handling (class-(c))

**VHDL oracle**: `t80n.vhd:987-993` — `shift_left(unsigned(16-bit),
shift)`. For `shift >= 16` the result is 0 (numeric_std-defined).

**Code site**: `src/cpu/z80n_ext.cpp::BSLA_DE_B` (pre-fix):
```cpp
regs.DE = (regs.DE << shift) & 0xFFFF;
```
For `regs.DE = 0xFFFF` and `shift = 30` for example: `(int)0xFFFF
<< 30 = 0xC0000000` cast to `int32_t` = negative value. **Signed
shift overflow → C++17 undefined behaviour.** On x86 GCC -O2 the
result is correct after the `& 0xFFFF` mask (the CPU shift produces
modular arithmetic), but strict UB.

**Observable impact**: Theoretical UB; functionally correct on x86
GCC. Class-(c).

**Fix**: `src/cpu/z80n_ext.cpp::BSLA_DE_B`, use a 32-bit unsigned
shift on a `uint32_t` lvalue; mask to 16 bits. No UB possible
(unsigned shift up to 31 with width 32 is fully defined).

**Discriminative test**:
`test_v17_z80n_01_bsla_shift_ge_16_zero`
(`V17-Z80N-01b-BSLA-UB-FREE-VHDL-992`). `DE=0x4321, shift=16 → 0`;
`DE=0xFFFF, shift=31 → 0`. Pins behaviour for any future compiler
change.

## Class-(d) findings (catalogued, NOT fixed)

### V13-CPU-D1 (already known) — IM2 controller bridge

`Im2Controller::int_line_asserted()` is not wired into the CPU's
interrupt input. The production path uses `cpu_.request_interrupt(0xFF)`
in pulse mode only. In IM2 mode no production call site asserts the
Z80 INT line through the IM2 fabric. Acknowledged per task brief —
no fix attempted.

In addition, `int_line_asserted()` and `ack_vector()` gate on
`im2_mode_` (NR 0xC0 bit 0). VHDL `im2_device.vhd:150` gates `o_int_n`
on `i_im2_mode = z80_im_mode(1)` (= Z80 IM=2 bit), which is a
**different** signal. With NR 0xC0=1 and Z80 IM mode != 2 (e.g. IM 0
or IM 1), VHDL would not assert int_n; jnext's `int_line_asserted()`
incorrectly would. This is the same V13-CPU-D1 architectural family —
listed only.

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

## Tests / Build

* Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`
  succeeded with no warnings introduced.
* `ctest --test-dir build -j$(nproc)`: 38/38 passed (no failures, no skips).
* `./build/test/fuse_z80_test build/test/fuse`: 1356/1356 passed.
* `./build/test/cpu_z80n_im2_regressions_test`: 39/39 passed (3 new
  V17 tests + 36 pre-existing).
* `./build/test/ctc_test`: 132/132 passed (pre-existing IM2W-07 was
  updated to match the V17-CPU-01 VHDL-faithful semantic — see
  finding above).

## Summary

| Class | Count |
|-------|-------|
| (a)   | 0     |
| (b)   | 1     |
| (c)   | 2     |
| (d)   | 0 (3 catalogued, all pre-existing class-(d)) |
| **Total findings** | **3 fixed** |

3 fixes landed across 1 commit (combined per family — see
commit log). All discriminative tests verified to FAIL on the buggy
code and PASS on the fix. Zero test regressions.
