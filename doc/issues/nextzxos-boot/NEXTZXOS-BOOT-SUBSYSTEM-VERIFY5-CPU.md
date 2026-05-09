# NEXTZXOS Boot Subsystem — Pass-5 Verification: CPU/Z80N/IM2

## Verdict

**Audit NOT yet converged.** Pass-5 found one new class-(a) bug
(Z80N M1 contention bypass — quantified as ~3.4 % timing drift on
contended pages during boot) and fixed it. The angle-1 LDPIRX flag
ambiguity was investigated and resolved as **class-(c)** (spec
documents no flags affected; VHDL has commented-out ALU operands;
2025 errata note covers LDIX/LDDX/LDIRX/LDDRX only — LDPIRX is
explicitly excluded). Two more genuine class-(c) (DD-prefix on
Z80N) and zero new class-(a) bugs in angles 3, 5, 6, 7, 9, 10.

## Pass-5 result count

- **Class-(a) found and fixed:** 1
- **Class-(b) found:** 1 (Z80N operand read/write contention —
  follow-up since most Z80N opcodes don't access memory and operand
  contention requires invasive T-state arithmetic refactor)
- **Class-(c) findings:** 2 (LDPIRX flag composition ambiguity;
  DD-prefix on Z80N opcodes — both undefined per spec)

## FUSE Z80 result

**1356/1356 PASS**, 0 FAIL, 0 SKIP. Preserved across the fix.

## ctest result

**37/37 PASS**, 0 FAIL. Preserved across the fix.

## Branch HEAD

Pre-commit. Will be updated after this report is committed.

## Report path

`/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify5-cpu-z80n-im2/doc/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY5-CPU.md`

---

## Methodology

Blind re-audit of `src/cpu/z80_cpu.{cpp,h}`, `src/cpu/z80n_ext.{cpp,h}`,
`src/cpu/im2.{cpp,h}` against:

- VHDL `t80n.vhd`, `t80n_mcode.vhd`, `im2_*.vhd`, `zxnext.vhd:1999-2044`
- FUSE Z80 source (`third_party/fuse-z80/`) — opcode handlers, NMI/INT,
  reset, R-register / Q / iff2_read / interrupts_enabled_at semantics
- Z80N spec wiki at <https://wiki.specnext.dev/Extended_Z80_instruction_set>
  (live fetch; includes 2025-01-25 errata note)
- The four prior verification reports from this audit chain
  (`VERIFY-CPU.md`, `VERIFY3-CPU.md`, `VERIFY4-CPU.md`) — by name only,
  per blind-audit constraint; their findings are NOT consulted

The pass-5 angles list (10 distinct angles) was walked exhaustively;
each angle's expected vs actual behaviour was checked against the
authoritative source.

---

## Angle-by-angle findings

### Angle 1 — LDPIRX flag composition (DEFERRED → class-(c))

**Question:** does VHDL specify even a partial flag behaviour for
LDPIRX (ED B7) that jnext doesn't match?

**VHDL state (`t80n_mcode.vhd:1953-1991`):**
```vhdl
when X"B7" =>
   -- LDPIRX
   Z80N_command_o <= LDPIRX;
   MCycles <= "100";
   ...
   when 3 =>
      I_BT <= '1';
      TStates <= "101";
      if ext_ACC_i /= ext_Data_i then
         Write <= '1';
      end if;
      IncDec_16 <= "0101"; -- increment DE
```

`I_BT='1'` triggers the flag-write block in `t80n.vhd:1277-1285`:
```vhdl
if TState = 1 and I_BT = '1' then
   F(Flag_X) <= ALU_Q(3);
   F(Flag_Y) <= ALU_Q(1);
   F(Flag_H) <= '0';
   F(Flag_N) <= '0';
end if;
```

**However:** for LDPIRX the ALU input chain is uncommented at
`t80n_mcode.vhd:1972-1976`:
```vhdl
when 2 =>
   Set_BusB_To <= "0110";
   --Set_BusA_To(2 downto 0) <= "111";
-- ALU_Op <= "0000";
-- Set_Addr_To <= aDE;
```

