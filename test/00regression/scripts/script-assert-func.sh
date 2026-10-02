#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP4 (§6.3) — a failing `assert` in a headless run: the message is
# logged as `ASSERT FAILED`, the machine stops at the offending event, and the
# exit status is 3. A passing assert in the same script changes nothing.
if want script-assert-func; then
    begin_func script-assert-func
    cat > "$TMP_DIR/script-assert.jds" <<'JDS'
on frame 2 do assert FRAME == 2 "never" end
on frame 4 do assert FRAME == 99 "frame ${FRAME} is not 99" end
on frame 8 do exit 0 end
JDS
    out=$(LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --script "$TMP_DIR/script-assert.jds" \
        --delayed-automatic-exit-frames 50 2>&1) && rc=0 || rc=$?
    if [[ $rc -eq 3 ]] && grep -q "ASSERT FAILED: frame 4 is not 99" <<<"$out" &&
       ! grep -q "ASSERT FAILED: never" <<<"$out" && ! grep -q "SCRIPT EXIT 0" <<<"$out"; then
        pass_row " (exit 3, the failing assert's message logged, the run ended there)"
    else
        fail_row " (exit $rc: $(grep -E 'SCRIPT|ASSERT' <<<"$out" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
