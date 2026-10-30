# GH #420 PAUSE NOTES (implementer, 2026-10-30)

Branch gh420-source-search-path, base epic23-source-debugging 10c7d8d08; epic f45f111b6 already merged in.

## Done (all committed)
- src/debug/source_search.{h,cpp} (resolve, root_for, split/join_list); config_keys PathList + paths.source_dirs + append_list_entry; --source-dir (cli_options.h, main.cpp); AppConfig source_dirs (one string); MainWindow source_dirs_/push_source_dirs/save on Yes; DebuggerManager set_source_dirs/source_dirs/on_source_dir_located + signals source_dir_save_requested, source_dirs_changed (MainWindow logs "Source search path: ..."; src/debugger must not include core/log.h); SourcePanel Locate File..., .m4 message, past-end message, tooltip; Preferences Paths row (sourceDirsEdit, sourceDirsAddButton).
- Rows: SRCPATH-01..13, SETK-17..19, AC-100 (+AC-97 nine keys), QSRC-66..74, MWSRC-01..04, cli-set-func Q6/S2/U1. Pins: source_map 105, config_keys 24, app_config 175, source_panel 120, debugger_menu 58 (epic delta for these = 0; only cli_options_test 58 came from the epic). # expect: 173 unchanged.
- Docs: man page, USAGE.md, user guide (+panel page, preferences), dev guide, design doc 8.2/13, FEATURES; matrix and SUBSYSTEM-TESTS-STATUS regenerated. docs-check green.

## Verified (post epic-merge unless noted)
- make unit-test: Total 14469/14469, 0 fail 0 skip (load ~11-17).
- make unit-test-sdl: 13012/13012. make unit-test-win: 12873/12873.
- docs-check OK. Rows cli-set-func, z88dk-source-map-func, source-map-func PASS (before epic merge; rows re-run NOT done after merge).
- Mutations (all restored via cp, tree clean): 1->SRCPATH-01,02,03; 2->06; 3->07,08,11,12 (QSRC-66 does NOT kill it, plan claim wrong); 4->06,07,08,09,11,12; 5a->07; 5b->09; 6->10; 7->08; 8->12; 9->SRCPATH-12 and QSRC-70; 10a->QSRC-68; 10b->QSRC-68,69; 11->QSRC-68; 12->QSRC-74; 13->QSRC-73; 14a/14b->QSRC-71; 15a/15b->QSRC-72; 16a(MainWindow saves cfg)->MWSRC-02,03,04; 16b(main.cpp saves merged)->cli-set-func Q1,Q6; 17a/17b->MWSRC-01..04; 18a/18b->MWSRC-03; 19a/19b->SETK-18; 20->cli-set-func Q6; 21->SETK-17; 22->AC-100,AC-95 etc. Mutation 8 (parent_path) also survives QSRC-68 (basename-only case), killed by SRCPATH-12.

## Left
1. make harness-selftest (was running, killed by pause; cli-set-func.sh was edited, HS-89 count should be unchanged). Log: ~/tmp/autorun/jnext/2026-10-30/gh420-hs.log (incomplete).
2. Re-run: LANG=C make regression-rows ROWS="cli-set-func z88dk-source-map-func source-map-func" (builds gui-release-non-pgo + sdl-release; expect 0 FAIL 0 SKIP; a timing fail under load -> re-run solo).
3. If the epic moved again: git merge epic23-source-debugging, recount pins, rerun make unit-test (regenerates matrix; then bash test/refresh-subsystem-status.sh build/test-summary.tsv test/SUBSYSTEM-TESTS-STATUS.md and commit).
4. Write ~/tmp/autorun/jnext/2026-10-30/gh420-report.md (full report), hand back.
- Deviations: Locate/panel wiring done in DebuggerManager::ensure_window (not DebuggerWindow); logging via manager signal -> MainWindow; added SourcePanel::location_tooltip(); Locate's own refresh() removed (the manager push-back refreshes); split_list drops empty entries.

## Traps
- Never `pkill -f` with a pattern in your own command line (kills the shell).
- Do not edit sources while a make/regression-rows build is running (mutation scripts do cp-backup restore: ~/tmp/autorun/jnext/2026-10-30/gh420-mut/mut.py).
- build/ is the Qt tree; build dirs build/sdl-unit-test, build/win-sdl-unit-test, build/gui-release-non-pgo exist. Test file picks in panel tests need real files (put_file creates dirs) or the modal hangs.
