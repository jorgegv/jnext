#!/usr/bin/env python3
"""The server half of `nextsync-func` (GH #167).

Speaks the NextSync4 wire protocol to the REAL, unmodified NextSync 1.2 dot
command running inside the emulator. Follows esp-loopback-peer.py in shape:

  1. Picks the host's RFC1918 address, or exits 3 so the shell SKIPs.
  2. Builds the corpus, and writes a manifest of "<path-on-the-Next> <sha256>"
     lines for the shell to verify against.
  3. Binds THAT ADDRESS, serves connections, and logs a stats line.
  4. Holds until killed.

WHY NOT 127.0.0.1: identical to esp-loopback-peer.py — the emulated ESP's
address policy denies loopback by default, deliberately, and RFC1918 is allowed
precisely so the guest can reach a machine on the user's own LAN. That is the
configuration NextSync is used in. A host with no RFC1918 address SKIPs rather
than pretending.

WHY NOT 0.0.0.0 EITHER, and why this file speaks the protocol itself instead of
running the vendored `nextsync/server/nextsync.py`: that server binds the
WILDCARD (`s.bind(("", PORT))`, with no option to narrow it), which would put a
listener on every interface of the developer's machine — including a public one
— for the length of the run. Binding only the discovered private address is the
narrower exposure. Getting there via the vendored server would mean either
patching it (so it is no longer the released artifact) or running it as a
subprocess — and a subprocess is a child to orphan, which is exactly how an
earlier draft of this file left strays sitting on port 2048. Serving the
protocol here costs ~60 lines, keeps the exposure narrow, and leaves this a
SINGLE process with no child, so the shell's kill is the entire lifecycle.

WHAT IS STILL THE REAL THING: the dot command, which is the software under test.
jnext's ESP emulation only ever sees a TCP byte stream, so the peer's
implementation cannot mask a defect in it — and a peer that framed a packet
wrongly would not slip through, because the dot verifies a checksum and a packet
number on every packet (uart.s `_checksum`) and the row asserts BOTH zero
retries AND byte-identical files. `nextsync/server/nextsync.py` is vendored
alongside as the protocol reference this was written from, and is what the
manual recipe in doc/testing/NEXTSYNC-VERIFICATION.md runs.

THE PORT IS NOT OURS TO CHOOSE. `.sync` has 2048 compiled into it, so unlike
esp-loopback-peer this cannot take an ephemeral port. Functional rows are
sourced sequentially by the driver (JNEXT_TEST_JOBS parallelises only the
screenshot rows), so the row cannot collide with itself inside one run; two
whole suites running at once on one box would collide, and then this exits 3 and
the row SKIPs rather than flaking.

Usage: nextsync-peer.py <syncroot> <ready.txt> <manifest.txt>
Exit 3 = no usable address, or port 2048 unavailable (the shell SKIPs).
"""

import hashlib
import os
import random
import socket
import sys
import time

# NextSync's hard-coded port (the string ",2048" is inside the dot command).
PORT = 2048

# The vendored server's default payload size, and what the dot is tuned for.
MAX_PAYLOAD = 1024

# The Next-side directory the corpus lands in. It must already exist on the SD
# card — esxdos will not create it — and `/tmp` does, in the stock NextZXOS
# image. Deliberately NOT the SD root: `boot-nextzxos-dotls` screenshots a root
# listing, and while functional rows run after every screenshot row today, a
# fixture that cannot disturb that reference even if the order changed is
# cheaper than remembering why the order matters.
DEST_DIR = "tmp"

# Upper bound on our own life. The shell kills us as soon as the row finishes,
# on success and on failure alike; this only matters when the shell itself dies
# without doing so, and then a stray must not sit on port 2048 for long. Far
# past the row's measured ~30 s, and far short of the 15 minutes an earlier
# draft of this file would have lingered for.
WATCHDOG_S = 180


def log(msg):
    print(msg)
    sys.stdout.flush()


