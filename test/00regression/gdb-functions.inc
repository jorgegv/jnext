# Helpers shared by the gdb-*-func rows (GH #281). Sourced by a row script after
# test-functions.inc, so $JNEXT, $SD_CARD_ARGS and $TMP_DIR are set. No trap:
# the harness owns the single EXIT handler, and every child here is reaped by
# gdb_stop.
# shellcheck shell=bash

# gdb_launch_cmd <log> <command...> — start the command in the BACKGROUND and
# wait — bounded, 10 s — for the server's `gdb: listening on 127.0.0.1:<port>`
# line, the bound port being the one the system chose. The command must include
# `--gdb-port 0`. Sets GDB_PID (the timeout wrapper's) and GDB_PORT, and
# DZRP_PORT too when the log also has a `dzrp: listening on` line. Returns 1,
# GDB_PORT empty, when the line never came or the command died first.
#
# No --delayed-automatic-exit: a running headless machine burns frames at full
# speed, so a frame bound would end the run while the client is mid-scenario.
# The row ends it with gdb_stop; the 120 s timeout is the hard bound behind it.
gdb_launch_cmd() {
    local log=$1 i
    shift
    GDB_PORT=""
    DZRP_PORT=""
    LANG=C timeout --foreground --kill-after=5s 120s "$@" >"$log" 2>&1 &
    GDB_PID=$!
    for i in $(seq 1 100); do
        GDB_PORT=$(grep -aoE 'gdb: listening on 127\.0\.0\.1:[0-9]+' "$log" 2>/dev/null |
                   head -1 | sed 's/.*://' || true)
        if [[ -n "$GDB_PORT" ]]; then
            DZRP_PORT=$(grep -aoE 'dzrp: listening on 127\.0\.0\.1:[0-9]+' "$log" 2>/dev/null |
                        head -1 | sed 's/.*://' || true)
            return 0
        fi
        kill -0 "$GDB_PID" 2>/dev/null || return 1
        sleep 0.1
    done
    return 1
}

# gdb_stop — end the jnext gdb_launch_cmd started and reap it. `|| true` on
# both: `wait` reports the signal that ended it, which under `set -e` would abort
# the whole regression run.
gdb_stop() {
    kill -TERM "$GDB_PID" 2>/dev/null || true
    wait "$GDB_PID" 2>/dev/null || true
}

# gdb_peer <scenario> [args...] — run one scenario of gdb-peer.py, bounded.
# Sets GDB_OUT (its last line) and returns its status.
gdb_peer() {
    local rc=0
    GDB_OUT=$(LANG=C timeout --foreground --kill-after=5s 60s \
        python3 "$SCRIPT_DIR/gdb-peer.py" "$@" 2>&1) || rc=$?
    GDB_OUT=$(tail -n 1 <<<"$GDB_OUT")
    return "$rc"
}
