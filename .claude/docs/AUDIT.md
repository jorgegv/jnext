# Audit methodology

Sourced from Task 2's 25 audit passes (May 2026) on boot-critical subsystems
(MMU / DivMMC / NMI / CPU). The methodology below is what worked after the
"defensive zero / coverage theatre" failures earlier in the project.

Five mandates govern every audit pass.

## The five mandates

### 1. Enumeration table at the top of every audit report

(`feedback_task2_audit_enumeration_table`)

Every audit report must begin with a complete table covering **every surface
in scope**. For a subsystem like MMU, surfaces include:
- Every NextREG register written to MMU
- Every Z80 port that touches MMU
- Every memory-slot mapping operation
- Every IPL / loader / supervisor consumer of MMU state
- Every test rig that should validate it

Each row: `Surface | C++ site | VHDL line | Match ✓/✗ | Notes`.

**Sparse tables → reject.** A row that says "covered in this commit" but
omits adjacent rows from the same surface = defensive zero. A claim of
"no findings" without a complete table = defensive zero.

### 2. Fix + discriminative regression test, same commit

Every finding becomes a commit containing:
- The fix.
- A new test case that FAILS on the pre-fix code and PASSES on the post-fix
  code.
- A reference (file + line) to the VHDL that motivates the fix.

If a fix is "too obvious" to need a test, that's a red flag — the test
documents the bug for future-Claude. Write it anyway.

### 3. Thorough per pass

(`feedback_task2_audit_thorough_per_pass`)

Find as many bugs as possible **in one pass**. Do not iterate "find one fix
one re-audit". The audit-agent prompt must emphasize maximal coverage per
pass, or agents drift into the wasteful one-bug-at-a-time pattern.

### 4. Enumerate consumers per boot stage

(`feedback_audit_enumerate_all_protocol_consumers_per_boot_stage`)

For protocol bits (SD R1, OCR, NextReg readback, port masks, save-state
schema), enumeration must cover consumers in:
- IPL / loader
- Kernel / runtime
- Supervisor (NextZXOS)
- Test rigs

Single-layer enumeration is defensive zero. The V20-DIVMMC-01 regression
(class-a hidden as class-c) shipped because only one layer was enumerated.

### 5. Audit prompt must include the fix mandate

(`feedback_task2_audit_prompt_must_include_fix_mandate`)

The audit-agent prompt MUST include the explicit text: *"fix + discriminative
regression test in same commit"*. Without it, agents default to report-only
mode and the caller has to run an extra fix-of-audit cycle. The
`subsystem-auditor` agent definition bakes this mandate in; don't remove it.

## Convergence

(`feedback_task2_converged_subsystem_skip`)

A subsystem is **converged** when:
- The latest audit pass returned ZERO findings, AND
- The independent reviewer returned APPROVE-no-missed.

Converged subsystems are **skipped in subsequent passes** to avoid wasted
work. The audit-pass skill tracks this.

If new code lands in a converged subsystem, it must be re-opened for audit
on the next pass.

## Comment-only fix-of-reviewer

(`feedback_task2_skip_review_comment_only`)

If a reviewer's findings result in a commit that only touches comments (e.g.
clarifying a VHDL citation), that fix commit skips the fix-reviewer step.
Only behavior-changing fixes need a second review.

## Class taxonomy

Findings are classified by severity:

- **class-a** — silent regression: emulator does the wrong thing and no test
  catches it. Highest priority.
- **class-b** — test gap: emulator does the right thing but no test would
  catch a future regression.
- **class-c** — lint / clean-up: code style, comments, dead code. Lowest
  priority but still fixed in the pass.
- **class-d** — architectural: requires a design change too big for an audit
  fix. Escalated to user for authorization.

The Task 2 cumulative tally (May 2026): 109 class-a + 23 class-b + 47 class-c + 3 follow-ups + D3 + 5 D3-adj fixes across 25 passes.

## Workflow per pass

1. Manager (or `audit-pass` skill) dispatches one `subsystem-auditor` per
   subsystem, in parallel.
2. Each auditor: scans surfaces, builds enumeration table, finds + fixes +
   tests in one commit per finding.
3. Manager dispatches one `subsystem-reviewer` per auditor, in parallel.
   **Never self-review.** (`feedback_never_self_review`)
4. Reviewer verdict: APPROVE / APPROVE-WITH-NITS / REJECT.
5. On APPROVE, manager merges to main (only the manager touches main).
6. On REJECT, findings go back to the auditor on its own branch.
7. Track convergence; skip converged subsystems next pass.

## Common pitfalls (from Task 2 history)

- **Defensive zero.** "I audited X and found nothing" without an enumeration
  table. Always reject.
- **Single-layer enumeration.** Listing IPL consumers but skipping supervisor
  consumers (or vice versa). Always reject.
- **Self-review.** Auditor approves their own work. Always reject; demand
  independent reviewer.
- **Non-discriminative test.** Test that passes on pre-fix code. Always
  reject; demand a test that fails pre-fix.
- **"Follow-up later" findings.** Reviewer notes a bug but defers it.
  (`feedback_review_no_defer`) Always reject; fix in this pass or escalate
  class-d.
- **Coalescing fixes into one commit.** Breaks the 1:1:1 model and the
  fix-test pairing. Always reject; require one commit per fix.

## Pragmatic remediation workflow

(`feedback_pragmatic_remediation_workflow`)

For large audit findings, the workflow is:
1. Audit identifies the finding.
2. Auditor commits the fix + test.
3. Reviewer confirms in same pass.
4. Coverage wave (separate, can be later) adds retroactive tests for similar
   surfaces.
5. Documentation of the audit (in the audit report and the FEATURES.md/TODO.md
   if significant).

For class-d items, the workflow is:
1. Audit identifies and documents the finding.
2. Escalate to user with cost / benefit analysis.
3. User authorizes the architectural change.
4. Implement on its own branch, with its own audit + review.
