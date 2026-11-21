# Helpers shared by the debugger-*-func rows (GH #548, `--debugger` / `--break-at`).
# Sourced by a row script after test-functions.inc and dzrp-functions.inc, so $JNEXT,
# $SD_CARD_ARGS, $TMP_DIR and $PROJECT_DIR are set. No trap: the harness owns the one
# EXIT handler, and every child here is reaped by dzrp_stop.
# shellcheck shell=bash

DBGSTART_NEX="$PROJECT_DIR/test/00regression/nex/dsl_demo.nex"
DBGSTART_MAP="$PROJECT_DIR/test/00regression/nex/dsl_demo.map"

# The expected PCs are read from the FILES by these, per each format's spec, never from
# jnext: a NEX header's entry PC is bytes 14-15 (little endian); a 48K .sna pushed the
# PC at the SP of its header (offset 23), the RAM starting at offset 27 for 0x4000;
# a .szx's Z80R chunk holds twelve register words with the PC last (offset 22).
dbgstart_pc_of() {
    local kind=$1 file=$2
    LANG=C python3 - "$kind" "$file" <<'PY'
import struct, sys
kind, path = sys.argv[1], sys.argv[2]
d = open(path, "rb").read()
if kind == "nex":
    pc = d[14] | d[15] << 8
elif kind == "sna":
    sp = d[23] | d[24] << 8
    pc = d[27 + sp - 0x4000] | d[27 + sp - 0x4000 + 1] << 8
elif kind == "szx":
    off, pc = 8, None
    while off + 8 <= len(d):
        cid, size = d[off:off + 4], struct.unpack("<I", d[off + 4:off + 8])[0]
        if cid == b"Z80R":
            pc = d[off + 8 + 22] | d[off + 8 + 23] << 8
            break
        off += 8 + size
    if pc is None:
        sys.exit("no Z80R chunk")
else:
    sys.exit("unknown kind " + kind)
print("0x%04x" % pc)
PY
}

# dbgstart_snapshot <file> [machine-args...] — a headless 48K run that saves a snapshot of
# the machine after 30 frames (the format by the extension), for the snapshot rows to
# start from. Returns jnext's status.
dbgstart_snapshot() {
    local file=$1
    LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --delayed-snapshot "$file" --delayed-snapshot-frames 30 \
        --delayed-automatic-exit-frames 40 >"$TMP_DIR/dbgstart-snapshot.log" 2>&1
}

# dbgstart_python — the rows need python3 for the DZRP / GDB / ZRCP client and the PC readers.
dbgstart_python() {
    command -v python3 &>/dev/null
}

# dbgstart_have_python <row> — true when python3 is there; else the row (already begun) is
# reported SKIPPED (the client and the PC readers are Python) and false.
dbgstart_have_python() {
    command -v python3 &>/dev/null && return 0
    skip_row " (python3 not available to run the DZRP client)"
    return 1
}

# dbgstart_dzrp_attempt <log> <expect_pc> <jnext command...> — launch the command (which
# must include `--dzrp-port 0`) in the background, attach the DZRP client of
# dzrp-peer.py's `startup` scenario, which expects the machine paused at <expect_pc> and
# held, then CONTINUE and PAUSE; also require the log's `debugger: stopped at $XXXX` line to
# say the same PC. Sets DBGSTART_MSG; returns 0 for a pass.
dbgstart_dzrp_attempt() {
    local log=$1 pc=$2 hex
    shift 2
    hex=$(printf '%04X' $((pc)))
    if ! dzrp_launch_cmd "$log" "$@"; then
        dzrp_stop
        DBGSTART_MSG="no 'dzrp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' ')"
        return 1
    fi
    if dzrp_peer startup "$pc"; then
        dzrp_stop
        if ! grep -aq "debugger: stopped at \$$hex (" "$log"; then
            DBGSTART_MSG="the client saw the PC, but the log has no 'debugger: stopped at \$$hex (': $(grep -a 'debugger:' "$log" | tr '\n' '|')"
            return 1
        fi
        DBGSTART_MSG="${DZRP_OUT#PASS }; $(grep -aoE 'debugger: stopped at [^ ]+ \([^)]*\)' "$log" | head -1)"
        return 0
    fi
    dzrp_stop
    DBGSTART_MSG="${DZRP_OUT#FAIL }; $(grep -a 'debugger:' "$log" | tr '\n' '|')"
    return 1
}

# dbgstart_dzrp_row <row> <expect_pc> <jnext command...> — the whole of a DZRP row.
dbgstart_dzrp_row() {
    local row=$1 pc=$2
    shift 2
    begin_func "$row"
    if ! dbgstart_have_python "$row"; then return 0; fi
    if dbgstart_dzrp_attempt "$TMP_DIR/$row.log" "$pc" "$@"; then
        pass_row " ($DBGSTART_MSG)"
    else
        fail_row " ($DBGSTART_MSG)"
    fi
}

# dbgstart_snapshot_row <row> sna|z80|szx|jns — a snapshot the row makes, started with
# --debugger. The expected PC comes from the file (sna, szx: dbgstart_pc_of; z80: the PC the
# row wrote into bytes 6-7 of a version 1 header, no jnext writer exists) or, for a .jns
# (a format the row does not read), from a .sna of the SAME frame of a second identical run.
dbgstart_snapshot_row() {
    local row=$1 kind=$2 f pc
    f="$TMP_DIR/$row.$kind"
    begin_func "$row"
    if ! dbgstart_have_python "$row"; then return 0; fi
    case $kind in
        sna|szx)
            if ! dbgstart_snapshot "$f"; then
                fail_row " (the headless run that saves the .$kind failed: $(tail -n 2 "$TMP_DIR/dbgstart-snapshot.log" | tr '\n' ' '))"
                return 0
            fi
            pc=$(dbgstart_pc_of "$kind" "$f") ;;
        jns)
            if ! dbgstart_snapshot "$f" || ! dbgstart_snapshot "$TMP_DIR/$row.control.sna"; then
                fail_row " (the headless runs that save the .jns and its control .sna failed: $(tail -n 2 "$TMP_DIR/dbgstart-snapshot.log" | tr '\n' ' '))"
                return 0
            fi
            pc=$(dbgstart_pc_of sna "$TMP_DIR/$row.control.sna") ;;
        z80)
            pc=0x9bcd
            LANG=C python3 - "$f" <<'PY'
import sys
h = bytearray(30)
h[6:8] = (0x9BCD).to_bytes(2, "little")       # PC (non-zero: a version 1 file)
h[8:10] = (0xFF00).to_bytes(2, "little")      # SP
open(sys.argv[1], "wb").write(bytes(h) + bytes(49152))   # flags byte 12 = 0: not compressed
PY
            ;;
    esac
    if dbgstart_dzrp_attempt "$TMP_DIR/$row.log" "$pc" "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 --load "$f"; then
        pass_row " ($DBGSTART_MSG)"
    else
        fail_row " ($DBGSTART_MSG)"
    fi
}
