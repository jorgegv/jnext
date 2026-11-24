# GH #558 S3: pause notes (2026-11-24, owner pause)

Branch `gh558-sdcard-file-manager`. Last code commit before this note: `2e31acc17`. Base: main v1.3.74 (merged).

## Step 1: the five carried gaps (all CLOSED; the kill of each is on a pushed mutant branch run, remote, unless noted)
| Gap | Row | Mutants and result |
|---|---|---|
| P02 merge-plan alias check | SDFA-W76 (replaced file sorts before the alias), W77 (new file sorts before), W78 (alias-named folder) | Q01 (check off): red on W76, W77, W78 (remote test-affected) |
| P19 video temp files and output held | SF-14 (video_recorder_cmd_native_test, ffmpeg_stub) | Q02b, Q02c, Q02d red on SF-14 (remote). Q02a (output hold removed): its push was rejected once; killed LOCALLY before the addendum (SF-14 red); not re-run remotely |
| PiQemu overlay and FIFO folder | PIT-67 (nextpi_test, nextpi_qemu_stub) | Q03a, Q03b, Q03c red on PIT-67 (remote) |
| H10 RZX released after stop | SDMT-34 | Q04 red on SDMT-34 (remote) |
| H14 output_files begin twice, end once | SDMT-35 | Q05 red on SDMT-35 (remote) |
Reviewer notes done: the dev guide names the esxDOS exception; the engine's alias message says "folder ... merge into that folder" or "file ... overwrite that file".

## Step 3: S3 itself (built, tested, NOT yet reviewed)
- `src/gui/sdcard_card_model.{h,cpp}`, `src/gui/sdcard_manager_dialog.{h,cpp}`: the window. Native widgets; both slots in the selector (empty one disabled, selector follows the machine); forced Read-only with the reason; Copy both ways, Rename, Delete (card permanent, host to the trash), New Folder in both panes; drops (Shift = move); warning before the first card change; held list read at each operation; "emulator wins" (resolve on open, reset to the root when the card changes while open, polled and checked before each operation).
- `sdmanager::slots()` was renamed `card_slots()` (Qt's `slots` macro broke the header in Qt files); `sdmanager::is_held()` added.
- Config keys `sdcard_manager.card/card_dir/host_dir` in config_keys, AppConfig, main.cpp (qt assignments), bug_report rule, man page (`--set` entries), user guide appendix (generated); dev guide paragraph.
- Rows: `sdcard_manager_window_test` SDMW-01..19 (pin 19, `# expect: 257`), `app_config_test` AC-104..106 (pin 182), `sdcard_manager_test` 52, `sdcard_file_add_test` 171, `video_recorder_cmd_native_test` 9, `nextpi_test` 103. Matrix tombstone added; matrix regenerated from the remote run's diff (commit 2e31acc17).
- Window mutants: remote runs finished and RED: W01 (SDMW-04), W02 (SDMW-06), W03 (SDMW-16), W04 (SDMW-12), W05 (SDMW-06, 09, 10, 11, 12, 13, 19), W06 (SDMW-05, 06, 13, 14, 15), W07 (SDMW-07), W08 (SDMW-16), W09 (SDMW-04). Killed LOCALLY before the addendum (not re-run remotely): W10 (AC-94, AC-105), W11 (SDMW-19), W13 (AC-106), W15 (SDMW-06), W05 first build error fixed. W17 (the `mounted !=` clause of the selector refresh) survives: it is redundant, a mounted flag change always changes the read-only reason, so the refresh fires anyway; documented, not killable.
- NOT RUN: gh558-mut-W10 was running LOCALLY when the pause came (slot.sh admitted it to a local slot) and was STOPPED by PID (nothing recorded); W11, W13, W15 queued and never started in the remote batch. Q02a never ran remotely (rejected push).

## Gates so far
- `unit-test-sdl` CLEAN (16014), `unit-test-wasm` CLEAN (13601) on `f201f7a57`.
- `unit-test-win` (wine) on `f201f7a57`: one failure, `PROV-STALL-08` (sdcard_provisioner_test, a 29.5-34 s wall-time window under load): unrelated to this change; needs a re-run when the hosts are quiet (counts once it passes solo or in a quiet run).
- `make test-affected` now plans a SUBSET (good). On `f201f7a57` it failed on: (a) developer guide stale (SDMW-19 range edited after the render), (b) a home path in `app_config_test` ("/home/u/..."), (c) the traceability matrix stale: (a), (b), (c) fixed in `2e31acc17` but NOT re-verified; (d) `lint-sed-inplace` failing on `test/test-affected-selftest.sh`: PRE-EXISTING ON MAIN (30444f6f3, #564's selftest uses bare `sed -i`; `bash test/lint-sed-inplace.sh` fails in the main checkout too). It will keep every test-affected run NOT CLEAN until main fixes it; not touched here.

## Next steps (in order)
1. When the owner lifts the pause: `make test-affected` (remote) on the branch tip; confirm only the main-side sed lint (d) remains; if main has fixed it, merge main first.
2. Re-run `unit-test-win` for PROV-STALL-08 on a quiet host.
3. Re-run remotely (gh558-mut-<id> branches, slot.sh, delete after) the mutants not run remotely: Q02a, W10, W11, W13, W15.
4. Hand S3 to an independent reviewer (the S3 review checks the five carried gaps first).
5. Then S4.

## Deferred to S4 (do not miss)
- Man page: `--sdcard-file-add` alias refusal in the refusal list and in EXIT STATUS 3 (the engine now refuses a destination that is another entry's 8.3 name, single file and folder merge); then `make docs-man`, `docs-userguide`.
- The toolbar icon (`sd-manager.svg`, the approved variant C3, file in the run dir), the File menu entry "SD Card File Manager..." (no mnemonic), `MainWindow::on_sd_manager()` (refuse while `card_op_` is set or no debugger; open the session, open the dialog application-modal with `open()`), closing via `QTimer::singleShot(0)` then `session.close()` and the status-bar message (ResetDeferred: restarts paused), saving the three remembered values on close (remember_* pattern, session overrides reset), `SessionPaths` gains the three optionals, `toolbar_icons_test` TBI-10 list and message, ICONSET-DESIGN.md section 3 row, FEATURES.md line, ChangeLog Unreleased line, user guide chapter `13-the-sd-card-file-manager.md` with its nav, TOC and the toolbar list, docshot capture, SDMW rows for File > SD Card File Manager and `card_op_`.
- The window is built so that the owner (MainWindow) declares the `SdManagerSession` AFTER the Debugger and calls `held_host_files()` itself only through the dialog.
