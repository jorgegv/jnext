#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# ADAPTED FOR JNEXT (GH #12, dzrp-frontend.md §11 WP-6) from the owner's
# dezogif_ng project — https://github.com/jorgegv/dezogif_ng,
# test/dzrp/queued-commands.py, at commit
# 709ae7d77d444e0e14d88d5c6114b2a7c18e2be6. GPLv3, as jnext is.
#
# WHAT CHANGED, AND WHY. The original makes two clients collide so that the
# stub's ESP transport receives two `+IPD` frames back to back, and adds a third
# that arrives while a HELD command is being answered (dezogif_ng issue #11: a
# frame read while scanning for something else was thrown away). jnext serves
# ONE client at a time over a plain socket (dzrp-frontend.md §2 row 1), so the
# collision cannot be made with three connections. The HAZARD survives the port
# unchanged — commands that arrive while the server is busy answering another
# must each be answered, none lost — and on jnext it lives in the drain-while-
# paused rule (SES-03, dzrp-frontend.md §4.2 item 3): while paused, one pump
# answers a queued chain. So here the queue is made on ONE connection:
#
#   1. while PAUSED, five commands written in one send, then a sixth written
#      the moment the first reply lands (the original's "latecomer": it arrives
#      while the server is still answering the others);
#   2. while RUNNING, three more in one send — one pump per frame then, the
#      other path a queued command can take.
#
# Every reply must come back, in order, with its own sequence number and its
# own payload. Exit 0 if so, 1 otherwise, with the reason on stdout.
# ---------------------------------------------------------------------------
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dzrp  # noqa: E402

HOST = os.environ.get("DZRP_HOST", "127.0.0.1")
PORT = int(os.environ.get("DZRP_PORT", "11000"))
TIMEOUT = float(os.environ.get("DZRP_TIMEOUT", "15"))

BIG_READ_ADDR = 0x4000
BIG_READ_LEN = 1024


def read_mem(addr, n):
    return b"\x00" + addr.to_bytes(2, "little") + n.to_bytes(2, "little")


def queue(d, commands):
    """Write every command in ONE send; return [(seq, check)] in order."""
    frames, expect = b"", []
    for cmd, payload, check in commands:
        frame, seq = d.build_command(cmd, payload)
        frames += frame
        expect.append((seq, check))
    d.send_raw(frames)
    return expect


def collect(d, expect, label):
    """Read len(expect) responses; notifications are kept aside."""
    problems = []
    for i, (seq, check) in enumerate(expect):
        while True:
            got_seq, body = d._read_frame()
            if got_seq == 0:
                d.notifications.append(body)
                continue
            break
        if got_seq != seq:
            problems.append("%s reply %d carries seq %d, expected %d" % (label, i + 1, got_seq, seq))
            break
        why = check(body)
        if why:
            problems.append("%s reply %d: %s" % (label, i + 1, why))
    return problems


def echo_of(payload):
    return lambda body: None if body == payload else "not its own %d-byte echo" % len(payload)


def main():
    try:
        d = dzrp.Dzrp(dzrp.open_remote("tcp:%s:%d" % (HOST, PORT), timeout=TIMEOUT),
                      start_byte=None, base_timeout=TIMEOUT)
        d.command(dzrp.CMD_INIT, dzrp.init_payload())
    except Exception as e:  # noqa: BLE001
        print("could not connect and initialise (%s: %s) — nothing was tested"
              % (type(e).__name__, e))
        return 1

    problems = []
    try:
        # --- 1. paused: a chain of five, then a latecomer -------------------
        p11, p22, p33 = bytes([0x11]) * 24, bytes([0x22]) * 24, bytes([0x33]) * 24
        expect = queue(d, [
            (dzrp.CMD_LOOPBACK, p11, echo_of(p11)),
            (dzrp.CMD_READ_MEM, read_mem(BIG_READ_ADDR, BIG_READ_LEN),
             lambda b: None if len(b) == BIG_READ_LEN else "a %d-byte read came back as %d"
             % (BIG_READ_LEN, len(b))),
            (dzrp.CMD_LOOPBACK, p22, echo_of(p22)),
            (dzrp.CMD_GET_REGISTERS, b"",
             lambda b: None if len(b) == 37 else "a %d-byte register block" % len(b)),
            (dzrp.CMD_GET_TBBLUE_REG, b"\x00",
             lambda b: None if len(b) == 1 else "%d bytes for one NextREG" % len(b)),
        ])
        problems += collect(d, expect[:1], "paused")
        # The first reply has landed: the server is answering the other four.
        expect += queue(d, [(dzrp.CMD_LOOPBACK, p33, echo_of(p33))])
        problems += collect(d, expect[1:], "paused")

        # --- 2. running: three in one send ----------------------------------
        d.command(dzrp.CMD_CONTINUE, bytes(11))
        ps = [bytes([0x40 + i]) * 16 for i in range(3)]
        expect = queue(d, [(dzrp.CMD_LOOPBACK, p, echo_of(p)) for p in ps])
        problems += collect(d, expect, "running")
        d.command(dzrp.CMD_PAUSE)
    except Exception as e:  # noqa: BLE001
        problems.append("%s: %s" % (type(e).__name__, e))
    finally:
        dzrp.send_close_quietly(d)

    if problems:
        for p in problems:
            print(p)
        print("a queued command was lost or answered out of order")
        return 1
    print("5 commands queued while paused plus a latecomer, and 3 while running: every "
          "reply in order, its own seq and payload")
    return 0


if __name__ == "__main__":
    sys.exit(main())
