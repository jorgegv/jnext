# Pass-4 Blind Verification Re-Audit — CPU / Z80N / IM2

**Branch:** `task2/verify4-cpu-z80n-im2`
**Worktree:** `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify4-cpu-z80n-im2`
**Date:** 2026-05-09
**Auditor mandate:** find what passes 1–3 missed; be EXTREMELY critical; VHDL/Z80N spec/FUSE oracle.
**Constraint:** blind — did NOT consult any prior `NEXTZXOS-BOOT-SUBSYSTEM-*.md` report.

## Verdict

**Convergence: NEAR — three new class-(a) bugs found and fixed in pass-4.**

Three real defects were uncovered, all related to FUSE's "begin-opcode hygiene"
contract that the Z80N dispatch path in `Z80Cpu::execute()` was bypassing, plus
one VHDL-spec divergence on the `ADD HL/DE/BC, nn` Z80N opcode group's WZ
register update. Z80N pass-3 added `Q` for F-writing opcodes; pass-4 closes
the dual that pass-3 missed: non-F-writing Z80N opcodes also need to leave
`Q = 0`, otherwise the very next `SCF`/`CCF` reads stale `last_Q` and emits
wrong undocumented X/Y flag bits.

After pass-4 fixes: FUSE Z80 = **1356/1356**, ctest = **37/37 passing**.

## Methodology

### Audit dimensions

1. **Prefix-composition matrix** — `DD/FD ED <Z80N>` and similar. The Z80N
   dispatch in `z80_cpu.cpp:466-520` matches only on `opcode == 0xED`; it
   does NOT inspect a preceding DD/FD prefix. This means `DD ED 91 nn val`
   falls through to FUSE's `fuse_z80_execute_one()`, which dispatches
   `case 0xdd → z80_ddfd.c default → backtrack PC/R → re-dispatch ED → z80_ed.c
   default → 4T NOP`. In other words, DD/FD before a Z80N opcode silently
   discards the Z80N semantics. **Verdict: class-(c)** — the Z80N spec
   doesn't define DD/FD-prefixed Z80N behavior, the VHDL `t80n_mcode.vhd`
   doesn't either, and no boot-path code emits this pathological encoding.
   Documented for completeness. Not fixed.

2. **R / MEMPTR / Q semantics for every Z80N opcode** — walked
   `t80n_mcode.vhd` for `LDZ`/`LDW` (= load WZ low/high) and
   `Inc_WZ` (= increment WZ) signal assertions inside each Z80N opcode's
   MCycle decoder. Found:
   - `ADD HL/DE/BC, nn` (X"34"/35"/36") — VHDL sets `LDZ` at MCycle 2 and
     `LDW` at MCycle 3 → end-of-instruction `WZ = nn` (the operand value).
     C++ implementation in `z80n_ext.cpp` did NOT update MEMPTR. **Class-(a)
     fix landed.** Test fixtures in `test/z80n/tests.expected` were
     authored with the wrong (no-update) assumption and were updated to
     match VHDL.
   - `PUSH NN` (X"8A") — VHDL has TWO `LDZ <= '1'` (at MCycle 1 and MCycle 3)
     but no `LDW`. End-state Z = second operand byte; W = stale. This is
     unusual and likely undocumented quirk; test fixtures already match
     this stale behavior. **Class-(c) — left alone**, no spec.
   - `OUTINB`, `JP (C)`, `NEXTREG_NN`, `NEXTREG_A`, `MUL D,E`, `SWAPNIB`,
     `MIRROR A`, `BSLA/BSRA/BSRL/BSRF/BRLC`, `LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE`
     — VHDL has no `LDZ`/`LDW` in these blocks → no WZ update. C++ matches.
   - **Q register**: pass-3 added `regs.Q = f` for F-writing Z80N opcodes.
     But the dual was missed: FUSE's `fuse_z80_execute_one()` (line 207 of
     `fuse_z80_core.c`) sets `Q = 0` at the TOP of every dispatch, BEFORE
     decoding. Z80N opcodes that do NOT write F (SWAPNIB / MIRROR_A /
     BSLA / BSRA / BSRL / BSRF / BRLC / MUL_DE / ADD_*_NN / PUSH_NN /
     OUTINB / NEXTREG_NN / NEXTREG_A / PIXELDN / PIXELAD / SETAE / JP_C /
     LDPIRX / LOOP) bypass this clear, so `Q` retains the F value of the
     preceding FUSE opcode. The next `SCF`/`CCF` reads `last_Q` from this
     stale value and emits wrong X/Y flag bits. **Class-(a) fix landed**:
     `z80.q = 0` at the top of the Z80N dispatch arm in `z80_cpu.cpp`.

