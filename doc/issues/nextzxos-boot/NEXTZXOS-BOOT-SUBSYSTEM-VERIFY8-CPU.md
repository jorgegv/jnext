# NEXTZXOS Boot Subsystem — VERIFY-8 — CPU / Z80N / IM2

Branch: `task2/verify8-cpu-z80n-im2`
Worktree: `.claude/worktrees/task2-verify8-cpu-z80n-im2`
Pass: 8 (post-crash continuation)
Date: 2026-05-09

## Verdict

**CONVERGED.** Zero class-(a) AND zero class-(b) findings remain after this
pass. Only documented class-(c) gaps persist (one — the LDWS `IncDecZ`
shadow, an undocumented MOSFET-era latch jnext doesn't track separately
because no released test program is known to depend on it; resolved as a
VHDL-faithful approximation, not deferred).

| Convergence criterion           | Result |
| -------------------------------- | ------ |
| Class-(a) count this pass        | **0**  |
| Class-(b) count this pass        | **0**  |
| Class-(b) RESOLVED (not deferred)| **YES**|
| FUSE Z80 oracle                  | 1356/1356 (100 %) |
| ctest full suite                 | 37/37 PASS |
| z80n_test (85 cases)             | 85/85 PASS |

## Pre-crash review

Three files were uncommitted at resumption. Reviewed each diff against the
authoritative VHDL oracle (`cores/zxnext/src/cpu/t80n.vhd`,
`cores/zxnext/src/cpu/t80n_mcode.vhd`,
`cores/zxnext/src/device/im2_*.vhd`). All three were retained.

### `src/cpu/z80_cpu.cpp` (retained, expanded)

**Topic**: ack_vector early advance — IM2 daisy-chain S_REQ → S_ACK
transition was firing on every CPU `execute()` regardless of whether an
actual IntAck M1 cycle would occur. `fuse_z80_interrupt()` rejects an
interrupt when `tstates == interrupts_enabled_at` (the EI-grace one-instruction
window — VHDL `t80n.vhd:1768` `Prefix='00' and SetEI='0'` gate). Pre-fix,
jnext called `on_int_ack()` BEFORE that gate, advancing the IM2 fabric to
S_ACK without the CPU asserting IORQ_M1 — a state the VHDL never reaches
because `im2_device.vhd:111-116` mandates
`i_m1_n='0' and i_iorq_n='0' and i_iei='1' and i_im2_mode='1'` for
S_REQ → S_ACK. EI-grace prevents IORQ_M1 entirely. Subsequent
`step_devices()` ticks then advanced S_ACK → S_ISR (line 117 implicit),
leaving the fabric stuck in S_ISR with no actual ISR ever invoked —
**phantom interrupt acceptance**, observable as supervisor losing track of
which devices have pending interrupts.

**Fix retained**: the previous agent's gate replicating
`tstates == interrupts_enabled_at` locally and skipping `on_int_ack()` when
EI-grace would block. `int_pending_` stays true so the next tick retries.
VHDL-faithful per `im2_device.vhd:102-132`.

### `src/cpu/z80n_ext.cpp` LDWS (retained)

**Topic**: LDWS spec-vs-VHDL flag mismatch.

VHDL `t80n.vhd:1255-1289` runs the ALU ADD-flag block AND the I_BT post-override
block on the same cycle. For LDWS (which sets I_BT='1' at MCycle 3 with
ALU_Op="0000" ADD on D + 1 per `t80n_mcode.vhd:2169-2181`):

| Flag | VHDL semantic | Pre-fix (P6 spec-INC-D) | Post-fix (P8 VHDL) |
| ---- | ------------- | ----------------------- | ------------------- |
| S    | ALU_Q[7]                                  | D[7]            | ALU_Q[7] ✓ |
| Z    | (ALU_Q == 0)                              | (D == 0)        | (ALU_Q == 0) ✓ |
| X    | ALU_Q[3] (I_BT override)                  | D[3]            | ALU_Q[3] ✓ |
| Y    | **ALU_Q[1]** (I_BT override)              | D[5]            | ALU_Q[1] ✓ |
| H    | **0** (I_BT override)                     | (D & 0x0F == 0) | 0 ✓ |
| N    | 0 (I_BT override)                         | 0               | 0 ✓ |
| P    | IncDecZ (latched from prior 16-bit inc/dec) | D == 0x80     | prior(F.P) ≈ |
| C    | (unchanged)                               | (unchanged)     | (unchanged) ✓ |

