# GH #372 PAUSE NOTES (2026-11-19 ~09:10)

Branch gh372-bp-options, base main 2c2012341. Owner approved OQ-1 (S3 may proceed; BPJ-14 drops exactly its
three cases `when`, `on read 0x9000..0x90FF`, `mask 0xFF value 0xFE`, description updated, other 14 unchanged;
name it in the S3 commit message as the owner-approved baseline change).

## State: stage S1 written and partly verified, NOT yet gated, NOT yet split from S2 work (S2 not in tree)

Tree = S1 only: Interrupt/NMI types (codec, model, dialog), merged name table (`bp_kind_name`,
`bp_port_mask_for`), Run to Next Interrupt (keymap action 21, Debug menu item, `on_run_to_interrupt`),
S1 rows, tally pins (DK-03, SETK-05, AC-94, DACC-05, manifest 21->25 / 147->157 / 47->48), S1 docs
(user guide, man page, dev guide, FEATURES.md). Docs are NOT re-rendered yet (`make docs-man`,
`docs-userguide`, `docs-devguide`; then `make docs-check`).

Verified (build/ tree, LANG=C): bp_script_test 25/25, debugger_keymap_test 48/48, debugger_verbs_test 157/157
(QBPX-02 first used the ROM handler and failed; it now re-seeds the HALT loop instead).
Mutations killed: S1-M2 (BPJ-22/23/24), S1-M7 codec table edit (BPJ-25; QBPX-01 not run for it).
NOT run yet: S1-M1, M3, M4, M5, M6 (spec ready: ~/tmp/gh372-full/scratch/s1multi.json + mmulti.py, combined run on
keymap + verbs suites; the rows differ per mutation so one run attributes them).
Not run: accel_test (DACC-05), app_config_test (DK-03, AC-94), config_keys_test (SETK-05) after the pin edits.

## Exact next steps
1. Build + run accel/app_config/config_keys suites; run the S1-M1/M3/M4/M5/M6 mutations (restore from cp backup).
2. Re-render docs (`make docs-man docs-userguide docs-devguide`), `make docs-check`; regenerate matrix per `make unit-test`.
3. S1 gate via slot.sh: `make unit-test`, `make unit-test-sdl`, `make unit-test-win`, targeted rows
   `make regression-rows ROWS="script-pass-func script-stop-func script-load-error-func script-mempoint-func script-guard-func script-isr-func cli-set-func debugger-window-wm-func"`.
   STOP if any geometry row moves or a dialog driver using unnamed findChild<QLineEdit*> breaks (S1: verbs_test passes, source_panel_test not yet run).
4. Commit S1 (name the tally pins in the message).
5. S2: full S1+S2 copies of the six source files and bp_script_test are in ~/tmp/gh372-full (the static_assert there
   uses enum-compare; replace by the `same_kind(int,int)` form now in the tree). S2 verbs rows draft:
   ~/tmp/gh372-full/scratch/s2_verbs.inc (QBPX-06..17), docs patch: ~/tmp/gh372-full/scratch/s2_docs.py. Manifest S2: bp_script_test 25->29, verbs 157->169.
   Add `#include "script/script_engine.h"` to verbs_test; call test_bp_hits(), test_bp_hits_more() in main.
6. S2 gate and mutations S2-M1..M9, commit. Then S3 per plan (BPJ-14 edit as above).
Scratch files (copied to ~/tmp/gh372-full/scratch/): 
(mutate.sh, mmulti.py, s1multi.json, s2_verbs.inc, s2_docs.py). Mutation helpers restore from backups; the tree has no mutation applied now.
