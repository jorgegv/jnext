#!/usr/bin/env python3
"""The peer half of `esp-udp-sntp-func` (GH #198).

The datagram twin of `esp-loopback-peer.py`, and it exists for the same reason
that one does: the unit suites each replace one end of the path with a fake, so
NOTHING else proves a Z80 guest reaching a REAL UDP socket through port 0x133B,
`UartChannel`, `EspUartAdapter`, `AtEngine` and `EspGatedTransport`, and every
byte of a real datagram back again.

It does three things, in this order, and they are cohesive rather than merely
bundled: it can only write the guest program once it knows its own address.

  1. Binds a UDP socket on an RFC1918 address and an ephemeral port.
  2. Writes the Z80 guest that talks to exactly that address:port.
  3. Serves ONE SNTP exchange — reads the client request, checks it is a
     well-formed NTP client packet, answers a real server packet, and records
     both so the shell can assert them.

WHAT MAKES THIS DIFFERENT FROM THE TCP ROW, and why it is not a copy:

  * THE GUEST REPRODUCES newt's OFF-BY-ONE ON PURPOSE. newt's `uart_tx_bin`
    (`uart.c`) is `do { ... } while (size--)`, which transmits `size + 1` bytes:
    after `AT+CIPSEND=48` it puts FORTY-NINE on the wire, the 49th being one
    byte past its own `calloc(48)`. The engine must transmit exactly 48 and must
    not let the stray byte — which lands in its command-line buffer — hold the
    reply's `+IPD` back for ever. Both are asserted: the peer checks it received
    exactly 48 bytes, and the trace has to contain the `+IPD`.

  * THE MAGIC PORT RUNS IN HEX MODE, not LINE mode. An SNTP reply is 48 bytes of
    binary, so a line-buffered trace would flush at whatever CR/LF the timestamps
    happened to contain. Hex gives the shell the exact byte stream instead, which
    is what lets it assert the whole session — `CONNECT` included — rather than a
    summary of it.

WHY NOT 127.0.0.1. Identical to the TCP row: the emulated ESP's address policy
denies loopback by default (design doc §8.2) and a test is not a reason to
weaken a security decision. RFC1918 is ALLOWED by that same decision, so this
binds the host's own private address. No packet leaves the host. If the host has
no RFC1918 address, the row SKIPs rather than pretending.

Usage: esp-udp-sntp-peer.py <guest.bin> <ready.txt> <received.bin> <sent.bin>
Exit 3 = no usable address (the shell turns that into a SKIP).
"""

import socket
import struct
import sys
import time

# ---------------------------------------------------------------------------
# The guest — byte-for-byte the table walker `esp-loopback-peer.py` documents.
#
# It is duplicated rather than shared because the two rows must be able to fail
# independently: a change that broke the walker should not take out both the
# only TCP end-to-end row and the only UDP one at once, leaving nothing that
# still proves the socket path works at all. The listing of record is in
# esp-loopback-peer.py; the bytes are pinned in both, and any drift between the
# bytes and the listing shows up as a failing row here as well, because this row
# also asserts the exact stream the program produces.
#
# The script table at 0x8100 is a list of [len][bytes...][expect] records: send
# the bytes, then consume RX until `expect` arrives. Everything received is
# echoed to the magic port on the way past, so the emulator's stderr IS the wire.
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

NTP_PORT_UNUSED = 123            # named for the reader; the peer binds ephemeral

# The 48-byte SNTP client request newt builds (`sntp.c`): li_vn_mode has VN 4
# and mode 3 (client) and everything else is zero except the transmit
# timestamp. 0x23 = 0b00_100_011.
NTP_CLIENT_MODE_BYTE = 0x23
NTP_PACKET_LEN = 48

# newt's off-by-one: one byte past a 48-byte calloc block. 0x00 is both a
# realistic value for that position and the one that matters — anything that is
# not 'A' proves the engine no longer lets debris block a URC.
STRAY_BYTE = 0x00

# A fixed instant, so the reply is deterministic and the row cannot depend on
# the wall clock: 2026-01-01 00:00:00 UTC in NTP seconds (Unix + 2208988800).
NTP_EPOCH_OFFSET = 2208988800
FIXED_UNIX_TIME = 1767225600
FIXED_NTP_SECONDS = FIXED_UNIX_TIME + NTP_EPOCH_OFFSET

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


