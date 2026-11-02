#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# annotations-func (CAP-ANN, GH #387): --annotations loads the user's labels, by
# page, into the symbol table a remote client reads. Two labels are written for
# $8000, on page 4 (which a Next maps there at boot) and on page 30 (which it
# does not): `monitor sym $8000` names the first and never the second, and a
# comment record loads beside them. A second run with a file that does not parse
# exits 1 naming the line. Discriminative: a binary without the flag refuses the
# option; one that ignores pages names `Other` or nothing.
if want annotations-func; then
    begin_func annotations-func
    fails=()
    nex="$TMP_DIR/annotations.nex"
    jdp="$TMP_DIR/annotations.jdp"
    bad="$TMP_DIR/annotations-bad.jdp"
    cp "$PROJECT_DIR/test/00regression/nex/menu.nex" "$nex"
    printf '%s\n' 'jnext-annotations 1' 'label P04 $8000 Start' 'comment P04 $8000 Entry\npoint' \
        'label P1E $8000 Other' > "$jdp"
    printf '%s\n' 'jnext-annotations 1' 'label P04 $8000 Start' 'label P04 $8001 !' > "$bad"
    # Matched on the file name: under wine jnext reports the path in Windows form.
    want_line="--annotations [^ ]*annotations\.jdp: 2 labels, 1 comment"

    log="$TMP_DIR/annotations-gdb.log"
    if gdb_launch_cmd "$log" "$JNEXT" --headless --machine next "${SD_CARD_ARGS[@]}" \
            --silent --load "$nex" --annotations "$jdp" --gdb-port 0; then
        for _ in $(seq 1 100); do
            grep -Eq "$want_line" "$log" && break
            sleep 0.1
        done
        grep -Eq "$want_line" "$log" || fails+=("no '--annotations ...: 2 labels, 1 comment' line in the log")
        client="$TMP_DIR/annotations-gdb.py"
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
            "sym Start" 'sym $8000' "sym Other" "mmu" 2>&1) || true
        grep -Fq 'Start = $8000 P04' <<<"$answer" ||
            fails+=("monitor sym Start answered: $(sed -n 1p <<<"$answer")")
        grep -Fq '$8000 P04 = Start' <<<"$answer" ||
            fails+=("monitor sym \$8000 answered: $(sed -n 2p <<<"$answer") (mmu: $(tail -n +4 <<<"$answer" | tr '\n' ' '))")
        grep -Fq 'Other = $8000 P1E' <<<"$answer" ||
            fails+=("monitor sym Other answered: $(sed -n 3p <<<"$answer")")
        ! grep -Fq 'Other' <<<"$(sed -n 2p <<<"$answer")" ||
            fails+=("monitor sym \$8000 names Other, a page that is not mapped there")
    else
        fails+=("the headless jnext never logged 'gdb: listening on'")
    fi
    gdb_stop

    rc=0
    out=$(LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --silent --load "$nex" --annotations "$bad" \
            --delayed-automatic-exit-frames 10 2>&1) || rc=$?
    [[ $rc -eq 1 ]] || fails+=("a file that does not parse exited $rc, not 1")
    grep -Fq 'line 3: ' <<<"$out" || fails+=("a file that does not parse did not name its line")

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (--annotations: monitor sym Start = \$8000 P04, \$8000 = Start, Other only in P1E; a bad file exits 1 naming the line)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
