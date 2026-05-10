# NEXTZXOS Boot Subsystem — Pass-16 CPU (Z80 + Z80N + IM2) Verify Audit

**Branch:** `task2/verify16-cpu-z80n-im2` (off integration HEAD `267764b`)
**Worktree:** `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify16-cpu-z80n-im2`
**Date:** 2026-05-10
**Mode:** BLIND (no read of `doc/issues/nextzxos-boot/` priors)

## Summary

| Class | Count |
|-------|-------|
| (a)   | 1     |
| (b)   | 0     |
| (c)   | 0     |
| (d)   | 0     |
| **Total** | **1** |

- **Tests:** ctest 38/38, FUSE Z80 1356/1356.
- **Build:** Release, `-DENABLE_QT_UI=ON`.
- **Pass-15 status check:** Pass-15 reviewer caught V15-CPU-NIT-03
  (ULA+ port contention shadow propagation) and fixed it inline.
  Pass-16 inspected the surrounding seam and found a one-line gap in
  the same family — ContentionModel's `port_ulap_io_en_` shadow is
  not re-pushed by `Emulator::load_state`. The rest of the CPU surface
  was re-scanned and is clean.

## Methodology

15 prior passes have closed the major CPU/Z80N/IM2 families. Pass-16
focused on **fresh angles** to avoid re-treading closed ground:

1. **Contention shadow re-push completeness in load_state** —
   compared `port_7ffd_io_en` (Verify12-memory class-(b) precedent)
   with the recently-added `port_ulap_io_en` (V15-CPU-NIT-03) for
   symmetric load-state coverage. **Found V16-CPU-01.**

2. **Other port contention OR-terms** — VHDL `port_contend`
   (zxnext.vhd:4496) is `(NOT cpu_a(0)) OR port_7ffd_active OR
   port_bf3b OR port_ff3b`. No additional NR-shadow gates —
   `port_dffd` / `port_1ffd` are not part of the OR-tree. ✓

3. **DD/FD prefix coverage exhaustive** — V14-CPU-NIT-01 (Pass-14)
   already extended the inner-opcode walk to INC/DEC BC + DJNZ. The
   walk in `Z80Cpu::execute()` (z80_cpu.cpp:819-842) now covers all
   IncDecZ-affecting opcodes through any DD/FD chain length. ✓

4. **CB-prefix opcode coverage** — `DD CB d <op>` and `FD CB d <op>`
   sequences: only DD and CB are M1; the d displacement and inner op
   are DATA reads (per FUSE z80_ddfd.c case 0xcb at line 511, and the
   z80_cpu.cpp on_m1_cycle walk at lines 752-757 — comments
   explicitly note this). VHDL im2_control.vhd FSM keys on
   `ifetch_fe_t3`, so non-M1 bytes don't advance the decoder. ✓

5. **NMI handling exhaustive** — HALT during NMI handled by
   `fuse_z80_nmi()` line 167 (`if (z80.halted) { PC++; halted=0; }`);
   `Z80Cpu::execute()` pre-computes `saved_pc = (PC+1)&0xFFFF` for
   the `on_nmi_servicing` callback (line 425) so NR 0xC2/0xC3 capture
   matches what gets pushed. NMI mid-instruction blocked by FUSE's
   single-instruction execute() granularity (matches VHDL line 1765
   `if NMI_s = '1' and Prefix = "00"`). NMI during EI grace fires
   correctly (NMI is edge-triggered, no IFF1 gate). ✓

6. **IM mode switching mid-instruction** — `Z80Cpu::execute()` runs
   one full instruction per call; IM mode changes only land at
   instruction boundaries (the FUSE single-instruction shape). NR
   0xC0 mode switching pushed via `Im2Controller::set_mode()` at NR
   write time, no race since it's between instructions. ✓

7. **Refresh register R behavior** — `R++` in fuse_z80_core.c:133
   (NMI), 170 (NMI again), 205 (each opcode); `Z80Cpu::execute()` at
   line 586 also does `z80.r = (z80.r + 2) & 0x7F;` for Z80N opcodes
   (one for ED, one for ext byte). Bit-7 preserved via separate
   `r7` field (FUSE convention). LD A,R correctly composes
   `(R&0x7f) | (R7&0x80)` (z80_ed.c:167). VHDL t80n.vhd:493 confirms
   "R(6 downto 0) <= R(6 downto 0) + 1" (low 7 bits). ✓

