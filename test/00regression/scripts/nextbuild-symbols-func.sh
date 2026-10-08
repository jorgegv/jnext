#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# nextbuild-symbols-func (CAP-SRC): a NextBuild / Boriel <stem>.Memory.txt
# beside a .nex attaches when the program is loaded, without opening the
# debugger or a file dialog, in every frontend, each of which exits cleanly.
# And the symbols are really in the backend's one table: a GDB client asks
# `monitor sym PlayerTick` of the headless machine that loaded them and gets
# the address back. Discriminative: a binary without the feature attaches
# nothing and answers "no symbol".
if want nextbuild-symbols-func; then
    begin_func nextbuild-symbols-func
    fails=()
    nex="$TMP_DIR/symbol-sidecar.nex"
    mem="$TMP_DIR/symbol-sidecar.Memory.txt"
    cp "$PROJECT_DIR/test/00regression/nex/menu.nex" "$nex"
    printf '8000: ._Main\n8123: ._PlayerTick\n' > "$mem"
    want_line="Loaded 2 NextBuild symbols from '$mem'"

    # Every frontend: attached, and a clean exit.
    rc=0
    o=$(LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --silent --load "$nex" --delayed-automatic-exit-frames 10 2>&1) || rc=$?
    [[ $rc -eq 0 ]] && grep -Fq "$want_line" <<<"$o" || fails+=("headless: exit $rc, attached=$(grep -Fc "$want_line" <<<"$o" || true)")
    rc=0
    o=$(env QT_QPA_PLATFORM=offscreen LANG=C timeout --foreground --kill-after=5s 60s \
        "$JNEXT" --machine next "${SD_CARD_ARGS[@]}" --silent --load "$nex" \
        --delayed-automatic-exit-frames 10 2>&1) || rc=$?
    [[ $rc -eq 0 ]] && grep -Fq "$want_line" <<<"$o" || fails+=("Qt: exit $rc, attached=$(grep -Fc "$want_line" <<<"$o" || true)")
    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    if [[ ! -x "$sdl_bin" ]]; then
        fails+=("SDL-only binary not built: $sdl_bin; run 'make sdl-release'")
    else
        rc=0
        o=$(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LANG=C \
            timeout --foreground --kill-after=5s 60s "$sdl_bin" --machine next --silent \
            "${SD_CARD_ARGS[@]}" --load "$nex" --delayed-automatic-exit-frames 10 2>&1) || rc=$?
        [[ $rc -eq 0 ]] && grep -Fq "$want_line" <<<"$o" || fails+=("SDL: exit $rc, attached=$(grep -Fc "$want_line" <<<"$o" || true)")
    fi

    # The lookup, through a remote client, after the load.
    log="$TMP_DIR/nextbuild-symbols-gdb.log"
    if gdb_launch_cmd "$log" "$JNEXT" --headless --machine next "${SD_CARD_ARGS[@]}" \
            --silent --load "$nex" --gdb-port 0; then
        for _ in $(seq 1 100); do
            grep -Fq "$want_line" "$log" && break
            sleep 0.1
        done
        client="$TMP_DIR/nextbuild-symbols-gdb.py"
        cat >"$client" <<'PY'
import socket, sys
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=10)
def send(body):
    s.sendall(b"$" + body + b"#" + ("%02x" % (sum(body) & 0xFF)).encode())
buf = b""
def packet():
    global buf
    while True:
        i = buf.find(b"$")
        j = buf.find(b"#", i + 1) if i >= 0 else -1
        if i >= 0 and j >= 0 and len(buf) >= j + 3:
            body, buf = buf[i + 1:j], buf[j + 3:]
            s.sendall(b"+")
            return body
        buf += s.recv(4096)
send(b"qRcmd," + b"sym PlayerTick".hex().encode())
text = ""
while True:
    p = packet()
    if p.startswith(b"O") and p != b"OK":
        text += bytes.fromhex(p[1:].decode()).decode()
    else:
        break
print(text.strip())
PY
        answer=$(LANG=C timeout --foreground --kill-after=5s 20s python3 "$client" "$GDB_PORT" 2>&1) || true
        grep -Fq 'PlayerTick = $8123' <<<"$answer" ||
            fails+=("monitor sym PlayerTick answered: $(tail -1 <<<"$answer")")
    else
        fails+=("the headless jnext never logged 'gdb: listening on'")
    fi
    gdb_stop

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (Memory.txt attached on load in headless, Qt and SDL, each exits 0; monitor sym PlayerTick = \$8123)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
