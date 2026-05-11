# NextZXOS Boot — CPU Subsystem (Z80 + Z80N + IM2) — Independent Review

**Reviewer**: Task 2 independent reviewer (CPU subsystem)
**Date**: 2026-05-09
**Worktree**: `.claude/worktrees/task2-cpu-z80n-im2-reviewer/`
**Branch**: `task2/cpu-z80n-im2-reviewer`
**Commit reviewed**: `65b5918` (`fix(z80n): include ED-prefix M1 fetch in Z80N T-state returns`)
**Original report**: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-CPU.md`

---

## Verdict

**APPROVE-WITH-NITS.**

The single Z80N T-state systematic bug fix is correct and well-grounded.
Every changed value matches the published Spectrum Next timing table
(`wiki.specnext.dev/Extended_Z80_instruction_set`) and the per-cycle
T-state model implied by `t80n_mcode.vhd` (default M1=4T, others=3T,
overrides via `TStates <= "..."`, plus the I_BT/I_BC/I_BTR/No_BTR
machinery for block-op repetition + IORQ wait-state cycles for
JP (C)/OUTINB).

The PUSH imm byte-order verification — the prime G46(b) suspect — was
re-derived independently three ways (VHDL trace, test fixture, big-
endian operand convention per the wiki) and confirms the agent's claim:
**PUSH imm is correct**, NOT the source of the G46(b) "3 missing PUSHes
/ 3 extra POPs" stack divergence.

Every test passes after rebuild from clean: FUSE Z80 1356/1356, Z80N
85/85, ctest 36/36.

The nits are: (1) the INT_PULSE_TSTATES open item is **machine-aware in
the IM2 fabric but hard-coded in Z80Cpu** — the agent flagged but did
not fix this, and the failing target machine is `next` so it IS active;
(2) three `tests.expected` T-state values are 1-off from the
spec-canonical values the agent set (the fixture is wrong, the agent
is right, but the runner ignores T-states behind a `NOTE` so it doesn't
matter for the passing test count) — worth a fixture update; (3) one
commentary nit on BRLC's UB analysis (the `& 0x0F` mask + `if rot != 0`
short-circuit make the UB unreachable, which the agent's note sort of
says but slightly muddles).

None of the nits block merge. The fix is a pure timing correction and
has no behavioural consequence for memory/registers/flags, so it
cannot regress anything that the FUSE and Z80N tests cover.

---

## 1. T-state change reassessment per opcode

I cross-referenced every changed value against three independent
sources (in this order):

1. **VHDL `t80n_mcode.vhd`** MCycles + per-MCycle TStates overrides,
   evaluated against the t80n timing model (default MCycle 1 = 4T,
   default other MCycles = 3T; per-cycle overrides via `TStates <=
   "..."`; +1 IO wait state when `IORQ <= '1'` because t80na.vhd:184
   instantiates the core with `IOWait => 1`).
2. **The Spectrum Next wiki**
   `https://wiki.specnext.dev/Extended_Z80_instruction_set` (which the
   agent cites).
3. **The in-tree test fixture** `test/z80n/tests.expected`.

### Agent's table — re-verified per opcode