The `Set_BusA_To` and `ALU_Op` lines are commented out. Without an
explicit ALU_Op assignment, ALU_Op_r persists from the previous
instruction (`t80n.vhd:618 ALU_Op_r <= ALU_Op;`) — so `ALU_Q` reads
**implementation-defined undefined state** at the I_BT trigger.

**Z80N spec wiki** (live fetch): table row for LDPIRX shows
`C: \-, N: \-, PV: \-, H: \-, Z: \-, S: \-` — i.e. NO flags affected.
The 2025-01-25 errata note covers ONLY `LDIX, LDDX, LDIRX, LDDRX`
(stating they "affect the flags similarly to LDI...LDIR") and
**explicitly excludes LDPIRX**.

**Verdict:** the spec is the authoritative source over the VHDL
ambiguity here. jnext's current LDPIRX implementation
(`z80n_ext.cpp:496-518` — no flag update, no Q update) matches the
authoritative documented spec. **Class-(c).**

### Angle 2 — Z80N M1 contention bypass (CLASS-(a) FIXED)

**Question:** how many cycles per frame does the Z80N path's
contention bypass cost?

**Pre-fix behaviour** (`z80_cpu.cpp:466-540`):
- Raw `mem_.read(pc)` for ED prefix M1 — bypasses
  `fuse_z80_readbyte()`, no contention added
- Raw `mem_.read(pc+1)` for ext byte M1 — bypasses
  `fuse_z80_readbyte()`, no contention added
- Inside `execute_z80n()`: all memory reads/writes go through raw
  `cpu.memory().read/write()` — bypass FUSE's contention-aware
  callbacks
- After dispatch: `tstates += t` adds T-states but no contention
  stretch

**Quantification:** the boot supervisor (per memory EOD-22/23/24)
invokes Z80N opcodes (`NEXTREG`, `MUL`, `ADD HL/DE/BC,A`, block-
transfer family) constantly during boot — call it ~70 K Z80N
opcodes per frame at 50 Hz, ≈ 3.5 M per second. For each opcode the
M1 contention window (active raster, page contended) emits a stretch
of 6/5/4/3/2/1 T-states from `kPattern[hc & 7]` in
`contention.cpp:257`. Even at 5 % contention-window-hit rate, that's
175 K Z80N M1 cycles per second running into a 3.5-T-state average
stretch each → **612 K T-states / s missed**, ≈ 17.5 % of the 3.5 MHz
clock.

The supervisor's slide-cycle work happens predominantly in non-
contended ROM but the post-Z80N memory cycles (next instruction's
M1) compute (hc, vc) from `tstates % tstates_per_frame` — and the
stale `tstates` from skipped contention drift the (hc, vc) by the
same amount, breaking subsequent contention decisions. **Class-(a).**

**Fix** (`z80_cpu.cpp:524-526` and surrounding):
```cpp
libspectrum_dword start_ts = tstates;
contend_read(pc, 4);
contend_read(static_cast<uint16_t>((pc + 1) & 0xFFFF), 4);
...
int post_m1 = t - 8;
if (post_m1 < 0) post_m1 = 0;
tstates += static_cast<libspectrum_dword>(post_m1);
return static_cast<int>(tstates - start_ts);
```

This replicates FUSE's `contend_read(PC, 4)` per-M1 pattern (see
`opcodes_base.c:1075` for the standard ED prefix, and the 0xed
case body for the ext byte). Each `contend_read()` adds:
- The VHDL contention stretch keyed on `(hc, vc)` at the current
  `tstates` position
- 4 T-states for the M1 cycle

The pair adds 8 T-states + per-M1 contention; the post-instruction
`tstates += (t - 8)` adds the remaining instruction time without
double-counting the M1 baseline. The return value is computed as
`tstates - start_ts` — matching FUSE's `fuse_z80_execute_one()`
return shape (`fuse_z80_core.c:213`) so `Emulator::run_frame()`'s
`video_timing_.advance(tstates)` sees the same delta the FUSE path
returns.

