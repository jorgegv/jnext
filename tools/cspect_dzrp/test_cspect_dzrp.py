"""Self-contained test suite for cspect_dzrp.

Validates:
  * Frame encode/decode (request, response, pause-notification).
  * Round-trip parsing of GET_REGISTERS payload.
  * End-to-end client-server interaction via a synthetic server stub that
    speaks the plugin's frame format. CSpect itself is NOT required.

Run:
    python3 -m unittest test_cspect_dzrp.py -v
or:
    python3 test_cspect_dzrp.py
"""

from __future__ import annotations

import socket
import struct
import threading
import time
import unittest

import cspect_dzrp as dz


# --------------------------------------------------------------------------- #
# Synthetic plugin server                                                     #
# --------------------------------------------------------------------------- #


class FakeCSpectServer:
    """Minimal stand-in for CSpect+DezogPlugin on a localhost port.

    Serves enough of the protocol for our client tests:
      * CMD_INIT
      * CMD_GET_REGISTERS (with canned register & slot values)
      * CMD_READ_MEM (returns byte = address & 0xFF)
      * CMD_GET_TBBLUE_REG (returns reg ^ 0x55)
      * CMD_ADD_BREAKPOINT (returns id=42)
      * CMD_REMOVE_BREAKPOINT (empty response)
      * CMD_CONTINUE (empty response, then a delayed PAUSE notification)
      * CMD_PAUSE (empty response)
      * CMD_SET_REGISTER (empty response)
      * CMD_WRITE_MEM (empty response)
      * CMD_SET_SLOT (1-byte zero error)
      * CMD_CLOSE (empty response, server then drops the connection)

    Captures every received frame for later assertion.
    """

    def __init__(self) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.received: list[tuple[int, int, bytes]] = []
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.client_conn: socket.socket | None = None
        # Canned register data — picked so each field is distinct.
        self.regs_payload = self._build_regs_payload()

    @staticmethod
    def _build_regs_payload() -> bytes:
        words = [0x01ED, 0xFF40, 0xCAFE, 0xBEEF, 0xDEAD, 0x3EAF,
                 0x1234, 0x5678, 0xABCD, 0x1111, 0x2222, 0x3333]
        body = struct.pack("<12H", *words)
        body += bytes([0x7E, 0x3F, 0x01, 0x00])  # R, I, IM, reserved
        body += bytes([0x08])  # slot count
        body += bytes([0xFF, 0xFF, 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01])  # slots
        return body

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        try:
            self.sock.close()
        except Exception:
            pass

    def _recv_frame(self, conn: socket.socket) -> tuple[int, int, bytes] | None:
        # CSpect request framing: Length = payload bytes only.
        # Total frame = 4 (length) + 1 (seqno) + 1 (cmd) + Length.
        hdr = self._recv_n(conn, 4)
        if hdr is None:
            return None
        (length,) = struct.unpack("<I", hdr)
        body = self._recv_n(conn, 2 + length)
        if body is None:
            return None
        return body[0], body[1], body[2:]

    @staticmethod
    def _recv_n(conn: socket.socket, n: int) -> bytes | None:
        buf = bytearray()
        while len(buf) < n:
            try:
                chunk = conn.recv(n - len(buf))
            except OSError:
                return None
            if not chunk:
                return None
            buf.extend(chunk)
        return bytes(buf)

    def _send(self, conn: socket.socket, frame: bytes) -> None:
        conn.sendall(frame)

    def _serve(self) -> None:
        try:
            conn, _ = self.sock.accept()
        except OSError:
            return
        self.client_conn = conn
        try:
            while True:
                msg = self._recv_frame(conn)
                if msg is None:
                    break
                seqno, cmd, payload = msg
                self.received.append((seqno, cmd, payload))
                self._dispatch(conn, seqno, cmd, payload)
                if cmd == int(dz.Cmd.CLOSE):
                    break
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def _dispatch(self, conn: socket.socket, seqno: int, cmd: int, payload: bytes) -> None:
        c = dz.Cmd
        if cmd == c.INIT:
            resp = (bytes([0])               # err
                    + bytes([2, 0, 0])       # version
                    + bytes([4])             # machine = ZXNEXT
                    + b"FakeCSpect\x00")
            self._send(conn, dz.encode_response(seqno, resp))
        elif cmd == c.GET_REGISTERS:
            self._send(conn, dz.encode_response(seqno, self.regs_payload))
        elif cmd == c.READ_MEM:
            # payload: reserved, addr_lo, addr_hi, size_lo, size_hi
            _res, addr, size = struct.unpack("<BHH", payload)
            data = bytes((addr + i) & 0xFF for i in range(size))
            self._send(conn, dz.encode_response(seqno, data))
        elif cmd == c.WRITE_MEM:
            self._send(conn, dz.encode_response(seqno))
        elif cmd == c.GET_TBBLUE_REG:
            reg = payload[0]
            self._send(conn, dz.encode_response(seqno, bytes([reg ^ 0x55])))
        elif cmd == c.ADD_BREAKPOINT:
            self._send(conn, dz.encode_response(seqno, struct.pack("<H", 42)))
        elif cmd == c.REMOVE_BREAKPOINT:
            self._send(conn, dz.encode_response(seqno))
        elif cmd == c.CONTINUE:
            # Mirror real plugin: empty response now, PAUSE notification later.
            self._send(conn, dz.encode_response(seqno))
            # Tiny sleep so the client has time to enter wait_for_pause().
            threading.Timer(0.02, lambda: self._send(
                conn,
                dz.encode_pause_ntf(dz.BreakReason.BREAKPOINT_HIT,
                                    long_address=0x01ED,
                                    reason_string="tmp bp"),
            )).start()
        elif cmd == c.PAUSE:
            self._send(conn, dz.encode_response(seqno))
        elif cmd == c.SET_REGISTER:
            self._send(conn, dz.encode_response(seqno))
        elif cmd == c.SET_SLOT:
            self._send(conn, dz.encode_response(seqno, bytes([0])))
        elif cmd == c.READ_PORT:
            self._send(conn, dz.encode_response(seqno, bytes([0xAA])))
        elif cmd == c.WRITE_PORT:
            self._send(conn, dz.encode_response(seqno))
        elif cmd == c.INTERRUPT_ON_OFF:
            self._send(conn, dz.encode_response(seqno))
        elif cmd == c.CLOSE:
            self._send(conn, dz.encode_response(seqno))
        else:
            # Unknown command: send empty response so client doesn't deadlock.
            self._send(conn, dz.encode_response(seqno))


