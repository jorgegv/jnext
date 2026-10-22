#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable - the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --delayed-pok-frames N FILE with --pok-trainers LIST (GH #381, owner Q1): a POK
# trainer file applied from the command line, in the headless and SDL-only runs
# that have no Machine > POK Trainers... window.
#
# WHAT THE ROW READS. A .scr screenshot is the ULA screen memory, byte for byte
# (6912 bytes), so a trainer that pokes 0x4000.. is read back from the file with
# nothing in between: no log line, no snapshot parser. Bank 5 IS the screen on a
# 128K, which puts a BANKED poke in the same file (bank 5, address 0xC010, is byte
# 0x10 of bank 5: the screen's byte 16; the same bank at 0x8011, below 0xC000, pokes
# the memory map and leaves the screen's byte 17 alone).
#
# THE CLAIMS, each with the control that makes it mean something:
#
#   applies   headless 48K: trainer 1 (a fixed poke and a value-256 poke given
#             =88) and trainer 2 land in the screen; a run without the options
#             leaves the three bytes 0.
#   frame     the trainer is applied after N frames: with the capture due at the
#             same frame N it is in the picture, with the capture one frame earlier
#             it is not. One frame either way flips one of the two.
#   order     --load x.sna with N = 0: the load happens in the first tick, the
#             trainer after it. A trainer applied BEFORE the load would be
#             overwritten by the snapshot's zero.
#   bank      headless 128K: bank 5 at 0xC010 is byte 16 of the screen; bank 5 at 0x8011, below 0xC000, is
#             NOT byte 17 (the bank is ignored there: a bank-relative reading would put 77 in it).
#   sdl       the same trainer and frame contract in the SDL-only window (dummy
#             video and audio drivers), which has its own loop.
#   refused   a usage error, or a file/list that does not check out, exits 1
#             before the machine starts; a trainer the machine refuses (bank 3
#             on a 48K) and an automatic exit before the frame exit non-zero, in
#             headless and in the SDL window; the Qt window refuses the option.
if want delayed-pok-func; then
    begin_func delayed-pok-func

    dp="$TMP_DIR/delayed-pok"
    rm -rf "$dp"; mkdir -p "$dp"
    dp_sdl="$JNEXT_SDL"
    dp_faults=()

    # Trainer 1 pokes 0x4000 (fixed 171) and 0x4001 (value 256, asked); trainer 2
    # pokes 0x4002; trainer 3 is a bank-3 poke a 48K cannot take.
    printf 'NScreen bytes\nM  8 16384 171 0\nZ  8 16385 256 0\nNOther\nZ  8 16386 99 0\nNBank 3\nZ  3 49443 1 0\nY\n' \
        > "$dp/t.pok"
    # Bank 5 at 0xC010: byte 0x10 of bank 5 (the screen). Bank 5 at 0x8011 is below 0xC000: the bank is
    # ignored and the memory map is poked (the 0x8000 RAM), so the screen's byte 17 stays 0.
    printf 'NBank 5\nZ  5 49168 55 0\nNBelow\nZ  5 32785 77 0\nY\n' > "$dp/bank.pok"
    # A file with a malformed trainer (an address outside 16384-65535).
    printf 'NBroken\nM  8 99 1 0\nZ  8 16384 1 0\nNFine\nZ  8 16384 2 0\nY\n' > "$dp/broken.pok"

    # dp_bytes <file> <offset> <count>: the bytes of a file as decimal numbers.
    dp_bytes() { od -An -tu1 -j"$2" -N"$3" "$1" | tr -s ' ' | sed 's/^ //'; }

    # dp_run <frontend> <tag> <jnext args...>: runs one frontend, keeps the log,
    # prints the status. The harness gives every row a private JNEXT_CONFIG_DIR.
    dp_run() {
        local fe=$1 tag=$2 rc=0; shift 2
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 90s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" "$@" >"$dp/$tag.log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 90s "$dp_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" "$@" >"$dp/$tag.log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 90s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" "$@" >"$dp/$tag.log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }

    # dp_refused <frontend> <tag> <expected text> <jnext args...>: must exit
    # non-zero and say <expected text> in its log.
    dp_refused() {
        local fe=$1 tag=$2 want_text=$3 rc; shift 3
        rc=$(dp_run "$fe" "$tag" "$@")
        [[ "$rc" != 0 ]] || dp_faults+=("$tag: exited 0")
        grep -qF -- "$want_text" "$dp/$tag.log" \
            || dp_faults+=("$tag: no '$want_text' in the log")
    }

    if [[ ! -x "$dp_sdl" ]]; then
        fail_row " (SDL-only binary not built: $dp_sdl; run 'make sdl-release')"
    else
        # --- applies, frame --------------------------------------------------
        # Capture at frame 10 in every run; the trainer at 10 (in the picture), at
        # 11 (one frame too late), and not at all (the control).
        common=(--machine 48k --delayed-screenshot-frames 10 --delayed-automatic-exit-frames 14)
        rc=$(dp_run headless ctl "${common[@]}" --delayed-screenshot "$dp/ctl.scr")
        [[ "$rc" == 0 ]] || dp_faults+=("control run exited $rc")
        [[ "$(dp_bytes "$dp/ctl.scr" 0 3)" == "0 0 0" ]] \
            || dp_faults+=("control: the three screen bytes are not 0 (the row proves nothing)")

        rc=$(dp_run headless on "${common[@]}" --delayed-screenshot "$dp/on.scr" \
                 --delayed-pok-frames 10 "$dp/t.pok" --pok-trainers 1=88,2)
        [[ "$rc" == 0 ]] || dp_faults+=("applies: exited $rc")
        [[ "$(dp_bytes "$dp/on.scr" 0 3)" == "171 88 99" ]] \
            || dp_faults+=("applies: screen bytes [$(dp_bytes "$dp/on.scr" 0 3)], want [171 88 99]")
        grep -qF "POK: applied 'Screen bytes'" "$dp/on.log" \
            || dp_faults+=("applies: no POK: applied line in the log")

        rc=$(dp_run headless late "${common[@]}" --delayed-screenshot "$dp/late.scr" \
                 --delayed-pok-frames 11 "$dp/t.pok" --pok-trainers 1=88,2)
        [[ "$rc" == 0 ]] || dp_faults+=("frame: the late run exited $rc")
        [[ "$(dp_bytes "$dp/late.scr" 0 3)" == "0 0 0" ]] \
            || dp_faults+=("frame: N=11 with the capture at 10 still shows [$(dp_bytes "$dp/late.scr" 0 3)]")

        # --- order: a snapshot, then the trainer, in the same first tick --------
        rc=$(dp_run headless base --machine 48k --delayed-snapshot "$dp/base.sna" \
                 --delayed-snapshot-frames 5 --delayed-automatic-exit-frames 8)
        [[ "$rc" == 0 && -s "$dp/base.sna" ]] || dp_faults+=("order: could not make the base snapshot (rc=$rc)")
        rc=$(dp_run headless order --machine 48k --load "$dp/base.sna" --delayed-pok-frames 0 "$dp/t.pok" \
                 --pok-trainers 1=88 --delayed-screenshot "$dp/order.scr" \
                 --delayed-screenshot-frames 3 --delayed-automatic-exit-frames 6)
        [[ "$rc" == 0 ]] || dp_faults+=("order: exited $rc")
        [[ "$(dp_bytes "$dp/order.scr" 0 2)" == "171 88" ]] \
            || dp_faults+=("order: screen bytes [$(dp_bytes "$dp/order.scr" 0 2)] after --load + N=0, want [171 88]")

        # --- bank -------------------------------------------------------------
        rc=$(dp_run headless bank --machine 128k --delayed-screenshot "$dp/bank.scr" \
                 --delayed-screenshot-frames 10 --delayed-automatic-exit-frames 14 \
                 --delayed-pok-frames 5 "$dp/bank.pok" --pok-trainers 1,2)
        [[ "$rc" == 0 ]] || dp_faults+=("bank: exited $rc")
        [[ "$(dp_bytes "$dp/bank.scr" 16 2)" == "55 0" ]] \
            || dp_faults+=("bank: screen bytes 16,17 are [$(dp_bytes "$dp/bank.scr" 16 2)], want [55 0] (bank 5 at 0xC010 and 0x8011)")

        # --- sdl --------------------------------------------------------------
        rc=$(dp_run sdl sdlon "${common[@]}" --delayed-screenshot "$dp/sdlon.scr" \
                 --delayed-pok-frames 10 "$dp/t.pok" --pok-trainers 1=88,2)
        [[ "$rc" == 0 ]] || dp_faults+=("sdl: exited $rc")
        [[ "$(dp_bytes "$dp/sdlon.scr" 0 3)" == "171 88 99" ]] \
            || dp_faults+=("sdl: screen bytes [$(dp_bytes "$dp/sdlon.scr" 0 3)], want [171 88 99]")
        rc=$(dp_run sdl sdllate "${common[@]}" --delayed-screenshot "$dp/sdllate.scr" \
                 --delayed-pok-frames 11 "$dp/t.pok" --pok-trainers 1=88,2)
        [[ "$(dp_bytes "$dp/sdllate.scr" 0 3)" == "0 0 0" ]] \
            || dp_faults+=("sdl: N=11 with the capture at 10 still shows [$(dp_bytes "$dp/sdllate.scr" 0 3)]")

        # --- refused: checked before the machine starts (exit 1, saying why) ------
        usage=(--machine 48k --delayed-automatic-exit-frames 20)
        dp_refused headless no-list "--delayed-pok-frames requires --pok-trainers LIST" \
            "${usage[@]}" --delayed-pok-frames 5 "$dp/t.pok"
        dp_refused headless no-file "--pok-trainers requires --delayed-pok-frames N FILE" \
            "${usage[@]}" --pok-trainers 1
        dp_refused headless bad-list "--pok-trainers: 'x'" \
            "${usage[@]}" --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 1,x
        dp_refused headless bad-n 'N must be' \
            "${usage[@]}" --delayed-pok-frames 5x "$dp/t.pok" --pok-trainers 1
        dp_refused headless no-such-file "cannot read $dp/missing.pok" \
            "${usage[@]}" --delayed-pok-frames 5 "$dp/missing.pok" --pok-trainers 1
        dp_refused headless past-end "has only 3 trainer(s)" \
            "${usage[@]}" --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 4
        dp_refused headless no-value "leaves a value open: give it as 1=VALUE" \
            "${usage[@]}" --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 1
        dp_refused headless extra-value "leaves no value open" \
            "${usage[@]}" --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 2=9
        dp_refused headless broken "trainer 1 ('Broken') is invalid - line 2" \
            "${usage[@]}" --delayed-pok-frames 5 "$dp/broken.pok" --pok-trainers 1
        dp_refused headless twice "can each be given once" \
            "${usage[@]}" --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 2 \
                          --delayed-pok-frames 6 "$dp/t.pok"
        # The Qt window has the dialog: both options are refused there, offscreen so
        # that a regression opens no real window.
        dp_refused qt qt-window "not available in the Qt window" \
            --machine 48k --delayed-automatic-exit-frames 20 \
            --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 2

        # --- refused later: the machine says no, or the exit comes first ---------
        for fe in headless sdl; do
            # Bank 3 cannot exist on a 48K: the run goes on to its end and exits 1.
            dp_refused "$fe" "$fe-48k-bank" "trainer 3 ('Bank 3'): line 7: bank 3 is not available on a 48K" \
                --machine 48k --delayed-automatic-exit-frames 20 \
                --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 3
            # The automatic exit at frame 5 comes before the trainer at frame 60.
            dp_refused "$fe" "$fe-cut" "--delayed-pok-frames: '$dp/t.pok' never happened" \
                --machine 48k --delayed-automatic-exit-frames 5 \
                --delayed-pok-frames 60 "$dp/t.pok" --pok-trainers 2
            # The control for the two above: the same trainer, applied, exits 0.
            rc=$(dp_run "$fe" "$fe-ok" --machine 48k --delayed-automatic-exit-frames 20 \
                     --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 2)
            [[ "$rc" == 0 ]] || dp_faults+=("$fe-ok: the control run exited $rc")
        done
        # An RZX that plays refuses every poke: the run says so and exits 1 (a
        # recording is made by this very run, so no fixture is needed).
        dp_refused headless rzx "RZX" \
            --machine 48k --delayed-automatic-exit-frames 20 --rzx-record "$dp/rec.rzx" \
            --delayed-pok-frames 5 "$dp/t.pok" --pok-trainers 2

        if [[ ${#dp_faults[@]} -gt 0 ]]; then
            fail_row " (${#dp_faults[@]} fault(s) in --delayed-pok-frames)"
            printf '      %s\n' "${dp_faults[@]}"
        else
            pass_row " (trainers land in the screen at frame N, not N-1; after a --load at N=0; banked at 0xC010, not at 0x8011; in headless and the SDL window; bad usage, a refused trainer, an RZX and an early exit fail the run; the Qt window refuses it)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
