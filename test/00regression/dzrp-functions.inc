# Helpers shared by the dzrp-*-func rows (GH #12). Sourced by a row script after
# test-functions.inc, so $JNEXT, $SD_CARD_ARGS and $TMP_DIR are set. No trap:
# the harness owns the single EXIT handler, and every child here is reaped by
# dzrp_stop.
# shellcheck shell=bash

# dzrp_launch <log> [jnext args...] — start jnext headless (48K) in the
# BACKGROUND with `--dzrp-port 0`, and wait — bounded, 10 s — for the server's
# `dzrp: listening on 127.0.0.1:<port>` line, the bound port being the one the
# system chose. Sets DZRP_PID (the timeout wrapper's) and DZRP_PORT. Returns 1,
# DZRP_PORT empty, when the line never came or jnext died first.
#
# No --delayed-automatic-exit: a running headless machine burns frames at full
# speed, so a frame bound would end the run while the client is mid-scenario.
# The row ends it with dzrp_stop; the 120 s timeout is the hard bound behind
# that.
dzrp_launch() {
    local log=$1
    shift
    dzrp_launch_cmd "$log" "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" \
        --dzrp-port 0 "$@"
}

# dzrp_launch_cmd <log> <command...> — the same for any command line (the SDL
# and Qt frontends, with their environment in front: `env VAR=... bin args`).
# The command must include `--dzrp-port 0`.
dzrp_launch_cmd() {
    local log=$1 i
    shift
    DZRP_PORT=""
    LANG=C timeout --foreground --kill-after=5s 120s "$@" >"$log" 2>&1 &
    DZRP_PID=$!
    for i in $(seq 1 100); do
        DZRP_PORT=$(grep -aoE 'dzrp: listening on 127\.0\.0\.1:[0-9]+' "$log" 2>/dev/null |
                    head -1 | sed 's/.*://' || true)
        [[ -n "$DZRP_PORT" ]] && return 0
        kill -0 "$DZRP_PID" 2>/dev/null || return 1
        sleep 0.1
    done
    return 1
}

# dzrp_stop — end the jnext dzrp_launch started and reap it. `|| true` on both:
# `wait` reports the signal that ended it, which under `set -e` would abort the
# whole regression run.
dzrp_stop() {
    kill -TERM "$DZRP_PID" 2>/dev/null || true
    wait "$DZRP_PID" 2>/dev/null || true
}

# dzrp_peer <scenario> [args...] — run one scenario of dzrp-peer.py against the
# launched jnext, bounded. Sets DZRP_OUT (its last line) and returns its status.
dzrp_peer() {
    local rc=0
    DZRP_OUT=$(LANG=C timeout --foreground --kill-after=5s 90s \
        python3 "$SCRIPT_DIR/dzrp-peer.py" "$1" "$DZRP_PORT" "${@:2}" 2>&1) || rc=$?
    DZRP_OUT=$(tail -n 1 <<<"$DZRP_OUT")
    return "$rc"
}

# dzrp_row <row> <scenario> — the whole of a §7.2 row: launch, run the scenario,
# stop, report. The row's facts are the scenario's; see dzrp-peer.py.
dzrp_row() {
    local row=$1 scenario=$2 log="$TMP_DIR/$1.log"
    begin_func "$row"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the DZRP client)"
        return 0
    fi
    if ! dzrp_launch "$log"; then
        dzrp_stop
        fail_row " (no 'dzrp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
        return 0
    fi
    if dzrp_peer "$scenario"; then
        dzrp_stop
        pass_row " (${DZRP_OUT#PASS })"
    else
        dzrp_stop
        fail_row " (${DZRP_OUT#FAIL })"
    fi
}

# dzrp_client_row <row> <client.py> [args...] — the same shape for a DZRP client
# copied from dezogif_ng into test/dzrp/: it takes the server from DZRP_HOST /
# DZRP_PORT, prints its verdict last and exits 0 when it holds. An argument
# containing `@PORT@` gets the bound port there. With DZRP_EXPECT_LAST set, the
# client's last line must also be exactly that.
dzrp_client_row() {
    local row=$1 client=$2 log="$TMP_DIR/$1.log" rc=0 last a
    local -a args=()
    shift 2
    begin_func "$row"
    DZRP_CLIENT_OUT=""
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the DZRP client)"
        return 0
    fi
    if ! dzrp_launch "$log"; then
        dzrp_stop
        fail_row " (no 'dzrp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
        return 0
    fi
    for a in "$@"; do args+=("${a//@PORT@/$DZRP_PORT}"); done
    DZRP_CLIENT_OUT=$(DZRP_HOST=127.0.0.1 DZRP_PORT="$DZRP_PORT" LANG=C \
        timeout --foreground --kill-after=5s 120s \
        python3 "$PROJECT_DIR/test/dzrp/$client" "${args[@]}" 2>&1) || rc=$?
    dzrp_stop
    last=$(tail -n 1 <<<"$DZRP_CLIENT_OUT")
    if [[ $rc -eq 0 && ( -z "${DZRP_EXPECT_LAST:-}" || "$last" == "$DZRP_EXPECT_LAST" ) ]]; then
        pass_row " ($last)"
    else
        fail_row " (exit $rc: $(tr '\n' ' ' <<<"$DZRP_CLIENT_OUT" | cut -c1-600))"
    fi
}
