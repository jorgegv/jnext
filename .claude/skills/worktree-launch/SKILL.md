---
name: worktree-launch
description: Create a fresh agent worktree under /home/jorgegv/tmp/worktrees/ (OUTSIDE the repo) off an up-to-date main, with the project's hygiene rules baked in. Use when the user says "spin up a worktree", "create a worktree for agent X", "set up a worktree for branch Y", or when about to dispatch an agent that needs an isolated work area.
---

# Launch an agent worktree

Create a worktree for an agent to work in, with the project's hygiene rules baked in.

## Inputs

Ask the user (if not specified):

- **Agent ID** (e.g. `a562cf38`, or a short descriptive slug like `mmu-pass26`).
- **Branch name** (e.g. `audit-mmu-pass-26`).
- **Whether demo artifacts are needed** (NEX/BIN/TAP/TZX/WAV — needed if the agent will run demos).

## Steps

### 1. Verify main is up to date with origin/main

Per `feedback_agent_worktree_stale_base`:

```bash
git fetch origin main
ahead=$(git rev-list --count origin/main..main)
behind=$(git rev-list --count main..origin/main)
echo "main: +$ahead ahead, -$behind behind origin/main"
```

If `behind` > 0, ask the user whether to fast-forward main first. Do NOT auto-pull — `feedback_keep_main_clean` is about not surprising the user.

### 2. Create the worktree

```bash
git worktree add /home/jorgegv/tmp/worktrees/agent-<ID> -b <BRANCH> main
```

### 3. Provision the roms/ fixtures (ALWAYS — the tests need them)

```bash
make -C /home/jorgegv/tmp/worktrees/agent-<ID> worktree-bootstrap
```

`roms/*` is git-ignored, so a fresh worktree gets only the tracked `nextboot.rom`.
Without the SD image, `make unit-test` and the regression suite cannot run. They now
say so loudly instead of quietly reporting a smaller number (Task 37) — but the
agent still can't work, so link the fixtures up front.

### 4. Sync demo artifacts (only if needed)

Per `feedback_worktree_demo_artifacts`, build artifacts under `demo/` aren't checked in, so they must be rsync'd:

```bash
rsync -a --include='*.nex' --include='*.bin' --include='*.tap' --include='*.tzx' \
      --include='*.wav' --include='*/' --exclude='*' \
      /home/jorgegv/src/spectrum/jnext/demo/ \
      /home/jorgegv/tmp/worktrees/agent-<ID>/demo/
```

Skip this step if the agent doesn't need to run demos.

### 5. Print the agent briefing footer

This goes into the agent's prompt:

```
WORKING DIRECTORY: /home/jorgegv/tmp/worktrees/agent-<ID>
BRANCH: <BRANCH>
BASE: main @ <SHA>

Hard rules per CLAUDE.md:
- Work ONLY in this worktree. Do NOT touch /home/jorgegv/src/spectrum/jnext directly.
- Do NOT write to main. Commit only on branch <BRANCH>.
- Do NOT push. The user authorizes pushes separately.
- Use `git -C <worktree-path> <cmd>` for git ops (not `cd ... && git ...`).
- When done, report:
  - List of commit SHAs on <BRANCH>
  - Triplet status on <BRANCH> (ctest / FUSE / regression)
  - Anything that needs reviewer attention
- Do NOT mark work complete without an independent reviewer agent approving.
```

## Hard rules per CLAUDE.md

- Each independent function = its own branch.
- Agents do not write to main.
- Code review by a different agent than the author.
- No pushes without explicit user authorization.

## Cleanup

After the agent's branch is merged to main:

```bash
git worktree remove /home/jorgegv/tmp/worktrees/agent-<ID>
git branch -d <BRANCH>   # only if user authorizes
```

Per `feedback_rehome_to_owner_plan`, prefer re-homing over deleting if the original work might still be needed.
