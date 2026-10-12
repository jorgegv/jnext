#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #325 — the 48K, 128K and +3 (and Pentagon) personalities start with the
# port enables the real firmware gives them. The oracle is tbblue.fw booting
# `--machine next` on a clone whose config.ini says default=2 (48K), 3 (128K),
# 5 (+3) or 14 (Pentagon) (menu.def): boot.c's mode 0/1/2/3 writes NR 0x03 =
# 0x91/0xA2/0xB3/0xC4 and the NR 0x82-0x85 enables itself. A headless
# standalone run of the same machine must agree on every bit the firmware
# writes the same whatever config.ini says:
#   NR 0x03 and NR 0x82 equal; NR 0x83 bits 7:6 = 0 (Layer 2, sprites);
#   NR 0x85 bits 7,3,2 = 0 (reset type, CTC, EFF7); NR 0x84 bit 0 equal on the
#   48K (AY, ay48 = 0) and bit 2 equal on the Pentagon (Soundrive 2).
# and must read back exactly C0/3F/FE/03, C2/3F/FF/03, DA/3F/FF/03 and
# C6/3F/FB/03 (the config-dependent bits stay on). The row requires the
# firmware to have logged its own NR 0x03 commit, so a run that never reached
# the personality FAILS instead of comparing two empty strings.
#
# The card is WRITTEN (config.ini), hence private-sd; the master is never
# touched. `sdfile_tool` is the in-tree FAT32 writer, no external tool.
if want machine-port-enables-func; then
    begin_func machine-port-enables-func

    sdfile="$TEST_BIN_DIR/sdfile_tool"
    sd_image="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    jds_a="$TMP_DIR/machine_port_enables_a.jds"
    jds_fw="$TMP_DIR/machine_port_enables_fw.jds"
    cfg_in="$TMP_DIR/machine_port_enables_config.ini"
    cfg_out="$TMP_DIR/machine_port_enables_config_n.ini"
    io_log="$TMP_DIR/machine_port_enables_io.log"
    verdict=""
    rm -f "$jds_a" "$jds_fw" "$cfg_in" "$cfg_out" "$io_log"

    if [[ ! -f "$sd_image" ]]; then
        skip_row " (no SD-card image clone for this run)"
        verdict=settled
    elif [[ ! -x "$sdfile" ]]; then
        fail_row " ($TEST_BIN_DIR/sdfile_tool missing — run 'make unit-test-build')"
        verdict=settled
    elif ! "$sdfile" get "$sd_image" machines/next/config.ini "$cfg_in" >"$io_log" 2>&1; then
        fail_row " (could not read config.ini from the SD clone: $(tr -d '\n' <"$io_log"))"
        verdict=settled
    fi

    state_line='log "STATE nr03=${nextreg[0x03]:x2} nr82=${nextreg[0x82]:x2} nr83=${nextreg[0x83]:x2} nr84=${nextreg[0x84]:x2} nr85=${nextreg[0x85]:x2}"'

    if [[ -z "$verdict" ]]; then
        cat >"$jds_a" <<JDS
on frame 100 once do
    $state_line
end
JDS
        cat >"$jds_fw" <<JDS
on nextreg 0x03 do
    log "NRW 03 = \${VALUE:x2}"
end
on frame 700 once do
    $state_line
end
JDS
    fi

    field() {   # $1 = log, $2 = key: the value of `key=` on its STATE line, upper-cased
        local line
        line=$(grep -m1 ' STATE ' "$1" || true)
        [[ -n "$line" ]] || return 0
        grep -o " $2=[0-9A-Fa-f]*" <<<"$line" | head -n 1 | cut -d= -f2 | tr 'a-f' 'A-F'
    }
    hexand() { printf '%02X' $(( 0x${1:-0} & 0x$2 )); }

    why=""
    summary=""
    if [[ -z "$verdict" ]]; then
        # name machine default= firmware-NR03 want82 want83 want84 want85 mask84
        for spec in "48k 48k 2 91 C0 3F FE 03 01" "128k 128k 3 A2 C2 3F FF 03 00" \
                    "plus3 plus3 5 B3 DA 3F FF 03 00" "pentagon pentagon 14 C4 C6 3F FB 03 04"; do
            read -r nm mach def fwc w82 w83 w84 w85 m84 <<<"$spec"
            log_a="$TMP_DIR/machine_port_enables_${nm}_a.log"
            log_b="$TMP_DIR/machine_port_enables_${nm}_b.log"
            rm -f "$log_a" "$log_b"

            LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine "$mach" \
                "${SD_CARD_ARGS[@]}" --script "$jds_a" \
                --delayed-automatic-exit-frames 110 >"$log_a" 2>&1 || true

            { grep -v '^default=' "$cfg_in" || true; printf 'default=%s\n' "$def"; } >"$cfg_out"
            if ! "$sdfile" put "$sd_image" machines/next/config.ini "$cfg_out" >"$io_log" 2>&1; then
                why+=" $nm: could not write config.ini: $(tr -d '\n' <"$io_log");"
                continue
            fi
            LANG=C timeout --foreground --kill-after=5s 240s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --script "$jds_fw" \
                --delayed-automatic-exit-frames 710 >"$log_b" 2>&1 || true

            a03=$(field "$log_a" nr03); a82=$(field "$log_a" nr82); a83=$(field "$log_a" nr83)
            a84=$(field "$log_a" nr84); a85=$(field "$log_a" nr85)
            b03=$(field "$log_b" nr03); b82=$(field "$log_b" nr82); b83=$(field "$log_b" nr83)
            b84=$(field "$log_b" nr84); b85=$(field "$log_b" nr85)
            fw_commit=$(grep -c "NRW 03 = $fwc" "$log_b" || true)

            # (a) the standalone run reads back the pinned bytes
            [[ "$a82" == "$w82" && "$a83" == "$w83" && "$a84" == "$w84" && "$a85" == "$w85" ]] ||
                why+=" $nm a:nr82-85=$a82/$a83/$a84/$a85 want $w82/$w83/$w84/$w85;"
            # (b) the firmware reached its personality
            (( fw_commit >= 1 )) || why+=" $nm b:the firmware never wrote NR 0x03 = 0x$fwc (config.ini default=$def not taken?);"
            [[ -n "$b82" ]] || why+=" $nm b:no STATE line;"
            # parity on the config-invariant bits
            [[ -n "$a03" && "$a03" == "$b03" && "$a82" == "$b82" &&
               "$(hexand "${a83:-0}" C0)" == "$(hexand "${b83:-0}" C0)" &&
               "$(hexand "${a85:-0}" 8C)" == "$(hexand "${b85:-0}" 8C)" &&
               "$(hexand "${a84:-0}" "$m84")" == "$(hexand "${b84:-0}" "$m84")" ]] ||
                why+=" $nm parity: standalone $a03/$a82/$a83/$a84/$a85 vs firmware $b03/$b82/$b83/$b84/$b85;"
            summary+=" $nm=$a82/$a83/$a84/$a85"
        done

        if [[ -z "$why" ]]; then
            pass_row " (standalone = firmware personality, nr82/83/84/85:$summary)"
        else
            fail_row " ($why)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
