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


def main():
    try:
        if len(sys.argv) >= 3 and sys.argv[1] == "handshake":
            handshake(int(sys.argv[2]))
            print("PASS qSupported, a 600-byte target.xml, ? T05, g, m, s, monitor mmu, D")
        elif len(sys.argv) >= 4 and sys.argv[1] == "both":
            both(int(sys.argv[2]), int(sys.argv[3]))
            print("PASS a GDB and a DZRP session live at once, each served; D left DZRP up")
        else:
            print("FAIL usage: gdb-peer.py handshake PORT | both GDBPORT DZRPPORT")
            return 1
    except (Fail, OSError, ValueError) as e:
        print("FAIL %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
