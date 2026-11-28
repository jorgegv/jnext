# GH #514 PAUSE NOTES (round B2 of Stage B)

Worktree /home/jorgegv/tmp/worktrees/jnext/gh514, branch gh514-qt-greying, tip = the commit that adds this file on top of
cb84f5611 (merge of epic486-remote-target 6e4b0227d = main v1.4.18). Target: the epic branch, never main.

## State: everything for B2 is done and gated except one platform run's final line

Done (all pushed):
- Stage A approved (A2). Stage B built (two managers: show_remote / show_local, RemoteSession, Role::Session), reviewed B1: REJECT R1-R5.
- B2 fixes, all in: R1 located dirs handed back + save relayed (RTQ-124), R2 check_breakpoint_hit row (RTQ-116), R3 six globals rows
  (RTQ-117..122), R4 take_*() counters add the session's (RTQ-123), R5 dev guide sentence fixed. Suite debugger_capabilities_test = 110.
- Merge of the epic (6e4b0227d, v1.4.18) DONE and committed (cb84f5611): `# expect: 263` kept (verified by the run: 263 suites,
  18904 rows), functional/HS pins came from the epic untouched, matrix totals 18904 / 12812 (hand-merged; the gate's traceability
  check printed no diff), search index re-rendered, `lint-devguide-cites` 0 failed. No half-done merge.
- Gates at cb84f5611 (slot.sh, remote): test-affected BASE=6e4b0227d CLEAN; unit-test CLEAN 18904/18904; unit-test-sdl CLEAN 16504;
  unit-test-win CLEAN 16301; unit-test-wasm CLEAN 14013.
- Mutants (each alone, line-preserving): B3 (`if (false)` on check_breakpoint_hit), G1-G6 (the six keymap/dirs/scheme copy and forward
  lines), R1a, R1b (hand-back and save relay), R4 (counter) - ALL KILLED (RTQ-116, 117, 119, 121, 118, 120, 122, 124, 124, 123).
  No-op anchor SURVIVED (as it must). Old-method runs for B3/G1/G2, mutant-test for G3-G6/R1a/R1b/R4/anchor. Mutant worktrees removed.

## Pending
- Nothing gated is pending. Only the hand-back remains: the "Stage B round B2" section of the report is written (see
  /home/jorgegv/tmp/autorun/jnext/2026-11-26/gh514-report-stageA.md); the reviewer re-reviews B2.

## B2 findings -> where fixed
R1 src/debugger/debugger_manager.cpp show_remote(): connect(source_dir_located -> on_source_dir_located), relay source_dir_save_requested.
R2 RTQ-116. R3 RTQ-117..122 (one row per edit). R4 take_*() in debugger_manager.h + RTQ-123. R5 10-2 guide paragraph.

## Resume steps
1. cd /home/jorgegv/tmp/worktrees/jnext/gh514; git status (clean); git log -1; check `git log -1 epic486-remote-target`; if it moved
   past 6e4b0227d: git merge --no-ff epic486-remote-target, recount `# expect:`, matrix, search index (docs-devguide), cites
   (CITE_BASE=<pre-merge> python3 ~/tmp/gh514-mut/fixcites.py), and re-run test-affected + unit-test + platform runs.
2. Hand back to the reviewer (all gates and mutants are recorded).
