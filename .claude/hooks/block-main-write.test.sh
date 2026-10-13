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
  # never-looser.test.sh harvests every case through CORPUS_OUT.
  if [ -n "${CORPUS_OUT:-}" ]; then jq -nc --arg d "$desc" --arg c "$cmd" --arg w "$cwd" '{desc:$d,cmd:$c,cwd:$w}' >>"$CORPUS_OUT"; fi
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

# Git GLOBAL OPTIONS before the subcommand must not hide it. Each family: blocked
# against main, allowed against the linked worktree.
while IFS= read -r opts; do
  [ -n "$opts" ] || continue
  check 2 "main:     git $opts commit"   "git -C $REPO $opts commit -m x"
  check 0 "worktree: git $opts commit"   "git -C $WT $opts commit -m x"
  check 2 "main:     git $opts -C last"  "git $opts -C $REPO commit -m x"
  check 0 "main:     git $opts log"      "git -C $REPO $opts log --oneline -1"
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
check 2 "merge behind -c"                          "git -c a=b -C $REPO merge feat/x"
check 2 "rebase behind --no-pager"                 "git --no-pager -C $REPO rebase main"
check 2 "cherry-pick behind -P"                    "git -P -C $REPO cherry-pick abc"
check 2 "revert behind -c"                         "git -c a=b -C $REPO revert abc"
check 2 "am behind -c"                             "git -c a=b -C $REPO am x.patch"
check 0 "override honoured behind -c"              "JNEXT_ALLOW_MAIN_WRITE=1 git -c a=b -C $REPO commit -m x"
check 2 "-c with bare commit, cwd main"            "git -c core.hooksPath=/dev/null commit -m x"
check 2 "--no-pager with bare commit, cwd main"    "git --no-pager commit -m x"
# --git-dir / --work-tree / GIT_DIR / GIT_WORK_TREE are not resolved: a write that uses one is blocked
check 2 "--git-dir X commit"                       "git --git-dir $REPO/.git commit -m x" "$WT"
check 2 "--git-dir=X commit"                       "git --git-dir=$REPO/.git commit -m x" "$WT"
check 2 "--work-tree X commit"                     "git --work-tree $WT commit -m x"
check 2 "--work-tree=X commit"                     "git --work-tree=$WT commit -m x"
check 2 "GIT_DIR=X prefix"                         "GIT_DIR=$REPO/.git git commit -m x" "$WT"
check 2 "GIT_WORK_TREE=X prefix"                   "GIT_WORK_TREE=$WT git commit -m x" "$WT"
check 0 "--git-dir X on a read-only op"            "git --git-dir $REPO/.git log --oneline -1" "$WT"
check 0 "override covers --git-dir"                "JNEXT_ALLOW_MAIN_WRITE=1 git --git-dir $REPO/.git commit -m x" "$WT"
# pull and reset (owner, 2026-10-13)
check 2 "pull"                                     "git -C $REPO pull"
check 2 "pull --rebase behind options"             "git --no-pager -c a=b -C $REPO pull --rebase origin main"
check 0 "pull in a worktree"                       "git -C $WT pull --ff-only"
check 0 "pull override honoured"                   "JNEXT_ALLOW_MAIN_WRITE=1 git -C $REPO pull"
check 2 "reset (no args)"                          "git -C $REPO reset"
check 2 "reset HEAD~1"                             "git -C $REPO reset HEAD~1"
check 2 "reset --soft HEAD~1"                      "git -C $REPO reset --soft HEAD~1"
check 2 "reset --mixed HEAD~1"                     "git -C $REPO reset --mixed HEAD~1"
check 2 "reset --merge"                            "git -C $REPO reset --merge"
check 2 "reset --keep HEAD~1"                      "git -C $REPO reset --keep HEAD~1"
check 2 "reset --hard \"\$sha\""                    "git -C $REPO reset --hard \"\$sha\""
check 2 "reset a path word without --"             "git -C $REPO reset README.md"
check 2 "reset behind -c"                          "git -c a=b -C $REPO reset --soft HEAD~1"
check 0 "reset -- <paths>"                         "git -C $REPO reset -- README.md"
check 0 "reset HEAD -- <paths>"                    "git -C $REPO reset HEAD -- README.md src/main.cpp"
check 0 "reset <rev> -- <paths> behind -c"         "git -c a=b -C $REPO reset HEAD~1 -- README.md"
check 0 "reset in a worktree"                      "git -C $WT reset --soft HEAD~1"
check 0 "stash stays allowed"                      "git -C $REPO stash"
check 0 "stash pop stays allowed"                  "git -C $REPO stash pop"
# backslash-newline continuation
check 2 "continued line hides -c and commit"       "git -C $REPO \\
  -c a=b \\
  commit -m x"
check 2 "continued line, commit on the next line"  "git -C $REPO \\
  commit -m x"
check 2 "subshell"                                 "(git -C $REPO -c a=b commit -m x)"
check 2 "bash -c wrapped"                          "bash -c 'git -C $REPO -c a=b commit -m x'"

printf '\nTotal: %d pass, %d fail\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