8. **PC wrap at 0xFFFF / SP at 0x0000** — FUSE uses 16-bit register
   types with natural wrap; `&0xFFFF` masks in jnext are belt-and-
   suspenders. NMI with PC=0xFFFF: PC+1 wraps to 0x0000. NMI with
   SP=0x0000: --SP wraps to 0xFFFF. Both behave per Z80 spec. ✓

9. **CPU reset paths** — hard reset clears all regs; soft reset
   preserves alternates. `Z80Cpu::reset()` calls `fuse_z80_reset(1)`
   (always hard) but the Emulator's run-time soft reset path
   (NR 0x02 trampoline) is owned by NextReg/NmiSource, not Z80Cpu.
   Out of scope for this audit. ✓

10. **DMA /BUSREQ sharing** — `Emulator::run_frame` line 5229 gates
    CPU execution behind `dma_.is_active()`. CPU is properly stalled
    during DMA bursts. ✓

11. **EI grace + INT pulse window** — `Z80Cpu::execute()` lines
    479-512 correctly handle EI-grace (one-instruction window) and
    pulse-expire (`tstates - int_requested_at_ > pulse_width &&
    !iff1`). The pulse-expire branch is gated on `!iff1` because
    iff1=1 takes the service branch (which has its own EI-grace
    check). Cross-frame analysis: if iff1=0 throughout the pulse
    window, the per-execute() top check fires before iff1 transitions.
    No bug. ✓

12. **Save/load field ordering** — `Z80Cpu::save_state` and
    `Z80Cpu::load_state` (z80_cpu.cpp:920-999) use matched `write`/
    `read` pairs in identical order: regs (12 u16), I/R, IFF1/IFF2,
    IM, halted, MEMPTR (Pass-3), Q (Pass-3), interrupts_enabled_at
    (Pass-4), iff2_read (Pass-4), nmi_pending, int_pending,
    int_vector, int_requested_at_. ✓

13. **OUTINB extended-M1 contention** — Pass-12 NIT-02 fix already
    routes the 1T extended-M1 cycle through `contend_read_no_mreq`
    on IR address. ✓

14. **Z80N R increment** — Z80Cpu::execute() line 586 `z80.r =
    (z80.r + 2) & 0x7F;`. Two M1 cycles (ED + ext) per Z80N opcode.
    Iterative Z80N (LDIRX etc.) re-fetches both bytes on each
    iteration via PC rewind, so R increments by 2 per iteration —
    matches VHDL t80n_mcode.vhd MCycles="100" with re-decode. ✓

15. **PUSH NN VHDL byte order / WZ end-state** — Pass-8 fix already
    documented and implemented. C++ writes `(SP+1)<-hh; SP<-ll`
    (matches VHDL MCycle 3 and 5 ordering). WZ-lo = ll, WZ-hi
    preserved. ✓

16. **Z80N flag composition (ADD HL/DE/BC,A; LDPIRX; etc.)** —
    Pass-10 fix already covers F.C clearing and I_BT flag composition
    for LDPIRX. Other Z80N opcodes (MUL_DE, SWAPNIB, etc.) don't
    write F per VHDL. ✓

17. **DivMMC automap on M1** — `on_m1_prefetch` fires once per
    `Z80Cpu::execute()` for the FIRST M1 only. DivMMC entry points
    are fixed PCs (0x0000, 0x0008, 0x0038, 0x0066, 0x04D7, 0x0562)
    and only ever match the first M1. Multi-byte prefix sequences
    (DD/FD/ED/CB) on a non-entry PC don't trigger automap. Correct
    per VHDL combinational decode. ✓

18. **NmiSource M1 observation** — `on_m1_prefetch` also feeds
    NmiSource for PC=0x0066 detection (NR 0xC2/0xC3 capture window).
    Same single-fire-per-instruction convention. ✓

## Findings

### V16-CPU-01 — class-(a)

