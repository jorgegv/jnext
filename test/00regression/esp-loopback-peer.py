#!/usr/bin/env python3
"""The peer half of `esp-loopback-func` (GH #25 branch 5).

Does three things, in this order, and they are cohesive rather than merely
bundled: it can only write the guest program once it knows its own address.

  1. Picks a bind address and an ephemeral port, and listens.
  2. Writes the Z80 guest that dials exactly that address:port.
  3. Serves one connection — reads the guest's payload, answers a fixed reply,
     and then HOLDS THE SOCKET OPEN until killed.

Step 3's last clause is load-bearing. Closing early would make the AT engine
emit an unsolicited `\\r\\nCLOSED\\r\\n`, which would appear in the guest's
magic-port trace and break the exact-sequence assertion — as a function of when
this process happened to exit, i.e. non-deterministically. The peer therefore
outlives the emulator, and the shell kills it.

WHY NOT 127.0.0.1. The emulated ESP's address policy denies loopback by
default (design doc §8.2), so a listener there is unreachable from the guest.
That deny is a deliberate owner decision, not an oversight, and a test is not a
reason to weaken it. RFC1918 is ALLOWED by that same decision (§8.1 item 4 —
"the emulated ESP must be able to reach machines on the user's own LAN"), so
this binds the host's own private address and the row exercises the
configuration the policy is actually written for. No packet leaves the host.
If the host has no RFC1918 address, the row SKIPs rather than pretending.

Portability: this file is plain `socket`, so it runs anywhere python3 does.
The C++ in-process listener in `src/esp01/test/esp_socket_test.cpp` is the
POSIX-only one (`arpa/inet.h`, `sys/wait.h`); a Windows consumer of the module
needs equivalents there, not here.

Usage: esp-loopback-peer.py <guest.bin> <ready.txt> <received.bin>
Exit 3 = no usable address (the shell turns that into a SKIP).
"""

import socket
import sys
import time

# ---------------------------------------------------------------------------
# The guest.
#
# 76 bytes of Z80, assembled from the listing below with z88dk's z80asm; the
# listing is the source of record and the bytes are pinned here so the
# regression suite needs no assembler. Any drift between the two shows up as a
# failing row, because the row asserts the exact byte stream the program
# produces.
#
# It is a table walker, not a parser: the script table at 0x8100 is a list of
# [len][bytes...][expect] records, and for each one it writes the bytes to
# UART 0 and then consumes RX until the `expect` byte arrives. Everything it
# receives is echoed to the magic port on the way past, so the emulator's
# stderr IS the wire trace. After the last record it echoes for ever.
#
# `expect` is a sync point, not a parse: each value is the first byte of that
# reply that cannot occur in the bytes before it, so waiting for it is enough
# to know the reply arrived without the guest having to understand it.
#
#     UART_STAT  equ 0x133B      ; read = status, write = TX byte
#     UART_RX    equ 0x143B      ; read = RX byte
#     MAGIC      equ 0xCAFE      ; --magic-port, LINE mode
#
#             org 0x8000
#     start:  di
#             ld   hl, 0x8100         ; script table (written below)
#     sloop:  ld   a,(hl)
#             or   a
#             jr   z, done
#             call send_buf
#             ld   a,(hl)             ; the byte that ends this exchange
#             inc  hl
#             call wait_for
#             jr   sloop
#     done:   call rx_byte            ; echo whatever the ESP says, for ever
#             jr   done
#
#     ; HL -> [len][data...]; returns HL just past the data
#     send_buf:
#             ld   d,(hl)
#             inc  hl
#     sb_next:
#             ld   a,d
#             or   a
#             ret  z
#             ld   e,(hl)
#             inc  hl
#             dec  d
#     sb_wait:
#             ld   bc, UART_STAT
#             in   a,(c)
#             and  0x02               ; TX FIFO full?
#             jr   nz, sb_wait
#             ld   a,e
#             ld   bc, UART_STAT
#             out  (c),a
#             jr   sb_next
#
#     ; blocks until a byte arrives; returns it in A, echoed to the magic port
#     rx_byte:
#             ld   bc, UART_STAT
#             in   a,(c)
#             and  0x01               ; RX available?
#             jr   z, rx_byte
#             ld   bc, UART_RX
#             in   a,(c)
#             ld   bc, MAGIC
#             out  (c),a
#             ret
#
#     ; consume (and echo) RX bytes until one equals A
#     wait_for:
#             ld   d,a
#     wf_next:
#             call rx_byte
#             cp   d
#             jr   nz, wf_next
#             ret
# ---------------------------------------------------------------------------
GUEST_CODE = bytes([
    0xF3, 0x21, 0x00, 0x81, 0x7E, 0xB7, 0x28, 0x0A, 0xCD, 0x17, 0x80, 0x7E,
    0x23, 0xCD, 0x44, 0x80, 0x18, 0xF2, 0xCD, 0x30, 0x80, 0x18, 0xFB, 0x56,
    0x23, 0x7A, 0xB7, 0xC8, 0x5E, 0x23, 0x15, 0x01, 0x3B, 0x13, 0xED, 0x78,
    0xE6, 0x02, 0x20, 0xF7, 0x7B, 0x01, 0x3B, 0x13, 0xED, 0x79, 0x18, 0xE9,
    0x01, 0x3B, 0x13, 0xED, 0x78, 0xE6, 0x01, 0x28, 0xF7, 0x01, 0x3B, 0x14,
    0xED, 0x78, 0x01, 0xFE, 0xCA, 0xED, 0x79, 0xC9, 0x57, 0xCD, 0x30, 0x80,
    0xBA, 0x20, 0xFA, 0xC9,
])
SCRIPT_TABLE_ORG = 0x8100        # code is padded out to here

