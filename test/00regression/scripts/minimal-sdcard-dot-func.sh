#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #549 - NextZXOS runs every dot command from C:/DOT, and its file browser opens most files
# (a .md text file with `.more`, a NEX with `.nexload`) through one, so the minimal card must carry
# /DOT. The oracle is the distribution's own full card running the same command: `.more readme.md`
# on the minimal card must render pixel for pixel like it. No reference PNG: a self-made one would
# be its own oracle.
#
# Three runs, the same keys (NextZXOS menu, the Command Line entry, then `.more readme.md`):
#   (a) the pinned minimal card, built from the cached distribution image;
#   (b) the run's own full card (SD_CARD_ARGS is empty: jnext's own provisioned image, cloned);
#   (c) the control: (a) with ::/dot removed by mtools (a foreign writer), the card as it was
#       before the fix. It must differ from (b): the diff shows the comparison sees the bug
#       (the screen reads `No such command`).
# Each log must say which card was mounted. The cards live under $RUN_DIR/private and are removed
# at the end; they are never kept.
if want minimal-sdcard-dot-func; then
    begin_func minimal-sdcard-dot-func

    md_faults=()
    md_missing=()
    for tool in sha256sum mdeltree; do
        command -v "$tool" &>/dev/null || md_missing+=("$tool")
    done

    if [[ ${#md_missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${md_missing[*]})"
    elif [[ ! -x "$MAKE_MINIMAL_CARD" ]]; then
        fail_row " (the card builder $MAKE_MINIMAL_CARD is not built: make unit-test-build)"
    elif [[ ! -f "$SD_MASTER_DIR/cspect-next-1gb.img" ]]; then
        fail_row " (the distribution image $SD_MASTER_DIR/cspect-next-1gb.img is absent)"
    else
        md_dir="$RUN_DIR/private/minimal-sdcard-dot"
        md_a="$md_dir/minimal.img"
        md_c="$md_dir/nodot.img"
        rm -rf "$md_dir"; mkdir -p "$md_dir"
        md_log_a="" md_log_b="" md_log_c="" md_rc=0
        # Pinned mode: the image digest and the card digest of kDistribution must come out.
        "$MAKE_MINIMAL_CARD" "$SD_MASTER_DIR/cspect-next-1gb.img" "$md_a" >"$md_dir/build.out" 2>"$md_dir/build.err" \
            || md_rc=$?
        [[ "$md_rc" == 0 ]] || md_faults+=("build: the card builder exited $md_rc")
        if [[ "$md_rc" == 0 ]]; then
            cp "$md_a" "$md_c"
            MTOOLS_SKIP_CHECK=1 mdeltree -i "$md_c@@32256" ::/dot >/dev/null 2>&1 \
                || md_faults+=("control: mdeltree could not remove ::/dot")

            # SPACE leaves the welcome, DOWN + ENTER pick the command line, then the command is
            # typed 15 frames apart and run. Frames are boot-relative (bf).
            md_keys=(677 space 747 down 777 enter)
            md_at=837
            for md_ch in . m o r e space r e a d m e . m d; do
                md_keys+=("$md_at" "$md_ch"); md_at=$((md_at + 15))
            done
            md_keys+=("$((md_at + 20))" enter)
            bf_keys "${md_keys[@]}"
            md_run() {   # md_run <png> [card args...]: the keys, a screenshot at bf 1300
                local png=$1; shift
                timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
                    "$@" --rtc "$NEXTZXOS_RTC" "${BF_KEYS[@]}" \
                    --delayed-screenshot "$png" --delayed-screenshot-frames "$(bf 1300)" \
                    --delayed-automatic-exit-frames "$(bf 1310)" 2>&1 || true
            }
            md_log_a=$(md_run "$TMP_DIR/dot-a.png" --sdcard "$md_a")
            md_log_b=$(md_run "$TMP_DIR/dot-b.png" ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"})
            md_log_c=$(md_run "$TMP_DIR/dot-c.png" --sdcard "$md_c")
            grep -qF "SD card image mounted: '$md_a'" <<<"$md_log_a" || md_faults+=("a: the run did not mount the minimal card")
            grep -qF "SD card image mounted: '$md_c'" <<<"$md_log_c" || md_faults+=("c: the run did not mount the control card")
            if grep -qF "SD card image mounted: '$md_a'" <<<"$md_log_b" || grep -qF "SD card image mounted: '$md_c'" <<<"$md_log_b" \
               || ! grep -q "SD card image mounted: '.*cspect-next-1gb-fixed.img'" <<<"$md_log_b"; then
                md_faults+=("b: the run did not mount the full card")
            fi
            if [[ -f "$TMP_DIR/dot-a.png" && -f "$TMP_DIR/dot-b.png" && -f "$TMP_DIR/dot-c.png" ]]; then
                md_ab=$(png_diff "$TMP_DIR/dot-a.png" "$TMP_DIR/dot-b.png")
                md_cb=$(png_diff "$TMP_DIR/dot-c.png" "$TMP_DIR/dot-b.png")
                [[ "$md_ab" == "0" ]] \
                    || md_faults+=("'.more readme.md' on the minimal card differs from the full card by $md_ab pixels")
                [[ "$md_cb" =~ ^[0-9]+$ && "$md_cb" -gt 0 ]] \
                    || md_faults+=("control: the card without /dot renders like the full card ($md_cb pixels): the comparison cannot see the bug")
            else
                md_faults+=("a screenshot was not written")
            fi
        fi

        if [[ ${#md_faults[@]} -eq 0 ]]; then
            pass_row " ('.more readme.md' on the pinned minimal card renders like the full card (0 pixels); the same card without /dot differs by $md_cb)"
        else
            fail_row " ($(IFS='; '; echo "${md_faults[*]}"))"
            show_output "build stderr" "$(cat "$md_dir/build.err" 2>/dev/null || true)"
            show_output "run a" "$md_log_a" 'mount|\[error\]|\[warning\]'
            show_output "run b" "$md_log_b" 'mount|\[error\]|\[warning\]'
            show_output "run c" "$md_log_c" 'mount|\[error\]|\[warning\]'
        fi
        rm -rf "$md_dir"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
