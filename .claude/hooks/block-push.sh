#!/usr/bin/env bash
# PreToolUse hook for Bash. Blocks `git push` and `gh pr create` unless
# each push invocation is prefixed with JNEXT_ALLOW_PUSH=1.
#
# CLAUDE.md mandate: "NEVER push to origin without explicit user authorization.
# This applies to the manager AND every spawned agent."
#
# Override: prefix EACH push invocation with `JNEXT_ALLOW_PUSH=1 ` (the user
# types this, or Claude does it after the user explicitly says "push", or for a
# push inside the standing authorization written in CLAUDE.md's push rule).
#
# Rules:
#  * Every `git ... push` / `gh ... pr create` in the command must be immediately
#    preceded by `JNEXT_ALLOW_PUSH=1<blanks>`; one unprefixed invocation blocks
#    the whole command. The override elsewhere (another command, a previous
#    line, after the push) covers nothing.
#  * `git` / `gh` are detected as standalone words, whatever precedes them
#    (quote, paren, backtick, path such as /usr/bin/git), so `bash -c '...'`,
#    `eval "..."`, `(...)` and `$(...)` are seen. A push inside them is allowed
#    only if it carries its own prefix (`bash -c 'JNEXT_ALLOW_PUSH=1 git push'`).
#  * Global options before the subcommand are skipped (`git --no-pager push`,
#    `git -c k=v push`, `git -C "a b" push`, `--git-dir/--work-tree/--namespace/--config-env/--super-prefix/--attr-source X`,
#    `gh -R o/r pr create`, `gh --repo o/r pr create`).
#  * Also gated: gh release write verbs (create/new/upload/edit/delete/
#    delete-asset); view/list/download stay allowed.
#  * Matching is textual and conservative: a harmless mention of the text
#    (a grep pattern, a commit message) blocks too. That is accepted.
#  * FAIL CLOSED: Claude Code blocks only on exit 2, so every failure path
#    (missing jq/perl, bad JSON, perl error) exits 2 with a message.
#
# Scope: this catches the ACCIDENTAL push, not deliberate obfuscation. EXAMPLES
# it does not see (not an exhaustive list): variable or alias indirection
# (`g=git; $g push`, git aliases), a push inside a script file, a push built by
# string concatenation, `gh api` writes.

set -euo pipefail

trap 'echo "[block-push hook] BLOCKED: internal error, failing closed (exit 2)." >&2; exit 2' ERR

for tool in jq perl; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "[block-push hook] BLOCKED: required tool '$tool' not found, failing closed." >&2
    exit 2
  fi
done

input="$(cat)"
cmd="$(printf '%s' "$input" | jq -r '.tool_input.command // ""')"

# Prints 1 if some push invocation lacks its prefix, 0 if all are prefixed.
unprefixed="$(printf '%s' "$cmd" | perl -0777 -ne '
  BEGIN { our $bad = 0; }
  our $bad;
  my $tok = q{(?:[^\s"\x27\\\\]|"[^"]*"|\x27[^\x27]*\x27|\\\\.)+};
  my $opt_git = q{(?:\s+(?:-[Cc]\s*} . $tok . q{|--(?:git-dir|work-tree|namespace|config-env|super-prefix|attr-source)\s+} . $tok . q{|-} . $tok . q{))*};
  my $opt_gh  = q{(?:\s+(?:-R\s+\S+|--repo\s+\S+|-\S+))*};
  s/\\\n/ /g;   # a backslash-newline continues the same simple command
  while (/(?<![\w.\/-])(?:[\w.\/-]*\/)?(?:git["\x27]?$opt_git\s+(?:subtree\s+)?["\x27]?push|gh["\x27]?$opt_gh\s+(?:pr\s+(?:create|new)|release\s+(?:create|new|upload|edit|delete|delete-asset)))/g) {
    my $pre = substr($_, 0, $-[0]);
    $bad = 1 unless $pre =~ /(?<![^\s;&|(`"\x27])JNEXT_ALLOW_PUSH=1[ \t]+\z/;
  }
  END { print $bad; }')"

if [ "$unprefixed" != "0" ]; then
  cat >&2 <<'EOF'
[block-push hook] BLOCKED: git push / gh pr create / gh release write requires explicit user authorization.

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
