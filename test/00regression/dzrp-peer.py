#!/usr/bin/env python3
"""The client half of the `dzrp-*-func` regression rows (GH #12).

The nine rows of dzrp-frontend.md §7.2 drive a LIVE jnext — started headless
with `--dzrp-port` — through `tools/cspect_dzrp/cspect_dzrp.py`, jnext's own
DZRP client, written against the CSpect plugin before jnext had a server. That
is the point of using it: it is an independent reading of the wire, so a
mistake the server and its unit suite share cannot pass here unnoticed.

    dzrp-peer.py <scenario> <port> [args...]

Prints `PASS <summary>` and exits 0, or `FAIL <reason>` and exits 1. Every
wait is bounded: a silent server is a FAIL, never a hang.

THE MACHINE. Every row runs `--machine 48k`, whose MMU is the fixed
`FF FF 0A 0B 04 05 00 01` map. A scenario that needs a program writes it into
RAM at 0x8000 with CMD_WRITE_MEM and points PC at it — exactly what DeZog does
with a `.sna` — so no row depends on a demo build. Interrupts are turned ON
(IM 1, IY at the 48K ROM's system variables), so the ROM's frame interrupt
counts FRAMES (23672) while the program runs.

"THE MACHINE RAN" IS READ FROM A COUNTER THE PROGRAM KEEPS, never from FRAMES
alone before a program is loaded: a client can attach while the ROM is still
clearing RAM (a GUI frontend runs at 50 Hz, so it does), and FRAMES is then
whatever the RAM test left there. A 32-bit counter loop cannot wrap in a run.
"""

import os
import socket
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "cspect_dzrp"))
import cspect_dzrp as dz  # noqa: E402

HOST = "127.0.0.1"
TIMEOUT = 10.0
FRAMES = 23672          # the 48K ROM's 24-bit frame counter
ORG = 0x8000
STACK = 0x9F00

# ld hl,(0x9000) / inc hl / ld (0x9000),hl / jr 0x8000 — a counter loop. Its
# `inc hl` at 0x8003 is where the breakpoint rows stop.
COUNTER_LOOP = bytes([0x2A, 0x00, 0x90, 0x23, 0x22, 0x00, 0x90, 0x18, 0xF7])
COUNTER = 0x9000

# 8000 ld hl,0x9000 / inc (hl) / jr nz,8000 / inc hl / inc (hl) / jr nz,8000 /
#      inc hl / inc (hl) / jr nz,8000 / inc hl / inc (hl) / jr 8000 — a 32-bit
# little-endian counter at 0x9000, ~2000 counts a 3.5 MHz frame.
COUNTER32_LOOP = bytes([0x21, 0x00, 0x90, 0x34, 0x20, 0xFA, 0x23, 0x34, 0x20, 0xF6,
                        0x23, 0x34, 0x20, 0xF2, 0x23, 0x34, 0x18, 0xEE])


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


def connect(port, version=(2, 2, 0)):
    c = dz.CSpectDZRP(HOST, port, timeout=TIMEOUT)
    c.connect()
    info = c.init(version=version, name="jnext-regression")
    check(info.error == 0, "CMD_INIT answered error %d" % info.error)
    return c


def load(c, code, pc=ORG):
    """Put `code` at 0x8000 and aim the CPU at it, interrupts on."""
    c.write_mem(ORG, code)
    check(c.read_mem(ORG, len(code)) == code, "the program did not land at 0x8000")
    c.set_register("PC", pc)
    c.set_register("SP", STACK)
    c.set_register("IY", 0x5C3A)
    c.set_register("IM", 1)
    c.interrupt_on_off(True)


def frames(c):
    b = c.read_mem(FRAMES, 3)
    return b[0] | (b[1] << 8) | (b[2] << 16)


def counter(c):
    return struct.unpack("<H", c.read_mem(COUNTER, 2))[0]


def load_counter32(c):
    c.write_mem(COUNTER, bytes(4))
    load(c, COUNTER32_LOOP)


def counter32(c):
    return struct.unpack("<I", c.read_mem(COUNTER, 4))[0]


