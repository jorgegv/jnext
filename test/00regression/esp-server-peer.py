#!/usr/bin/env python3
"""The peer half of `esp-server-func` (GH #210).

The mirror image of `esp-loopback-peer.py`: there the guest dialled out and
this process listened, here **the guest listens** and this process dials in.
That reversal is the whole feature, and it is why the row exists — the emulated
ESP has to bind a real socket on the host, accept a real connection and hand it
to guest software, and no unit suite can prove that (the AT suite drives a fake
listener, the socket suite has no AT engine).

  1. Picks a free TCP port, and writes the Z80 guest that tells jnext to listen
     on exactly that port.
  2. Retries `connect()` until jnext's listener is up — the guest has to boot,
     be injected and run three AT commands first.
  3. Exchanges bytes both ways: reads the guest's greeting, THEN sends a
     request the guest echoes to its trace.

STEP 3 IS IN THAT ORDER TO REMOVE A RACE, not for realism. If this end sent
first, its bytes would be waiting while the guest was still reacting to
`1,CONNECT`, and whether the engine framed the `+IPD` before or after the
guest's next `AT+CIPSEND` would depend on which arrived first at the wire —
both orders correct, only one assertable. Sending only after the guest has
spoken means nothing is ever pending while the guest is mid-command, so the
trace has exactly one possible order.

WHY 127.0.0.1 HERE, WHERE THE OUTBOUND PEER NEEDED RFC1918. The two directions
have opposite defaults, and both are deliberate. Outbound, loopback is DENIED
(the guest must not reach daemons on the host), so that peer binds a private
address. Inbound, loopback is the DEFAULT bind (`--esp-listen-address`, design
doc §13.4) precisely so a listening guest is reachable only from this machine.
So this row needs no flag, and exercises the shipped default rather than a
configuration nobody runs.

THE PORT IS PICKED BY BINDING AND RELEASING, which is a small race: another
process could take it in the gap. It is the only option available — the port
has to be known before the guest program is written, and the guest is what
opens it. A collision makes the row fail loudly (no connection) rather than
pass wrongly, and the kernel does not hand out a just-released ephemeral port
again in any hurry.

Usage: esp-server-peer.py <guest.bin> <ready.txt> <received.bin> [--lan] [--close]

With `--lan` the client dials this host's own RFC1918 address instead of
127.0.0.1, which is how `esp-server-lan-func` proves that
`--esp-listen-address` reaches the socket rather than only the log: a guest
listening on the DEFAULT is unreachable that way, so the connection succeeds
only if the flag really moved the bind. Exit 3 = this host has no RFC1918
address (the shell turns that into a SKIP).

With `--close` (GH #211) the guest ends the conversation with
`AT+CIPCLOSE=<id>` instead of sending nothing further, and this process stops
being a passive socket-holder: it reads to EOF and then DIALS IN AGAIN.
`<received.bin>` holds the greeting, a newline, and one of
`EOF+RECONNECT` / `EOF+NORECONNECT` / `OPEN`.

THE RECONNECT IS THE ASSERTION, AND THE EOF ON ITS OWN IS NOT — measured, not
assumed. jnext exits at the end of the row and process exit closes every socket
it owns, so a peer that only waits for EOF sees one even against a build that
never closed anything: both mutations of the close path were run against an
EOF-only version of this and both PASSED it. Dialling in again removes the
confound in one step, because it can only succeed while jnext is still running
AND still listening, and because the id the guest then sees says whether the
slot was really returned to the pool: `1,CONNECT` only if the closed connection
freed slot 1, `2,CONNECT` if it merely stopped being open.
"""

import importlib.util
import os
import socket
import sys
import time

# The Z80 the loopback row already uses, byte for byte. It is a TABLE WALKER —
# each record is [len][bytes...][expect]: write the bytes to UART 0, then
# consume RX (echoing everything to the magic port) until `expect` arrives —
# so a different conversation is a different table, not different code. Loaded
# rather than copied because the bytes are pinned to an assembly listing that
# lives in that file, and two copies of a pinned constant is one copy too many.
_HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "esp_loopback_peer", os.path.join(_HERE, "esp-loopback-peer.py"))
_loopback = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_loopback)

GUEST_CODE = _loopback.GUEST_CODE
SCRIPT_TABLE_ORG = _loopback.SCRIPT_TABLE_ORG

# What crosses the accepted connection, in each direction. ASCII and — for the
# inbound half — CRLF-terminated, so the magic port's LINE mode flushes it as
# one line and the row can assert it as an exact string. A real DZRP frame is
# binary and would need the hex mode, which buys nothing here: what is under
# test is the transport, not the protocol on top of it.
GREETING = b"READY"        # guest -> client, via AT+CIPSEND=1,5
REQUEST = b"DZRP-REQ\r\n"  # client -> guest, arrives as +IPD,1,10:

# Long enough for a headless boot plus the 100-frame inject delay on a loaded
# box (JNEXT_TEST_JOBS=4), far short of the row's own timeout.
CONNECT_TIMEOUT_S = 45
# How long the guest may take to issue its close once it has greeted us: three
# AT commands' worth of emulated time, no boot in it.
RECLOSE_TIMEOUT_S = 20
# How long the SECOND connect may take. Deliberately short — the listener is
# already bound and the slot is already free, so this is a localhost connect and
# nothing else. A generous bound here would let a connect that only succeeded
# because it retried past something count as success.
RECONNECT_TIMEOUT_S = 5
WATCHDOG_S = 600


