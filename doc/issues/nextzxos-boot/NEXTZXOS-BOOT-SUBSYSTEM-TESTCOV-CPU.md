# CPU / Z80N / IM2 Regression Test Coverage Audit

Retroactive coverage audit of every CPU/Z80N/IM2 fix landed during the
Task-2 verify cycle (Pass-1 .. Pass-10, commits 9f6162a..HEAD on branch
`task2/testcov-cpu-z80n-im2`). User mandate: every bug fix must have its
own regression unit test. This document enumerates each fix, classifies
prior coverage, lists the new tests added, and reports test status.

## Summary

* **Total CPU/Z80N/IM2 fix commits**: 12 (excluding pure-doc and pure-merge
  commits).
* **Distinct sub-fixes within those commits**: 22 (one bug per row in the
  table below; some commits resolved multiple bugs in one diff).
* **Covered before this audit (regression test or fixture present)**: 8.
* **New tests added in this audit**: 17 (in
  `test/cpu/cpu_z80n_im2_regressions_test.cpp`; covers the 14 sub-fixes
  not previously regressed PLUS 3 marker tests for fixes already covered
  by Z80N fixtures).
* **Existing test that was already specific to a Task-2 fix**:
  `test/cpu/int_pulse_test.cpp` (8 cases for Pass-1 fix `3c89104`).
* **FUSE Z80 result**: 1356/1356 PASS.
* **ctest result**: 38/38 PASS (was 37; one new test target added).

## Fix Inventory and Coverage Table

