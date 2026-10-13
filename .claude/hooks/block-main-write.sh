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

# The checkout this hook lives in: <checkout>/.claude/hooks/<this file>.
# Derived, not hardcoded, so the hook works in any clone (GH #204). Used
# only as the fallback when the tool call carries no cwd.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

input="$(cat)"
cmd="$(printf '%s' "$input" | jq -r '.tool_input.command // ""')"
cwd="$(printf '%s' "$input" | jq -r '.cwd // ""')"
# A backslash-newline continues the same command.
cmd="$(printf '%s' "$cmd" | sed -z 's/\\\n/ /g')"
[ -n "$cwd" ] || cwd="$REPO_ROOT"

# Override.
if printf '%s' "$cmd" | grep -qE '(^|[[:space:]])JNEXT_ALLOW_MAIN_WRITE=1([[:space:]]|$)'; then
  exit 0
fi

# git's global options that may sit between `git` and the subcommand: those with a
# separate value, `--opt=value`, and the flags. Textual and conservative, like
# the rest of this hook; an option missing from this list hides the subcommand.
GIT_OPTS='((-C|-c|--git-dir|--work-tree|--namespace|--config-env|--exec-path|--super-prefix|--attr-source|--shallow-file)[[:space:]]+[^[:space:]]+|--[a-z][a-z-]*=[^[:space:]]*|-p|-P|--paginate|--no-pager|--bare|--no-replace-objects|--literal-pathspecs|--glob-pathspecs|--noglob-pathspecs|--icase-pathspecs|--no-optional-locks|--no-advice|--no-lazy-fetch)'
GIT_PFX="(^|[[:space:](\`'\"])git([[:space:]]+$GIT_OPTS)*[[:space:]]+"

# Write subcommands. `reset` is separate: a path reset (`git reset [<rev>] -- <paths>`)
# only touches the index and is allowed; every other reset moves the branch.
is_write_op() {
  printf '%s' "$1" | grep -qE "${GIT_PFX}(commit|merge|rebase|cherry-pick|revert|am|pull)([[:space:]]|$)" && return 0
  printf '%s' "$1" | grep -qE "${GIT_PFX}reset([[:space:]]|$)" || return 1
  local rest
  rest="$(printf '%s' "$1" | sed -E "s/^.*${GIT_PFX}reset//")"
  printf '%s' "$rest" | grep -qE '(^|[[:space:]])--([[:space:]]|$)' && return 1
  return 0
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

  # --git-dir / --work-tree / GIT_DIR= / GIT_WORK_TREE= can point the write anywhere;
  # not resolved here, so a write that uses one is blocked (override as below).
  if printf '%s' "$seg" | grep -qE '(^|[[:space:]])(--git-dir|--work-tree)([=[:space:]]|$)|(^|[[:space:]])GIT_(DIR|WORK_TREE)='; then
    cat >&2 <<EOF
[block-main-write hook] BLOCKED: git write op with --git-dir / --work-tree / GIT_DIR / GIT_WORK_TREE.

The target is not resolved for these; use -C <path> instead, or override (when
explicitly authorized to land work on main):
    JNEXT_ALLOW_MAIN_WRITE=1 <your command>

  segment: $seg
EOF
    exit 2
  fi

  target="$(segment_target "$seg" || true)"
  if [ -z "$target" ]; then
    target="$dir"
  else
    # A relative `-C` (notably `git -C .`) resolves against the tracked directory,
    # not the session cwd — otherwise `cd <worktree> && git -C . commit` is judged
    # against main and wrongly blocked.
    case "$target" in
      /*) ;;
      *)  target="$dir/$target" ;;
    esac
  fi

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
