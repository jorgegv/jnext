#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP4 (§6.6) — --script-key FRAME N: the headless form of a host key.
# `on hostkey 3` runs at the end of frame 7 (FRAME == 7) and decides the exit;
# a key for a number no rule names changes nothing.
if want script-key-func; then
    begin_func script-key-func
    cat > "$TMP_DIR/script-key.jds" <<'JDS'
on hostkey 3 do
    log "KEY ${KEY} at frame ${FRAME}"
    exit 7
end
JDS
    out=$(LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --script "$TMP_DIR/script-key.jds" \
        --script-key 2 5 --script-key 7 3 --delayed-automatic-exit-frames 50 2>&1) && rc=0 || rc=$?
    if [[ $rc -eq 7 ]] && grep -q "KEY 3 at frame 7" <<<"$out" &&
       [[ $(grep -c "KEY " <<<"$out") -eq 1 ]]; then
        pass_row " (hostkey 3 at frame 7, exit 7; key 5 named by no rule changed nothing)"
    else
        fail_row " (exit $rc: $(grep -E 'KEY|SCRIPT' <<<"$out" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