def local_ipv4():
    """The address this host would use to reach the outside world.

    A connected UDP socket only sets local routing state — nothing is sent and
    nothing needs to be reachable, so this works with the cable out. 192.0.2.1
    is TEST-NET-1 (RFC 5737), reserved for exactly this.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


def is_rfc1918(ip):
    try:
        p = [int(x) for x in ip.split(".")]
    except (AttributeError, ValueError):
        return False
    if len(p) != 4:
        return False
    return (p[0] == 10
            or (p[0] == 172 and 16 <= p[1] <= 31)
            or (p[0] == 192 and p[1] == 168))


def build_corpus(syncroot):
    """Two files, each proving something the other cannot.

    Returns [(path_on_the_next, bytes, sha256_hex)].
    """
    dest = os.path.join(syncroot, DEST_DIR)
    os.makedirs(dest, exist_ok=True)

    # 1. Binary hostility. Every byte value in both directions, runs of NUL
    #    (the byte AT+CIPSENDEX terminates on), and the literal AT response
    #    frames — anything the emulation mistakes for a control character or a
    #    reply corrupts this file rather than merely garbling a screen.
    b = bytearray(range(256))
    b += b"\r\nOK\r\n\r\nERROR\r\nSEND OK\r\n> +IPD,\x00\x00\x00"
    b += bytes(range(255, -1, -1))
    b += bytes(64)
    b += b"\x1a\x0d\x0a\x00\xff" * 32
    while len(b) < 4096:
        b.append((len(b) * 7) & 0xFF)
    binary = bytes(b)

    # 2. Multi-packet. At MAX_PAYLOAD, 16 KB is 16 payload packets plus the
    #    handshake — enough that an off-by-one in packet accounting, or one
    #    dropped packet, shows up as a bad hash. Deterministic seed, so the row
    #    asserts the same digest on every host.
    random.seed(167)
    big = bytes(random.randrange(256) for _ in range(16384))

    out = []
    for name, data in (("NSBIN.BIN", binary), ("NSBIG.BIN", big)):
        with open(os.path.join(dest, name), "wb") as f:
            f.write(data)
        out.append(("%s/%s" % (DEST_DIR, name), data, hashlib.sha256(data).hexdigest()))
    return out


def framed(payload, packetno):
    """One NextSync packet: BE16(len+5), payload, xor sum, running sum, seq.

    Mirrors sendpacket() in the vendored server, which is the reference.
    """
    c0 = 0
    c1 = 0
    for x in payload:
        c0 = (c0 ^ x) & 0xFF
        c1 = (c1 + c0) & 0xFF
    return ((len(payload) + 5).to_bytes(2, "big") + payload
            + bytes([c0, c1, packetno & 0xFF]))


def serve_one(srv, corpus):
    """One connection's worth of the protocol."""
    conn, addr = srv.accept()
    log("peer: connection from %s:%d" % addr)
    state = {"retries": 0, "restarts": 0, "packets": 0, "last": b""}
    fn = 0                  # index into corpus of the next file to announce
    filedata = b""
    fileofs = 0
    packetno = 0

    def send(payload, seq):
        state["last"] = payload
        state["packets"] += 1
        conn.sendall(framed(payload, seq))

    with conn:
        conn.settimeout(WATCHDOG_S)
        while True:
            data = conn.recv(1024)
            if not data:
                break
            # "Neex"/"Gee" are the mistransmits the vendored server tolerates;
            # accepted here too so a real UART glitch degrades the same way.
            if data == b"Sync3":
                send(b"NextSync3", 0)
            elif data in (b"Next", b"Neex"):
                if fn >= len(corpus):
                    send(b"\x00\x00\x00\x00\x00", 0)      # end of list
                else:
                    sd_path, filedata, _digest = corpus[fn]
                    spec = "/" + sd_path
                    send(len(filedata).to_bytes(4, "big")
                         + bytes([len(spec)]) + spec.encode(), 0)
                    log("peer: sending %s, %d bytes" % (spec, len(filedata)))
                    fileofs = 0
                    packetno = 0
                    fn += 1
            elif data in (b"Get", b"Gee"):
                n = min(MAX_PAYLOAD, len(filedata) - fileofs)
                send(filedata[fileofs:fileofs + n], packetno)
                fileofs += n
                packetno += 1
            elif data == b"Retry":
                state["retries"] += 1
                send(state["last"], packetno - 1)
            elif data == b"Restart":
                state["restarts"] += 1
                fileofs = 0
                packetno = 0
                send(b"Back", 0)
            elif data == b"Bye":
                send(b"Later", 0)
                break
            else:
                send(b"Error", 0)
    # Same shape as the vendored server's summary line, because the row greps it.
    log("peer: packets: %d, retries: %d, restarts: %d"
        % (state["packets"], state["retries"], state["restarts"]))


def main(argv):
    syncroot, ready_path, manifest_path = argv[1], argv[2], argv[3]

    ip = local_ipv4()
    if not is_rfc1918(ip):
        log("no RFC1918 IPv4 on this host (got %r); the ESP address policy "
            "denies loopback, so there is nowhere to listen" % (ip,))
        return 3

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        # THE DISCOVERED ADDRESS, never the wildcard — see the module docstring.
        srv.bind((ip, PORT))
    except OSError as e:
        log("cannot bind %s:%d (%s); NextSync's dot command has that port "
            "compiled in, so the row cannot pick another" % (ip, PORT, e))
        return 3
    srv.listen(1)

    corpus = build_corpus(syncroot)
    with open(manifest_path, "w") as f:
        for sd_path, _data, digest in corpus:
            f.write("%s %s\n" % (sd_path, digest))

    # Written LAST: the shell waits for this file, so it must not appear before
    # both the corpus and the listener are ready.
    with open(ready_path, "w") as f:
        f.write("%s\n" % ip)
    log("peer: listening on %s:%d, %d files" % (ip, PORT, len(corpus)))

    # Serve until the shell kills us. Two independent stop conditions, so a
    # stray cannot outlive its run even if the kill never comes: a total time
    # bound, and orphan detection — if the shell dies without killing us, init
    # adopts us and getppid() changes. Polling with a 1 s accept timeout is what
    # makes both of them reachable while idle.
    parent = os.getppid()
    deadline = time.time() + WATCHDOG_S
    srv.settimeout(1.0)
    while time.time() < deadline and os.getppid() == parent:
        try:
            serve_one(srv, corpus)
        except socket.timeout:
            continue
        except OSError as e:
            log("peer: connection error: %s" % e)
    if os.getppid() != parent:
        log("peer: parent went away — exiting rather than lingering on the port")
    else:
        log("peer: watchdog expired after %d s" % WATCHDOG_S)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
