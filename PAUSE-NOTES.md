# GH #389 stage B: PAUSE NOTES (owner pause, host quiet)

Branch gh389-skoolkit-ghidra. Stage A approved earlier (tip then 47e65f104). Epic 61fbcd892 (#387) merged (3b7ae5d2c).
After this WIP commit nothing is running; I did not restart any gate.

## Done (committed before this WIP)
- Merge of epic 61fbcd892; conflicts resolved line by line (man page, cli_options.h, debugger.h rebuilt = CAP-ANN then CAP-REX,
  mkdocs, cli_options_test, unit-tests.conf, functional_tests.conf); generated files regenerated. Pins recounted:
  `# expect: 175`, cli_options_test now 62 (59 + CLI-ANN-01 + CLI-REX-01 + CLI-REX-02), reexport_test 40,
  source_map_test 129 (#387), functional `# expect: 183 / macos 159 / windows 155` (counted by script from the conf).
- Stage B code: `skoolkit::parse_ctl()` / `ghidra::parse_symbols()` (+ `reexport::Imported`), `Debugger::import_skoolkit/import_ghidra`
  (scratch copy, one `replace_annotations()`), comments in `export_skoolkit()`, `--import-skoolkit` / `--import-ghidra`
  (repeatable; cli_options.h, main.cpp, EmulatorConfig, ScriptHostOptions, `ScriptHost::start()` after --annotations, 3 owners), man page.
- Rows: reexport_test +16 (SKCTL-09..14, GHSYM-03..05, SRCDBG-REX-10..15, 13a) = 40/40 green; CLI-REX-02; skoolkit-foreign-func part B4
  (+ `skoolkit-check.py compare|comments`); B4 verified by hand end to end (55 labels + 2 comments equal) and the row PASSed on r6.
- Design 11.2 import paragraph; SKCTL-10 tightened (comments.size()==2) from mutant B22.
- Mutants run so far (unit level), all killed: B19,19b,19c (header/stem), B20 (untagged placement), B21a/b (0x, unknown tag), B22 (titles as
  comments), B23a/b (first/last wins), B24a/b (transactional), B25 (no replace), B26 (toolchain_symbols), B27 (no comments in export),
  B28 (':'), B29 ('*'), B30 (blank directive). Not finished: B31 (address > 65535 check removed), B32 (a bad file skipped).
  The driver /home/jorgegv/tmp/impl-gh389-scratch/mutdrv3.py was killed mid-run; skoolkit_ctl.cpp was restored from its .mutbak and
  verified identical to the previous commit (git diff clean).

## In this WIP commit (uncommitted before it)
- `04-7-skoolkit-and-ghidra-exports.md`: new "### Import" subsection with TOKENS `@@file|regex@@` where a `file:line` goes. NOT YET RESOLVED:
  the page does not render cleanly and `make docs-check` will fail until the tokens are replaced.

## Not done / next steps (in order)
1. Resolve the tokens: for each `@@src-file-basename|regex@@`, replace by the line number of the first match (cites are `symbol()` (`file:N`)).
   Then recompute ALL older jnext cites on the page: stage B added includes/code to skoolkit_ctl.cpp (+1 include line, reader appended at end),
   ghidra_symbols.cpp (+2 include lines), debugger_reinterop.cpp (+2 includes + import code at the end, comment loop in export_skoolkit),
   so the old line numbers (e.g. skoolkit_ctl.cpp:18/33/86/141/185/195/246-254/265/285/287, ghidra_symbols.cpp:27/31/37/52/62,
   debugger_reinterop.cpp:31/47/57/73/82/91/96/98/111/125/135/149/156/174/179/180/193/201/218) are stale. Method: take the line text at
   47e65f104 and find it in the new file. Also re-point the other chapter-7 cites (03-loaders, 05-breakpoints, 06-lifetime, 01-what-jnext-is)
   to the merged tree (they were taken from the epic's side) and run `/home/jorgegv/tmp/impl-gh389-scratch/check.py <epic61fbcd892> HEAD <md>`
   adapted, plus a script that resolves every cite into a changed file (emulator.cpp moved 17 lines past ~9759; debugger.h, main_window.cpp changed by #387).
   Also run the new #430 cite gate if it is on the epic by then (symbol right before the parenthesis or inside, on the cited line, one cite per line).
2. Finish mutants B31, B32 (`python3 -u /home/jorgegv/tmp/impl-gh389-scratch/mutdrv3.py B31 B32`), a functional one for the importer (e.g. remove the
   `import_all("--import-ghidra"...)` call in script_host.cpp -> CLI-REX-02) and one for B4 (e.g. untagged placement or `*` handling) via
   `fmut`-style `make regression-rows`. Record in the report.
3. Row IDs: check absence on epic23-source-debugging, gh430-docs-cites-gate (HS-102..148) and epic353-c-variables with `git grep`
   (SKCTL-09..14, GHSYM-03..05, SRCDBG-REX-10..15/13a, CLI-REX-02). Not done yet.
4. Re-render man/user guide/dev guide (`make docs-man docs-userguide docs-devguide`), commit; the user guide reference page was re-rendered once already.
5. Gates, when the host is quiet: `make unit-test` (regenerates matrix + dashboard: commit them), `make unit-test-sdl`, `make unit-test-win`
   (file IO), `make regression-rows ROWS="skoolkit-foreign-func annotations-func sld-labels-func cli-set-func"`; solo re-run of
   cli_options_test / tap_loader_test / rewind_test on a #440 timeout. `make docs-check`, `make cli-check`, `make harness-selftest`
   (HS count unchanged at 245).
6. Write /home/jorgegv/tmp/autorun/jnext/2026-10-30/gh389-report-b.md (commits, merge + recount, each plan item, disagreements with the plan,
   rows, mutants, gates with load) and return it.

## Gates on the current tip
- RUN and green (earlier tips of stage B): reexport_test 40/40; cli_options_test 62 with CLI-REX-02 PASS (one FAIL CLI-DOC-06 before the user-guide
  re-render, fixed by `make docs-man`/`docs-userguide`); `make regression-rows ROWS=skoolkit-foreign-func` PASS (with B4) at tip 00b410479.
- NOT run on the current tip: `make unit-test`, `unit-test-sdl`, `unit-test-win`, `docs-check`, `cli-check`, harness-selftest, the other
  regression rows (annotations-func, sld-labels-func, cli-set-func). The traceability matrix / dashboard are NOT regenerated for the new rows.

## Plan deviations so far (for the report)
- Plan said `annotations().entries()` gives a comment per key: true; `replace_annotations()` is public and does not apply the toolchain-name rule (matches).
- A header-less title `.` line in skool2ctl -k output counts as a title (`titles`), `.` after D as `continued`.
- `@ start` / `@ org` count as "other directives" so SKCTL-09's "nothing skipped" is "only those two".
- The Ghidra reader is strict on `#` lines and on `TAG` prefixes; blank lines are skipped (the script would raise).
- Plan 2.8 says imports before --load use the mapping at that moment: implemented and documented (man page).
