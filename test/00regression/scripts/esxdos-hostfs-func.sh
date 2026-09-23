#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# esxdos-hostfs-func (GH #31): a REAL guest program reading a REAL host file
# through RST $08, end to end — NEX loaded from the CLI, esxDOS F_OPEN /
# F_SEEK / F_READ / F_CLOSE serviced by --esxdos-stub-root, and the bytes the
# guest got printed back out through the magic port so the row can compare
# them to what is actually on disk.
#
# WHY THE SEEK MATTERS. The guest seeks to offset 6 before reading, and the
# expected string is the SLICE at 6..11 of a 16-byte file, not its start. A
# F_SEEK that silently did nothing (which is what the stub did before this
# change — it was hardcoded to fail with A=5) would print "012345" instead,
# and the row fails. The assertion is the file's content at an offset, which
# nothing but a working open+seek+read can produce.
#
# DISCRIMINATIVE BOTH WAYS. The identical NEX is run a second time with NO
# --esxdos-stub-root. That run MUST NOT print the file content: without a root
# there is no host directory, F_OPEN fails, and the program prints ERR. So a
# pass proves the content came from the directory this flag named, rather than
# from the NEX, the SD card, or anywhere else.
#
# THE FILESPEC IS DRIVE-QUALIFIED ("c:/data.txt"), not a bare name. That form
# is what real esxDOS software writes — asm_esx_f_open.asm documents "A=drive
# specifier (overridden if filespec includes a drive)" and NextZXOS's own ROM
# carries literals like "c:/nextzxos/autoexec.1st" — and it is the form that
# exposed a marshalling helper truncating the filespec at ':' (GH #31 review).
# A unit row cannot stand in for this one: the bug lived between guest memory
# and the sandbox, so only a real guest handing over a real filespec crosses it.
#
# THIRD ASSERTION — the sandbox, from inside the guest. After reading its
# file, the program tries to open "../escape.txt", which really does exist one
# level above the root. It must be refused (Fc=1), and the guest says which
# way it went: SANDBOX-OK or SANDBOX-ESCAPED. A path escape is a security
# defect, so it is asserted by the program that would exploit it, not only by
# a unit row calling the class.
#
# The NEX and the host files are synthesised here; nothing binary in git.
if want esxdos-hostfs-func; then
    begin_func esxdos-hostfs-func

    hf_root="$TMP_DIR/hostfs-root"
    hf_nex="$TMP_DIR/hostfs.nex"
    mkdir -p "$hf_root"
    printf '0123456789ABCDEF' > "$hf_root/data.txt"
    printf 'SECRET' > "$TMP_DIR/escape.txt"

    python3 - "$hf_nex" <<'PY'
import sys, struct

# ── A tiny two-pass assembler, so no offset is computed by hand ────────────
CODE_ORG = 0x8000
DATA = {                       # fixed data addresses, well clear of the code
    "fname":   0x8200,
    "escname": 0x8210,
    "m_ok":    0x8230,
    "m_bad":   0x8240,
    "m_err":   0x8250,
    "buf":     0x8260,
    "handle":  0x8280,
}
MAGIC = 0xCAFE

items = []                     # bytes | ("rel", label) | ("w16", label)
labels = {}

def b(*vals):      items.append(bytes(vals))
def w16(label):    items.append(("w16", label))
def rel(label):    items.append(("rel", label))
def label(name):   labels[name] = None; items.append(("label", name))

def ld_a_n(n):     b(0x3E, n)
def ld_ix(addr):   b(0xDD, 0x21, addr & 0xFF, addr >> 8)
def ld_b_n(n):     b(0x06, n)
def ld_bc(v):      b(0x01, v & 0xFF, v >> 8)
def ld_de(v):      b(0x11, v & 0xFF, v >> 8)
def ld_hl(v):      b(0x21, v & 0xFF, v >> 8)
def rst08(hook):   b(0xCF, hook)
def ld_a_mem(a):   b(0x3A, a & 0xFF, a >> 8)
def ld_mem_a(a):   b(0x32, a & 0xFF, a >> 8)
def jr_c(l):       b(0x38); rel(l)
def jr_nc(l):      b(0x30); rel(l)
def jr_nz(l):      b(0x20); rel(l)
def jr(l):         b(0x18); rel(l)
def call(l):       b(0xCD); w16(l)

F_OPEN, F_CLOSE, F_READ, F_SEEK = 0x9A, 0x9B, 0x9D, 0x9F

# F_OPEN "data.txt", esx_mode_read. A=drive, IX=filespec, B=mode.
ld_a_n(ord('*'))
ld_ix(DATA["fname"])
ld_b_n(0x01)
rst08(F_OPEN)
jr_c("err")
ld_mem_a(DATA["handle"])

# F_SEEK to 6. A=handle, BCDE=distance, IXL=esx_seek_set (0).
ld_a_mem(DATA["handle"])
ld_bc(0)
ld_de(6)
ld_ix(0x0000)
rst08(F_SEEK)
jr_c("err")

# F_READ 6 bytes. A=handle, IX=dest, BC=count.
ld_a_mem(DATA["handle"])
ld_ix(DATA["buf"])
ld_bc(6)
rst08(F_READ)
jr_c("err")

# Terminate what was read with CR,0 and print it.
ld_hl(DATA["buf"] + 6)
b(0x36, 0x0D)                  # ld (hl),13
b(0x23)                        # inc hl
b(0x36, 0x00)                  # ld (hl),0
ld_hl(DATA["buf"])
call("puts")

ld_a_mem(DATA["handle"])
rst08(F_CLOSE)