def build_guest(port, close_at_end=False):
    def rec(line, expect):
        assert len(line) < 256
        return bytes([len(line)]) + line + expect

    # `AT+CIPSERVER`'s expect byte is 'T', which its own reply (`\r\nOK\r\n`)
    # does not contain — so the guest keeps consuming past it and blocks until
    # the 'T' of `1,CONNECT` arrives. That is what removes the race: there is no
    # window in which the listener is up and the guest is not yet waiting.
    table = (rec(b"ATE0\r\n", b"K")
             + rec(b"AT+CIPMUX=1\r\n", b"K")
             + rec(b"AT+CIPSERVER=1,%d\r\n" % port, b"T")
             + rec(b"AT+CIPSEND=1,%d\r\n" % len(GREETING), b">")
             + rec(GREETING, b"S"))
    if close_at_end:
        # `D` is the first byte of `1,CLOSED` that cannot occur in what precedes
        # it on this wire, which is the same sync-point rule every other record
        # follows. The walker echoes for ever after the last record, so the rest
        # of `1,CLOSED\r\n\r\nOK\r\n` still reaches the trace.
        table += rec(b"AT+CIPCLOSE=1\r\n", b"D")
    table += b"\x00"
    assert len(GUEST_CODE) <= SCRIPT_TABLE_ORG - 0x8000
    return GUEST_CODE + bytes(SCRIPT_TABLE_ORG - 0x8000 - len(GUEST_CODE)) + table


def free_port(ip):
    """A port free on `ip` itself — probed there, not on loopback, because a
    port free on one local address is not necessarily free on another."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind((ip, 0))
        return s.getsockname()[1]
    finally:
        s.close()


def main(argv):
    guest_path, ready_path, received_path = argv[1], argv[2], argv[3]
    want_lan = "--lan" in argv[4:]
    want_close = "--close" in argv[4:]

    if want_lan:
        # The host's own private address. Reused from the loopback peer rather
        # than reimplemented — same UDP-connect probe, and the same reasoning
        # about why no packet leaves the machine.
        ip = _loopback.local_ipv4()
        if not _loopback.is_rfc1918(ip):
            print("no RFC1918 IPv4 on this host (got %r); there is no non-loopback "
                  "address to prove the widened bind against" % (ip,))
            return 3
    else:
        ip = "127.0.0.1"

    port = free_port(ip)
    with open(guest_path, "wb") as f:
        f.write(build_guest(port, close_at_end=want_close))
    # Written LAST: the shell waits for this file before starting jnext, so it
    # must not appear before the guest binary is complete.
    with open(ready_path, "w") as f:
        f.write("%s %d\n" % (ip, port))

    deadline = time.time() + CONNECT_TIMEOUT_S
    conn = None
    while time.time() < deadline:
        try:
            conn = socket.create_connection((ip, port), timeout=2)
            break
        except OSError:
            time.sleep(0.2)
    if conn is None:
        print("peer: nothing accepted a connection on %s:%d" % (ip, port))
        return 1

    conn.settimeout(WATCHDOG_S)
    got = b""
    while len(got) < len(GREETING):
        chunk = conn.recv(64)
        if not chunk:
            break
        got += chunk
    if want_close:
        # The guest closes the connection next. A `recv` of zero bytes IS the
        # FIN. A timeout is reported as `OPEN` rather than raised, so the shell
        # can print it as a verdict instead of a traceback.
        conn.settimeout(RECLOSE_TIMEOUT_S)
        verdict = b"OPEN"
        try:
            while True:
                chunk = conn.recv(64)
                if not chunk:
                    verdict = b"EOF"
                    break
                got += chunk
        except OSError as exc:
            # A reset counts as gone too — the peer learned the connection
            # ended, which is the property under test.
            print("peer: recv after greeting raised %r" % (exc,))
            verdict = b"EOF" if isinstance(exc, ConnectionResetError) else b"OPEN"
        conn.close()

        # AND NOW DIAL IN AGAIN — see the module docstring. Bounded tightly: the
        # listener is already up, so a connect that needs seconds is a connect
        # that is racing jnext's exit, which is the very thing this must not
        # mistake for success.
        again = None
        if verdict == b"EOF":
            deadline = time.time() + RECONNECT_TIMEOUT_S
            while time.time() < deadline:
                try:
                    again = socket.create_connection((ip, port), timeout=1)
                    break
                except OSError:
                    time.sleep(0.1)
            verdict = b"EOF+RECONNECT" if again is not None else b"EOF+NORECONNECT"

        with open(received_path, "wb") as f:
            f.write(got + b"\n" + verdict + b"\n")
        print("peer: guest greeted with %r, then %s" % (got, verdict.decode()))
        sys.stdout.flush()
        # Held open exactly as the non-close mode holds its socket: an early
        # close here would emit a second unsolicited CLOSED into the guest's
        # trace as a function of when this process happened to exit.
        time.sleep(WATCHDOG_S)
        return 0

    with open(received_path, "wb") as f:
        f.write(got)
    # Only now — see the module docstring: sending earlier would make the
    # guest's trace order depend on a race.
    conn.sendall(REQUEST)
    print("peer: guest greeted with %r" % got)
    sys.stdout.flush()

    # Held open, exactly as the loopback peer holds its accepted socket: closing
    # early would make the engine emit an unsolicited `1,CLOSED`, which would
    # land in the guest's trace and break the exact-sequence assertion as a
    # function of when this process happened to exit.
    time.sleep(WATCHDOG_S)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