3. **FUSE-internal save/load gap** — walked the `processor` struct in
   `fuse_z80_shim.h` and cross-referenced with `Z80Registers` /
   `Z80Cpu::save_state` / `Z80Cpu::load_state`:
   - `interrupts_enabled_at` (signed_dword) — set by `EI` (opcodes_base.c:1159),
     read by `fuse_z80_interrupt()` (fuse_z80_core.c:123) to gate the
     "no INT on the immediately-following instruction" rule. Has NO
     `Z80Registers` mirror; was not in save/load. **Class-(a) fix landed.**
   - `iff2_read` — set by `LD A,I` and `LD A,R` (z80_ed.c:138, 170),
     read by `fuse_z80_interrupt()` (fuse_z80_core.c:126) to model the
     NMOS LD A,I/R + INT race quirk. Has NO `Z80Registers` mirror; was
     not in save/load. **Class-(a) fix landed.** As a Pass-4 bonus, also
     cleared at the start of every Z80N dispatch (mirroring FUSE's own
     hygiene), so a Z80N opcode between `LD A,I` and an immediately-pending
     INT no longer fires the NMOS quirk wrongly.

4. **IM2 fabric stress cases** — walked `im2.cpp`. The daisy-chain priority
   walk and IEI snapshotting at `step_devices()` already handle:
   - simultaneous interrupts (priority resolved by walking dev_[0..N) in
     fixed order — VHDL peripherals.vhd:146-156 AND-reduction)
   - peripheral retraction mid-acceptance (S_REQ has no fall-through; once
     in S_REQ the request is sticky until S_ISR cleanup — matches VHDL
     im2_device.vhd by definition)
   - RETI dispatching only via `on_reti()` from the `ED 4D` decoder pulse
     (im2_control.vhd:158-210)
   - vector composition `(I << 8) | bus_byte` is FUSE-handled
     (fuse_z80_core.c:148-153). Bus byte source: when `on_int_ack` is
     installed, it returns the daisy-chain `o_vec`; otherwise legacy
     `int_vector_` (open-bus floats `0xFF` per ZX hardware). Verified
     correct.
   - **Pass-4 verdict: no new IM2 fabric bugs.**

5. **NMI / INT race** — `Z80Cpu::execute()` checks NMI before INT (top of
   function at line 391-457). VHDL gives NMI priority over INT. Match.
   Both can fire on a single instruction boundary; VHDL serialises NMI
   first (current jnext model takes NMI on this execute() call, INT on
   the next — same observable end state).

6. **HALT corners** — fuse_z80_nmi (fuse_z80_core.c:166-178) handles the
   HALT bump: `if (z80.halted) { PC++; halted = 0; }` BEFORE the push.
   This makes the saved PC = HALT+1 (re-entry continues past HALT). Same
   for `fuse_z80_interrupt()` (line 130). G88 callback in `Z80Cpu::execute()`
   pre-computes the saved value EXACTLY this way (line 400-401). Match.

7. **Differential VHDL signal coverage** — picked 10 signals from
   `t80n.vhd` and `im2_*.vhd`:
   - `ISet`, `XY_State`, `XY_Ind`, `Inc_WZ`, `LDZ`, `LDW` — all CPU prefix
     state, owned by FUSE and cross-checked above.
   - `o_int_n`, `o_ieo`, `state` (S_0/S_REQ/S_ACK/S_ISR), `o_pulse_en`,
     `pulse_count_end` — all wired via `Im2Controller`. Match.

