#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable; the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# z88dk-source-func (CAP-SRC, GH #346): the remote and script surface of the source map.
# The program is the committed z88dk -debug fixture (prog.nex, prog.map and prog_CODE.bin
# beside it; main.c:19 starts at $8CB5, `main` is $8C92, the loop of lines 18-21 runs 8
# times and the program then idles in lines 25-27):
#   1. `--load prog.nex --script s.jds` with `on execute main.c:19` rules: the script
#      names a source line, so it waits for the program's map (headless, Qt, SDL); each
#      logs the first hit once, at $8CB5, and the line's 8 runs, and exits 0;
#   2. the GDB server of a headless run answers `monitor src`: $8CB8 is main.c:19 in
#      P04, `monitor src main` is $8C92 main.c:15, `monitor src` alone is the PC (in the
#      idle loop, lines 25-27);
#   3. a script naming a line the map does not have (main.c:99) exits 1 and says so, in
#      headless, Qt and SDL;
#   1b. a script naming a symbol of the sidecar map (`on execute @_main`, no --map) waits
#      for the program too and fires at $8C92.
if want z88dk-source-func; then
    begin_func z88dk-source-func
    fx="$PROJECT_DIR/test/fixtures/z88dk/v2.4/sccz80-debug"
    d="$TMP_DIR/z88src"
    mkdir -p "$d"
    cp "$fx/prog.nex" "$fx/prog.map" "$fx/prog_CODE.bin" "$d/"
    cat >"$d/lines.jds" <<'JDS'
var hits = 0
on execute main.c:19 once do log "Z88SRC first main.c:19 at ${PC:x4}" end
on execute main.c:19 do set hits = hits + 1 end
on execute main.c:23 once do log "Z88SRC main.c:19 ran ${hits:d} times" end
JDS
    printf '%s\n' 'on execute main.c:99 do stop end' > "$d/bad.jds"
    # A symbol of the program's own sidecar map (no --map): it exists only once the program is loaded.
    printf '%s\n' 'on execute @_main once do log "Z88SRC sym main at ${PC:x4}" end' > "$d/sym.jds"
    fails=()

    # 1. the script in the three frontends
    run_all_script() {
        local prefix=$1 script=$2 rh=0 rq=0 rs=0 oh oq os
        oh=$(LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --silent --load "$d/prog.nex" --script "$script" \
            --delayed-automatic-exit-frames 60 2>&1) || rh=$?
        oq=$(env QT_QPA_PLATFORM=offscreen LANG=C timeout --foreground --kill-after=5s 60s \
            "$JNEXT" --machine next "${SD_CARD_ARGS[@]}" --silent --load "$d/prog.nex" \
            --script "$script" --delayed-automatic-exit-frames 60 2>&1) || rq=$?
        os="(SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release')"
        rs=1
        if [[ -x "$JNEXT_SDL" ]]; then
            rs=0
            os=$(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LANG=C \
                timeout --foreground --kill-after=5s 60s "$JNEXT_SDL" --machine next --silent \
                "${SD_CARD_ARGS[@]}" --load "$d/prog.nex" --script "$script" \
                --delayed-automatic-exit-frames 60 2>&1) || rs=$?
        fi
        printf -v "${prefix}_hl" '%s' "$oh"
        printf -v "${prefix}_qt" '%s' "$oq"
        printf -v "${prefix}_sdl" '%s' "$os"
        printf -v "${prefix}_rc" '%s' "$rh:$rq:$rs"
    }
    run_all_script g "$d/lines.jds"
    n=0
    for v in "$g_hl" "$g_qt" "$g_sdl"; do
        n=$((n + 1))
        [[ $(grep -c 'Z88SRC first main\.c:19 at 8CB5' <<<"$v") -eq 1 ]] ||
            fails+=("frontend $n: not exactly one 'first main.c:19 at 8CB5'")
        [[ $(grep -c 'Z88SRC main\.c:19 ran 8 times' <<<"$v") -eq 1 ]] ||
            fails+=("frontend $n: not exactly one 'ran 8 times'")
        grep -Eq "names a source line; it loads when the --load program has loaded" <<<"$v" ||
            fails+=("frontend $n: no 'names a source line' line")
    done
    [[ $g_rc == 0:0:0 ]] || fails+=("exit codes $g_rc, not 0:0:0")

    # 1b. a script naming a symbol of the sidecar map waits for the program too
    run_all_script y "$d/sym.jds"
    n=0
    for v in "$y_hl" "$y_qt" "$y_sdl"; do
        n=$((n + 1))
        [[ $(grep -c 'Z88SRC sym main at 8C92' <<<"$v") -eq 1 ]] ||
            fails+=("frontend $n: not exactly one 'sym main at 8C92'")
    done
    [[ $y_rc == 0:0:0 ]] || fails+=("symbol script exit codes $y_rc, not 0:0:0")

    # 2. the GDB server
    log="$TMP_DIR/z88src-gdb.log"
    if gdb_launch_cmd "$log" "$JNEXT" --headless --machine next "${SD_CARD_ARGS[@]}" \
            --silent --load "$d/prog.nex" --gdb-port 0; then
        for _ in $(seq 1 100); do
            grep -Eq "Loaded 46 z88dk source lines" "$log" && break
            sleep 0.1
        done
        grep -Eq "Loaded 46 z88dk source lines" "$log" || fails+=("no 'Loaded 46 z88dk source lines' line in the log")
        client="$TMP_DIR/z88src-gdb.py"
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
for cmd in sys.argv[2:]:
    send(b"qRcmd," + cmd.encode().hex().encode())
    text = ""
    while True:
        p = packet()
        if p.startswith(b"O") and p != b"OK":
            text += bytes.fromhex(p[1:].decode()).decode()
        else:
            break
    print(text.strip())
PY
        answer=$(LANG=C timeout --foreground --kill-after=5s 20s python3 "$client" "$GDB_PORT" \
            'src $8CB8' "src main" "src" 2>&1) || true
        [[ $(sed -n 1p <<<"$answer") == '$8CB8 main.c:19 P04' ]] ||
            fails+=("monitor src \$8CB8 answered: $(sed -n 1p <<<"$answer")")
        [[ $(sed -n 2p <<<"$answer") == '$8C92 main.c:15 P04' ]] ||
            fails+=("monitor src main answered: $(sed -n 2p <<<"$answer")")
        grep -Eq '^\$[0-9A-F]{4} main\.c:2[5-7] P04$' <<<"$(sed -n 3p <<<"$answer")" ||
            fails+=("monitor src answered: $(sed -n 3p <<<"$answer")")
    else
        fails+=("the headless jnext never logged 'gdb: listening on'")
    fi
    gdb_stop

    # 3. a line the map does not have: exit 1 in every frontend, the error logged
    run_all_script b "$d/bad.jds"
    n=0
    for v in "$b_hl" "$b_qt" "$b_sdl"; do
        n=$((n + 1))
        grep -Eq "SCRIPT ERROR [^ ]*bad\.jds:1:12: .*main\.c:99" <<<"$v" ||
            fails+=("frontend $n: no 'SCRIPT ERROR ... main.c:99' line")
    done
    [[ $b_rc == 1:1:1 ]] || fails+=("main.c:99 script exit codes $b_rc, not 1:1:1")

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (on execute main.c:19 waits for the --load program in headless, Qt and SDL and sees 8 runs; monitor src answers the line and memory; an unknown line exits 1)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