| Opcode (mnem)            | Pre-fix | Post-fix | VHDL | Wiki | tests.expected | Verdict |
|--------------------------|---------|----------|------|------|----------------|---------|
| SWAPNIB (ED 23)          | 4       | 8        | 8    | 8    | 8              | ✓       |
| MIRROR A (ED 24)         | 4       | 8        | 8    | 8    | 8              | ✓       |
| TEST n (ED 27)           | 7       | 11       | 11   | 11   | **12**         | ✓ (fixture wrong by 1) |
| BSLA/BSRA/BSRL/BSRF/BRLC | 4       | 8        | 8    | 8    | 8              | ✓       |
| MUL D,E (ED 30)          | 4       | 8        | 8    | 8    | 8              | ✓       |
| ADD HL/DE/BC,A           | 4       | 8        | 8    | 8    | 8              | ✓       |
| ADD HL/DE/BC,nn          | 12      | 16       | 16   | 16   | 16             | ✓       |
| PUSH nn (ED 8A)          | 23      | 23       | 23   | 23   | 23             | ✓ (unchanged) |
| OUTINB (ED 90)           | 10      | 16       | 16   | 16   | 16             | ✓       |
| NEXTREG nn,nn (ED 91)    | 16      | 20       | 20   | 20   | 20             | ✓       |
| NEXTREG nn,A (ED 92)     | 13      | 17       | 17   | 17   | **16**         | ✓ (fixture wrong by 1) |
| PIXELDN (ED 93)          | 4       | 8        | 8    | 8    | 8              | ✓       |
| PIXELAD (ED 94)          | 4       | 8        | 8    | 8    | 8              | ✓       |
| SETAE (ED 95)            | 4       | 8        | 8    | 8    | 8              | ✓       |
| JP (C) (ED 98)           | 12      | 13       | 13   | 13   | **12**         | ✓ (fixture wrong by 1) |
| LDIX (ED A4)             | 13      | 16       | 16   | 16   | —              | ✓       |
| LDWS (ED A5)             | 14      | 14       | 14   | 14   | —              | ✓ (unchanged) |
| LDDX (ED AC)             | 13      | 16       | 16   | 16   | —              | ✓       |
| LDIRX (ED B4) repeat     | 13      | 21       | 21   | 21/16 | 21+16=37 ✓    | ✓       |
| LDIRX (ED B4) terminal   | 13      | 16       | 16   | 21/16 | 21+16=37 ✓    | ✓       |
| LDDRX (ED BC)            | 13      | 21/16    | 21/16| 21/16 | 37 (BC=2)     | ✓       |
| LDPIRX (ED B7)           | 13      | 21/16    | 21/16| 21/16 | 37 (BC=2)     | ✓       |
| LDIRSCALE (ED B6)        | 13      | 21/16    | 21/16| —     | 37 (BC=2)     | ✓       |
| LOOP (ED FB)             | 4       | 8        | 8 (NOP fall-through) | — | — | ✓ |

**Method for VHDL re-derivation** (representative cases):

- **PUSH imm (ED 8A)**: MCycles=6 in body. Cycles: 4(M1) + 3 + 3 + 3 + 3 + 3 = 19T. Plus ED-prefix M1 = 4T. Total = **23T** ✓.
- **NEXTREG nn,nn (ED 91)**: MCycles=5. Cycles: 4 + 3 + 3 + 3 + 3 = 16T. Plus ED M1 = 4T. Total = **20T** ✓.
- **NEXTREG nn,A (ED 92)**: MCycles=4. Cycles: 4 + 3 + 3 + 3 = 13T. Plus ED M1 = 4T. Total = **17T** ✓.
- **JP (C) (ED 98)**: MCycles=2 with `TStates <= "100"` override on cycle 1; cycle 2 has IORQ. Cycles: 4 + (4+1 IO wait) = 9T. Plus ED M1 = 4T. Total = **13T** ✓.
- **LDIX/LDIRX terminal (ED A4/B4 with No_BTR=1)**: 3 MCycles fire (cycle 4 fires only when BTR_r=1). Cycles: 4 + 3 + 5 = 12T. Plus ED M1 = 4T. Total = **16T** ✓.
- **LDIRX repeating (ED B4 with BTR_r=1)**: 4 MCycles. Cycles: 4 + 3 + 5 + 5 = 17T. Plus ED M1 = 4T. Total = **21T** ✓.
- **OUTINB (ED 90)**: MCycles=3 with `TStates <= "101"` on cycle 1, cycle 3 has IORQ. Cycles: 5 + 3 + (3+1 IO wait) = 12T. Plus ED M1 = 4T. Total = **16T** ✓.
- **TEST n (ED 27)**: MCycles=2. Cycles: 4 + 3 = 7T. Plus ED M1 = 4T. Total = **11T** ✓.

