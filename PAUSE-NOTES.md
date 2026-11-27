# GH #513 paused after Stage A (2026-11-27)

Stage A (R0.1) is done and independently reviewed (round 2: APPROVE) on this branch:
`src/debug/target.h`, the five approved `debugger.h` declarations, the section 15 inventory and
the new section 11 rows of `doc/design/DEBUGGER-REMOTE-REAL-NEXT-PLAN.md`, lint + HS-57d, the
`debug_types_check.cpp` pins, CAP rows TGT-01..04.

Paused because Stage B (R1.1) waits for the owner's hardware checkpoint #512, by the owner's
decision recorded in the plan. This is a multi-stage issue: one branch, one merge into
`epic486-remote-target` when Stages B and C are done too. Nothing is merged yet.

Resume:
- Plan: `~/tmp/autorun/jnext/2026-11-26/gh513-plan.md` (Stages B and C), reports and reviews
  `gh513-report.md`, `gh513-review-1.md`, `gh513-review-2.md` in the same directory.
- Merge `epic486-remote-target` into this branch first (the epic now carries main up to v1.4.7).
- Run `make test-affected BASE=<epic merge-base SHA>` (a branch name does not resolve on a
  remote host).
- Stage B notes from the reviews: developer guide 3.9 still says "four published headers"
  (`09-debug-and-the-debugger.md:14`, `:107`, `:122`); a TGT-03 row must require a remote
  target to report `Remote` (`TargetInfo` defaults to `Local`); every `debugger.h` edit shifts
  developer-guide `debugger.h:N` cites (`lint-devguide-cites` catches them).
