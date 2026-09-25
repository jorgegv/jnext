#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #154 — AT+CIPDOMAIN end to end, and the address policy with it.
#
# WHY THIS ROW EXISTS WHEN THE UNIT SUITES ALREADY COVER THE COMMAND. They
# cover the ENGINE and the RESOLVER; only a run of the real binary covers the
# WIRING. That distinction is not theoretical: the first version of this
# feature passed all 744 unit rows while the product did nothing at all,
# because `ThreadedEsp`'s worker calls `advance_transports()` and
# `service_transports()` directly rather than `AtEngine::poll()`, so a service
# hook added to `poll()` never ran for any threaded consumer — which is every
# real one. This row is what found that.
#
# IT NEEDS NO NETWORK. All three names are IP LITERALS, which take the
# synchronous fast path, so there is no DNS server, no peer and nothing to
# race. The ADDRESS POLICY still applies to a literal exactly as it does to a
# resolved name, which is the whole point: the policy is the thing being
# proved, not the lookup.
#
# THE STRONGEST ASSERTION HERE IS A NEGATIVE: neither denied address may appear
# anywhere in what the guest was told. A resolver without the policy would have
# answered `+CIPDOMAIN:127.0.0.1` quite happily, and that is a way to read back
# exactly the addresses `AddressPolicy` exists to keep away from the guest.
if want esp-cipdomain-func; then
    begin_func esp-cipdomain-func

    guest_py="$SCRIPT_DIR/esp-wifi-setup-guest.py"
    guest_bin="$TMP_DIR/esp_cipdomain_guest.bin"
    run_log="$TMP_DIR/esp_cipdomain_run.log"
    rm -f "$guest_bin" "$run_log"

    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to build the guest binary)"
    else
        python3 "$guest_py" "$guest_bin" cipdomain

        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" \
            --esp \
            --magic-port 0xCAFE --magic-port-mode line \
            --inject "$guest_bin" --inject-org 8000 --inject-pc 8000 \
            --inject-delay 100 --delayed-automatic-exit-frames 1200 \
            >"$run_log" 2>&1 || true

        # Every spdlog line starts with its `[timestamp]`; the magic port writes
        # with a bare fprintf. So the non-bracketed lines ARE the wire.
        mapfile -t wire < <(grep -av '^\[' "$run_log" || true)
        joined=$(printf '%s\n' "${wire[@]}")
        dump=$(printf '%s|' "${wire[@]:0:20}")
        [[ "${#wire[@]}" -gt 20 ]] && dump+="...(${#wire[@]} lines total)"

        fails=()

        # A denominator first, so every check below has something real to run
        # against rather than passing vacuously on an empty wire.
        if [[ "${#wire[@]}" -ne 6 ]]; then
            fails+=("wire has ${#wire[@]} lines, expected 6: $dump")
        fi

        # 1. The ALLOWED literal is answered, unquoted per the 1.x manual.
        #    A herestring, never `printf | grep -q`: under pipefail that form
        #    reports a match as a miss (harness-selftest HS-30).
        grep -qx '+CIPDOMAIN:192.168.100.238' <<<"$joined" \
            || fails+=("no '+CIPDOMAIN:192.168.100.238' for the RFC1918 literal: $dump")

        # 2. Both DENIED literals are refused, with the documented failure pair.
        if [[ "$(grep -cx 'DNS Fail' <<<"$joined" || true)" -ne 2 ]]; then
            fails+=("expected exactly 2 'DNS Fail' lines: $dump")
        fi

        # 3. THE NON-DISCLOSURE PROPERTY. Neither denied address may appear
        #    anywhere the guest can see it.
        for denied in '127.0.0.1' '169.254.169.254'; do
            if grep -qF "$denied" <<<"$joined"; then
                fails+=("the DENIED address $denied reached the guest: $dump")
            fi
        done

        # 4. ...while the OPERATOR is told exactly which control fired. The two
        #    are deliberately asymmetric: the guest learns nothing, the person
        #    running jnext learns everything.
        grep -aq "REFUSED by address policy: loopback" "$run_log" \
            || fails+=("no operator log line naming the loopback refusal")
        grep -aq "REFUSED by address policy: cloud metadata" "$run_log" \
            || fails+=("no operator log line naming the cloud-metadata refusal")

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (RFC1918 literal answered; loopback and cloud-metadata refused, neither address disclosed)"
        else
            fail_row " (${fails[*]})"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
