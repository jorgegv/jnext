# PAUSE notes (PR #543 merge of main v1.3.22)

Done: merge commit + regenerated man/guides + regenerated TRACEABILITY-MATRIX committed.
Not done: gates. `make unit-test` was queued behind the host load and never started on the final tree
(an earlier run on the merged tree built and passed docs-check; only the matrix was stale, now regenerated).
Next, each through $R/slot.sh env LANG=C make -C <wt> ...: unit-test, unit-test-sdl, unit-test-wasm,
wasm-browser-test, regression-rows ROWS="no-sdcard-func esxdos-nexdir-func harness-selftest-func".
Then re-check `make unit-test-dashboard` drift warning (test/SUBSYSTEM-TESTS-STATUS.md) and commit if it changed.
