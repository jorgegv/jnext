# Pass-13 Independent Review — NMI + Multiface + Port + NextREG Subsystem

**Reviewer**: Pass-13 independent reviewer (no prior pass reports read).
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify13-nmi-mf-port-reviewer`
**Branch**: `task2/verify13-nmi-mf-port-reviewer` off `task2/verify13-nmi-mf-port` HEAD `943f656`.
**Audit head reviewed**: `943f656`.
**Verdict**: **APPROVE-WITH-NITS**.

## Summary

Pass-13 found one class-(b) finding (V13-NMP-01) — NR 0x05 bit 2
(`nr_05_5060`) cache leakage in Pentagon mode — and added a fix in the
same commit chain. The finding is real, the VHDL claim is correctly
cited, the fix is correct in shape and behaviour, the discriminative
test fails as predicted on revert, and the full Release-mode test
suite passes (ctest 38/38, FUSE 1356/1356, nextreg integration
235/235). The subsystem-wide hunt for similar patterns confirms
V13-NMP-01 is the **only** instance of the runtime-priority-1-clear
gate in `zxnext.vhd` clocked processes — defensible-zero for the rest
of the family.

Three small documentation NITs are catalogued below; none affect
correctness or change the verdict.

## VHDL claim verification

The audit cites `cores/zxnext/src/zxnext.vhd:5832-5841`:

```vhdl
process (i_CLK_28)
begin
   if rising_edge(i_CLK_28) then
      if nr_03_machine_timing(2) = '1' then
         nr_05_5060 <= '0';   -- Pentagon is always 50 Hz
      elsif nr_05_we = '1' then
         nr_05_5060 <= nr_wr_dat(2);
      elsif hotkey_5060 = '1' then
         nr_05_5060 <= not nr_05_5060;
      end if;
   end if;
end process;
```

Confirmed verbatim at lines 5832-5841. The IF branch (line 5835) is
priority-1: while Pentagon timing is active, the FF is forced to '0'
on every clock edge regardless of `nr_05_we` or `hotkey_5060`. The
audit's interpretation is exact.

The frame-sync latch at `:6701` (`eff_nr_05_5060 <= nr_05_5060`)
copies the cleared FF into the read-mux source on every video frame.
Until Pentagon is exited and a fresh write/hotkey sets bit 2, the
read-mux returns 0.

## Fix correctness

Two seams, both at `src/core/emulator.cpp`, both correctly modelled
on the V11-NMP-02 / V11-NMP-03 mask-canonicalisation pattern:

1. **NR 0x05 write_handler** (lines 1108-1119): when Pentagon timing
   is active, returns `v & ~0x04`, so `regs_[0x05]` stores the bit-2-
   cleared canonical byte. Joystick decode is unaffected by bit 2
   (joy0_bits=`{v[3],v[7],v[6]}`, joy1_bits=`{v[1],v[5],v[4]}` per
   `src/input/joystick.cpp:67-74`).

2. **NR 0x03 write_handler timing→Pentagon edge** (lines 1989-1997):
   on the timing→Pentagon transition, calls
   `nextreg_.write(0x05, cached & ~0x04)` to canonicalise the cache
   in lock-step with the VHDL FF clear.

Read-side mask at lines 1151-1170 (Pass-10 TC-NR05-PENTAGON) remains
correct for the Pentagon-live read window. The new write/edge
canonicalisations extend correctness to the Pentagon-exit window
where the read mask no longer fires.

I traced the full sequence-A and sequence-B paths manually:

**Sequence A (write-while-Pentagon)**:
- Pentagon ON, NR 0x03 = 0xC0 → machine_timing = 0x04
- NR 0x05 write 0x04 → write_handler: pentagon=true → returns
  `0x04 & ~0x04 = 0x00` → regs_[0x05] = 0x00
- NR 0x03 = 0xB0 → machine_timing = 0x03 (Pentagon off)
- NR 0x05 read → read_handler: pentagon=false → bit 2 sourced from
  cached = 0 → returns 0x00 ✓ (matches VHDL FF state)

**Sequence B (Pentagon-edge-clear)**:
- Pentagon OFF (default 0x03 +3), NR 0x05 write 0x04 → write_handler:
  pentagon=false → returns 0x04 → regs_[0x05] = 0x04 (FF latches 1)
- NR 0x05 read → read_handler: pentagon=false → returns 0x04 (got_b_pre)
- NR 0x03 = 0xC0 → machine_timing = 0x04. Edge fix fires:
  `nextreg_.write(0x05, cached(0x05) & ~0x04)` = `nextreg_.write(0x05, 0x00)`.
  Routes through write_handler, which, with Pentagon now active,
  returns 0x00 & ~0x04 = 0x00 → regs_[0x05] = 0x00.
- NR 0x03 = 0xB0 → machine_timing = 0x03 (Pentagon off)
- NR 0x05 read → cached = 0 → returns 0x00 ✓

Both discriminators correct.

## Discriminative revert check

I temporarily reverted both seams (NR 0x05 write_handler bit-2 mask
and NR 0x03 timing→Pentagon edge cache clear) at lines 1108-1119 and
1989-1997 respectively, rebuilt `nextreg_integration_test`, and ran:

```
[FAIL] V13-NMP-01: NR 0x05 bit 2 Pentagon-mode cache canonicalisation:
       (a) write-while-Pentagon does not leak; (b) Pentagon-engagement
       clears prior bit-2 latch [zxnext.vhd:5832-5841 / :5897 / :6701]
       [timing_a=0x04 got_a=0x04 got_b_pre=0x04 timing_b=0x04 got_b=0x04]
