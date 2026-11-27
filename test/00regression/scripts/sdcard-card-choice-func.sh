#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #568 - the SD card commands (file-add, file-extract, writes-flatten, export-changes, import-changes)
# act on ONE card: card 1 by --sdcard (or --sdcard1), card 2 by --sdcard2, never both; a card-2 command
# never resolves, provisions or locks card 1, and a card-1 command never mounts or locks card 2 (not even
# the Preferences one). The five --sdcard1* spellings are the same options as their --sdcard* ones, and a
# line holding both spellings of one option is refused. Real processes on private clones; the oracle is the
# owner's design on the issue; the cards are read back with mtools, a foreign reader.
if want sdcard-card-choice-func; then
    begin_func sdcard-card-choice-func

    faults=()
    missing=()
    for tool in mdir mtype mformat python3 sha256sum od dd realpath; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done
    BASE="$RUN_DIR/private/sdcard-card-choice"
    W="$BASE/work"
    MASTER="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${missing[*]} - install mtools, python3 and coreutils)"
    elif [[ ! -f "$MASTER" ]]; then
        fail_row " (no SD-card image clone for this run)"
    else
    export MTOOLS_SKIP_CHECK=1
    mkdir -p "$W" "$BASE/host"
    HOLD_PIDS=()
    part_lba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$MASTER" | tr -d ' ')
    OFF=$((part_lba * 512))
    D_OFF=1048576
    sha() { sha256sum "$1" | cut -d' ' -f1; }
    make_card() {   # a 64 MiB synthetic card (no NextZXOS byte), FAT32 from sector 2048
        local card=$1 sectors=$(( (64 * 1024 * 1024 - 1048576) / 512 ))
        truncate -s $((64 * 1024 * 1024)) "$card"
        printf '\x0c' | dd of="$card" bs=1 seek=$((0x1BE + 4)) conv=notrunc status=none
        printf '\x00\x08\x00\x00' | dd of="$card" bs=1 seek=$((0x1BE + 8)) conv=notrunc status=none
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((sectors & 255)) $(((sectors >> 8) & 255)) $(((sectors >> 16) & 255)) $(((sectors >> 24) & 255)))" \
            | dd of="$card" bs=1 seek=$((0x1BE + 12)) conv=notrunc status=none
        printf '\x55\xaa' | dd of="$card" bs=1 seek=510 conv=notrunc status=none
        LANG=C mformat -i "$card@@1048576" -F -T "$sectors" -h 255 -s 63 -c 1 :: </dev/null >/dev/null 2>&1 \
            || faults+=("harness: mformat could not build $card")
    }
    mt() { LANG=C mtype -i "$1@@$2" "::$3" 2>/dev/null || true; }   # mt <card> <offset> <path>
    printf 'payload\n' > "$BASE/host/p.bin"
    # run <label> <want-rc> <cfg> <args...>: a jnext command in $BASE/host; output in $W/<label>.log
    run() {
        local label=$1 want=$2 cfg=$3; shift 3
        TRY_RC=0
        ( cd "$BASE/host" && JNEXT_CONFIG_DIR="$cfg" QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            timeout --foreground --kill-after=5s 120s "$JNEXT" "$@" ) > "$W/$label.log" 2>&1 || TRY_RC=$?
        [[ "$TRY_RC" == "$want" ]] || faults+=("$label: exit $TRY_RC, wanted $want: $(head -c 300 "$W/$label.log")")
    }
    log_has() { grep -qF -- "$2" "$W/$1.log" || faults+=("$1: the output lacks '$2': $(head -c 300 "$W/$1.log")"); }
    hold() {   # hold <label> <cfg> <args...>: a headless jnext holding its cards (no ZRCP)
        local label=$1 cfg=$2; shift 2
        JNEXT_CONFIG_DIR="$cfg" "$JNEXT" --headless --machine 48k --rewind-buffer-size 0 "$@" \
            --delayed-automatic-exit 900 > "$W/$label.log" 2>&1 &
        HOLD_PID=$!
        HOLD_PIDS+=("$HOLD_PID")
        local i
        for i in $(seq 150); do
            grep -q "SD card image mounted" "$W/$label.log" 2>/dev/null && return 0
            kill -0 "$HOLD_PID" 2>/dev/null || break
            sleep 0.2
        done
        faults+=("$label: the holder never reported its card mounted")
    }
    stop_holder() { kill "$1" 2>/dev/null || true; wait "$1" 2>/dev/null || true; }

    CFGE="$BASE/cfg-empty"; mkdir -p "$CFGE"
    cp --reflink=auto "$MASTER" "$BASE/a.img"
    for c in b b4 b8 b9 b10 d; do make_card "$BASE/$c.img"; done

    # 1. --sdcard1 is --sdcard
    run s1 0 "$CFGE" --sdcard1 "$BASE/a.img" --sdcard-file-add p.bin --sdcard-file-dest /S1.BIN
    [[ "$(mt "$BASE/a.img" $OFF /S1.BIN)" == "payload" ]] || faults+=("s1: --sdcard1 did not name card 1")

    # 2. card 2 with an EMPTY config dir: no default card, no download, card 1 never resolved
    run c2empty 0 "$CFGE" --sdcard2 "$BASE/b.img" --sdcard-file-add p.bin --sdcard-file-dest /C2.BIN
    [[ "$(mt "$BASE/b.img" $D_OFF /C2.BIN)" == "payload" ]] || faults+=("c2empty: /C2.BIN is not on card 2")
    [[ ! -e "$CFGE/sdcard" ]] || faults+=("c2empty: a card-2 command resolved or provisioned card 1 ($(ls "$CFGE" | tr '\n' ' '))")

    # 3. a holder on the default card of a config dir does not stop a card-2 command
    CFGD="$BASE/cfg-default"; mkdir -p "$CFGD/sdcard"
    cp --reflink=auto "$MASTER" "$CFGD/sdcard/cspect-next-1gb-fixed.img"
    sd_card_sidecars "$MASTER" "$CFGD/sdcard/cspect-next-1gb-fixed.img"
    hold hd "$CFGD"
    hd=$HOLD_PID
    run c2held 0 "$CFGD" --sdcard2 "$BASE/b.img" --sdcard-file-add p.bin --sdcard-file-dest /C2H.BIN
    [[ "$(mt "$BASE/b.img" $D_OFF /C2H.BIN)" == "payload" ]] || faults+=("c2held: /C2H.BIN is not on card 2")
    run c1held 5 "$CFGD" --sdcard-file-add p.bin
    log_has c1held "is in use by another jnext"
    kill -0 "$hd" 2>/dev/null || faults+=("harness: the holder hd exited early")
    stop_holder "$hd"

    # 4. card 2 with its writes file: the copy goes into the file, the image is untouched
    b4=$(sha "$BASE/b4.img")
    run c2w 0 "$CFGE" --sdcard2 "$BASE/b4.img" --sdcard2-writes "$BASE/w4.jsdw" --sdcard-file-add p.bin --sdcard-file-dest /W4.BIN
    [[ "$(sha "$BASE/b4.img")" == "$b4" ]] || faults+=("c2w: the image changed although a writes file was given")
    cp --reflink=auto "$BASE/b4.img" "$W/b4-applied.img"
    python3 -I "$PROJECT_DIR/test/00regression/sdcard-writes-apply.py" "$W/b4-applied.img" "$BASE/w4.jsdw" \
        && [[ "$(mt "$W/b4-applied.img" $D_OFF /W4.BIN)" == "payload" ]] || faults+=("c2w: /W4.BIN is not in the writes file")

    # 5. a held card-2 card is exit 5 for a verb (it was 1), whichever verb
    cp --reflink=auto "$MASTER" "$BASE/c.img"
    hold h2 "$CFGE" --sdcard "$BASE/c.img" --sdcard2 "$BASE/d.img"
    h2=$HOLD_PID
    run held-add 5 "$CFGE" --sdcard2 "$BASE/d.img" --sdcard-file-add p.bin
    log_has held-add "is in use by another jnext"
    run held-extract 5 "$CFGE" --sdcard2 "$BASE/d.img" --sdcard-file-extract /X
    run held-flatten 5 "$CFGE" --sdcard2 "$BASE/d.img" --sdcard2-writes "$BASE/wf.jsdw" --sdcard-writes-flatten "$BASE/flat.img"
    stop_holder "$h2"

    # 6. both cards named: refused (exit 1) with each verb
    mkdir -p "$BASE/empty-dir"
    for v in "--sdcard-file-add p.bin" "--sdcard-file-extract /X" "--sdcard-export-changes $BASE/exp-x" \
             "--sdcard-import-changes $BASE/empty-dir" "--sdcard-writes-flatten $BASE/flat-x.img"; do
        lbl="both-${v%% *}"
        # shellcheck disable=SC2086
        run "$lbl" 1 "$CFGE" --sdcard "$BASE/a.img" --sdcard2 "$BASE/b.img" $v
        log_has "$lbl" "acts on one SD card: give --sdcard (or --sdcard1) or --sdcard2, not both."
    done

    # 7. first-card options with a card-2 command are refused; a read-only card 2 is no target for a write
    run fo 1 "$CFGE" --sdcard2 "$BASE/b.img" --sdcard-writes "$BASE/x.jsdw" --sdcard-file-add p.bin
    log_has fo "--sdcard-writes is a first-card option; this command acts on the second card (--sdcard2)"
    run ro-add 1 "$CFGE" --sdcard2 "$BASE/b.img" --sdcard2-readonly --sdcard-file-add p.bin
    run ro-imp 1 "$CFGE" --sdcard2 "$BASE/b.img" --sdcard2-readonly --sdcard-import-changes "$BASE/empty-dir"
    log_has ro-add "--sdcard2-readonly"
    log_has ro-imp "--sdcard2-readonly"

    # 8. extract, export, import and flatten on card 2
    cp --reflink=auto "$BASE/b8.img" "$BASE/b8-pristine.img"
    printf 'second card file\n' > "$BASE/host/x.txt"
    run x-add 0 "$CFGE" --sdcard2 "$BASE/b8.img" --sdcard-file-add x.txt
    mkdir -p "$BASE/out"
    ( cd "$BASE/out" && JNEXT_CONFIG_DIR="$CFGE" timeout --foreground --kill-after=5s 120s "$JNEXT" --sdcard2 "$BASE/b8.img" --sdcard-file-extract /x.txt ) \
        > "$W/x-extract.log" 2>&1 || faults+=("x-extract: exit $?: $(head -c 300 "$W/x-extract.log")")
    [[ "$(cat "$BASE/out/x.txt" 2>/dev/null || true)" == "second card file" ]] || faults+=("x-extract: card 2's file was not extracted")
    run x-export 0 "$CFGE" --sdcard2 "$BASE/b8.img" --sdcard-export-changes "$BASE/exp" --sdcard-export-base "$BASE/b8-pristine.img"
    grep -q "x.txt" "$BASE/exp/CHANGES.txt" 2>/dev/null || faults+=("x-export: CHANGES.txt does not list x.txt")
    run x-import 0 "$CFGE" --sdcard2 "$BASE/b9.img" --sdcard-import-changes "$BASE/exp"
    [[ "$(mt "$BASE/b9.img" $D_OFF /x.txt)" == "second card file" ]] || faults+=("x-import: /x.txt is not on card 2")
    run x-wadd 0 "$CFGE" --sdcard2 "$BASE/b10.img" --sdcard2-writes "$BASE/w10.jsdw" --sdcard-file-add x.txt
    run x-flat 0 "$CFGE" --sdcard2 "$BASE/b10.img" --sdcard2-writes "$BASE/w10.jsdw" --sdcard-writes-flatten "$BASE/flat10.img"
    [[ "$(mt "$BASE/flat10.img" $D_OFF /x.txt)" == "second card file" ]] || faults+=("x-flat: the flattened card 2 lacks /x.txt")
    run x-flat-need 1 "$CFGE" --sdcard2 "$BASE/b10.img" --sdcard-writes-flatten "$BASE/flat11.img"
    log_has x-flat-need "--sdcard-writes-flatten requires --sdcard2-writes FILE."

    # 8b. the swap clears every card-2 option: a read-only card 2 is a source for extract, and an offline card-2 writes reset
    #     works (both were refused with "needs --sdcard2 FILE" when the swap left the card-2 variables set)
    mkdir -p "$BASE/out2"
    ( cd "$BASE/out2" && JNEXT_CONFIG_DIR="$CFGE" timeout --foreground --kill-after=5s 120s "$JNEXT" --sdcard2 "$BASE/b8.img" --sdcard2-readonly --sdcard-file-extract /x.txt ) \
        > "$W/x-ro-extract.log" 2>&1 || faults+=("x-ro-extract: exit $?: $(head -c 300 "$W/x-ro-extract.log")")
    [[ "$(cat "$BASE/out2/x.txt" 2>/dev/null || true)" == "second card file" ]] || faults+=("x-ro-extract: a read-only card 2 was not extracted")
    run x-reset 0 "$CFGE" --sdcard2 "$BASE/b4.img" --sdcard2-writes "$BASE/w4.jsdw" --sdcard2-writes-reset --sdcard-file-add p.bin --sdcard-file-dest /NEW4.BIN
    cp --reflink=auto "$BASE/b4.img" "$W/b4-reset.img"
    if python3 -I "$PROJECT_DIR/test/00regression/sdcard-writes-apply.py" "$W/b4-reset.img" "$BASE/w4.jsdw"; then
        [[ "$(mt "$W/b4-reset.img" $D_OFF /NEW4.BIN)" == "payload" ]] || faults+=("x-reset: /NEW4.BIN is not in the fresh writes file")
        LANG=C mdir -i "$W/b4-reset.img@@$D_OFF" ::/W4.BIN >/dev/null 2>&1 && faults+=("x-reset: the old journal content (/W4.BIN) survived the reset")
    else
        faults+=("x-reset: the writes file is not a valid journal")
    fi

    # 9. (Qt build) the saved Preferences' second card is not mounted or locked by a card-1 command
    CFGQ="$BASE/cfg-qt"; mkdir -p "$CFGQ"
    printf '[paths]\nsd_card2_path=%s\n' "$BASE/b.img" > "$CFGQ/jnext.conf"
    cp --reflink=auto "$MASTER" "$BASE/c2.img"
    hold hq "$CFGQ" --sdcard "$BASE/c2.img" --sdcard2 "$BASE/b.img"
    hq=$HOLD_PID
    run prefs 0 "$CFGQ" --sdcard "$BASE/a.img" --sdcard-file-add p.bin --sdcard-file-dest /PREFS.BIN
    stop_holder "$hq"
    [[ "$(mt "$BASE/a.img" $OFF /PREFS.BIN)" == "payload" ]] || faults+=("prefs: /PREFS.BIN is not on card 1")

    # 10. each alias behaves like its option
    run a-ro1 1 "$CFGE" --sdcard1 "$BASE/a.img" --sdcard1-readonly --sdcard-file-add p.bin
    run a-ro0 1 "$CFGE" --sdcard "$BASE/a.img" --sdcard-readonly --sdcard-file-add p.bin
    [[ "$(grep -v '^\[' "$W/a-ro1.log")" == "$(grep -v '^\[' "$W/a-ro0.log")" ]] || faults+=("a-ro: --sdcard1-readonly and --sdcard-readonly refuse differently")
    log_has a-ro1 "--sdcard-file-add cannot be combined with --sdcard-readonly"
    a0=$(sha "$BASE/a.img")
    run a-wr 0 "$CFGE" --sdcard1 "$BASE/a.img" --sdcard1-writes "$BASE/wa.jsdw" --sdcard-file-add p.bin --sdcard-file-dest /AW.BIN
    [[ "$(sha "$BASE/a.img")" == "$a0" && -s "$BASE/wa.jsdw" ]] || faults+=("a-wr: --sdcard1-writes did not send the copy into its file")
    run a-reset 0 "$CFGE" --headless --machine 48k --rewind-buffer-size 0 --sdcard1 "$BASE/a.img" --sdcard1-writes "$BASE/wa.jsdw" \
        --sdcard1-writes-reset --delayed-automatic-exit-frames 5
    if [[ -e "$BASE/wa.jsdw" ]]; then
        [[ "$(stat -c %s "$BASE/wa.jsdw")" -le 64 ]] || faults+=("a-reset: --sdcard1-writes-reset left the writes in $BASE/wa.jsdw")
    fi
    run a-ins1 1 "$CFGE" --headless --machine 48k --rewind-buffer-size 0 --sdcard "$BASE/a.img" \
        --delayed-sdcard1-insert-frames 5 "$BASE/missing.img" --delayed-automatic-exit-frames 40
    run a-ins0 1 "$CFGE" --headless --machine 48k --rewind-buffer-size 0 --sdcard "$BASE/a.img" \
        --delayed-sdcard-insert-frames 5 "$BASE/missing.img" --delayed-automatic-exit-frames 40
    grep -qi "missing.img" "$W/a-ins1.log" && grep -qi "missing.img" "$W/a-ins0.log" \
        || faults+=("a-ins: --delayed-sdcard1-insert-frames and --delayed-sdcard-insert-frames do not refuse a missing file alike")

    # 11. an option and its alias together: refused
    run m-card 1 "$CFGE" --sdcard "$BASE/a.img" --sdcard1 "$BASE/a.img" --sdcard-file-add p.bin
    log_has m-card "--sdcard and --sdcard1 are the same option; give it once"
    run m-sd 1 "$CFGE" --sd-card "$BASE/a.img" --sdcard1 "$BASE/a.img" --sdcard-file-add p.bin
    log_has m-sd "--sd-card and --sdcard1 are the same option; give it once"
    run m-ro 1 "$CFGE" --sdcard-readonly --sdcard1-readonly --sdcard-file-extract /X
    log_has m-ro "--sdcard-readonly and --sdcard1-readonly are the same option; give it once"
    run m-wr 1 "$CFGE" --sdcard-writes "$BASE/q.jsdw" --sdcard1-writes "$BASE/q.jsdw" --sdcard-file-extract /X
    log_has m-wr "--sdcard-writes and --sdcard1-writes are the same option; give it once"
    run m-rs 1 "$CFGE" --sdcard-writes-reset --sdcard1-writes-reset --sdcard-file-extract /X
    log_has m-rs "--sdcard-writes-reset and --sdcard1-writes-reset are the same option; give it once"
    run m-di 1 "$CFGE" --delayed-sdcard-insert-frames 5 x --delayed-sdcard1-insert-frames 5 x --headless
    log_has m-di "--delayed-sdcard-insert-frames and --delayed-sdcard1-insert-frames are the same option; give it once"

    for p in "${HOLD_PIDS[@]}"; do kill "$p" 2>/dev/null || true; done
    fi
    rm -rf "$BASE"
    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (an SD card command acts on card 1 or card 2 but never both; a card-2 command never touches card 1, nor a card-1 command card 2 (Preferences included); extract, export, import and flatten work on card 2; the --sdcard1* spellings are the same options and are refused beside their twin)"
    else
        fail_row " (${#faults[@]} fault(s) in the SD card choice)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
