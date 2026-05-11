# Worktrees

Project worktrees live under `.claude/worktrees/agent-<id>/`. Each agent gets
its own worktree off `main`. The team manager creates them, agents work in
them, the manager merges to main and cleans them up.

## Layout

```
/home/jorgegv/src/spectrum/jnext/
├── .claude/
│   └── worktrees/
│       ├── agent-a562cf38/   ← worktree (full repo copy with shared .git)
│       ├── agent-ad6b7cf6/
│       └── ...
└── ... (main checkout)
```

Each worktree is a full working copy on its own branch, sharing the `.git`
object database with the main checkout. Cheap to create, cheap to delete.

## Creating

Use `/worktree-launch <agent-id> <branch-name>` to create one. It:

1. Verifies main is up to date with origin/main (`feedback_agent_worktree_stale_base`).
2. Creates the worktree at `.claude/worktrees/agent-<id>` on branch
   `<branch-name>` based off main.
3. Optionally rsyncs build artifacts under `demo/` (per
   `feedback_worktree_demo_artifacts` — `.nex`/`.bin`/`.tap`/`.tzx`/`.wav`
   files aren't checked in).
4. Prints a briefing footer for the agent prompt.

## The five rules

1. **Stay in the worktree.** Agents work ONLY in their assigned worktree path.
   Never `cd` to the main checkout. Never modify `/home/jorgegv/src/spectrum/jnext/` directly. (`feedback_agent_worktree_escape`)

2. **Use `git -C` for git ops.** Never `cd <worktree> && git <cmd>`. The
   warn-cd-git hook will warn; future versions may block.

3. **No writes to main from the worktree.** Workers commit on their branch,
   not on main. The manager handles merges.

4. **No pushes.** Worktree branches stay local. The user authorizes the
   eventual push of the merged main.

5. **Sync demo artifacts on creation, not later.** Don't `git add` `.nex` files;
   they're build outputs.

## Stale-base trap

Per `feedback_agent_worktree_stale_base`: if `main` in the project is behind
`origin/main`, a worktree created off `main` starts at the stale SHA. Agents
working there will diverge from origin/main without realizing. The
`/worktree-launch` command checks and asks the user first.

If you discover this happened, the second-to-merge agent fixes the rebase on
its own branch:

```bash
git -C /path/to/worktree fetch origin
git -C /path/to/worktree rebase origin/main
```

## Cleanup

After a worker's branch is merged to main:

```bash
git worktree remove /home/jorgegv/src/spectrum/jnext/.claude/worktrees/agent-<id>
git branch -d <branch-name>   # only if user authorizes branch deletion
```

Per `feedback_rehome_to_owner_plan`, branches that are still owned by another
agent should be re-homed (e.g. with `git -C <worktree> branch -m <new-name>`)
rather than deleted, so the original work isn't lost.

## What goes in the worktree, what doesn't

- ✅ Source edits, test edits, new commits — all on the worktree branch.
- ✅ Build artifacts in `build/` — gitignored, regenerated per worktree.
- ✅ Demo NEX/BIN/etc. rsync'd at creation time — gitignored.
- ❌ Logs / probe output — keep these under `/tmp/`, not in the worktree.
- ❌ User-visible documentation — that goes on main via a merged PR-equivalent.

## Common patterns from history

The settings.local.json has dozens of `Bash(LANG=C make -C /home/jorgegv/src/spectrum/jnext/.claude/worktrees/agent-XXXX unit-test)` entries — these
get covered by the wildcard `Bash(LANG=C make -C *:*)` in the consolidated
settings.json.