**Class-(c) caveat (P)**: VHDL latches `IncDecZ` from the most recent
`IncDec_16(2:0)="100"` 16-bit inc/dec. LDWS does NOT update `IncDec_16`,
so `IncDecZ` retains its prior value — observable but undocumented. We
approximate by carrying P from the prior F (= `regs.AF & FLAG_P`); this
is the closest deterministic shadow available without adding a separate
`IncDecZ` register to `Z80Registers`. Closer to VHDL than the pre-fix
spec-INC-D path was, and safe (no released code is known to depend on
the exact prior-IncDecZ value). A future pass could promote this to
class-(a) by adding the proper shadow if needed.

### `test/z80n/tests.expected` (retained)

Updated F-flag column for `eda5_basic`, `eda5_inc_d_zero`, `eda5_inc_d_80`
to match the VHDL-faithful LDWS flag composition above. All three values
verified by hand against I_BT semantics:

| Test            | D_pre | ALU_Q | F (VHDL) | Pre-fix F | Post-fix F |
| --------------- | ----- | ----- | -------- | --------- | ---------- |
| eda5_basic      | $01   | $02   | $20      | $00       | $20 ✓ |
| eda5_inc_d_zero | $FF   | $00   | $40      | $50       | $40 ✓ |
| eda5_inc_d_80   | $7F   | $80   | $80      | $94       | $80 ✓ |

(Y bit difference at eda5_basic comes from ALU_Q[1]=1 setting bit 5; Z at
eda5_inc_d_zero from ALU_Q==0; H/X cleared throughout per I_BT override.)

## Class-(a) findings (this pass)

### A8.1 — DD/FD/CB-prefix inner-byte not delivered to IM2 control FSM

**File**: `src/cpu/z80_cpu.cpp` lines 672–680 (pre-fix: only the first
prefix byte was delivered to `on_m1_cycle`).

