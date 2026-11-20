# GH #498 paused at Stage 0 (parked until the owner answers Q1)

Base 288f97668 (v1.3.27). Prototype: PAUSE-PROTOTYPE.diff (attach_backend() no longer requests call-stack tracking; live raster kept). `make unit-test`: 16980/16996, 5 suites FAIL.

## FAIL list: 16 rows (plan expected 4)
- Expected, present (4): PBPUI-10, PBPUI-11 (debugger_persistent_bp_test); INSPW-16 (debugger_inspect_watchpoint_test); DHB-01 (debugger_halt_batch_test).
- Not in the plan, debugger_verbs_test (5): QPE-02, QEN-03, QMAP-03, QMAP-08, QANN-05. They read the Call Stack panel or tracked frames while the tab is hidden.
- Not in the plan, debugger_source_panel_test (7): QSRC-06, QSRC-49, QSRC-57 (Step Over lands in the callee) and QSRC-08, QCOV-06, QSRC-200, QSRC-219 (probable follow-ons).

## Hypothesis (UNVERIFIED)
The three QSRC Step Over rows and their follow-ons are fixed by D2's source_step_in_flight_ alone; this needs stage 3 built to confirm. The five debugger_verbs_test rows are not fixed by it.

## Devguide cites
Any edit to src/debugger/debugger_manager.cpp that moves lines breaks docs-devguide-cites-check: 24 cites in 4 docs (10-2-the-qt-debugger.md, 04-6-user-annotations.md, 04-7-skoolkit-and-ghidra-exports.md, 07-frontends.md) need re-anchoring in the same commit.

## Resume
- Owner says YES to Q1 (now 16 rows): resume at stage 1 (test/core/call_stack_reconstruct_test.cpp is the draft CSR-01..11, not registered in CMake or the manifest).
- Owner says NO: the fallback plan, keep the tracker on while the window is open and make it cheap (skip the hooks when SP did not move, opcode not E3/E9/DD/FD, no frame awaiting its target fetch); needs its own plan, saving unmeasured.

Report: ~/tmp/autorun/jnext/2026-11-17/gh498-report.md
