# GH #360 landing 3: pause notes (2026-11-01)

Branch gh360-long-address. Nothing pushed. The tree is clean apart from this file.

## Done (committed: "the opening fit survives a WM that reverts the pre-map resize; window_size fixture waits for the fit")
- R1 fix, src/debugger/debugger_window.{h,cpp}: `resizeEvent()` has a new branch. When the fit is not awaiting or over, the window still has its default size, `fit_set_size_` is valid and the new size equals `fit_prev_size_` (the WM took the pre-map request back), it clears `fit_set_size_`, bumps `fit_reverts_` (bound `kMaxFitReverts` = 3) and queues a fresh pass. That pass runs on the managed window with its real decoration.
- R2 fix, test/debugger/window_size_test.cpp: the Fixture polls (100 ms, at most 5 s) until the size is unchanged twice, as window_grow_test's does. No expectation changed.

## Verified, with evidence (scratch: /home/jorgegv/tmp/impl-gh360-scratch/, harness sess.sh = fresh xvfb-run -d + openbox + QPA xcb, one pinned core + 8 busy loops)
- R1: debugger_window_grow_test with the fix: 0/30 fresh sessions failed. The mutant of the new branch (`false &&`): 6/40 stuck at 1170x900 (GR-02/03/05), the reviewer's exact symptom. Killed.
- R2: debugger_window_size_test (1366x768) with the fix: 0/30 under the same contention (reviewer's tip: 7/29).
- R3: `bash test/00regression/scripts/debugger-window-wm-func.sh` x10 in a row under pinned contention (8 loops on one core): 10/10 PASS (r3.log). No `quiet` tag added: no parallel-phase failure measured.
- OLDDECO still killed (row: DW-12, frame 771 tall).
- Q131-05 passes in the row runs (quit_cleanup_test, 8/8).

## Open / needs a decision
- NORESUME and NOPENDING now SURVIVE under openbox (0/15 sessions each, bin-NORESUME / bin-NOPENDING). Probe: a stderr print in the `size() == fit_prev_size_` branch of `grow_default_size_to_natural()` was reached 0 times in 20 sessions. The new revert branch handles what that wait path used to handle, so the wait path (`fit_awaiting_`, the pending branch, the resume in `resizeEvent`) looks dead with openbox. The reviewer demanded they die; they cannot, unless the dead path is deleted (my recommendation, and then the two mutants disappear) or the owner accepts them as surviving. NOUSER (Q131-05) was not re-run after the change beyond the row passes.
- Gates NOT yet run on the final commit: `make unit-test` (an attempt was interrupted by this pause; killed), `make unit-test-sdl`, `make regression-rows ROWS="debugger-window-wm-func"` (the row itself ran 10x green). Touched-suite builds are green (`make unit-test-build`).
- The dashboard (test/SUBSYSTEM-TESTS-STATUS.md) and matrix need no change expected (no row added), but regenerate if `make unit-test` reports them stale.

## Next step
1. Decide on the dead wait path (delete it with its fields, or keep and note the surviving mutants).
2. `LANG=C make unit-test-dashboard`, `make unit-test-sdl`, `make regression-rows ROWS="debugger-window-wm-func"` on a quiet host (load < 8; the 12-CPU host was at 20 from other agents, which times out rewind_test and others).
3. Commit any regenerated files and write gh360-report-landing3.md final.
