#!/usr/bin/env bash
# Self-test for block-push.sh: every push invocation in a command must carry its
# own JNEXT_ALLOW_PUSH=1 prefix; the override elsewhere in the command does not
# cover an unprefixed push (observed defect: a compound command with one
# unprefixed and one prefixed push was allowed). Also pins: standalone-word
# detection (quotes, subshells, paths), global options, fail-closed behaviour.
#
# Run: bash .claude/hooks/block-push.test.sh

set -uo pipefail

HOOK="$(cd "$(dirname "$0")" && pwd)/block-push.sh"
pass=0 fail=0

report() {  # want got desc
  if [ "$2" = "$1" ]; then
    printf 'PASS  %s\n' "$3"; pass=$((pass+1))
  else
    printf 'FAIL  %s (want %s, got %s)\n' "$3" "$1" "$2"; fail=$((fail+1))
  fi
}

# want: 0 = allow, 2 = block
check() {
  local want="$1" desc="$2" cmd="$3" got
  printf '%s' "$(jq -nc --arg c "$cmd" '{tool_input:{command:$c}}')" \
    | bash "$HOOK" >/dev/null 2>&1
  report "$want" "$?" "$desc"
}

# Run the hook with PATH restricted to a scratch dir holding only the named tools.
check_path() {
  local want="$1" desc="$2" input="$3"; shift 3
  local d t got
  d="$(mktemp -d)"
  for t in "$@"; do ln -s "$(command -v "$t")" "$d/$t"; done
  printf '%s' "$input" | PATH="$d" "$(command -v bash)" "$HOOK" >/dev/null 2>&1
  got=$?
  rm -rf "$d"
  report "$want" "$got" "$desc"
}

P="git push"
O="JNEXT_ALLOW_PUSH=1"
NL=$'\n'
GOODJSON='{"tool_input":{"command":"git push origin B"}}'

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
check 2 "separator without space: make;push"  "make;$P origin B"
check 0 "no push at all"                      "git status"
check 0 "echo push (not a git subcommand)"    "echo push"
check 0 "git stash push (not matched)"        "git stash push -m x"
check 0 "word push in a path"                 "ls /tmp/git-push-notes"

# Prefix anchoring: the assignment must be exactly JNEXT_ALLOW_PUSH=1 as a word.
check 2 "prefixed name glued: NOT<override>"  "NOT$O $P origin B"
check 2 "override value 0"                    "JNEXT_ALLOW_PUSH=0 $P origin B"
check 2 "override value 10"                   "JNEXT_ALLOW_PUSH=10 $P origin B"
check 0 "subshell around prefixed push"       "($O $P origin B)"
check 2 "subshell unprefixed push"            "($P origin B)"

# Multi-line commands (R1): a newline ends the simple command.
check 2 "override, newline, push"             "$O ${NL}$P origin B"
check 2 "override, newline, push (no blank)"  "$O${NL}$P origin B"
check 2 "push then another line"              "$P origin B${NL}echo done"
check 2 "unprefixed second line"              "$O $P origin A${NL}$P origin B"
check 0 "every line prefixed"                 "$O $P origin A${NL}$O $P origin B"
check 2 "backslash continuation inside push"  "git \\${NL}push origin B"
check 0 "prefixed push, then plain line"      "$O $P origin B${NL}echo done"

# Standalone-word detection (R3): quotes, subshells, substitutions, paths.
check 2 "bash -c single-quoted"               "bash -c '$P origin B'"
check 2 "sh -c double-quoted"                 "sh -c \"$P origin B\""
check 2 "eval double-quoted"                  "eval \"$P origin B\""
check 2 "eval gh pr create"                   "eval 'gh pr create'"
check 2 "command substitution"                "echo \$($P origin B)"
check 2 "backticks"                           "echo \`$P origin B\`"
check 2 "absolute path /usr/bin/git"          "/usr/bin/git push origin B"
check 2 "backslash-escaped \\git"             "\\git push origin B"
check 2 "quoted \"git\" push"                 "\"git\" push origin B"
check 0 "prefix inside bash -c is honoured"   "bash -c '$O $P origin B'"
check 0 "prefixed absolute path"              "$O /usr/bin/git push origin B"
# Conservative by design: a harmless mention blocks too.
check 2 "mention after space in quotes"       "grep -n 'see git push docs' CLAUDE.md"
check 2 "mention glued to a quote"            "grep -n '$P' CLAUDE.md"
check 0 "dotted name .git is not git"         "ls .git push"
check 0 "word digit-git is not git"           "echo mygit push"

# Global options before the subcommand (R4).
check 2 "git --no-pager push"                 "git --no-pager push origin B"
check 2 "git -c k=v push"                     "git -c k=v push origin B"
check 2 "git -C \"path with space\" push"     "git -C \"/a b\" push origin B"
check 2 "git -C 'path with space' push"       "git -C '/a b' push origin B"
check 2 "git -C x -c a=b push"                "git -C /x -c a=b push origin B"
check 2 "git --git-dir=x push"                "git --git-dir=/x/.git push origin B"
check 2 "git --work-tree x push"              "git --work-tree /x push origin B"
check 0 "prefixed git --no-pager push"        "$O git --no-pager push origin B"
check 0 "git --no-pager log (not a push)"     "git --no-pager log -1"
check 2 "gh -R o/r pr create"                 "gh -R o/r pr create"
check 2 "gh --repo o/r pr create"             "gh --repo o/r pr create"
check 0 "prefixed gh -R pr create"            "$O gh -R o/r pr create"
check 0 "gh pr list (not create)"             "gh -R o/r pr list"

# Fail closed (R2): any failure path must exit 2, never 0/1/127/5.
check_path 2 "no perl"                        "$GOODJSON" jq cat
check_path 2 "no jq"                          "$GOODJSON" perl cat
check_path 2 "malformed JSON"                 'not json' jq perl cat
check_path 0 "tools present, benign allows"   '{"tool_input":{"command":"git status"}}' jq perl cat
d="$(mktemp -d)"
printf '#!/bin/sh\nexit 1\n' > "$d/perl"; chmod +x "$d/perl"
ln -s "$(command -v jq)" "$d/jq"; ln -s "$(command -v cat)" "$d/cat"
printf '%s' "$GOODJSON" | PATH="$d" "$(command -v bash)" "$HOOK" >/dev/null 2>&1
report 2 "$?" "perl exits 1"
printf '%s' '{"tool_input":{"command":"git status"}}' | PATH="$d" "$(command -v bash)" "$HOOK" >/dev/null 2>&1
report 2 "$?" "perl exits 1 on a benign command too"
rm -rf "$d"

printf '\nTotal: %d pass, %d fail\n' "$pass" "$fail"
[ "$fail" = 0 ]