```

Exact match for the audit's `got_a=0x04 got_b=0x04` prediction.

Restored fix → `Total: 235 Passed: 235 Failed: 0 Skipped: 0`.

## Full Release-mode test suite

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON      # OK
cmake --build build -j$(nproc)                                   # OK
ctest --test-dir build --output-on-failure                       # 38/38 PASS
./build/test/fuse_z80_test build/test/fuse                       # 1356/1356 PASS
./build/test/nextreg_integration_test                            # 235/235 PASS
   (TestCov-NMI-MF-Port: 45/45)
```

Zero failures, zero regressions.

## Hunt for missed cases

I performed a systematic sweep for other NR fields where a runtime
gating signal forces a priority-1 unconditional clear in a clocked
process — the V13-NMP-01 pattern shape — across all
`process (i_CLK_28)` blocks in `zxnext.vhd` (~2236 lines extracted).

Pattern matched: `if <gate> = '1' then <field> <= '0'; elsif nr_*_we
= '1' then ...`.

**Result**: V13-NMP-01 (NR 0x05 bit 2, Pentagon gate) is the **only**
instance in `zxnext.vhd`. All other priority-1 IFs in clocked
processes that precede `nr_*_we` elsifs are `if reset = '1' then`
(proper reset semantics, not runtime gating). I checked all 31
`elsif nr_*_we` patterns and verified none have a runtime-gate
priority-1 clear before them.

I also surveyed `nr_03_machine_timing(2)` and
`machine_timing_pentagon` references throughout `zxnext.vhd`:
- Line 4448: `i_timing_pentagon` to ULA component (combinational, no
  cache).
- Line 4481: contention-disable formula (combinational, no cache).
- Line 5769: `machine_timing_pentagon <= '1'` decode (combinational).
- Line 5835: V13-NMP-01 (the one fix site).

No other Pentagon-gated NR-bit cache leak exists.

I also re-checked the audit's defensive-zero claims for the broader
NR readback-mask family (NR 0x06 ps2_mode, NR 0x0A bits 7:5, NR 0x09,
NR 0x10 coreid, NR 0x11, etc.) — those are config_mode-gated, not
machine_timing-gated, and Pass-11 (V11-NMP-02/03) covers them. Not
in scope of this finding.

**Conclusion**: V13-NMP-01 closes the runtime-priority-1-clear cache
leak family completely. No other instances exist in zxnext.vhd.

## Reviewer NITs (catalogued, not blocking)

### NIT-V13-NMP-01-A — incorrect "bypass" comment in NR 0x03 fix

`src/core/emulator.cpp:1992-1995` says:

```
// Direct write into the cache — bypassing the NR 0x05
// write_handler so we don't fan out to Joystick (whose
// joy0/joy1 fields aren't affected by bit 2; this is a
// pure cache-canonicalisation step).
nextreg_.write(0x05, cached_05);
```

This is **incorrect**: `NextReg::write()` (`src/port/nextreg.cpp:417-
456`) explicitly routes through `write_handlers_[reg]` if registered
(line 451-452). So `joystick_.set_nr_05(cached_05)` IS called, and
the write_handler's Pentagon-mode bit-2 mask IS applied (line 1112-
1116). Functionally still correct — joystick decode doesn't depend
on bit 2 (`src/input/joystick.cpp:67-74`), and the masked-byte
double-application is idempotent. But the comment misrepresents the
control flow.

**Fix**: rewrite the comment to say "Routes through the NR 0x05
write_handler — the Pentagon-mode bit-2 mask there will canonicalise
the byte; `Joystick::set_nr_05` is also re-invoked but bit 2 doesn't
affect the joystick mode decode".

Not blocking — code is correct.

### NIT-V13-NMP-01-B — fix comment claim "F3 callback at line 3355-3361"

`src/core/emulator.cpp:1145-1146` (in the read_handler comment block)
says "The F3 callback at line 3355-3361 already gates the toggle on
Pentagon" — but the F3 callback is actually at lines **3508-3514**
(the line numbers have shifted as code has been added). Stale
line-number reference. Not blocking.

### NIT-V13-NMP-01-C — audit report's "post-fix lines ~1976-1992"

The audit's report (NEXTZXOS-BOOT-SUBSYSTEM-VERIFY13-NMI-MF-PORT.md
line 122) says "post-fix lines ~1976-1992" for the NR 0x03 edge
clear, but the actual post-fix lines are 1974-1997. Off-by-a-few
guideline reference. Not blocking.

## Verdict justification

V13-NMP-01 is a real class-(b) bug (observable cache divergence,
latent because no current jnext-tested path exercises Pentagon mode,
but reachable from any guest that touches NR 0x03 timing around an
NR 0x05 write). The fix is shape-correct (mirror V11-NMP-02/03),
behaviour-correct (verified manually for both sequences), test-
discriminating (revert produces exactly the predicted
`got_a=0x04 got_b=0x04`), and clean across the full Release-mode
test suite. The defensive-zero hunt for other Pentagon-gated cache
leaks confirms V13-NMP-01 is the sole instance in zxnext.vhd.

The three documentation NITs are minor — incorrect "bypass" claim,
stale line numbers — and do not affect correctness. **APPROVE-WITH-
NITS**.

## Files reviewed

- `cores/zxnext/src/zxnext.vhd:5832-5841` (VHDL oracle)
- `src/core/emulator.cpp:1108-1170` (NR 0x05 write/read handlers)
- `src/core/emulator.cpp:1972-1997` (NR 0x03 edge fix)
- `src/core/emulator.cpp:3499-3514` (F2/F3 hotkey paths)
- `src/port/nextreg.cpp:417-456` (NextReg::write routing)
- `src/input/joystick.cpp:55-91` (set_nr_05 decode — bit 2 not used)
- `test/nextreg/nextreg_integration_test.cpp:4778-4848` (V13-NMP-01)
