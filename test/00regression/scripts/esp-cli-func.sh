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
#      would leave the user believing a restriction is in force.
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

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (ESP default-off, --esp, --no-esp and --esp-allow gating verified)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
