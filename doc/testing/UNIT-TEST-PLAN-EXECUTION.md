# Unit Test Plan Execution Process

This document describes how the VHDL-derived unit test plans in
`doc/design/*-TEST-PLAN-DESIGN.md` are authored, executed, maintained, and
evolved in lockstep with emulator fixes. Read it before touching any
test plan, any `test/<subsystem>/<subsystem>_test.cpp`, or any emulator fix
that flips tests from skip/fail to pass.

## 1. What a test plan is — and what it is not

Each `*-TEST-PLAN-DESIGN.md` is a systematic compliance specification for
one emulator subsystem, **derived exclusively from the VHDL sources** in
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`. Every
row carries a test ID, preconditions, stimulus, expected value, and a VHDL
`file:line` citation. Expected values come from VHDL — never from the
current C++ implementation. The tests are the specification of correct
behaviour; the emulator is the thing under test.

Consequence: a failing unit test does not mean the test is wrong. The
default assumption is that the emulator is wrong and the test is a correct
reading of VHDL. Only after an independent review comparing the assertion,
the VHDL citation, and the C++ implementation should a test ever be
altered. This rule is non-negotiable — it is what prevents coverage theatre
(see §8).

## 2. Three outcome classes: pass / fail / skip

A unit-test plan row can resolve into one of three observable states:

- **Pass** — the C++ implementation matches the VHDL-derived expected value
  under the plan's stimulus. Counts toward the live pass rate.
- **Fail** — the C++ implementation produces a value that disagrees with
  the VHDL citation. Counts toward the live fail count. Every fail must be
  traceable to a specific entry in the Emulator Bug backlog.
- **Skip** — the plan row cannot be exercised against the current C++
  surface. Does NOT count as pass or fail. Reported separately with a
  one-line reason.

The test harness provides a `skip(id, reason)` helper (canonical reference
implementation: `test/copper/copper_test.cpp`) that prints the row at the
end of the run without touching the pass/fail counters.

### When to skip vs. fail

Skip a row when the facility does not exist in `src/` at all:

- An entire subsystem is absent (example: the whole joystick pipeline in
  `test/input/input_test.cpp`).
- A register, port, or state-machine field is not declared anywhere in C++
  (example: `nr_64_copper_offset` in `src/video/copper*`).
- A signal is not publicly observable from the class under test — no
  accessor, no handler, no side-effect visible via the public API
  (example: `mirror_inc_i` pulse in `SpriteEngine`).
- A cycle-accurate bus or timing surface that the harness cannot drive
  without emulator work (example: simultaneous CPU+Copper writes on the
  shared `nr_wr_*` bus).

Fail a row (let the existing test run and fail) when the facility *exists*
in C++ but produces the wrong value:

- A register has a handler, but the handler stores the wrong bits
  (example: `NextReg::reset()` zeroes NR 0x05 instead of applying the VHDL
  default 0x40).
- A computation exists but disagrees with VHDL arithmetic (example:
  `Layer2::compute_ram_addr()` omits the `+1` bank transform from
  `layer2.vhd:172`).
- A code path is taken but returns the wrong answer (example: blend
  modes 110/111 falling back to SLU in `renderer.cpp:259`).

The distinction matters because skips are advertising "work has to be done
here" while fails are advertising "the code is lying about behaviour." Both
feed the Emulator Bug backlog but they point at different fix categories and
should not be conflated.

### What skip is NOT

- **Not a workaround for a test bug.** If the assertion itself is wrong,
  fix the assertion. Do not hide a test-authoring mistake behind a skip.
- **Not a way to inflate the pass rate.** A skip is explicit admission of
  non-coverage. Converting a fail to a skip without a corresponding real
  reason to un-count it is theatre.
- **Not a `check(x, false, "NOT_IMPL: ...")` placeholder.** That pattern
  was tried in the original Input rewrite and rejected by review: it
  pollutes the fail count, breaks the honest pass-rate signal, and can
  accidentally pin wrong values once the facility lands. Use `skip()`
  instead.

## 3. Authoring new test code from a plan (full rewrite)

When an existing `*_test.cpp` file is full-rewritten from a plan (the Task 5
Step 5 Phase 2 pattern), follow this recipe:

1. **Isolate in a git worktree.** Create a fresh worktree under
   `.claude/worktrees/` so the author can commit on a dedicated branch
   without risking main. Worktree isolation protects against absolute-path
   `cd` escapes (see `memory/feedback_agent_worktree_escape.md`).
2. **Read the plan in full before touching code.** The plan is the
   authoritative contract. If the plan is ambiguous or disagrees with
   VHDL, STOP and report — do not silently deviate. Plan nits go in the
   review report, not in `git diff` of the plan.
3. **Read VHDL for every row you touch.** The citation in the plan is a
   starting point, not a substitute for reading the surrounding VHDL
   process. Walk the signal through at least one full clock boundary.
4. **Never use the C++ implementation as an oracle.** Read `src/<subsystem>/*`
   only to discover the public API surface. If your assertion matches
   whatever the C++ happens to do today, you have written a tautology.
5. **Every `check()` cites VHDL file+line in the description.** Future
   reviewers and future you need to jump straight to the spec line that
   justifies each expected value.
6. **One C++ section per plan row (or tight group)**, with the plan's test
   ID in the section name. This is what makes the traceability matrix
   (Step 6) possible.
7. **Lint discipline.** `test/lint-assertions.sh` rejects tautologies:
   `check(x, true, ...)`, `|| true`, `a == b || a != b`. The baseline is
   checked into `test/lint-assertions.baseline`; `new: 0` is required on
   every commit. Do not put the forbidden substrings in comments either —
   the lint matches raw text.
8. **No Co-Authored-By trailers** in commits. House style.
9. **Do NOT run cmake or the test binary from within a sub-agent.**
   Sandbox restrictions block them; build, run, and commit happen from the
   main session (see `memory/feedback_subagent_sandbox.md`). The author
   sub-agent delivers source code only.
10. **Do NOT patch `src/` to make tests pass.** That belongs to the
    Emulator Bug backlog; see
    §5.

## 4. Independent review is mandatory

Every authored rewrite passes through an independent critic reviewer agent
before it can be merged to main. The reviewer is a different agent from
the author, ideally with the same domain expertise, and operates on the
same worktree branch (not a fresh one) so its fix commits chain onto the
original. CLAUDE.md codifies this rule for every feature and bugfix — test
rewrites are no exception.

The reviewer's job is triage, not rewriting:

1. **VHDL fidelity sampling.** Pick 10+ assertions at random across groups
   and verify the VHDL citation actually matches what VHDL says. Silent
   drift from the cited line is the most common author mistake.
2. **Failure classification.** For every failing assertion, classify:
   - **(A) Test harness bug** — helper function wrong, stimulus wrong,
     coordinate-space confusion. Example from Phase 2: the Sprites
     reviewer found the `fresh()` helper defaulted `over_border=false`,
     silently clipping ~59 rendering tests; fixing that one line turned
     67 failures into 1.
   - **(B) Test expected-value bug** — the author misread VHDL or did the
     arithmetic wrong. Example: Copper MUT-03 asserted an over-wide NR
     0x62 self-write when VHDL preserves the low byte.
   - **(C) Legitimate emulator bug** — C++ disagrees with VHDL. Leave
     failing, add to the Emulator Bug backlog.
   - **(D) Plan bug** — plan row itself disagrees with VHDL. Note in the
     report; do not edit the plan (that's a separate decision).
3. **Stub honesty check.** For every `skip()`, verify the facility is
   genuinely unreachable through the current API. Hidden work is common:
   "I didn't feel like plumbing this through" is not a valid skip reason.
4. **Coverage count.** Every plan row should map to exactly one test ID
   (or be justified as deferred to integration tier). Count the rows and
   compare against the plan total. Missing rows are the canonical theatre
   failure mode.
5. **Tautology / anti-test sweep by eye.** Lint catches the obvious
   patterns; the reviewer catches the subtle ones (e.g. `check(x, x, ...)`
   where x was just written by the setter under test).
6. **Fix (A) and (B) on the spot.** The reviewer commits follow-up fixes
   on the same branch with messages like `test(<subsystem>): review fix
   — <what>`. Rebuild and re-run to measure final pass/fail/skip numbers.
7. **Report.** Verdict (APPROVE / APPROVE-WITH-FIXES / REJECT), per-
   category summary, Emulator Bug backlog items found, final pass rate, commit
   SHAs of any fixes.

Only APPROVE or APPROVE-WITH-FIXES (with the fixes committed and the rerun
passing) are merge-eligible. REJECT bounces back to the author.

## 5. Running emulator fixes against existing skips and fails

This is the process that moves the honest pass rate up over time. It is a
separate task stream from test authoring (the Emulator Bug backlog) and
has strict rules.

### The 1:1:1 rule

One emulator fix branch handles **one coherent feature area** from the
Emulator Bug backlog (e.g. "implement NR 0x64 / cvc offset" or "apply `+1` bank
transform in `compute_ram_addr`"). On that branch:

1. Apply the emulator fix in `src/`.
2. In the same branch, un-skip (or update) every test row the fix
   unblocks. "Un-skip" means: delete the `skip(id, reason)` call, replace
   it with real `check(...)` assertions from the plan row, cite the VHDL
   line, and make sure it compiles.
3. Run the affected subsystem test binary. Any newly-live rows either
   pass (fix is correct) or fail (fix is incomplete or has a new bug).
   Iterate on the emulator code, not the test.
4. Run the full regression suite on real desktop (`bash test/regression.sh`
   — sandbox has no graphics substrate; see
   `memory/feedback_regression_refs.md`) before merging. No cross-
   subsystem regressions allowed.
5. Independent code review of both the C++ fix AND the newly-live tests,
   same rules as §4.
6. Merge to main. Update the plan's **Current status** block and the
   subsystem row in `EMULATOR-DESIGN-PLAN.md` § "Fix baseline of
   subsystem tests not passing" so the published numbers stay honest.

### Why un-skipping is deliberate, not automatic

An emulator fix does not magically flip skips to passes. Someone has to
re-read the plan row and VHDL citation at the moment the facility becomes
observable and write the real assertion. This is on purpose:

- It forces a human (or reviewer agent) to revisit the spec at the moment
  the test first crosses the C++ boundary, catching half-working
  implementations before they get rubber-stamped.
- It keeps the skip list an honest inventory of outstanding work; skips
  that silently flip to passes because a setter now exists would hide
  which features actually got implemented correctly.
- It gives the fix branch a natural scope: "this fix un-skips exactly
  these N rows" is both the acceptance criterion and the PR description.

### Counter-examples (not all un-skips are 1:1 with emulator work)

- **Observability-only skips.** Sprites `G9.RO-03/04` (delta counter) and
  `G12.RP-03/04` (anchor-H latch) are skipped because the internal state
  is not exposed, not because the behaviour is missing. Un-skipping them
  is an API decision — add an observer/accessor on `SpriteEngine`, or
  restructure the test to probe through side effects — not an emulator
  feature implementation.
- **Harness-only skips.** Copper `ARB-01/02/03` need a cycle-accurate
  shared `nr_wr_*` bus in the test harness, not just in the emulator.
  Un-skipping requires harness work even after the emulator side lands.
- **One-fix-many-skips.** Copper `OFS-01..06` are 6 rows blocked by a
  single missing feature (NR 0x64 / cvc offset model). One emulator fix,
  6 un-skips in a single commit.
- **One-subsystem-many-batches.** Input has ~68 skips in the joystick
  family. They should be un-skipped in sub-feature batches as each piece
  lands (NR 0x05 decoder → Kempston 1/2 → MD3 → MD6+NR 0xB2 → Sinclair/
  Cursor adapters → I/O mode mux), not all at once. Each batch is its own
  branch with its own review.

## 6. Publishing honest pass rates

Every test plan has a **Current status** block near the top of the
document (or in §5 Notes for the older plans). The block reports live
pass / live fail / skip numbers, the merge commit where those numbers were
measured, and a one-line pointer at what the failures are. Update it
whenever:

- A test rewrite lands — publish the measured numbers, not the plan
  target.
- An Emulator Bug backlog fix lands — publish the new numbers after re-running
  the suite on main.
- A new row is added or retracted — update the denominator.

`doc/design/EMULATOR-DESIGN-PLAN.md` carries the aggregate table across
all 16 subsystems and the unit-test grand total. Keep that table and the
per-plan Current status blocks in sync.

**Never publish a 100% pass rate for a plan that still has skips.** 100%
pass on a live subset is honest; 100% pass on the whole plan is only
honest when the skip list is empty.

## 6a. Updating the traceability matrix after a test run

`doc/testing/TRACEABILITY-MATRIX.md` is the canonical map from every plan
row to its test ID, VHDL citation, runtime status, and test-file location.
It was generated mechanically at Task 5 Step 6 and must be refreshed
whenever anything below changes, so that the published numbers in the
matrix agree with what the binaries actually report:

- A subsystem test suite is re-run and at least one row flips state
  (pass ↔ fail, pass/fail ↔ skip, or `—` → any concrete state).
- A test rewrite or review-fix commit lands on main — the last-touch SHA
  in the per-subsystem header changes, and any new/removed test IDs
  shift the "In-test" and "Missing" counts.
- A plan row is added, retracted, renamed, or re-cited against different
  VHDL lines.
- A `skip()` is replaced by a real `check()` (per §5's un-skip rule) or
  vice versa.
- An "Extra coverage" row gets promoted to a real plan row (or the
  reverse).
- A subsystem gets refactored from a helper-function / data-driven
  pattern to per-row `check()` IDs — the older suites currently report
  `—` in the Status column precisely because they lack per-row tracking,
  and refactoring one of them flips dozens of rows at once.

### Refresh workflow

The mechanical parts of this workflow (running binaries, parsing FAIL sets,
greping source for `check()`/`skip()`/`stub()` call sites, rewriting the
Status and Test-file columns while preserving column widths) are automated
by `test/refresh-traceability-matrix.py`. The script covers all 15 non-Z80N
subsystems (the 9 Task 1 refactored suites plus the 6 Phase 2 rewrites —
Layer2, Sprites, Copper, Compositor, IO Port Dispatch, Input). Z80N is
deliberately excluded — its FUSE data-driven runner has no per-row IDs and
stays permanently `missing` in the Summary by design.

Run it from the repo root after the affected test binary is built:

```bash
cmake --build build --target <sub>_test -j$(nproc)
python3 test/refresh-traceability-matrix.py
```

The script prints a per-subsystem tally (pass / fail / skip / miss) so you
can immediately see whether the refresh made sense. A non-zero **miss**
column is the important signal: it means the matrix has plan row IDs that
are NOT found anywhere in the current test source. Investigate before
committing — usually it points at one of:

- **ID-naming drift**: the author of the Phase 2 rewrite chose a slightly
  different ID string than the pre-refactor matrix used (e.g. `S01.14` vs
  `S1.14`, or `I2C-P05` split into `I2C-P05a`/`I2C-P05b`). The script
  already normalises sub-letter variants up to `a`/`b`/`c`, but other drift
  must be fixed by hand — typically by editing the matrix Test ID column
  to match the authoritative source file.
- **Pseudo-header rows** in the matrix that were never real plan row IDs
  (e.g. `ROM3-conditional` as a group label, `0x82-85` as a register range
  header). Delete them from the matrix; they were coverage-theatre to begin
  with.
- **Genuinely missing rows**: the rewrite dropped a plan row entirely.
  This is the theatre-failure mode §8 exists to prevent — re-read the plan
  and re-add the assertion before committing the refresh.

The script's input assumptions:

- Test source files are at `test/<sub>/<sub>_test.cpp` with one file per
  subsystem.
- `check("ID", ...)` and `skip("ID", ...)` calls use string literals for
  plan row IDs. Struct-of-rows loops with `check(r.id, ...)` are also
  handled: the script grep for any plan-row-shaped string literal in the
  source, so initializers like `{ "MMU-01", ... }` feed into the pass set.
- `skip()` call sites are always direct, not hidden behind a loop variable.
- Test binaries print `  FAIL ID: ...` or `  FAIL ID [...` for every
  failing row — this is the one output-format assumption the script makes,
  and it has held across every harness in the repo.

The manual fallback is below, in case the script is unavailable or the
refresh touches areas the script doesn't automate (last-touch commit hash,
discrepancies bullets, Summary table totals line):

1. **Re-run the affected suite** on main with a clean build. Capture the
   raw `check()`/`skip()` output; don't trust memory.
2. **Open `doc/testing/TRACEABILITY-MATRIX.md`** and locate the
   subsystem's section. Every section has a fixed shape: a last-touch
   commit header, one main table, and (sometimes) an "Extra coverage"
   subsection.
3. **Update the last-touch commit** at the top of the section to the new
   SHA (both full form and 10-char short form as elsewhere in the doc).
   `git log -1 --format='%H' -- test/<sub>/<file>.cpp` is the source of
   truth.
4. **Update each row's Status column** for every row whose state moved.
   Allowed values: `pass`, `fail`, `skip`, `stub`, `missing`, `—`. Use
   `—` only when the suite genuinely cannot report per-row status (the
   older helper-function patterns); do not use `—` as a synonym for
   "haven't looked yet."
5. **Update each row's Test file:line** if assertions were re-grouped or
   moved. Grep the test file for the test ID once; the first matching
   `check()` or `skip()` call wins, same rule the original extractor
   used.
6. **Update the Summary table at the top** of the document — the
   Pass / Fail / Skip-Stub / Missing columns and the aggregate Totals
   line. This is the most common place to miss an update, and the
   summary is what most readers look at first.
7. **Re-align the table borders.** Every `|` in a column must still land
   at the same position after your edits. If a cell got longer, bump the
   column width for that whole table (header, separator row, and every
   data row). A wider column by one cell is not acceptable — the hard
   rule from §7 of this doc applies: tables are aligned or they are
   broken.
8. **Discrepancies section.** If your refresh resolves a discrepancy
   previously listed (e.g. you refactored Tilemap to per-row IDs and it
   no longer needs `—`), remove the corresponding bullet. If your run
   uncovers a new discrepancy, add a bullet.
9. **Commit the matrix update alongside the change that triggered it.**
   An emulator fix branch, a test rewrite branch, or a suite-refactor
   branch should all include the matrix diff in the same commit (or the
   immediately following commit on the same branch) so reviewers can
   see the numbers move together with the code. A separate "update
   matrix" commit with no other context is a smell — it usually means
   someone ran the tests, saw drift, and patched the doc instead of
   investigating.

### What NOT to do

- **Do not fake pass/fail without running the binary.** The matrix was
  originally generated read-only (status derived from `check()` vs
  `skip()` structure) and that is the only context where `—` is
  allowed. Once you're updating a row's status, you must have actually
  executed the test on main.
- **Do not regenerate the whole matrix from scratch for a small change.**
  The mechanical extraction is expensive in tool calls and risks
  reintroducing fixed discrepancies. Edit in place.
- **Do not update the matrix without also updating the corresponding
  plan's Current status block and the EMULATOR-DESIGN-PLAN.md table**
  (§6 rule). All three numbers must move together.
- **Do not lose the "Extra coverage (not in plan)" subsection** for a
  subsystem just because a refresh removed the rows that populated it.
  Leave the subsection header with an explicit "(none)" line so future
  readers know the absence is intentional, not an oversight.

### Per-subsystem gotchas to watch for

- **Phase 2 rewrites (Layer2, Sprites, Copper, Compositor, IO Port
  Dispatch, Input)**: these have proper per-row `skip()`/`check()`
  tracking. Refreshes should produce concrete `pass`/`fail`/`skip`
  values for every row except ones genuinely deferred to the integration
  tier. If a row in one of these six subsystems still reports `—` after
  a refresh, that is a bug in the refresh, not an acceptable state.
- **Older rewrites (Memory/MMU, ULA Video, Tilemap, Audio, DMA, DivMMC,
  CTC, UART, NextREG)**: per-row status is `—` until the suite is
  refactored. A refresh after a run of these suites should still leave
  Status as `—` unless the refactor has also landed. The useful signal
  to update for these is the aggregate Pass / Fail count in the summary
  table — derive it from the suite's trailing summary line, not from
  the per-row table.
- **Z80N**: data-driven FUSE runner; per-row is permanently `missing`
  by design. The aggregate Pass column in the summary table tracks the
  runner's overall pass count (currently 1356/1356 FUSE + 78/78 Z80N
  compliance). Update only the aggregate, never the per-row rows.

## 7. Worktree mechanics and the sub-agent sandbox

A few operational rules that bit us during Phase 2 and are easy to forget:

- **Always use absolute paths when cd'ing into worktrees from the main
  session.** Relative `.claude/worktrees/agent-xxxxx` is ambiguous once
  the shell cwd changes mid-session.
- **Sub-agents cannot run cmake, the test binary, or `regression.sh`.**
  The sandbox blocks them regardless of permission mode. Sub-agents write
  test source and commit on their branch; the main session builds, runs
  tests, and merges. See `memory/feedback_subagent_sandbox.md`.
- **Isolation-worktree agents can escape with absolute `cd`.** The
  isolation is advisory, not enforced. Brief every sub-agent prompt with
  a "NEVER cd out of the worktree" clause and a "when using Write/Edit,
  verify the path resolves inside the worktree, not the main checkout"
  clause. See `memory/feedback_agent_worktree_escape.md`.
- **Prune worktrees after merge.** `git worktree remove --force
  .claude/worktrees/agent-xxxxx` after the merge commit lands. Stale
  worktrees accumulate build artifacts and confuse later `git worktree
  list` output. If the worktree contains submodules, `--force` is
  required.
- **Regression always runs on real desktop.** The sandbox reports
  ~billions of pixel diffs on every screenshot test because it has no
  graphics substrate. Ask the user to run `bash test/regression.sh`
  before claiming a merge is safe.

## 8. The theatre pattern and why this process exists

Between v0.91 and the Task 4 critical review in April 2026, six subsystem
test plans reported "100% passing" while containing:

- Tautological assertions (`check(x, x, ...)`, `|| true` as condition
  padding, `check(a == b || a != b, true, ...)` — all three patterns were
  found).
- Anti-tests that pinned wrong behaviour as "expected" so the suite
  couldn't catch regressions.
- Missing rows — the plan listed 95 rows and shipped 61 tests with no
  indication that 34 had been silently dropped.
- VHDL citations copied from the C++ implementation rather than the spec,
  so the assertion happened to match whatever the emulator did.
- "`|| true` OR condition" in section toggles so an entire group ran in
  no-op mode without anyone noticing.

Every one of these made the pass rate meaningless. The audit retracted
all six "100%" claims (`Task 5 Step 5 Phase 2`), and the current plan
rewrites are the replacement. The process in this document — the no-C++-
as-oracle rule, the skip/fail distinction, the 1:1:1 emulator-fix rule,
the required independent review, the honest-numbers-in-plan-files
discipline — exists specifically to make theatre impossible to reintroduce
without someone noticing.

If any of these rules feel like friction during real work, that is
intentional friction. The friction is the prevention. Do not file a
short-cut until you have re-read §4 and §5 and thought about what the
short-cut would have let slip through in the original theatre suites.

## 9. Quick reference: what to do when…

| Situation | What to do |
|---|---|
| A test plan row does not match VHDL | Report in the review. Do not silently edit the plan. |
| A test fails because VHDL disagrees with C++ | File an Emulator Bug backlog item in `.prompts/YYYY-MM-DD.md`. Leave the test failing. |
| A test fails because the author misread VHDL | Reviewer fixes the assertion on the same branch. Commit message `test(<sub>): review fix — <what>`. |
| A plan row cannot be exercised via the current API | Replace the assertion with `skip(id, reason)`. Reason must be one line and explain *what* is unreachable, not *that* it is unreachable. |
| An emulator fix lands and unblocks skipped rows | Same branch as the fix: delete the `skip()` calls, write the real assertions from the plan. Re-run. |
| A test suite reports 100% pass but has skips | That's expected and honest as long as the skip list is published. Only the empty-skip-list case qualifies for a "fully green" headline. |
| Regression shows a spurious screenshot failure | Check if you're running in the sandbox. The sandbox has no graphics substrate. Rerun on real desktop before assuming a real regression. |
| Two test rewrites land in parallel and conflict on `test/CMakeLists.txt` | Merge conflicts belong to the agent that tried to merge last (per CLAUDE.md). Resolve on that branch, not main. |

## 10. Related documents and memory

- `doc/design/EMULATOR-DESIGN-PLAN.md` — the emulator design plan this
  process plugs into.
- `doc/testing/REGRESSION-TEST-SUITE.md` — the golden-output screenshot
  suite (separate track from unit tests).
- `doc/design/*-TEST-PLAN-DESIGN.md` — the 16 per-subsystem plans.
- `.prompts/YYYY-MM-DD.md` Emulator Bug backlog sections — the live emulator-bug
  backlog sourced from these plans.
- `memory/feedback_test_from_vhdl.md` — the no-C++-as-oracle rule.
- `memory/feedback_verify_before_commit.md` — never commit a fix without
  the user confirming it works first.
- `memory/feedback_regression_refs.md` — never regenerate regression
  reference images without asking first.
- `memory/feedback_subagent_sandbox.md` — build/test/commit from the main
  session, not sub-agents.
- `memory/feedback_agent_worktree_escape.md` — brief sub-agents to never
  leave their worktree.
- `memory/project_test_plan_audit_20260414.md` — the audit that triggered
  the Phase 2 rewrites.
- `memory/project_task5_step5_phase2_complete.md` — the Phase 2 outcome
  and Emulator Bug backlog snapshot.
