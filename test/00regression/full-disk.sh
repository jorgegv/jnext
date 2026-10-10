#!/usr/bin/env bash
# full-disk.sh <command> [args...] -- run <command> as if every regular file it
# writes lived on a full disk: the open succeeds, the write fails (EFBIG), with
# SIGXFSZ ignored so the failure is an error return and not a signal (GH #319).
#
# The portable twin of /dev/full for a host that has none (macOS): the rows
# rzx-record-status-func and rzx-reset-func record to a path that opens and
# fails on write. Run with bash by those rows (never sourced), so the `trap`
# here is not the row-script trap that lint-traps bans.
#
# The command's stdout and stderr go through a pipe: RLIMIT_FSIZE limits every
# write to a regular file, including the row's redirected log, and a pipe is not
# one. The pipe's reader (cat) is outside the limited subshell. Exit status is
# the command's.
set -uo pipefail
( ulimit -f 0; trap '' XFSZ; exec "$@" ) 2>&1 | cat
exit "${PIPESTATUS[0]}"
