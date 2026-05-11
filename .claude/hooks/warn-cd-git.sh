#!/usr/bin/env bash
# PreToolUse hook for Bash. Warns (does NOT block) when the user/agent uses
# `cd <path> && git ...` instead of `git -C <path> ...`.
#
# Per CLAUDE.md: "For git commands that run against another directory (e.g. a
# worktree), always use `git -C /abs/path <cmd> ...` instead of `cd /abs/path
# && git <cmd> ...`. The -C flag avoids shell-state side effects and keeps the
# current working directory stable across tool calls."

set -euo pipefail

input="$(cat)"
cmd="$(printf '%s' "$input" | jq -r '.tool_input.command // ""')"

if printf '%s' "$cmd" | grep -qE '(^|[[:space:];&|])cd[[:space:]]+[^[:space:]]+[[:space:]]*&&[[:space:]]*git[[:space:]]'; then
  cat >&2 <<'EOF'
[warn-cd-git hook] WARNING: prefer `git -C <path> <cmd>` over `cd <path> && git <cmd>`.
The -C flag avoids shell-state side effects and skips the cd permission prompt.
(Not blocked. Suppress this warning by using git -C.)
EOF
  # exit 0 = allow; the warning is informational.
fi

exit 0