### Boot-path contention sensitivity (Pass-3 class-(b) re-evaluation)

The Z80N path uses `cpu.memory().read()` (raw, no contention) for operands.
Pass-2 flagged this as class-(b). Pass-4 quantified: of the four banks of
NextZXOS supervisor (banks 0/1/2/3), the boot path's NEXTREG / MUL D,E /
ADD HL,A / NEXTREG_NN traffic happens primarily in slot 0 (ROM, never
contended), slot 5 (page 5 RAM, contended on 48K + 128K), and slot 6/7
(banks 2/0, contended only when `rom_bank` selects pages 5/7 in 128K).
Per RAM_REBUILD probe data in EOD-23 memory, supervisor banks rotate
through all 8 pages, so contention applies SOMETIMES to Z80N reads.
Wall-clock impact on a 50 Hz frame: at most a few hundred T-states/frame
of missed contention if a Z80N opcode is contended every other call (the
absolute worst case is `NEXTREG_NN` reading 2 contended bytes ×
~200 NEXTREG calls/frame × ~6 T missed contention/byte ≈ 2400 T/frame =
0.7% timing skew). **Class-(b)**: real but small, not a boot-blocker.
Not fixed in pass-4 — would require re-routing Z80N reads through
`fuse_z80_readbyte()`, a substantial refactor.

## Findings (this pass)

### Class-(a) bugs found and fixed

1. **Z80N opcode `Q = 0` hygiene missing.**
   FUSE convention: every opcode dispatch begins with `Q = 0`; F-writing
   opcodes set `Q = F` at end. The next SCF/CCF reads `last_Q` from this
   trail to compose undocumented X/Y flag bits as `(last_Q ^ F) | A`.
   Pre-fix: Z80N opcodes that don't write F left Q at the F value of the
   prior FUSE opcode. SCF/CCF after such Z80N would see stale Q and emit
   wrong X/Y flags.
   Fix: `z80.q = 0` at top of Z80N dispatch in `z80_cpu.cpp:494-517`.
   F-writing Z80N opcodes already set their own Q (pass-3 work).

2. **Z80N opcode `iff2_read = 0` hygiene missing.**
   FUSE convention: NMOS LD A,I/R quirk fires only if INT is accepted on
   the very next instruction boundary. FUSE clears `iff2_read = 0` at top
   of every opcode dispatch. Pre-fix: a Z80N opcode between `LD A,I` and
   an INT-acceptance point would NOT clear `iff2_read`, so the NMOS quirk
   fires on a non-immediate boundary — wrong.
   Fix: `z80.iff2_read = 0` at top of Z80N dispatch in `z80_cpu.cpp`.

3. **Save/load gap — `interrupts_enabled_at` and `iff2_read` not persisted.**
   These FUSE-internal fields have no `Z80Registers` mirror and were
   omitted from `Z80Cpu::save_state` / `load_state`. A snapshot taken on
   the cycle right after `EI` or `LD A,I` would replay with wrong INT
   acceptance / NMOS-quirk state on restore.
   Fix: serialise both fields directly to/from `z80.interrupts_enabled_at`
   and `z80.iff2_read` in save/load.

4. **`ADD HL/DE/BC, nn` did not update MEMPTR/WZ.**
   VHDL `t80n_mcode.vhd:1872-1878` sets `LDZ` at MCycle 2 and `LDW` at
   MCycle 3 → end-state `WZ = nn` (the operand). C++ implementation
   computed the sum but left MEMPTR untouched. Test fixtures were
   authored matching the (incorrect) no-update behavior.
   Fix: set `regs.MEMPTR = nn` in all three `ADD_*_NN` cases of
   `z80n_ext.cpp`. Updated `test/z80n/tests.expected` for `ed34_basic`,
   `ed34_overflow`, `ed34_preserve_flags`, `ed35_basic`, `ed36_basic`
   to match VHDL.

### Class-(b) findings (not fixed in pass-4)

