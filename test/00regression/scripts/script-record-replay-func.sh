#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #20 / GH #26 WP6 — record, emit, replay, end to end through the binary.
# A headless 48K session types into BASIC (--delayed-keypress-frames) and takes
# two captures with --script-key FRAME 8, recorded by --record-script; the
# script is written when jnext exits. Replayed WITHOUT the typing options, it
# must reach its `exit 0` (both compare_scr equal). The control: the same
# replay with one byte of the second capture changed exits 3 on it.
if want script-record-replay-func; then
    begin_func script-record-replay-func
    dir="$TMP_DIR/record-replay"
    mkdir -p "$dir/bad"
    rec=$(cd "$dir" && LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --record-script rr.jds \
        --delayed-keypress-frames 100 1 --delayed-keypress-frames 115 2 --delayed-keypress-frames 130 3 \
        --script-key 125 8 --script-key 160 8 --delayed-automatic-exit-frames 200 2>&1) && rrc=0 || rrc=$?
    edges=$(grep -cE '^on frame [0-9]+ do (press|release) ' "$dir/rr.jds" 2>/dev/null) || edges=0
    caps=$(grep -c 'compare_scr "rr-000' "$dir/rr.jds" 2>/dev/null) || caps=0
    rp=$(cd "$dir" && LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --script rr.jds --delayed-automatic-exit-frames 300 2>&1) && prc=0 || prc=$?
    cp "$dir/rr.jds" "$dir/rr-0001.scr" "$dir/bad/" 2>/dev/null || true
    if [[ -s "$dir/rr-0002.scr" ]]; then
        # Flip the byte at offset 2048 (the middle third of the bitmap).
        python3 -c 'import sys; d=bytearray(open(sys.argv[1],"rb").read()); d[2048]^=0xFF; open(sys.argv[2],"wb").write(d)' \
            "$dir/rr-0002.scr" "$dir/bad/rr-0002.scr"
    fi
    bad=$(cd "$dir/bad" && LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --script rr.jds --delayed-automatic-exit-frames 300 2>&1) && brc=0 || brc=$?
    if [[ $rrc -eq 0 && $edges -eq 6 && $caps -eq 2 && $prc -eq 0 && $brc -eq 3 ]] &&
       grep -q "SCRIPT EXIT 0" <<<"$rp" && grep -q "ASSERT FAILED: capture 2 at FRAME" <<<"$bad"; then
        pass_row " (recorded 6 edges + 2 .scr; replay exit 0; a changed capture exits 3)"
    else
        fail_row " (record $rrc edges=$edges caps=$caps; replay $prc; control $brc: $(grep -E 'RECORD|SCRIPT|ASSERT' <<<"$rec$rp$bad" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
