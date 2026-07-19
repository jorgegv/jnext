#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Qt-frontend half of the unwritable-path contract — see screenshot-io-func.sh.

if want screenshot-io-qt-func; then
    begin_func screenshot-io-qt-func
    bad_png="$TMP_DIR/no-such-dir-qt/io.png"  # parent directory does not exist
    png_ok="$TMP_DIR/io-qt-ok.png"
    rm -rf "$TMP_DIR/no-such-dir-qt"; rm -f "$png_ok"

    # 120 s timeout, not the 60 s used elsewhere: a Qt-offscreen boot to frame 60
    # takes ~28 s on an idle box, and the whole point of this test is that a
    # non-zero exit means "the PNG failed to write" — a timeout kill would be
    # indistinguishable from the very failure being asserted. Give it headroom.
    if out=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-screenshot "$bad_png" --delayed-screenshot-frames 60 \
                --delayed-automatic-exit 5 2>&1)
    then qio_rc=0; else qio_rc=1; fi

    if QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$png_ok" --delayed-screenshot-frames 60 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then qioctrl_rc=0; else qioctrl_rc=1; fi

    if [[ "$qio_rc" -ne 0 ]] && [[ ! -f "$bad_png" ]] \
       && echo "$out" | grep -q "FAILED to write" \
       && echo "$out" | grep -q "No such file or directory" \
       && [[ "$qioctrl_rc" -eq 0 ]] && [[ -s "$png_ok" ]]; then
        pass_row " (Qt unwritable path: error+reason, exit!=0, no PNG; control writes one)"
    else
        fail_row " (qt_io_rc=$qio_rc png_exists=$([[ -f "$bad_png" ]] && echo y || echo n) control_rc=$qioctrl_rc)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