Three test fixtures (`ed27_basic`=12, `ed92_basic`=16, `ed98_basic`=12)
are 1-off from the spec values. **The fixture, not the agent's fix, is
wrong.** The runner does not actually compare T-states (parked behind
a `NOTE` at `z80n_test.cpp:243-247`), so the 85/85 pass count is
unaffected. Worth filing a follow-up to align the fixture and enable
T-state comparison once the runner exposes a per-instruction tstates
counter.

**No T-state change is incorrect.** The agent's fix is sound on every
opcode it touches.

---

## 2. PUSH imm byte order verdict — CONFIRMED CORRECT

This is the most important verification of the review (top G46(b)
suspect per the prompt). I re-derived independently of the agent.

### VHDL trace (`t80n_mcode.vhd:1921-1948`)

```
when X"8A" =>
   -- PUSH VAL nn
   MCycles <= "110";   -- 6 cycles total
   case MCycle is
   when 1 => Inc_PC; LDZ;                          -- read FIRST operand byte → DI_Reg
   when 2 => IncDec_16=1111; aSP; BusB="0110";     -- SP--; BusB := DI_Reg (= first byte)
   when 3 => Inc_PC; LDZ; Write;                   -- write BusB to mem[SP]; read SECOND byte → DI_Reg
   when 4 => IncDec_16=1111; aSP; BusB="0110";     -- SP--; BusB := DI_Reg (= second byte now)
   when 5 => Write;                                -- write BusB to mem[new SP]
```

After this:
- Final SP = SP_orig − 2.
- mem[SP_orig − 1] = first operand byte (high address).
- mem[SP_orig − 2] = second operand byte (low address).

A subsequent `RET` reads low byte from mem[SP], high from mem[SP+1] →
returns to `(first << 0) | (second << 8)`? No — wait. RET uses
`PC_low = mem[SP]; PC_high = mem[SP+1]`. So:
- PC_low = mem[SP_orig − 2] = second operand byte.
- PC_high = mem[SP_orig − 1] = first operand byte.

So the value returned to is `(first << 8) | second` — i.e. the **first
byte after `ED 8A` is the HIGH byte** of the pushed value. **The
operand encoding is big-endian**, opposite to standard Z80 little-endian
operand convention.

### Wiki confirmation

Quoted directly from the Spectrum Next wiki (this review fetched it):

> The encoding of the operand of the `PUSH $im16` is unique: it is the
> only operand encoded as big-endian.

The encoding line: `PUSH $im16 — ED 8A high low`. **First byte after
ED 8A = high byte.** ✓

### Test fixture confirmation

```
ed8a_basic
  ... regs, SP=fff0 PC=8000 ...
  00 02 0 0 0 0 23                  ; tstates=23
  8000 ed 8a 12 34 -1               ; instruction stream
  ffee 34 12 -1                     ; final memory: mem[ffee]=34, mem[ffef]=12
```

Final SP=$ffee. Stack layout: mem[$ffee]=$34 (low), mem[$ffef]=$12 (high).
Value pushed = $1234 (= the literal in instruction order).

`ed8a_preserve` further verifies F is preserved (no flag effect): AF
stays at $05ff after `PUSH $cdab`. The encoded operand is `ab cd` and
the stack ends `mem[$ffee]=cd, mem[$ffef]=ab` → value pushed = $abcd.

### C++ implementation (`z80n_ext.cpp:205-217`)

```cpp
case Z80NOpcode::PUSH_NN: {
    auto regs = cpu.get_registers();
    uint8_t hh = cpu.memory().read(regs.PC);            // first  → high
    uint8_t ll = cpu.memory().read(regs.PC + 1);        // second → low
    regs.PC = (regs.PC + 2) & 0xFFFF;
    regs.SP = (regs.SP - 2) & 0xFFFF;
    cpu.memory().write(regs.SP + 1, hh);                // high at SP+1
    cpu.memory().write(regs.SP, ll);                    // low at SP
    cpu.set_registers(regs);
    return 23;
}
```

For `ED 8A 12 34`: hh=$12, ll=$34. SP=$fff0−2=$ffee. write(SP+1=$ffef, $12) ✓.
write(SP=$ffee, $34) ✓.

