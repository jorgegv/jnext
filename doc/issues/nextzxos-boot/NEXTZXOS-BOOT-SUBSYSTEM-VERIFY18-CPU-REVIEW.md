# Pass-18 CPU + Z80N + IM2 — Independent Review

Branch: `task2/verify18-cpu-z80n-im2-reviewer`
Audit HEAD reviewed: `6877d7f` (`doc(task2-pass18-cpu): audit report — defensible-zero, CPU converged`)
Date: 2026-05-10
Reviewer: independent blind-protocol re-audit.

## Verdict

**REJECT — missed findings.**

The audit's defensible-zero claim is mostly accurate — the 29 enumerated
sweep angles were genuinely covered and the Z80N strict-UB-free family
(BSLA/BSRA/BSRF/BSRL/BRLC) plus the V17-CPU-01 IM2 pulse-mode latch are
all clean. However the reviewer's independent re-audit surfaced **2
class-(c) missed findings** that fall inside Pass-18's stated scope
("be extremely thorough", "find as many bugs as possible per pass") and
1 NIT.

If the manager prefers to defer rather than rebuild the audit chain, the
2 class-(c) items below can be down-graded to class-(d) follow-ups and
the audit can be ratified — but they are NOT pre-existing class-(d)
items and they ARE software-fixable in a constrained patch.

## Methodology

Independent re-read of:

* `src/cpu/z80_cpu.cpp` (every interrupt + Z80N + prefix-walk path)
* `src/cpu/z80n_ext.cpp` (all 31 Z80N opcodes including PIXELDN composite
  add, BSRL/BRLC UB-free spot-check, LDPIRX I_BT flag composition)
* `src/cpu/im2.cpp` (full pipeline: tick → step_pulse → step_devices
  Phase 1/2/3 → step_dma_delay; on_reti IEI snapshot; advance_decoder
  FSM; on_m1_cycle prefix walk)
* `src/cpu/im2.h` (DevIdx/Im2Level enum alignment)
* `src/core/emulator.cpp:4480-4498, 5217-5226, 6390-6398` (DMA + ULA +
  UART hookups)
* `third_party/fuse-z80/fuse_z80_core.c:118-178` (INT/NMI semantics)
* VHDL `cores/zxnext/src/cpu/t80n.vhd:700-1040, 1163-1289, 1361-1366`
* VHDL `cores/zxnext/src/cpu/t80n_mcode.vhd:1668-1707, 1837-1848,
  1921-1991, 2095-2226`
* VHDL `cores/zxnext/src/cpu/t80n_alu.vhd:155-208` (AND flag composition
  for TEST_N)
* VHDL `cores/zxnext/src/device/im2_peripheral.vhd:88-194`
* VHDL `cores/zxnext/src/device/im2_device.vhd:81-160`
* VHDL `cores/zxnext/src/zxnext.vhd:1960-2010, 2017-2044`

Tests run (Release):

* `./build/test/fuse_z80_test build/test/fuse` → 1356/1356 PASS
* `./build/test/cpu_z80n_im2_regressions_test` → 40/40 PASS
* `./build/test/ctc_test` → all groups PASS

Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release` clean.

## Findings

### V18R-CPU-01 (CLASS-c — MISSED) — `/INT` pulse-expired-after-IFF1-enable accept-bug

**File**: `src/cpu/z80_cpu.cpp:452-456`

**Code**:

```cpp
if (int_pending_) {
    if (tstates - int_requested_at_ > int_pulse_tstates && !z80.iff1) {
        int_pending_ = false;
    } else if (z80.iff1) {
        // ... accept INT ...
    }
}
```

**VHDL oracle**: `zxnext.vhd:2017-2033` — `pulse_int_n` is driven low for
32 T-states (48K/+3) or 36 T-states (128K/Pentagon/Next default). Once
the count reaches `pulse_count_end`, `pulse_int_n` returns to '1' and
the Z80 /INT pin (sampled at the rising edge of the last clock of M1)
sees a HIGH level — i.e. no INT acceptance.

**Bug**: the drop-the-INT arm requires BOTH "pulse expired" AND
"interrupts disabled". When the pulse has expired but interrupts are
ENABLED, the drop arm is skipped and the second `else if (z80.iff1)`
arm accepts the INT. Real hardware would NOT accept the INT (the /INT
line went high; the M1 sample sees a high level).

**Reachable scenario** (boot-realistic):

1. T=0: frame INT fires; `int_pending_=true`, `int_requested_at_=0`.
2. T=0..N: CPU executes an ISR with `DI` in effect (IFF1=0).
3. At each boundary while IFF1=0, the first arm checks
   `(tstates - 0) > 32 && !IFF1` — drops if expired.
4. ISR ends with `EI; RETI; …`. The boundary AFTER EI has `IFF1=1` and
   `tstates - int_requested_at_` may now be > 32 (or > 36 in
   non-48K/+3 mode).
5. With IFF1=1 the FIRST arm short-circuits (it requires `!IFF1`); the
   SECOND arm accepts the INT — but the real /INT line went high
   somewhere in the gap between the last `!IFF1` boundary and the
   current `IFF1=1` boundary. The accept is spurious.

**Why the existing test misses it**: `test/cpu/int_pulse_test.cpp`
sequence is:

```cpp
*fuse_z80_tstates_ptr() = delta;  // Jump FUSE counter
cpu.execute();                    // IFF1=0 here — drop arm fires
r.IFF1 = 1; cpu.set_registers(r);
cpu.execute();                    // IFF1=1 — but int_pending_ already
                                  //  cleared by previous call