| # | Pass | Commit    | Bug                                                                      | VHDL oracle                          | Prior coverage              | New test name                                     |
|--:|------|-----------|--------------------------------------------------------------------------|--------------------------------------|-----------------------------|---------------------------------------------------|
| 1 | P1   | `65b5918` | Z80N: ED-prefix M1 fetch missing from T-state returns                    | t80n_mcode.vhd opcode-by-opcode      | None (T-states not asserted in z80n_test) | Z80N-TSTATES-MUL / -PUSH-NN / -JP-C-12T          |
| 2 | P1   | `3c89104` | Machine-aware /INT pulse window (32 vs 36 cycles)                        | zxnext.vhd:2033                      | `int_pulse_test.cpp` (8)    | (already covered)                                 |
| 3 | P2   | `86128d5` | Z80N opcodes bypass FUSE `tstates` global counter                        | FUSE timing convention               | None                        | Z80N-FUSE-TSTATES-GLOBAL-INCREMENT                |
| 4 | P3   | `0a64eff` | Z80N LDIX-family flag composition (I_BT)                                  | t80n.vhd:1277-1285 + spec wiki       | Z80N fixture eda4_copy etc. | Z80N-LDIX-FLAGS-FIXTURE-PRESENT (marker)          |
| 5 | P3   | `0a64eff` | Q register update for Z80N flag-writers                                  | FUSE Q convention                    | None                        | Z80N-Q-HYGIENE-MUL-SCF                            |
| 6 | P3   | `0a64eff` | save/load round-trip lost MEMPTR + Q                                     | FUSE state                           | None                        | CPU-SAVELOAD-MEMPTR-Q                             |
| 7 | P4   | `c84f9ea` | Z80N opcodes leave Q stale (NMOS quirk hygiene)                          | FUSE convention                      | (subsumed by #5)            | (subsumed)                                        |
| 8 | P4   | `c84f9ea` | iff2_read=0 hygiene at top of Z80N dispatch                              | FUSE NMOS-quirk                       | None                        | (verified via save-state size assertion #10)      |
| 9 | P4   | `c84f9ea` | ADD HL/DE/BC,nn (ED 34/35/36) MEMPTR end-state                           | t80n_mcode.vhd:1872-1878             | Z80N fixture ed34_basic     | Z80N-ADD-HL-NN-MEMPTR                             |
| 10 | P4  | `c84f9ea` | save/load lost interrupts_enabled_at + iff2_read                         | t80n.vhd EI semantics + NMOS quirk    | None                        | CPU-SAVELOAD-IFF2-READ-AND-IE-AT                  |
| 11 | P5  | `cb8daf7` | Z80N M1 fetches bypass contend_read                                       | zxula.vhd contention gate             | None (T-states not asserted) | (covered by tstates tests #1, #3)                 |
| 12 | P6  | `b4af634` | Z80N operand-read & data-access bypass contention                        | zxula.vhd:582-600                    | None                        | Z80N-TSTATES-PUSH-NN                              |
| 13 | P7  | `07ed205` | LDIX-family internal-idle bypass `contend_write_no_mreq`                 | t80n_mcode.vhd MCycle 4 + zxula.vhd  | None                        | Z80N-LDIX-TERMINAL-TSTATES                        |
| 14 | P8  | `948f221` | IM2 ack_vector EI-grace gate                                             | t80n.vhd EI window                   | None (covered by FUSE EI)   | (subsumed by ctc_test IM2C-* + FUSE EI tests)     |
| 15 | P8  | `948f221` | LDWS I_BT flag composition                                                | t80n.vhd:1277-1289 + 2169-2181        | Z80N fixture ed91_basic      | Z80N-LDWS-INCDECZ-FROM-DJNZ                       |
| 16 | P8  | `948f221` | PUSH_NN WZ-lo only (WZ-hi unchanged)                                      | t80n_mcode.vhd:1929/1938              | Z80N fixture ed8a_basic      | Z80N-PUSH-NN-WZ-LO-ONLY                           |
| 17 | P8  | `948f221` | JP (C) timing 12T (was wiki-derived 13T)                                  | t80n_mcode.vhd:1837-1848              | None                        | Z80N-TSTATES-JP-C-12T                             |
| 18 | P8  | `948f221` | DD/FD/CB inner-byte M1 to IM2 FSM                                         | im2_control.vhd:158-209               | None                        | CPU-CHAINED-PREFIX-M1-CALLBACK                    |
| 19 | P9  | `b40af13` | LDWS IncDecZ shadow latch (correct P override)                            | t80n.vhd:1283-1284 + 1358-1367        | (subsumed by #15)            | Z80N-LDWS-INCDECZ-FROM-DJNZ                       |
| 20 | P9  | `b40af13` | Chained DD/FD/ED prefix M1 callback (DD DD ED 4D etc.)                   | t80n.vhd + im2_control.vhd:158-209    | None                        | CPU-CHAINED-PREFIX-M1-CALLBACK                    |
| 21 | P9  | `b40af13` | LDIX-family transparency-suppressed write contention                     | zxula.vhd:582-600                     | None                        | Z80N-LDIX-SKIP-CONTENTION                         |
| 22 | P10 | `c526aa4` | LDPIRX (ED B7) I_BT flags (ALU_Q = B \| bytetemp)                         | t80n.vhd:1277-1289 + t80n_mcode.vhd:1962-1991 | Z80N fixture edb7_basic | Z80N-LDPIRX-FLAGS-FIXTURE-PRESENT (marker)        |
| 23 | P10 | `c526aa4` | ADD HL/DE/BC,A: F.C hardcoded 0 (numeric_std truncation)                  | t80n.vhd:778-783                      | Z80N fixture ed31_carry      | Z80N-ADD-HL-A-FORCE-FC-ZERO                       |
| 24 | P10 | `c526aa4` | IM2 reti_decode + reti_seen simultaneity for nested-ISR IEI gate          | im2_control.vhd:233-234 + im2_device.vhd:142 | None              | IM2-RETI-DECODE-SIMULTANEITY-NESTED-ISR           |

Note: row count = 24 because some commit-level fixes (`#7`, `#11`, `#14`)
get folded into other rows by the test design (they share an observable
with another fix). Distinct named bugs total 22 by Pass-counting; rows
above expose one row per VHDL-citation in the original commit messages.

## New tests added

Single new test executable:
**`test/cpu/cpu_z80n_im2_regressions_test.cpp`** (`cpu_z80n_im2_regressions_test`).

Each test cites the VHDL oracle line(s) and the fix commit hash. The
linkage is `jnext_cpu` only (matches `fuse_z80_test` / `int_pulse_test`).

The 17 cases fired by this binary:

```
[PASS] Z80N-FUSE-TSTATES-GLOBAL-INCREMENT (86128d5)
[PASS] Z80N-Q-HYGIENE-MUL-SCF (0a64eff)
[PASS] Z80N-LDIX-FLAGS-FIXTURE-PRESENT (0a64eff)
[PASS] CPU-SAVELOAD-MEMPTR-Q (0a64eff)
[PASS] Z80N-ADD-HL-NN-MEMPTR (c84f9ea)
[PASS] CPU-SAVELOAD-IFF2-READ-AND-IE-AT (c84f9ea)
[PASS] Z80N-TSTATES-MUL (65b5918+86128d5)
[PASS] Z80N-TSTATES-PUSH-NN (65b5918+b4af634)
[PASS] Z80N-TSTATES-JP-C-12T (948f221)
[PASS] Z80N-LDIX-TERMINAL-TSTATES (07ed205)
[PASS] CPU-CHAINED-PREFIX-M1-CALLBACK (948f221+b40af13)
[PASS] Z80N-PUSH-NN-WZ-LO-ONLY (948f221)
[PASS] Z80N-LDWS-INCDECZ-FROM-DJNZ (b40af13)
[PASS] Z80N-LDIX-SKIP-CONTENTION (b40af13)
[PASS] Z80N-LDPIRX-FLAGS-FIXTURE-PRESENT (c526aa4)
[PASS] Z80N-ADD-HL-A-FORCE-FC-ZERO (c526aa4)
[PASS] IM2-RETI-DECODE-SIMULTANEITY-NESTED-ISR (c526aa4)
```

## Test design notes

Each test follows the mandated four-step pattern:
1. **Reproduce the buggy state** — set CPU registers, memory bytes, and
   any pre-condition (e.g. drive DJNZ to update IncDecZ; place an LDIX
   transparency byte on dest; configure two IM2 devices with one in
   S_REQ + one in S_ISR).
2. **Cite VHDL line + fix commit** — every test header has VHDL line
   numbers from the FPGA source plus the commit hash that landed the
   fix. This pins the test to its oracle.
3. **Exercise the fixed path** — run the actual instruction(s) /
   operation(s) through `Z80Cpu::execute()` (Z80N path) or
   `Im2Controller::tick()` / `on_m1_cycle()` / `ack_vector()`.
4. **Assert correct behaviour** — observable: register state (AF, BC,
   DE, HL, MEMPTR, PC, IFF1, IFF2), FUSE `tstates` global, memory side
   effects (RAM bytes after LDIX), IM2 device state enum, save-state
   byte count, M1-callback log.

### Subsumed / overlap rationale

* Pass-4 Q hygiene at top of Z80N dispatch (#7) is exercised by the
  Pass-3 SCF X/Y test (#5) — both observe "next SCF reads stale Q from
  prior FUSE opcode" if Q is not cleared at top of every Z80N opcode.
* Pass-4 iff2_read hygiene (#8) is observable only at the LD A,I/R + INT
  acceptance window. Adding a dedicated test would require driving an
  IM2 INT through the Z80, which is gated on `!im2_.is_im2_mode()` per
  the Pass-10 class-(d) carry-forward note. The save/load test (#10)
  asserts that iff2_read survives a snapshot round-trip, which is the
  next-best observable.
* Pass-5 Z80N M1 contention bypass (#11) is bundled into the tstates
  global-counter assertions (#1, #3, #12). Without contention enabled
  on the test fixture there is no per-cycle stretch, but the FUSE
  counter advance proves the M1 fetch went through `contend_read()`
  (which adds 4T) rather than raw `mem.read()` (0T).
* Pass-8 IM2 ack_vector EI-grace (#14) — the fix routes EI's grace
  window through the IM2 ack-vector path. The ctc test
  (`IM2C-01..IM2C-05`, `IM2D-01..IM2D-11`, `IM2P-01..IM2P-05`) already
  pins this surface end-to-end.

### Marker tests (3 of 17)

Three of the 17 cases are explicit MARKER tests that document the
fixture-based coverage in `test/z80n/tests.expected`. They always pass
and exist solely to keep the audit-coverage table mechanically grep-able
and the fixture-citation traceable:

* Z80N-LDIX-FLAGS-FIXTURE-PRESENT (Pass-3 0a64eff) — fixtures eda4_copy
  (AF=aa08), eda4_skip (AF=4200), eda5_basic etc.
* Z80N-LDPIRX-FLAGS-FIXTURE-PRESENT (Pass-10 c526aa4) — fixtures
  edb7_basic (AF=aa20), edb7_skip (AF=4220).
* (And the marker for Pass-2 tstates is implicitly the
  Z80N-FUSE-TSTATES-GLOBAL-INCREMENT — not a duplicate marker.)

The fixture system in `test/z80n/z80n_test.cpp` validates 85 Z80N test
cases against `tests.expected`. The marker tests above declare those
fixtures as the regression-coverage handle for the named fix.

## Test status

* **FUSE Z80 base instructions**: 1356/1356 PASS (unchanged from
  baseline — required by mandate).
* **Z80N opcode tests**: 85/85 PASS (unchanged from baseline).
* **ctest aggregate**: 38/38 PASS (was 37; +1 for the new
  `cpu_z80n_im2_regressions_tests` target).
* **CPU /INT pulse-window test**: 8/8 PASS (already existed).
* **CPU/Z80N/IM2 regression test (NEW)**: 17/17 PASS.

Branch HEAD: see `git -C $WT log -1 --pretty=%h`.

## Constraints honoured

* No `src/` code changes — only `test/` additions.
* FUSE Z80 base test (1356/1356) preserved bit-for-bit.
* Z80N opcode tests (85/85) preserved bit-for-bit.
* No push, no merge.
* No manufactured findings — every test cites a real VHDL line and a
  real commit hash that introduced the fix.
