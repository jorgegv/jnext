#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Sprite collision/overtime (port 0x303B) under render-skip.
# Collision (bit 0) and line-budget overtime (bit 1) are computed
# inside the sprite render path (sprites.vhd:971-995); a skipped frame must
# still produce them (Emulator::run_frame calls
# Renderer::run_sprite_side_effects on skipped frames). This row proves it:
# the SAME poller program runs headless (never skips) and in the Qt GUI at
# --speed 400 (skips most composites), and the two magic-port status
# sequences must be IDENTICAL, with the control actually observing
# collisions (else the row proves nothing).
#
# Poller: test/00regression/bin/sprite_collision_poll.bin (64 bytes,
# org/pc 0x8000, hand-assembled):
#   di ; nextreg 0x15,0x01                      ; sprites visible
#   ld bc,0x303B ; xor a ; out (c),a            ; select slot 0
#   ld bc,0x0057                                ; attribute upload port
#   2x [ x=100, y=100, byte2=0, byte3=0x80 ]    ; two overlapping sprites,
#                                               ; pattern 0 (all-zero bytes,
#                                               ; != transparent 0xE3)
#   loop: ld hl,0x2000 ; dly: dec hl; ld a,h; or l; jr nz,dly   (~213k T)
#         ld bc,0x303B ; in a,(c)               ; read status (clears flags)
#         ld bc,0x1234 ; out (c),a              ; magic port
#         jr loop
if want sprite-collision-turbo-func; then
    begin_func sprite-collision-turbo-func
    poll_bin="$SCRIPT_DIR/bin/sprite_collision_poll.bin"
    scp_ctrl_log="$TMP_DIR/scp-ctrl.log"
    scp_turbo_log="$TMP_DIR/scp-turbo.log"
    rm -f "$scp_ctrl_log" "$scp_turbo_log"
    SCP_ARGS=(--rewind-buffer-size 0
              --inject "$poll_bin" --inject-org 0x8000 --inject-pc 0x8000
              --inject-delay 20
              --magic-port 0x1234 --magic-port-mode hex
              --delayed-automatic-exit-frames 400)

    scp_fail=""
    # Control: headless never skips a render.
    timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" "${SCP_ARGS[@]}" \
        >/dev/null 2>"$scp_ctrl_log" || scp_fail="ctrl-run"
    # Qt GUI at --speed 400: most frames skip the compositor.
    if [[ -z "$scp_fail" ]]; then
        QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
            "${SD_CARD_ARGS[@]}" --silent --speed 400 "${SCP_ARGS[@]}" \
            >/dev/null 2>"$scp_turbo_log" || scp_fail="turbo-run"
    fi
    if [[ -z "$scp_fail" ]]; then
        scp_ctrl_seq=$(grep -E '^[0-9A-F]{2}$' "$scp_ctrl_log")
        scp_turbo_seq=$(grep -E '^[0-9A-F]{2}$' "$scp_turbo_log")
        scp_polls=$(echo "$scp_ctrl_seq" | grep -c . || true)
        scp_hits=$(echo "$scp_ctrl_seq" | grep -cE '^(01|03)$' || true)
        if [[ "$scp_polls" -eq 0 ]]; then
            scp_fail="no-polls"
        elif [[ "$scp_hits" -eq 0 ]]; then
            scp_fail="control-saw-no-collisions"
        elif [[ "$scp_ctrl_seq" != "$scp_turbo_seq" ]]; then
            scp_fail="turbo-status-sequence-differs (ctrl $scp_polls polls/$scp_hits hits, turbo $(echo "$scp_turbo_seq" | grep -c . || true)/$(echo "$scp_turbo_seq" | grep -cE '^(01|03)$' || true))"
        fi
    fi
    if [[ -z "$scp_fail" ]]; then
        pass_row " (0x303B identical headless vs GUI@400%: $scp_polls polls, $scp_hits collision reads)"
    else
        fail_row " ($scp_fail)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
