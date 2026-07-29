#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #25 — the emulated ESP-01's CLI contract, asserted against the REAL binary.
#
# The unit suites prove the Emulator honours EmulatorConfig::esp_enabled. What
# they cannot reach is main.cpp's parse loop, which is where "default off"
# actually lives for a user. Four facts, all of them security-relevant:
#
#   1. a plain run does NOT bring the ESP up (the default is the whole posture);
#   2. --esp does;
#   3. --no-esp wins when it comes last, so the run can always be forced off —
#      the escape hatch that makes the saved GUI preference safe to have;
#   4. --esp-allow without an enabled ESP is a refusal, not a silent no-op that
#      would leave the user believing a restriction is in force;
#   5. a comma-separated HOST is a refusal for the same reason. The config
#      file's own allowed_hosts IS comma-separated, so this is the mistake the
#      product invites; taken literally it stores one unmatchable name, refuses
#      every connection, and displays what looks like a correct two-host
#      allowlist.
if want esp-cli-func; then
    begin_func esp-cli-func
    fails=()

    esp_run() {
        timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 "$@" 2>&1 || true
    }
    # The one info line setup_esp() emits when it brings the module up.
    banner='ESP-01 enabled on UART 0'

    if esp_run | grep -q "$banner"; then
        fails+=("the ESP came up with no --esp flag")
    fi
    if ! esp_run --esp | grep -q "$banner"; then
        fails+=("--esp did not bring the ESP up")
    fi
    if esp_run --esp --no-esp | grep -q "$banner"; then
        fails+=("--no-esp did not override an earlier --esp")
    fi

    # This one must FAIL the run, so its exit status is the assertion.
    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 \
        --esp-allow example.test >/dev/null 2>&1; then
        fails+=("--esp-allow without --esp was accepted instead of refused")
    fi

    # Also a usage error, and --esp is present so the refusal can only be about
    # the comma. Both the exit status AND the message are asserted: exiting 1
    # with some other complaint would satisfy the status alone.
    comma_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 \
        --esp --esp-allow a.example,b.example 2>&1) && comma_ok=0 || comma_ok=1
    if [[ $comma_ok -eq 0 ]]; then
        fails+=("--esp-allow accepted a comma-separated HOST")
    elif ! grep -q "commas are not separators" <<<"$comma_out"; then
        fails+=("--esp-allow rejected a comma-separated HOST for the wrong reason")
    fi

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (ESP default-off, --esp, --no-esp, --esp-allow gating and comma refusal verified)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
