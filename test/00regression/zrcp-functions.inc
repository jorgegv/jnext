# Helpers shared by the zrcp-*-func rows (GH #280). Sourced by a row script after
# test-functions.inc, so $JNEXT, $SD_CARD_ARGS and $TMP_DIR are set. No trap:
# the harness owns the single EXIT handler, and every child here is reaped by
# zrcp_stop.
# shellcheck shell=bash

# zrcp_launch_cmd <log> <command...> — start a jnext command line in the
# BACKGROUND and wait — bounded, 10 s — for the server's `zrcp: listening on
# 127.0.0.1:<port>` line, the bound port being the one the system chose. The
# command must include `--zrcp-port 0`. Sets ZRCP_PID (the timeout wrapper's)
# and ZRCP_PORT, and DZRP_PORT_ALSO when the command also serves DZRP. Returns
# 1, ZRCP_PORT empty, when the line never came or jnext died first.
#
# No --delayed-automatic-exit: a running headless machine burns frames at full
# speed, so a frame bound would end the run while the client is mid-scenario.
# The row ends it with zrcp_stop; the 120 s timeout is the hard bound behind
# that.
zrcp_launch_cmd() {
    local log=$1 i
    shift
    ZRCP_PORT=""
    DZRP_PORT_ALSO=""
    LANG=C timeout --foreground --kill-after=5s 120s "$@" >"$log" 2>&1 &
    ZRCP_PID=$!
    for i in $(seq 1 100); do
        ZRCP_PORT=$(grep -aoE 'zrcp: listening on 127\.0\.0\.1:[0-9]+' "$log" 2>/dev/null |
                    head -1 | sed 's/.*://' || true)
        DZRP_PORT_ALSO=$(grep -aoE 'dzrp: listening on 127\.0\.0\.1:[0-9]+' "$log" 2>/dev/null |
                         head -1 | sed 's/.*://' || true)
        [[ -n "$ZRCP_PORT" ]] && return 0
        kill -0 "$ZRCP_PID" 2>/dev/null || return 1
        sleep 0.1
    done
    return 1
}

# zrcp_stop — end the jnext zrcp_launch_cmd started and reap it. `|| true` on
# both: `wait` reports the signal that ended it, which under `set -e` would
# abort the whole regression run.
zrcp_stop() {
    kill -TERM "$ZRCP_PID" 2>/dev/null || true
    wait "$ZRCP_PID" 2>/dev/null || true
}

# zrcp_peer <scenario> [args...] — run one scenario of zrcp-peer.py against the
# launched jnext, bounded. Sets ZRCP_OUT (its last line) and returns its status.
zrcp_peer() {
    local rc=0
    ZRCP_OUT=$(LANG=C timeout --foreground --kill-after=5s 90s \
        python3 "$SCRIPT_DIR/zrcp-peer.py" "$1" "$ZRCP_PORT" "${@:2}" 2>&1) || rc=$?
    ZRCP_OUT=$(tail -n 1 <<<"$ZRCP_OUT")
    return "$rc"
}
