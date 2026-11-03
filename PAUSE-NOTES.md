# PAUSE-NOTES gh443 (host needed by owner)

Done (commit 1a66680df): regression.sh unknown-name + empty-name refusal + usage comment;
harness-selftest.sh HS-157a..h, run_preflight passes trailing args, EXPECTED_TOTAL 300 -> 308.
In the WIP commit: docs edits (REGRESSION-TEST-SUITE.md, dev guide 03 + 06 incl. R2 "141" dropped).

Not done: `LANG=C make docs-devguide` re-render (doc/developer-guide not regenerated); the build
`LANG=C make unit-test-build gui-release-non-pgo sdl-release` was killed at ~78% (restart; ccache helps);
then selftest (expect 308/308), regression-rows, the two bogus-name runs, make unit-test, mutations M1-M12.
worktree-bootstrap needed `git submodule update --init --recursive` (done).

Mutants run: none. Gates finished: only `bash -n` on both scripts (ok). No totals produced, none voided.
Host load was 19-33 during the build (threshold 12); start gates only when below 12.
Report gh443-report.md NOT yet written. Delete this file before the final commit.
