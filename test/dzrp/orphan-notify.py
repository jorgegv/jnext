#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# ADAPTED FOR JNEXT (GH #12, dzrp-frontend.md §11 WP-6) from the owner's
# dezogif_ng project — https://github.com/jorgegv/dezogif_ng,
# test/dzrp/orphan-notify.py, at commit
# 709ae7d77d444e0e14d88d5c6114b2a7c18e2be6. GPLv3, as jnext is.
#
# THE SCENARIO IS THE ORIGINAL'S: make the remote owe an UNPROMPTED
# notification to a client that has gone — CMD_CONTINUE sent and the socket
# closed in the same breath, with a breakpoint the program is about to hit —
# and then ask whether the remote still serves the next client.
#
# WHAT CHANGED, AND WHY. The original provokes the stop by crashing a zeroed
# debuggee into a stray RST 0 and judges the stub's screen for a TX-timeout
# fault. jnext has no stub and no RST breakpoints; the contract the scenario
# maps onto here is SES-01 (backend.md CAP-SES-01, dzrp-frontend.md §4.1): a
# dropped socket is a CMD_CLOSE, the client's breakpoints go with it, and a
# pause that was ITS OWN is released — "a crashed DeZog must not leave the
# machine hung". So the debuggee is a 32-bit counter loop with a breakpoint on
# its `inc`, and the verdict is taken by a second client:
#
#   * it is served at all (CMD_INIT and a 37-byte register block), and
#   * the counter has run far past the breakpoint: had the orphaned
#     breakpoint survived, or its stop not been released, the loop would sit
#     at 0x8003 with the counter at 0 or 1.
#
# Exit 0 if both hold, 1 otherwise, with the reason on stdout.
# ---------------------------------------------------------------------------
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dzrp  # noqa: E402

HOST = os.environ.get("DZRP_HOST", "127.0.0.1")
PORT = int(os.environ.get("DZRP_PORT", "11000"))
TIMEOUT = float(os.environ.get("DZRP_TIMEOUT", "15"))

# A 32-bit little-endian counter at 0x9000, so it cannot wrap in a run:
#   8000 ld hl,0x9000 / inc (hl) / jr nz,8000 / inc hl / inc (hl) / jr nz,8000
#        / inc hl / inc (hl) / jr nz,8000 / inc hl / inc (hl) / jr 8000
COUNTER_LOOP = bytes([0x21, 0x00, 0x90, 0x34, 0x20, 0xFA, 0x23, 0x34, 0x20, 0xF6,
                      0x23, 0x34, 0x20, 0xF2, 0x23, 0x34, 0x18, 0xEE])
BP_ADDR = 0x8003
COUNTER = 0x9000
REG_PC, REG_SP = 0, 1


def w16(v):
    return v.to_bytes(2, "little")


def connect():
    d = dzrp.Dzrp(dzrp.open_remote("tcp:%s:%d" % (HOST, PORT), timeout=TIMEOUT),
                  start_byte=None, base_timeout=TIMEOUT)
    d.command(dzrp.CMD_INIT, dzrp.init_payload())
    return d


def counter(d):
    return struct.unpack("<I", d.command(dzrp.CMD_READ_MEM, b"\x00" + w16(COUNTER) + w16(4)))[0]


def main():
    try:
        d = connect()
        d.command(dzrp.CMD_WRITE_MEM, b"\x00" + w16(COUNTER) + bytes(4))
        d.command(dzrp.CMD_WRITE_MEM, b"\x00" + w16(0x8000) + COUNTER_LOOP)
        d.command(dzrp.CMD_SET_REGISTER, bytes([REG_PC]) + w16(0x8000))
        d.command(dzrp.CMD_SET_REGISTER, bytes([REG_SP]) + w16(0x9F00))
        bp = d.command(dzrp.CMD_ADD_BREAKPOINT, w16(BP_ADDR) + b"\x00\x00")
        if len(bp) != 2 or bp == b"\x00\x00":
            print("PRECONDITION: the breakpoint was refused (%s)" % bp.hex())
            return 1
    except Exception as e:  # noqa: BLE001
        print("PRECONDITION: could not set the debuggee up (%s: %s)" % (type(e).__name__, e))
        return 1

    # Command and FIN together: the stop this CONTINUE runs into is owed to a
    # client that is already gone.
    frame, _ = d.build_command(dzrp.CMD_CONTINUE, bytes(11))
    d.send_raw(frame)
    d.close()
    print("   sent CMD_CONTINUE and closed the socket in the same breath")

    time.sleep(1.0)

    try:
        d2 = connect()
        regs = d2.command(dzrp.CMD_GET_REGISTERS)
        n = counter(d2)
        dzrp.send_close_quietly(d2)
    except Exception as e:  # noqa: BLE001
        print("FAILED: a new client was not served afterwards (%s: %s)" % (type(e).__name__, e))
        return 1
    if len(regs) != 37:
        print("FAILED: the new client got a %d-byte register block" % len(regs))
        return 1
    if n < 10000:
        print("FAILED: the counter is %d — the machine stayed stopped on the gone client's "
              "breakpoint" % n)
        return 1
    print("the orphaned stop was released and its breakpoint removed: the loop ran on "
          "(counter %d), and a new client was served (37-byte register block)" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