**Title:** `Emulator::load_state` does not re-push
`port_ulap_io_en` shadow from NR 0x85 bit 0; ULA+ contention
silently disabled until next NR 0x85 write after load.

**VHDL oracle:**
- `zxnext.vhd:2439` — `port_ulap_io_en <= internal_port_enable(24);`
  (bit 24 is the first bit of nr_85, i.e. NR 0x85 bit 0)
- `zxnext.vhd:2685-2686` — `port_bf3b/port_ff3b` are AND-gated by
  `port_ulap_io_en`
- `zxnext.vhd:4496` — `port_contend <= (NOT cpu_a(0)) OR
  port_7ffd_active OR port_bf3b OR port_ff3b;`
- `zxnext.vhd:1229` — `nr_85_internal_port_enable` resets to all-1
- `zxnext.vhd:5800-5828` (precedent for shadow-not-serialized
  flip-flops persisting across non-reset edges)

**Symptom:** After `Emulator::load_state`, the
`ContentionModel::port_ulap_io_en_` shadow stays at whatever the
runtime instance previously held — NOT the value implied by the
just-restored NR 0x85 bit 0. If the snapshot has NR 0x85 b0 = 1
(power-on default — all four port-enable bits set, 0x0F low nibble)
but the live model previously had the shadow pulled to 0 (e.g. via
a prior NR 0x85 write that subsequently reverted), the post-load
shadow stays 0. Result: ULA+ port IORQs to $BF3B/$FF3B during the
active raster window on a 128K/+3 machine miss the per-cycle
contention stretch — exactly the gap V15-CPU-NIT-03 closed for the
init() seam, just in the load-state seam instead.

**Pre-fix code (emulator.cpp:6511-6524):**

```cpp
{
    contention_.rebuild_for_type(mmu_.machine_type());

    const uint8_t cs07 = static_cast<uint8_t>(nextreg_.cached(0x07) & 0x03);
    contention_.set_cpu_speed(cs07);
    contention_.set_pending_cpu_speed(cs07);
    contention_.set_contention_disable(mmu_.contention_disabled());
    contention_.set_port_7ffd_io_en((nextreg_.cached(0x82) & 0x02) != 0);
    // ← no set_port_ulap_io_en push
}
```

**Post-fix code:**

```cpp
{
    contention_.rebuild_for_type(mmu_.machine_type());

    const uint8_t cs07 = static_cast<uint8_t>(nextreg_.cached(0x07) & 0x03);
    contention_.set_cpu_speed(cs07);
    contention_.set_pending_cpu_speed(cs07);
    contention_.set_contention_disable(mmu_.contention_disabled());
    contention_.set_port_7ffd_io_en((nextreg_.cached(0x82) & 0x02) != 0);
    contention_.set_port_ulap_io_en((nextreg_.cached(0x85) & 0x01) != 0);
}
```

**Failure mode:** `rebuild_for_type` (contention.cpp:20-31)
explicitly preserves dynamic gate state ("rebuild_for_type preserves
dynamic gate state, so the subsequent gate re-push isn't clobbered"
per the load_state inline comment) — so the shadow doesn't get
spuriously cleared. But it ALSO doesn't get refreshed from the
just-restored NR 0x85. Same Verify12-memory class-(b) gap pattern
that the existing comment references for `port_7ffd_io_en` —
the fix added `port_7ffd_io_en` push but not `port_ulap_io_en` push.

**Discriminative regression test:** `test_v16_cpu_01_load_state_repushes_port_ulap_io_en`
in `test/rewind/rewind_test.cpp` — exercises Emulator save/load with
shadow planted false post-save, asserts post-load shadow == true and
post-load `contention_tick` at $BF3B fires non-zero stretch.

Reverting the one-line fix flips both assertions to FAIL (verified):

```
FAIL [test/rewind/rewind_test.cpp:374] V16-CPU-01: load_state re-pushes port_ulap_io_en from NR 0x85 b0
FAIL [test/rewind/rewind_test.cpp:390] post-load contention_tick at $BF3B with default param fires non-zero stretch
```

Re-applying returns both to PASS.

**Doc fixups bundled with this fix (no functional impact):**