# Sandbox probe: "../escape.txt" exists one level above the root.
ld_a_n(ord('*'))
ld_ix(DATA["escname"])
ld_b_n(0x01)
rst08(F_OPEN)
jr_nc("escaped")               # Fc=0 means it OPENED — the sandbox failed
ld_hl(DATA["m_ok"])
call("puts")
jr("spin")

label("escaped")
ld_hl(DATA["m_bad"])
call("puts")
jr("spin")

label("err")
ld_hl(DATA["m_err"])
call("puts")

label("spin")
jr("spin")

# puts: HL -> asciiz, one byte per OUT to the magic port.
label("puts")
label("puts_loop")
b(0x7E)                        # ld a,(hl)
b(0xB7)                        # or a
b(0xC8)                        # ret z
ld_bc(MAGIC)
b(0xED, 0x79)                  # out (c),a
b(0x23)                        # inc hl
jr("puts_loop")

# ── Pass 1: addresses ─────────────────────────────────────────────────────
pc = CODE_ORG
for it in items:
    if isinstance(it, bytes):
        pc += len(it)
    elif it[0] == "label":
        labels[it[1]] = pc
    elif it[0] == "rel":
        pc += 1
    elif it[0] == "w16":
        pc += 2
# ── Pass 2: emit ──────────────────────────────────────────────────────────
out = bytearray()
pc = CODE_ORG
for it in items:
    if isinstance(it, bytes):
        out += it; pc += len(it)
    elif it[0] == "label":
        pass
    elif it[0] == "rel":
        target = labels[it[1]]; pc += 1
        delta = target - pc
        assert -128 <= delta <= 127, (it[1], delta)
        out.append(delta & 0xFF)
    elif it[0] == "w16":
        target = labels[it[1]]
        out += struct.pack("<H", target); pc += 2

bank2 = bytearray(16384)
bank2[0:len(out)] = out
def put(addr, raw):
    off = addr - 0x8000
    bank2[off:off + len(raw)] = raw
put(DATA["fname"],   b"c:/data.txt\0")   # DRIVE-QUALIFIED on purpose
put(DATA["escname"], b"../escape.txt\0")
put(DATA["m_ok"],    b"SANDBOX-OK\r\0")
put(DATA["m_bad"],   b"SANDBOX-ESCAPED\r\0")
put(DATA["m_err"],   b"ERR\r\0")

h = bytearray(512)
h[0:4] = b"Next"; h[4:8] = b"V1.2"
h[10] = 0                                  # screen_flags
h[12:14] = struct.pack("<H", 0xBFF0)       # SP
h[14:16] = struct.pack("<H", 0x8000)       # PC
h[18 + 2] = 1                              # bank 2 present ($8000)
open(sys.argv[1], "wb").write(bytes(h) + bytes(bank2))
PY

    hf_rc=0
    hf_out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next \
        --magic-port 0xCAFE --magic-port-mode line \
        --esxdos-stub-root "$hf_root" --load "$hf_nex" \
        --delayed-automatic-exit-frames 120 2>&1) || hf_rc=$?

    # Control: the same NEX with no root. Nothing may hand it the file.
    hf_ctl=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next \
        --magic-port 0xCAFE --magic-port-mode line \
        --esxdos-stub --load "$hf_nex" \
        --delayed-automatic-exit-frames 120 2>&1) || true

    # FOURTH ASSERTION — a root that cannot be served is FATAL. Parsing the flag
    # also turns the stub on, so continuing would quietly serve the in-memory
    # file instead and a typo in a CI script would go green with the feature
    # silently off. Same contract --rzx-record's start-up check has.
    bad_rc=0
    bad_out=$("$JNEXT" --headless --machine 48k \
        --esxdos-stub-root "$TMP_DIR/definitely-not-here" \
        --delayed-automatic-exit-frames 5 2>&1) || bad_rc=$?
    bad_msg=$(echo "$bad_out" | grep -cF -- "--esxdos-stub-root:" || true)

    # "6789AB" is bytes 6..11 of the 16-byte host file — only a working
    # F_OPEN + F_SEEK + F_READ produces it.
    hf_slice=$(echo "$hf_out" | grep -cxF "6789AB" || true)
    hf_sandbox=$(echo "$hf_out" | grep -cxF "SANDBOX-OK" || true)
    hf_escaped=$(echo "$hf_out" | grep -cxF "SANDBOX-ESCAPED" || true)
    hf_err=$(echo "$hf_out" | grep -cxF "ERR" || true)
    ctl_slice=$(echo "$hf_ctl" | grep -cxF "6789AB" || true)
    ctl_err=$(echo "$hf_ctl" | grep -cxF "ERR" || true)

    if [[ "$hf_rc" -eq 0 && "$hf_slice" -ge 1 && "$hf_sandbox" -ge 1 \
          && "$hf_escaped" -eq 0 && "$hf_err" -eq 0 \
          && "$ctl_slice" -eq 0 && "$ctl_err" -ge 1 \
          && "$bad_rc" -ne 0 && "$bad_msg" -ge 1 ]]; then
        pass_row " (drive-qualified c:/data.txt read at offset 6 via RST \$08; escape refused; no root = no content; bad root fatal)"
    else
        fail_row " (rc=$hf_rc want0, slice=$hf_slice want>=1, sandbox_ok=$hf_sandbox want>=1, escaped=$hf_escaped want0, err=$hf_err want0, ctl_slice=$ctl_slice want0, ctl_err=$ctl_err want>=1, bad_rc=$bad_rc want!=0, bad_msg=$bad_msg want>=1)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
