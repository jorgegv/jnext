# GH #516 Stage A: PAUSE NOTES (2026-11-27 ~21:10)

Branch gh516-remote-stepping, base epic486-remote-target @ feb7a6812. Stage A code is complete and committed;
paused by the orchestrator for #585 (cheaper mutant runner). Stage B not started (waits for #515).

## Done
- step_successors() (src/debug/step_successors.{h,cpp}), suite step_successors_test SSC-01..21 (21/21 green).
- Findings: CTL-04-05/06 (local Step Over via step_successors; RST $08 stops at pc+2), DZRP-CONT-06 (5-byte
  CMD_CONTINUE), SLNMI-RETI-01 + SSC-17 (RETI no longer takes the stackless address; z80_ed.c).
  Each new row was seen FAILING on the base (commit 8a9172e43 holds rows only; remote log
  gh516-remote-stepping-1806-574775.log: CTL-04-05, DZRP-CONT-06, SLNMI-RETI-01 FAIL; CTL-04-06 passes on base, as expected).
- step_out_returned() / step_out_sp_past() in debug_state.h (members delegate); DzrpClient::wait_pause() + DZC-WP-01..03.
- Pins: expect 261->262, debugger_backend 1607, dzrp_adapter 169, dzrp_client 73, cpu_z80n_im2_regressions 66.
- Docs: user guide Step Over note, dev guide 09-4 (+ cite in 07-frontends.md), re-rendered; matrix regenerated; suite tombstoned in
  test/refresh-traceability-matrix.pl. (Guides/matrix were rendered locally with `nice make docs-...`, NOT via slot.sh: the local lane was starved.)
- `make test-affected BASE=feb7a6812` (remote, tip dd9a1c3b1): all 177 selected suites green; only failure was the devguide cite
  check, fixed in 8cfc3a787 (not re-run since). Platform runs it names: unit-test-sdl, unit-test-win, unit-test-wasm: NOT RUN yet.
  Full `make unit-test` NOT RUN yet.

## Mutants (via slot.sh --patch, at pushed tips; script ~/tmp/gh516/mut/mutants.py, driver run.sh/poll.sh/verdict.sh)
Because the tip carried no failing check besides docs, read CAUGHT = a FAIL row.
- CAUGHT: M2 (cond fall only: SSC-01,05,06,09,10,18,20), M3 (chain cap like the disassembler: SSC-09, SSC-20),
  M5 (IX/IY swap: SSC-05,06,09,18,20), M6 (stack word at SP+1 for RETI: SSC-03,09,17,18,20).
- KILLED MID-RUN, NO VERDICT (rerun): M1, M8, M10, M11 (worktrees left at ~/tmp/worktrees/jnext/gh516-M1/-M8/-M10/-M11).
- PENDING: M12 (JP (C) guess), M13a (stackless ignored), M13b (RETI uses shadow), M14 (RETI fix reverted; mutants.py restores shared case),
  M15 (server size check removed), M18 (step_out >=), M27 (wait_pause timeout tears session down), M28 (step_over first target only),
  M29 (no dedupe), M30 (chain bound 0x1FFFF), M31 (no plain-ED rule behind prefix), M32 (Over ignores call-like).
- Note: sdcard_provisioner_test PROV-STALL-08 failed once in M3 under load (timing row, unrelated to this branch).

## Notes for resume
- Merge main (#585 landed) into the epic, then the epic into this branch; re-pin counts from the files.
- #584 (cap of 64 DD/FD hops per execute()): SSC-09's chains of 65, 66, 70 and 200 prefixes (g_chains() n list) and the DD/FD ED sweeps assume
  one chain = one execute(); SSC-20's "65535 prefixes then NOP" row only checks the function (no core run). Adapt SSC-09 when #584 lands.
- Findings for the report: behind a DD/FD prefix the core treats every ED opcode as the standard one (Z80N interception tests the first byte
  only); step_successors follows the core (plain_ed). Over's call-like test is first-byte only (DD CD enters the call), same as local Step Over.
