#!/usr/bin/env bash
# Self-test for block-amend.sh: `git commit --amend` must be caught behind any
# git global option, not only `-C <path>`.
#
# Run: bash .claude/hooks/block-amend.test.sh

set -uo pipefail

HOOK="$(dirname "$0")/block-amend.sh"
pass=0 fail=0

# want: 0 = allow, 2 = block
check() {
  local want="$1" desc="$2" cmd="$3"
  local got
  printf '%s' "$(jq -nc --arg c "$cmd" '{tool_input:{command:$c}}')" | bash "$HOOK" >/dev/null 2>&1
  got=$?
  # never-looser.test.sh harvests every case through CORPUS_OUT.
  if [ -n "${CORPUS_OUT:-}" ]; then jq -nc --arg d "$desc" --arg c "$cmd" '{desc:$d,cmd:$c,cwd:""}' >>"$CORPUS_OUT"; fi
  if [ "$got" = "$want" ]; then
    printf 'PASS  %s\n' "$desc"; pass=$((pass+1))
  else
    printf 'FAIL  %s (want %s, got %s)\n' "$desc" "$want" "$got"; fail=$((fail+1))
  fi
}

check 2 "bare amend"                       "git commit --amend"
check 2 "amend with message"               "git commit --amend -m x"
check 2 "amend after -m"                   "git commit -m x --amend"
check 2 "amend --no-edit"                  "git commit --amend --no-edit"
check 2 "git -C <path> amend"              "git -C /some/path commit --amend"
check 2 "amend after &&"                   "git add -A && git commit --amend --no-edit"
check 2 "quoted ; before --amend"          "git commit -m \"a; b\" --amend"
check 0 "plain commit"                     "git commit -m x"
check 0 "amend text in a non-commit"       "git log --amend"
check 0 "override honoured"                "JNEXT_ALLOW_AMEND=1 git commit --amend"
check 0 "override honoured behind -c"      "JNEXT_ALLOW_AMEND=1 git -c a=b commit --amend"

while IFS= read -r opts; do
  [ -n "$opts" ] || continue
  check 2 "git $opts commit --amend"       "git $opts commit --amend"
  check 2 "git -C p $opts commit --amend"  "git -C /p $opts commit --amend"
  check 0 "git $opts commit (no amend)"    "git $opts commit -m x"
done <<'OPTS'
-c core.hooksPath=/dev/null
-c a=b -c c=d
--no-pager
-P
-p
--paginate
--bare
--no-replace-objects
--no-lazy-fetch
--literal-pathspecs
--glob-pathspecs
--noglob-pathspecs
--icase-pathspecs
--no-optional-locks
--no-advice
--exec-path=/usr/libexec/git-core
--list-cmds=main
--git-dir /p/.git
--git-dir=/p/.git
--work-tree /p
--work-tree=/p
--namespace ns
--namespace=ns
--config-env=a.b=HOME
--config-env a.b=HOME
--super-prefix sp
--attr-source HEAD
--attr-source=HEAD
--shallow-file /dev/null
--no-pager -c a=b --no-optional-locks
OPTS

check 2 "env GIT_DIR prefix"               "GIT_DIR=/p/.git git -c a=b commit --amend"
check 2 "bash -c wrapped"                  "bash -c 'git -c a=b commit --amend'"
check 2 "subshell"                         "(git --no-pager commit --amend)"
check 2 "continued line"                   "git -c a=b commit \\
  --amend --no-edit"
check 2 "continued line before commit"     "git -c a=b \\
  commit --amend"

printf '\nTotal: %d pass, %d fail\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
