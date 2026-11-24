#!/usr/bin/env bash
# Group script: the whole screenshot suite — parallel launcher, throttle,
# evaluation loop and --update reference regeneration.
# Sourced by regression.sh (the driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Diff artifacts and --update references are written here.
mkdir -p "$IMG_DIR"

# --- Screenshot tests ---
echo -e "${BOLD}Running screenshot tests...${RESET}"
echo ""

# Maximum parallel jobs: JNEXT_TEST_JOBS, default every CPU — the same lanes
# the parallel functional phase gets (regression_lanes, parallel-rows.inc).
# A screenshot row counts EMULATED frames, so load slows it without changing
# the frame it captures; the rows that pace against real time are the
# functional rows tagged `quiet`, which run after this, on their own.
MAX_JOBS=${JNEXT_TEST_JOBS:-$(nproc 2>/dev/null || echo 4)}

# Phase 1: Launch all emulator instances in parallel to generate screenshots
ORDERED_TESTS=()

while IFS= read -r line; do
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
    read -r test_name machine_type nex_file delay_frames extra_args <<< "$line"

    want "$test_name" || continue

    # A boot-timeline frame is written @bf:N (or @bf:N:K after a firmware reboot), in the frame
    # field or among the extra arguments: it is expanded with the boot-ready helper
    # (test-functions.inc). A literal frame stays a literal. On the reference card the
    # expansion is the literal, so the command line is the one the row always had.
    delay_frames=$(bf_expand "$delay_frames")
    if [[ "$extra_args" == *@bf:* ]]; then
        read -ra bf_words <<< "$extra_args"
        extra_args=""
        for bf_word in "${bf_words[@]}"; do extra_args+=" $(bf_expand "$bf_word")"; done
        extra_args=${extra_args# }
    fi

    ORDERED_TESTS+=("$test_name")

    out_img="$TMP_DIR/${test_name}.png"
    # Wall-clock safety: assume the emulator clears at least 25 fps
    # headless (real-world is much higher; this is a worst-case bound
    # so the auto-exit + timeout don't fire before the screenshot).
    # +5 s buffer lets the screenshot+quit cleanup finish.
    exit_delay=$(( delay_frames / 25 + 5 ))
    [[ $exit_delay -lt 15 ]] && exit_delay=15
    # SHOT_WALL_FACTOR (regression.sh sets it for the WebAssembly run only) widens
    # the bound for a runtime that is slower than native; the emulated exit is untouched.
    wall_timeout=$(shot_wall_bound "$exit_delay")

    cmd=("timeout" "--kill-after=5s" "${wall_timeout}s"
         "$JNEXT" "--headless"
         "${SD_CARD_ARGS[@]}"
         "--machine" "$machine_type"
         "--delayed-screenshot" "$out_img"
         "--delayed-screenshot-frames" "$delay_frames"
         "--delayed-automatic-exit" "$exit_delay")

    if [[ "$nex_file" != "BOOT" ]]; then
        cmd+=("--load" "$PROJECT_DIR/$nex_file")
    fi

    # Append extra CLI arguments (e.g. --delayed-keypress 2 0).
    #
    # `@private-sd` is a HARNESS sentinel, not a jnext flag: it is stripped
    # here and gives this row its own SD-card clone instead of the run-wide
    # one. Needed by any row whose guest WRITES to the card — jnext opens the
    # image read-write and NextZXOS writes back, and every other row in the
    # run resolves the SAME $RUN_DIR/sdcard image (see the JNEXT_CONFIG_DIR
    # note in test-functions.inc). The run-wide clone protects the MASTER from
    # the run; this protects the other ROWS from this row.
    #
    # Proven, not theoretical (GH #113): boot-nextzxos-cpm drives the NextZXOS
    # CP/M loader, which imports 31 files onto the card. Without this, the
    # later-running tape-save-boot-func and sdcard-readonly-func rows booted
    # the mutated image and their frame-400 welcome comparison failed — a
    # reproducible 2-row FAIL that looks exactly like a rendering regression.
    #
    # Opt-in rather than always-on: the clone is a reflink on the dev host but
    # degrades to a real 1 GB copy where reflink is unavailable (CI containers),
    # so only rows that need it pay.
    row_env=()
    if [[ -n "$extra_args" ]]; then
        read -ra extra_array <<< "$extra_args"
        for extra_arg in "${extra_array[@]}"; do
            if [[ "$extra_arg" == "@private-sd" ]]; then
                priv_cfg="$RUN_DIR/private/$test_name"
                mkdir -p "$priv_cfg/sdcard"
                cp --reflink=auto \
                   "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" \
                   "$priv_cfg/sdcard/cspect-next-1gb-fixed.img"
                row_env=(env "JNEXT_CONFIG_DIR=$priv_cfg")
            else
                cmd+=("$extra_arg")
            fi
        done
    fi

    # GH #555 - one jnext per card image: the rows run in parallel, and a card path is locked by
    # the jnext that boots it (under its config dir), so rows may not share the run's one clone
    # path or its config dir. A row without a clone of its own gets a config dir of its own,
    # $RUN_DIR/shots/<row>, holding a HARD LINK of the run's clone (the same inode: nothing is
    # copied, and the card's size and mtime, which the warm start keys on, are the run's), the
    # clone's distribution sidecars, and the run's warm-start cache, as parallel-rows.inc gives
    # every functional row. The command line is the same for every row (no --sdcard).
    if [[ ${#row_env[@]} -eq 0 && -f "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" ]]; then
        shot_cfg="$RUN_DIR/shots/$test_name"
        mkdir -p "$shot_cfg/sdcard"
        ln -f "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" "$shot_cfg/sdcard/" \
            || harness_fault "could not link the run's SD clone into screenshot row ${BOLD}$test_name${RESET}'s $shot_cfg/sdcard"
        for shot_sc in distro distro-declined; do
            [[ ! -f "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img.$shot_sc" ]] \
                || cp "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img.$shot_sc" "$shot_cfg/sdcard/" \
                || harness_fault "could not copy the run's SD clone sidecar $shot_sc into screenshot row ${BOLD}$test_name${RESET}'s $shot_cfg/sdcard"
        done
        if [[ -d "$RUN_DIR/warm-start" ]]; then
            cp -r --reflink=auto "$RUN_DIR/warm-start" "$shot_cfg/" \
                || harness_fault "could not copy the warm-start cache for screenshot row ${BOLD}$test_name${RESET}"
        fi
        row_env=(env "JNEXT_CONFIG_DIR=$shot_cfg")
    fi

    # Launch in background
    "${row_env[@]}" "${cmd[@]}" &>/dev/null &

    # Throttle: wait if we've reached MAX_JOBS
    while [[ $(jobs -rp | wc -l) -ge $MAX_JOBS ]]; do
        wait -n 2>/dev/null || true
    done
done < "$CONF"

# Wait for all background jobs to finish
wait 2>/dev/null || true

# Phase 2: Evaluate results (sequential, for ordered output)
for test_name in "${ORDERED_TESTS[@]}"; do
    ref_img="$IMG_DIR/${test_name}-reference.png"
    out_img="$TMP_DIR/${test_name}.png"

    CURRENT_ROW=$test_name
    printf "  %-25s " "[$test_name]"

    # Check if emulator produced output
    if [[ ! -f "$out_img" ]]; then
        fail_row " (emulator crashed or timed out)"
        continue
    fi

    if $UPDATE_MODE; then
        cp "$out_img" "$ref_img"
        echo -e "${YELLOW}UPDATED${RESET} reference"
        pass=$((pass + 1))
        continue
    fi

    if [[ ! -f "$ref_img" ]]; then
        skip_row " (no reference image — run with --update first)"
        continue
    fi

    if $HAS_COMPARE; then
        diff_pixels=$(png_diff "$out_img" "$ref_img")
        if [[ "$diff_pixels" -le "$TOLERANCE" ]]; then
            pass_row " (${diff_pixels} pixel diff)"
        else
            fail_row " (${diff_pixels} pixels differ)"
            compare "$out_img" "$ref_img" "$IMG_DIR/${test_name}-diff.png" 2>/dev/null || true
            # JNEXT_REGRESSION_SHOTS_DIR (validated by regression.sh): keep the
            # actual PNG of a failing row; $TMP_DIR is deleted on exit.
            [[ -z "${JNEXT_REGRESSION_SHOTS_DIR:-}" ]] || cp "$out_img" "$JNEXT_REGRESSION_SHOTS_DIR/${test_name}.png" || true
            continue
        fi
    else
        skip_row " (no ImageMagick)"
        continue
    fi
done

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