```

The first `cpu.execute()` runs the drop arm WITH IFF1=0 at the
already-jumped-past-pulse `tstates`. So the test exercises the
"IFF1=0 across the boundary" case but never the "IFF1=1 across a
boundary where pulse has expired" case.

**Discriminative test sketch**:

```cpp
// IFF1=0, IFF=0 (in DI), delta=25 (within window for 48K).
*fuse_z80_tstates_ptr() = 25;
cpu.execute();   // NOP — IFF1=0, !pulse-expired → falls through.
// After NOP, tstates=29.
// Now run EI manually: writes IFF1=1; interrupts_enabled_at = 29.
mem.ram[1] = 0xfb;  // EI
cpu.execute();   // EI executes — tstates=33, IFF1=1,
                 //   interrupts_enabled_at=29 → ei_grace doesn't fire
                 //   at boundary because tstates>29.
// Pulse expired (33 > 32) AND IFF1=1 → bug fires; INT accepted.
// Real HW: /INT went high at T=32, no accept; PC should advance to
// the next NOP, not 0x0038.
EXPECT(cpu.get_registers().PC != 0x0038);
```

**Severity**: Class-(c). Software-fixable in 1 line:

```cpp
if (int_pending_) {
    const bool pulse_expired =
        (tstates - int_requested_at_) > int_pulse_tstates;
    if (pulse_expired) {
        int_pending_ = false;
    } else if (z80.iff1) {
        // ... accept ...
    }
}
```

Plus a discriminative regression test in
`cpu_z80n_im2_regressions_test`.

**Practical impact**: Spurious INT accepts after DI/EI windows ≥ 32T
(48K/+3) or ≥ 36T (128K/Next-default). Boot-period impact: medium —
NextZXOS supervisor uses DI/EI bracketing freely; a long bank-flip
section could push the boundary past the pulse window with IFF1=1,
firing a spurious INT into the supervisor's normal flow. The fact that
no current screenshot test catches it is not evidence of harmlessness;
it's evidence that the divergence is timing-local and doesn't poison
the screen pixels.

### V18R-CPU-02 (CLASS-c — MISSED) — `raise(Im2Level::DMA)` legacy-API DevIdx-collision pollutes `dev_[10].int_status` (= CTC7)

**Files**: `src/cpu/im2.cpp:128-138, 754-808` + `src/cpu/im2.h:14-19`
+ `src/core/emulator.cpp:4491-4493`

**VHDL oracle**: `im2_peripheral.vhd:160`:

```vhdl
int_status <= (int_req or i_int_unq) or (int_status and not i_int_status_clear);
```

The `int_status` bit is set on the rising edge of `int_req` (the
`(i_int_req AND NOT int_req_d)` pulse), gated ONLY by reset and
status-clear — NOT by `i_int_en`. In the real Next FPGA, CTC4..CTC7's
`i_int_req` is hardwired to '0' (zxnext.vhd:4092), so their
`int_status` can never rise from peripheral activity.

**Bug**: `Im2Level::DMA` has enum value 10 (counting from `FRAME_IRQ=0`
through `DMA`). `DevIdx::CTC7` also has value 10 (counting from
`LINE=0` through `CTC7`). `Im2Controller::raise(Im2Level lvl)` does:

```cpp
int i = static_cast<int>(level);
if (i >= 0 && i < N) {
    dev_[i].int_req = true;
}
```

So `raise(Im2Level::DMA)` (called from `dma_.on_interrupt` in
`emulator.cpp:4491-4493`) sets `dev_[10].int_req = true` which is
**CTC7**'s int_req in the DevIdx mapping.

Phase-1 of `step_devices()` (im2.cpp:774-808) then runs:

```cpp
const bool edge = d.int_req && !d.int_req_d;
if (edge) { d.int_status = true; }   // NOT gated by int_en (VHDL:160)
```

→ CTC7's `int_status` becomes true after every DMA completion. Now
`int_status_mask_c9()` (im2.cpp:330-341) reads CTC7's status and packs
bit 7 of NR 0xC9. Software reading NR 0xC9 sees CTC7=1 spuriously.

**Observable**:

* NR 0xC9 read after a DMA completion returns bit 7 = 1 even if CTC7
  is disabled and no CTC interrupts ever fired. VHDL would always
  return bit 7 = 0 because CTC7's peripheral input is hardwired 0.
* If software also enables CTC7 via NR 0xC5 bit 7 (unusual but legal),
  every DMA completion also promotes CTC7 to S_REQ → `pulse_int_n_`
  goes low (in pulse mode) or `im2_int_req` latches (in IM2 mode) →
  spurious interrupt fired from a phantom CTC7. The vector returned
  would be `vector_base | (10 << 1) = vector_base | 0x14`.

**Why this slipped past prior passes**: the legacy-API comment at
emulator.cpp:4486-4488 reads:

> The legacy raise(Im2Level::DMA) is left in place as a vestigial
> save/load-schema artifact — Im2Level::DMA maps to DevIdx::ULA via
> to_devidx() (harmless because the legacy API path uses its own
> pending view keyed by Im2Level index) and the legacy mask bit is
> unused by any live code path.

This statement is wrong on two counts:

1. `raise(Im2Level::DMA)` does NOT use `to_devidx()` — it indexes
   `dev_[]` directly by the Im2Level value (im2.cpp:135). The
   `to_devidx()` mapping (DMA→ULA) applies only to the (not currently
   wired) DevIdx-based propagation in to/from converters, not to
   `raise()` itself.
2. The Phase-1 wrapper edge-detect runs for ALL `dev_[i]` indices
   regardless of which API set `int_req` — so `dev_[10].int_status`
   gets set whichever entry point flipped `int_req`.

**Severity**: Class-(c). Two-line fix in
`emulator.cpp:4491-4493`:

```cpp
dma_.on_interrupt = [this]() {
    // No-op: DMA is a victim of INT, not an IM2 priority source;
    // dma_int_pending() / dma_delay() handle the VHDL semantic
    // (vhdl:2003-2008). Do NOT call im2_.raise() here — the legacy
    // Im2Level::DMA value collides with DevIdx::CTC7 in dev_[10]
    // and pollutes CTC7's int_status.
};
```

Plus a discriminative regression test asserting:
`raise(Im2Level::DMA); tick(); int_status_mask_c9() == 0x00`.

**Practical impact**: Boot-time, NextZXOS reads NR 0xC9 to dispatch
CTC interrupts. A spurious CTC7 bit could lead it to clear a status
it never set, or worse, to dispatch to a phantom CTC7 ISR (if the
status drove fall-through dispatch logic). Verifying boot-time
observable impact requires a screenshot regression for NextZXOS's
CTC dispatch path; no such test exists today.

### V18R-CPU-NIT-01 (NIT — DOWNGRADED FROM CLASS-c) — LDPIRX MEMPTR-low not updated to opcode byte 0xB7

**File**: `src/cpu/z80n_ext.cpp:872-946` (LDPIRX)

**VHDL oracle**: `t80n_mcode.vhd:1962-1991`. MCycle 1 sets `LDZ <= '1'`
(line 1967). Per `t80n.vhd:1181-1182`:

```vhdl
if LDZ = '1' then
   TmpAddr(7 downto 0) <= DI_Reg;
