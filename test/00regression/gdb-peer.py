#!/usr/bin/env python3
"""The client half of the `gdb-*-func` regression rows (GH #281).

Drives a LIVE jnext started with `--gdb-port` over a real TCP socket, speaking
the GDB Remote Serial Protocol the way `z88dk-gdb` does at connect
(gdb-rsp-frontend.md §1.1): `qSupported`, `qXfer:features:read:target.xml`,
`?`, then `g` and memory reads. The framing here is this script's own, written
from the RSP manual — not the adapter's codec — so a framing bug the two
shared could not pass.

    gdb-peer.py handshake <gdb-port>
    gdb-peer.py both <gdb-port> <dzrp-port>
    gdb-peer.py hold <gdb-port>
    gdb-peer.py idle-check qt|sdl <log-file>

`both` also opens a DZRP session on the second port (through jnext's own DZRP
client, tools/cspect_dzrp) while the GDB session is live: two servers on one
backend, one client each (WP-4).

Prints `PASS <summary>` and exits 0, or `FAIL <reason>` and exits 1. Every wait
is bounded: a silent server is a FAIL, never a hang.
"""

import os
import socket
import sys

HOST = "127.0.0.1"
TIMEOUT = 10.0


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


class Rsp:
    def __init__(self, port):
        self.s = socket.create_connection((HOST, port), timeout=TIMEOUT)
        self.buf = b""
        self.acks = b""

    def send(self, body):
        data = body.encode("latin-1")
        self.s.sendall(b"$" + data + b"#" + ("%02x" % (sum(data) & 0xFF)).encode())

    def packet(self):
        """The next packet body; acks are collected on the way."""
        while True:
            while self.buf[:1] in (b"+", b"-"):
                self.acks += self.buf[:1]
                self.buf = self.buf[1:]
            if self.buf[:1] == b"$":
                end = self.buf.find(b"#")
                if end >= 0 and len(self.buf) >= end + 3:
                    body = self.buf[1:end]
                    want = int(self.buf[end + 1:end + 3], 16)
                    check(sum(body) & 0xFF == want, "a reply with a bad checksum: %r" % body)
                    self.buf = self.buf[end + 3:]
                    return body.decode("latin-1")
            elif self.buf:
                raise Fail("stray bytes outside a packet: %r" % self.buf[:16])
            chunk = self.s.recv(65536)
            if not chunk:
                raise Fail("the server closed the connection")
            self.buf += chunk

    def cmd(self, body):
        self.send(body)
        return self.packet()

    def closed(self):
        """True once the server has closed its end (bounded by TIMEOUT)."""
        try:
            while True:
                chunk = self.s.recv(4096)
                if not chunk:
                    return True
        except socket.timeout:
            return False


def handshake(port):
    g = Rsp(port)
    sup = g.cmd("qSupported")
    check("qXfer:features:read+" in sup, "qSupported lacks qXfer:features:read+: %r" % sup)
    check("PacketSize=4000" in sup, "qSupported: %r" % sup)
    xml = g.cmd("qXfer:features:read:target.xml:0,3fff")
    check(xml.startswith("l") and "<architecture>z80</architecture>" in xml,
          "target.xml: %r" % xml[:80])
    check(len(xml) < 1023, "the target.xml reply is %d bytes: z88dk-gdb v2.4 overflows at 1023"
          % len(xml))
    stop = g.cmd("?")
    check(stop == "T05thread:1;", "? answered %r" % stop)
    regs = g.cmd("g")
    check(len(regs) == 56 and all(c in "0123456789abcdef" for c in regs), "g answered %r" % regs)
    rom = g.cmd("m0,4")
    # The 48K ROM's first four bytes: DI; XOR A; LD DE,0xFFFF.
    check(rom == "f3af11ff", "m0,4 answered %r (not the 48K ROM)" % rom)
    step = g.cmd("s")
    check(step == "T05thread:1;", "s answered %r" % step)
    g.send("qRcmd," + "mmu".encode().hex())
    lines = []
    while True:
        p = g.packet()
        if p.startswith("O") and p != "OK":
            lines.append(bytes.fromhex(p[1:]).decode())
            continue
        check(p == "OK", "monitor mmu ended with %r" % p)
        break
    check(len(lines) == 9 and lines[0].startswith("slot 0: page"), "monitor mmu: %r" % lines)
    check(g.acks.count(b"+") >= 7, "the server did not acknowledge every packet: %r" % g.acks)
    bye = g.cmd("D")
    check(bye == "OK", "D answered %r" % bye)
    check(g.closed(), "the server did not close the connection after D")
    return g


