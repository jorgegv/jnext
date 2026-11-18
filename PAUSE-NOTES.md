# PAUSE NOTES: GH #505 stage 1 (second SD card), parked for an owner decision

Branch gh505-second-sd, worktree /home/jorgegv/tmp/worktrees/jnext/gh505-second-sd. Base main 1d25fe238 (merged, includes #507). Not pushed.
Full stage-1 report: /home/jorgegv/tmp/autorun/jnext/2026-11-17/gh505-report-stage1.md. Plan: .../gh505-plan.md.

## Commits on top of 1d25fe238
7e7c018f4 core+CLI+warm start+rewind+unit rows; bd8e5aa31 sdcard2-func + man page; 624ff9d4f SDSLOT-13 leaves config mode;
477fe6a75 guides/FEATURES/plan doc; e62269b00 sdcard2-func writes+warm legs; 87122ee37 no pipe into grep -q.
Stage 1 implementation is complete; stages 2 (live change, GUI, Preferences key, --delayed-sdcard2-insert-frames) and 3 (.jns) not started.

## What is red and why
Deliberate change: Emulator::save_state appends the second card's FSM (+589 bytes + 4-byte sentinel = 593) and warm_start::kFormatVersion goes 4 -> 5.
Three existing rows pin that layout. Nothing else is red (unit suites touched, sdcard2-func and 6 other SD/warm rows pass).
Red in: make unit-test, unit-test-sdl (14813/14816), unit-test-win (14639/14642), regression row rewind-func (368/370).

Exact edits each pin needs (NOT made; editing existing tests is the owner's call):
1. test/rewind/rewind_test.cpp JNSX-S5B-LENGTHS (~line 4433-4436): Next 2154307 -> 2154900; 48K/128K/+3 2162499 -> 2163092; description text: add "GH #505 adds 593: the second SD card's SPI FSM (589 + 4-byte sentinel)".
   Derivation: current numbers + 593, where 593 = the S6 SD FSM figure in that row's own history (589 + 4 sentinel); the second card is a second SdCardDevice with the same fixed-width FSM. NOT yet confirmed against the measured value:
   on failure the row prints to stderr "JNSX-S5B-LENGTHS: next=N (want M) ..."; after editing, run ./build/test/rewind_test and check N equals 2154900 (and 48k 2163092). If it differs, the derivation is wrong; investigate before accepting.
2. rewind_test S5B-WARMSTART-VERSION (~line 4615): `kFormatVersion == 4` -> `== 5` (and the description's "is 4").
3. test/*/atic_atac_nmi_test.cpp ATIC-NMI-02 (~line 200): kSdCard lambda returns one FSM + sentinel; it must count two cards: `2 * (m.position() + sizeof(uint32_t))`.
4. Optional, for refusing --sdcard2 in the browser build: CLI-WEB-01 (test/core/cli_options_test.cpp) pins cli::browser_refusal at exactly 25 names; adding --sdcard2 etc. means editing it. Not done; man page makes no browser claim.

## Alternative: keep card 2 out of save_state
Then none of the three pins change (kFormatVersion could stay 4, no stream growth). It removes: plan item 3 (rewind carries card 2), SDSLOT-17, mutation M8, and the kFormatVersion bump rationale
(the warm-start header/key change JNEXTWS4 stays, it is independent). Cost: a rewind taken mid-transfer on card 2 restores a card not streaming; stage 3's .jns "sdcard2" member would also have no FSM. I recommend editing the pins instead.

## Pending after the decision
- Regenerate doc/testing/TRACEABILITY-MATRIX.md (make unit-test rewrites it; committed copy is the old one).
- Update test/SUBSYSTEM-TESTS-STATUS.md: DivMMC+SPI 155->156, DivMMC (int) 6->14, SD write journal 28->29, Warm start 116->119, grand total +13 (not edited).
- Re-run: make unit-test, unit-test-sdl, unit-test-win, regression-rows (sdcard2-func sdcard-swap-func sdcard-readonly-func sdcard-writes-func sdcard-file-add-func warm-start-func warm-start-chans-func rewind-func); then independent review of stage 1.

## Notes for the reviewer
- Warm-start header layout deviates from the plan: stored length stays at offset 88 (existing rows poke it); card 2 size/mtime at 96/104; header 112; magic JNEXTWS4.
- Card 2 warm key is (size, mtime) only; a --sdcard2-writes journal's content is not folded in (card 0 folds it).
- WSC-INV-10 still says "thirteen refusal branches"; the card 2 refusal is a fourteenth (pinned by SDSLOT-30).
- --sdcard2 with --sdcard-file-add is validated but ignored, not refused.
- M6 (journal bound to the wrong image in main.cpp) is killed only by sdcard2-func `writes`; SDSLOT-15/55 bind in test code.
- sdcard2-func `d` leg key timeline (CD "d:" from boot frame 737) is boot-timeline dependent like sdcard-swap-func.
- Mutation scripts: /home/jorgegv/tmp/gh505-mut (mut.py, run.sh, restore.sh, battery*.sh).
