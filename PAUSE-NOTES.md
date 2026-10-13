# GH #356 PAUSE NOTES (2026-10-13)

Base 7acc6feac (v1.1.53), branch gh356-rslv13-counter. Plan: ~/tmp/autorun/2026-10-10/gh356-plan.md.

## Done
- Fix per plan, test-only: RSLV-08a, ASYNC-09a, ASYNC-11a (+ASYNC-11 gate.store(true), entered0/completed0 captures, skip-list IDs, invariant comment) in src/esp01/test/esp_socket_test.cpp.
- test/unit-tests.conf esp_socket_test 241 -> 244 with reason comment; ESP-WIFI-CONFIG-TEST-PLAN.md line fixed; TRACEABILITY-MATRIX regenerated (own commit).
- No production file changed. No ChangeLog (per plan).

## Mutations (all done; outputs in ~/tmp/gh356-mut/*.out; files restored, grep DIAG356 = 0)
- M1 (start_detached empty): before 23 FAIL (ASYNC-09 not among them... 241 rows); after 26 FAIL incl RSLV-08a, ASYNC-09a, ASYNC-11a.
- M2 one.test 20/300ms: before RSLV-13 FAIL entered=201; after 20/300/1500 all 244/244.
- M3 never-answers 400ms: before RSLV-13 FAIL; after 400/1500 pass.
- M4 bare check + 20ms: RSLV-08a and RSLV-13 FAIL.
- M5 post-gate delays: before 241/241 pass (vacuous); after ASYNC-08 FAIL (243/244).
- M6 delete gate.store in ASYNC-11a: ASYNC-11a FAIL (243/244).
- M7 one.test 2500ms: RSLV-08a FAIL by name (+RSLV-13).
- Loaded loop: 100 runs, 24 busy loops, load 7->24, 0 FAIL (non-discriminating).

## Gates (all on the tree before the WIP commit, base content committed eb4ca156b4d0)
- make clean && make gui-release OK (~/tmp/gh356-gui.log)
- unit-test 12869/12869 (~/tmp/gh356-ut.log); unit-test-sdl 11864/11864 (gh356-sdl.log); unit-test-win 11770/11770 (gh356-win.log); FUSE 1356/1356
- make regression 230 pass 0 fail 0 skip, stamped (~/tmp/gh356-reg.log), load 0.83 at end.

## Left
1. make regression-win (not run). Log to ~/tmp/gh356-regwin.log.
2. Final report fields: draft close comment, acceptance ticks (diff file list check: only the 4 files).
3. Hand back for independent review.