# --------------------------------------------------------------------------- #
# Pure-function frame tests (no socket)                                       #
# --------------------------------------------------------------------------- #


class TestFrames(unittest.TestCase):
    def test_request_roundtrip(self) -> None:
        f = dz.encode_request(7, dz.Cmd.READ_MEM, b"\x00\x34\x12\x40\x00")
        seqno, cmd, payload = dz.decode_request(f)
        self.assertEqual(seqno, 7)
        self.assertEqual(cmd, int(dz.Cmd.READ_MEM))
        self.assertEqual(payload, b"\x00\x34\x12\x40\x00")

    def test_request_length_field_is_LE(self) -> None:
        # Per CSpect Server.cs: Length = payload-only on request side.
        # INIT has zero payload, so Length = 0.
        f = dz.encode_request(1, dz.Cmd.INIT, b"\xAA\xBB")
        # body = seqno(1) + cmd(1) = 2 bytes
        self.assertEqual(f[:4], b"\x02\x00\x00\x00")
        self.assertEqual(f[4], 1)
        self.assertEqual(f[5], int(dz.Cmd.INIT))

    def test_seqno_zero_rejected(self) -> None:
        with self.assertRaises(ValueError):
            dz.encode_request(0, dz.Cmd.INIT)

    def test_pause_ntf_roundtrip(self) -> None:
        frame = dz.encode_pause_ntf(dz.BreakReason.BREAKPOINT_HIT,
                                    long_address=0x040123,
                                    reason_string="hit")
        # frame = len(4) seqno(1)=0 ntf(1)=1 reason(1) addr(3) "hit\0"(4)
        self.assertEqual(frame[4], 0)  # seqno
        self.assertEqual(frame[5], int(dz.Ntf.PAUSE))
        ntf = dz.parse_pause_ntf(frame[5:])
        self.assertEqual(ntf.reason, dz.BreakReason.BREAKPOINT_HIT)
        self.assertEqual(ntf.long_address, 0x040123)
        self.assertEqual(ntf.reason_string, "hit")

    def test_parse_registers(self) -> None:
        payload = FakeCSpectServer._build_regs_payload()
        regs = dz.parse_registers(payload)
        self.assertEqual(regs.PC, 0x01ED)
        self.assertEqual(regs.SP, 0xFF40)
        self.assertEqual(regs.AF, 0xCAFE)
        self.assertEqual(regs.HL, 0x3EAF)
        self.assertEqual(regs.AFp, 0xABCD)
        self.assertEqual(regs.R, 0x7E)
        self.assertEqual(regs.I, 0x3F)
        self.assertEqual(regs.IM, 1)
        self.assertEqual(regs.slots, [0xFF, 0xFF, 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01])

    def test_parse_registers_too_short(self) -> None:
        with self.assertRaises(ValueError):
            dz.parse_registers(b"\x00" * 10)


