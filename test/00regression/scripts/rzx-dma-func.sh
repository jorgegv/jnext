#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# rzx-dma-func: an RZX recording of a program that reads its input through the
# DMA replays that input, frame after frame (GH #283).
#
# WHY THIS ROW EXISTS. An RZX stores every value the machine reads from a port
# and a playback answers those reads from the recording. The DMA's port reads
# took a path with neither hook (dma_.read_io -> PortDispatch::read()), so they
# were never recorded, and a playback read the LIVE hardware instead — here, a
# keyboard with nobody pressing it. Nothing reported it.
#
# THE FIXTURE: bin/rzx_dma_demo.bin (source and build:
# demo/rzx_dma_demo/rzx_dma_demo.asm), a 48K program — RZX recording is refused
# on a Next (GH #274), and the zxnDMA is there on every machine type. Every
# frame the CPU reads one keyboard half-row (IN), the DMA reads another 32 times
# into a buffer, and the program paints an attribute row from what the DMA read
# (PAPER) and the CPU read (INK), then moves a cursor one row on — two while
# the DMA read SPACE. The cursor moves every frame whatever the input.
#
# THE MEASUREMENT (.scr: the raw ULA screen, compared byte for byte):
#   record   four recordings with the same keys (SPACE and M on the DMA's
#            half-row, A and S on the CPU's), each with a screenshot at frame
#            60, 61, 62 or 63: the truth. The four files must be identical —
#            the recording is deterministic, so any one of them stands for all.
#   replay   the recording played with NO keys, screenshot at each of those
#            frames: every one equals the truth at the same frame.
#   moving   adjacent truth frames differ: a frozen replay cannot pass (GH #274
#            showed a static picture cannot tell the two apart).
#   dma      the truth holds cells whose PAPER came from a key the DMA read
#            (blue for SPACE, green for M), so equal pictures prove the DMA's
#            values were replayed, not merely the CPU's.
#   control  the same program run live with no keys differs from the truth, so
#            the keys — which a replay does not have — really shaped it.
if want rzx-dma-func; then
    begin_func rzx-dma-func

    rd_dir="$TMP_DIR/rzx-dma"
    rm -rf "$rd_dir"; mkdir -p "$rd_dir"
    rd_bin="$SCRIPT_DIR/bin/rzx_dma_demo.bin"
    rd_frames=(60 61 62 63)
    rd_keys=(--delayed-keypress-frames 40 space --delayed-keypress-frames 44 a
             --delayed-keypress-frames 50 m --delayed-keypress-frames 56 s)
    rd_faults=()

    # rd_run <tag> <jnext args...>: a headless 48K run; prints its exit status.
    rd_run() {
        local tag=$1 rc=0; shift
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" "$@" >"$rd_dir/$tag.log" 2>&1 || rc=$?
        echo "$rc"
    }
    # rd_dma_cells <scr>: attribute cells whose PAPER is blue (SPACE) or green
    # (M) — painted from a value the DMA read.
    rd_dma_cells() {
        od -An -tu1 -v -j 6144 -N 768 "$1" \
            | tr -s ' ' '\n' | grep -c -x -E '8|9|10|11|12|13|14|15|32|33|34|35|36|37|38|39' || true
    }

    for f in "${rd_frames[@]}"; do
        rc=$(rd_run "rec$f" --machine 48k --inject "$rd_bin" "${rd_keys[@]}" \
                 --rzx-record "$rd_dir/rec$f.rzx" \
                 --delayed-screenshot "$rd_dir/rec$f.scr" --delayed-screenshot-frames "$f" \
                 --delayed-automatic-exit-frames 70)
        [[ "$rc" == 0 && -s "$rd_dir/rec$f.rzx" && -s "$rd_dir/rec$f.scr" ]] \
            || rd_faults+=("record $f: rc=$rc or no recording/screenshot written")
    done

    if [[ ${#rd_faults[@]} -gt 0 ]]; then
        fail_row " (could not record the ground truth: $(IFS=';'; echo "${rd_faults[*]}"))"
    else
        for f in "${rd_frames[@]:1}"; do
            cmp -s "$rd_dir/rec60.rzx" "$rd_dir/rec$f.rzx" \
                || rd_faults+=("record $f: the recording differs from record 60 — not deterministic")
        done

        prev=""
        for f in "${rd_frames[@]}"; do
            rc=$(rd_run "play$f" --rzx-play "$rd_dir/rec60.rzx" \
                     --delayed-screenshot "$rd_dir/play$f.scr" --delayed-screenshot-frames "$f" \
                     --delayed-automatic-exit-frames "$((f + 1))")
            if [[ "$rc" != 0 ]]; then
                rd_faults+=("replay $f: rc=$rc")
            elif ! cmp -s "$rd_dir/rec$f.scr" "$rd_dir/play$f.scr"; then
                rd_faults+=("replay $f: differs from the recording")
            fi
            if [[ -n "$prev" ]] && cmp -s "$rd_dir/rec$prev.scr" "$rd_dir/rec$f.scr"; then
                rd_faults+=("truth $prev and $f are the same picture — a frozen replay would pass")
            fi
            prev=$f
        done

        cells=$(rd_dma_cells "$rd_dir/rec60.scr")
        [[ "$cells" -gt 0 ]] \
            || rd_faults+=("truth 60 has no cell painted from a key the DMA read — proves nothing about the DMA")

        rc=$(rd_run ctrl --machine 48k --inject "$rd_bin" \
                 --delayed-screenshot "$rd_dir/ctrl.scr" --delayed-screenshot-frames 60 \
                 --delayed-automatic-exit-frames 61)
        if [[ "$rc" != 0 ]]; then
            rd_faults+=("control: rc=$rc")
        elif cmp -s "$rd_dir/rec60.scr" "$rd_dir/ctrl.scr"; then
            rd_faults+=("control: the keyless live run equals the truth — the keys changed nothing")
        fi

        if [[ ${#rd_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${rd_faults[*]}"))"
        else
            pass_row " (a DMA-driven 48K program replays from its RZX with no keys: frames 60-63 equal the recording, adjacent frames differ, ${cells} cells come from DMA reads)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