def both(gdb_port, dzrp_port):
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, "..", "..", "tools", "cspect_dzrp"))
    import cspect_dzrp as dz  # noqa: E402

    g = Rsp(gdb_port)
    check("qXfer:features:read+" in g.cmd("qSupported"), "qSupported")
    stop = g.cmd("?")
    check(stop == "T05thread:1;", "? answered %r" % stop)

    d = dz.CSpectDZRP(HOST, dzrp_port, timeout=TIMEOUT)
    d.connect()
    info = d.init(version=(2, 2, 0), name="jnext-regression")
    check(info.error == 0, "DZRP CMD_INIT answered error %d" % info.error)

    # Both sessions live on one backend: each answers its own client.
    regs = g.cmd("g")
    check(len(regs) == 56, "g answered %r with DZRP attached" % regs)
    dz_pc = d.get_registers().PC
    gdb_pc = int(regs[46:48] + regs[44:46], 16)
    check(dz_pc == gdb_pc, "the two clients see different PCs: DZRP %04X, GDB %04X"
          % (dz_pc, gdb_pc))
    check(g.cmd("D") == "OK", "D")
    check(g.closed(), "the server did not close the GDB connection after D")
    # The DZRP session is untouched by the GDB client leaving.
    check(d.loopback(b"still here") == b"still here", "the DZRP session died with the GDB one")
    d.close()
    return g


def hold(port):
    """GH #428: interrupt a live machine, hold it paused for several seconds,
    poke memory once in the middle, resume. The `idle-check` scenario then
    reads what the frontend reported about its redraws over that time."""
    import time
    g = Rsp(port)
    time.sleep(2.5)                      # running: the log gets windows with uploads
    g.s.sendall(b"\x03")
    stop = g.packet()
    check(stop.startswith("T05"), "the interrupt was answered %r" % stop)
    check(g.cmd("?") == "T05thread:1;", "? after the interrupt")
    time.sleep(4.5)                      # held: whole idle one-second windows
    check(g.cmd("M5800,1:ff") == "OK", "the poke was refused")
    time.sleep(3.5)                      # the window with the poke, then idle again
    g.send("c")                          # resume: no reply until the next stop
    time.sleep(3.0)                      # running again: uploads resume
    g.s.close()


def idle_check(kind, path):
    """GH #428: read the frontend's per-second redraw counters and prove a held
    machine redraws nothing. Counting, not timing: the assertions hold on a
    loaded host. A log without the counters is a FAIL, never a pass."""
    import re
    with open(path, "r", errors="replace") as f:
        text = f.read()
    if kind == "qt":
        rx = re.compile(r"ticks: n=(\d+) .*\| uploads=(\d+) panel-refreshes=(\d+)")
        win = [(int(a), int(b), int(c)) for a, b, c in rx.findall(text)]
    else:
        rx = re.compile(r"sdl present: ticks=(\d+) uploads=(\d+)")
        win = [(int(a), int(b), 0) for a, b in rx.findall(text)]
    check(win, "no %s counter lines in the log (the instrument is missing)" % kind)
    idle = lambda w: w[0] > 0 and w[1] == 0 and w[2] == 0
    i0 = next((i for i, w in enumerate(win) if w[1] > 0), None)
    check(i0 is not None, "no window before the pause had uploads>0: %r" % win)
    i = next((k for k in range(i0 + 1, len(win) - 1) if idle(win[k]) and idle(win[k + 1])), None)
    check(i is not None, "no two consecutive idle windows (ticks>0, uploads=0, "
          "panel-refreshes=0) while the machine was held: %r" % win)
    start = i + 2
    if kind == "qt":
        j = next((k for k in range(start, len(win)) if win[k][0] > 0 and win[k][1] == 0
                  and win[k][2] >= 1), None)
        check(j is not None, "the window with the poke had no panel refresh: %r" % win)
        start = j + 1
    k = next((k for k in range(start, len(win)) if win[k][1] > 0), None)
    check(k is not None, "no window after the resume had uploads>0: %r" % win)
    return len(win)


def main():
    try:
        if len(sys.argv) >= 3 and sys.argv[1] == "handshake":
            handshake(int(sys.argv[2]))
            print("PASS qSupported, a 600-byte target.xml, ? T05, g, m, s, monitor mmu, D")
        elif len(sys.argv) >= 4 and sys.argv[1] == "both":
            both(int(sys.argv[2]), int(sys.argv[3]))
            print("PASS a GDB and a DZRP session live at once, each served; D left DZRP up")
        elif len(sys.argv) >= 3 and sys.argv[1] == "hold":
            hold(int(sys.argv[2]))
            print("PASS interrupted, held, poked, resumed")
        elif len(sys.argv) >= 4 and sys.argv[1] == "idle-check":
            n = idle_check(sys.argv[2], sys.argv[3])
            print("PASS %d one-second windows: running redraws, held machine redraws nothing "
                  "(a poke refreshes the panels once), resume redraws" % n)
        else:
            print("FAIL usage: gdb-peer.py handshake PORT | both GDBPORT DZRPPORT | hold PORT "
                  "| idle-check qt|sdl LOG")
            return 1
    except (Fail, OSError, ValueError) as e:
        print("FAIL %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
