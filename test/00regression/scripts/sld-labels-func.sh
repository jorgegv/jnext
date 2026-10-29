#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# sld-labels-func (CAP-SRC, GH #375): the labels of an SLD sidecar beside a
# program reach the symbol table a remote client reads, by page. Two labels
# assembled for $8000, on page 4 (which a Next maps there at boot) and on page 30
# (which it does not): `monitor sym $8000` names the first, and never the second.
# Discriminative: a binary that does not load labels logs no label line and
# answers "no symbol Start".
if want sld-labels-func; then
    begin_func sld-labels-func
    fails=()
    nex="$TMP_DIR/label-sidecar.nex"
    sld="$TMP_DIR/label-sidecar.sld"
    cp "$PROJECT_DIR/test/00regression/nex/menu.nex" "$nex"
    dev='|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344'
    printf '%s\n' '|SLD.data.version|1' "main.bas|1||0$dev" 'main.bas|1||0|4|32768|T|' \
        'main.bas|1||0|4|32768|L|,Start,' 'main.bas|2||0|30|32768|L|,Other,' > "$sld"
    # Matched on the file name: under wine jnext reports the path in Windows form.
    want_line="Loaded 2 SLD labels from '[^']*label-sidecar\.sld'"

    log="$TMP_DIR/sld-labels-gdb.log"
    if gdb_launch_cmd "$log" "$JNEXT" --headless --machine next "${SD_CARD_ARGS[@]}" \
            --silent --load "$nex" --gdb-port 0; then
        for _ in $(seq 1 100); do
            grep -Eq "$want_line" "$log" && break
            sleep 0.1
        done
        grep -Eq "$want_line" "$log" || fails+=("no 'Loaded 2 SLD labels' line in the log")
        client="$TMP_DIR/sld-labels-gdb.py"
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
            "sym Start" 'sym $8000' "mmu" 2>&1) || true
        grep -Fq 'Start = $8000' <<<"$answer" ||
            fails+=("monitor sym Start answered: $(sed -n 1p <<<"$answer")")
        grep -Fq '$8000 = Start' <<<"$answer" ||
            fails+=("monitor sym \$8000 answered: $(sed -n 2p <<<"$answer") (mmu: $(tail -n +3 <<<"$answer" | tr '\n' ' '))")
    else
        fails+=("the headless jnext never logged 'gdb: listening on'")
    fi
    gdb_stop

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (SLD sidecar labels attach on load; monitor sym Start = \$8000, \$8000 = Start, not Other)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