def pause_expecting(c, reason, addr=None, timeout=TIMEOUT):
    try:
        ntf = c.wait_for_pause(timeout=timeout)
    except TimeoutError:
        raise Fail("no NTF_PAUSE within %.0f s (expected reason %d)" % (timeout, reason))
    check(ntf.reason == reason,
          "NTF_PAUSE reason %d (%r), expected %d" % (ntf.reason, ntf.reason_string, reason))
    if addr is not None:
        check(ntf.long_address & 0xFFFF == addr,
              "NTF_PAUSE at 0x%04X, expected 0x%04X" % (ntf.long_address & 0xFFFF, addr))
    return ntf


def no_pause_for(c, seconds):
    try:
        ntf = c.wait_for_pause(timeout=seconds)
    except TimeoutError:
        return
    raise Fail("an unexpected NTF_PAUSE: reason %d at 0x%04X %r"
               % (ntf.reason, ntf.long_address & 0xFFFF, ntf.reason_string))


def bank_byte(ntf):
    return (ntf.long_address >> 16) & 0xFF


# ---------------------------------------------------------------------------
# Raw framing, for the rows that are ABOUT the frame
# ---------------------------------------------------------------------------


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise Fail("the server closed the connection")
        buf += chunk
    return buf


def raw_exchange(s, seq, cmd, payload):
    """Send one command; return (response length field, seq, payload)."""
    s.sendall(struct.pack("<IBB", len(payload), seq, cmd) + payload)
    length = struct.unpack("<I", recv_exact(s, 4))[0]
    body = recv_exact(s, length)
    return length, body[0], body[1:]


# ---------------------------------------------------------------------------
# The §7.2 scenarios
# ---------------------------------------------------------------------------


def sc_loopback(port):
    """CMD_LOOPBACK 1..8192 bytes, both length conventions, seq wrap 255 -> 1."""
    s = socket.create_connection((HOST, port), timeout=TIMEOUT)
    s.settimeout(TIMEOUT)
    try:
        # Machine-free: served before any CMD_INIT.
        sizes = (1, 2, 255, 256, 1024, 2048, 8191, 8192)
        seq = 0
        for n in sizes:
            seq = seq % 255 + 1
            data = bytes((i * 7 + n) & 0xFF for i in range(n))
            length, rseq, echo = raw_exchange(s, seq, dz.Cmd.LOOPBACK, data)
            # The COMMAND's length counted the payload only; the RESPONSE's
            # counts from the seq byte: the two conventions, both at once.
            check(length == n + 1, "a %d-byte echo came back with length %d, expected %d"
                  % (n, length, n + 1))
            check(rseq == seq, "seq %d echoed as %d" % (seq, rseq))
            check(echo == data, "the %d-byte echo differs" % n)
        # One past the maximum: declined in-band (an empty echo), and the
        # connection stays in sync for the next command.
        seq = seq % 255 + 1
        length, rseq, echo = raw_exchange(s, seq, dz.Cmd.LOOPBACK, bytes(8193))
        check(length == 1 and rseq == seq and echo == b"",
              "an 8193-byte loopback was answered with %d payload bytes" % len(echo))
        # The sequence number wraps 255 -> 1, skipping the notifications' 0,
        # and every one is echoed verbatim.
        seen = []
        for _ in range(300):
            seq = seq % 255 + 1
            _l, rseq, echo = raw_exchange(s, seq, dz.Cmd.LOOPBACK, bytes([seq]))
            check(rseq == seq and echo == bytes([seq]), "seq %d echoed as %d" % (seq, rseq))
            seen.append(seq)
        check(255 in seen and seen[seen.index(255) + 1] == 1, "the seq never wrapped 255 -> 1")
    finally:
        s.close()
    return "%d sizes 1..8192 exact with response length = payload + 1; 8193 declined " \
           "in sync; 300 seqs echoed across the 255->1 wrap" % len(sizes)


