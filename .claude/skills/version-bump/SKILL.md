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

### 0. Public release only

Read doc/RELEASE-PROTOCOL.md §2.2 and §7 first. If the release closes a milestone, both `milestone <M>: ... Guide audit and review` issues must be closed (§2.3). Run `make cli-check` and the §2.2 read of the docs against the running product.

### 1. Tests must pass

On `main`'s tip, BEFORE the bump (doc/RELEASE-PROTOCOL.md §7 step 1):
- a CLEAN CI batch gate (CLAUDE.md merge protocol step 2): `LANG=C make ci-gate-check RUN=<run-id> SHA=<sha>` exits 0 with `verdict: CLEAN`, and `LANG=C make regression-ci-check` finds that run for the regression. Record the run's URL.
- Only when CI cannot run: the local gate list of CLAUDE.md merge protocol step 2, in a detached worktree of `main`'s tip; a matching `LANG=C make regression-stamp-check` stands in for its `make regression`.

All must report PASS for every test: no FAIL and no SKIP (CLAUDE.md, owner 2026-10-06). If anything fails, **STOP and report** — bump is not possible until tests are green.

### 2. Update the traceability matrix

No manual run (GH #196): `make unit-test` (step 3's `make unit-test-dashboard` runs it) regenerates `doc/testing/TRACEABILITY-MATRIX.md` and fails if the committed copy differs. If it changed, commit it (`docs(traceability): refresh for vX.Y.Z`).

### 3. Update the unit-test status report

Run: `LANG=C make unit-test-dashboard` (runs `make unit-test` and rebuilds `test/SUBSYSTEM-TESTS-STATUS.md` from its summary).

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

Read doc/RELEASE-PROTOCOL.md first. Ask whether this is a public release, then run: `JNEXT_ALLOW_MAIN_WRITE=1 make bump-<bump_type> PUBLIC_RELEASE=y|n` where bump_type ∈ {patch, minor, major} (without `PUBLIC_RELEASE`, a non-TTY run silently makes a private tag; `y` only when the user said this is a public release).

This will:
- Bump `version.yaml`
- Commit
- Create a git tag

**Do NOT push the tag.** Per CLAUDE.md, pushes require explicit user authorization. Print the new tag and stop.

## Final report

```
## Version bumped: vX.Y.Z

### Steps
- [✓] Tests passed (CI gate <run-id>: CLEAN; or the local fallback's counts)
- [✓] Traceability matrix refreshed (commit <sha>)
- [✓] Unit-test status refreshed (commit <sha>)
- [✓] DEVELOPMENT-SESSIONS updated (commit <sha>)
- [✓] ChangeLog updated (commit <sha>)
- [✓] Version bumped via make bump-<type> (commit <sha>, tag vX.Y.Z)

### Next
- NOT pushed. Push per doc/RELEASE-PROTOCOL.md §7 step 5: `main` (the bump commit), then
  the batch's private tags in one push, then a public release tag on its own; explicit
  refspecs, no CI run on `main` in progress (§6). A public release push needs the user's
  "push" in the current message.
```

## Hard rules

- **Don't skip steps.** All 7 are mandatory.
- **Don't push.** The block-push hook will refuse anyway; the user authorizes the push.
- **Don't `--no-verify`.** If a pre-commit hook fails, fix the root cause; don't bypass.
- **Stop on any failure.** Partial bumps leave the repo in a confusing state.