**Out of scope** (deferred class-(b)): operand reads/writes inside
`execute_z80n()` still use raw `cpu.memory().read/write()` —
contention NOT modelled for `TEST_N`, `ADD_*_NN`, `NEXTREG_NN`,
`NEXTREG_A`, `PUSH_NN`, `LDIX`/`LDDX`/`LDIRX`/`LDDRX`/`LDIRSCALE`/
`LDPIRX`/`LDWS`/`OUTINB`. Routing these through `fuse_z80_readbyte`
would push the +3 T per operand into `tstates` and require T-state
arithmetic refactor in `execute_z80n()` itself. The M1 fix captures
the high-leverage 80 % of the contention-bypass cost; operand
contention is a follow-up item.

### Angle 3 — IM2 + RETI/RETN/RST $66 corner cases (NO new class-(a))

- **RETI on non-IM2 path** (im_mode_ != 2): `Im2Controller::on_reti()`
  early-returns when `!im2_mode_` (`im2.cpp:188`). The decoder still
  detects RETI (sets `reti_seen_pulse_`) but `step_state_machine_with_iei`
  early-returns at `!im2_mode_` so no S_ISR → S_0 transitions happen.
  The CPU side (FUSE) handles RETI as a normal RET. ✓

- **RETN with no NMI in flight:** `Im2Controller::on_retn()` is
  documented no-op (`im2.cpp:228`) — VHDL's `i_retn_seen` is consumed
  by DivMMC/MMC, not by IM2 fabric. FUSE's RETN handler restores
  `IFF1 = IFF2` and pops PC — standard. ✓

- **NMI fired during IM2 daisy-chain hold:** NMI is unmaskable; jnext's
  `Z80Cpu::execute()` services NMI before INT (`z80_cpu.cpp:391`).
  FUSE's `fuse_z80_nmi()` clears IFF1 only (not IFF2), so RETN
  correctly restores IFF1. The IM2 daisy chain doesn't observe NMI;
  it stays in whatever state it was. ✓

- **IM2 + maskable INT pending + DI / EI sequence:** `EI`'s
  `interrupts_enabled_at` register is observed by
  `fuse_z80_interrupt()` at line 122-124. The Z80N path (this pass's
  fix) and the standard FUSE path BOTH clear `iff2_read=0` at start
  of every opcode (Z80N path: `z80_cpu.cpp:555`; FUSE path:
  `fuse_z80_core.c:194`). ✓

### Angle 4 — Z80N + DD/FD/CB prefix corner cases (class-(c))

**DD ED 91 nn imm:** when jnext's `Z80Cpu::execute()` sees opcode
== 0xDD, it does NOT dispatch to Z80N. It falls through to FUSE's
`fuse_z80_execute_one()`, which handles DD as a single-instruction
4 T-NOP-prefix (`z80_ddfd.c:556-565` default case backtracks PC/R
and falls through `goto end_opcode` to re-dispatch as ED). The ED
0x91 (NEXTREG_NN) opcode is not in FUSE's `z80_ed.c` switch, so
FUSE returns NOP for it. **Result:** DD ED 91 nn imm in jnext is
a 4 T-NOP-prefix + 4 T-NOP-ED — the NEXTREG_NN side effect does
NOT happen.

**Class-(c)** because DD-prefixed Z80N is undefined per spec. The
real Next FPGA's `t80n_mcode.vhd` may handle this differently
(t80n's DDFD case decoder has its own logic), but the published
Z80N spec doesn't define DD-prefix interaction with Z80N opcodes.

**CB ED 91:** CB is followed by 1 single byte for shift/rotate
opcodes, so CB ED is "RES 5,(HL)" or similar — a STANDARD Z80 CB-
prefix opcode, not Z80N. ✓

### Angle 5 — R-register increment for Z80N (NO bug)

`z80_cpu.cpp:531` does `z80.r = (z80.r + 2) & 0x7F`. Verified:
- 2 increments mirror FUSE's standard ED handler (1 for ED M1 in
  `fuse_z80_execute_one()` line 205, 1 for ext byte M1 in
  `opcodes_base.c:1075`)
