#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #324 — `--machine pentagon` is the Next's Pentagon 128 personality, and
# the oracle for "what that is" is the real firmware, not this emulator:
#
#   (a) a headless `--machine pentagon` run reads back NR 0x03 = 0x44,
#       NR 0x82 = 0xC6, NR 0x84 = 0xFB, NR 0x85 bit 7 = 0, NR 0x8F = 0x00,
#       NR 0x08 bit 3 = 1 and MACHINE = 3; its frame is 573440 master cycles
#       (zxula_timing.vhd:150-168, 448 x 320 x 4); and NR 0x05 bit 2 (60 Hz)
#       cannot be set (zxnext.vhd:5835-5836);
#   (b) tbblue.fw booting `--machine next` on a clone whose config.ini says
#       `default=14` (menu.def "Pentagon 128K,3,8,128.rom") runs boot.c's mode
#       3 itself and ends in the same register state. The row requires the
#       firmware to have logged its `NR 0x03 = 0xC4` commit, so a run that
#       never reached the firmware's personality FAILS instead of comparing
#       two empty strings.
#
# The card is WRITTEN (config.ini), hence private-sd; the master is never
# touched. `sdfile_tool` is the in-tree FAT32 writer, no external tool.
if want machine-pentagon-func; then
    begin_func machine-pentagon-func

    sdfile="$PROJECT_DIR/build/test/sdfile_tool"
    sd_image="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    jds="$TMP_DIR/machine_pentagon.jds"
    jds_fw="$TMP_DIR/machine_pentagon_fw.jds"
    log_a="$TMP_DIR/machine_pentagon_a.log"
    log_b="$TMP_DIR/machine_pentagon_b.log"
    cfg_in="$TMP_DIR/machine_pentagon_config.ini"
    cfg_out="$TMP_DIR/machine_pentagon_config_14.ini"
    io_log="$TMP_DIR/machine_pentagon_io.log"
    verdict=""
    rm -f "$jds" "$jds_fw" "$log_a" "$log_b" "$cfg_in" "$cfg_out" "$io_log"

    if [[ ! -f "$sd_image" ]]; then
        skip_row " (no SD-card image clone for this run)"
        verdict=settled
    elif [[ ! -x "$sdfile" ]]; then
        fail_row " (build/test/sdfile_tool missing — run 'make unit-test-build')"
        verdict=settled
    fi

    # The state line both runs print; the same text so one parser reads both.
    state_line='log "STATE nr03=${nextreg[0x03]:x2} nr08=${nextreg[0x08]:x2} nr82=${nextreg[0x82]:x2} nr84=${nextreg[0x84]:x2} nr85=${nextreg[0x85]:x2} nr8f=${nextreg[0x8F]:x2} MACHINE=${MACHINE:d}"'

    if [[ -z "$verdict" ]]; then
        cat >"$jds" <<JDS
var c0 = 0
on frame 100 once do
    set c0 = CYCLE
end
on frame 150 once do
    log "FRAMELEN \${CYCLE - c0:d}"
    $state_line
    set nextreg[0x05] = 0x85
end
on frame 152 once do
    log "NR05 \${nextreg[0x05]:x2}"
end
JDS
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine pentagon \
            "${SD_CARD_ARGS[@]}" --script "$jds" \
            --delayed-automatic-exit-frames 160 >"$log_a" 2>&1 || true

        cat >"$jds_fw" <<JDS
on nextreg 0x03 do
    log "NRW 03 = \${VALUE:x2}"
end
on frame 600 once do
    $state_line