end if;
```

`TmpAddr` is the MEMPTR register. At MCycle 1 / TState 3 the inner-M1
fetch's `DI_Reg` is the opcode byte (0xB7 for LDPIRX). So VHDL writes
MEMPTR_lo = 0xB7 after LDPIRX.

The audit's report acknowledges the divergence (under "MEMPTR updates
per LDZ/LDW VHDL strobes") and dismisses it as observationally zero —
no public test or program reads MEMPTR after LDPIRX.

**Reviewer judgement**: agree the impact is observably zero; the
contention is whether the audit's enumerated cleanliness statement
holds. It does NOT strictly hold — VHDL writes MEMPTR_lo=0xB7, C++
leaves it. But the practical impact is nil. I downgrade this from
class-c to NIT because the divergence is observably undetectable.

**Suggested fix** (optional, for completeness):

```cpp
// LDPIRX MCycle 1 sets LDZ=1 capturing the inner-M1 opcode byte
// (0xB7) into MEMPTR_lo per VHDL t80n_mcode.vhd:1967 +
// t80n.vhd:1181-1182. Software-visible impact is zero.
regs.MEMPTR = (regs.MEMPTR & 0xFF00) | 0xB7;
```

## Convergence assessment

* Audit's 29-angle enumeration: confirmed thorough. Each angle was
  re-checked at random-sample density 30% (covering BSRL/BRLC UB-free
  shifts, IM2 pulse mode transitions, IEI chain, F-flag composition
  for ADD HL/DE/BC,A, TEST_N AND flags, PIXELDN composite increment,
  LDPIRX I_BT flag composition, JP_C PC composition, OUTINB extended
  M1, MEMPTR strobes for ADD_*_NN/PUSH_NN/NEXTREG_*) — all match VHDL.
* Beyond the audit's 29 angles, I re-audited:
  - `Im2Level` ↔ `DevIdx` enum alignment (V18R-CPU-02 found here)
  - `/INT` pulse-window-and-EI interaction (V18R-CPU-01 found here)
  - LDPIRX MEMPTR strobe (V18R-CPU-NIT-01 found here, downgraded)
  - SETAE bit-select pattern (clean)
  - MIRROR_A bit-reverse loop semantics (clean)
  - MUL_DE 8×8→16 unsigned product (clean)
  - SWAPNIB nibble-swap (clean)
  - FUSE NMI/INT IFF1/IFF2 handling — `IFF1=IFF2=0` on INT, only
    `IFF1=0` on NMI (clean; FUSE matches Z80 spec).
  - R-register increment in IntAck / NMI (clean; +1 each).
  - Q flag clearing post-INT acceptance (clean; FUSE clears Q=0
    at end of fuse_z80_interrupt; ISR's first opcode sees clean Q).
  - HALT + INT/NMI resume PC+1 adjustment (clean).
  - The full `Im2Level::DMA`/`DIVMMC`/`MULTIFACE` legacy mapping →
    found V18R-CPU-02 — the comment claiming the legacy raise() path
    is harmless is incorrect.

**Recommendation**: This subsystem does NOT qualify for the SKIP rule
yet. Both V18R-CPU-01 and V18R-CPU-02 are class-(c) software-fixable
bugs found inside the explicit Pass-18 mandate. After they are fixed
+ regression-tested + independently re-reviewed, the CPU subsystem can
re-attempt convergence in Pass-19.

If the manager prefers to defer, both can be re-classed to class-(d)
follow-ups and the audit ratified — but the precedent should be
documented.

## Tests run

| Test                                | Result      |
|-------------------------------------|-------------|
| `fuse_z80_test build/test/fuse`     | 1356/1356   |
| `cpu_z80n_im2_regressions_test`     | 40/40       |
| `ctc_test`                          | all groups  |

No regressions introduced by the reviewer.

## Confidence statement

I cross-checked roughly 30% of the audit's 29 claimed angles with
fresh reads of the VHDL and C++. I additionally re-audited 11 areas
the audit's report addressed only briefly (LDPIRX MEMPTR, SETAE bits,
MIRROR_A bit-reverse, MUL_DE, SWAPNIB, FUSE INT/NMI IFF/IFF2
semantics, R-register update per accept cycle, Q flag clearing
post-INT, HALT+INT resume, `/INT` pulse-window-and-EI interaction,
`Im2Level` ↔ `DevIdx` enum alignment). Two of those additional areas
surfaced new class-(c) findings.

The defensible-zero claim is REJECTED. Pass-18 closed the families
the audit aimed at, but the broader CPU+Z80N+IM2 subsystem is not
yet converged.
