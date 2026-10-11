#!/usr/bin/env bash
# PreToolUse hook for Bash. Blocks `git push` and `gh pr create` unless
# each push invocation is prefixed with JNEXT_ALLOW_PUSH=1.
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

# Match `git push`, `git -C <path> push`, and `gh pr create`. EVERY such
# invocation must itself be immediately preceded by `JNEXT_ALLOW_PUSH=1 ` (an
# env-assignment prefix of that simple command); the override anywhere else in
# the command (another command, an earlier push) does not cover it. Matching is
# textual and conservative: a quoted mention of `git push` is treated as a push.
unprefixed="$(printf '%s' "$cmd" | perl -0777 -ne '
  my $bad = 0;
  while (/(?:^|[\s;&|])(git(?:\s+-C\s+\S+)?\s+push|gh\s+pr\s+create)/g) {
    my $pre = substr($_, 0, $-[1]);
    $bad = 1 unless $pre =~ /(?:^|[\s;&|(])JNEXT_ALLOW_PUSH=1[ \t]+$/;
  }
  print $bad;')"
if [ "$unprefixed" = "1" ]; then
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
