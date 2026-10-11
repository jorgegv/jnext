#!/usr/bin/env bash
# Self-test for block-push.sh: every push invocation in a command must carry its
# own JNEXT_ALLOW_PUSH=1 prefix; the override elsewhere in the command does not
# cover an unprefixed push (observed defect: a compound command with one
# unprefixed and one prefixed push was allowed).
#
# Run: bash .claude/hooks/block-push.test.sh

set -uo pipefail

HOOK="$(dirname "$0")/block-push.sh"
pass=0 fail=0

# want: 0 = allow, 2 = block
check() {
  local want="$1" desc="$2" cmd="$3" got
  printf '%s' "$(jq -nc --arg c "$cmd" '{tool_input:{command:$c}}')" \
    | bash "$HOOK" >/dev/null 2>&1
  got=$?
  if [ "$got" = "$want" ]; then
    printf 'PASS  %s\n' "$desc"; pass=$((pass+1))
  else
    printf 'FAIL  %s (want %s, got %s)\n' "$desc" "$want" "$got"; fail=$((fail+1))
  fi
}

P="git push"
O="JNEXT_ALLOW_PUSH=1"

check 2 "plain push"                          "$P origin B"
check 0 "prefixed push"                       "$O $P origin B"
check 2 "compound: unprefixed then prefixed"  "cd /x && $P origin B >/dev/null 2>&1; $O git -C /x push origin B"
check 2 "compound: prefixed then unprefixed"  "$O $P origin B; $P origin B"
check 0 "two prefixed pushes"                 "$O $P origin A && $O $P origin B"
check 0 "two prefixed, -C and plain"          "$O git -C /x push origin A; $O $P origin B"
check 2 "git -C path push unprefixed"         "git -C /x push origin B"
check 0 "git -C path push prefixed"           "$O git -C /x push origin B"
check 2 "gh pr create unprefixed"             "gh pr create --title t"
check 0 "gh pr create prefixed"               "$O gh pr create --title t"
check 2 "prefix on a different command"       "$O make foo && $P origin B"
check 2 "override after the push"             "$P origin B && $O true"
check 2 "override as unrelated argument"      "echo $O; $P origin B"
check 2 "pipe: unprefixed push"               "true | $P origin B"
check 0 "no push at all"                      "git status"
check 0 "echo push (not a git subcommand)"    "echo push"
check 0 "git stash push (not matched)"        "git stash push -m x"
check 0 "word push in a path"                 "ls /tmp/git-push-notes"
# Matching breadth is unchanged from the old hook: `git push` must follow
# start/whitespace/;&|. A mention after a space inside quotes therefore blocks
# (conservative); one glued to a quote character is not matched (as before).
check 2 "mention after space in quotes blocks (conservative)" "grep -n 'see git push docs' CLAUDE.md"
check 0 "mention glued to a quote is not matched (as before)" "grep -n 'git push' CLAUDE.md"

printf '\nTotal: %d pass, %d fail\n' "$pass" "$fail"
[ "$fail" = 0 ]
