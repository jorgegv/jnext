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
- **Base branch** (default `main`; an autonomous run passes the epic branch for epic sub-issues).

Unattended runs (`autonomous-run`) do not ask: they pass these inputs, fast-forward `main` themselves (`git fetch origin`, then `JNEXT_ALLOW_MAIN_WRITE=1 git merge --ff-only origin/main`), and skip an issue whose branch or worktree another session holds.

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
git worktree add /home/jorgegv/tmp/worktrees/agent-<ID> -b <BRANCH> <BASE>   # <BASE> = main unless the caller names another (e.g. an epic branch)
```

**If the branch or the worktree already exists, STOP** — an existing workspace is
a signal, not a convenience. Another Claude session may be live in it:

```bash
ps -eo pid,etimes,cmd | grep -c '[c]laude --output-format stream-json'
```

A failed `worktree add`, an uncommitted diff nobody in this session wrote, or a
file changing under you are all the same tell-tale. Ask the user which session
should continue; never merge the two streams of work yourself. Two agents in one
worktree trash each other, which is what the one-branch-per-agent rule exists to
prevent (`feedback_check_for_concurrent_sessions`).

### 3. Provision the roms/ fixtures (ALWAYS — the tests need them)

```bash
make -C /home/jorgegv/tmp/worktrees/agent-<ID> worktree-bootstrap
```

`roms/*` is git-ignored, so a fresh worktree gets only the tracked `nextboot.rom`.
Without the SD image, `make unit-test` and the regression suite cannot run. They now
say so loudly instead of quietly reporting a smaller number (Task 37) — but the
agent still can't work. The target verifies the machine-wide SD master (`~/.jnext/sdcard/`) and the git submodules (`git worktree add` leaves them empty), and says how to fix either.

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
- Never `cd` out of this worktree, and run `pwd` before every commit to confirm it
  matches the path above. Worktree isolation does NOT sandbox the shell: an
  absolute `cd` lands your commits on main.
- Do NOT write to main. Commit only on branch <BRANCH>.
- Do NOT push. The user authorizes pushes separately.
- Use `git -C <worktree-path> <cmd>` for git ops (not `cd ... && git ...`).
- When done, report:
  - List of commit SHAs on <BRANCH>
  - Triplet status on <BRANCH> (unit N/N • sdl N/N • FUSE 1356/1356 • regression P/F/S)
  - Anything that needs reviewer attention
- Do NOT mark work complete without an independent reviewer agent approving.
```

## Hard rules per CLAUDE.md

- Each independent function = its own branch.
- Agents do not write to main.
- Code review by a different agent than the author.
- No pushes without explicit user authorization.

## Cleanup

After the agent's branch is merged into its target (`<TARGET>` = `main`, or the epic branch for an epic sub-issue). A detached reviewer worktree has no branch: only the status check applies.

```bash
git -C /home/jorgegv/tmp/worktrees/agent-<ID> status --short   # must be empty
git merge-base --is-ancestor <BRANCH> <TARGET>                   # must succeed
git worktree remove --force /home/jorgegv/tmp/worktrees/agent-<ID>   # --force: the tree has submodules
git branch -d <BRANCH>   # only if user authorizes
```

Plain `git worktree remove` always refuses here (submodules). The auto-mode classifier allows `--force` only when the owner asked for the cleanup in that message; otherwise leave the worktree and list it in the handover.

Keep the branch unless the user authorizes deleting it.
