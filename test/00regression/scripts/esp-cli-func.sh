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
#
# GH #210 ADDS THE INBOUND FLAG, and it is here for the reason the four above
# are: main.cpp's parse loop is not linked into any test binary, so NOTHING a
# unit suite can do reaches it. That is not hypothetical — review found that
# deleting the single line which copies the parsed --esp-listen-address into
# EmulatorConfig left the ENTIRE triplet green (unit 6742, FUSE 1356,
# regression 122) while the flag was silently ignored and jnext went on binding
# loopback. A security control whose CLI wiring nothing asserts is a security
# control that can be deleted by accident.
#
#   6. a value that is not a numeric address is refused, with the documented
#      reason, and a non-zero exit;
#   7. --esp-listen-address without --esp is refused, exactly as --esp-allow is
#      — an address configured for a module that is off reads as "I have said
#      where it listens" when nothing will listen;
#   8. the value REACHES the emulator: the startup posture line names what was
#      asked for, and the default names 127.0.0.1. This pair is what fails when
#      the copy line goes missing.
#
# Fact 8 asserts the value arrives; that the LISTENER then binds it, and that
# binding it is observably different from the default, is esp-server-lan-func's
# job — it dials this host's real LAN address and gets in.
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

    # ── GH #210: --esp-listen-address ────────────────────────────────────
    #
    # The posture line setup_esp() emits for the INBOUND direction. Asserting
    # the address it names is what proves the parsed flag reached
    # EmulatorConfig; asserting the DEFAULT names loopback is what makes the
    # first assertion meaningful rather than a tautology (a build that ignored
    # the flag would satisfy neither).
    bind_banner='ESP-01 server mode binds'

    if ! esp_run --esp | grep -q "$bind_banner 127.0.0.1 "; then
        fails+=("the default bind address is not 127.0.0.1")
    fi
    if ! esp_run --esp --esp-listen-address 0.0.0.0 | grep -q "$bind_banner 0.0.0.0 "; then
        fails+=("--esp-listen-address 0.0.0.0 did not reach the emulator")
    fi

    # A usage error, and --esp is present so the refusal can only be about the
    # value. Both the exit status AND the message are asserted: exiting 1 with
    # some other complaint would satisfy the status alone.
    addr_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 \
        --esp --esp-listen-address not-an-address 2>&1) && addr_ok=0 || addr_ok=1
    if [[ $addr_ok -eq 0 ]]; then
        fails+=("--esp-listen-address accepted a value that is not an address")
    elif ! grep -q "must be a numeric IP address" <<<"$addr_out"; then
        fails+=("--esp-listen-address rejected a bad value for the wrong reason")
    fi

    # A NAME is refused even though it would resolve: a bind address that could
    # depend on DNS is one that could change under the user.
    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 \
        --esp --esp-listen-address localhost >/dev/null 2>&1; then
        fails+=("--esp-listen-address accepted a host NAME instead of an address")
    fi

    # Mirrors the --esp-allow gate above, and for the same reason.
    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 \
        --esp-listen-address 0.0.0.0 >/dev/null 2>&1; then
        fails+=("--esp-listen-address without --esp was accepted instead of refused")
    fi

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (ESP default-off, --esp, --no-esp, --esp-allow gating and comma refusal, --esp-listen-address default/widened/malformed/name/gate verified)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
