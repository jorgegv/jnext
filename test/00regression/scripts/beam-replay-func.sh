#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #362 — Pixel Wheels tore and stuttered while scrolling: the tilemap, LoRes
# and Layer 2 read their memory at the END of the frame, so a write made after
# the beam had passed a byte (the game rewrites its tile map in the vertical
# blank) showed up in the frame that was already on its way out. The VHDL
# fetches every one of those bytes at beam time (tilemap.vhd:226-231, lores.vhd
# + zxnext.vhd:6603-6631, layer2.vhd `hc_eff <= hc + 1`), exactly as the ULA
# does for its own (zxula.vhd:270-303).
#
# THE WITNESS DOES NOT USE THE GAME, and does not trust the renderer. The CPU
# is halted (`DI ; HALT ; JR $-3`, four bytes built with printf below), so
# nothing writes memory except the script. For each layer lane the script
# writes a band of the layer's memory at scanline 230 of frame POKE_F — CVC
# 229, past the last displayed line, so on hardware the frame being drawn has
# already fetched everything and the write belongs to the NEXT frame. The
# oracle is a CONTROL RUN of the very same script without the write:
#
#   * the image of frame POKE_F must be IDENTICAL to the control run's;
#   * the image of frame POKE_F + 1 must DIFFER from the control run's (the
#     write is not lost, and the lane is not vacuous).
#
# (A screenshot taken in `on frame f` holds frame f + 1.)
#
# Lanes. ULA, sprite attributes and sprite patterns are the controls: they
# already replay at beam time and must keep doing so. Tilemap, LoRes, Layer 2
# 256x192 and Layer 2 320x256 are the lanes the fix is for. Two more Layer 2
# lanes pin the arming of its SRAM log: one turns Layer 2 ON mid-frame (before
# the write), the other turns it OFF mid-frame (before the write) and back on
# after it — in both the write must still be invisible to the frame in flight.
#
# Deterministic: halted CPU, --rtc pinned, headless. Every lane runs twice
# (poke, control); the runs are independent processes and go four at a time.
if want beam-replay-func; then
    begin_func beam-replay-func

    br_dir="$TMP_DIR/beam-replay"
    br_halt="$br_dir/halt.bin"
    br_delay=160          # frames before the 4-byte halt program is injected
    br_setup_f=170        # frame whose start sets the lane up
    br_poke_f=200         # frame whose scanline 230 carries the write
    mkdir -p "$br_dir"
    printf '\xf3\x76\x18\xfd' > "$br_halt"

    # ── lane definitions: <lane>_setup / <lane>_poke print jds statements ──
    br_xor_mem() {   # $1 = first address, $2 = count
        local i
        for ((i = 0; i < $2; i++)); do
            printf '    set mem[0x%04x] = mem[0x%04x] ^ 0xFF\n' $(($1 + i)) $(($1 + i))
        done
    }
    br_xor_page() {  # $1 = page, $2 = first offset, $3 = count
        local i
        for ((i = 0; i < $3; i++)); do
            printf '    set phys[0x%02x, 0x%04x] = phys[0x%02x, 0x%04x] ^ 0xFF\n' \
                $1 $(($2 + i)) $1 $(($2 + i))
        done
    }

    br_ula_setup() { :; }
    br_ula_poke() {   # the first 8 pixel lines of the first character row
        local l
        for ((l = 0; l < 8; l++)); do br_xor_mem $((0x4000 + l * 256)) 32; done
    }

    br_sprattr_setup() {
        echo '    set nextreg[0x15] = 0x01'
        echo '    set nextreg[0x68] = 0x80'
        echo '    out 0x303B 0'
        local i
        for ((i = 0; i < 256; i++)); do echo '    out 0x5B 0xE0'; done
        echo '    out 0x303B 0'
        printf '    out 0x57 %s\n' 100 100 0 0x80
    }
    br_sprattr_poke() {
        echo '    out 0x303B 0'
        printf '    out 0x57 %s\n' 160 100 0 0x80
    }

    br_sprpat_setup() { br_sprattr_setup; }
    br_sprpat_poke() {
        echo '    out 0x303B 0'
        local i
        for ((i = 0; i < 256; i++)); do echo '    out 0x5B 0x1C'; done
    }

    br_tilemap_setup() {
        # tilemap palette entries 1 and 2, 40x32 tilemap, map at bank 5 + 0x0000,
        # tile definitions at bank 5 + 0x2000, ULA off; tile 1 = all pixels 1.
        echo '    set nextreg[0x43] = 0x30'
        echo '    set nextreg[0x40] = 1'
        echo '    set nextreg[0x41] = 0xE0'
        echo '    set nextreg[0x40] = 2'
        echo '    set nextreg[0x41] = 0x1C'
        echo '    set nextreg[0x6B] = 0x80'
        echo '    set nextreg[0x6E] = 0x00'
        echo '    set nextreg[0x6F] = 0x20'
        echo '    set nextreg[0x68] = 0x80'
        echo '    set nextreg[0x4C] = 0x0F'
        echo '    set nextreg[0x2F] = 0'
        echo '    set nextreg[0x30] = 0'
        echo '    set nextreg[0x31] = 0'
        local i
        for ((i = 0; i < 32; i++)); do printf '    set phys[0x0B, 0x%02x] = 0x11\n' $((0x20 + i)); done
    }
    br_tilemap_poke() {   # map rows 4..7: tile index 1 (map entries are 2 bytes)
        local i
        for ((i = 0; i < 160; i++)); do printf '    set mem[0x%04x] = 1\n' $((0x4140 + 2 * i)); done
    }

    br_lores_setup() { echo '    set nextreg[0x15] = 0x80'; }
    br_lores_poke() { br_xor_mem 0x4500 512; }

    br_l2_256_setup() {
        echo '    set nextreg[0x12] = 9'
        echo '    set nextreg[0x70] = 0'
        echo '    out 0x123B 0x02'
    }
    br_l2_256_poke() { br_xor_page 0x12 0x1400 1024; }

    br_l2_320_setup() {
        echo '    set nextreg[0x12] = 9'
        echo '    set nextreg[0x70] = 0x10'
        echo '    out 0x123B 0x02'
    }
    br_l2_320_poke() { br_xor_page 0x12 0x1400 1024; }

    # Layer 2 configured but OFF at the frame's start; ON from scanline 10.
    br_l2_late_on_setup() {
        echo '    set nextreg[0x12] = 9'
        echo '    set nextreg[0x70] = 0'
    }
    br_l2_late_on_poke() { br_xor_page 0x12 0x1400 1024; }
    br_l2_late_on_extra() {
        echo "on scanline 10 when FRAME == $br_poke_f do"
        echo '    out 0x123B 0x02'
        echo 'end'
    }

    # Layer 2 ON at the frame's start; OFF at scanline 100, ON again at 240.
    br_l2_mid_off_setup() { br_l2_256_setup; }
    br_l2_mid_off_poke() { br_xor_page 0x12 0x1400 1024; }
    br_l2_mid_off_extra() {
        echo "on scanline 100 when FRAME == $br_poke_f do"
        echo '    out 0x123B 0x00'
        echo 'end'
        echo "on scanline 240 when FRAME == $br_poke_f do"
        echo '    out 0x123B 0x02'
        echo 'end'
    }

    br_lanes=(ula sprattr sprpat tilemap lores l2_256 l2_320 l2_late_on l2_mid_off)

    br_make_script() {   # $1 = lane, $2 = poke|control, $3 = output dir
        local lane=$1 kind=$2 out=$3
        {
            echo "on frame $br_setup_f do"
            "br_${lane}_setup"
            echo 'end'
            if declare -F "br_${lane}_extra" >/dev/null; then "br_${lane}_extra"; fi
            if [[ "$kind" == poke ]]; then
                echo "on scanline 230 when FRAME == $br_poke_f do"
                "br_${lane}_poke"
                echo 'end'
            fi
            echo "on frame $((br_poke_f - 1)) do screenshot \"$out/frame_a.png\" end"
            echo "on frame $br_poke_f do screenshot \"$out/frame_b.png\" end"
            echo "on frame $((br_poke_f + 4)) do exit 0 end"
        } > "$out/run.jds"
    }

    br_run() {   # $1 = lane, $2 = poke|control
        local out="$br_dir/$1-$2"
        mkdir -p "$out"
        br_make_script "$1" "$2" "$out"
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rtc 2026-07-10T08:55:00 \
            --inject "$br_halt" --inject-org 0x8000 --inject-delay "$br_delay" \
            --script "$out/run.jds" > "$out/run.log" 2>&1 || true
    }

    br_jobs=0
    for br_lane in "${br_lanes[@]}"; do
        for br_kind in poke control; do
            br_run "$br_lane" "$br_kind" &
            br_jobs=$((br_jobs + 1))
            if (( br_jobs >= 4 )); then
                wait -n || true
                br_jobs=$((br_jobs - 1))
            fi
        done
    done
    wait || true

    br_bad=""
    for br_lane in "${br_lanes[@]}"; do
        br_p="$br_dir/$br_lane-poke"
        br_c="$br_dir/$br_lane-control"
        if [[ ! -s "$br_p/frame_a.png" || ! -s "$br_p/frame_b.png" \
              || ! -s "$br_c/frame_a.png" || ! -s "$br_c/frame_b.png" ]]; then
            br_bad+=" $br_lane:no-screenshot;"
        elif ! cmp -s "$br_p/frame_a.png" "$br_c/frame_a.png"; then
            br_bad+=" $br_lane:poke-visible-in-the-frame-being-drawn;"
        elif cmp -s "$br_p/frame_b.png" "$br_c/frame_b.png"; then
            br_bad+=" $br_lane:poke-never-shown(vacuous);"
        fi
    done

    if [[ -z "$br_bad" ]]; then
        pass_row " (${#br_lanes[@]} lanes: a write at CVC 229 is absent from the frame in flight and present in the next)"
    else
        fail_row " ($br_bad)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
