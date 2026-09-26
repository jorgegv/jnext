#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --delayed-snapshot (headless-only): save/reload proof plus the same
# "requested but never written" loud-failure contract the --delayed-screenshot
# tests use (screenshot-pending-func).
if want snapshot-save-func; then
    begin_func snapshot-save-func
    szx="$TMP_DIR/snap.szx"
    orig_png="$TMP_DIR/snap-orig.png"
    reloaded_png="$TMP_DIR/snap-reloaded.png"
    rm -f "$szx" "$orig_png" "$reloaded_png"

    # Positive control: boot 48K to the BASIC copyright screen, capture it
    # AND save a .szx at the same frame (150) in the same run, so
    # snap-orig.png is a screenshot of the exact state snap.szx captured.
    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$orig_png" --delayed-screenshot-frames 150 \
            --delayed-snapshot "$szx" --delayed-snapshot-frames 150 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then save_rc=0; else save_rc=1; fi

    # Reload proof: a FRESH process loads the saved file and renders a
    # frame. This must be PIXEL content-verified, not just "a PNG came
    # out" — a structurally-valid .szx with garbage RAM payloads still
    # loads and renders. 0 pixel diff vs snap-orig.png is required
    # (BASIC's copyright screen is static, so the reload must reproduce
    # it exactly).
    if [[ -s "$szx" ]] && timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$szx" \
            --delayed-screenshot "$reloaded_png" --delayed-screenshot-frames 1 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then reload_rc=0; else reload_rc=1; fi

    content_ok=0
    diff_pixels=-1
    if [[ -s "$orig_png" ]] && [[ -s "$reloaded_png" ]]; then
        if $HAS_COMPARE; then
            diff_pixels=$(png_diff "$reloaded_png" "$orig_png")
            [[ "$diff_pixels" -eq 0 ]] && content_ok=1
        else
            # No ImageMagick: cannot content-verify. Do NOT silently pass —
            # that would advertise coverage that does not exist.
            content_ok=-1
        fi
    fi

    # Negative control: a snapshot requested but never due before auto-exit
    # fires must be a loud non-zero-exit failure, never a silent no-op.
    pending="$TMP_DIR/snap-pending.szx"
    rm -f "$pending"
    if out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-snapshot "$pending" --delayed-snapshot-frames 5000 \
                --delayed-automatic-exit 1 2>&1)
    then pend_rc=0; else pend_rc=1; fi

    # Negative control: .szx is scoped to 48K/128K/+2A/+3 only — see the
    # SzxSaver class doc-comment SCOPE. jnext's DEFAULT --machine is Next,
    # so this is the common path in practice, not an edge case: it must
    # fail loudly (non-zero exit, clear reason logged, no file written),
    # never silently write a truncated/misrepresenting snapshot.
    refused="$TMP_DIR/snap-refused.szx"
    rm -f "$refused"
    if out_next=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-snapshot "$refused" --delayed-snapshot-frames 5 \
                --delayed-automatic-exit 3 2>&1)
    then refuse_rc=0; else refuse_rc=1; fi

    # Negative control, GH #274: `.sna` is the 48K form only, so it cannot
    # represent a Next either — the extra RAM, the NextREGs and every Next
    # video layer are simply not in the format. It used to be written anyway:
    # 49179 bytes, exit 0, "saved 48K snapshot" in the log, and a user who
    # asked for a snapshot of a Next got a file that quietly was not one. Same
    # loud-failure contract as .szx above.
    refused_sna="$TMP_DIR/snap-refused.sna"
    rm -f "$refused_sna"
    if out_next_sna=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-snapshot "$refused_sna" --delayed-snapshot-frames 5 \
                --delayed-automatic-exit 3 2>&1)
    then refuse_sna_rc=0; else refuse_sna_rc=1; fi

    # Positive control for that SAME boundary: a refusal that is too wide is as
    # wrong as one that is too narrow, so a machine `.sna` CAN represent must
    # still save — and in the form that machine needs. A 48K writes the 48K
    # form (49179 bytes); a 128K writes the 128K form (131103), which is the
    # OTHER half of GH #274: jnext used to write 49179 there too, dropping five
    # banks and the paging register without saying so.
    ok_sna="$TMP_DIR/snap-ok.sna"
    ok_128="$TMP_DIR/snap-ok-128.sna"
    rm -f "$ok_sna" "$ok_128"
    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-snapshot "$ok_sna" --delayed-snapshot-frames 5 \
            --delayed-automatic-exit 3 >/dev/null 2>&1
    then ok_sna_rc=0; else ok_sna_rc=1; fi
    ok_sna_size=$([[ -f "$ok_sna" ]] && stat -c%s "$ok_sna" || echo -1)

    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 128k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-snapshot "$ok_128" --delayed-snapshot-frames 5 \
            --delayed-automatic-exit 3 >/dev/null 2>&1
    then ok_128_rc=0; else ok_128_rc=1; fi
    ok_128_size=$([[ -f "$ok_128" ]] && stat -c%s "$ok_128" || echo -1)

    # The conditional refusal, end to end: a +3 in SPECIAL PAGING is a machine
    # the format cannot describe at all (its three blocks are DEFINED as banks
    # 5, 2 and the bank paged at 0xC000), so it must fail as loudly as the Next
    # does. The injected program is DI; LD BC,1FFD; LD A,1; OUT (C),A; JR $ —
    # in special config 0 bank 2 stays at 0x8000, so it keeps running.
    p3_special="$TMP_DIR/snap-plus3-special.sna"
    rm -f "$p3_special"
    printf '\xf3\x01\xfd\x1f\x3e\x01\xed\x79\x18\xfe' > "$TMP_DIR/snap-special.bin"
    if out_p3=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine plus3 \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --inject "$TMP_DIR/snap-special.bin" --inject-delay 20 \
                --delayed-snapshot "$p3_special" --delayed-snapshot-frames 40 \
                --delayed-automatic-exit 3 2>&1)
    then p3_rc=0; else p3_rc=1; fi

    if [[ "$content_ok" -eq -1 ]]; then
        skip_row " (no ImageMagick — cannot content-verify the reload)"
    elif [[ "$save_rc" -eq 0 ]] && [[ -s "$szx" ]] \
       && [[ "$reload_rc" -eq 0 ]] && [[ -s "$reloaded_png" ]] \
       && [[ "$content_ok" -eq 1 ]] \
       && [[ "$pend_rc" -ne 0 ]] && [[ ! -f "$pending" ]] \
       && echo "$out" | grep -q "NO snapshot was written" \
       && [[ "$refuse_rc" -ne 0 ]] && [[ ! -f "$refused" ]] \
       && echo "$out_next" | grep -qi "cannot represent this machine" \
       && [[ "$refuse_sna_rc" -ne 0 ]] && [[ ! -f "$refused_sna" ]] \
       && echo "$out_next_sna" | grep -qi "cannot represent a ZX Spectrum Next" \
       && [[ "$ok_sna_rc" -eq 0 ]] && [[ "$ok_sna_size" -eq 49179 ]] \
       && [[ "$ok_128_rc" -eq 0 ]] && [[ "$ok_128_size" -eq 131103 ]] \
       && [[ "$p3_rc" -ne 0 ]] && [[ ! -f "$p3_special" ]] \
       && echo "$out_p3" | grep -qi "SPECIAL PAGING"; then
        pass_row " (reload pixel-identical to pre-save screen; pending-never-written: error+exit!=0, no file; --machine next refused for BOTH .szx and .sna: error+exit!=0, no file; .sna writes 49179 on 48K and 131103 on 128K; a +3 in special paging refused: error+exit!=0, no file)"
    else
        fail_row " (save_rc=$save_rc szx_exists=$([[ -s "$szx" ]] && echo y || echo n) reload_rc=$reload_rc png_exists=$([[ -s "$reloaded_png" ]] && echo y || echo n) content_ok=$content_ok diff_pixels=$diff_pixels pend_rc=$pend_rc pending_exists=$([[ -f "$pending" ]] && echo y || echo n) refuse_rc=$refuse_rc refused_exists=$([[ -f "$refused" ]] && echo y || echo n) refuse_sna_rc=$refuse_sna_rc refused_sna_exists=$([[ -f "$refused_sna" ]] && echo y || echo n) ok_sna_rc=$ok_sna_rc ok_sna_size=$ok_sna_size ok_128_rc=$ok_128_rc ok_128_size=$ok_128_size p3_rc=$p3_rc p3_exists=$([[ -f "$p3_special" ]] && echo y || echo n))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
