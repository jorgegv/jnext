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
- the regression: `LANG=C make regression-ci-check` on `main`'s tip BEFORE the bump (a green CI run on a commit with the same non-doc content satisfies it — doc/RELEASE-PROTOCOL.md §7), else `LANG=C make regression-stamp-check` (a local green stamp), else `LANG=C make regression`

Both must report PASS for every test: no FAIL and no SKIP (CLAUDE.md, owner 2026-10-06). If anything fails, **STOP and report** — bump is not possible until tests are green.

### 2. Update the traceability matrix

No manual run (GH #196): `make unit-test` in step 1 regenerates `doc/testing/TRACEABILITY-MATRIX.md` and fails if the committed copy differs. If step 1 changed it, commit it (`docs(traceability): refresh for vX.Y.Z`).

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
- [✓] Tests passed (unit N/N • sdl N/N • FUSE 1356/1356 • regression P/F/S)
- [✓] Traceability matrix refreshed (commit <sha>)
- [✓] Unit-test status refreshed (commit <sha>)
- [✓] DEVELOPMENT-SESSIONS updated (commit <sha>)
- [✓] ChangeLog updated (commit <sha>)
- [✓] Version bumped via make bump-<type> (commit <sha>, tag vX.Y.Z)

### Next
- NOT pushed. To push, ask the user explicitly. Then:
    JNEXT_ALLOW_PUSH=1 git push origin main && JNEXT_ALLOW_PUSH=1 git push origin vX.Y.Z
  (explicit refspecs, ≤3 tags per push, CI on main idle)
```

## Hard rules

- **Don't skip steps.** All 7 are mandatory.
- **Don't push.** The block-push hook will refuse anyway; the user authorizes the push.
- **Don't `--no-verify`.** If a pre-commit hook fails, fix the root cause; don't bypass.
- **Stop on any failure.** Partial bumps leave the repo in a confusing state.