1. `src/memory/contention.h:177-178` — corrected the `contention_tick`
   doxygen comment from "mirrors NR 0x82 bit 4" to the
   VHDL-faithful "mirrors NR 0x85 bit 0 (zxnext.vhd:2439)" with a
   note about the OR-fold semantics.
2. `src/memory/contention.cpp:143` — corrected the inline comment
   from "NR 0x82 bit 8" to "NR 0x85 bit 0".

## Class-(d) escalations

**None.** No architectural-refactor finding required this pass. The
sole finding is a one-line completion of the V15-CPU-NIT-03 fix
family.

## Items considered and not classed as findings

- **Stale `int_pending_` survival across frame boundary if iff1=1
  at frame end.** Investigated: requires (a) INT requested very late
  in frame N (within ~32 T-states of frame end), (b) iff1=1 across
  the boundary (no DI). The unsigned subtraction in the pulse-expire
  check `tstates - int_requested_at_` wraps cleanly to a huge value
  > 32 in frame N+1, so the check would discard if iff1=0. With
  iff1=1, the service branch fires; on_int_ack sees no S_REQ device
  (the ULA INT request was consumed once in frame N if accepted, or
  the device cleared via clear() in `Im2Controller::clear` on the
  next ULA scheduler arm). Concrete scenario unreachable in practice
  because the ULA INT scheduler in `run_frame()` line 5067 fires near
  the START of each frame (`frame_int_master_cycle_offset()`), not
  near the end. **Not a finding.**

- **Pending `cpu_speed` shadow not separately re-pushed in load_state.**
  load_state at emulator.cpp:6530-6531 calls both `set_cpu_speed`
  and `set_pending_cpu_speed` with the same NR 0x07 value, so post-
  load shadow == effective. If snapshot was taken mid-commit window
  (shadow != effective), that information is lost — but neither
  field is serialized, so the snapshot never carried the divergence
  in the first place. **Not a finding** (architecturally consistent
  with the not-serialized convention).

- **`contention_disable_shadow` not separately re-pushed.** load_state
  at emulator.cpp:6532 calls `set_contention_disable` which updates
  BOTH shadow AND effective to the same value (per the contention.h
  contract — line 93-96). So post-load both fields are coherent.
  **Not a finding.**

## Build & test outputs

```
Release build (CMake -DENABLE_QT_UI=ON):
  [100%] Linking CXX executable jnext
  [100%] Built target jnext

ctest:
  100% tests passed, 0 tests failed out of 38
  Total Test time (real) = 0.42 sec

FUSE Z80:
  Total: 1356  Passed: 1356  Failed:    0  Skipped:    0
```

## Files touched

- `src/core/emulator.cpp` — one-line fix at load_state (port_ulap_io_en
  re-push) + 8-line comment expansion documenting the gap closure.
- `src/memory/contention.h` — doc-comment correction (NR 0x82 b4 →
  NR 0x85 b0; note about OR-fold semantics).
- `src/memory/contention.cpp` — inline-comment correction (NR 0x82 b8
  → NR 0x85 b0).
- `test/rewind/rewind_test.cpp` — new discriminative test (~ 100 lines
  including comments) wired into main().

## Convergence note (Pass-16)

Pass-15 reviewer caught V15-CPU-NIT-03 (a missed finding from a
"defensible-zero" Pass-15 audit). Pass-16 explicitly re-tests
convergence by hunting the same family of issues (port-shadow
propagation across all Emulator seams).

**Result:** Pass-16 found ONE additional finding (V16-CPU-01) in the
exact same family — load_state re-push completeness. The fix is a
strict superset of V15-CPU-NIT-03 (init() push was already done;
this completes the load_state push).

CPU subsystem is **NOT yet honestly converged** for Pass-17 skipping —
the same family has now produced findings in 3 consecutive passes
(Pass-13 IncDecZ shadow rebroadcast, Pass-14 prefix-walk for IncDecZ,
Pass-15 ULA+ shadow + Pass-16 load_state push). The pattern suggests
that "shadow propagation" is a tractable but subtle area where new
findings can still surface as adjacent code is scanned. **Recommend
Pass-17 continues with CPU in scope** to verify whether this finding
is the last in the family (e.g., are there other ContentionModel
shadows that load_state misses? — none identified, but a fresh
reviewer should re-scan).