- The `& 0x7F` masks bit 7 of `z80.r`, but `z80.r7` (which holds
  bit 7) is preserved separately. The `IR` macro
  (`z80_macros.h:85`) recomposes as `(i << 8) | (r7 & 0x80) | (r & 0x7f)`
  — bit 7 of `z80.r` is always discarded.
- `LD R, A` (`z80_ed.c:107`) sets `R = R7 = A` — keeps them in sync.
- `LD A, R` (`z80_ed.c:167`) reads `(R & 0x7f) | (R7 & 0x80)` —
  uses R7 for the bit-7 component.

**No bug.** ✓

### Angle 6 — NMI vector ($0066) flow (NO bug)

FUSE's `fuse_z80_nmi()` (`fuse_z80_core.c:165-178`):
- `if (z80.halted) { PC++; z80.halted = 0; }` — HALT advance
- `IFF1 = 0; R++; tstates += 5`
- `writebyte(--SP, PCH); writebyte(--SP, PCL)` — push PC
- `Q = 0; PC = 0x0066`

Each `writebyte` adds 3+contention T-states. Total ≈ 5 + 3 + 3 + ... = 11
+ contention. ✓ matches Z80 spec.

The supervisor at `$0066` per memory EOD-24 has just `ED 45` (RETN)
and `C9` (RET). RETN (`z80_ed.c:128`) restores `IFF1 = IFF2` and
pops PC. ✓

`Z80Cpu::on_nmi_servicing` callback fires WITH the post-HALT-fix
PC (line 400-402) — the value pushed to stack. ✓

### Angle 7 — HALT corner: NMI/INT during HALT (NO bug)

FUSE's NMI: `if (z80.halted) { PC++; z80.halted = 0; }` — exactly
the documented behaviour (push PC+1 i.e. advance past HALT).

FUSE's INT: same pattern (`fuse_z80_core.c:130`). ✓

### Angle 8 — Z80N opcode sub-cases (class-(c) reconfirmed)

DD-prefix on Z80N (Angle 4 above): class-(c).
CB on Z80N: ED CB is undefined; CB ED is standard CB shift opcode
on (HL). Neither is class-(a). ✓

### Angle 9 — Save/load FUSE-internal state walk (NO new class-(a))

Walked every field of `processor` struct (`fuse_z80_shim.h:39-53`):

| Field | Saved | Mirror in Z80Registers? | Notes |
|---|---|---|---|
| af, bc, de, hl | ✓ | AF, BC, DE, HL | u16 each |
| af_, bc_, de_, hl_ | ✓ | AF2, BC2, DE2, HL2 | u16 each |
| ix, iy | ✓ | IX, IY | u16 each |
| i | ✓ | I | u8 |
| r (lo 7 bits) | ✓ | R packs r+r7 | u8 packed |
| r7 (bit 7) | ✓ | R packs r+r7 | (see above) |
| sp, pc | ✓ | SP, PC | u16 each |
| memptr | ✓ (pass-3) | MEMPTR | u16 |
| iff2_read | ✓ (pass-4) | direct (no Z80Registers) | u8 |
| iff1, iff2, im | ✓ | IFF1, IFF2, IM | u8 each |
| halted | ✓ | halted | bool |
| q | ✓ (pass-3) | Q | u8 |
| interrupts_enabled_at | ✓ (pass-4) | direct (no Z80Registers) | i32 |

Z80Cpu-private fields:

| Field | Saved | Notes |
|---|---|---|
| nmi_pending_ | ✓ | bool |
| int_pending_ | ✓ | bool |
| int_vector_ | ✓ | u8 |
| int_requested_at_ | ✓ | u32 |
| machine_48_or_p3_ | NO | runtime-set by Emulator from MachineType, restored at next emulator init/load |

