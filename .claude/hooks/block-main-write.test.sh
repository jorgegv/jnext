#!/usr/bin/env bash
# Self-test for block-main-write.sh. The guard sat inert for 11 days because
# `git -C` skipped it entirely and nothing checked; this pins the behaviour.
#
# Run: bash .claude/hooks/block-main-write.test.sh

set -uo pipefail

HOOK="$(dirname "$0")/block-main-write.sh"
# The PRIMARY checkout (the one the test needs to be on `main`), derived
# rather than hardcoded to one machine (GH #204). `--git-common-dir` is the
# shared .git of the whole worktree set, so this resolves to the primary
# checkout even when the test is run from a worktree. `$0`-relative would
# resolve to the WORKTREE, which is on a feature branch, so the hook would
# correctly decline to block and all nine "must block" cases would FAIL —
# measured: 8 pass, 9 fail. Loud, but a fault in the test, not in the hook.
REPO="$(cd "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
WT="$(git -C "$REPO" worktree list --porcelain | awk '/^worktree /{print $2}' | sed -n 2p)"

pass=0 fail=0

# want: 0 = allow, 2 = block
check() {
  local want="$1" desc="$2" cmd="$3" cwd="${4:-$REPO}"
  local got
  printf '%s' "$(jq -nc --arg c "$cmd" --arg d "$cwd" \
    '{tool_input:{command:$c},cwd:$d}')" | bash "$HOOK" >/dev/null 2>&1
  got=$?
  if [ "$got" = "$want" ]; then
    printf 'PASS  %s\n' "$desc"; pass=$((pass+1))
  else
    printf 'FAIL  %s (want %s, got %s)\n' "$desc" "$want" "$got"; fail=$((fail+1))
  fi
}

if [ -z "$WT" ]; then
  echo "SKIP: no linked worktree to test against; create one first" >&2
  exit 0
fi

check 2 "bare commit from repo root"          "git commit -m x"
check 2 "git -C <repo> commit"                "git -C $REPO commit -m x"
check 2 "cd <repo> && commit"                 "cd $REPO && git commit -m x"
check 2 "merge into main"                     "git -C $REPO merge feat/x"
check 2 "reset --hard on main"                "git -C $REPO reset --hard HEAD~1"
check 0 "git -C <worktree> commit"            "git -C $WT commit -m x"
check 0 "cd <worktree> && commit"             "cd $WT && git commit -m x"
check 0 "read-only op on main"                "git -C $REPO log --oneline -1"
check 0 "status on main"                      "git -C $REPO status"
check 0 "override honoured"                   "JNEXT_ALLOW_MAIN_WRITE=1 git -C $REPO commit -m x"
check 0 "mixed: read main, write worktree"    "git -C $REPO log && git -C $WT commit -m x"
check 2 "mixed: read worktree, write main"    "git -C $WT log && git -C $REPO commit -m x"
check 0 "unresolvable target fails open"      "git -C /nonexistent-path commit -m x"
check 2 "commit piped to tail"                "git -C $REPO commit -m x | tail -3"
# `-C .` is relative: it must resolve against the tracked dir, not the session cwd.
check 0 "cd <worktree> && git -C . commit"    "cd $WT && git -C . commit -m x"
check 2 "cd <repo> && git -C . commit"        "cd $REPO && git -C . commit -m x"
check 2 "bare git -C . commit from repo cwd"  "git -C . commit -m x"

printf '\nTotal: %d pass, %d fail\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
