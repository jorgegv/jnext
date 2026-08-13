#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #251 — the joystick-connector serial source's CLI contract, against the REAL
# binary.
#
# The unit rows (uart_integration_test JOY-01..08) prove the Emulator honours
# EmulatorConfig::joy_uart_rx_* and that the NR 0x0B mux gates both directions.
# What no unit binary can reach is main.cpp's parse loop, and that is exactly
# where a feature like this dies quietly: the ESP's own history in this suite
# records that deleting the single line copying a parsed flag into EmulatorConfig
# left the ENTIRE triplet green while the flag was silently ignored.
#
# Six facts:
#
#   1. --joy-uart-rx reaches the emulator: the startup posture line names the
#      file and its byte count. A plain run emits no such line, so fact 1 is not
#      satisfiable by unconditional logging.
#   2. --joy-uart-connector reaches it: the same line names joy 2 by default and
#      joy 1 when asked. The DEFAULT half is what makes the override half mean
#      something.
#   3. --joy-uart-rx-delay-frames reaches it, named on the same line.
#   4. THE STREAM IS ACTUALLY CLOCKED. A run whose guest never sets NR 0x0B must
#      end with the "exhausted, all bytes LOST" warning — which can only appear
#      if the source was ticked, paced to exhaustion, and gated by the sink. This
#      is the one assertion here that exercises the whole path rather than the
#      parse.
#   5. ...and its converse: a delay longer than the run means nothing was sent,
#      so the warning must NOT appear. Without this pair, fact 4 is satisfiable
#      by a build that warns unconditionally.
#   6. The refusals. Every one of them is a run that would otherwise complete
#      having transmitted nothing, reporting a green pass for a path no byte took.
if want joy-uart-func; then
    begin_func joy-uart-func
    fails=()

    src="$TMP_DIR/joy-uart-src.bin"
    printf 'ABC' > "$src"          # 3 bytes; $TMP_DIR is removed by the harness
    empty="$TMP_DIR/joy-uart-empty.bin"
    : > "$empty"

    joy_run() {
        timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 3 "$@" 2>&1 || true
    }
    # The one info line setup_joy_uart_source() emits, and the warning
    # service_joy_uart_source() emits when the whole stream is lost.
    banner='joystick serial source attached to joy'
    lost='joystick serial source exhausted with all'

    # Fact 1 — and its control.
    if joy_run | grep -q "$banner"; then
        fails+=("a source was attached with no --joy-uart-rx flag")
    fi
    if ! joy_run --joy-uart-rx "$src" | grep -q "$banner"; then
        fails+=("--joy-uart-rx did not reach the emulator")
    fi
    if ! joy_run --joy-uart-rx "$src" | grep -q "3 byte(s) from"; then
        fails+=("the attached source's byte count did not reach the emulator")
    fi

    # Fact 2 — default joy 2, overridden to joy 1.
    if ! joy_run --joy-uart-rx "$src" | grep -q "$banner 2 "; then
        fails+=("the default --joy-uart-connector is not joy 2")
    fi
    if ! joy_run --joy-uart-rx "$src" --joy-uart-connector 1 | grep -q "$banner 1 "; then
        fails+=("--joy-uart-connector 1 did not reach the emulator")
    fi

    # Fact 3 — the delay, named on the posture line.
    if ! joy_run --joy-uart-rx "$src" | grep -q 'starting after 0 frame(s)'; then
        fails+=("the default --joy-uart-rx-delay-frames is not 0")
    fi
    if ! joy_run --joy-uart-rx "$src" --joy-uart-rx-delay-frames 7 \
        | grep -q 'starting after 7 frame(s)'; then
        fails+=("--joy-uart-rx-delay-frames did not reach the emulator")
    fi

    # Fact 4 — the stream really is clocked out and really is dropped. The boot
    # firmware never puts NR 0x0B into a UART mode, so all 3 bytes are lost.
    if ! joy_run --joy-uart-rx "$src" | grep -q "$lost 3 byte(s) LOST"; then
        fails+=("an unheard stream did not report its bytes as lost")
    fi

    # Fact 5 — held past the end of the run, nothing is sent, so nothing is lost.
    if joy_run --joy-uart-rx "$src" --joy-uart-rx-delay-frames 10000 \
        | grep -q "$lost"; then
        fails+=("a stream still inside its start delay reported bytes as lost")
    fi

    # Fact 6 — the refusals. Both the exit status AND the message are asserted:
    # exiting 1 with some other complaint would satisfy the status alone.
    joy_refuse() {
        local why=$1 want=$2; shift 2
        local out rc=0
        out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 "$@" 2>&1) || rc=$?
        if [[ $rc -eq 0 ]]; then
            fails+=("$why was accepted instead of refused")
        elif ! grep -q "$want" <<<"$out"; then
            fails+=("$why was refused for the wrong reason")
        fi
    }
    joy_refuse "a missing source file" 'joy-uart-rx' \
        --joy-uart-rx "$TMP_DIR/joy-uart-does-not-exist.bin"
    # An empty file is the silent no-op in its purest form: a source that sends
    # nothing can only ever produce a run that looks like a pass.
    joy_refuse "an empty source file" 'file is empty' --joy-uart-rx "$empty"
    joy_refuse "a connector that is not 1 or 2" 'must be 1 or 2' \
        --joy-uart-rx "$src" --joy-uart-connector 3
    joy_refuse "a non-numeric delay" 'non-negative' \
        --joy-uart-rx "$src" --joy-uart-rx-delay-frames 3frames
    joy_refuse "a negative delay" 'non-negative' \
        --joy-uart-rx "$src" --joy-uart-rx-delay-frames -1
    # And the two flags that configure a cable nobody attached.
    joy_refuse "a connector without a source" 'require a serial source' \
        --joy-uart-connector 2
    joy_refuse "a delay without a source" 'require a serial source' \
        --joy-uart-rx-delay-frames 5

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (--joy-uart-rx attach + byte count, --joy-uart-connector default/override, --joy-uart-rx-delay-frames default/override, unheard-stream warning and its held-stream converse, 7 refusals verified)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