**Verdict: PUSH imm byte order is CORRECT.** The C++ code matches the
VHDL, the wiki, and the test fixture. PUSH imm is **NOT** the source
of the G46(b) "3 missing PUSHes / 3 extra POPs" divergence.

---

## 3. Spot-check of "no fix needed" claims

### NEXTREG dispatch (ED 91 / ED 92)

Z80N opcodes dispatch `cpu.io().out(0x243B, reg); cpu.io().out(0x253B, val)`.
The port handlers at `emulator.cpp:2418-2437` route both ports to the
same `nextreg_.select()` / `nextreg_.write_selected()` functions used
by direct port writes. The same `defer_cpu_nr_writes_` per-instruction
window applies (set/cleared around `cpu_.execute()` at `emulator.cpp:4414/4416`).
**Both NEXTREG-via-Z80N-opcode and NEXTREG-via-port go through identical
state machinery** — confirmed.

### IM2 fabric

Spot-checked the agent's VHDL citations:

- `im2_control.vhd:158-210` — decoder FSM. The C++ at `im2.cpp:608-689`
  walks the same states (S_0, S_ED_T4, S_ED4D_T4, S_ED45_T4, S_SRL_T1,
  S_SRL_T2, S_CB_T4, S_DDFD_T4) with the same transitions. Verified.
- `im2_control.vhd:218-227` — IM-mode bit decode. C++ at `im2.cpp:635-647`
  matches `(b4 AND b3) & (b4 AND NOT b3)` exactly. Verified.
- `im2_control.vhd:234/236` — reti_seen / retn_seen are one-cycle
  pulses. C++ raises `reti_seen_pulse_=true` once per S_ED_T4 → S_ED4D_T4
  transition, cleared by next call. Verified.
- `im2_device.vhd:136-146` — IEO daisy chain. C++ `device_ieo()`
  at `im2.cpp:990-1014` walks 0..i with `S_0 → iei`, `S_REQ → iei && reti_decode`,
  `else → false`. Verified.
- `zxnext.vhd:2017-2031` — pulse fabric. C++ `step_pulse()` at
  `im2.cpp:875-939` matches `pulse_count_end = bit5 && (machine_48_or_p3 || bit2)`.
  Verified.

### JP (C) PC composition

- VHDL `t80n.vhd:939-944` (PIXELAD section, but the same primitive
  rules apply elsewhere): `PC[13:6] := DI_Reg`, `PC[5:0] := 0`,
  `PC[15:14]` unwritten = preserved.
- C++ `z80n_ext.cpp:302`: `regs.PC = (regs.PC & 0xC000) | ((uint16_t)val << 6);`
  — preserves [15:14], writes val<<6 (= bits [13:6] = val[7:0]),
  bits [5:0] = 0 by construction.
- Test fixture `ed98_ff` (PC=$8000, BC=$ff00, port stub returns
  port>>8 = $FF): expected PC=$BFC0.
  - 0x8000 & 0xC000 = 0x8000 (preserves "10")
  - 0xFF << 6 = 0x3FC0
  - 0x8000 | 0x3FC0 = 0xBFC0 ✓.

### LDDX / LDDRX direction (HL−−, DE++, BC−−)

VHDL `t80n_mcode.vhd:2230-2256` clearly shows for ED AC | BC (LDDX,
LDDRX):
- Cycle 2: `IncDec_16 <= "1110"; -- decrement HL`
- Cycle 3: `IncDec_16 <= "0101"; -- increment DE`

The Spectrum Next wiki confirms (this review fetched it):

> LDDX: `{if HL*!=A DE*:=HL*;} DE++; HL--; BC--`
> LDDX/LDDRX advance DE by incrementing it (like LDI), while HL is
> decremented (like LDD).

C++ at `z80n_ext.cpp:350-362` (LDDX) matches: `regs.DE+=1`, `regs.HL-=1`,
`regs.BC-=1`. ✓

### PIXELAD bit composition

VHDL `t80n.vhd:939-944`:
- H = "010" & DE[15:14] & DE[10:8]
- L = DE[13:11] & DE[7:3]

