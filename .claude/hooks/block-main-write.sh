#!/usr/bin/env bash
# PreToolUse hook for Bash. Blocks writes to the `main` branch when the project
# repo's current branch is `main`. Catches: git commit, git merge into main,
# git rebase onto main from main, etc.
#
# CLAUDE.md mandate: "Agents should NOT write to the main branch, ever. Only
# on their own branches and worktrees!"
#
# Override: JNEXT_ALLOW_MAIN_WRITE=1 (only when user explicitly authorizes a
# main-branch operation, e.g. merging an agent's converged branch).

set -euo pipefail

REPO_ROOT="/home/jorgegv/src/spectrum/jnext"

input="$(cat)"
cmd="$(printf '%s' "$input" | jq -r '.tool_input.command // ""')"

# Override.
if printf '%s' "$cmd" | grep -qE '(^|[[:space:]])JNEXT_ALLOW_MAIN_WRITE=1([[:space:]]|$)'; then
  exit 0
fi

# Only enforce when this is a git write operation against the main repo (not a
# worktree). A `git -C <path>` operation against a worktree is allowed; only
# bare `git commit`/`git merge`/`git rebase`/`git reset --hard` etc. without
# -C, executed from the repo root, are subject to the main-branch check.
is_write_op() {
  printf '%s' "$1" | grep -qE '(^|[[:space:];&|])git[[:space:]]+(commit|merge|rebase|reset[[:space:]]+--hard|cherry-pick|revert|am)([[:space:]]|$)'
}

# Skip if the command uses `git -C <some-path>` targeting a worktree.
if printf '%s' "$cmd" | grep -qE '(^|[[:space:];&|])git[[:space:]]+-C[[:space:]]+'; then
  exit 0
fi

if ! is_write_op "$cmd"; then
  exit 0
fi

# Resolve current branch of the main repo. If we can't, fail open (don't block).
branch="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '')"

if [ "$branch" = "main" ]; then
  cat >&2 <<EOF
[block-main-write hook] BLOCKED: write op on main branch.

Per CLAUDE.md: "Agents should NOT write to the main branch, ever. Only on
their own branches and worktrees!"

Current branch in $REPO_ROOT is 'main'. Switch to a feature branch / worktree
first, or override (when explicitly authorized to land work on main):
    JNEXT_ALLOW_MAIN_WRITE=1 <your command>
EOF
  exit 2
fi

exit 0