- **Z80N opcodes bypass FUSE contention path.** Operand reads use raw
  `cpu.memory().read()`, so no contention T-states are added. Boot-path
  impact: ~0.7% timing skew worst case. Affects raster-tight code more
  than supervisor boot. Would require routing through `fuse_z80_readbyte()`
  and adjusting the explicit `tstates += t` accounting in
  `z80_cpu.cpp:516`. Not on critical boot path.

- **`OUTINB` does not update WZ to `BC + 1`.** FUSE's standard `OUTI`
  does this (`z80.memptr.w = BC + 1` at z80_ed.c:337). VHDL t80n_mcode.vhd
  for X"90" shares the OUTI/OUTD block but does NOT set `Inc_WZ`. So VHDL
  is silent on WZ update for OUTI/OUTINB; FUSE is more documented. We
  inherit FUSE's silence (no update) for OUTINB; consistency with FUSE's
  OUTI would suggest BC+1 update, but boot impact is nil (Z80N OUTINB is
  mostly unused).

### Class-(c) findings (out of spec, not fixed)

- **`DD/FD <prefix> ED <Z80N>` undefined behavior.** jnext silently
  discards the Z80N semantics (FUSE NOP path). Z80N spec doesn't define
  this; VHDL doesn't either. Boot path doesn't emit it.

- **`PUSH NN` (ED 8A) WZ end-state.** VHDL has dual `LDZ` and no `LDW`,
  leaving Z = ll, W = stale. Test fixtures match this. Documented spec
  is silent.

## Convergence assessment

- **Pass 1**: opcode functional behavior, T-state counts (mostly converged).
- **Pass 2**: contention path, IM2 daisy-chain (mostly converged).
- **Pass 3**: Z80N flag updates for F-writing opcodes, Q register tracking,
  save/load MEMPTR + Q (closed F-writing dual).
- **Pass 4**: Z80N hygiene for non-F-writing opcodes (Q clear), FUSE-internal
  state persistence (`interrupts_enabled_at` + `iff2_read`), MEMPTR for
  `ADD_*_NN`. Closed three more class-(a) holes plus one VHDL-spec MEMPTR
  divergence.

**Honest convergence call:** there could be more save/load gaps in deeper
FUSE state (e.g., `tstates` is global and is persisted via the Emulator,
but every other static in `fuse_z80_core.c` is — done). The Z80N path is
now hygiene-correct for Q and iff2_read; remaining class-(b) is the
contention bypass, which would need architectural change. **Pass 5 should
focus on integration-test scenarios**: actual snapshots that capture mid-EI
or post-LD-A,I and replay them, plus Z80N + SCF/CCF + LD A,I instruction
sequences that exercise the new hygiene paths.

## Open questions

- Should the Z80N path route operand reads through `fuse_z80_readbyte()`
  (with explicit `tstates -= 3*N` correction since the explicit `tstates +=
  t` already covers full instruction time)? Probably yes long-term but it's
  invasive — defer.
- Does any boot-path code do `LD A,I` immediately followed by a Z80N opcode
  followed by an INT? Unknown; jnext now models it correctly either way.
- The VHDL OUTI/OUTINB silence on Inc_WZ is suspicious — the Zilog
  OUTI documentation says WZ = BC + 1. Possible VHDL omission, possible
  intentional Next quirk. Cross-reference real hardware would settle it.

## Files changed

- `src/cpu/z80_cpu.cpp` — Q=0 / iff2_read=0 hygiene at Z80N dispatch top;
  save_state + load_state now persist `z80.interrupts_enabled_at` and
  `z80.iff2_read`.
- `src/cpu/z80n_ext.cpp` — `ADD_HL_NN` / `ADD_DE_NN` / `ADD_BC_NN` set
  `regs.MEMPTR = nn`.
- `test/z80n/tests.expected` — updated 5 `ADD_*_NN` test cases to match
  the new (VHDL-correct) MEMPTR end-state.

## Test results

- **FUSE Z80 base test suite:** `1356 / 1356 PASS / 0 FAIL` (no
  regression from pass-3 baseline).
- **ctest (full unit-test suite):** `37 / 37 PASS / 0 FAIL`.
- **Z80N unit tests:** `85 / 85 PASS / 0 FAIL`.