C++ at `z80n_ext.cpp:275-285`:
- H = 0x40 | ((D & 0xC0) >> 3) | (D & 0x07) — i.e. "010" prefix |
  D[7:6]→bits[4:3] | D[2:0]→bits[2:0]. With D=DE[15:8]: D[7:6]=DE[15:14] ✓,
  D[2:0]=DE[10:8] ✓.
- L = ((D & 0x38) << 2) | (E >> 3) — i.e. D[5:3]→bits[7:5] | E[7:3]→bits[4:0].
  With D=DE[15:8]: D[5:3]=DE[13:11] ✓. With E=DE[7:0]: E[7:3]=DE[7:3] ✓.

Matches VHDL.

### PUSH/POP/CALL/RET/EX (SP),HL/IX/IY in FUSE base

The FUSE Z80 test suite `test/fuse/tests.expected` includes test
vectors for `c9` (RET), `cd` (CALL nn), `e3` (EX (SP),HL), `dde3` (EX (SP),IX),
`fde3` (EX (SP),IY), `c7..ff` (RST $00 / $08 / $10 / $18 / $20 / $28 /
$30 / $38), `76` (HALT), `eda0..edb9` (LDI/LDD/LDIR/LDDR/CPI/CPD/CPIR/CPDR/
INI/IND/INIR/INDR/OUTI/OUTD/OTIR/OTDR), `ed45/ed4d` (RETN/RETI),
`ed46/ed4e/ed56/ed5e/ed66/ed6e/ed76/ed7e` (IM 0/1/2 set), and
`ed47/ed4f/ed57/ed5f` (LD I,A / LD R,A / LD A,I / LD A,R). All passing
1356/1356. **No coverage gap on Z80 base instructions for boot-critical
flows.**

---

## 4. Test verification

After clean rebuild from `65b5918`:

```
$ ./build/test/fuse_z80_test build/test/fuse
FUSE Z80 Test Results
=====================
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ LANG=C ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 36
Total Test time (real) =   1.35 sec
```

FUSE 1356/1356 ✓. ctest 36/36 ✓. Z80N 85/85 (subset of ctest) ✓.

**No regressions.**

---

## 5. Coverage gaps

The agent did not actively re-verify the following Z80 base ops against
VHDL because they're delegated to the FUSE core. Every one is FUSE-
test-covered (1356/1356), so this is not a gap — but I list them
explicitly to record that I checked:

- ✓ `EX (SP), HL` (e3) — FUSE test cases present, pass.
- ✓ `EX (SP), IX/IY` (DD/FD e3) — FUSE coverage, pass.
- ✓ `RST $00 / $08 / $10 / $18 / $20 / $28 / $30 / $38` (c7..ff) — full coverage, pass.
- ✓ `RETN` IFF1 := IFF2 (ed45) — FUSE test vector, pass.
- ✓ `LD A,I` / `LD A,R` P/V := IFF2 (ed57/ed5f) — FUSE coverage, pass.
- ✓ `LDI/LDD/LDIR/LDDR` (eda0/eda8/edb0/edb8) — pass.
- ✓ `OTIR/OTDR` (edb3/edbb) — pass.
- ✓ `JR/DJNZ` taken vs not-taken — FUSE test cases for both branches,
  pass.
- ✓ `HALT` (76) — FUSE coverage; NMI-during-HALT delegated to
  `fuse_z80_nmi()` which bumps PC by +1 if HALTed before pushing
  (per `z80_cpu.cpp:391-405` comment). Validated by ctest's
  `nmi_tests` and `nmi_integration_tests` (both pass).
- ✓ Z80N `JP (C)` port C read — `z80n_ext.cpp:301` calls `cpu.io().in(regs.BC)`
  which dispatches through the standard port mux. Verified by `ed98_*` fixtures.
- ✓ Z80N `LDPIRX` source addressing — `(HL & 0xFFF8) | (E & 0x07)` per
  `z80n_ext.cpp:430`. Matches VHDL. HL not modified, DE++, BC−−. ✓
