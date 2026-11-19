# GH #544 pause notes (2026-11-19)

Done (commits on this branch, base 2c2012341): core fast_boot_hides_screen(), shown_frame() at both present sites,
startup.fast_boot_hide_screen (config_keys, --set arm, AppConfig, Preferences check box), rows FBT-30..36,
SDLT-15..17, PA-40, AC-102/103, fast-boot-hide-func (+ conf expect 198, HS-89 scanned=12), docs re-rendered,
dev guide cites re-anchored, traceability and dashboard regenerated, fast_boot_test dashboard label added.

Verified: unit suites fast_boot 33/33, sdl_app_tick 17/17, preferences_apply 101/101, app_config 178/178;
make unit-test 16924/0/0 (224 suites); unit-test-sdl 14977/0/0 (178 suites); unit-test-wasm 12426/0/0 (140 suites);
make docs-check green (cites 0 failed); make regression-rows: fast-boot-hide-func, fast-boot-func,
boot-nextzxos-welcome, boot-nextzxos-menu, sdl-keypress-func, qt-keypress-func, cli-set-func PASS;
harness-selftest-func was still running at pause (see report).

Not done: the plan's mutations M1..M16 (need rebuilds; run after the host is free, cp backups, targeted
suites/rows only, not while a regression-rows run compiles from the tree), the report body and ChangeLog edit
(draft only in the report), confirm harness-selftest-func result.
Next step: check ~/tmp/gh544-gates.log / gh544-rows2.log for harness-selftest-func, then run mutations.
Delete this file before the branch is handed to review.
