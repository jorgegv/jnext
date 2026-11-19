# GH #544 pause notes (2) 2026-11-19

Done: main (288f97668, v1.3.27) merged; conflicts resolved line by line (functional expect 200 / macos 165 / windows 161,
app_config_test 179 = main 177 + AC-102/103, fast_boot 33, sdl_app_tick 17, preferences_apply 101, suites total 227);
guides, matrix, dashboard regenerated; dev guide cites re-anchored; docs-check green.
Verified after the merge: make unit-test 17009/0/0 (227 suites); harness-selftest-func PASS 453/453 (HS-89 scanned=12 holds);
fast-boot-func PASS.
Finding: fast-boot-hide-func FAILED (G and I: "a host Shift did not end the window") on the merged tree, twice, deterministically.
Cause (from the kept log): on the now quiet host `--fast-boot 60` (3000 frames) runs in ~4 s, so the window ends by time up before
the row's Shift can be sent (soft reset at 2 s, end at 3.8 s). Not a feature bug: the row's design was timing-dependent.
Fix in progress (uncommitted-then-committed WIP): fast-boot-hide-func.sh rewritten to grab the window in a loop while
the log lacks "fast boot: ended" (a grab counts only if the log still lacks it after the grab); G/I: >=1 grab and all black;
H/I2 (controls): >=1 non-black grab; post-end grab (0.7 s after "ended") non-black; J unchanged. No host key in this row any more
(key end is covered by fast-boot-func and FBT-32). The rewritten row's FIRST run was voided by the stop: UNVERIFIED.
Next steps, in order: (1) run `make regression-rows ROWS="fast-boot-hide-func"` through slot.sh, fix until green (the SDL
captures may be black regardless under X; I2 control tells); (2) unit-test-sdl, unit-test-wasm on the final tree;
(3) mutations M1..M16 from cp backups (targeted: fast_boot_test, sdl_app_tick_test, PA/AC, fast-boot-hide-func); the Qt
skip-update survivor is accepted; (4) `--no-sdcard` check: a NEX at start sets fl.program (main.cpp: load_file) so fast boot is
disarmed, code-read only, not run; (5) delete this file, commit, write "## Resumed" in the report.