end
JDS
        if ! "$sdfile" get "$sd_image" machines/next/config.ini "$cfg_in" >"$io_log" 2>&1; then
            fail_row " (could not read config.ini from the SD clone: $(tr -d '\n' <"$io_log"))"
            verdict=settled
        else
            { grep -v '^default=' "$cfg_in" || true; printf 'default=14\n'; } >"$cfg_out"
            if ! "$sdfile" put "$sd_image" machines/next/config.ini "$cfg_out" >"$io_log" 2>&1; then
                fail_row " (could not write config.ini to the SD clone: $(tr -d '\n' <"$io_log"))"
                verdict=settled
            fi
        fi
    fi

    if [[ -z "$verdict" ]]; then
        LANG=C timeout --foreground --kill-after=5s 240s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --script "$jds_fw" \
            --delayed-automatic-exit-frames 610 >"$log_b" 2>&1 || true

        field() {   # $1 = log, $2 = key: the value of `key=` on its STATE line, upper-cased
            local line
            line=$(grep -m1 ' STATE ' "$1" || true)
            [[ -n "$line" ]] || return 0
            grep -o " $2=[0-9A-Fa-f]*" <<<"$line" | head -n 1 | cut -d= -f2 | tr 'a-f' 'A-F'
        }
        hexand() { printf '%02X' $(( 0x${1:-0} & 0x$2 )); }

        a03=$(field "$log_a" nr03); a08=$(field "$log_a" nr08); a82=$(field "$log_a" nr82)
        a84=$(field "$log_a" nr84); a85=$(field "$log_a" nr85); a8f=$(field "$log_a" nr8f)
        am=$(grep -m1 ' STATE ' "$log_a" | grep -o 'MACHINE=[0-9]*' | cut -d= -f2 || true)
        b03=$(field "$log_b" nr03); b08=$(field "$log_b" nr08); b82=$(field "$log_b" nr82)
        b84=$(field "$log_b" nr84); b85=$(field "$log_b" nr85); b8f=$(field "$log_b" nr8f)
        flen=$(grep -m1 'FRAMELEN' "$log_a" | grep -o 'FRAMELEN [0-9]*' | cut -d' ' -f2 || true)
        n05=$(grep -m1 ' NR05 ' "$log_a" | grep -o 'NR05 [0-9A-Fa-f]*' | cut -d' ' -f2 | tr 'a-f' 'A-F' || true)
        fw_commit=$(grep -c 'NRW 03 = C4' "$log_b" || true)

        why=""
        # (a) the standalone personality
        [[ "$a03" == 44 && "$a82" == C6 && "$a84" == FB && "$a8f" == 00 && "$am" == 3 ]] ||
            why+=" a:regs nr03=$a03 nr82=$a82 nr84=$a84 nr8f=$a8f MACHINE=$am;"
        [[ -n "$a85" && "$(hexand "$a85" 80)" == 00 ]] || why+=" a:nr85=$a85;"
        [[ -n "$a08" && "$(hexand "$a08" 08)" == 08 ]] || why+=" a:nr08=$a08;"
        if [[ -z "$flen" ]] || (( flen < 50 * 573440 - 200 || flen > 50 * 573440 + 200 )); then
            why+=" a:50 frames took '$flen' master cycles (want 28672000 +-200);"
        fi
        [[ -n "$n05" && "$(hexand "$n05" 04)" == 00 ]] || why+=" a:nr05=$n05 after writing 0x85;"
        # (b) the firmware's own personality, the oracle
        (( fw_commit >= 1 )) || why+=" b:the firmware never wrote NR 0x03 = 0xC4 (config.ini default=14 not taken?);"
        [[ "$b03" == 44 && "$b82" == C6 && "$b8f" == 00 ]] ||
            why+=" b:regs nr03=$b03 nr82=$b82 nr8f=$b8f;"
        [[ -n "$b84" && "$(hexand "$b84" 04)" == 00 ]] || why+=" b:nr84=$b84;"
        [[ -n "$b85" && "$(hexand "$b85" 80)" == 00 ]] || why+=" b:nr85=$b85;"
        [[ -n "$b08" && "$(hexand "$b08" 08)" == 08 ]] || why+=" b:nr08=$b08;"
        # parity: the same bits in both
        [[ "$a03" == "$b03" && "$a82" == "$b82" && "$a8f" == "$b8f" &&
           "$(hexand "${a84:-0}" 04)" == "$(hexand "${b84:-0}" 04)" &&
           "$(hexand "${a85:-0}" 80)" == "$(hexand "${b85:-0}" 80)" ]] ||
            why+=" parity: standalone vs firmware differ;"

        if [[ -z "$why" ]]; then
            pass_row " (standalone = firmware personality: nr03=$a03 nr82=$a82 nr84=$a84 nr8f=$a8f; frame $((flen / 50)) master cycles; 60 Hz bit forced off)"
        else
            fail_row " ($why)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
