# GH #516 Stage A: PAUSE NOTES (2026-11-28, second pause)

Branch gh516-remote-stepping. The epic merge (epic486-remote-target @ 6e4b0227d) is DONE and committed cleanly
(cece694b5): conflicts only in test/unit-tests.conf and the 07-frontends.md cite, resolved by hand; `# expect:` now 263
(epic 262 + step_successors_test); my row counts kept (debugger_backend 1607, dzrp_adapter 169, dzrp_client 73,
cpu_z80n_im2_regressions 66); guides and matrix regenerated after the merge (rendered with `nice make docs-...` directly: the local lane
was held by other agents; the matrix with `perl test/refresh-traceability-matrix.pl --no-fails`). Functional expect lines
and HS EXPECTED_TOTAL come from the epic untouched (I changed neither). No run is queued or live; no mutant worktree is left.

## Stage A state
Code, rows, pins, docs: complete (see the earlier notes in the report). Not yet run on the merged tip:
`make test-affected BASE=<epic tip 6e4b0227d>`, `unit-test-sdl`, `unit-test-win`, `unit-test-wasm`, one full `make unit-test`.
(Before the merge, test-affected at dd9a1c3b1 was green except a devguide cite, fixed.)

## Mutants (all on the merged tip cece694b5 via `slot.sh --patch make -C <wt> mutant-test SUITES=...`; scripts in ~/tmp/gh516/mut: mutants.py, one.sh, batch.sh)
KILLED (rows): M1 (SSC-01,09,10,18), M8 (SSC-11,13), M10 (SSC-01,05,06,09,14,18), M11 (SSC-03,04,09,18,20,21),
M12 (SSC-04,09,15,18,20), M13a (SSC-16), M13b (SSC-17), M14 (SSC-17, SLNMI-RETI-01), M15 (DZRP-CONT-06),
M18 (STPOUT-P09, P11, W08), M27 (DZC-WP-02), M28 (CTL-04-05), M29 (SSC-19), M30 (SSC-20), M31 (SSC-09).
Earlier on the pre-merge tip, by FAIL row in test-affected runs: M2 (SSC-01,05,06,09,10,18,20), M3 (SSC-09, SSC-20), M5 (SSC-05,06,09,18,20), M6 (SSC-03,09,17,18,20).
NO VERDICT: M32 (Over ignores is_call_like: `if (false) {` made the build fail, probably an unused-function/variable error). Fix: mutate
the condition to `kind == StepKind::Over && false && ::is_call_like(pc, code)` and re-run with SUITES=step_successors_test; expected killers SSC-11/12/13.

## Resume steps
1. Merge the epic again if it moved (`git -C <wt> merge --no-ff epic486-remote-target`), recount pins from the files, regenerate matrix and guides.
2. Re-run M32 (above).
3. `slot.sh make -C <wt> test-affected BASE=<epic tip>`, then the platform runs it names (`unit-test-sdl`, `unit-test-win`, `unit-test-wasm`), then one `make unit-test`.
4. Remove this file from the branch, write the report (/home/jorgegv/tmp/autorun/jnext/2026-11-26/gh516-report-stageA.md; the earlier draft content is in the pause hand-back and in this file's history).
5. SSC-09 (chains of 65/66/70/200 prefixes and the DD/FD ED sweeps) assumes one chain = one execute(); adapt it if #584 lands first.