- ✓ Z80N `LDIRSCALE` — same shape as LDIRX (HL++, DE++, BC−−); the
  alternate-register addition is commented out in VHDL per agent's
  note, which the C++ correctly does not implement.

**No additional coverage gaps identified at the CPU subsystem level.**

The Z80N runner's lack of T-state comparison is a known follow-up, not
a gap in this commit's scope.

---

## 6. INT pulse window verdict

### VHDL oracle (`zxnext.vhd:2014-2033`)

```
-- duration is 32 cpu cycles for 48K and +3
-- duration is 36 cpu cycles for 128K and pentagon
...
pulse_count_end <= pulse_count(5) and (machine_timing_48 or machine_timing_p3 or pulse_count(2));
```

Pulse termination:
- **48K / +3**: bit 5 of pulse_count = 1 → terminates at count 32. Pulse width = 32 T-states.
- **128K / Pentagon / Next-default**: bit 5 AND bit 2 = 1 → terminates at count 36 (binary 100100). Pulse width = 36 T-states.

### Current jnext behaviour

`Im2Controller::step_pulse` at `im2.cpp:921-924` correctly implements
the machine-aware termination via `machine_48_or_p3_`, which is set by
`set_machine_timing_48_or_p3(bool)` per the comment at `im2.cpp:850-853`.

**However**, `Z80Cpu::execute()` at `z80_cpu.cpp:418` hard-codes the
gate that decides whether a pending interrupt has expired:

```cpp
static constexpr uint32_t INT_PULSE_TSTATES = 32;
if (int_pending_) {
    if (tstates - int_requested_at_ > INT_PULSE_TSTATES && !z80.iff1) {
        int_pending_ = false;  // missed
    } ...
}
```

This is the wall-clock window in which a software-disabled INT is
considered "missed". On 128K/Pentagon/Next-default, the real hardware
holds /INT low for **36 T-states**, so a CPU that re-enables interrupts
between T-state 32 and T-state 36 should still acknowledge — but jnext
will have already cleared `int_pending_`. The window is 4 T-states
shorter than spec.

### Boot relevance

The G46(b) target machine is `next` (the canonical Next-default
configuration). This bug IS active in the failing trace.

Whether it materially contributes to the "3 missing PUSHes / 3 extra
POPs" stack divergence between RST $08 hits #2 and #3 is hard to say
from first principles:
- An ISR re-entry after EI/RETI in a 4 T-state window is an edge case;
  the supervisor's RST $08 handler typically re-enables interrupts well
  outside that window.
- But interrupt-acknowledged-in-jnext-vs-CSpect divergence is exactly
  the class of "the same code reaches different stack states" that
  matches G46(b) symptoms.

### Recommendation

The agent **should have fixed it** as part of this branch — or at least
opened an `// TODO(machine-aware INT pulse)` annotation at the
`INT_PULSE_TSTATES` constant. Right now there is only the open-question
note in the report markdown, which is easier to lose track of.

The fix is mechanically straightforward: add a `set_int_pulse_tstates(uint32_t)`
setter to `Z80Cpu`, wire it from the same code path that calls
`Im2Controller::set_machine_timing_48_or_p3()`. (Or even better, share
a single pulse-counter source between IM2 and Z80Cpu — the duplication
is a maintenance hazard.)

**Reviewer's recommendation**: file as a follow-up commit on this
branch before merge. It's a one-screen change and would close the
machine-awareness gap completely. **Not blocking** if the agent prefers
to defer it; but if so, the `Z80Cpu::execute()` constant deserves a
loud `// FIXME` with a citation to `zxnext.vhd:2017-2031` and to
`Im2Controller::set_machine_timing_48_or_p3`, so future readers don't
duplicate the analysis.

---

## 7. Code quality

### Style / typos

- `z80n_ext.cpp:228` — comment `"M1+M1+R+IO ≈ 16 per spec"` uses an
  approximation symbol ("≈") for a value that is exactly 16 per the
  wiki/VHDL. Cosmetic; not load-bearing.
