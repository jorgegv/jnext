#!/usr/bin/env bash
#
# run-bounded.sh <seconds> <command...> - run a command under `timeout` with the signals delivered.
#
# `timeout` puts its child in a process group of its own, which a terminal's SIGINT never reaches, and bash
# defers a trap until the foreground child ends: Ctrl-C on a long bench was ignored until the run finished.
# So the command runs in the BACKGROUND and the script waits (bash runs a trap during `wait`); INT and TERM
# are forwarded to it, and the script exits 130 / 143 once it is gone. The command (the bench driver) kills
# its own browser process groups on that signal.
#
# Standalone script, run with bash (not sourced by a row runner), so it owns its trap.
set -uo pipefail
export LC_ALL=C LANG=C

secs=${1:?usage: run-bounded.sh <seconds> <command...>}
shift

child=""
forward() {
    [[ -n "$child" ]] && kill -TERM "$child" 2>/dev/null
    # give the command time to clean up; timeout's own --kill-after bounds it
    wait "$child" 2>/dev/null
}
trap 'forward; exit 130' INT
trap 'forward; exit 143' TERM

timeout --kill-after=30s "${secs}s" "$@" &
child=$!
wait "$child"
exit $?
