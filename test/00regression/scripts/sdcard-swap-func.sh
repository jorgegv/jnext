#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #93 — changing the SD card while the machine runs.
#
# The Next has no card-detect line, so NextZXOS cannot notice a swap; its
# REMOUNT command asks "Remove/insert SD and press Y" and re-reads the card
# when Y comes. This row drives exactly that, end to end, headless:
#
#   swap     boot card A, type REMOUNT, insert card B (--delayed-sdcard-insert-
#            frames), press Y, then `.nexload magic.nex` — a file that exists
#            ONLY on card B. Its program writes "Hello from ZX Next!" to the
#            magic port, so the line on stderr is the guest having read card B.
#   control  the same keystrokes with no insert: the file is not on card A, so
#            the line must NOT appear. Without it, "swap printed the line" could
#            mean the file was reachable anyway.
#
# Two more parts pin the frontends' share:
#
#   refused  a headless insert that cannot happen fails the run (exit != 0),
#            as a failed --load does, and says why: a missing image, an RZX
#            recording, an insert cut off by the automatic exit, the flag
#            without --headless, an empty FILE, a malformed or negative N, a directory, and two
#            inserts in one tick.
#   readonly an insert keeps --sdcard-readonly and is mounted read-only.
#   reset    JNEXT_DELAYED_RESET_FRAMES fires a hard reset in the SAME tick as
#            the insert: the change must be made BEFORE the cold boot, which
#            then boots the new card (it would otherwise die with the machine).
#   qt       the same for QtApp, through JNEXT_HOST_PROBE=sdcard:<image>
#            (src/platform/host_probe.h): change and hard reset requested in one
#            pump; the reset must boot the NEW card from the frontend's own
#            config, and the window must be told.
#
# Every card is a private reflink clone under $RUN_DIR, the shape the screenshot
# suite's `@private-sd` sentinel uses; the harness's EXIT trap removes them. This
# row installs no trap of its own (GH #153).
if want sdcard-swap-func; then
    begin_func sdcard-swap-func

    faults=()
    BASE="$RUN_DIR/private/sdcard-swap"
    W="$BASE/work"
    mkdir -p "$W"
    MASTER="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"

    clone_a() {   # clone_a <name> — a config dir whose fallback card is a clone of A
        mkdir -p "$BASE/$1/sdcard"
        cp --reflink=auto "$MASTER" "$BASE/$1/sdcard/cspect-next-1gb-fixed.img"
    }
    clone_a swap
    clone_a control
    clone_a qt
    clone_a refused

    # Card B: card A plus MAGIC.NEX, written by jnext's own --sdcard-file-add
    # (validated by a foreign reader in sdcard-file-add-func).
    CARD_B="$BASE/card-b.img"
    cp --reflink=auto "$MASTER" "$CARD_B"
    rc=0
    timeout --foreground --kill-after=5s 120s \
        "$JNEXT" --sdcard "$CARD_B" \
                 --sdcard-file-add "$PROJECT_DIR/test/00regression/nex/magic_port_demo.nex" \
                 --sdcard-file-dest /MAGIC.NEX > "$W/add.log" 2>&1 || rc=$?
    [[ $rc -eq 0 ]] || faults+=("--sdcard-file-add onto card B exited $rc")

    # Frame-based throughout. SPACE@400 skips the welcome tour, DOWN+ENTER reach
    # the command line (as boot-nextzxos-dotls), REMOUNT is typed from 560 and
    # its prompt is up by 700; the card goes in at 720 and Y at 760. The re-read
    # takes ~280 frames; `.nexload magic.nex` is typed from 1100.
    keys=(--delayed-keypress-frames 400 space --delayed-keypress-frames 470 down
          --delayed-keypress-frames 500 enter)
    f=560
    for k in r e m o u n t enter; do
        keys+=(--delayed-keypress-frames "$f" "$k"); f=$((f + 15))
    done
    keys+=(--delayed-keypress-frames 760 y)
    f=1100
    for k in . n e x l o a d space m a g i c . n e x enter; do
        keys+=(--delayed-keypress-frames "$f" "$k"); f=$((f + 15))
    done

    remount_run() {   # remount_run <name> [extra args...]
        local name=$1 rc=0; shift
        JNEXT_CONFIG_DIR="$BASE/$name" timeout --foreground --kill-after=5s 300s \
            "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                     --magic-port 0xCAFE --magic-port-mode line \
                     "${keys[@]}" "$@" \
                     --delayed-automatic-exit-frames 1500 \
            > "$W/$name.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("$name: the NextZXOS run exited $rc")
    }
    remount_run swap --delayed-sdcard-insert-frames 720 "$CARD_B"
    remount_run control

    grep -qF "SD card inserted: '$CARD_B'" "$W/swap.log" \
        || faults+=("swap: the card change was not performed")
    grep -qF "Hello from ZX Next!" "$W/swap.log" \
        || faults+=("swap: after REMOUNT + Y, NextZXOS did not run MAGIC.NEX from card B")
    grep -qF "Hello from ZX Next!" "$W/control.log" \
        && faults+=("control: MAGIC.NEX ran from card A, so the swap result proves nothing")

    # refused: every way a headless insert can fail fails the run, saying why —
    # a missing image (refused when performed), an RZX recording (refused when
    # requested), an insert the automatic exit cuts off, and the flag without
    # --headless (rejected at parse time).
    short_run() {   # short_run <label> <expect-in-log> [jnext args...]
        local label=$1 want=$2 rc=0; shift 2
        JNEXT_CONFIG_DIR="$BASE/refused" timeout --foreground --kill-after=5s 120s \
            "$JNEXT" "$@" > "$W/$label.log" 2>&1 || rc=$?
        [[ $rc -ne 0 ]] || faults+=("$label: exited 0")
        grep -qF -- "$want" "$W/$label.log" || faults+=("$label: no '$want' in the log")
    }
    short_run missing "cannot open '$BASE/no-such-card.img'" \
        --headless --machine 48k --rewind-buffer-size 0 \
        --delayed-sdcard-insert-frames 5 "$BASE/no-such-card.img" \
        --delayed-automatic-exit-frames 20
    short_run rzx "SD card change refused: an RZX recording is being made" \
        --headless --machine 48k --rewind-buffer-size 0 --rzx-record "$W/refused.rzx" \
        --delayed-sdcard-insert-frames 5 "$CARD_B" --delayed-automatic-exit-frames 20
    short_run cutoff "--delayed-sdcard-insert-frames" \
        --headless --machine 48k --rewind-buffer-size 0 \
        --delayed-sdcard-insert-frames 100 "$CARD_B" --delayed-automatic-exit-frames 20
    # Offscreen: if the parse-time check ever regressed, no real window opens.
    QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
    short_run windowed "--delayed-sdcard-insert-frames requires --headless" \
        --delayed-sdcard-insert-frames 5 "$CARD_B"
    # An empty FILE would be an EJECT to the emulator: an unset variable in a
    # script must not pull the card and exit 0.
    short_run empty "FILE is empty" \
        --headless --machine 48k --rewind-buffer-size 0 \
        --delayed-sdcard-insert-frames 5 "" --delayed-automatic-exit-frames 20
    short_run badn "N must be a non-negative frame number" \
        --headless --machine 48k --delayed-sdcard-insert-frames 5x "$CARD_B"
    short_run negn "N must be a non-negative frame number" \
        --headless --machine 48k --delayed-sdcard-insert-frames -5 "$CARD_B"
    short_run dir "is not a file" \
        --headless --machine 48k --rewind-buffer-size 0 \
        --delayed-sdcard-insert-frames 5 "$W" --delayed-automatic-exit-frames 20
    # Two inserts due in the same tick: the second is refused, never silently
    # swapped for the first.
    short_run twice "another SD card change is already pending" \
        --headless --machine 48k --rewind-buffer-size 0 \
        --delayed-sdcard-insert-frames 5 "$CARD_B" \
        --delayed-sdcard-insert-frames 5 "$BASE/refused/sdcard/cspect-next-1gb-fixed.img" \
        --delayed-automatic-exit-frames 20
    grep -qF "SD card inserted: '$CARD_B'" "$W/twice.log" \
        || faults+=("twice: the FIRST insert did not go in")

    # The parts below must succeed.
    ok_run() {   # ok_run <label> [env...] -- [jnext args...]
        local label=$1 rc=0; shift
        local envs=()
        while [[ $1 != -- ]]; do envs+=("$1"); shift; done; shift
        env JNEXT_CONFIG_DIR="$BASE/refused" "${envs[@]}" \
            timeout --foreground --kill-after=5s 120s \
            "$JNEXT" "$@" > "$W/$label.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("$label: exited $rc")
    }
    # readonly: an insert keeps the session's --sdcard-readonly — the card is
    # MOUNTED read-only (the device's own line), not merely labelled so.
    ok_run readonly -- --headless --machine 48k --rewind-buffer-size 0 --sdcard-readonly \
        --delayed-sdcard-insert-frames 5 "$CARD_B" --delayed-automatic-exit-frames 20
    grep -qF "SD card inserted: '$CARD_B' (read-only)" "$W/readonly.log" \
        || faults+=("readonly: the inserted card is not read-only")
    grep -qF "SD image opened read-only by request: $CARD_B" "$W/readonly.log" \
        || faults+=("readonly: the inserted card was not OPENED read-only")
    # reset: a hard reset raised in the SAME tick as the insert boots the NEW
    # card — the loop owner performs the change BEFORE its cold-boot poll. The
    # boot's own mount line names the card it booted.
    ok_run reset JNEXT_DELAYED_RESET_FRAMES=5 -- --headless --machine 48k \
        --rewind-buffer-size 0 --delayed-sdcard-insert-frames 5 "$CARD_B" \
        --delayed-automatic-exit-frames 30
    grep -qF "SD card image mounted: '$CARD_B'" "$W/reset.log" \
        || faults+=("reset: the same-tick hard reset did not boot the new card")
    grep -qF "outlived a reset" "$W/reset.log" \
        && faults+=("reset: the change was serviced after the cold boot, not before")

    # qt: QtApp's poll, its order against the cold boot, its own config, and its
    # report to the window — through the probe, which requests the change and a
    # hard reset in ONE pump. Offscreen, as qt-host-order-func.
    rc=0
    JNEXT_CONFIG_DIR="$BASE/qt" JNEXT_HOST_PROBE="sdcard:$CARD_B" \
    QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
    timeout --foreground --kill-after=5s 120s \
        "$JNEXT" --silent --machine 48k --rewind-buffer-size 0 \
                 --delayed-automatic-exit-frames 150 > "$W/qt.log" 2>&1 || rc=$?
    [[ $rc -eq 0 ]] || faults+=("qt: the run exited $rc")
    grep -qF "HOSTPROBE sdcard: same-tick-reset card=new" "$W/qt.log" \
        || faults+=("qt: the hard reset did not keep the new card (no poll, or its own config not updated)")
    grep -qF "SD card image mounted: '$CARD_B'" "$W/qt.log" \
        || faults+=("qt: the same-tick hard reset did not boot the new card")
    grep -qF "outlived a reset" "$W/qt.log" \
        && faults+=("qt: the change was serviced after the cold boot, not before")
    grep -qF "status bar: SD card inserted: $CARD_B" "$W/qt.log" \
        || faults+=("qt: the window was not told the change was made")

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (REMOUNT read the inserted card; control did not; refusal and Qt poll pinned)"
    else
        fail_row " (${#faults[@]} fault(s) in the live SD-card change)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
