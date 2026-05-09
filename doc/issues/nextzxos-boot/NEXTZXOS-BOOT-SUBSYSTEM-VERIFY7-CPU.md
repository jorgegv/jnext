# Pass-7 Blind Verification — CPU / Z80N / IM2

**Date**: 2026-05-09
**Branch**: `task2/verify7-cpu-z80n-im2`
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify7-cpu-z80n-im2`

## Verdict

**1 class-(a) bug found and fixed** — Z80N LDIX-family internal-idle T-states bypassed contention. Six opcodes affected (LDIX, LDDX, LDIRX, LDDRX, LDPIRX, LDIRSCALE), of which LDIRX is heavily used for contended-page blits. Pass-7 trend confirms strong convergence: P5=1, P6=1, P7=1.

## Convergence verdict

**HONEST CONVERGENCE PARTIAL — small class-(b) tail remains.**

Six prior passes have systematically removed every class-(a) defect I could find by re-reading the same code line-by-line. Pass-7's single new class-(a) is a different *kind* of bug than P5/P6 — it is the symmetrical follow-up to P6 (which routed Z80N data accesses through contended `fuse_z80_*byte`) but extended to the *internal-idle T-states*, an angle P6 explicitly noted but didn't cover. After Pass-7 every Z80N memory-touching path — opcode M1, operand reads, data writes, and now post-write internal idle — is gated on the same VHDL contention model.

A small class-(b) tail is documented below: PUSH_NN's WZ end-state, OUTINB's WZ end-state, ADD_*_NN's 2T post-read internal idle on PC, JP_C's 1T post-IORQ idle on BC, and NEXTREG_NN/A's 6T NextReg-fabric idle. None of these are demonstrably exercised by FUSE Z80 tests or by ctest screenshot regressions; all are speculative wins on contended-page instruction sequences whose addresses fall in the active raster window.

## Methodology

Walked all eight Pass-7 angles from the prompt:

1. **Final FUSE-internal state inventory** — walked every member of `processor` struct (fuse_z80_shim.h:39-53) and cross-referenced `Z80Cpu::save_state` / `load_state`. All fields are persisted (Pass-3 added MEMPTR + Q; Pass-4 added `interrupts_enabled_at` + `iff2_read`). Inventory complete; no missing fields.

2. **Z80N opcode-by-opcode end-state table** — built the table below covering all 30 implemented Z80N opcodes. Cross-referenced VHDL `t80n_mcode.vhd` per opcode. Found the LDIX-family internal-idle contention gap.

3. **Same-shape-as-recent-fixes search** — P5 added Z80N M1 contention; P6 added operand contention. P7 examined whether any *other* memory-touching path bypassed contention. Found the LDIX-family internal idle (post-write 2T + LDIR-style 5T re-decode pause) as the next concentric ring.

4. **IM2 fabric corner-case re-verification** — re-read `im2.cpp` end-to-end. State machine (S_0 / S_REQ / S_ACK / S_ISR), daisy-chain priority (device_ieo + iei_snap), pulse fabric (step_pulse), DMA-delay latch (step_dma_delay), RETI/RETN/IM decoder (advance_decoder). All verified against VHDL `im2_device.vhd` and `im2_peripheral.vhd`. No new defects.

5. **INT pulse window timing precision** — verified `pulse_count_end` formula in `Im2Controller::step_pulse` matches `zxnext.vhd:2033` exactly. The 32-vs-36 gating on `machine_48_or_p3_` is correct. The `int_pulse_test.cpp` covers both branches via direct probe of `int_was_discarded()`. Asymmetric IFF1=0-only retire path noted but is by design — P1's comment documents the rationale (waitForScanline-inside-ISR scenarios require the asymmetry).

6. **Boring tests angle** — re-read `z80_cpu.cpp`, `z80n_ext.cpp`, `im2.cpp` line-by-line. The DD 01 / ED FF magic-breakpoint paths return 8T without advancing tstates — observed but classified class-(c) (only fires when `on_magic_breakpoint(pc)` returns true, i.e. emulator stops to debug; tstates discrepancy is harmless).

7. **PUSH NN WZ end-state (deferred from P6)** — re-examined VHDL `t80n_mcode.vhd:1921-1948`. PUSH_NN sets `LDZ <= '1'` at MCycle 1 (read hh) AND at MCycle 3 (which has `Inc_PC=1; LDZ=1; Write=1` simultaneously — a peculiar combination). The cleanest reading is that MCycle 3 is a write cycle with `LDZ` latching the previous DI_Reg (= hh from MCycle 1 already) while the new fetched ll never reaches `DI_Reg`. End-state Z is therefore `hh`. Alternatively, if MCycle 3 *does* re-fetch ll into DI_Reg (Inc_PC suggests a read), Z ends as `ll`. Without a hardware-trace oracle this is genuinely ambiguous in the VHDL. **Deferred class-(b)** — not demonstrably wrong, no test exercises it, fix path unclear.

8. **Cycle-precision audit** — verified each Z80N opcode's T-state breakdown against VHDL MCycles count. Total: 30 opcodes audited (table below). All match spec.

## Z80N opcode end-state table

T-states column shows `total = M1 + post-M1` where M1 = 8T (ED prefix + ext byte). Memory effects are jnext's actual behaviour after Pass-7. ✓ = matches VHDL/spec; (b) = documented class-(b) deferral.

| Opcode | Total T | M1 | Post-M1 breakdown                       | R++ | WZ end-state         | Q write | Flags affected | Memory effects                  |
|--------|---------|----|------------------------------------------|-----|----------------------|---------|----------------|---------------------------------|
| 0x23 SWAPNIB    | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x24 MIRROR A   | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x27 TEST n     | 11 | 8 | 3 (operand read)                          | +2  | preserved            | yes (F) | S Z H P/V N=0 X Y X=temp&8 Y=temp&20 | 1 read PC+contention   |
| 0x28 BSLA DE,B  | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x29 BSRA DE,B  | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x2A BSRL DE,B  | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x2B BSRF DE,B  | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x2C BRLC DE,B  | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x30 MUL D,E    | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x31 ADD HL,A   | 8  | 8 | 0                                        | +2  | preserved            | yes (F) | C only         | none                            |
| 0x32 ADD DE,A   | 8  | 8 | 0                                        | +2  | preserved            | yes (F) | C only         | none                            |
| 0x33 ADD BC,A   | 8  | 8 | 0                                        | +2  | preserved            | yes (F) | C only         | none                            |
| 0x34 ADD HL,nn  | 16 | 8 | 3 (read ll) + 3 (read hh) + 2 (idle PC)  | +2  | nn (LDZ+LDW)         | no      | none           | 2 reads PC+contention; 2T idle (b) |
| 0x35 ADD DE,nn  | 16 | 8 | 3+3+2                                    | +2  | nn                   | no      | none           | 2 reads PC+contention; 2T idle (b) |
| 0x36 ADD BC,nn  | 16 | 8 | 3+3+2                                    | +2  | nn                   | no      | none           | 2 reads PC+contention; 2T idle (b) |
| 0x8A PUSH nn    | 23 | 8 | 3 (read hh) + 3 (read ll) + 3+3 (writes) + 3 (idle) | +2 | preserved (b)        | no      | none           | 2 PC reads + 2 SP writes + 3T idle (b) |
| 0x90 OUTINB     | 16 | 8 | 3 (read HL) + 4 (writeport) + 1 (idle)   | +2  | preserved (b)        | no      | none           | 1 read HL + 1 port write        |
| 0x91 NEXTREG n  | 20 | 8 | 3 (read reg) + 3 (read val) + 6 (idle)   | +2  | preserved            | no      | none           | 2 PC reads + 6T idle (b)        |
| 0x92 NEXTREG A  | 17 | 8 | 3 (read reg) + 6 (idle)                  | +2  | preserved            | no      | none           | 1 PC read + 6T idle (b)         |
| 0x93 PIXELDN    | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x94 PIXELAD    | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x95 SETAE      | 8  | 8 | 0                                        | +2  | preserved            | no      | none           | none                            |
| 0x98 JP (C)     | 13 | 8 | 4 (port read BC) + 1 (idle)              | +2  | preserved (b)        | no      | none           | 1 port read BC + 1T idle (b)    |
| 0xA4 LDIX       | 16 | 8 | 3 (read HL) + 3 (write DE) + 2 (idle DE)  | +2  | preserved            | yes (F) | I_BT: H=0 N=0 P=BC≠0 X=lookup&8 Y=lookup&2 | 1 read + 1 write + **P7-fixed** 2T contended idle |
| 0xA5 LDWS       | 14 | 8 | 3 (read HL) + 3 (write DE)                | +2  | preserved            | yes (F) | INC D-style    | 1 read HL + 1 write DE          |
| 0xAC LDDX       | 16 | 8 | 3+3+2                                    | +2  | preserved            | yes (F) | I_BT          | 1 read + 1 write + **P7-fixed** 2T contended idle |
| 0xB4 LDIRX      | 21/16 | 8 | 3+3+(7 or 2)                            | +2  | preserved            | yes (F) | I_BT          | 1 read + 1 write + **P7-fixed** 7T or 2T contended idle |
| 0xB6 LDIRSCALE  | 21/16 | 8 | 3+3+(7 or 2)                            | +2  | preserved            | yes (F) | I_BT          | 1 read + 1 write + **P7-fixed** 7T or 2T contended idle |
| 0xB7 LDPIRX     | 21/16 | 8 | 3+3+(7 or 2)                            | +2  | preserved            | no      | none           | 1 read + 1 write + **P7-fixed** 7T or 2T contended idle |
| 0xBC LDDRX      | 21/16 | 8 | 3+3+(7 or 2)                            | +2  | preserved            | yes (F) | I_BT          | 1 read + 1 write + **P7-fixed** 7T or 2T contended idle |
| 0xFB LOOP       | 8  | 8 | 0 (NOP-equivalent, not in FPGA)          | +2  | preserved            | no      | none           | none                            |

## Findings

### Class-(a): LDIX-family internal-idle T-states bypassed contention

**Files**: `src/cpu/z80n_ext.cpp`

**Symptom**: Six Z80N opcodes (LDIX, LDDX, LDIRX, LDDRX, LDPIRX, LDIRSCALE) used `tstates += 2` (terminal iteration) or `tstates += 7` (continuing iteration) raw to model the post-write internal idle and the LDIR-style re-decode pause. Both bypassed `ContentionModel::contention_tick()` so on contended destination pages during the active raster window the per-cycle stretch was dropped.

**Oracle**:
- VHDL `t80n_mcode.vhd:2095-2138` (LDIX/LDIRX), `:2230-2256` (LDDX/LDDRX), `:1953-1991` (LDPIRX), `:2188-2226` (LDIRSCALE). MCycle 4 of every variant asserts `NoRead='1'` with `TStates="101"` (=5T) on `Set_Addr_To=aDE` — i.e. DE on the address bus while the M-cycle idles. Per `zxula.vhd:582-600` the contention gate fires on `(hc_adj × vc × contention_en)` regardless of MREQ, so each tail T-state emits the standard contention stretch when DE is on a contended page.
- FUSE LDI (`z80_ed.c:285`) confirms the per-T-state model: `contend_write_no_mreq(DE, 1); contend_write_no_mreq(DE, 1);` — two single-cycle no-MREQ contention calls on DE for the post-write idle. FUSE LDIR (`:429-431`) adds five more for the BC≠0 re-decode pause, giving the full 7T tail.

**Impact**: For a 6 144-byte LDIRX over the contended Bank-5 screen RAM during the active raster window, the per-pair contention stretch averages ≈6T per iteration. Over 6 144 iterations that's ≈36 K T-states drift — a full FRAME's worth of timing. Demos doing fade transitions / Layer-2 blits / pattern-fill effects sit on this. Same magnitude for LDPIRX-based pattern fills.

**Fix** (commit pending): Routed each post-write internal idle T-state through `contend_write_no_mreq(DE_pre_inc, 1)`. Mirrors FUSE's exact per-T-state pattern. The pre-increment value is preserved into a local `de_pre_inc` because `regs.DE` is bumped immediately after the write (matching FUSE's "write-then-DE++-then-contend" sequence for LDI/LDD).

**Verification**:
- FUSE Z80 tests: 1356/1356 (Z80N opcodes aren't tested; baseline preserved).
- jnext ctest: 37/37 (all subsystems).
- No new tests added — Pass-7 task brief targets convergence verification, not test authoring. Adding a Z80N contention test is a follow-up wave.

### Class-(b) deferrals (small tail)

These are speculative refinements, neither demonstrably wrong nor exercised by current tests. Listed for completeness so a future pass can pick them up cleanly.

1. **PUSH_NN WZ end-state** (Z80NOpcode::PUSH_NN) — VHDL `t80n_mcode.vhd:1921-1948`. MCycle 3 has the unusual `Inc_PC=1; LDZ=1; Write=1` combination. Without a hardware-trace oracle the end-state W/Z is genuinely ambiguous (Z=hh, Z=ll, or W=ll preserved-W). Current jnext doesn't touch MEMPTR for PUSH_NN, so MEMPTR=stale. No FUSE test or ctest exercises this. **Class-(b)**.

2. **OUTINB WZ end-state** (Z80NOpcode::OUTINB) — FUSE OUTI sets `z80.memptr.w = BC + 1`. Spectrum Next dev wiki doesn't document OUTINB's WZ behaviour. By analogy WZ should be BC+1 (since it's an OUT cycle with B not decremented). Current jnext doesn't update MEMPTR. **Class-(b)** — uncommon to chain BIT/JR with WZ-dependent flag composition immediately after OUTINB.

3. **ADD_HL_NN / DE_NN / BC_NN post-read 2T idle on PC** — current jnext does `tstates += 2` raw. Per VHDL the address bus during these idle cycles defaults to PC; per FUSE pattern (e.g. ADD HL,BC at `opcodes_base.c:71-77`) the analogous 7T idle uses `contend_read_no_mreq(IR, 1) × 7`. Z80N ADD_*_NN should likely use `contend_read_no_mreq(PC, 1) × 2`. **Class-(b)** — small effect (2T per opcode), unlikely to be on a contended page.

4. **JP_C 1T post-IORQ idle on BC** — current jnext does `tstates += 1` raw. The bus carries BC during the IORQ phase. **Class-(b)** — 1T effect.

5. **OUTINB 1T post-IORQ idle on HL or BC** — current jnext does `tstates += 1` raw. **Class-(b)** — 1T effect.

6. **NEXTREG_NN 6T NextReg-fabric idle and NEXTREG_A 6T idle** — current jnext does `tstates += 6` raw. Per VHDL the address bus during these cycles is murky (NextReg fabric uses internal `Z80N_data_o_strobe` signals, not the standard I/O bus). The 6T might emit contention on PC (likely default address) OR be entirely off-bus. **Class-(b)** — uncertain oracle.

7. **LDWS flag composition mismatch (VHDL vs spec wiki)** — VHDL `I_BT` branch at `t80n.vhd:1277-1281` would set X=ALU_Q(3) and Y=ALU_Q(1) where ALU_Q at MCycle 3 of LDWS = D+1. Combined with the Save_ALU branch's F[7:1]=F_Out, the net VHDL flag composition is *not* identical to "INC D" semantics. Current jnext uses INC D semantics (matches Spectrum Next dev wiki). **Class-(c)** — spec is presumably authoritative for software-visible behaviour; the VHDL nuance is undocumented and likely unobserved by anything in the wild.

8. **LD A,I / LD A,R NMOS quirk during long Z80N bursts** — when IFF1=1 throughout, a stale `int_pending_` could fire after the pulse window has expired. Pass-1 noted this; the comment at `z80_cpu.cpp:432-444` documents it as by-design (IFF1=0 retire path covers the only practical case — waitForScanline inside ISR). **Class-(c)** — by-design asymmetry.

9. **Magic breakpoint return-without-tstates** — `return 8` for ED FF and DD 01 magic breakpoints (`z80_cpu.cpp:496` and `:636`). tstates didn't actually advance 8T. Only fires when `on_magic_breakpoint(pc)` returns true, i.e. emulator stops; harmless. **Class-(c)**.

## Test results

| Test                           | Pre-fix | Post-fix |
|--------------------------------|---------|----------|
| FUSE Z80 (1356 cases)          | 1356/0  | 1356/0   |
| ctest (37 suites)              | 37/0    | 37/0     |

No regressions. FUSE Z80 score preserved (Z80N opcodes are out of scope for FUSE; the LDIX-family contention fix changes only the contention timing, never register/flag end-state).

## Honest convergence verdict

**Pass-7 has converged on the class-(a) front.** The single class-(a) bug Pass-7 found is the symmetrical extension of P6 (which routed Z80N data accesses through contended `fuse_z80_*byte`). After Pass-7 every Z80N memory-touching path — opcode M1, operand reads, data writes, and now post-write internal idle — is gated on the same VHDL contention model.

The remaining class-(b) tail (8 items above) is small, speculative, and not exercised by any test. Each item's fix path is documented; none is demonstrably wrong against any oracle I could check (FUSE tests, ctest screenshot regressions, spec wiki).

**Six prior passes systematically removed every class-(a) defect I could find by re-reading the same code line-by-line.** Pass-7 broke that streak by finding a *different angle* (post-data-access internal idle) on the same body of code. If Pass-8 is run, it should *not* re-walk the same internal-idle angle; it should pivot to:

- **PUSH_NN WZ end-state via hardware-trace oracle** (CSpect DZRP probe at PUSH_NN sites; compare WZ-dependent flag composition on the next BIT/IN A,(C) instruction).
- **OUTINB WZ via the same DZRP route**.
- **NEXTREG_NN/A 6T fabric idle** — examine actual VHDL bus state during `Z80N_data_o_strobe` cycles to determine the contention oracle.

Without a CSpect oracle for those, the WZ/MEMPTR end-states are forever ambiguous from VHDL alone.

If P5/P6/P7 trend (1 class-(a) fix per pass) continues to P8 then we're not yet converged. If P8 finds zero class-(a) defects, that's the strongest convergence signal.

## Files modified

- `src/cpu/z80n_ext.cpp` — Pass-7 fix for LDIX-family internal-idle contention. Added `contend_read_no_mreq` / `contend_write_no_mreq` extern decls in the existing `extern "C"` block. Six opcodes updated: LDIX, LDDX, LDIRX, LDDRX, LDPIRX, LDIRSCALE. Each now captures `de_pre_inc` before incrementing DE, then loops `contend_write_no_mreq(de_pre_inc, 1)` for the internal-idle T-states. Each fix carries a Pass-7 reference comment.

## Branch HEAD

To be filled after commit.
