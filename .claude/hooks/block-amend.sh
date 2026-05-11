#!/usr/bin/env bash
# PreToolUse hook for Bash. Blocks `git commit --amend`.
#
# CLAUDE.md mandate / feedback memory: "Always create a NEW commit rather than
# amending an existing commit." When a pre-commit hook fails, the commit did
# NOT happen, so --amend would modify the PREVIOUS commit and may destroy work.
#
# Override: prefix with JNEXT_ALLOW_AMEND=1 if the user explicitly asks to amend.

set -euo pipefail

input="$(cat)"
cmd="$(printf '%s' "$input" | jq -r '.tool_input.command // ""')"

if printf '%s' "$cmd" | grep -qE '(^|[[:space:]])JNEXT_ALLOW_AMEND=1([[:space:]]|$)'; then
  exit 0
fi

if printf '%s' "$cmd" | grep -qE '(^|[[:space:];&|])git([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+commit[[:space:]].*--amend'; then
  cat >&2 <<'EOF'
[block-amend hook] BLOCKED: git commit --amend.

Per CLAUDE.md / feedback memory: always create a NEW commit instead of
amending. If a pre-commit hook failed, the commit didn't happen, and --amend
would modify the PREVIOUS commit — potentially destroying work.

Override (only when the user explicitly says to amend):
    JNEXT_ALLOW_AMEND=1 git commit --amend ...
EOF
  exit 2
fi

exit 0
