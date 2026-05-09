# Pass-3 CPU subsystem verification (Z80 + Z80N + IM2)

## Verdict

**3 class-(a) bugs found and fixed.** Each is independently observable
and breaks VHDL/Z80N spec faithfulness; none affected FUSE Z80
1356/1356 (those tests don't exercise Z80N) and none affected the
existing 37-test ctest suite once one stale test fixture was updated to
match the spec-compliant behaviour.

## Methodology

Blind audit (prior pass-1/pass-2 findings/fix reports NOT read).
Audited:

1. `src/cpu/z80_cpu.{h,cpp}` — Z80 wrapper, INT/NMI fabric, save/load
2. `src/cpu/z80n_ext.{h,cpp}` — 32 Z80N opcodes
3. `src/cpu/im2.{h,cpp}` — IM2 controller + RETI/RETN/IM-mode FSM
4. `src/cpu/im2_client.h` — peripheral facade (trivial wrapper)

against:

- VHDL `cores/zxnext/src/cpu/t80n.vhd`, `t80n_alu.vhd`, `t80n_mcode.vhd`
- VHDL `cores/zxnext/src/device/im2_control.vhd`,
  `im2_device.vhd`, `im2_peripheral.vhd`
- VHDL `cores/zxnext/src/zxnext.vhd:1941-2044, 6232-6254` (priority
  fan-out, vector composition, NMI capture, NR 0xC0/C2/C3/C8/C9/CA/CC)
- FUSE Z80 (`third_party/fuse-z80/`) — Z80 base oracle
- Spectrum Next spec
  ([wiki.specnext.dev/Extended_Z80_instruction_set](https://wiki.specnext.dev/Extended_Z80_instruction_set))

Pass-3 specifically targeted areas the previous two passes are unlikely
to have re-checked: R-register increment count for ED-prefix Z80N,
MEMPTR/Q (F-assembly) hidden-register coverage, undocumented X/Y flag
composition for Z80N block-transfer ops, and save/load completeness for
hidden CPU state.

## Findings

### Class-(a) #1 — Z80N block-transfer ops (LDIX/LDDX/LDIRX/LDDRX/LDIRSCALE) failed to update flags

**Severity:** spec/VHDL faithfulness — flag state visible to Z80
software. Detected by VHDL `I_BT='1'` block-transfer flag pulse in
`t80n.vhd:1277-1285` paired with the per-opcode mcode in
`t80n_mcode.vhd:2095-2256` and confirmed against the Spectrum Next
spec page (LDIX/LDDX/LDIRX/LDDRX explicitly list "Flags: N, H, P/V, X,
Y").

**What was wrong:** The C++ implementations preserved AF entirely —
they only updated DE/HL/BC and (for the repeating variants) PC. Per
VHDL the ALU runs at MCycle 2 with `Set_BusA_To = A` and `ALU_Op = ADD`,
producing `ALU_Q = bytetemp + A`. At MCycle 3, `I_BT=1` fires the
block-transfer flag composition:

```
F.X = ALU_Q[3]
F.Y = ALU_Q[1]
F.H = 0
F.N = 0
F.P = (BC != 0 after dec)   -- via IncDecZ for the BC decrementer
S, Z, C preserved
```

This is identical to the standard Z80 LDI/LDIR/LDD/LDDR flag pattern
and is exactly what FUSE encodes at `z80_ed.c:288-289`.

**Fix:** Added `ldi_family_flags()` helper and applied to LDIX, LDDX,
LDIRX, LDDRX, LDIRSCALE. Q (F-assembly shadow, see #2) is also updated
since these ops write F.

**Why pass-1/2 missed it:** The existing `test/z80n/tests.expected`
fixture coded the "preserved AF" behaviour as expected, locking in the
incorrect flag-omission as if it were correct. Pass-3 targeted VHDL
oracle review explicitly and noticed the I_BT signal cascade.

**Test side-effect:** 5 tests (`eda4_copy`, `edac_copy`, `edb4_basic`,
`edbc_basic`, `edb6_basic`) needed their expected AF byte updated from
`aa00` → `aa08` / `aa28` to match the spec-faithful flag composition.
This is a fix to the test fixture, not a regression.

**LDPIRX deliberately NOT fixed.** Its VHDL mcode at `t80n_mcode.vhd:1971-1976`
comments out `Set_BusA_To` and `ALU_Op`, leaving the ALU operands at
their default values for that MCycle (Set_BusA_To=B reg, ALU_Op derived
from `IR(5:3)` = `0110` = OR). The resulting ALU_Q definition is
implementation-quirk dependent; without a hardware oracle for that
specific composition I left LDPIRX flag behaviour unchanged.
Documented as class-(b) for follow-up.

### Class-(a) #2 — Z80N flag-writing opcodes failed to update the `Q` (F-assembly) shadow

**Severity:** undocumented X/Y flag composition for SCF/CCF run after
a Z80N flag-write. Detected by reading FUSE's SCF/CCF macros
(`opcodes_base.c:316-321, 361-366`) — both compute the new F as
`F = (F & SZP) | ((IS_CMOS ? A : ((last_Q ^ F) | A)) & (FLAG_3 |
FLAG_5)) | ...` where `last_Q` is captured at the start of every
opcode from `z80.q`.

**What was wrong:** The Z80N opcodes that write F (TEST_N, ADD_HL_A,
ADD_DE_A, ADD_BC_A, LDWS, plus the LDIX-family from #1) updated
`regs.AF` but not `regs.Q`. After such a Z80N op finishes, the next
fuse_z80_execute_one() snapshots `last_Q = z80.q` from the value
present when the Z80N op ran (i.e. pre-Z80N). A subsequent
SCF/CCF then composes its X/Y bits from a stale `last_Q ^ F`.

VHDL `t80n.vhd` does not model Q — the FPGA Z80 core uses a different
SCF/CCF formulation. But the FUSE Z80 *core* is the engine running the
non-Z80N opcodes in jnext, so we have to honour its Q convention to
keep cross-instruction flag state consistent. T80N is not the oracle
for Q.

**Fix:** Every C++ Z80N case that writes `regs.AF` now also assigns
`regs.Q = f`, with comment pointing to the FUSE convention.

### Class-(a) #3 — `Z80Cpu::save_state` / `load_state` omitted MEMPTR and Q

**Severity:** save/restore loses hidden-register state required for
correct undocumented-flag computation across snapshots.

**What was wrong:** `regs_.MEMPTR` (the hidden WZ register, observable
through BIT (HL)/(IX+d)/(IY+d) X/Y flag composition — see FUSE
`BIT_MEMPTR` macro using `z80.memptr.b.h`) and `regs_.Q` (F-assembly
shadow, see #2) are first-class fields of `Z80Registers` but were not
written/read by the save/load pair. After a load, both fields would
remain at whatever pre-load value they had (effectively zero on a fresh
emulator). Software that observes BIT (HL) X/Y flags or runs CCF/SCF
immediately after restore would diverge from real hardware.

Already-saved state: PC, AF, BC, DE, HL, AF', BC', DE', HL', IX, IY,
SP, I, R, IFF1, IFF2, IM, halted, plus the Z80Cpu wrapper's interrupt
queue (nmi_pending_, int_pending_, int_vector_, int_requested_at_).

**Fix:** Added `write_u16(MEMPTR); write_u8(Q)` to save_state, mirrored
in load_state at the symmetric position. The save format is already
in-process-only per the IM2 controller's `save_state` comment block
("rewind buffers are in-process only and invalidated by any build"),
so adding fields is non-breaking — old saves were already invalid by
build identity.

**Not fixed (class-(b)):** `z80.interrupts_enabled_at` (FUSE-internal
EI grace-period timestamp) and `z80.iff2_read` (CMOS-vs-NMOS post-NMI
parity-flag quirk) are also part of the FUSE z80 struct and are not
saved/restored. They reset to `-1` / `0` on each `Z80Cpu::reset()` and
are repopulated on the next EI/RETN. The window in which their
non-default values matter is narrow (one instruction post-EI / one
instruction post-NMI return) — at worst, snapshots taken in that
window will lose one cycle of EI grace or one parity-flag quirk after
NMI return. Documented for completeness but not fixed: would require
plumbing access to FUSE-internal state through the wrapper.

## Spot-checks (negative results — no bugs found)

The following areas were audited and verified VHDL-faithful:

### Z80N MUL_DE
8x8 → 16-bit unsigned product. VHDL `unsigned(D) * unsigned(E)` at
`t80n.vhd:732-735`. C++ `(uint16_t)d * (uint16_t)e`. Verified for
boundary inputs D∈{0,1,$10,$80,$FF}, E∈{0,1,$10,$80,$FF} via
standalone math test against VHDL reference.

### Z80N BSLA / BSRA / BSRL / BSRF / BRLC
5-bit shift count (B[4:0]) per VHDL `RegsH(...)(4 downto 0)` at
`t80n.vhd:993, 1014, 1028`. C++ all use `(B >> 8) & 0x1F`. Edge
verification:
- BSLA shift∈{0..31}: `<< 16+` produces 0; verified against
  `shift_left(unsigned(15:0), N)` semantics (N≥16 → 0).
- BSRA shift∈{0..31} for DE∈{0, 0x7FFF, 0x8000, 0xFFFF}: arithmetic
  fill-with-sign for N≥16; `int_promotion >> N` matches VHDL's 17-bit
  signed `shift_right`.
- BSRF: 17-bit signed shift with bit 16 = IR[0] = 1; the C++ uses
  `(int32_t)(((1<<16)|DE) << 15) >> 15` to sign-extend bit 16 to bit
  31 then arithmetic-shifts down. Verified for shift∈{0..31} and DE
  values covering both bit-15 polarities — bit-exact match with VHDL
  reference.
- BRLC: 5-bit shift count, but rotate_left auto-mods by 16. C++
  `if (rot != 0) { rot &= 0x0F; ... }` — verified at boundaries
  rot∈{0, 15, 16, 17, 20, 31, 32}.

All passed against the VHDL-reference functions at all sampled inputs.

### Z80N TEST_N
Per VHDL `t80n_mcode.vhd:1778-1787` + `t80n_alu.vhd:172-208`: AND
operation with operand, flag composition AND-style — H=1, N=0, C=0,
S=Q[7], Z=(Q==0), P=parity, X=Q[3], Y=Q[5], A unchanged. C++
matches bit-for-bit. Q now also updated (fix #2 above).

### Z80N PIXELAD / PIXELDN / SETAE / SWAPNIB / MIRROR_A
All bit-pattern shuffles. VHDL formulas at `t80n.vhd:702-708, 900-947`
match C++ implementations exactly (verified against VHDL bit-slice
notation manually + spot-checks).

### Z80N JP_C
Reads byte from port BC, writes PC[13:6] = byte and PC[5:0] = 0,
preserves PC[15:14]. VHDL `t80n.vhd:980-983`. C++
`(PC & 0xC000) | (val << 6)`. ✓

### Z80N PUSH_NN
Big-endian operand stream → little-endian stack push. C++ reads hh
then ll, writes ll at SP and hh at SP+1. ✓

### Z80N OUTINB
HL increments, B is *not* decremented (unlike OUTI). VHDL
`t80n_mcode.vhd:2519-2553` — `if IRB /= X"90"` guards the B decrement.
C++ matches.

### Z80N NEXTREG_NN / NEXTREG_A
Two-OUT sequence to ports 0x243B (register select) + 0x253B (data).
Behaviourally equivalent to VHDL's NEXTREG_W command path (which
plumbs to the same ports through a different pipeline). ✓

### Z80N LDIRX/LDDRX/LDPIRX/LDIRSCALE per-iteration shape
Per-iteration step + PC rewind on BC≠0, mirroring VHDL `MCycles="100"`
pattern. INT can be sampled between iterations (matches VHDL). BC=0
on entry → 65536 iterations (post-decrement underflows to 0xFFFF). ✓

### Z80N R-register increment
Each Z80N op adds 2 to `z80.r` (line 494) — one M1 for ED, one for
ext byte. R[7] is preserved in `z80.r7` by the FUSE shim. ✓

### IM2 RETI/RETN/IM-mode decoder FSM
Per-byte advance on `on_m1_cycle()`. ED + ext bytes both delivered
(line 488-489 / 533-534). State transitions match
`im2_control.vhd:158-209`:
- S_0 + ED → S_ED_T4
- S_ED_T4 + 4D → S_ED4D_T4 + reti_seen pulse
- S_ED_T4 + 45 → S_ED45_T4 + retn_seen pulse
- S_ED4D_T4 / S_ED45_T4 → S_SRL_T1 → S_SRL_T2 → S_0
- S_ED_T4 + IM-mode-byte (xx46/4E/56/66/6E/76/5E/7E)
  → im_mode latched per VHDL `:218-227` with bit-decode
  `b4=0:IM0; b4=1,b3=0:IM1; b4=1,b3=1:IM2`.
- S_DDFD_T4 / S_CB_T4 transitions match.

DD/FD inner-byte and CB inner-byte delivery is documented as a
known scope gap (line 528-532) and only affects RETI/RETN within
DD/FD/CB-prefixed sequences — practically empty case. Out of pass-3
scope.

### IM2 device daisy-chain priority
14-device daisy chain, device 0 IEI hardwired '1' (zxnext.vhd:1984).
IEO derived from state per `im2_device.vhd:136-146`: S_0 → IEI,
S_REQ → IEI∧reti_decode, others → 0. Iterative O(N²)=196-op walk.
Match.

### IM2 ack_vector / vector composition
VHDL `zxnext.vhd:1999`: `vector = nr_c0_im2_vector[2:0] & im2_vec[3:0] & '0'`.
C++ `compute_vector()` matches; `ack_vector()` walks priority order,
latches first S_REQ device with IEI=1 to S_ACK, returns composed
vector. Rest of devices stay in S_REQ, blocked by upstream IEO=0. ✓

### IM2 pulse fabric
VHDL `zxnext.vhd:2017-2044` machine-aware /INT pulse generator
(48K/+3: 32 cycles, 128K/Pent/Next-default: 36 cycles). C++ pulse
counter strict `>` comparator gives exact VHDL semantics (the test
`test/cpu/int_pulse_test.cpp` covers all four boundary T-states +
machine variants). Z80Cpu's mirror gate (`int_pulse_tstates` at
z80_cpu.cpp:426) + the strict-`>` comparator at line 428 release
late by 1 cycle — VHDL `pulse_count_end` asserts at delta=32, so
delta=33 must see the pulse cleared. `int_pulse_test` validates
this exact boundary.

### IM2 DMA-delay latch (NR 0xCC)
`im2_dma_delay <= im2_dma_int OR (nmi_activated AND
nr_cc_dma_int_en_0_7) OR (im2_dma_delay AND dma_delay)`. The C++
`step_dma_delay()` mirrors. ✓

### Save/load coverage in IM2
14 devices × 9 fields, decoder state, pulse fabric, NR 0xC0/CC state,
and ACK book-keeping all included. Modulo the documented version-byte
omission (in-process rewind only). ✓

## Convergence assessment

After three independent verification passes, the CPU subsystem
(Z80 + Z80N + IM2) appears at high VHDL-faithfulness with the
following residual class-(b) gaps:

1. **Z80N opcode contention.** The Z80N fast-path bypasses
   `contend_read()` for both M1 fetches (ED + ext) and operand reads;
   the VHDL ULA contention model would stretch these on the active
   raster window when Z80N runs from a contended page. Impact is low
   — most Z80N use is from non-contended ROM. Fixing requires
   refactoring the Z80N path to call `contend_read(pc, 4)` for each
   M1 and `fuse_z80_readbyte()` for operand reads. Out of scope for
   pass-3 (would risk regressions in well-tested timing path).
2. **`z80.interrupts_enabled_at` and `z80.iff2_read` not in
   save/load.** Narrow-window state (one post-EI cycle / one post-RETN
   cycle). Plumbing them into `Z80Registers` would extend FUSE-shim
   surface; deferred.
3. **DD/FD/CB inner-byte delivery to IM2 decoder FSM.** Documented gap
   in z80_cpu.cpp comment block; would require fanning out `on_m1_cycle`
   inside FUSE prefix dispatchers. RETI/RETN are not legitimate within
   DD/FD/CB-prefixed sequences in any real software.
4. **LDPIRX block-transfer flag update.** VHDL's commented-out ALU
   operands make the spec ambiguous; left preserving AF for now.

None of (1)-(4) is observable in the FUSE 1356-test suite, the
existing 37 ctest subsystems, or in normal NextZXOS boot path
(audited via the G46(b) investigation logs).

## Test results

```
$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed: 0  Skipped: 0

$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
```

## Branch / artefacts

- Worktree: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify3-cpu-z80n-im2`
- Branch: `task2/verify3-cpu-z80n-im2`
- Files modified:
  - `src/cpu/z80n_ext.cpp` — Q updates + LDI-family flag updates +
    `ldi_family_flags()` helper.
  - `src/cpu/z80_cpu.cpp` — MEMPTR + Q in save/load.
  - `test/z80n/tests.expected` — five LDI-family fixtures updated to
    spec-faithful flag composition.
- Files reviewed but unchanged:
  - `src/cpu/z80_cpu.h`, `src/cpu/z80n_ext.h`, `src/cpu/im2.{h,cpp}`,
    `src/cpu/im2_client.h`.

## Open questions / residuals

- LDPIRX flag behaviour (class-(b)) — could be fixed if a hardware
  oracle (real-hardware capture or verified-against-FPGA emulator)
  confirms which ALU-default operands feed the I_BT pulse.
- Z80N opcode contention (class-(b)) — would close the last
  M1-fetch contention gap. Need a contention-stress regression
  fixture to validate post-fix.
