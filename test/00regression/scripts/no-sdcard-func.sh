#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# no-sdcard-func (GH #540): --no-sdcard runs a directly loaded NEX with no SD
# card at all. No SD_CARD_ARGS anywhere: the run's card must not be needed.
#
#   A  the esxdos-hostfs probe (one generator, invented bytes) in d/ with
#      --no-sdcard and nothing else: its own directory is served (6789AB,
#      SANDBOX-OK), the warm start is skipped quietly, no card is mounted and
#      no ROM fetch is reported; exit 0
#   B  the IM 1 probe (no-sdcard-probe.py): prints IM1-ARMED, then the first
#      frame interrupt fetches $0038 from the empty ROM, which is logged once
#   C  each refusal exits 1 with its message: a card option (either slot), a delayed card
#      insert (either slot), a delayed NMI, a machine other than the Next, a non-.nex load
#      and no --load at all
#   D  the window with a saved 48K in Preferences and no --machine: refused (exit 1)
if want no-sdcard-func; then
    begin_func no-sdcard-func

    ns_dir="$TMP_DIR/d"
    mkdir -p "$ns_dir"
    printf '0123456789ABCDEF' > "$ns_dir/data.txt"
    printf 'SECRET' > "$TMP_DIR/escape.txt"
    python3 "$SCRIPT_DIR/esxdos-hostfs-probe.py" "$ns_dir/hostfs.nex"
    python3 "$SCRIPT_DIR/no-sdcard-probe.py" "$TMP_DIR/im1.nex"
    printf 'x' > "$TMP_DIR/prog.tap"

    ns_run() {  # args: extra jnext args; prints the output, then "RC=<exit status>"
        local rc=0
        timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --no-sdcard \
            --magic-port 0xCAFE --magic-port-mode line \
            "$@" --delayed-automatic-exit-frames 120 2>&1 || rc=$?
        echo "RC=$rc"
    }
    cnt()  { grep -cxF -- "$1" <<<"$2" || true; }
    has()  { grep -cF -- "$1" <<<"$2" || true; }

    ns_a=$(ns_run --load "$ns_dir/hostfs.nex")
    a_rc=$(cnt RC=0 "$ns_a")
    a_slice=$(cnt 6789AB "$ns_a"); a_ok=$(cnt SANDBOX-OK "$ns_a"); a_err=$(cnt ERR "$ns_a")
    a_skip=$(has "warm start: skipped (--no-sdcard)" "$ns_a")
    a_card=$(has "SD card image mounted" "$ns_a")
    a_rom=$(has "executed ROM code" "$ns_a")
    a_noimg=$(has "no SD image is mounted" "$ns_a")

    ns_b=$(ns_run --load "$TMP_DIR/im1.nex")
    b_armed=$(cnt IM1-ARMED "$ns_b")
    b_rom=$(has "executed ROM code at \$0038 (IM 1); --no-sdcard has no ROM" "$ns_b")
    b_any=$(has "executed ROM code" "$ns_b")

    # The refusals: "<expected message>|<args>" (args split on spaces; no paths with spaces).
    c_bad=""
    while IFS='|' read -r msg args; do
        # shellcheck disable=SC2086
        out=$(ns_run $args)
        if [[ "$(cnt RC=1 "$out")" -ne 1 || "$(cnt "$msg" "$out")" -ne 1 ]]; then
            c_bad+=" [$args]"
        fi
    done <<EOF
--no-sdcard cannot be combined with --sdcard: there is no SD card.|--sdcard $TMP_DIR/card.img --load $ns_dir/hostfs.nex
--no-sdcard cannot be combined with --delayed-sdcard-insert-frames: there is no SD card.|--delayed-sdcard-insert-frames 10 $TMP_DIR/card.img --load $ns_dir/hostfs.nex
--no-sdcard cannot be combined with --sdcard2: there is no SD card.|--sdcard2 $TMP_DIR/card2.img --load $ns_dir/hostfs.nex
--no-sdcard cannot be combined with --sdcard2-readonly: there is no SD card.|--sdcard2-readonly --load $ns_dir/hostfs.nex
--no-sdcard cannot be combined with --delayed-sdcard2-insert-frames: there is no SD card.|--delayed-sdcard2-insert-frames 10 $TMP_DIR/card2.img --load $ns_dir/hostfs.nex
--no-sdcard cannot be combined with --delayed-nmi: there is no ROM to service an NMI.|--delayed-nmi 1 mf --load $ns_dir/hostfs.nex
--no-sdcard needs the Next: the other machines run from ROMs on the SD card.|--machine 48k --load $ns_dir/hostfs.nex
--no-sdcard needs --load of a .nex (directly, or in a .zip): with no SD card there are no ROMs to run anything else.|--load $TMP_DIR/prog.tap
--no-sdcard needs --load of a .nex (directly, or in a .zip): with no SD card there are no ROMs to run anything else.|
EOF

    # D: the window boots the machine saved in Preferences when --machine is not given, so a
    # saved 48K must be refused too (the CLI-only check let it boot a ROM-less 48K).
    mkdir -p "$TMP_DIR/cfg48"
    printf '[startup]\nmachine_type=48k\n' > "$TMP_DIR/cfg48/jnext.conf"
    d_rc=0
    ns_d=$(env JNEXT_CONFIG_DIR="$TMP_DIR/cfg48" QT_QPA_PLATFORM=offscreen \
        timeout --foreground --kill-after=5s 60s "$JNEXT" --no-sdcard --load "$ns_dir/hostfs.nex" \
        --delayed-automatic-exit-frames 120 2>&1) || d_rc=$?
    d_msg=$(has "--no-sdcard needs the Next: the other machines run from ROMs on the SD card (the saved machine is 48K: give --machine next)." "$ns_d")

    if [[ "$a_rc" -eq 1 && "$a_slice" -ge 1 && "$a_ok" -ge 1 && "$a_err" -eq 0 && "$a_skip" -eq 1 \
          && "$a_card" -eq 0 && "$a_rom" -eq 0 && "$a_noimg" -eq 0 \
          && "$b_armed" -ge 1 && "$b_rom" -eq 1 && "$b_any" -eq 1 && -z "$c_bad" && "$d_rc" -eq 1 && "$d_msg" -eq 1 ]]; then
        pass_row " (NEX dir served with no card, warm start skipped, IM 1 ROM fetch reported once, 9 refusals, a saved 48K refused)"
    else
        fail_row " (A clean-exit=$a_rc want1 slice=$a_slice ok=$a_ok err=$a_err skip-log=$a_skip want1 card=$a_card want0 rom=$a_rom want0 no-image-error=$a_noimg want0; B armed=$b_armed rom-im1=$b_rom want1 any-rom=$b_any want1; C refused wrongly:${c_bad:- none}; D saved-48K exit=$d_rc want1 message=$d_msg want1)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
