#!/usr/bin/env bash
# PreToolUse hook for Bash. Blocks git write operations against a checkout whose
# current branch is `main`: commit, merge, rebase, reset --hard, cherry-pick,
# revert, am — run bare, via `git -C <path>`, or after a `cd <path>`.
#
# CLAUDE.md mandate: "Agents should NOT write to the main branch, ever. Only
# on their own branches and worktrees!"
#
# The test is the TARGET CHECKOUT'S BRANCH, not which checkout it is. A worktree
# that somehow sits on main is just as forbidden, and `git -C <worktree> commit`
# on a feature branch is fine. `-C` used to skip the hook entirely, which made
# the guard inert under CLAUDE.md's own "always use git -C" convention.
#
# Each shell segment resolves separately, so `git -C <repo> log && git -C
# <worktree> commit` is allowed.
#
# Override: JNEXT_ALLOW_MAIN_WRITE=1 (only when the user has explicitly
# authorized a main-branch operation, e.g. merging an agent's converged branch).

set -euo pipefail

REPO_ROOT="/home/jorgegv/src/spectrum/jnext"

input="$(cat)"
cmd="$(printf '%s' "$input" | jq -r '.tool_input.command // ""')"
cwd="$(printf '%s' "$input" | jq -r '.cwd // ""')"
[ -n "$cwd" ] || cwd="$REPO_ROOT"

# Override.
if printf '%s' "$cmd" | grep -qE '(^|[[:space:]])JNEXT_ALLOW_MAIN_WRITE=1([[:space:]]|$)'; then
  exit 0
fi

is_write_op() {
  printf '%s' "$1" | grep -qE '(^|[[:space:]])git([[:space:]]+-C[[:space:]]+[^[:space:]]+)*[[:space:]]+(commit|merge|rebase|reset[[:space:]]+--hard|cherry-pick|revert|am)([[:space:]]|$)'
}

# This segment's `-C <path>`, if any. Last one wins, as git itself does.
segment_target() {
  printf '%s' "$1" | grep -oE '[[:space:]]-C[[:space:]]+[^[:space:]]+' | tail -1 | awk '{print $2}'
}

# Track the working directory across segments so `cd <path> && git commit`
# resolves against <path> rather than the session cwd.
dir="$cwd"
while IFS= read -r seg; do
  [ -n "${seg//[[:space:]]/}" ] || continue

  if printf '%s' "$seg" | grep -qE '^[[:space:]]*cd[[:space:]]+'; then
    newdir="$(printf '%s' "$seg" | sed -E 's/^[[:space:]]*cd[[:space:]]+//; s/[[:space:]].*$//')"
    case "$newdir" in
      /*) dir="$newdir" ;;
      *)  dir="$dir/$newdir" ;;
    esac
    continue
  fi

  is_write_op "$seg" || continue

  target="$(segment_target "$seg" || true)"
  [ -n "$target" ] || target="$dir"

  # Fail open when the branch cannot be resolved (not a checkout, bad path).
  branch="$(git -C "$target" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '')"
  [ "$branch" = "main" ] || continue

  cat >&2 <<EOF
[block-main-write hook] BLOCKED: git write op against a checkout on 'main'.

Per CLAUDE.md: "Agents should NOT write to the main branch, ever. Only on
their own branches and worktrees!"

  segment: $seg
  target:  $target (branch 'main')

Switch to a feature branch / worktree first, or override (when explicitly
authorized to land work on main):
    JNEXT_ALLOW_MAIN_WRITE=1 <your command>
EOF
  exit 2
done < <(printf '%s\n' "$cmd" | tr ';&|' '\n')

exit 0