def ntp_client_request():
    """The request newt sends: VN 4, mode 3, transmit timestamp set."""
    pkt = bytearray(NTP_PACKET_LEN)
    pkt[0] = NTP_CLIENT_MODE_BYTE
    struct.pack_into(">I", pkt, 40, FIXED_NTP_SECONDS)   # transmit timestamp
    return bytes(pkt)


def ntp_server_reply(request):
    """A real SNTP server packet: VN 4, mode 4, stratum 2, timestamps filled.

    Not a blob of filler — the row is about SNTP, so the bytes on the wire are
    the ones an SNTP client would parse, and the originate timestamp is echoed
    from the request exactly as RFC 4330 requires.
    """
    pkt = bytearray(NTP_PACKET_LEN)
    pkt[0] = 0x24            # LI 0, VN 4, mode 4 (server)
    pkt[1] = 2               # stratum 2 — NOT 0, which would be Kiss-o'-Death
    pkt[2] = 4               # poll
    pkt[3] = 0xFA            # precision, -6
    pkt[12:16] = b"LOCL"     # reference id
    struct.pack_into(">I", pkt, 16, FIXED_NTP_SECONDS)   # reference
    pkt[24:32] = request[40:48]                          # originate = client's transmit
    struct.pack_into(">I", pkt, 32, FIXED_NTP_SECONDS)   # receive
    struct.pack_into(">I", pkt, 40, FIXED_NTP_SECONDS)   # transmit
    return bytes(pkt)


def build_guest(host, port):
    def rec(line, expect):
        assert len(line) < 256
        return bytes([len(line)]) + line + expect

    request = ntp_client_request()

    # newt's `sntp` path, in its own order (`main.c` -> `sntp.c` -> `net.c`):
    #   ATE0                              echo off
    #   AT+CIPCLOSE                       "ensure no open connections" — answers
    #                                     ERROR with nothing open, which newt
    #                                     accepts (`check_end` takes OK or ERROR)
    #   AT+CIPSTART="UDP",<server>,123    net_connect_udp; it reads ONE line and
    #                                     demands it start with CONNECT
    #   AT+CIPSEND=48 + 48 payload bytes  net_send_data / net_send_bin
    #   (+IPD)                            net_recv_data
    # The expect bytes are sync points, not parses: each is the first byte of
    # that reply that cannot occur before it.
    table = (rec(b"ATE0\r\n", b"K")
             + rec(b"AT+CIPCLOSE\r\n", b"R")
             + rec(b'AT+CIPSTART="UDP","%s",%d\r\n' % (host.encode(), port), b"K")
             + rec(b"AT+CIPSEND=%d\r\n" % NTP_PACKET_LEN, b">")
             # 49 bytes for a declared 48 — newt's bug, reproduced deliberately.
             + rec(request + bytes([STRAY_BYTE]), b"S")
             + b"\x00")
    assert len(GUEST_CODE) <= SCRIPT_TABLE_ORG - 0x8000
    return GUEST_CODE + bytes(SCRIPT_TABLE_ORG - 0x8000 - len(GUEST_CODE)) + table


def main(argv):
    guest_path, ready_path, received_path, sent_path = argv[1:5]

    ip = local_ipv4()
    if not is_rfc1918(ip):
        print("no RFC1918 IPv4 on this host (got %r); the ESP address policy "
              "denies loopback, so there is nowhere to bind" % (ip,))
        return 3

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((ip, 0))
    port = sock.getsockname()[1]

    with open(guest_path, "wb") as f:
        f.write(build_guest(ip, port))
    # Written LAST: the shell waits for this file, so it must not appear before
    # the socket and the guest are both ready.
    with open(ready_path, "w") as f:
        f.write("%s %d\n" % (ip, port))

    sock.settimeout(WATCHDOG_S)
    request, addr = sock.recvfrom(4096)
    with open(received_path, "wb") as f:
        f.write(request)

    reply = ntp_server_reply(request if len(request) == NTP_PACKET_LEN
                             else bytes(NTP_PACKET_LEN))
    sock.sendto(reply, addr)
    with open(sent_path, "wb") as f:
        f.write(reply)

    print("peer: %s:%d sent %d byte(s), first byte 0x%02X"
          % (addr[0], addr[1], len(request), request[0] if request else 0))
    sys.stdout.flush()

    # Nothing to hold open — UDP has no connection — but the process outlives
    # the emulator anyway so that a late retransmission is answered rather than
    # provoking an ICMP port-unreachable the engine would report as a failure.
    time.sleep(WATCHDOG_S)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
