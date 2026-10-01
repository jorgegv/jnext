#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# QtApp performs a GUEST hard reset raised inside a tick's frames BEFORE that
# tick's backend pump (GH #278 WP2 — the obligation package B3 left to Q;
# CAP-CTL-12's ordering paragraph). So a guest reset and a client `reset(Hard)`
# in one tick run in that order, and the client's reboots the freshly booted
# machine.
#
# QtApp used to poll the hard-reset request in pre_frames(), i.e. in the NEXT
# tick: a client `reset(Hard)` in this tick's pump then destroyed the machine
# with the guest's request still pending on it, and the guest reset never
# happened. That order is what this row turns red.
#
# THE OBSERVABLE is JNEXT_HOST_PROBE=order (src/platform/host_probe.h): a
# `Frame` handler raises the guest request (the flag NR 0x02 bit 1 and F1 set)
# from inside the frames; in the next pump the probe logs how many Reset{Hard}
# pushes the guest boot has produced so far and issues its own reset(Hard);
# ten frames later it logs the total. Poll-before-pump reads
# "guest-before-client=1 client=ok" then "resets=2"; pump-before-poll reads
# "guest-before-client=0" and "resets=1".
#
# QT_QPA_PLATFORM=offscreen, as qt-host-probe-func: no X server needed.
if want qt-host-order-func; then
    begin_func qt-host-order-func

    log="$TMP_DIR/qt-host-order.log"
    rm -f "$log"

    JNEXT_HOST_PROBE=order QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
    timeout --foreground --kill-after=5s 120s \
        "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --machine 48k --rewind-buffer-size 0 \
        --delayed-automatic-exit-frames 150 >"$log" 2>&1 || true

    armed=$(grep -c "HOSTPROBE armed (client [0-9]*, order)" "$log" || true)
    first=$(grep -c "HOSTPROBE order: guest-before-client=1 client=ok" "$log" || true)
    total=$(grep -c "HOSTPROBE order: resets=2$" "$log" || true)
    seen=$(grep -o "HOSTPROBE [^|]*" "$log" | tr '\n' '|' || true)

    if [[ "$armed" -eq 1 && "$first" -eq 1 && "$total" -eq 1 ]]; then
        pass_row " (guest hard reset performed before the pump; the client's reset(Hard) followed it)"
    else
        fail_row " (armed=$armed guest_first=$first two_resets=$total: $seen)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