- `z80n_ext.cpp:75` — comment `"M1+M1+R(+1 internal) per spec"` — the
  "+1 internal" annotation is correct (TEST n breaks down as 4 + 4 + 3
  = 11), but the wording "+1 internal" is misleading (it's not a
  separate internal cycle). Cosmetic.
- `z80n_ext.cpp:117` — `"BSx / BRLC / MUL / ADD HL/DE/BC,A"` listed
  ambiguously in the report (`MUL D,E` is `MUL D,E` not `MUL`). Trivial.
- The agent's `BRLC >> 16 UB` analysis (`z80n_ext.cpp:107-120`,
  documented in the report Class (c) section) is technically wrong
  about *what makes the code safe* — the safety comes from the
  `if (rot != 0)` short-circuit + `rot &= 0x0F` mask making `(16 - rot)`
  always in [1, 15]. The "C integer promotion makes UB into well-
  defined zero" line is true but *unreachable* for the rot=16 path.
  Doesn't matter for correctness, but the rationale should be
  tightened in a follow-up.
- `LDWS` was already correct and is unchanged. Worth a `// LDWS:
  pre-fix value of 14 was already correct; 14 = 4+4+3+3 (M1+M1+R+W),
  consistent with the spec.` comment so future maintainers don't
  wonder why this opcode "escaped" the systematic fix. Optional.

### Off-by-one

None found. The repeating-block-op split (16 vs 21) is implemented
correctly: terminal iteration (BC == 0 after decrement) returns 16,
non-terminal returns 21 with PC rewound. Confirmed by the `edb4_basic`
fixture (BC=2 → exactly 1 repeat + 1 terminal = 21 + 16 = 37).

### Missing returns / dead code

None.

### Commit message

Excellent. Concise table of changes, clear "why" (boot relevance
called out specifically with the bank-3 wrapper at `$5B48` reference),
explicit test status, explicit non-change for PUSH imm with rationale.
Follows project style perfectly. **Approve.**

---

## Summary

- **Verdict**: APPROVE-WITH-NITS
- **Confirmed findings**: 24 (every T-state change + every "no fix"
  claim)
- **Disputed findings**: 0
- **Added findings**:
  1. Three `tests.expected` T-state values (`ed27_basic`, `ed92_basic`,
     `ed98_basic`) are off by 1 from the spec values the agent set —
     the fixture is wrong; runner ignores it.
  2. `Z80Cpu::INT_PULSE_TSTATES = 32` should be machine-aware (32 for
     48K/+3, 36 for 128K/Pentagon/Next-default). Agent flagged but
     did not fix; the failing G46(b) target machine is `next` so the
     bug IS active.
  3. Minor style nit on BRLC UB rationale (cosmetic).
- **Test status**:
  - FUSE Z80: **1356/1356** ✓
  - ctest: **36/36** ✓ (includes Z80N 85/85, mmu, nextreg, IM2/NMI,
    contention, etc.)
- **Branch HEAD**: `65b5918` (unchanged by this review)
- **PUSH imm byte order verdict**: **CORRECT**. Verified independently
  via VHDL trace, wiki encoding rule ("the only operand encoded as
  big-endian"), and test fixture. **NOT** the source of G46(b).
- **Report path**: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-CPU-REVIEW.md`

The agent's fix is correct and merge-ready. The remaining INT-pulse-
window gap is recommended as an immediate follow-up commit on this
branch (it's small, well-bounded, and closes the only loose end I
can see in the CPU subsystem at boot-critical scope).

---

## References

- VHDL: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/cpu/t80n_mcode.vhd`
- VHDL: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/cpu/t80n.vhd`
- VHDL: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/cpu/t80na.vhd`
- VHDL: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/device/im2_control.vhd`
- VHDL: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/device/im2_device.vhd`
- VHDL: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
- Spectrum Next wiki: `https://wiki.specnext.dev/Extended_Z80_instruction_set`
- Test fixture: `test/z80n/tests.{in,expected}`
- FUSE Z80 suite: `test/fuse/tests.{in,expected}`
- Original report: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-CPU.md`
