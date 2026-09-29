#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# ADAPTED FOR JNEXT (GH #12, dzrp-frontend.md §11 WP-6) from the owner's
# dezogif_ng project — https://github.com/jorgegv/dezogif_ng,
# test/dzrp/abandoned-send-client.py, at commit
# 709ae7d77d444e0e14d88d5c6114b2a7c18e2be6. GPLv3, as jnext is.
#
# THE QUESTION IS THE ORIGINAL'S: two clients in a row, the first one's reply
# lost — can the remote still serve anybody afterwards?
#
# WHAT CHANGED, AND WHY. The original loses the reply inside the stub's ESP
# send (an `AT+CIPSEND` it abandoned, dezogif_ng issue #16) and reads its
# precondition off the stub's screen. jnext writes to a socket, so the reply is
# lost the way a crashed DeZog loses one: client 1 asks for a 64 KB memory read
# and closes WITHOUT reading a byte of it. The contract is SES-01 again
# (backend.md CAP-SES-01, dzrp-frontend.md §4.1): the write to a peer that has
# gone ends the session, the session's own pause (CMD_INIT's) is released, and
# the next client is served. So client 1 also leaves a 32-bit counter loop at
# the PC, and the verdict is client 2's:
#
#   * CMD_INIT is answered — the server is back to accepting clients;
#   * the counter has run — client 1's pause did not outlive it.
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
GAP = float(os.environ.get("DZRP_GAP", "1.0"))

# The same 32-bit counter loop as orphan-notify.py.
COUNTER_LOOP = bytes([0x21, 0x00, 0x90, 0x34, 0x20, 0xFA, 0x23, 0x34, 0x20, 0xF6,
                      0x23, 0x34, 0x20, 0xF2, 0x23, 0x34, 0x18, 0xEE])
COUNTER = 0x9000
REG_PC, REG_SP = 0, 1


def w16(v):
    return v.to_bytes(2, "little")


def main():
    try:
        d = dzrp.Dzrp(dzrp.open_remote("tcp:%s:%d" % (HOST, PORT), timeout=TIMEOUT),
                      start_byte=None, base_timeout=TIMEOUT)
        d.command(dzrp.CMD_INIT, dzrp.init_payload())
        d.command(dzrp.CMD_WRITE_MEM, b"\x00" + w16(COUNTER) + bytes(4))
        d.command(dzrp.CMD_WRITE_MEM, b"\x00" + w16(0x8000) + COUNTER_LOOP)
        d.command(dzrp.CMD_SET_REGISTER, bytes([REG_PC]) + w16(0x8000))
        d.command(dzrp.CMD_SET_REGISTER, bytes([REG_SP]) + w16(0x9F00))
    except Exception as e:  # noqa: BLE001
        print("PRECONDITION: client 1 could not set the debuggee up (%s: %s)"
              % (type(e).__name__, e))
        return 2

    # A 65535-byte read, and gone before a byte of the answer is read.
    frame, _ = d.build_command(dzrp.CMD_READ_MEM, b"\x00" + w16(0) + w16(0xFFFF))
    d.send_raw(frame)
    d.close()
    print("client 1 asked for 65535 bytes and closed without reading them")

    time.sleep(GAP)

    t0 = time.time()
    try:
        d2 = dzrp.Dzrp(dzrp.open_remote("tcp:%s:%d" % (HOST, PORT), timeout=TIMEOUT),
                       start_byte=None, base_timeout=TIMEOUT)
        d2.command(dzrp.CMD_INIT, dzrp.init_payload())
        n = struct.unpack("<I", d2.command(dzrp.CMD_READ_MEM,
                                           b"\x00" + w16(COUNTER) + w16(4)))[0]
        dzrp.send_close_quietly(d2)
    except Exception as e:  # noqa: BLE001
        print("client 2 was not served (%s: %s)" % (type(e).__name__, e))
        return 1
    if n < 10000:
        print("client 2 was served, but the counter is %d: client 1's pause outlived it" % n)
        return 1
    print("client 2's CMD_INIT answered in %.2fs, and the machine ran after client 1 went "
          "(counter %d)" % (time.time() - t0, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
