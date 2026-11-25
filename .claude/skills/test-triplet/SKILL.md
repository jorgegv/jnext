---
name: test-triplet
description: Run the full jnext test triplet (unit + SDL-only unit suites + FUSE Z80 opcodes + screenshot regression) and cache the result. Use when the user says "run the triplet", "full tests", "test everything", "run all tests", or for the LOCAL FALLBACK of the batch gate (CI unavailable). Never before a branch merge (branches run `make test-affected`).
---

# Test triplet

Run all three test layers and produce the canonical triplet line:

    unit N/N • sdl N/N • FUSE 1356/1356 • regression P/F/S

A handover ends with the CI batch gate; this line ends one only when the local fallback ran instead. Result is cached at `.claude/last-test-triplet.txt` so the SessionStart hook can display it next session.

The batch gate is a green CI run on `main`'s tip (CLAUDE.md merge protocol step 2: `test/ci-gate.sh`, `make ci-gate-check`). This skill runs its LOCAL FALLBACK only, when GitHub Actions is down or gives no runner within 2 hours. Then run the whole fallback list of CLAUDE.md step 2, in its order (read it there; this skill covers its unit, SDL, FUSE and regression layers), in a detached worktree of `main`'s tip (`~/tmp/worktrees/jnext/main-gate`), or `remote-run.sh --gate`. On a branch, never run it (unless the change is to those harnesses or to the PGO build itself, CLAUDE.md merge protocol step 2): run `make test-affected` before each review round and `make unit-test` once before landing.

**Expected counts are not restated here** — they go stale. The unit total is
pinned per suite in `test/unit-tests.conf` and the harness refuses to run if the
manifest and the registered suites disagree; the regression totals are pinned in
`test/00regression/regression_tests.conf` + `functional_tests.conf`. Take the
numbers from the run, and treat a changed total as a manifest question, not a
transcription question. FUSE is 1356 and does not move.

## Inputs

Target: a detached worktree of `main`'s tip, `~/tmp/worktrees/jnext/main-gate` (create it if missing), unless the user names another. Never the main checkout (step 1's `make clean` wipes its `build/`), never an author's worktree.

## Steps (sequential — later layers depend on earlier ones)

### 0. Read the host load

```bash
nproc; cat /proc/loadavg; ps -eo pcpu,pid,comm --sort=-pcpu | head -5
```

Report it with the result. Load, solo re-run, stamp and PENDING rules: CLAUDE.md
§Testing ("Run the full regression only when it is needed", "Those two rows are
examples, not the list"). Follow them; this skill does not restate them.

### 1. Always rebuild — clean, gui-release

```bash
LANG=C make -C $TARGET clean && LANG=C make -C $TARGET gui-release
```

ABSOLUTE (`feedback_test_runs_always_rebuild`): every test run rebuilds first.
Never test an existing build — a stale binary gives false FAILs and false PASSes.
Before diagnosing any failure, check the binary's timestamp against `git log -1`.

### 2. Unit tests

```bash
LANG=C make -C $TARGET unit-test > /tmp/unit-$short.log 2>&1; echo "status=$?"
tail -25 /tmp/unit-$short.log
```

### 2b. SDL-only unit suites

```bash
LANG=C make -C $TARGET unit-test-sdl > /tmp/unit-sdl-$short.log 2>&1; echo "status=$?"
tail -25 /tmp/unit-sdl-$short.log
```

### 3. FUSE Z80 opcode suite

```bash
$TARGET/build/test/fuse_z80_test $TARGET/build/test/fuse > /tmp/fuse-$short.log 2>&1; echo "status=$?"
tail -3 /tmp/fuse-$short.log
```

### 4. Screenshot regression

`make -C $TARGET regression-stamp-check` first; a matching green stamp satisfies the gate.

```bash
short=$(git -C $TARGET rev-parse --short HEAD)
LANG=C make -C $TARGET regression > /tmp/regression-$short.log 2>&1
echo "status=$?"
tail -30 /tmp/regression-$short.log
```

**Redirect every one of these — never pipe.** `| tail` and `| tee` hand you the
pipeline's exit status, so a failing `make` or a failing suite reads as success.
This is not hypothetical: a run printing `62 pass, 1 fail` and `UNIT TESTS FAILED`
in bold once went green in CI for exactly this reason
(`feedback_ci_runs_exact_local_commands`).

### 5. Cache

```bash
echo "unit N/N • sdl N/N • FUSE 1356/1356 • regression P/F/S" > $TARGET/.claude/last-test-triplet.txt
```

If any layer has new FAILs vs baseline, append `(dirty)` so future SessionStart messages don't mislead.

## Report format

```
## Triplet
unit N/N • sdl N/N • FUSE 1356/1356 • regression P/F/S

- branch: <branch> @ <short-sha>
- host load at run: <1min>/<5min>/<15min> on <nproc> cores
- logs: <paths>
- new failures: <list or "none">
```

## Hard rules per feedback memory

- `LANG=C` on every build/test command (`feedback_lang_c_builds`) — the shell
  locale is Spanish and greps for `warning:` otherwise match nothing.
- Clean gui-release rebuild first, always (`feedback_test_runs_always_rebuild`).
- Redirect, never pipe; check the status (`feedback_ci_runs_exact_local_commands`).
- Any SKIP is a gate failure (CLAUDE.md, owner 2026-10-06): report it as a SKIP, never as a pass. The make targets now EXIT non-zero on one, so the exit status agrees with the counts.
- Cache reset only on a fully-green run.
- Don't update reference screenshots without explicit user authorization.
- Don't fix anything — this skill reports, it doesn't fix.

## When to escalate to the `regression-runner` subagent

If the triplet is part of a larger flow (e.g. the fallback batch gate, or a noisy environment), dispatch `regression-runner` instead — it surfaces new failures and every SKIP, and protects the main context from raw test output. This skill is for ad-hoc triplet runs.
