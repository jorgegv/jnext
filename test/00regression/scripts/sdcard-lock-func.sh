#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #555 - one jnext per SD card image. Two processes on one card each cache the FAT and write
# sectors the other never sees; the second jnext on a card (or on an --sdcard-writes FILE) is
# refused, whatever spelling of the path it uses, and the lock dies with its holder (kill -9
# included). Measured with the real binary, headless, on private clones; the oracle is the design
# in the issue (the message the user guide quotes, exit 1 at startup, exit 5 for --sdcard-file-add
# whose image is "unusable", a live insert of a held card refused with the current card kept).
if want sdcard-lock-func; then
    begin_func sdcard-lock-func

    faults=()
    BASE="$RUN_DIR/private/sdcard-lock"
    W="$BASE/work"
    CFG="$BASE/cfg"
    mkdir -p "$W" "$CFG"
    MASTER="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    for c in a x y z q; do cp --reflink=auto "$MASTER" "$BASE/$c.img"; done
    HOLD_PIDS=()

    # hold <label> <jnext args...>: start a long-running jnext in the background; its pid in HOLD_PID.
    # The automatic exit is only the backstop: the row ends every holder itself, so it is far longer than a run
    # under full load needs (headless runs at full speed: a short one expired mid-row, and the lock was free).
    hold() {
        local label=$1; shift
        JNEXT_CONFIG_DIR="$CFG" "$JNEXT" --headless --machine 48k --rewind-buffer-size 0 \
            "$@" --delayed-automatic-exit 900 > "$W/$label.log" 2>&1 &
        HOLD_PID=$!
        HOLD_PIDS+=("$HOLD_PID")
        local i
        for i in $(seq 150); do   # up to 30 s: it has taken its locks once it reports the card
            grep -q "SD card image mounted" "$W/$label.log" 2>/dev/null && return 0
            kill -0 "$HOLD_PID" 2>/dev/null || break
            sleep 0.2
        done
        faults+=("$label: the holder never reported its card mounted")
    }
    # try <label> <want-rc> <jnext args...>: a second jnext; its status in TRY_RC, output in $W/<label>.log
    try() {
        local label=$1 want=$2; shift 2
        TRY_RC=0
        JNEXT_CONFIG_DIR="$CFG" timeout --foreground --kill-after=5s 120s "$JNEXT" "$@" > "$W/$label.log" 2>&1 || TRY_RC=$?
        [[ "$TRY_RC" == "$want" ]] || faults+=("$label: exit $TRY_RC, wanted $want")
    }
    msg_image() { printf 'the SD card image %s is in use by another jnext (process ' "$1"; }
    stop_holder() { kill "$1" 2>/dev/null || true; wait "$1" 2>/dev/null || true; }

    # 1. the same card: refused, with the documented message (the pid is the holder's, digits)
    hold h1 --sdcard "$BASE/a.img"
    h1=$HOLD_PID
    try same 1 --headless --machine 48k --sdcard "$BASE/a.img" --delayed-automatic-exit 5 --log-file "$W/same.jlog"
    # the refusal is in the log too, not only on stderr (GH #555 review S2-3b)
    grep -qF "SD card refused: $(msg_image "$BASE/a.img")" "$W/same.jlog" 2>/dev/null \
        || faults+=("same: the refusal is not in the log file: $({ grep -h 'refused' "$W/same.jlog" 2>/dev/null || true; } | head -c 300)")
    grep -qF "$(msg_image "$BASE/a.img")" "$W/same.log" || faults+=("same: the refusal does not name the image: $(head -c 300 "$W/same.log")")
    grep -qE "\(process $h1\); give each jnext its own card \(--sdcard FILE\)" "$W/same.log" \
        || faults+=("same: the refusal does not name the holder's pid $h1 and the advice")

    # 2. other spellings of the same card are the same lock
    mkdir -p "$BASE/sub"
    ( cd "$BASE" && JNEXT_CONFIG_DIR="$CFG" timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine 48k \
        --sdcard ./a.img --delayed-automatic-exit 5 > "$W/dot.log" 2>&1 ) && faults+=("dot: ./a.img was not refused") || true
    grep -qF "is in use by another jnext" "$W/dot.log" || faults+=("dot: no refusal for ./a.img")
    try updir 1 --headless --machine 48k --sdcard "$BASE/sub/../a.img" --delayed-automatic-exit 5
    grep -qF "is in use by another jnext" "$W/updir.log" || faults+=("updir: no refusal for sub/../a.img")

    # 5. --sdcard-file-add on a held card: exit 5 (image unusable), the message, the card untouched
    before=$(sha256sum "$BASE/a.img" | cut -d' ' -f1)
    printf 'payload\n' > "$W/payload.bin"
    try fileadd 5 --sdcard "$BASE/a.img" --sdcard-file-add "$W/payload.bin" --sdcard-file-dest /LOCKED.BIN
    grep -qF "is in use by another jnext" "$W/fileadd.log" || faults+=("fileadd: no lock message")
    [[ "$(sha256sum "$BASE/a.img" | cut -d' ' -f1)" == "$before" ]] || faults+=("fileadd: the card changed although it was locked")

    # 6. a live insert of a held card is refused with the message, and the run fails
    try insert 1 --headless --machine 48k --rewind-buffer-size 0 --sdcard "$BASE/q.img" \
        --delayed-sdcard-insert-frames 5 "$BASE/a.img" --delayed-automatic-exit-frames 40
    grep -qF "SD card change refused: $(msg_image "$BASE/a.img")" "$W/insert.log" \
        || faults+=("insert: the live insert of a held card was not refused with the lock message: $({ grep -h 'refused\|in use' "$W/insert.log" || true; } | head -c 300)")
    # 6b. the same card as the SECOND slot at startup: refused, logged as the second card's
    try second 1 --headless --machine 48k --rewind-buffer-size 0 --sdcard "$BASE/q.img" --sdcard2 "$BASE/a.img" \
        --delayed-automatic-exit 5 --log-file "$W/second.jlog"
    grep -qF "second SD card refused: $(msg_image "$BASE/a.img")" "$W/second.jlog" 2>/dev/null \
        || faults+=("second: the slot-1 refusal is not in the log file: $({ grep -h 'refused' "$W/second.jlog" 2>/dev/null || true; } | head -c 300)")
    kill -0 "$h1" 2>/dev/null || faults+=("harness: the holder h1 exited before its checks finished; they tested nothing")
    stop_holder "$h1"

    # 3. the overlay FILE is locked on its own: another card, the same writes file
    hold h3 --sdcard "$BASE/x.img" --sdcard-writes "$BASE/w.jsdw"
    h3=$HOLD_PID
    try overlay 1 --headless --machine 48k --sdcard "$BASE/y.img" --sdcard-writes "$BASE/w.jsdw" --delayed-automatic-exit 5
    grep -qF "the SD-writes file $BASE/w.jsdw is in use by another jnext (process $h3)" "$W/overlay.log" \
        || faults+=("overlay: the writes-file refusal is missing or wrong: $(head -c 300 "$W/overlay.log")")
    kill -0 "$h3" 2>/dev/null || faults+=("harness: the holder h3 exited before its checks finished; they tested nothing")
    stop_holder "$h3"

    # 4. kill -9: the OS drops the lock, the next jnext starts (exit 0)
    hold h4 --sdcard "$BASE/z.img"
    h4=$HOLD_PID
    kill -9 "$h4" 2>/dev/null || true
    wait "$h4" 2>/dev/null || true
    try afterkill 0 --headless --machine 48k --sdcard "$BASE/z.img" --delayed-automatic-exit-frames 5

    # and the free card, once its holder left cleanly, is free
    try released 0 --headless --machine 48k --sdcard "$BASE/a.img" --delayed-automatic-exit-frames 5

    rm -rf "$BASE"
    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (a second jnext on a held card, in any spelling, or on a held writes file is refused with the documented message; --sdcard-file-add exits 5 and leaves the card untouched; a live insert of a held card is refused; kill -9 and a clean exit free the card)"
    else
        fail_row " (${#faults[@]} fault(s) in the SD card image lock)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
