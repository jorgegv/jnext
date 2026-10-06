#!/usr/bin/env bash
# PreToolUse hook for Bash. Blocks `git push` and `gh pr create` unless
# JNEXT_ALLOW_PUSH=1 is set in the environment of the launched command.
#
# CLAUDE.md mandate: "NEVER push to origin without explicit user authorization.
# This applies to the manager AND every spawned agent."
#
# Override: prefix the command with `JNEXT_ALLOW_PUSH=1` (the user types this,
# or Claude does it after the user explicitly says "push", or for a push inside
# the standing authorization written in CLAUDE.md's push rule).

set -euo pipefail

input="$(cat)"
cmd="$(printf '%s' "$input" | jq -r '.tool_input.command // ""')"

# If the user/agent embedded the override, allow.
if printf '%s' "$cmd" | grep -qE '(^|[[:space:]])JNEXT_ALLOW_PUSH=1([[:space:]]|$)'; then
  exit 0
fi

# Match `git push`, `git -C <path> push`, and `gh pr create`.
if printf '%s' "$cmd" | grep -qE '(^|[[:space:];&|])(git([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+push|gh[[:space:]]+pr[[:space:]]+create)'; then
  cat >&2 <<'EOF'
[block-push hook] BLOCKED: git push / gh pr create requires explicit user authorization.

Per CLAUDE.md: "NEVER push to origin without explicit user authorization. This
applies to the manager AND every spawned agent. Local commits, rebases, and
merges on owned branches/worktrees are fine; git push, git push -u,
git push --force, gh pr create, and any equivalent are all forbidden unless
the user explicitly says 'push' or 'open a PR'."

Exception: the standing authorization in CLAUDE.md's push rule (after a green,
APPROVED merge + bump: push main and its new tags, <=3 tags, CI on main idle;
push epic branches, never force). PRs and anything else stay per-message.

If the user just said push/PR, or the push is inside that standing scope,
prefix the command with:
    JNEXT_ALLOW_PUSH=1 <your command>
EOF
  exit 2
fi

exit 0
