#!/usr/bin/env bash
# Property test: the guard hooks are never LOOSER than the original ones.
#
# Corpus = every command that block-main-write.test.sh and block-amend.test.sh
# feed their hook (harvested through CORPUS_OUT), malformed inputs included. Each
# is replayed, with the same cwd, against the ORIGINAL hook (the copy at commit
# $BASE) and the current one. Whenever the original blocked (rc 2) the current
# one must block too, unless the case is one of the reviewed exceptions below.
#
# An exception is a deliberate false-positive fix, not a hole: the original
# matched the raw TEXT, the current hooks only match where the shell would run a
# git command. Every exception is listed with its reason; one that no longer
# triggers is itself a failure, so the list cannot go stale.
#
# Run: bash .claude/hooks/never-looser.test.sh

set -uo pipefail

BASE=5ee5a8f7f
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d "${TMPDIR:-$HOME/tmp}/never-looser.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# "<hook>|<case description>" -> reason
declare -A EXC=(
)
declare -A USED=()

git -C "$HERE" cat-file -e "$BASE:.claude/hooks/block-main-write.sh" 2>/dev/null \
  || { echo "FAIL: baseline $BASE not found in this repository" >&2; exit 1; }
for h in block-main-write block-amend; do
  git -C "$HERE" show "$BASE:.claude/hooks/$h.sh" >"$WORK/old-$h.sh"
done

pass=0 fail=0 skipped=0
for pair in "main-write:block-main-write" "amend:block-amend"; do
  name="${pair%%:*}" hook="${pair##*:}"
  corpus="$WORK/$name.jsonl"
  : >"$corpus"
  CORPUS_OUT="$corpus" bash "$HERE/$hook.test.sh" >/dev/null 2>&1 \
    || { echo "FAIL: $hook.test.sh failed while harvesting its corpus" >&2; exit 1; }
  [ -s "$corpus" ] || { echo "FAIL: empty corpus for $hook" >&2; exit 1; }
  while IFS= read -r line; do
    desc="$(jq -r .desc <<<"$line")" cmd="$(jq -r .cmd <<<"$line")" cwd="$(jq -r .cwd <<<"$line")"
    if [ -n "$cwd" ]; then json="$(jq -nc --arg c "$cmd" --arg d "$cwd" '{tool_input:{command:$c},cwd:$d}')"
    else json="$(jq -nc --arg c "$cmd" '{tool_input:{command:$c}}')"; fi
    bash "$WORK/old-$hook.sh" <<<"$json" >/dev/null 2>&1; old=$?
    bash "$HERE/$hook.sh" <<<"$json" >/dev/null 2>&1; new=$?
    if [ "$old" = 2 ] && [ "$new" != 2 ]; then
      key="$name|$desc"
      if [ -n "${EXC[$key]:-}" ]; then
        USED[$key]=1; skipped=$((skipped+1))
        printf 'EXCEPTION  %s  (%s)\n' "$key" "${EXC[$key]}"
      else
        printf 'FAIL  LOOSER THAN THE ORIGINAL: %s  cmd=%q\n' "$key" "$cmd"; fail=$((fail+1))
      fi
    else
      pass=$((pass+1))
    fi
  done <"$corpus"
done

for key in "${!EXC[@]}"; do
  [ -n "${USED[$key]:-}" ] || { printf 'FAIL  stale exception (no longer needed): %s\n' "$key"; fail=$((fail+1)); }
done

printf '\nTotal: %d not looser, %d reviewed exceptions, %d fail\n' "$pass" "$skipped" "$fail"
[ "$fail" -eq 0 ]
