---
name: test-triplet
description: Run the full jnext test triplet (ctest unit-tests + FUSE Z80 opcodes + screenshot regression) and cache the result. Use when the user says "run the triplet", "full tests", "test everything", "run all tests", or asks for a comprehensive test status before committing/merging/version-bump.
---

# Test triplet

Run all three test layers and produce the canonical triplet line:

    ctest N/N • FUSE 1356/1356 • regression P/F/S

This is the test posture that ends every jnext handover. Result is cached at `.claude/last-test-triplet.txt` so the SessionStart hook can display it next session.

## Inputs

Optional:
- **Worktree path** (default: repo root `/home/jorgegv/src/spectrum/jnext`).

## Steps (sequential — later layers depend on earlier ones)

### 1. Build
```bash
cmake --build $TARGET/build -j$(nproc) 2>&1 | tail -5
```

### 2. ctest (subsystem unit tests)
```bash
LANG=C make -C $TARGET unit-test 2>&1 | tail -20
```
Parse: tests passed / total. Currently 38 expected.

### 3. FUSE Z80 opcode suite
```bash
$TARGET/build/test/fuse_z80_test $TARGET/build/test/fuse 2>&1 | tail -3
```
Parse: tests passed / 1356.

### 4. Screenshot regression
```bash
short=$(git -C $TARGET rev-parse --short HEAD)
log=/tmp/regression-$short.log
bash $TARGET/test/00regression/regression.sh 2>&1 | tee $log
```
Parse: P / F / S.

### 5. Cache
```bash
echo "ctest N/N • FUSE 1356/1356 • regression P/F/S" > $TARGET/.claude/last-test-triplet.txt
```

If any layer has new FAILs vs baseline, append `(dirty)` to avoid misleading future SessionStart messages.

## Report format

```
## Triplet
ctest N/N • FUSE 1356/1356 • regression P/F/S

- branch: <branch> @ <short-sha>
- log: <path>
- new failures: <list or "none">
```

## Hard rules per feedback memory

- `LANG=C` mandatory on unit tests (`feedback_lang_c_builds`).
- Pre-existing skips ≠ regressions.
- Cache reset only on a fully-green run.
- Don't update reference screenshots without explicit user authorization.
- Don't fix anything — this skill reports, it doesn't fix.

## When to escalate to the `regression-runner` subagent

If the triplet is part of a larger flow (e.g. pre-merge check across many worktrees, or noisy environment), dispatch `regression-runner` instead — it surfaces only new failures, not pre-existing skips, and protects the main context from raw test output. This skill is for ad-hoc triplet runs.
