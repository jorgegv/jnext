#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# ADAPTED FOR JNEXT (GH #12, dzrp-frontend.md §11 WP-6) from the owner's
# dezogif_ng project — https://github.com/jorgegv/dezogif_ng,
# test/dzrp/split-command.py, at commit
# 709ae7d77d444e0e14d88d5c6114b2a7c18e2be6. GPLv3, as jnext is.
#
# WHAT CHANGED, AND WHY. The original splits client A's command across two
# `+IPD` frames and makes client B speak in the gap (dezogif_ng issue #13: the
# stub read the rest of A's payload out of B's frame, and flushed A's reply to
# B's socket). jnext has no `+IPD` framing, and it serves one client at a time:
# B is turned away at once. What survives the port is the hazard itself — a
# command that arrives in pieces must be reassembled from ITS OWN bytes only —
# and on jnext that is the frame reassembly of the DZRP framer (WP-1), with a
# second client's connection landing mid-command. So:
#
#   1. A writes the six header bytes of CMD_GET_TBBLUE_REG and stops;
#   2. B connects and writes a whole CMD_LOOPBACK whose FIRST byte (its length
#      LSB) is exactly the register number a spliced payload would ask for;
#   3. A writes its one payload byte.
#
# A's reply must carry A's sequence number and RIGHT_REG's value — never
# WRONG_REG's (a splice) — and B must be closed having been sent nothing (it was
# never a session). Then a frame dribbled one byte per send must still be
# answered whole, and a fresh client after A must be served.
#
# RIGHT_REG and WRONG_REG are read first and must differ, or the check would be
# comparing a value against itself.
#
# Exit 0 if all of that holds, 1 otherwise, with the reason on stdout.
# ---------------------------------------------------------------------------
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dzrp  # noqa: E402

HOST = os.environ.get("DZRP_HOST", "127.0.0.1")
PORT = int(os.environ.get("DZRP_PORT", "11000"))
TIMEOUT = float(os.environ.get("DZRP_TIMEOUT", "15"))
GAP = 0.05

RIGHT_REG = 0x00        # machine ID
WRONG_REG = 0x03        # machine type — nonzero on a 48K, so it differs from 0x00
B_PAYLOAD = bytes([0x5A]) * WRONG_REG


def connect(init=True):
    t = dzrp.open_remote("tcp:%s:%d" % (HOST, PORT), timeout=TIMEOUT)
    try:
        t.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass
    d = dzrp.Dzrp(t, start_byte=None, base_timeout=TIMEOUT)
    if init:
        d.command(dzrp.CMD_INIT, dzrp.init_payload())
    return d


def read_reg(d, reg):
    body = d.command(dzrp.CMD_GET_TBBLUE_REG, bytes([reg]))
    if len(body) != 1:
        raise dzrp.DzrpError("CMD_GET_TBBLUE_REG answered %d bytes, expected 1" % len(body))
    return body[0]


def main():
    try:
        a = connect()
        right = read_reg(a, RIGHT_REG)
        wrong = read_reg(a, WRONG_REG)
    except Exception as e:  # noqa: BLE001
        print("PRECONDITION: could not connect and read NextREG 0x%02X / 0x%02X (%s: %s)"
              % (RIGHT_REG, WRONG_REG, type(e).__name__, e))
        return 1
    if right == wrong:
        print("PRECONDITION: NextREG 0x%02X and 0x%02X both read 0x%02X — a splice would "
              "be invisible" % (RIGHT_REG, WRONG_REG, right))
        return 1

    failed = False
    a_frame, a_seq = a.build_command(dzrp.CMD_GET_TBBLUE_REG, bytes([RIGHT_REG]))
    b = dzrp.Dzrp(dzrp.open_remote("tcp:%s:%d" % (HOST, PORT), timeout=TIMEOUT),
                  start_byte=None, base_timeout=TIMEOUT)
    b_frame, _ = b.build_command(dzrp.CMD_LOOPBACK, B_PAYLOAD)
    assert b_frame[0] == WRONG_REG

    a.send_raw(a_frame[:6])         # length, seq, command id
    time.sleep(GAP)
    try:
        b.send_raw(b_frame)         # a whole command, from somebody else
    except OSError:
        pass                        # already turned away: equally fine
    time.sleep(GAP)
    a.send_raw(a_frame[6:])         # the register number

    try:
        seq, body = a._read_frame()
        if seq != a_seq:
            print("A was answered with seq %d, not its own %d" % (seq, a_seq))
            failed = True
        elif body != bytes([right]):
            print("A was answered %s — %s" % (body.hex(), "NextREG 0x%02X's value: its payload "
                  "was read out of B's frame" % WRONG_REG if body == bytes([wrong])
                  else "neither register's value"))
            failed = True
    except Exception as e:  # noqa: BLE001
        print("A's split command was never answered (%s: %s)" % (type(e).__name__, e))
        failed = True

    # B was a second client: closed, and sent nothing — not a reply of A's,
    # not an echo of its own.
    b.t.set_timeout(TIMEOUT)
    got = b""
    try:
        while True:
            chunk = b.t.sock.recv(4096)
            if not chunk:
                break
            got += chunk
    except OSError:
        pass
    if got:
        print("the second client was sent %d bytes: %s" % (len(got), got[:16].hex()))
        failed = True
    b.close()

    # A frame dribbled one byte per send is still one command.
    try:
        payload = bytes(range(1, 65))
        frame, seq = a.build_command(dzrp.CMD_LOOPBACK, payload)
        for i in range(len(frame)):
            a.send_raw(frame[i:i + 1])
            time.sleep(0.002)
        rseq, body = a._read_frame()
        if rseq != seq or body != payload:
            print("a byte-at-a-time LOOPBACK came back as seq %d, %d bytes" % (rseq, len(body)))
            failed = True
    except Exception as e:  # noqa: BLE001
        print("a byte-at-a-time LOOPBACK was not answered (%s: %s)" % (type(e).__name__, e))
        failed = True
    dzrp.send_close_quietly(a)

    # And the server serves on.
    try:
        c = connect()
        echo = bytes([0x77]) * 16
        if c.command(dzrp.CMD_LOOPBACK, echo) != echo:
            print("a fresh client afterwards got a wrong echo")
            failed = True
        dzrp.send_close_quietly(c)
    except Exception as e:  # noqa: BLE001
        print("no fresh client was served afterwards (%s: %s)" % (type(e).__name__, e))
        failed = True

    if failed:
        return 1
    print("A's split command answered on its own seq with NextREG 0x%02X = 0x%02X (not "
          "0x%02X's 0x%02X); the second client closed with nothing sent; a byte-at-a-time "
          "frame reassembled; a fresh client served" % (RIGHT_REG, right, WRONG_REG, wrong))
    return 0


if __name__ == "__main__":
    sys.exit(main())
