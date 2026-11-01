# GH #428 PAUSE NOTES (branch gh428-idle-cpu, base caa387105)

## Done (committed)
- Production: framebuffer generation (+ cold-boot carry), render_policy::upload_needed, Qt widget/CRT gate, SDL gate + redraw requests + `sdl present:` line, backend `state_revision()`, DebuggerManager paused stamp / force_refresh / qApp input-epoch filter, SourcePanel::file_changed_on_disk, `ticks:` `uploads= panel-refreshes=`.
- Decision (orchestrator, QSRC-44): the paused stamp includes the WHOLE CPU register file. QSRC-44 unchanged and green. Row QIDLE-09 pins it.
- Rows (all green when run): RP-UP-01..04, FBG-01..07, REV-*, PC-IDLE-01..05, QIDLE-01..09, QTH-02 rewritten (owner-approved 2026-11-01).
- Functional rows paused-idle-qt-func / paused-idle-sdl-func written (conf 172/151/147), NOT YET RUN.
- Manifest counts in test/unit-tests.conf updated; dev guide sources + render, qt-frontend.md updated.

## Verified
All debugger_* / frame_sequencer / host_* / window_* suites green; `make unit-test` ran everything and failed ONLY at traceability-check because the matrix was stale; it was regenerated (committed in the WIP commit). Re-run `make unit-test` to confirm green.

## Left
1. `make unit-test` again (expect green), `make unit-test-dashboard` (refresh test/SUBSYSTEM-TESTS-STATUS.md, commit).
2. `make unit-test-sdl`, `make unit-test-win`, `make docs-check`.
3. `make regression-rows ROWS="paused-idle-qt-func paused-idle-sdl-func frame-loop-wiring-func gdb-qt-func gdb-sdl-func dzrp-qt-func zrcp-qt-func screenshot-paused-func snapshot-paused-advance-func quick-snapshot-qt-func qt-host-order-func"` plus Qt/SDL screenshot rows; fix the new rows if they fail (the Qt row needs the debugger window to open on a GDB pause in offscreen).
4. Wine: `make regression-win ROWS=...` (check the Makefile accepts ROWS) for the two new rows.
5. Mutation table M1-M20 (+ register-term mutant killed by QIDLE-09); restore by cp backup only.
6. Before/after instructions:u: baseline binaries saved in /home/jorgegv/tmp/impl-gh428-scratch/base/{jnext-qt,jnext-sdl} (caa387105); scripts in /home/jorgegv/tmp/plan-gh428-scratch (exp-qt.sh, exp-sdl.sh). Build non-pgo Release + sdl-release of the branch first.
7. Final report: note deviations (cold-boot carry, non-const get_framebuffer, QIDLE-06 location, double Move/Resize filter call, register-stamp decision).

## Exact next step
`cd /home/jorgegv/tmp/worktrees/jnext/gh428 && LANG=C make unit-test` (background, log to ~/tmp/impl-gh428-scratch/).
