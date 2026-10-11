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
    sdfile="$TEST_BIN_DIR/sdfile_tool"
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

    # On a failure, print the exchange around the FIRST Retry/Restart, from both
    # sides (GH #319): the peer's timestamped RX/TX events, and jnext's own esp01
    # debug lines (+IPD framing, flushes) within 400 ms of it, by wall clock. The
    # next failure then names its own cause: a reply that never arrived intact
    # (the peer's TX with no matching +IPD) reads differently from a late one.
    nextsync_exchange() {
        local first t t2 n_over art
        # Keep the two full logs where the CI job can upload them (build/ is the
        # workspace's, so it survives the row's $TMP_DIR; ci.yml uploads
        # build/regression-artifacts on a red run), and print the peer's WHOLE
        # exchange below -- it is ~100 lines -- so the job log alone names the cause.
        art="$PROJECT_DIR/build/regression-artifacts"
        mkdir -p "$art" 2>/dev/null \
            && cp "$peer_log" "$art/nextsync-peer-$$.log" 2>/dev/null \
            && cp "$run_log" "$art/nextsync-jnext-$$.log" 2>/dev/null \
            && echo "        --- full logs kept in build/regression-artifacts/nextsync-{peer,jnext}-$$.log ---" || true
        echo "        --- WHERE IT WENT WRONG: byte digests per hop, then the exact counts when the guest sent its Restart/Retry ---"
        python3 -I "$SCRIPT_DIR/nextsync-digest.py" "$peer_log" "$run_log" 2>&1 | sed 's/^/        |/' || true
        echo "        --- the peer's whole exchange (RX = what the guest sent, TX = the reply; times are the peer's wall clock) ---"
        head -n 400 "$peer_log" | sed 's/^/        | /'
        # The guest-visible side: the UART logs every byte it had to DROP (RX FIFO
        # full) at debug level. A dropped reply byte is a corrupt packet to the guest.
        n_over=$(grep -ac "RX FIFO overflow" "$run_log" || true)
        echo "        --- UART RX FIFO overflow lines in jnext's log: ${n_over:-0}; first 3: ---"
        grep -a "RX FIFO overflow" "$run_log" | head -n 3 | sed 's/^/        | /' || true
        first=$(grep -an "RX b'Re" "$peer_log" | head -1 | cut -d: -f1 || true)
        if [[ -z "$first" ]]; then
            echo "        --- no Retry/Restart in the peer log; its last 12 lines ---"
            tail -n 12 "$peer_log" | sed 's/^/        | /'
            return 0
        fi
        echo "        --- peer log around its first Retry/Restart (line $first) ---"
        sed -n "$(( first > 8 ? first - 8 : 1 )),$(( first + 8 ))p" "$peer_log" | sed 's/^/        | /'
        # The window starts at the peer's last event BEFORE the Restart (the reply the
        # guest then failed to act on) and ends 300 ms after the Restart; the UART's
        # per-byte TX lines are left out, they bury the rest.
        t=$(sed -n "$(( first > 1 ? first - 1 : 1 ))p" "$peer_log" | awk '{print $2}')
        echo "        --- jnext esp01/uart lines from $t (the peer's last event before the Restart) to its Restart + 300 ms ---"
        t2=$(sed -n "${first}p" "$peer_log" | awk '{print $2}')
        awk -v t="$t" -v t2="$t2" 'function ms(x,  a){ split(x, a, /[:.]/); return ((a[1]*60 + a[2])*60 + a[3])*1000 + a[4] }
            BEGIN { c = ms(t) - 30; e = ms(t2) + 300 }
            match($0, /\[[0-9][0-9]:[0-9][0-9]:[0-9][0-9]\.[0-9][0-9][0-9]\]/) {
                d = ms(substr($0, RSTART + 1, RLENGTH - 2))
                if (d > c && d < e) print
            }' "$run_log" | grep -aE "esp|uart" | grep -av "TX write" | head -n 80 | sed 's/^/        | /' || true
    }

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
        fail_row " ($TEST_BIN_DIR/sdfile_tool missing — run 'make unit-test-build')"
        verdict=settled
    fi

    if [[ -z "$verdict" ]]; then
        python3 "$peer_py" "$syncroot" "$ready" "$manifest" >"$peer_log" 2>&1 &
        peer_pid=$!
        # 90 s, and the number is not arbitrary: the peer retries a busy bind
        # for BIND_RETRY_S = 45 s (nextsync-peer.py, GH #186) before deciding
        # to exit 3, and THIS loop must outlast that decision. If it gave up
        # first the peer would still be alive, the `wait` below would block on
        # its 180 s watchdog, and a port collision would be reported as a
        # FAILURE instead of the SKIP the peer chose.
        for _ in $(seq 1 900); do
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
            --log-level info,esp01=debug,uart=debug \
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

        # The byte-digest instrumentation is part of the row (GH #319): on every run each hop's
        # digests must agree with its neighbour's, with a checkpoint count above zero on all four
        # hops, so a drifted twin (C++ vs Python) or a moved/deleted hook fails HERE and not as a
        # lying verdict in some later dump. It is also an assertion that no hop changed a byte.
        digest_bad=$(python3 -I "$SCRIPT_DIR/nextsync-digest.py" --check "$peer_log" "$run_log" 2>&1) \
            || fails+=("byte-digest check failed: ${digest_bad//$'\n'/;}")

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (NextSync 1.2 synced 2 files from $peer_ip, byte-identical, no retries)"
        else
            fail_row " (${fails[*]})"
            nextsync_exchange
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