# --------------------------------------------------------------------------- #
# End-to-end tests against the synthetic server                                #
# --------------------------------------------------------------------------- #


class TestEndToEnd(unittest.TestCase):
    def setUp(self) -> None:
        self.srv = FakeCSpectServer()
        self.srv.start()
        # Connect on the random port the OS gave us.
        self.cli = dz.CSpectDZRP("127.0.0.1", self.srv.port, timeout=2.0)
        self.cli.connect()

    def tearDown(self) -> None:
        self.cli.close()
        self.srv.stop()

    def test_init(self) -> None:
        info = self.cli.init()
        self.assertEqual(info.error, 0)
        self.assertEqual(info.dzrp_version, (2, 0, 0))
        self.assertEqual(info.machine_type, 4)
        self.assertEqual(info.program_name, "FakeCSpect")

    def test_get_registers(self) -> None:
        self.cli.init()
        regs = self.cli.get_registers()
        self.assertEqual(regs.PC, 0x01ED)
        self.assertEqual(regs.HL, 0x3EAF)
        self.assertEqual(regs.slots[2], 0x0A)

    def test_read_mem(self) -> None:
        self.cli.init()
        data = self.cli.read_mem(0x1234, 8)
        # Server returns (addr+i)&0xFF
        expected = bytes(((0x1234 + i) & 0xFF) for i in range(8))
        self.assertEqual(data, expected)

    def test_get_tbblue_reg(self) -> None:
        self.cli.init()
        v = self.cli.get_tbblue_reg(0x07)
        self.assertEqual(v, 0x07 ^ 0x55)

    def test_breakpoint_lifecycle(self) -> None:
        self.cli.init()
        bp = self.cli.add_breakpoint(0x01ED)
        self.assertEqual(bp, 42)
        self.cli.remove_breakpoint(bp)

    def test_continue_then_pause_notification(self) -> None:
        self.cli.init()
        self.cli.cont(tmp_bp1=0x01ED)
        ntf = self.cli.wait_for_pause(timeout=2.0)
        self.assertEqual(ntf.reason, dz.BreakReason.BREAKPOINT_HIT)
        self.assertEqual(ntf.long_address, 0x01ED)

    def test_set_register_payload_shape(self) -> None:
        self.cli.init()
        self.cli.set_register("PC", 0x4000)
        # Inspect the captured frame on server side.
        # Last received frame should be SET_REGISTER with payload [0, 0x00, 0x40]
        seqno, cmd, payload = self.srv.received[-1]
        self.assertEqual(cmd, int(dz.Cmd.SET_REGISTER))
        self.assertEqual(payload[0], 0)  # PC reg num
        self.assertEqual(payload[1:3], b"\x00\x40")

    def test_seqno_advances_and_skips_zero(self) -> None:
        self.cli.init()
        for _ in range(260):
            self.cli.get_tbblue_reg(0x01)
        seqnos = [s for (s, _c, _p) in self.srv.received]
        self.assertNotIn(0, seqnos)
        self.assertTrue(all(1 <= s <= 255 for s in seqnos))


if __name__ == "__main__":
    unittest.main(verbosity=2)