def sc_init_regs(port):
    """INIT as 2.0.0 and 2.2.0; GET_SUPPORTED_COMMANDS; GET_REGISTERS; seq echo."""
    out = []
    for ver in ((2, 0, 0), (2, 2, 0)):
        c = dz.CSpectDZRP(HOST, port, timeout=TIMEOUT)
        c.connect()
        info = c.init(version=ver, name="jnext-regression")
        check(info.error == 0, "INIT as %s: error %d" % (ver, info.error))
        check(info.dzrp_version == (2, 2, 0),
              "INIT as %s answered DZRP %s, expected 2.2.0" % (ver, info.dzrp_version))
        check(info.machine_type == 4, "machine type %d, expected 4 (ZXNEXT)" % info.machine_type)
        check(info.program_name.startswith("jnext v"), "program name %r" % info.program_name)
        c.close()
        out.append("%d.%d.%d" % ver)

    c = connect(port)
    try:
        bits = c.get_supported_commands()
        check(bits == bytes([0xDE, 0x8F, 0xBF, 0x1F, 0x80, 0x0F, 0x0C]),
              "GET_SUPPORTED_COMMANDS answered %s" % bits.hex(" "))
        for clear in (5, 12, 13, 14, 22):
            check(not (bits[clear // 8] >> (clear % 8)) & 1, "bit %d is set" % clear)

        raw = c.request(dz.Cmd.GET_REGISTERS)
        check(len(raw) == 37, "GET_REGISTERS answered %d bytes, expected 37" % len(raw))
        check(raw[28] == 8, "slot count %d" % raw[28])
        check(list(raw[29:37]) == [0xFF, 0xFF, 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01],
              "48K slots %s" % raw[29:37].hex(" "))

        # 270 more commands: after INIT, GET_SUPPORTED_COMMANDS and
        # GET_REGISTERS (seqs 1-3) the seq runs 4..255 and wraps to 1..18, each
        # reply's seq checked by the client (it raises on a mismatch).
        for _ in range(270):
            c.get_tbblue_reg(0x00)
        check(c._seqno == 18, "the client's seq is %d, not 18: the wrap was not crossed"
              % c._seqno)

        # A CMD_INIT with no version is refused in its error field.
        bad = c.request(dz.Cmd.INIT)
        check(len(bad) >= 5 and bad[0] == 1, "a version-less INIT answered error %d"
              % (bad[0] if bad else -1))
    finally:
        c.close()
    return ("INIT as %s answered 2.2.0/ZXNEXT/'jnext v'; bitfield DE 8F BF 1F 80 0F 0C, "
            "bits 5/12/13/14/22 clear; 37-byte registers, slots FF FF 0A 0B 04 05 00 01; "
            "270 seqs across the wrap; version-less INIT error 1" % " and ".join(out))


def sc_bp_continue(port):
    """Breakpoint, continue, NTF reason 2 + bank byte; temp at the same address -> 0."""
    c = connect(port)
    try:
        load(c, COUNTER_LOOP)
        bp = c.add_breakpoint(0x8003)
        check(bp != 0, "ADD_BREAKPOINT refused")
        c.cont()
        n1 = pause_expecting(c, dz.BreakReason.BREAKPOINT_HIT, 0x8003)
        # Slot 4 (0x8000) holds page 4: the bank byte is page + 1.
        check(bank_byte(n1) == 5, "bank byte %d, expected 5" % bank_byte(n1))
        # A CONTINUE from the breakpoint with a TEMPORARY at the same address:
        # the temporary wins (F8), so DeZog's step is reported as a step.
        c.cont(tmp_bp1=0x8003)
        pause_expecting(c, dz.BreakReason.NO_REASON, 0x8003)
        c.remove_breakpoint(bp)
        c.cont(tmp_bp1=0x8007)
        pause_expecting(c, dz.BreakReason.NO_REASON, 0x8007)

        # A BANKED breakpoint fires only with its page at the PC's slot.
        good = c.add_breakpoint(0x8003, bank=5)
        c.cont()
        n2 = pause_expecting(c, dz.BreakReason.BREAKPOINT_HIT, 0x8003)
        c.remove_breakpoint(good)
        wrong = c.add_breakpoint(0x8003, bank=8)       # page 7: not mapped there
        check(wrong != 0, "a breakpoint on page 7 was refused")
        before = counter(c)
        c.cont()
        no_pause_for(c, 1.0)
        c.pause()
        pause_expecting(c, dz.BreakReason.MANUAL_BREAK)
        check(counter(c) != before, "the loop did not run while the page-7 breakpoint was set")
    finally:
        c.close()
    return ("bp 0x8003 -> NTF 2 bank byte 5; temp at the same address -> NTF 0; temp at "
            "0x8007 -> NTF 0; banked bp page 4 hits (bank byte %d), page 7 never does"
            % bank_byte(n2))


# ld a,0x55 / ld (0x8FFF),a / ld (0x90FF),a / ld (0x9100),a / ld a,(0x9000) / jr $
WATCH_PROG = bytes([0x3E, 0x55, 0x32, 0xFF, 0x8F, 0x32, 0xFF, 0x90, 0x32, 0x00, 0x91,
                    0x3A, 0x00, 0x90, 0x18, 0xFE])


def sc_watch(port):
    """A 256-byte write watch: the byte before and the byte after never stop it."""
    c = connect(port)
    try:
        c.write_mem(0x8FF0, bytes(0x120))
        load(c, WATCH_PROG)
        check(c.add_watchpoint(0x9000, 0x100, 2) == 0, "ADD_WATCHPOINT refused")
        c.cont()
        n = pause_expecting(c, dz.BreakReason.WATCHPOINT_WRITE, 0x90FF)
        check(bank_byte(n) == 5, "bank byte %d, expected 5" % bank_byte(n))
        check(c.read_mem(0x8FFF, 1) == b"\x55", "the write to 0x8FFF (one before) never ran")
        # The write one PAST the area, and a READ inside it, must not stop a
        # WRITE watch.
        c.cont()
        no_pause_for(c, 1.0)
        c.pause()
        pause_expecting(c, dz.BreakReason.MANUAL_BREAK)
        check(c.read_mem(0x9100, 1) == b"\x55", "the write to 0x9100 (one past) never ran")
        c.remove_watchpoint(0x9000, 0x100, 2)
        # A READ watch on the area's first byte.
        check(c.add_watchpoint(0x9000, 1, 1) == 0, "the read watch was refused")
        c.set_register("PC", 0x800B)
        c.cont()
        pause_expecting(c, dz.BreakReason.WATCHPOINT_READ, 0x9000)
    finally:
        c.close()
    return ("write watch 0x9000+256: one NTF 4 at 0x90FF (bank byte 5); 0x8FFF and 0x9100 "
            "written without a stop; a read watch -> NTF 3 at 0x9000")


def regs_tuple(r):
    return (r.PC, r.SP, r.AF, r.BC, r.DE, r.HL, r.IX, r.IY, r.AFp, r.BCp, r.DEp, r.HLp)


def refused_restore(c, blob, what):
    c.write_state(blob)
    ntf = pause_expecting(c, dz.BreakReason.OTHER)
    check("no state to restore" in ntf.reason_string,
          "WRITE_STATE of %s: NTF text %r" % (what, ntf.reason_string))


def sc_state(port):
    """READ_STATE token, restore, the mid-frame refusal and the no-latch rule."""
    c = connect(port)
    try:
        saved = c.get_registers()
        token = c.read_state()
        check(token[:4] == b"JNXB" and len(token) > 4, "READ_STATE answered %r" % token)
        c.set_register("BC", saved.BC ^ 0x1234)
        c.write_state(token)
        after = c.get_registers()
        check(regs_tuple(after) == regs_tuple(saved),
              "after WRITE_STATE the registers are %s, saved %s" % (after.format(), saved.format()))

        # A breakpoint stop is mid-frame: the state cannot be saved there.
        load(c, COUNTER_LOOP)
        bp = c.add_breakpoint(0x8003)
        c.cont()
        pause_expecting(c, dz.BreakReason.BREAKPOINT_HIT, 0x8003)
        check(c.read_state() == b"", "READ_STATE after a breakpoint hit was not refused")
        at = c.get_registers()
        refused_restore(c, b"", "0 bytes")
        refused_restore(c, b"garbage!", "garbage")
        refused_restore(c, b"JNXB" + b"dzrp-9999-7", "an unissued token")
        check(regs_tuple(c.get_registers()) == regs_tuple(at), "a refused restore changed the registers")
        # No latch: the machine still runs.
        c.remove_breakpoint(bp)
        c.cont(tmp_bp1=0x8007)
        pause_expecting(c, dz.BreakReason.NO_REASON, 0x8007)
    finally:
        c.close()
    return ("JNXB token saved and restored exactly; mid-frame READ_STATE empty; empty, garbage "
            "and unissued WRITE_STATE -> NTF 255 'no state to restore', registers unchanged; "
            "CONTINUE still runs")


def sc_bank(port):
    """WRITE_BANK / WRITE_BANK_MEM / SET_SLOT / READ_MEM / READ_BANK_MEM."""
    c = connect(port)
    try:
        slots = c.get_slots()
        pattern = bytes((i * 7 + 3) & 0xFF for i in range(8192))
        err, msg = c.write_bank(14, pattern)
        check(err == 0, "WRITE_BANK 14: error %d %r" % (err, msg))
        c.write_bank_mem(14, 0x100, b"\xDE\xAD\xBE\xEF")
        want = pattern[:0x100] + b"\xDE\xAD\xBE\xEF" + pattern[0x104:]
        c.set_slot(6, 14)
        got = c.read_mem(0xC000, 0x200)
        check(got == want[:0x200], "page 14 through slot 6 differs from what was written")
        edge = c.read_bank_mem(14, 0x1FF0, 0x20)
        check(edge == want[0x1FF0:0x2000],
              "READ_BANK_MEM across the page edge answered %d bytes" % len(edge))
        err, msg = c.write_bank(250, pattern)
        check(err != 0 and msg, "WRITE_BANK 250 answered error %d %r" % (err, msg))
        rom = c.read_bank_mem(0xFF, 0, 16)
        check(rom == c.read_mem(0x0000, 16) and len(rom) == 16,
              "READ_BANK_MEM 255 is not the ROM the CPU sees at 0x0000")
        c.set_slot(6, slots[6])
    finally:
        c.close()
    return ("WRITE_BANK 14 + WRITE_BANK_MEM 14@0x100 read back through SET_SLOT 6,14 at "
            "0xC000; READ_BANK_MEM 14@0x1FF0 x32 -> 16 bytes; WRITE_BANK 250 -> %r; "
            "bank 255 = the ROM at 0x0000" % msg)


def sc_brkint(port):
    """ENABLE_BREAK_ON_INTERRUPT: 1 stops at the IM 1 entry, 0 does not."""
    c = connect(port)
    try:
        load(c, COUNTER_LOOP)
        c.enable_break_on_interrupt(True)
        c.cont()
        n = pause_expecting(c, dz.BreakReason.OTHER, 0x0038)
        check(n.reason_string == "Break on interrupt.", "NTF text %r" % n.reason_string)
        c.enable_break_on_interrupt(False)
        f0 = frames(c)
        c.cont()
        no_pause_for(c, 1.0)
        c.pause()
        pause_expecting(c, dz.BreakReason.MANUAL_BREAK)
        f1 = frames(c)
        check(f1 - f0 >= 5, "only %d frames ran with break-on-interrupt off" % (f1 - f0))
    finally:
        c.close()
    return ("on: NTF 255 'Break on interrupt.' at 0x0038; off: %d frames without a stop"
            % (f1 - f0))


def sc_unsupported(port):
    """Unsupported ids answered seq-only, the connection alive; legacy served."""
    c = connect(port)
    try:
        for cmd, payload in ((13, b"\x01\x00\x80\x00"), (22, b"\x00\x00"), (99, b"")):
            got = c.request(cmd, payload)
            check(got == b"", "command %d answered %d payload bytes" % (cmd, len(got)))
        check(c.loopback(b"alive") == b"alive", "the connection is not in sync afterwards")
        c.set_border(2)
        pattern = bytes((i * 5 + 1) & 0xFF for i in range(8192))
        err, msg = c.write_bank(3, pattern)
        check(err == 0, "legacy WRITE_BANK from a 2.2.0 client: error %d %r" % (err, msg))
        check(c.read_bank_mem(3, 0, 16) == pattern[:16], "the legacy WRITE_BANK did not land")
    finally:
        c.close()
    return "ids 13/22/99 seq-only, in sync; legacy SET_BORDER and WRITE_BANK served to 2.2.0"


def sc_close_resume(port):
    """Pause, CLOSE -> the machine runs again; INIT again on the connection; reconnect."""
    c = connect(port)
    try:
        load_counter32(c)
        n0 = counter32(c)
        time.sleep(0.3)
        check(counter32(c) == n0, "the program ran while CMD_INIT held the machine")
        check(c.request(dz.Cmd.CLOSE) == b"", "CMD_CLOSE answered with a payload")
        time.sleep(0.5)
        c.init(name="jnext-regression")
        n1 = counter32(c)
        check(n1 - n0 >= 10000, "the counter moved %d after CMD_CLOSE" % (n1 - n0))
    finally:
        c.close()
    time.sleep(0.5)
    c = connect(port)
    try:
        n2 = counter32(c)
        check(n2 - n1 >= 10000, "the counter moved %d after the client closed" % (n2 - n1))
        check(len(c.request(dz.Cmd.GET_REGISTERS)) == 37, "the new client got no register block")
    finally:
        c.close()
    return ("held: the counter still for 0.3 s; after CLOSE it ran %d; after a close and "
            "reconnect %d more" % (n1 - n0, n2 - n1))


# ---------------------------------------------------------------------------
# dzrp-sdl-func / dzrp-qt-func — the server on the GUI loop owners
# ---------------------------------------------------------------------------


def sc_gui_drain(port):
    """The server is up on this frontend's loop, serves a session, and drains.

    THE DRAIN is what the paused budget `PumpBudget{0, 2, 10}` buys (T's
    decision, transport.md §2 item 15): while paused with a client attached, a
    chain of queued commands is answered in ONE tick. With the running budget
    `PumpBudget{}` a GUI tick answers one command, so eight commands would
    arrive a tick (~20 ms) apart. So eight LOOPBACKs are written in one send
    and the spread of their replies' arrival is measured: one tick's drain is
    a few milliseconds; eight ticks is well over 100.
    """
    s = socket.create_connection((HOST, port), timeout=TIMEOUT)
    s.settimeout(TIMEOUT)
    try:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        _l, rseq, body = raw_exchange(s, 1, dz.Cmd.INIT,
                                      bytes([2, 2, 0]) + b"jnext-regression\x00")
        check(rseq == 1 and body[:1] == b"\x00", "CMD_INIT failed: %s" % body.hex())
        burst = b""
        for i in range(8):
            burst += struct.pack("<IBB", 4, 2 + i, int(dz.Cmd.LOOPBACK)) + bytes([i]) * 4
        s.sendall(burst)
        arrivals = []
        for i in range(8):
            length = struct.unpack("<I", recv_exact(s, 4))[0]
            body = recv_exact(s, length)
            arrivals.append(time.monotonic())
            check(body[0] == 2 + i and body[1:] == bytes([i]) * 4,
                  "burst reply %d: seq %d, %s" % (i, body[0], body[1:].hex()))
        spread = arrivals[-1] - arrivals[0]
        check(spread < 0.06, "the 8 queued replies were spread over %.0f ms: not drained in "
              "one tick" % (spread * 1000))
    finally:
        s.close()

    # And the machine runs under this loop when the client lets it: 0.5 s is
    # ~25 frames at 50 Hz, ~50000 counts.
    c = connect(port)
    try:
        load_counter32(c)
        c.cont()
        time.sleep(0.5)
        c.pause()
        pause_expecting(c, dz.BreakReason.MANUAL_BREAK)
        n = counter32(c)
        check(n >= 10000, "the counter reached only %d in 0.5 s after CMD_CONTINUE" % n)
    finally:
        c.close()
    return ("8 commands queued while paused answered within %.1f ms (one tick's drain); "
            "CONTINUE ran the program (counter %d in 0.5 s), PAUSE -> NTF 1"
            % (spread * 1000, n))


# ---------------------------------------------------------------------------
# dzrp-paused-headless-func — no spin, and the exit bound still fires
# ---------------------------------------------------------------------------


def process_table():
    """{pid: (ppid, name)} of every process. /proc where there is one (a bare
    CI container has no `ps`), else `ps` (macOS)."""
    table = {}
    if os.path.isdir("/proc/self"):
        for d in os.listdir("/proc"):
            if not d.isdigit():
                continue
            try:
                with open("/proc/%s/stat" % d) as f:
                    st = f.read()
            except OSError:
                continue
            fields = st[st.rindex(")") + 2:].split()
            table[int(d)] = (int(fields[1]), st[st.index("(") + 1:st.rindex(")")])
        return table
    out = subprocess.run(["ps", "-A", "-o", "pid=,ppid=,comm="], capture_output=True,
                         text=True, env={"LANG": "C", "PATH": os.environ.get("PATH", "")}).stdout
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[0].isdigit() and parts[1].isdigit():
            table[int(parts[0])] = (int(parts[1]), os.path.basename(parts[2].strip()))
    return table


def jnext_process_under(root):
    """The jnext process started (directly or through wrappers) by <root>.

    Not "the first child": a wrapper (the wine runner of `make regression-win`
    is two shells deep, and a shim script's comm is its own name) puts other
    processes between. The candidates are the descendants named jnext or
    jnext.exe; the DEEPEST one is the real process (a shim is its ancestor). On
    Linux without a wrapper it finds the same pid the first child was.
    """
    table = process_table()
    kids = {}
    for pid, (ppid, _) in table.items():
        kids.setdefault(ppid, []).append(pid)
    best, frontier, seen = None, [root], {root}
    while frontier:
        nxt = []
        for p in frontier:
            for k in kids.get(p, []):
                if k in seen:
                    continue
                seen.add(k)
                nxt.append(k)
                if table[k][1] in ("jnext", "jnext.exe"):
                    best = k
        frontier = nxt
    return best


def cpu_seconds(pid):
    """CPU seconds (user + system) the process has used: /proc where there is
    one, else POSIX `ps -o time=` ([[dd-]hh:]mm:ss[.ff], macOS)."""
    try:
        with open("/proc/%d/stat" % pid) as f:
            st = f.read()
    except FileNotFoundError:
        t = subprocess.run(["ps", "-o", "time=", "-p", str(pid)], capture_output=True, text=True,
                           env={"LANG": "C", "PATH": os.environ.get("PATH", "")}).stdout.strip()
        if not t:
            # The process is gone (Linux: /proc entry vanished) or ps cannot tell.
            seen = subprocess.run(["ps", "-A", "-o", "pid,ppid,stat,time,comm"], capture_output=True,
                                  text=True, env={"LANG": "C", "PATH": os.environ.get("PATH", "")}).stdout
            mine = [l.strip() for l in seen.splitlines() if "jnext" in l or "timeout" in l or str(pid) in l]
            raise RuntimeError("no CPU time for pid %d; ps sees: %s" % (pid, " | ".join(mine)))
        days = 0
        if "-" in t:
            d, t = t.split("-", 1)
            days = int(d)
        secs = 0.0
        for part in t.split(":"):
            secs = secs * 60 + float(part)
        return days * 86400 + secs
    fields = st[st.rindex(")") + 2:].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def sc_paused_headless(port, wrapper_pid, exit_frames):
    """Started BEFORE jnext listens, so it attaches in the first frames.

    Then it holds the machine paused and measures (a) the CPU jnext uses while
    it waits — a spinning loop burns a whole core — and (b) when jnext exits on
    its own: `--delayed-automatic-exit-frames N` must still fire, charged in
    wall time at 20 ms a frame while a client holds the machine.

    THE HOLD STARTS CHATTY: for 0.8 s the client sends a LOOPBACK every couple
    of milliseconds, so the headless loop goes round hundreds of times a second
    (every command ends a `pump()` wait). The exit charge is WALL time, so it
    does not care how often the loop turns — which is exactly what a loop that
    also counted a frame per turn (the paused branch falling through instead of
    `continue`-ing) does care about: it would exit within a fraction of a
    second, mid-chat. The silent half after it is where the CPU is measured.
    """
    wrapper_pid, exit_frames = int(wrapper_pid), int(exit_frames)
    deadline = time.monotonic() + 20
    c = None
    while c is None:
        try:
            c = dz.CSpectDZRP(HOST, port, timeout=TIMEOUT)
            c.connect()
        except OSError:
            c = None
            check(time.monotonic() < deadline, "could not connect within 20 s")
            time.sleep(0.001)
    c.init(name="jnext-regression")
    t_attach = time.monotonic()
    jpid = jnext_process_under(wrapper_pid)
    check(jpid is not None, "could not find jnext under pid %d" % wrapper_pid)
    # The whole register set, R included (it counts every opcode fetch): a
    # machine that executed anything while held would not read back the same.
    r0 = c.get_registers()
    held = regs_tuple(r0) + (r0.R,)

    # Chatty: a round trip every ~2 ms until 0.85 s after the attach.
    chats = 0
    while time.monotonic() - t_attach < 0.85:
        try:
            echo = c.loopback(bytes([chats & 0xFF]))
        except (OSError, dz.DZRPError):
            raise Fail("jnext exited %.2f s after the attach, %d commands into a chatty "
                       "hold — its exit bound counted loop turns, not wall time"
                       % (time.monotonic() - t_attach, chats))
        check(echo == bytes([chats & 0xFF]), "LOOPBACK %d echoed %r" % (chats, echo))
        chats += 1
        time.sleep(0.002)

    # Silent: the CPU jnext burns while it waits.
    time.sleep(0.05)
    cpu0, w0 = cpu_seconds(jpid), time.monotonic()
    time.sleep(0.8)
    cpu1, w1 = cpu_seconds(jpid), time.monotonic()
    used = (cpu1 - cpu0) / (w1 - w0)
    r1 = c.get_registers()
    check(regs_tuple(r1) + (r1.R,) == held, "the machine executed while the client held it")
    check(used < 0.25, "jnext used %.0f%% of a CPU while paused with a client attached"
          % (used * 100))

    # Now wait for jnext to go, the client still attached.
    c._sock.settimeout(20)
    try:
        while c._sock.recv(4096):
            pass
    except socket.timeout:
        raise Fail("jnext was still running %.0f s after the attach: its exit bound "
                   "(%d frames) never fired while the client held the machine"
                   % (time.monotonic() - t_attach, exit_frames))
    except OSError:
        pass
    gone = time.monotonic() - t_attach
    # The frames run before the attach (one or two) are not subtracted: the
    # 0.7 below absorbs them, and more than that would be a late attach.
    expected = exit_frames * 0.020
    check(gone >= 0.7 * expected, "jnext exited %.2f s after the attach — before its exit bound "
          "(~%.1f s)" % (gone, expected))
    # The charge is WALL time, exact whatever the host load, so the bound is
    # tight: a charge at half or double the rate lands outside [0.7x, x + 1 s].
    check(gone <= expected + 1.0, "jnext exited %.2f s after the attach, expected ~%.1f s"
          % (gone, expected))
    return ("%d commands in a chatty hold then %.1f%% CPU silent, nothing executed; exited on "
            "its own %.2f s after the attach, the client still attached (bound %d frames ~ "
            "%.1f s)" % (chats, used * 100, gone, exit_frames, expected))


SCENARIOS = {
    "loopback": sc_loopback,
    "init-regs": sc_init_regs,
    "bp-continue": sc_bp_continue,
    "watch": sc_watch,
    "state": sc_state,
    "bank": sc_bank,
    "brkint": sc_brkint,
    "unsupported": sc_unsupported,
    "close-resume": sc_close_resume,
    "paused-headless": sc_paused_headless,
    "gui-drain": sc_gui_drain,
}


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in SCENARIOS:
        print("usage: dzrp-peer.py {%s} <port> [args...]" % "|".join(SCENARIOS))
        return 2
    try:
        summary = SCENARIOS[sys.argv[1]](int(sys.argv[2]), *sys.argv[3:])
    except Fail as e:
        print("FAIL %s" % e)
        return 1
    except (OSError, dz.DZRPError, TimeoutError, ValueError) as e:
        print("FAIL %s: %s" % (type(e).__name__, e))
        return 1
    print("PASS %s" % summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
