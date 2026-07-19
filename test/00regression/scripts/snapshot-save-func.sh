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

    if [[ "$content_ok" -eq -1 ]]; then
        skip_row " (no ImageMagick — cannot content-verify the reload)"
    elif [[ "$save_rc" -eq 0 ]] && [[ -s "$szx" ]] \
       && [[ "$reload_rc" -eq 0 ]] && [[ -s "$reloaded_png" ]] \
       && [[ "$content_ok" -eq 1 ]] \
       && [[ "$pend_rc" -ne 0 ]] && [[ ! -f "$pending" ]] \
       && echo "$out" | grep -q "NO snapshot was written" \
       && [[ "$refuse_rc" -ne 0 ]] && [[ ! -f "$refused" ]] \
       && echo "$out_next" | grep -qi "cannot represent this machine"; then
        pass_row " (reload pixel-identical to pre-save screen; pending-never-written: error+exit!=0, no file; --machine next refused: error+exit!=0, no file)"
    else
        fail_row " (save_rc=$save_rc szx_exists=$([[ -s "$szx" ]] && echo y || echo n) reload_rc=$reload_rc png_exists=$([[ -s "$reloaded_png" ]] && echo y || echo n) content_ok=$content_ok diff_pixels=$diff_pixels pend_rc=$pend_rc pending_exists=$([[ -f "$pending" ]] && echo y || echo n) refuse_rc=$refuse_rc refused_exists=$([[ -f "$refused" ]] && echo y || echo n))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
