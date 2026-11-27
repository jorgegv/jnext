#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #568 - `jnext --sdcard-file-add` on a card another jnext holds is copied THROUGH that jnext, over
# its ZRCP server, found in the lock file. Real processes, real sockets, private clones of the run's
# card. The oracle is the owner's design on the issue (the lock file lines, exit 0 with the file on the
# card, the copy's own exit statuses, a refusal with exit 5 and a sentence otherwise) and FOREIGN readers:
# mtools reads the card back, sdcard-writes-apply.py applies a writes file. Every holder is started with
# --zrcp-port 0 and a 900 s backstop, and ended by the row.
if want sdcard-forward-func; then
    begin_func sdcard-forward-func

    faults=()
    missing=()
    for tool in mdir mtype mformat python3 sha256sum od dd realpath ln; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done
    BASE="$RUN_DIR/private/sdcard-forward"
    W="$BASE/work"
    CFG="$BASE/cfg"
    MASTER="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${missing[*]} - install mtools, python3 and coreutils)"
    elif [[ ! -x "$JNEXT_SDL" ]]; then
        fail_row " (SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release')"
    elif [[ ! -f "$MASTER" ]]; then
        fail_row " (no SD-card image clone for this run)"
    else
    export MTOOLS_SKIP_CHECK=1
    mkdir -p "$W" "$CFG" "$BASE/other/sub"
    for c in a c e f g h; do cp --reflink=auto "$MASTER" "$BASE/$c.img"; done
    part_lba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$MASTER" | tr -d ' ')
    OFF=$((part_lba * 512))
    HOLD_PIDS=()

    # card 2: 64 MiB, an MBR with one FAT32 partition from sector 2048, one-sector clusters (as sdcard2-func)
    make_card() {
        local card=$1 off=1048576 sectors
        sectors=$(( (64 * 1024 * 1024 - off) / 512 ))
        truncate -s $((64 * 1024 * 1024)) "$card"
        printf '\x0c' | dd of="$card" bs=1 seek=$((0x1BE + 4)) conv=notrunc status=none
        printf '\x00\x08\x00\x00' | dd of="$card" bs=1 seek=$((0x1BE + 8)) conv=notrunc status=none
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((sectors & 255)) $(((sectors >> 8) & 255)) $(((sectors >> 16) & 255)) $(((sectors >> 24) & 255)))" \
            | dd of="$card" bs=1 seek=$((0x1BE + 12)) conv=notrunc status=none
        printf '\x55\xaa' | dd of="$card" bs=1 seek=510 conv=notrunc status=none
        LANG=C mformat -i "$card@@$off" -F -T "$sectors" -h 255 -s 63 -c 1 :: </dev/null >/dev/null 2>&1 \
            || faults+=("harness: mformat could not build $card")
    }
    make_card "$BASE/d.img"
    D_OFF=1048576
    sha() { sha256sum "$1" | cut -d' ' -f1; }

    # host files
    printf 'one\n' > "$BASE/other/sub/p.bin"
    printf 'version three\n' > "$BASE/other/v3.nex"
    ln -s v3.nex "$BASE/other/latest.nex"
    : > "$BASE/other/empty.bin"
    head -c 70000 /dev/urandom > "$BASE/other/big.bin"
    mkdir -p "$BASE/other/tree/l1/l2"
    printf 'deep\n' > "$BASE/other/tree/l1/l2/x.txt"
    printf 'two\n' > "$BASE/other/p2.bin"
    printf 'replacement\n' > "$BASE/other/new.bin"

    # hold <label> <args...>: a long-running headless jnext with ZRCP on a free port; HOLD_PID, HOLD_PORT
    hold() {
        local label=$1; shift
        JNEXT_CONFIG_DIR="$CFG" "$JNEXT" --headless --machine 48k --rewind-buffer-size 0 \
            "$@" --zrcp-port 0 --delayed-automatic-exit 900 > "$W/$label.log" 2>&1 &
        HOLD_PID=$!
        HOLD_PIDS+=("$HOLD_PID")
        wait_listening "$label"
    }
    wait_listening() {
        local label=$1 i
        HOLD_PORT=
        for i in $(seq 150); do
            HOLD_PORT=$(sed -n 's/.*zrcp: listening on 127\.0\.0\.1:\([0-9][0-9]*\).*/\1/p' "$W/$label.log" 2>/dev/null | head -1)
            [[ -n "$HOLD_PORT" ]] && return 0
            kill -0 "$HOLD_PID" 2>/dev/null || break
            sleep 0.2
        done
        faults+=("$label: the holder never reported 'zrcp: listening on'")
    }
    stop_holder() { kill "$1" 2>/dev/null || true; wait "$1" 2>/dev/null || true; }
    # fwd <label> <want-rc> <dir> <jnext args...>: the second jnext, run from <dir>
    fwd() {
        local label=$1 want=$2 dir=$3; shift 3
        TRY_RC=0
        ( cd "$dir" && JNEXT_CONFIG_DIR="$CFG" timeout --foreground --kill-after=5s 120s "$JNEXT" "$@" ) > "$W/$label.log" 2>&1 || TRY_RC=$?
        [[ "$TRY_RC" == "$want" ]] || faults+=("$label: exit $TRY_RC, wanted $want: $(head -c 300 "$W/$label.log")")
    }
    lock_of() {   # lock_of <path>: the lock file that names <path>'s canonical form
        local key f
        key=$(realpath -m "$1")
        for f in "$CFG"/locks/*.lock; do
            grep -qxF "image: $key" "$f" 2>/dev/null && { echo "$f"; return 0; }
            grep -qxF "writes: $key" "$f" 2>/dev/null && { echo "$f"; return 0; }
        done
        return 1
    }
    mt() { LANG=C mtype -i "$1@@$2" "::$3" 2>/dev/null || true; }   # mt <card> <offset> <path>
    log_has() { grep -qF -- "$2" "$W/$1.log" || faults+=("$1: the output lacks '$2': $(head -c 300 "$W/$1.log")"); }

    # 1-5. card 1: the lock file, a relative forward, a symlink, exists/force, empty/big/tree
    hold h1 --sdcard "$BASE/a.img"
    h1=$HOLD_PID; p1=$HOLD_PORT
    lk=$(lock_of "$BASE/a.img" || true)
    want=$(printf 'pid: %s\nimage: %s\nzrcp_listening_address: 127.0.0.1\nzrcp_listening_port: %s' "$h1" "$(realpath "$BASE/a.img")" "$p1")
    [[ -n "$lk" && "$(cat "$lk")" == "$want" ]] || faults+=("lock: the lock file of a.img is not the four labelled lines: $(cat "${lk:-/dev/null}" 2>/dev/null | tr '\n' '|')")

    fwd rel 0 "$BASE/other" --sdcard ../a.img --sdcard-file-add sub/p.bin
    log_has rel "(by the running jnext, process $h1)"
    fwd link 0 "$BASE/other" --sdcard ../a.img --sdcard-file-add latest.nex
    fwd again 4 "$BASE/other" --sdcard ../a.img --sdcard-file-add new.bin --sdcard-file-dest /p.bin
    fwd force 0 "$BASE/other" --sdcard ../a.img --sdcard-file-add new.bin --sdcard-file-dest /p.bin --sdcard-file-force
    fwd empty 0 "$BASE/other" --sdcard ../a.img --sdcard-file-add empty.bin
    fwd big 0 "$BASE/other" --sdcard ../a.img --sdcard-file-add big.bin
    fwd tree 0 "$BASE/other" --sdcard ../a.img --sdcard-file-add tree
    fwd first 0 "$BASE/other" --sdcard ../a.img --sdcard-file-add sub/p.bin --sdcard-file-dest /FIRST.BIN
    kill -0 "$h1" 2>/dev/null || faults+=("harness: the holder h1 exited before its checks finished")
    stop_holder "$h1"
    [[ "$(mt "$BASE/a.img" $OFF /p.bin)" == "replacement" ]] || faults+=("force: /p.bin is not the replacement")
    [[ "$(mt "$BASE/a.img" $OFF /FIRST.BIN)" == "one" ]] || faults+=("rel: /FIRST.BIN lacks the bytes")
    [[ "$(mt "$BASE/a.img" $OFF /latest.nex)" == "version three" ]] || faults+=("link: /latest.nex is not the link's target bytes")
    LANG=C mdir -i "$BASE/a.img@@$OFF" ::/v3.nex >/dev/null 2>&1 && faults+=("link: the target's own name v3.nex is on the card (the link was canonicalised)")
    [[ "$(mt "$BASE/a.img" $OFF /tree/l1/l2/x.txt)" == "deep" ]] || faults+=("tree: /tree/l1/l2/x.txt missing or wrong")
    [[ -z "$(mt "$BASE/a.img" $OFF /empty.bin)" ]] && LANG=C mdir -i "$BASE/a.img@@$OFF" ::/empty.bin >/dev/null 2>&1 \
        || faults+=("empty: /empty.bin is missing")
    LANG=C mcopy -n -i "$BASE/a.img@@$OFF" ::/big.bin "$W/big.out" 2>/dev/null || true
    cmp -s "$W/big.out" "$BASE/other/big.bin" || faults+=("big: /big.bin differs from the 70000-byte source")

    # 6-9. card 2 (image + writes locks both carry the port), mismatches, the card-modifying option
    hold h2 --sdcard "$BASE/c.img" --sdcard2 "$BASE/d.img" --sdcard2-writes "$BASE/w.jsdw"
    h2=$HOLD_PID; p2=$HOLD_PORT
    ci=$(sha "$BASE/c.img"); di=$(sha "$BASE/d.img")
    for f in "$BASE/d.img" "$BASE/w.jsdw"; do
        lk=$(lock_of "$f" || true)
        grep -qx "zrcp_listening_port: $p2" "${lk:-/dev/null}" 2>/dev/null || faults+=("card2: the lock of $f lacks the port $p2")
    done
    fwd c2 0 "$BASE/other" --sdcard2 ../d.img --sdcard2-writes ../w.jsdw --sdcard-file-add p2.bin
    fwd c2nowr 0 "$BASE/other" --sdcard2 ../d.img --sdcard-file-add p2.bin --sdcard-file-dest /NOWR.BIN
    fwd mm1 5 "$BASE/other" --sdcard ../d.img --sdcard-file-add p2.bin --sdcard-file-dest /MM1.BIN
    log_has mm1 "is card 2 in the running jnext"
    fwd mm2 5 "$BASE/other" --sdcard2 ../c.img --sdcard-file-add p2.bin --sdcard-file-dest /MM2.BIN
    log_has mm2 "is card 1 in the running jnext"
    wsha=$(sha "$BASE/w.jsdw")
    fwd wr 5 "$BASE/other" --sdcard2 ../d.img --sdcard2-writes ../other.jsdw --sdcard-file-add p2.bin --sdcard-file-dest /WR.BIN
    log_has wr "$BASE/w.jsdw"
    [[ ! -e "$BASE/other.jsdw" ]] || faults+=("wr: the refused request created other.jsdw")
    fwd reset 5 "$BASE/other" --sdcard2 ../d.img --sdcard2-writes-reset --sdcard2-writes ../w.jsdw --sdcard-file-add p2.bin --sdcard-file-dest /RS.BIN
    log_has reset "acts on the card itself"
    [[ "$(sha "$BASE/w.jsdw")" == "$wsha" ]] || faults+=("reset: the holder's writes file changed")
    kill -0 "$h2" 2>/dev/null || faults+=("harness: the holder h2 exited before its checks finished")
    stop_holder "$h2"
    [[ "$(sha "$BASE/c.img")" == "$ci" && "$(sha "$BASE/d.img")" == "$di" ]] || faults+=("card2: an image changed although all of it went into the writes file or was refused")
    # the NOWR copy went through the writes file too: the holder's card 2 has a writes file
    cp --reflink=auto "$BASE/d.img" "$W/d-applied.img"
    if python3 -I "$PROJECT_DIR/test/00regression/sdcard-writes-apply.py" "$W/d-applied.img" "$BASE/w.jsdw"; then
        [[ "$(mt "$W/d-applied.img" $D_OFF /p2.bin)" == "two" ]] || faults+=("card2: /p2.bin is not in the writes file")
        [[ "$(mt "$W/d-applied.img" $D_OFF /NOWR.BIN)" == "two" ]] || faults+=("card2: /NOWR.BIN (no writes option given) is not in the writes file")
        for n in MM1.BIN MM2.BIN WR.BIN RS.BIN; do
            LANG=C mdir -i "$W/d-applied.img@@$D_OFF" "::/$n" >/dev/null 2>&1 && faults+=("card2: the refused /$n is on the card")
        done
    else
        faults+=("card2: the writes file is not a valid journal")
    fi

    # 10. a read-only holder
    hold h3 --sdcard "$BASE/e.img" --sdcard-readonly
    h3=$HOLD_PID
    ei=$(sha "$BASE/e.img")
    fwd ro 5 "$BASE/other" --sdcard ../e.img --sdcard-file-add p2.bin
    log_has ro "read-only in the running jnext"
    stop_holder "$h3"
    [[ "$(sha "$BASE/e.img")" == "$ei" ]] || faults+=("ro: the read-only card changed")

    # 11. a holder without ZRCP
    JNEXT_CONFIG_DIR="$CFG" "$JNEXT" --headless --machine 48k --rewind-buffer-size 0 --sdcard "$BASE/f.img" \
        --delayed-automatic-exit 900 > "$W/h4.log" 2>&1 &
    h4=$!
    HOLD_PIDS+=("$h4")
    for i in $(seq 150); do grep -q "SD card image mounted" "$W/h4.log" 2>/dev/null && break; sleep 0.2; done
    fi_=$(sha "$BASE/f.img")
    fwd nozrcp 5 "$BASE/other" --sdcard ../f.img --sdcard-file-add p2.bin
    log_has nozrcp "is in use by another jnext (process $h4), which is not listening on ZRCP. Restart it with --zrcp-port PORT"
    stop_holder "$h4"
    [[ "$(sha "$BASE/f.img")" == "$fi_" ]] || faults+=("nozrcp: the held card changed")

    # 12. busy: a ZRCP client (a debugger) holds the server's one connection
    hold h5 --sdcard "$BASE/g.img"
    h5=$HOLD_PID; p5=$HOLD_PORT
    python3 -I - "$p5" > "$W/busy-client.log" 2>&1 <<'PY' &
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=10)
s.recv(4096)
print("connected", flush=True)
time.sleep(60)
PY
    busy_py=$!
    for i in $(seq 50); do grep -q connected "$W/busy-client.log" 2>/dev/null && break; sleep 0.2; done
    fwd busy 5 "$BASE/other" --sdcard ../g.img --sdcard-file-add p2.bin
    log_has busy "another ZRCP client is connected"
    kill "$busy_py" 2>/dev/null || true; wait "$busy_py" 2>/dev/null || true
    stop_holder "$h5"

    # 12b. options that act on the card itself are refused for a forwarded copy: --sdcard-sha256 on an explicit card, and
    #      --sdcard-download-force / --sdcard-upgrade on the default card (their holder runs on a config dir's default card)
    hold h8 --sdcard "$BASE/g.img"
    h8=$HOLD_PID
    gi=$(sha "$BASE/g.img")
    fwd sha 5 "$BASE/other" --sdcard ../g.img --sdcard-sha256 "$(printf 'a%.0s' $(seq 1 64))" --sdcard-file-add p2.bin
    log_has sha "--sdcard-sha256 cannot be used for a copy through a running jnext (process $h8): it acts on the card itself"
    stop_holder "$h8"
    [[ "$(sha "$BASE/g.img")" == "$gi" ]] || faults+=("sha: the card changed")
    CFGD="$BASE/cfg-default"; mkdir -p "$CFGD/sdcard"
    cp --reflink=auto "$MASTER" "$CFGD/sdcard/cspect-next-1gb-fixed.img"
    sd_card_sidecars "$MASTER" "$CFGD/sdcard/cspect-next-1gb-fixed.img"
    JNEXT_CONFIG_DIR="$CFGD" "$JNEXT" --headless --machine 48k --rewind-buffer-size 0 --zrcp-port 0 \
        --delayed-automatic-exit 900 > "$W/h9.log" 2>&1 &
    h9=$!
    HOLD_PIDS+=("$h9"); HOLD_PID=$h9
    wait_listening h9
    di=$(sha "$CFGD/sdcard/cspect-next-1gb-fixed.img")
    for opt in "--sdcard-download-force" "--sdcard-upgrade yes"; do
        lbl="dflt-${opt%% *}"
        TRY_RC=0
        # shellcheck disable=SC2086
        ( cd "$BASE/other" && JNEXT_CONFIG_DIR="$CFGD" timeout --foreground --kill-after=5s 120s "$JNEXT" $opt --sdcard-file-add p2.bin ) \
            > "$W/$lbl.log" 2>&1 || TRY_RC=$?
        [[ "$TRY_RC" == 5 ]] || faults+=("$lbl: exit $TRY_RC, wanted 5: $(head -c 300 "$W/$lbl.log")")
        log_has "$lbl" "${opt%% *} cannot be used for a copy through a running jnext (process $h9): it acts on the card itself"
    done
    stop_holder "$h9"
    [[ "$(sha "$CFGD/sdcard/cspect-next-1gb-fixed.img")" == "$di" ]] || faults+=("dflt: the default card changed")

    # 13. every frontend installs the hook: an SDL holder and a Qt holder each take one forwarded copy
    JNEXT_CONFIG_DIR="$CFG" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$JNEXT_SDL" --silent \
        --machine 48k --sdcard "$BASE/h.img" --zrcp-port 0 --delayed-automatic-exit 900 > "$W/h6.log" 2>&1 &
    h6=$!
    HOLD_PIDS+=("$h6"); HOLD_PID=$h6
    wait_listening h6
    fwd sdl 0 "$BASE/other" --sdcard ../h.img --sdcard-file-add p2.bin --sdcard-file-dest /SDL.BIN
    stop_holder "$h6"
    [[ "$(mt "$BASE/h.img" $OFF /SDL.BIN)" == "two" ]] || faults+=("sdl: /SDL.BIN is not on the SDL holder's card")
    cp --reflink=auto "$MASTER" "$BASE/q.img"
    JNEXT_CONFIG_DIR="$CFG" QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy "$JNEXT" --silent \
        --machine 48k --sdcard "$BASE/q.img" --zrcp-port 0 --delayed-automatic-exit 900 > "$W/h7.log" 2>&1 &
    h7=$!
    HOLD_PIDS+=("$h7"); HOLD_PID=$h7
    wait_listening h7
    fwd qt 0 "$BASE/other" --sdcard ../q.img --sdcard-file-add p2.bin --sdcard-file-dest /QT.BIN
    stop_holder "$h7"
    [[ "$(mt "$BASE/q.img" $OFF /QT.BIN)" == "two" ]] || faults+=("qt: /QT.BIN is not on the Qt holder's card")

    for p in "${HOLD_PIDS[@]}"; do kill "$p" 2>/dev/null || true; done
    fi
    rm -rf "$BASE"
    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (a copy forwarded to a running jnext, headless, SDL and Qt, lands on its card 1 or 2 (writes file included) with the offline exit statuses; the lock file carries the endpoint; every mismatch, read-only, busy and no-ZRCP case is exit 5 with its sentence)"
    else
        fail_row " (${#faults[@]} fault(s) in the forwarded copy)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