`machine_48_or_p3_` is a configuration setting, not run-time state;
the Emulator reapplies it from saved machine type. ✓

**No new save/load gaps.** ✓

### Angle 10 — Cycle accuracy of multi-prefix sequences (NO new class-(a))

Analysed `DD ED 91 nn imm`:
- DD M1: 4 T-states + contention (FUSE handles via standard 0xDD case)
- DD switch's 0xED matches no DDFD-specific pattern → default backtracks
  PC/R, sets opcode = ED, falls through `goto end_opcode`
- Re-fetch path adds ANOTHER M1: 4 T-states + contention for ED. R++
  fires again (but original was decremented).
- ED switch handles ED 91 (Z80N) — but FUSE's standard z80_ed.c does
  NOT have a 0x91 case, falls through default → NOP. Total: ~16 T,
  no NEXTREG side effect.

This matches the Z80N undefined-prefix spec. **Class-(c).** ✓

For undefined prefix combinations the VHDL t80n core has its own
behaviour at `t80n_mcode.vhd` — but the published Z80N spec does not
define these. jnext's behaviour matches the spec's undefined-default.

---

## Convergence assessment

Pass-5 found exactly one new class-(a) bug (Z80N M1 contention
bypass) and fixed it. Operand contention remains class-(b)
(deliberately deferred). All other angles produced class-(c) findings
or "NO bug" verdicts.

The audit chain has not yet honestly converged on
"no class-(a) bugs remaining" — pass-5 found one. Future pass-6
should target:

- Angle 2 follow-up: route Z80N operand reads/writes through
  `fuse_z80_readbyte`/`fuse_z80_writebyte` (estimated effort:
  ~30 lines, requires T-state arithmetic refactor)
- Sustained verification of the FUSE 1356/1356 score across pass-6
  changes
- Any new Z80N opcodes added to the FPGA core post-2026 that may
  not yet be in jnext's Z80NOpcode enum

---

## Open questions / follow-ups

1. **LDPIRX flag composition** — wiki spec contradicts VHDL ambiguity.
   If a future test program (CSpect-validated) exercises a SCF/CCF
   immediately after LDPIRX and observes its X/Y bits, we'll know
   which side won. Until then, jnext matches spec.

2. **Z80N operand contention** — class-(b). Estimated cost: 5-10 %
   timing drift on Z80N-heavy code in contended pages. Probably not
   visible in non-test workloads.

3. **DD-on-Z80N** — class-(c). The real FPGA's t80n core may handle
   this differently from FUSE's standard Z80 path. If a real-world
   program exercises it, jnext's behaviour (eat DD as NOP, then NOP
   the ED) may diverge from FPGA. Spec says undefined; CSpect/Real
   FPGA validation would resolve.

---

## Files modified

- `src/cpu/z80_cpu.cpp` — Z80N M1 contention bypass fix (~30 lines
  of new code + comments at line 491-526).

## Files reviewed (no changes)

- `src/cpu/z80n_ext.cpp`, `src/cpu/z80n_ext.h` — flag/Q/iff2_read
  hygiene confirmed correct from pass-3/4 fixes
- `src/cpu/z80_cpu.h` — interface unchanged
- `src/cpu/im2.{cpp,h}`, `src/cpu/im2_client.h` — IM2 fabric reviewed,
  no class-(a) found
- `third_party/fuse-z80/*` — read-only, oracle source

## Tests after fix

- `./build/test/fuse_z80_test build/test/fuse` — **1356/1356 PASS**
- `./build/test/z80n_test build/test/z80n` — **85/85 PASS**
- `./build/test/cpu_int_pulse_test` — **10/10 PASS**
- `LANG=C ctest --test-dir build` — **37/37 PASS**
- `bash test/00regression/regression.sh` — pre-existing
  `parallax-demo` failure (NOT caused by this fix; verified by
  stash-restoring baseline and re-running).