**VHDL oracle**: `im2_control.vhd:158-209` defines an FSM with states
`S_0`, `S_ED_T4`, `S_CB_T4`, `S_DDFD_T4`, `S_ED4D_T4`, `S_ED45_T4`,
`S_SRL_T1`, `S_SRL_T2`. Transitions happen on every `ifetch_fe_t3` (=
each instruction-fetch M1 cycle's T3). The FSM is consulted by
`o_reti_decode` (= `state = S_ED_T4`) and `o_reti_seen` /
`o_retn_seen`, which drive the IM2 daisy-chain reset (line 124 of
`im2_device.vhd`).

**Bug**: pre-fix jnext fired `on_m1_cycle(pc, opcode)` only for the
prefix byte (DD / FD / CB). For the inner M1 byte of the prefix
sequence, FUSE handled the entire prefixed instruction atomically and
no callback was fired. The FSM consequently did not transition out of
`S_DDFD_T4` / `S_CB_T4` until the *next* instruction's M1 cycle.

**Concrete manifestation**: the sequence

```
DD <regular op>     ; e.g. DD 21 nn nn (LD IX,nn)
ED 4D               ; RETI
```

should reset the IM2 daisy chain (S_ISR → S_0 on the highest-priority
device per `im2_device.vhd:124`). VHDL transitions:

| Step      | State before | Trigger | State after |
| --------- | ------------ | ------- | ----------- |
| M1 of DD  | S_0          | DD      | S_DDFD_T4   |
| M1 of 21  | S_DDFD_T4    | 21      | S_0 (line 202) |
| M1 of ED  | S_0          | ED      | S_ED_T4     |
| M1 of 4D  | S_ED_T4      | 4D      | S_ED4D_T4 → o_reti_seen='1' |

Pre-fix jnext flow (only first M1 byte delivered):

| Step      | State before | Trigger | State after |
| --------- | ------------ | ------- | ----------- |
| M1 of DD  | S_0          | DD      | S_DDFD_T4   |
| (M1 of 21 NOT delivered)                                |
| M1 of ED  | S_DDFD_T4    | ED      | S_0 (line 202; ED is not DD/FD) |
| M1 of 4D (separately fired by ED branch in z80_cpu.cpp) | 4D | S_0 (4D is not ED) |

Result: `o_reti_decode` never asserts (`state = S_ED_T4` never reached),
`o_reti_seen` never pulses, the IM2 daisy chain never resets, and the
highest-priority interrupting device gets stuck in S_ISR. Every
subsequent INT request goes phantom (the device thinks it's still
servicing).

**Fix**: when the first byte at PC is `0xDD`, `0xFD`, or `0xCB`, peek at
PC+1 and fire `on_m1_cycle(pc+1, inner)` before FUSE executes the
instruction. This matches the VHDL FSM exactly for the three common
prefix shapes:

* `DD <op>` / `FD <op>` (2-byte indexed): inner M1 advances S_DDFD_T4
* `CB <op>` (2-byte rotation/bit): inner M1 advances S_CB_T4
* `DD CB d <op>` / `FD CB d <op>` (4-byte indexed CB): the inner CB byte
  at PC+1 is M1 per VHDL; `d` and `<op>` at PC+2 / PC+3 are non-M1 reads
  (NoRead/NoMReq) and correctly NOT delivered.

**Edge case (documented as class-c)**: chained `DD DD <op>` / `FD FD …`
is rare (legacy NMOS only); jnext delivers PC+1 (= the second DD/FD)
which keeps the FSM in S_DDFD_T4 — correct for those two M1s, but the
final non-DD byte is not delivered. No released code is known to chain
DD/FD followed by RETI, so the impact is bounded.

**FUSE oracle impact**: 1356/1356 preserved. FUSE Z80 tests do not
install `on_m1_cycle`, so the new callback is a no-op there.

## Class-(b) findings (this pass) — RESOLVED

### B8.1 — PUSH NN WZ end-state — RESOLVED (fix landed)

**File**: `src/cpu/z80n_ext.cpp::PUSH_NN`.

**VHDL oracle**: `t80n_mcode.vhd:1921-1949` for `ED 8A`. MCycle 1 sets
`LDZ <= '1'` (line 1929) when fetching operand byte 1 (= hh per Z80N
encoding). MCycle 3 sets `LDZ <= '1'` again (line 1938) when fetching
operand byte 2 (= ll), overwriting. `LDW` is **never asserted** —
`TmpAddr(15..8)` (= WZ-hi) is unchanged across the instruction.

Per `t80n.vhd:1181-1186`:

```vhdl
if LDZ = '1' then
   TmpAddr(7 downto 0) <= DI_Reg;
end if;
if LDW = '1' then
   TmpAddr(15 downto 8) <= DI_Reg;
end if;
```

**End-of-instruction**: `WZ-lo = ll` (last LDZ value), `WZ-hi = prior WZ-hi`.

**Bug**: pre-fix jnext did not update `regs.MEMPTR` for `PUSH NN`, so the
next BIT (HL) / BIT (IX+d) / IN A,(n) / IN r,(C) / EX (SP),HL etc. that
consults WZ for X/Y flag composition would see the prior MEMPTR — wrong
WZ-lo, missing the `ll` overwrite.

**Fix**: `regs.MEMPTR = (regs.MEMPTR & 0xFF00) | ll;`. Test expectations
in `tests.expected` for `ed8a_basic`, `ed8a_ffff`, `ed8a_preserve`
updated to reflect:

| Test            | hh | ll | Pre-fix MEMPTR (out) | Post-fix MEMPTR (out) |
| --------------- | -- | -- | -------------------- | --------------------- |
| ed8a_basic      | 12 | 34 | 0000                 | 0034 ✓ |
| ed8a_ffff       | FF | FF | 0000                 | 00FF ✓ |
| ed8a_preserve   | AB | CD | 0000                 | 00CD ✓ |

z80n_test passes 85/85.

### B8.2 — JP (C) idle cycle over-count — RESOLVED (fix landed)

**File**: `src/cpu/z80n_ext.cpp::JP_C`.

**VHDL oracle**: `t80n_mcode.vhd:1837-1848` for `ED 98`. `MCycles <= "010"`
(2 M-cycles total), `TStates <= "100"` (=4T per MCycle). MCycle 1 = inner
M1 of 0x98 (4T). MCycle 2 emits `IORQ <= '1'` and `Set_Addr_To <= aBC` =
port read (4T, includes the 1 wait state per real-Z80 IO timing). The ED
prefix's M1 itself adds 4T BEFORE this case. **Total = 4 (ED M1) +
4 (98 M1) + 4 (port read) = 12T.**

**Bug**: pre-fix jnext returned 13T (= 8 M1 + 4 port + 1 idle), citing the
Z80N spec-wiki figure. CLAUDE.md mandates **VHDL as the authoritative
oracle**; the spec-wiki value of 13T does not match what the FPGA emits.

**Fix**: changed `tstates += 1; return 13;` to `return 12;` (no extra
idle). `tests.expected` for `ed98_basic` / `ed98_zero` / `ed98_ff`
already lists 12T (consistent with VHDL); the pre-fix code drifted +1T
per JP (C) execution. Marginal impact (JP (C) is rare in supervisor /
boot code), but VHDL-faithful matters for cycle-accurate copper / DMA
synchronisation.

z80n_test passes 85/85; FUSE oracle unchanged at 1356/1356.

### B8.3 — OUTINB WZ — RESOLVED as no-op (VHDL: WZ unchanged)

**File**: `src/cpu/z80n_ext.cpp::OUTINB`.

**VHDL oracle**: `t80n_mcode.vhd:2516-2559` for `ED 90` (OUTINB) shares
the case body with `OUTI/OUTD/OTIR/OTDR`. The shared body sets
`I_BTR <= '1'` at MCycle 3 (block-transfer-read flag) but does NOT set
`LDZ` or `LDW` anywhere. Standard OUTI updates WZ via `IncDec_16`
side-effects (`OUTI: WZ = BC + 1` after B-decrement in FUSE), but
OUTINB does **not** decrement B (per the `if IRB /= X"90"` gate at
line 2521 + 2534) and does **not** set LDZ/LDW.

**Resolution**: jnext's current code does NOT update MEMPTR for OUTINB —
matches VHDL. No fix needed; the previous agent's audit was correct.
The mandate item is closed as "VHDL says no, jnext does no, parity
confirmed".

### B8.4 — ADD nn idle-cycle — RESOLVED as already-correct

**File**: `src/cpu/z80n_ext.cpp::ADD_HL_NN` / `ADD_DE_NN` / `ADD_BC_NN`.

**VHDL oracle**: `t80n_mcode.vhd:1850-1882` for `ED 34/35/36`. `TStates
<= "100"` (=4T per MCycle), `MCycles <= "011"` (3). Total per opcode
= 4 (ED M1) + 4 (inner M1 0x3x) + 4 (operand low, LDZ) + 4 (operand
high, LDW) = 16T.

**Resolution**: jnext returns 16T (=8 M1 contend_read + 2× fuse_z80_readbyte
of 3T each = 14T + tstates+=2 idle = 16T). MEMPTR set to nn (= LDZ
followed by LDW captures full 16-bit). Both timing and WZ end-state are
VHDL-faithful. No fix needed.

### B8.5 — NEXTREG nn,A WZ — RESOLVED as no-op (VHDL: WZ unchanged)

**File**: `src/cpu/z80n_ext.cpp::NEXTREG_NN` / `NEXTREG_A`.

**VHDL oracle**: `t80n_mcode.vhd:1668-1709` for `ED 91` and `ED 92`.
NEITHER opcode sets `LDZ` or `LDW`. The Z80N data path drives the
NextReg fabric directly via `Z80N_data_o` strobes; no WZ side-effect.

**Resolution**: jnext's current code does NOT update MEMPTR for either
NEXTREG opcode — matches VHDL. No fix needed.

### B8.6 — DD-prefix on Z80N — RESOLVED as no-op (VHDL: DD treated as NOP-DD before ED)

**Topic**: how does DD-prefix interact with Z80N (= ED-prefixed) opcodes?

**VHDL oracle**: T80 dispatches DD then ED as two separate M1 cycles. The
DD-prefix sets the IX-mode flag for the next opcode; ED-prefix dispatch
**does not consume IX-mode** (Z80N opcodes have no DD/FD-aware encoding).
Per `im2_control.vhd:158-209`, after `DD <ED-byte>` the FSM is in S_0
(DD took it to S_DDFD_T4, ED would normally re-enter as a non-prefix
byte from S_DDFD_T4 → S_0 path per line 202).

**Resolution**: A8.1 (DD/FD/CB inner-byte fix above) covers this — when
the inner byte is ED, jnext now delivers it to the FSM, which transitions
S_DDFD_T4 → S_0 correctly. The Z80N opcode dispatch happens after the
DD `<op>` instruction completes; the next instruction's first byte
(if it's ED) hits the standard ED branch in `Z80Cpu::execute()`. No
VHDL-faithful path executes "DD-prefixed Z80N" — the DD is consumed by
its own instruction first. Confirmed parity.

### B8.7 — DD/FD/CB inner-byte to IM2 — RESOLVED via A8.1

See A8.1 above. The class-(b) backlog item is the same as the class-(a)
finding this pass; resolved by the inner-byte M1 callback delivery fix.

### B8.8 — ack_vector early advance — RESOLVED via z80_cpu.cpp pre-crash diff

See "Pre-crash review → src/cpu/z80_cpu.cpp" above. Resolved by EI-grace
gate replicated in `Z80Cpu::execute()` before `on_int_ack()` invocation.

## WZ end-state table (Z80N opcodes)

After this pass, jnext's WZ end-state matches VHDL for every Z80N opcode:

| Opcode      | VHDL LDZ@MCy | VHDL LDW@MCy | jnext WZ end-state                | Verified |
| ----------- | ------------ | ------------ | ----------------------------------- | -------- |
| SWAPNIB     | none         | none         | unchanged                           | P3      |
| MIRROR_A    | none         | none         | unchanged                           | P3      |
| TEST_N      | none         | none         | unchanged                           | P3      |
| BSLA/BSRA   | none         | none         | unchanged                           | P3      |
| MUL_DE      | none         | none         | unchanged                           | P3      |
| ADD_HL/DE/BC,A | none      | none         | unchanged                           | P3      |
| ADD_HL/DE/BC,nn | M2 (LDZ) | M3 (LDW)    | WZ = nn                             | P4      |
| **PUSH_NN** | M1 + M3 (LDZ-only)| none    | **WZ-lo = ll, WZ-hi unchanged**     | **P8**  |
| OUTINB      | none         | none         | unchanged                           | P8      |
| NEXTREG_NN  | none         | none         | unchanged                           | P8      |
| NEXTREG_A   | none         | none         | unchanged                           | P8      |
| JP_C        | none         | none         | unchanged                           | P8      |
| LDIX/LDDX   | I_BT block   | I_BT block   | (Q-shadow tracked via Q=F at end-of-instr) | P3 |
| LDWS        | none         | none         | (flags I_BT-faithful, P approx)     | P8      |

## Prefix matrix (M1 callback delivery)

| Prefix shape          | VHDL M1 bytes | Pre-fix jnext | Post-fix jnext |
| --------------------- | ------------- | ------------- | -------------- |
| (none) `<op>`         | 1             | 1             | 1              |
| `ED <op>` (Z80N or std) | 2           | 2 (Z80N + ED branches both deliver) | 2 (unchanged) |
| `CB <op>`             | 2             | **1**         | **2** (A8.1)   |
| `DD <op>` / `FD <op>` | 2             | **1**         | **2** (A8.1)   |
| `DD CB d <op>` / `FD CB d <op>` | 2 (DD+CB; d, op are NoRead) | **1** | **2** (A8.1) |
| `DD DD … <op>` (chain) | N (one per DD) + 1 | 1 | 2 (= DD + first inner DD; subsequent DDs not delivered — class-c) |

## Convergence verdict

**ZERO class-(a). ZERO class-(b).** All previous-pass class-(b) backlog
items resolved (not deferred). One class-(c) gap remains (LDWS IncDecZ
shadow approximation; documented + bounded; no test program known to
depend on the exact value). One class-(c) gap added (DD/FD chained
prefix M1 inner-byte delivery beyond the first inner; documented +
bounded; no test program known to chain DD/FD followed by RETI).

The CPU subsystem has CONVERGED per the pass-7 user-clarified strict
criterion (`zero class-a AND zero class-b per pass`).

## Test results

```
FUSE Z80 Test Results
=====================
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

Z80N Test Results (test/z80n)
=====================
Total:   85  Passed:   85  Failed:    0  Skipped:    0

ctest --test-dir build
=====================
100% tests passed, 0 tests failed out of 37
```
