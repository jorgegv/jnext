#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #167 — NextSync over the emulated ESP-01, end to end, against the REAL
# third-party program.
#
# esp-loopback-func proves the transport with a purpose-built 76-byte guest.
# This row proves the PRODUCT CLAIM: that NextSync 1.2 — the tool most Next
# developers actually use to move a build onto the machine — completes a
# transfer through `--esp` and the files arrive intact. Nothing else in the
# suite runs third-party software over the ESP, and a transport that is correct
# for a hand-written 5-byte payload can still be wrong for sustained bulk
# binary traffic at 1.152 Mbaud, which is what this sends.
#
# WHAT IT ASSERTS, and the third one is the row:
#   1. the guest connected to the peer (the emulator's own `esp01` log);
#   2. the vendored server reported a completed sync with NO retries and NO
#      restarts — a transfer that only succeeds after retransmission is a
#      passing sync but a failing pacing model, and the difference matters;
#   3. both files read back out of the SD-card image are BYTE-IDENTICAL to what
#      the server served, by sha256 computed here. One is 4 KB of adversarial
#      binary (every byte value, embedded NULs, and the literal `OK` / `ERROR` /
#      `SEND OK` / `> ` / `+IPD,` response frames); the other is 16 KB, i.e. 16
#      payload packets, so a single dropped or duplicated packet fails it.
#
# WHY IT CANNOT FLAKE. No screenshot, no pixel compare, no wall-clock threshold:
# the verdict is a pair of hashes. The frame budget is EMULATED time, and CPU
# contention pushes it the safe way — a loaded box runs fewer frames per second,
# so the same budget buys MORE wall time for the transfer, never less. The only
# wall-clock number here is the `timeout`, a crash guard set ~10x over measured.
#
# NO mtools, NO new dependency: `sdfile_tool` injects the dot command and the
# config into this run's private SD clone through the in-tree FAT32 writer
# (src/core/fat32_image.h), the same read_tree/upsert/format_and_populate
# sequence sdcard_provisioner.cpp uses for MACHINES/NEXT/config.ini.
#
# The dot command is vendored unmodified and is public domain; the peer speaks
# the protocol itself so it can bind ONE address rather than the wildcard, and
# so the row has no subprocess to orphan. See nextsync/README.md.
if want nextsync-func; then
    begin_func nextsync-func

    peer_py="$SCRIPT_DIR/nextsync-peer.py"
    dot_src="$SCRIPT_DIR/nextsync/dot/sync"
    sdfile="$PROJECT_DIR/build/test/sdfile_tool"
    sd_image="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"

    syncroot="$TMP_DIR/nextsync-root"
    ready="$TMP_DIR/nextsync_ready"
    manifest="$TMP_DIR/nextsync_manifest"
    peer_log="$TMP_DIR/nextsync_peer.log"
    run_log="$TMP_DIR/nextsync_run.log"
    cfg="$TMP_DIR/nextsync.cfg"
    got_dir="$TMP_DIR/nextsync-got"
    rm -rf "$syncroot" "$got_dir"
    rm -f "$ready" "$manifest" "$peer_log" "$run_log" "$cfg"
    mkdir -p "$syncroot" "$got_dir"

    peer_pid=""
    verdict=""

    # Preconditions. A missing SD clone is a SKIP (the provision row will have
    # said why); a missing sdfile_tool is a FAILURE, because `make regression`
    # depends on unit-test-build and its absence means the build is broken —
    # exactly the rewind_test precedent, never a silently absent row.
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to host the NextSync server)"
        verdict=settled
    elif [[ ! -f "$sd_image" ]]; then
        skip_row " (no SD-card image clone for this run)"
        verdict=settled
    elif [[ ! -x "$sdfile" ]]; then
        fail_row " (build/test/sdfile_tool missing — run 'make unit-test-build')"
        verdict=settled
    fi

    if [[ -z "$verdict" ]]; then
        python3 "$peer_py" "$syncroot" "$ready" "$manifest" >"$peer_log" 2>&1 &
        peer_pid=$!
        for _ in $(seq 1 600); do
            [[ -s "$ready" ]] && break
            kill -0 "$peer_pid" 2>/dev/null || break
            sleep 0.1
        done

        if [[ ! -s "$ready" ]]; then
            wait "$peer_pid" 2>/dev/null && rc=0 || rc=$?
            peer_pid=""
            if [[ "$rc" -eq 3 ]]; then
                skip_row " ($(tr -d '\n' <"$peer_log"))"
            else
                fail_row " (peer never became ready: $(tr -d '\n' <"$peer_log"))"
            fi
            verdict=settled
        fi
    fi

    if [[ -z "$verdict" && -n "$peer_pid" ]]; then
        read -r peer_ip <"$ready"

        # The dot command reads the server address from this file, as bare
        # ASCII with no terminator — the exact bytes `.sync <addr>` writes.
        # Injecting it is what lets the row run the sync directly instead of
        # typing an IP address in one keystroke at a time.
        printf '%s' "$peer_ip" >"$cfg"

        inject_err=""
        "$sdfile" put "$sd_image" \
            dot/sync "$dot_src" \
            sys/config/nextsync.cfg "$cfg" >"$TMP_DIR/nextsync_inject.log" 2>&1 \
            || inject_err=$(tr -d '\n' <"$TMP_DIR/nextsync_inject.log")

        if [[ -n "$inject_err" ]]; then
            fail_row " (could not inject the dot command into the SD image: $inject_err)"
            verdict=settled
        fi
    fi

    if [[ -z "$verdict" && -n "$peer_pid" ]]; then
        # Boot NextZXOS to its command line and type `.sync`. The first three
        # keypresses are the ones boot-nextzxos-dotls uses to get there; the
        # RTC is pinned for the same reason it is there — a reproducible boot.
        # 4000 frames = 80 s of emulated time for a ~20 KB transfer measured at
        # ~1.5 s emulated, so the budget is ~50x the work.
        timeout --foreground --kill-after=5s 300s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" \
            --esp --esp-allow "$peer_ip" \
            --rtc 2026-07-30T12:00:00 \
            --delayed-keypress-frames 400 space \
            --delayed-keypress-frames 470 down \
            --delayed-keypress-frames 500 enter \
            --delayed-keypress-frames 560 . \
            --delayed-keypress-frames 575 s \
            --delayed-keypress-frames 590 y \
            --delayed-keypress-frames 605 n \
            --delayed-keypress-frames 620 c \
            --delayed-keypress-frames 635 enter \
            --delayed-automatic-exit-frames 4000 \
            >"$run_log" 2>&1 || true

        fails=()

        grep -aq "connection OPENED to $peer_ip:2048" "$run_log" \
            || fails+=("no 'connection OPENED' log line for $peer_ip:2048")

        # The peer prints this line only after the guest says "Bye". Its
        # retry/restart counters are on the same line, and both must be zero.
        stats=$(grep -a 'packets: ' "$peer_log" | tail -1 || true)
        if [[ -z "$stats" ]]; then
            fails+=("the peer never reported a completed sync")
        elif [[ "$stats" != *"retries: 0, restarts: 0"* ]]; then
            fails+=("transfer needed retransmission: ${stats#*| }")
        fi

        # THE assertion: every file the server served came back off the image
        # with the same sha256.
        while read -r sd_path want_hash; do
            [[ -n "$sd_path" ]] || continue
            local_name=${sd_path##*/}
            if ! "$sdfile" get "$sd_image" "$sd_path" "$got_dir/$local_name" \
                    >>"$TMP_DIR/nextsync_extract.log" 2>&1; then
                fails+=("$sd_path never arrived on the SD image")
                continue
            fi
            got_hash=$(sha256sum "$got_dir/$local_name" | cut -d' ' -f1)
            [[ "$got_hash" == "$want_hash" ]] \
                || fails+=("$sd_path is CORRUPT: sha256 $got_hash, expected $want_hash")
        done <"$manifest"

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (NextSync 1.2 synced 2 files from $peer_ip, byte-identical, no retries)"
        else
            fail_row " (${fails[*]})"
        fi
    fi

    # THE ONLY LIFECYCLE. The peer is a single process with no child of its own
    # (that is why it speaks the protocol rather than running the vendored
    # server as a subprocess), so this kill is the whole of it, and it runs on
    # every path out of the row — pass, fail, and the early-return ones above,
    # which set peer_pid empty only when the peer is already dead. No trap: the
    # suite library owns the single EXIT handler (GH #153). Belt and braces on
    # the peer's own side, for the case where the shell dies without getting
    # here: it bounds its own life and exits when it sees itself orphaned.
    # `|| true` on both is load-bearing under `set -e` — `wait` reports the
    # signal that killed it, which would otherwise abort the whole regression
    # run at the moment this row had just passed.
    if [[ -n "$peer_pid" ]]; then
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
