# GH #434 PAUSE NOTES (stage 1 WIP, paused by coordinator: z88dk-latest being rebuilt)

## Step 0 (epic353 worktree)
Merge of main (8a3ee685d) into epic353-c-variables is COMMITTED: 5d610c353. Not yet done on epic353:
- test/SUBSYSTEM-TESTS-STATUS.md and doc/testing/TRACEABILITY-MATRIX.md hold the epic side (stale): regenerate from a real run
  (`make unit-test` then dashboard) and commit.
- make unit-test / unit-test-sdl / unit-test-win NOT run (build was killed at ~94%; no gate totals exist).
- Resolutions: unit-tests.conf expect 177 (174 + reexport + ay_cache + cdb_record), source_debug_test 413 (354+47+12),
  source_map_test 130, reexport_test 47; generate.sh MODS union; MANIFEST.sha256 rebuilt from files on disk (only generate.sh hash differs
  from each side); 4 dev-guide sources took the epic side then 15 cites re-anchored (xlate procedure, lint 0 failed, 321 cites);
  dev guide re-rendered (make docs-devguide) ; docs-check not yet run.
- generate.sh into scratch DEST (before the pause, against z88dk-latest 39421a01c): v2.4, v2.4-dotn, master identical to committed (diff -rq).
  That evidence predates the rebuild and the rebuilt z88dk may differ: re-run after the rebuild.

## Stage 1 (this branch, off 5d610c353) done in the tree, NOT compiled yet
- variable_store.{h,cpp}: CPiece/pieces, CScope.offset, CConv, CFrameHelpers, CNoRecordFacts, locate(), scope_at().
- z88dk_source_map.cpp: fill_extents, value-based fill_scopes (F-A), helpers, facts, conv from listings; z88dk_lis: z88dk_listing_functions().
- F-B: Client::call_stack, Impl::sync_call_stack(), set_call_stack_enabled(ClientId,bool), recheck wrapper (recheck_pending),
  want_call_stack removed, ZRCP/DebuggerManager adapted, existing test call sites adapted (signature only).
- debugger.h: CFrame + c_frames() DECLARED; implementation (src/debug/debugger_cframes.cpp) NOT written.
- fixtures: src/cross.c, generate.sh cross builds (v2.4 sccz80, master sccz80/sdcc/80cc), derive-scopes.py --extents.
  cross fixtures NOT generated (needs z88dk; the P3 probe in scratch p3/ showed padding forms work: #asm for sccz80/80cc, __asm for zsdcc, all via #ifdef __SDCC).
  Those probe outputs are void (taken while the rebuild may have started). MANIFEST/README not updated for cross.
## Next
Build gh434 (cmake -B build as unit-test-build does), write debugger_cframes.cpp, rows Z88CDB-41..46 and SRCDBG-CVAL-01..17,
adapt unit-tests.conf counts, run generate.sh after the z88dk rebuild, docs (7.10, 7.5, 7.9, 7.8, 3.9.1, index, user guide line), mutations M1-M16,
gates. Plan disagreements so far: helper named z88dk_listing_functions (returns all listed functions + fastcall flag); test call sites for
set_call_stack_enabled are 22 not 25; P4 confirmed (__code_compiler_tail present).
Test-file concern to check: CTL-12-55 (a direct emulator write must survive an in-place load) vs sync_call_stack() at the end of recheck().
