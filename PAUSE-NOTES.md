# GH #517 Stage A: PAUSED 2026-11-28 08:20 (coordinator order)

Branch gh517-remote-connect-ui, tip = the commit that adds this file on top of 89223cfc0.
The merge of epic486-remote-target (6e4b0227d) was FINISHED cleanly (not aborted): commit 89223cfc0.
All queued runs were killed; MU4's old test-affected run (started, > 10 min) was killed, so MU4 has NO verdict.

## Done
- Stage A code: valid_link_host/format_link_uri, Type::Host, remote.host/port/connect_timeout_ms/response_timeout_ms,
  remote.program (orchestrator addition), AppConfigData load/save/merge, --remote-target + refusals, Preferences
  group (+ remote_host_ok modal), bug_report rules, man page, user guide pages.
- Rows: RLK-URI-03/04, SETK-25/26, AC-107/108/109, CLI-RT-01..05, PA-41, PA-42 (invalid-host modal on OK/Apply, driven
  by a timer closing QApplication::activeModalWidget(); item 4 of the last order is DONE, passes in the full run).
  Pins: remote_transport 87, config_keys 33, app_config 185, cli_options 53, cli_options_native 34,
  preferences_apply 108; SETK-08 n 5->8. After the merge: expect 262 (epic's), HS 514, functional 245/197/190 untouched by me.
- Renders: man/USAGE/appendix/user guide/devguide rendered and committed up to f608a311e (pre-merge).
  Traceability matrix regenerated (perl test/refresh-traceability-matrix.pl --no-fails) after the merge.
- Gates passed: full `make unit-test` (remote, tip f608a311e): CLEAN, 261 suites, 18728 rows, docs-check and cites green.
  test-affected anchor CLEAN. Devguide cites confirmed by the lint (0 failed).
- Mutants (KILLED, by row): MU1 RLK-URI-03 (after strengthening it with the bracket reason: the first run SURVIVED),
  MU2 SETK-26, MU3 SETK-26, MU5 AC-107/AC-95, MU6 AC-94, MU7 AC-109, MU10 CLI-RT-03, MU11 CLI-RT-04, MU12 CLI-RT-02,
  MU27 BR-35, MU31 PA-41, MUP1 AC-107/AC-95, MUP2 AC-109, MUPA PA-42. No-op anchor (mutant-test) SURVIVED exit 0 = clean.
  MU8/MU9 need remote-qt-func (Stage B). MU4 has no verdict (killed): rerun it.

## Pending
1. After the MERGE with the epic, `doc/developer-guide/search/search_index.json` is the pre-merge copy and
   the generated docs may be stale: run `make docs-man docs-userguide docs-devguide` (local slot only: it starved for
   hours twice), commit.
2. Rerun MU4: slot.sh --patch make -C ~/tmp/worktrees/jnext/gh517-mut-MU4 mutant-test SUITES=app_config_test
   JNEXT_SUITE_VMEM_KB=3145728 (worktree has the diff, based on f608a311e: reset it to the new tip first:
   git -C <wt> diff > d; checkout -- .; checkout --detach <tip>; apply d).
3. One full `make unit-test` at the final tip (post-merge) and `make test-affected` + platform runs it names
   (unit-test-sdl, unit-test-win: not yet run at all).
4. Report: append "Round A1, gated" to gh517-report-stageA.md.

## Resume steps
- helper scripts: ~/tmp/gh517-mut/{mt.sh,run.sh,verdict.sh}; mutant worktrees ~/tmp/worktrees/jnext/gh517-mut-* (remove when done:
  git worktree remove --force). Never pkill -f with a pattern that matches your own shell.
