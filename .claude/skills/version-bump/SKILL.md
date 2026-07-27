---
name: version-bump
description: Execute the 7-step version-bump sequence from CLAUDE.md. Atomic — bails out if any step fails. Use when the user says "bump version" or "release vX".
---

# Version bump

Execute the 7-step sequence from CLAUDE.md verbatim. Each step is gated: if a step fails, **stop and report**, don't continue.

## Inputs

Ask:
- **Bump type:** `patch`, `minor`, or `major`.

## The 7 steps

### 1. Tests must pass

Run both:
- `LANG=C make unit-test`
- `bash test/00regression/regression.sh`

Both must report PASS for every test (SKIPs acceptable, FAILs not). If anything fails, **STOP and report** — bump is not possible until tests are green.

### 2. Update the traceability matrix

Run: `perl test/refresh-traceability-matrix.pl`

Commit the result if it produced changes (`docs(traceability): refresh for vX.Y.Z`).

**A non-zero exit here is expected and is NOT a bump blocker.** It means the
matrix was rewritten *and* still under-records — the script lists the rows its
mapped test sources assert that the matrix omits, plus the suites with no
section at all (GH #117). Note whether that backlog grew since the last bump;
it never blocks. Only step 1's test failures block a bump.

### 3. Update the unit-test status report

Run: `bash test/refresh-subsystem-status.sh`

Commit the result if changed.

### 4. Update DEVELOPMENT-SESSIONS.md

Open `doc/DEVELOPMENT-SESSIONS.md` and add a section for the upcoming version. Distill from this session's git log + handover memo if present. Per CLAUDE.md, this is a development diary — different audience than ChangeLog.

Commit.

### 5. Update ChangeLog

Invoke the `changelog-update` skill (which handles the rules: 4 sections, terse, no commit IDs, no trivial fixes, etc.).

Commit the ChangeLog update.

### 6. Commit the prep changes

Should already be committed step-by-step above. Verify clean tree.

### 7. Bump

Run: `make bump-<bump_type>` where bump_type ∈ {patch, minor, major}.

This will:
- Bump `version.yaml`
- Commit
- Create a git tag

**Do NOT push the tag.** Per CLAUDE.md, pushes require explicit user authorization. Print the new tag and stop.

## Final report

```
## Version bumped: vX.Y.Z

### Steps
- [✓] Tests passed (ctest N/N, regression P/F/S)
- [✓] Traceability matrix refreshed (commit <sha>)
- [✓] Unit-test status refreshed (commit <sha>)
- [✓] DEVELOPMENT-SESSIONS updated (commit <sha>)
- [✓] ChangeLog updated (commit <sha>)
- [✓] Version bumped via make bump-<type> (commit <sha>, tag vX.Y.Z)

### Next
- NOT pushed. To push, ask the user explicitly. Then:
    JNEXT_ALLOW_PUSH=1 git push origin main --tags
```

## Hard rules

- **Don't skip steps.** All 7 are mandatory.
- **Don't push.** The block-push hook will refuse anyway; the user authorizes the push.
- **Don't `--no-verify`.** If a pre-commit hook fails, fix the root cause; don't bypass.
- **Stop on any failure.** Partial bumps leave the repo in a confusing state.
