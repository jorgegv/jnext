#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #18 — the .SCR screenshot format, end to end through a real boot.
#
# The oracle is NOT this project's own screenshot code. A 48K .SNA is a
# 27-byte header followed by bank 5, bank 2, bank 0 verbatim (src/core/
# sna_saver.cpp), so sna[27 .. 27+6912] IS the ULA screen memory, produced by
# a completely separate code path (SnaSaver reading through the Mmu) from the
# one under test (Ula::screen_dump reading the ULA's own bank-5 storage). Both
# are captured at the SAME frame of the SAME run, so they must agree byte for
# byte. A dump from the wrong bank, the wrong offset or the wrong length fails
# here even though it would still be 6912 plausible-looking bytes.
#
# The run also asserts the screen is not blank, because "all zeros" would match
# a broken dump against a broken snapshot: the 48K boot screen carries the
# copyright line, so a real capture has non-zero pixel bytes and 768 attribute
# bytes of 0x38 (black ink on white paper).
if want scr-screenshot-func; then
    begin_func scr-screenshot-func
    scr="$TMP_DIR/boot48.scr"
    sna="$TMP_DIR/boot48.sna"
    png="$TMP_DIR/boot48.png"
    qtscr="$TMP_DIR/boot48-qt.scr"
    rm -f "$scr" "$sna" "$png" "$qtscr"

    # Headless: .scr and .sna at the same frame.
    if timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$scr" --delayed-screenshot-frames 100 \
            --delayed-snapshot "$sna" --delayed-snapshot-frames 100 \
            --delayed-automatic-exit 10 >/dev/null 2>&1
    then scr_rc=0; else scr_rc=1; fi

    # Same command, .png target: the extension is the ONLY difference, so this
    # is the control that proves the dispatch did not simply break PNG.
    if timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$png" --delayed-screenshot-frames 100 \
            --delayed-automatic-exit 10 >/dev/null 2>&1
    then png_rc=0; else png_rc=1; fi

    # --delayed-screenshot-layers has nothing to select in a .SCR, so it is
    # refused rather than ignored. The delays are deliberately a combination
    # that would SUCCEED if the refusal were removed — capture at frame 10,
    # exit at frame 60 — so a build that merely ignored the option would exit
    # 0 and leave a file behind, and all three of the checks below would flip
    # rather than just the message one.
    if out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-screenshot "$TMP_DIR/never.scr" \
                --delayed-screenshot-frames 10 \
                --delayed-screenshot-layers layer2 \
                --delayed-automatic-exit-frames 60 2>&1)
    then layers_rc=0; else layers_rc=1; fi

    # The Qt frontend writes a .scr through the same dispatcher. 120 s for the
    # same reason screenshot-io-qt-func gives: an offscreen Qt boot to frame 60
    # is slow, and a timeout kill must not be mistaken for the failure.
    if QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
            --machine 48k "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$qtscr" --delayed-screenshot-frames 60 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then qt_rc=0; else qt_rc=1; fi

    scr_size=$(stat -c %s "$scr" 2>/dev/null || echo 0)
    qt_size=$(stat -c %s "$qtscr" 2>/dev/null || echo 0)

    # Byte comparison + non-blank check, in one pass over both files.
    cmp_out=$(python3 - "$scr" "$sna" <<'PY'
import sys
scr = open(sys.argv[1], 'rb').read()
sna = open(sys.argv[2], 'rb').read()
ref = sna[27:27 + 6912]
nonzero = sum(1 for b in scr[:6144] if b)
attrs = set(scr[6144:6912])
print("match=%d nonzero=%d attrs=%s" % (scr == ref, nonzero, sorted(attrs)))
PY
) || cmp_out="match=0 nonzero=0 attrs=[]"

    png_magic=$(head -c 4 "$png" 2>/dev/null | od -An -tx1 | tr -d ' \n' || true)

    if [[ "$scr_rc" -eq 0 ]] && [[ "$scr_size" -eq 6912 ]] \
       && [[ "$cmp_out" == "match=1 nonzero="* ]] \
       && [[ "$cmp_out" != *"nonzero=0 "* ]] \
       && [[ "$cmp_out" == *"attrs=[56]" ]] \
       && [[ "$png_rc" -eq 0 ]] && [[ "$png_magic" == "89504e47" ]] \
       && [[ "$layers_rc" -ne 0 ]] \
       && echo "$out" | grep -q "cannot be used with a .scr screenshot" \
       && [[ ! -f "$TMP_DIR/never.scr" ]] \
       && [[ "$qt_rc" -eq 0 ]] && [[ "$qt_size" -eq 6912 ]]; then
        pass_row " (.scr == SNA bank-5 screen, 6912 B; .png still PNG; layers refused; Qt writes .scr)"
    else
        fail_row " (scr_rc=$scr_rc scr_size=$scr_size $cmp_out png_rc=$png_rc png_magic=$png_magic layers_rc=$layers_rc qt_rc=$qt_rc qt_size=$qt_size)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
