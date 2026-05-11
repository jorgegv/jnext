# Workflow rules

Project-wide workflow constraints. These are the rules that apply to every
session, regardless of subsystem or task. Sourced from CLAUDE.md and the
project's `feedback_*` memory entries.

## Branch & merge

- **No writes to `main` by any agent.** Workers commit on their own branch in
  their own worktree. The team manager merges to main (and only after
  independent review approves).
- **One branch per independent unit of work.** When parallel agents are
  dispatched, each gets its own branch + worktree. This prevents code-trashing.
- **Merge conflicts are the second-to-merge agent's responsibility.** They fix
  the conflict on their own branch, not on main.
- **Don't move tags.** Once a version tag is created (via `make bump-*`), it
  doesn't move. If a bump was wrong, bump again. (`feedback_dont_move_tags`)

## Commits

- **One concept per commit.** Don't bundle a fix and a refactor.
  (`feedback_individual_commits`)
- **Terse but insightful commit messages.** No commit-message essays. State
  what changed and the WHY in one or two lines. (`feedback_terse_commit_messages`)
- **NEVER `--amend`.** Always create a new commit. The block-amend hook enforces
  this. Reason: pre-commit hook failures mean the commit didn't happen; --amend
  would modify the PREVIOUS commit and destroy work.
- **NEVER `--no-verify`.** Pre-commit hooks fail for a reason; fix the root
  cause.
- **NEVER include `Co-Authored-By` lines.** Project mandate (CLAUDE.md).
- **Verify before commit.** Run the affected tests before committing.
  (`feedback_verify_before_commit`)

## Pushes

- **NEVER push to origin without explicit user authorization.** This applies to
  every agent, including the manager. The block-push hook enforces this.
  Override: `JNEXT_ALLOW_PUSH=1 git push ...` (only after user says "push").
- **Local main can be ahead of origin/main for a long time.** That's normal;
  it doesn't mean "we need to push". (`feedback_local_main_not_origin`)
- **Same rule applies to `gh pr create`** and any equivalent.

## Code review

- **Code review is NEVER by the same agent that wrote the code.**
  (`feedback_never_self_review`) The reviewer agent is independent. Use
  `subsystem-reviewer` for change review, or dispatch a separate general-purpose
  Agent.
- **Reviewers are critical by default.** A "looks fine" review is not enough.
  (`feedback_audit_passing_rows`)
- **Reviewers don't defer findings.** If a bug is found, surface it now —
  don't write "follow-up" notes. (`feedback_review_no_defer`,
  `feedback_dont_defer_dashboard_fixes`)
- **Reviewers run the screenshot regression** for changes that could affect
  rendering. (`feedback_review_run_screenshot_regression`)
- **Reviewer also checks test-removal.** If a change removes a test, an
  independent reviewer must justify it. (`feedback_reviewer_for_test_removal`)

## Parallel agents

- **Use Agent Team pattern** when tasks are independent. Manager dispatches
  workers (one per task), each in its own branch/worktree.
- **Manager does NOT write code.** Manager plans, dispatches, merges.
  (CLAUDE.md)
- **Parallel budget.** Per `feedback_parallel_agent_budget_20260421`, don't
  fork unbounded agents; cap at a sensible parallel count (the user can adjust
  this).
- **Spawn parallel agents in one message.** Single tool-call message with
  multiple `Agent` invocations — that's what triggers parallelism.

## Code style

- **No trivial backlog.** Don't write "TODO: maybe rename this later" comments.
  (`feedback_no_trivial_backlog`)
- **Approximation comments are drift flags.** A comment that says "this is
  approximately what VHDL does" means the emulator doesn't match VHDL faithfully —
  flag it for fix, don't normalize it. (`feedback_approximation_comments_are_drift_flags`)
- **No "won't" taxonomy.** Don't categorize bugs as "won't fix" preemptively.
  (`feedback_wont_taxonomy`)
- **No files outside the repo.** Probe logs, test artifacts, etc. go under
  `/tmp/` or in repo subdirs. Don't write to `~/` arbitrary paths.
  (`feedback_no_files_outside_repo`)
- **Demo source code first.** When investigating a demo issue, read the demo's
  source (C / asm) before tracing emulator behavior. (`feedback_demo_source_code_first`)

## Testing posture

See `TESTING.md`. Highlights:

- VHDL is the single oracle.
- Three test layers (ctest, FUSE, regression) must be green.
- Pre-existing skips are not failures.
- Pixel-equivalence required for reference screenshot regeneration.

## Git command hygiene

- **Use `git -C <path>` instead of `cd <path> && git ...`.** The block-cd-git
  hook warns when you don't.
- **Read-only paths** like `awk`, `sed -n`, `xxd`, `grep` are pre-allowed in
  settings.json. Don't surprise the user with `chmod`, `rm -rf`, etc.

## Daily prompt files

- The `.prompts/<date>.md` file contains the day's tasks. Keep a **Task
  Completion Status** section in it. Update when tasks finish. (CLAUDE.md)

## Tremendous claims

- **Every claim needs proof.** "I fixed it" requires triplet output or a probe
  log showing the change. Don't declare victory without evidence.
  (`feedback_tremendous_claims_proof`)
- **Verify behavior, not just compilation.** A build success is not a fix.
  (`feedback_verify_behavior`)
- **Verify gating dependency.** If a fix changes a code path, verify the new
  path is actually exercised by the test that "passes". (`feedback_verify_gating_dependency`)
