#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP4 (§6.3, §6.5, §7.3) — the other exit statuses a headless script
# decides:
#   1. `stop` with no `exit` exits 3 and logs SCRIPT STOP with the reason;
#   2. a run-time error (division by zero) exits 1;
#   3. the watchdog (--delayed-automatic-exit-frames) firing before a verdict
#      the script declared exits 3 with `SCRIPT: N deferred actions never ran`;
#   4. a script with no verdict to reach lets the watchdog exit 0;
#   5. a script's `exit 0` never hides an earlier failure (a failed --load).
if want script-stop-func; then
    begin_func script-stop-func
    fails=()
    run_jds() {
        printf '%s\n' "$2" > "$TMP_DIR/script-stop-$1.jds"
        LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --script "$TMP_DIR/script-stop-$1.jds" \
            --delayed-automatic-exit-frames 30 2>&1
    }
    out=$(run_jds stop 'on frame 3 do stop "halt here" end') && rc=0 || rc=$?
    if [[ $rc -ne 3 ]] || ! grep -q "SCRIPT STOP: halt here at PC=" <<<"$out"; then
        fails+=("stop: exit $rc")
    fi
    out=$(run_jds div 'on frame 3 do log "${1 / 0}" end') && rc=0 || rc=$?
    if [[ $rc -ne 1 ]] || ! grep -q "SCRIPT ERROR .*script-stop-div.jds:1:" <<<"$out"; then
        fails+=("run-time error: exit $rc")
    fi
    out=$(run_jds late 'on frame 1000 do exit 0 end') && rc=0 || rc=$?
    if [[ $rc -ne 3 ]] || ! grep -q "SCRIPT: 1 deferred actions never ran" <<<"$out"; then
        fails+=("watchdog before the verdict: exit $rc")
    fi
    out=$(run_jds guard 'on write 0x0000..0x3FFF do stop "ROM write" end') && rc=0 || rc=$?
    if [[ $rc -ne 0 ]]; then
        fails+=("a guard that never trips: exit $rc")
    fi
    # 5. a script's `exit 0` never hides an earlier failure (a --load that failed)
    printf 'on frame 200 do exit 0 end\n' > "$TMP_DIR/script-stop-hide.jds"
    out=$(LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --load "$TMP_DIR/script-stop-missing.tap" \
        --script "$TMP_DIR/script-stop-hide.jds" --delayed-automatic-exit-frames 400 2>&1) && rc=0 || rc=$?
    if [[ $rc -ne 1 ]] || ! grep -q "script requested exit 0" <<<"$out"; then
        fails+=("exit 0 after a failed --load: exit $rc")
    fi
    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (stop = 3, run-time error = 1, verdict not reached = 3, nothing to reach = 0, exit 0 keeps an earlier 1)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
