#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #458 - the minimal NextZXOS card (tools/make-minimal-sdcard-image.sh) must
# BOOT: the NextZXOS menu and a warm-started NEX must match the existing
# references of the full card, pixel for pixel. The references are never
# regenerated for this row.
#
# Both runs mount the minimal card with --sdcard, and each must say so in its
# log ("SD card image mounted: '<card>'"): the full card renders the same two
# screenshots, so the pictures alone cannot prove which card booted.
#
# The minimal card's welcome screen finishes drawing later than the full
# card's (frame ~410 against 390-400), so SPACE goes in at frame 500 and the
# screenshot 50 frames later; SPACE at 400 captures the welcome screen. The
# warm-start cache goes to this row's own $JNEXT_CONFIG_DIR. The card lives
# under $RUN_DIR/private and is removed at the end; it is never kept.
if want minimal-sdcard-image-func; then
    begin_func minimal-sdcard-image-func

    ms_faults=()
    ms_missing=()
    for tool in mformat mcopy mdir minfo mmd sfdisk truncate sha256sum realpath; do
        command -v "$tool" &>/dev/null || ms_missing+=("$tool")
    done

    if [[ ${#ms_missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${ms_missing[*]} - install mtools and util-linux)"
    elif [[ ! -f "$SD_MASTER_DIR/cspect-next-1gb.img" ]]; then
        fail_row " (the distribution image $SD_MASTER_DIR/cspect-next-1gb.img is absent)"
    else
        ms_dir="$RUN_DIR/private/minimal-sdcard-image"
        ms_card="$ms_dir/minimal.img"
        rm -rf "$ms_dir"; mkdir -p "$ms_dir"
        ms_menu="" ms_nex="" ms_build_rc=0
        bash "$PROJECT_DIR/tools/make-minimal-sdcard-image.sh" \
            "$SD_MASTER_DIR/cspect-next-1gb.img" "$ms_card" >"$ms_dir/build.out" 2>"$ms_dir/build.err" \
            || ms_build_rc=$?
        [[ "$ms_build_rc" == 0 ]] || ms_faults+=("build: the script exited $ms_build_rc")

        if [[ "$ms_build_rc" == 0 ]]; then
            # Menu: SPACE at the welcome screen, the NextZXOS menu 50 frames later.
            ms_shot_menu="$TMP_DIR/minimal-menu.png"
            ms_menu=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
                --sdcard "$ms_card" --rtc "$NEXTZXOS_RTC" \
                --delayed-keypress-frames 500 space \
                --delayed-screenshot "$ms_shot_menu" --delayed-screenshot-frames 550 \
                --delayed-automatic-exit-frames 560 2>&1 || true)
            grep -qF "SD card image mounted: '$ms_card'" <<<"$ms_menu" \
                || ms_faults+=("menu: the run did not mount the minimal card")
            if [[ -f "$ms_shot_menu" ]]; then
                ms_d=$(png_diff "$ms_shot_menu" "test/00regression/img/boot-nextzxos-menu-reference.png")
                [[ "$ms_d" == "0" ]] \
                    || ms_faults+=("menu: the minimal card's NextZXOS menu differs from the reference by $ms_d pixels")
            else
                ms_faults+=("menu: no screenshot was written")
            fi

            # NEX: warm start from the minimal card, own cache dir ($JNEXT_CONFIG_DIR).
            ms_shot_nex="$TMP_DIR/minimal-nex.png"
            ms_nex=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
                --sdcard "$ms_card" --rtc "$NEXTZXOS_RTC" \
                --load test/00regression/nex/tilemap_demo.nex \
                --delayed-screenshot "$ms_shot_nex" --delayed-screenshot-frames 150 \
                --delayed-automatic-exit-frames 160 2>&1 || true)
            grep -qF "SD card image mounted: '$ms_card'" <<<"$ms_nex" \
                || ms_faults+=("nex: the run did not mount the minimal card")
            grep -q "recorded a NextZXOS-resident machine" <<<"$ms_nex" \
                || ms_faults+=("nex: the boot from the minimal card did not produce a NextZXOS-resident machine")
            grep -q "NextZXOS is resident; the program is applied on top of it" <<<"$ms_nex" \
                || ms_faults+=("nex: the restored machine failed the residency re-check")
            if grep -q "falls back to the synthetic machine" <<<"$ms_nex"; then
                ms_faults+=("nex: the load fell back to the synthetic machine")
            fi
            if [[ -f "$ms_shot_nex" ]]; then
                ms_d=$(png_diff "$ms_shot_nex" "test/00regression/img/tilemap-demo-reference.png")
                [[ "$ms_d" == "0" ]] \
                    || ms_faults+=("nex: tilemap-demo from the minimal card differs from its reference by $ms_d pixels")
            else
                ms_faults+=("nex: no screenshot was written")
            fi
        fi

        if [[ ${#ms_faults[@]} -eq 0 ]]; then
            pass_row " (the minimal card builds, boots the NextZXOS menu and warm-starts a NEX, both matching the full-card references)"
        else
            fail_row " ($(IFS='; '; echo "${ms_faults[*]}"))"
            show_output "build stderr" "$(cat "$ms_dir/build.err" 2>/dev/null || true)"
            show_output "menu run" "$ms_menu" 'mount|warm start|\[error\]|\[warning\]|NextZXOS'
            show_output "nex run" "$ms_nex" 'mount|warm start|\[error\]|\[warning\]|NextZXOS'
        fi
        rm -rf "$ms_dir"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
