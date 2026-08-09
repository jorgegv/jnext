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

    # ── GH #246: the reported station address ────────────────────────────
    #
    # Same seam as fact 8 above: the startup line names the address the module
    # will report, so a default that names 192.168.1.50 and an override that
    # names the requested value together prove the flag reached the emulator.
    ip_banner='ESP-01 reports station address'

    if ! esp_run --esp | grep -q "$ip_banner 192.168.1.50 "; then
        fails+=("the default reported station address is not 192.168.1.50")
    fi
    if ! esp_run --esp --esp-ip-address 10.0.0.42 | grep -q "$ip_banner 10.0.0.42 "; then
        fails+=("--esp-ip-address did not reach the emulator")
    fi

    ip_refuse() {
        local why=$1; shift
        local out rc=0
        out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 "$@" 2>&1) || rc=$?
        if [[ $rc -eq 0 ]]; then
            fails+=("$why was accepted instead of refused")
        elif ! grep -q 'esp-ip-address' <<<"$out"; then
            fails+=("$why was refused for the wrong reason")
        fi
    }
    ip_refuse "an address without --esp" --esp-ip-address 10.0.0.42
    ip_refuse "a host NAME as the reported address" --esp --esp-ip-address localhost
    ip_refuse "a malformed reported address" --esp --esp-ip-address 999.1.1.1
    # 0.0.0.0 is what the module reports while it is NOT associated, so
    # accepting it as the configured address would make the two states
    # indistinguishable to the guest.
    ip_refuse "0.0.0.0 as the reported address" --esp --esp-ip-address 0.0.0.0

    # ── GH #246: the scheduled WiFi outage ───────────────────────────────
    #
    # Same seam, same reason: the two frame numbers are parsed in main.cpp and
    # copied into EmulatorConfig, and nothing linked into a unit binary can see
    # that copy. What a real run emits at each edge is the assertion, and the
    # ORDER matters — a build that fired both edges on the same frame, or fired
    # the second one first, would satisfy a pair of independent greps.
    # The trailing --delayed-automatic-exit-frames overrides the 2 that esp_run
    # passes (last occurrence wins), because a run that stopped at frame 2
    # would never reach the second edge.
    outage=$(esp_run --esp --esp-delayed-disassociate-frames 1 \
                          --esp-delayed-associate-frames 2 \
                          --delayed-automatic-exit-frames 6)
    # `|| true` on both: the whole point of these two is that the line may be
    # ABSENT, and under `set -euo pipefail` a grep that matches nothing would
    # kill the row instead of failing it — which reports as a harness fault,
    # not as the defect it is. (Found by mutation-testing this row.)
    lost_line=$(grep -n 'association LOST at frame 1' <<<"$outage" | head -1 | cut -d: -f1) || true
    back_line=$(grep -n 'association REGAINED at frame 2' <<<"$outage" | head -1 | cut -d: -f1) || true
    if [[ -z "$lost_line" ]]; then
        fails+=("--esp-delayed-disassociate-frames did not reach the emulator")
    elif [[ -z "$back_line" ]]; then
        fails+=("--esp-delayed-associate-frames did not reach the emulator")
    elif (( back_line <= lost_line )); then
        fails+=("the association was regained before it was lost")
    elif [[ $(grep -c 'association LOST' <<<"$outage") -ne 1 ]]; then
        # Each edge fires ONCE. A schedule tested with `>=` instead of `==`
        # reaches the same end state and would pass every assertion above while
        # re-applying itself on every frame for the rest of the run.
        fails+=("the disassociate edge fired more than once")
    fi
    # And the run that asks for nothing must produce neither edge, or the two
    # assertions above prove only that jnext logs unconditionally.
    if esp_run --esp | grep -qE 'association (LOST|REGAINED)'; then
        fails+=("an unscheduled run reported an association change")
    fi

    # The four refusals. Each is a schedule that would run to completion and
    # report a green pass for a path it never took.
    # Both the exit status AND the message are asserted, as everywhere else in
    # this row: exiting 1 with some other complaint would satisfy the status
    # alone.
    esp_refuse() {
        local why=$1; shift
        local out rc=0
        out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 "$@" 2>&1) || rc=$?
        if [[ $rc -eq 0 ]]; then
            fails+=("$why was accepted instead of refused")
        elif ! grep -q 'esp-delayed' <<<"$out"; then
            fails+=("$why was refused for the wrong reason")
        fi
    }
    esp_refuse "a scheduled outage without --esp" --esp-delayed-disassociate-frames 1
    esp_refuse "an associate with no disassociate" --esp --esp-delayed-associate-frames 1
    esp_refuse "an associate that does not come after the disassociate" \
        --esp --esp-delayed-disassociate-frames 5 --esp-delayed-associate-frames 5
    esp_refuse "a non-numeric frame number" \
        --esp --esp-delayed-disassociate-frames 10s

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (ESP default-off, --esp, --no-esp, --esp-allow gating and comma refusal, --esp-listen-address default/widened/malformed/name/gate, --esp-ip-address default/override + 4 refusals, scheduled WiFi outage edges + 4 refusals verified)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