# What the guest sends to the peer, and what the peer answers. The reply is
# CRLF-terminated so the magic port's LINE mode flushes it as one line; a raw
# payload would sit in the line buffer for ever and the row could not see it.
PAYLOAD = b"HELLO"
REPLY = b"JNEXT-ESP-OK\r\n"

# The peer must outlive the emulator (see the module docstring) but must not
# outlive a crashed shell. 10 minutes is far past the row's ~5 s.
WATCHDOG_S = 600


def local_ipv4():
    """The address this host would use to reach the outside world.

    A connected UDP socket only sets the local routing state — nothing is sent
    and nothing needs to be reachable, so this works with the network cable
    out. 192.0.2.1 is TEST-NET-1 (RFC 5737), reserved for exactly this.
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


def build_guest(host, port):
    def rec(line, expect):
        assert len(line) < 256
        return bytes([len(line)]) + line + expect

    # NXtel's evidenced init sequence, then a real connect / send / receive.
    # AT+CIPCLOSE with nothing open answers ERROR, and that is deliberate —
    # nextsync loops it WHILE `ERROR` is not seen (design doc §5.2), so the
    # row would catch an engine that answered OK there.
    table = (rec(b"ATE0\r\n", b"K")
             + rec(b"AT+CIPCLOSE\r\n", b"R")
             + rec(b"AT+CIPMUX=0\r\n", b"K")
             + rec(b'AT+CIPSTART="TCP","%s",%d\r\n' % (host.encode(), port), b"K")
             + rec(b"AT+CIPSEND=%d\r\n" % len(PAYLOAD), b">")
             + rec(PAYLOAD, b"S")
             + b"\x00")
    assert len(GUEST_CODE) <= SCRIPT_TABLE_ORG - 0x8000
    return GUEST_CODE + bytes(SCRIPT_TABLE_ORG - 0x8000 - len(GUEST_CODE)) + table


def main(argv):
    guest_path, ready_path, received_path = argv[1], argv[2], argv[3]

    ip = local_ipv4()
    if not is_rfc1918(ip):
        print("no RFC1918 IPv4 on this host (got %r); the ESP address policy "
              "denies loopback, so there is nowhere to listen" % (ip,))
        return 3

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((ip, 0))
    srv.listen(1)
    port = srv.getsockname()[1]

    with open(guest_path, "wb") as f:
        f.write(build_guest(ip, port))
    # Written LAST and atomically-enough: the shell waits for this file, so it
    # must not appear before the listener and the guest are both ready.
    with open(ready_path, "w") as f:
        f.write("%s %d\n" % (ip, port))

    srv.settimeout(WATCHDOG_S)
    conn, addr = srv.accept()
    conn.settimeout(WATCHDOG_S)
    got = b""
    while len(got) < len(PAYLOAD):
        chunk = conn.recv(64)
        if not chunk:
            break
        got += chunk
    conn.sendall(REPLY)
    with open(received_path, "wb") as f:
        f.write(got)
    print("peer: %s sent %r" % (addr[0], got))
    sys.stdout.flush()

    time.sleep(WATCHDOG_S)       # hold the connection open; the shell kills us
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
