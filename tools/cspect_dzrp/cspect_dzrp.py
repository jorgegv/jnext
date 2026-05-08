"""cspect_dzrp.py — minimal DZRP client for the CSpect DeZogPlugin.

Protocol verified against:
  https://github.com/maziac/DeZogPlugin (commit on main as of 2026-05-08)
  - Server.cs: enum DZRP { ... }, frame layout, SendResponse(), pause notification
  - Commands.cs: per-handler payload layouts

Wire format
-----------
Request  (host -> CSpect): [len:u32-LE][seqno:u8][cmd:u8][payload...]
Response (CSpect -> host): [len:u32-LE][seqno:u8][payload...]      (cmd not echoed)
Notify   (CSpect -> host): [len:u32-LE][seqno=0][ntf_id:u8][payload...]

`len` = number of bytes that follow the 4-byte length field
        (i.e. seqno + cmd + payload, or seqno + ntf_id + payload).

Sequence numbers
----------------
Client picks a non-zero seqno per request; server echoes it. seqno=0 is reserved
for asynchronous notifications (pause/break events).

Notes / subtleties
------------------
- DZRP version reported by the plugin: 2.0.0
- There is NO separate CMD_GET_SLOTS. The 8 NextReg slot mappings (NR $50..$57)
  are appended to the GET_REGISTERS response.
- There is NO single-step command. Use CONTINUE with one or two temporary
  breakpoints. To "step over an instruction" set TmpBP1 = PC + instruction_len
  and CONTINUE.
- NEXTREG reads use CMD_GET_TBBLUE_REG (id=11), payload = single register byte.
- ADD_BREAKPOINT takes a "long address": addr_lo, addr_hi, bank (bank=0 means
  64K-address, otherwise physical bank+1 like DeZog encodes). Returns a u16 id.
- The plugin maintains *temporary* breakpoints via the CONTINUE payload itself
  (two 16-bit "tmp BP" addresses). These are 64K-address only and are torn down
  automatically on the next stop. Convenient for one-shot run-until.

This module is pure stdlib (socket + struct).
"""

from __future__ import annotations

import enum
import socket
import struct
import threading
import time
from dataclasses import dataclass, field
from typing import Optional


# --------------------------------------------------------------------------- #
# Protocol constants                                                          #
# --------------------------------------------------------------------------- #

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 11000


class Cmd(enum.IntEnum):
    """DZRP command IDs (verified against Server.cs enum DZRP)."""

    INIT = 1
    CLOSE = 2
    GET_REGISTERS = 3
    SET_REGISTER = 4
    WRITE_BANK = 5
    CONTINUE = 6
    PAUSE = 7
    READ_MEM = 8
    WRITE_MEM = 9
    SET_SLOT = 10
    GET_TBBLUE_REG = 11
    SET_BORDER = 12
    SET_BREAKPOINTS = 13
    RESTORE_MEM = 14
    LOOPBACK = 15
    GET_SPRITES_PALETTE = 16
    GET_SPRITES_CLIP_WINDOW_AND_CONTROL = 17
    GET_SPRITES = 18
    GET_SPRITE_PATTERNS = 19
    READ_PORT = 20
    WRITE_PORT = 21
    EXEC_ASM = 22
    INTERRUPT_ON_OFF = 23
    ADD_BREAKPOINT = 40
    REMOVE_BREAKPOINT = 41
    ADD_WATCHPOINT = 42
    REMOVE_WATCHPOINT = 43
    READ_STATE = 50
    WRITE_STATE = 51


class Ntf(enum.IntEnum):
    PAUSE = 1


class BreakReason(enum.IntEnum):
    NO_REASON = 0
    MANUAL_BREAK = 1
    BREAKPOINT_HIT = 2
    WATCHPOINT_READ = 3
    WATCHPOINT_WRITE = 4
    OTHER = 255


# Register-number table for CMD_SET_REGISTER (Commands.cs SetRegister switch).
SET_REG_NUMS = {
    "PC": 0, "SP": 1, "AF": 2, "BC": 3, "DE": 4, "HL": 5,
    "IX": 6, "IY": 7,
    "AF'": 8, "BC'": 9, "DE'": 10, "HL'": 11,
    "IM": 13,
    "F": 14, "A": 15, "C": 16, "B": 17, "E": 18, "D": 19,
    "L": 20, "H": 21, "IXL": 22, "IXH": 23, "IYL": 24, "IYH": 25,
    "F'": 26, "A'": 27, "C'": 28, "B'": 29, "E'": 30, "D'": 31,
    "L'": 32, "H'": 33,
    "R": 34, "I": 35,
}


# --------------------------------------------------------------------------- #
# Data classes                                                                #
# --------------------------------------------------------------------------- #


@dataclass
class Registers:
    """Result of CMD_GET_REGISTERS.

    Layout (Commands.cs:GetRegisters):
        12 LE u16: PC SP AF BC DE HL IX IY AF' BC' DE' HL'
        4  u8:    R I IM 0(reserved)
        1  u8:    slot count (=8)
        8  u8:    NR $50..$57 bank values
    """

    PC: int = 0
    SP: int = 0
    AF: int = 0
    BC: int = 0
    DE: int = 0
    HL: int = 0
    IX: int = 0
    IY: int = 0
    AFp: int = 0
    BCp: int = 0
    DEp: int = 0
    HLp: int = 0
    R: int = 0
    I: int = 0
    IM: int = 0
    slots: list[int] = field(default_factory=lambda: [0] * 8)

    # Convenience accessors for the high/low halves.
    @property
    def A(self) -> int: return (self.AF >> 8) & 0xFF
    @property
    def F(self) -> int: return self.AF & 0xFF
    @property
    def B(self) -> int: return (self.BC >> 8) & 0xFF
    @property
    def C(self) -> int: return self.BC & 0xFF
    @property
    def D(self) -> int: return (self.DE >> 8) & 0xFF
    @property
    def E(self) -> int: return self.DE & 0xFF
    @property
    def H(self) -> int: return (self.HL >> 8) & 0xFF
    @property
    def L(self) -> int: return self.HL & 0xFF

    def format(self) -> str:
        return (
            f"PC={self.PC:04X} SP={self.SP:04X} "
            f"AF={self.AF:04X} BC={self.BC:04X} DE={self.DE:04X} HL={self.HL:04X} "
            f"IX={self.IX:04X} IY={self.IY:04X} | "
            f"AF'={self.AFp:04X} BC'={self.BCp:04X} DE'={self.DEp:04X} HL'={self.HLp:04X} | "
            f"I={self.I:02X} R={self.R:02X} IM={self.IM} | "
            f"slots=[{' '.join(f'{b:02X}' for b in self.slots)}]"
        )


@dataclass
class InitInfo:
    error: int
    dzrp_version: tuple[int, int, int]
    machine_type: int
    program_name: str


@dataclass
class PauseNtf:
    reason: BreakReason
    long_address: int       # bank<<16 | addr16 ; bank=0 -> 64K addr
    reason_string: str


# --------------------------------------------------------------------------- #
# Frame helpers (used by client and the test stub)                            #
# --------------------------------------------------------------------------- #


def encode_request(seqno: int, cmd: int, payload: bytes = b"") -> bytes:
    """Build a request frame.

    Per CSpect DezogPlugin Server.cs: the wire `Length` field on the
    REQUEST side counts ONLY the payload bytes (NOT seqno or cmd). The
    server reads `totalLength = 4 (length) + 2 (seqno+cmd) + Length`.
    Asymmetric vs the response, where Length includes the seqno (but
    not cmd, since responses carry no cmd byte).
    """
    if not (0 < seqno <= 0xFF):
        raise ValueError(f"seqno must be in 1..255, got {seqno}")
    return (struct.pack("<I", len(payload))
            + bytes([seqno & 0xFF, cmd & 0xFF])
            + payload)


def decode_request(frame: bytes) -> tuple[int, int, bytes]:
    """Inverse of encode_request. Returns (seqno, cmd, payload).

    Per server framing: Length field = payload bytes only. Total frame
    size is `4 + 2 + Length`.
    """
    if len(frame) < 6:
        raise ValueError("frame shorter than minimum header")
    (length,) = struct.unpack("<I", frame[:4])
    if length + 6 != len(frame):
        raise ValueError(f"length field {length} disagrees with frame size {len(frame)}")
    return frame[4], frame[5], frame[6:]


def encode_response(seqno: int, payload: bytes = b"") -> bytes:
    """Build a response frame (no cmd byte). `len` = 1(seqno) + payload."""
    body = bytes([seqno & 0xFF]) + payload
    return struct.pack("<I", len(body)) + body


def encode_pause_ntf(reason: BreakReason, long_address: int, reason_string: str = "") -> bytes:
    """Build a NTF_PAUSE notification (seqno=0).

    Payload after seqno: NTF_PAUSE(1), reason(u8), addr(3 bytes LE: lo, mid, bank),
                         reason_string + NUL.
    """
    rs = reason_string.encode("ascii") + b"\x00"
    addr_bytes = bytes([
        long_address & 0xFF,
        (long_address >> 8) & 0xFF,
        (long_address >> 16) & 0xFF,
    ])
    payload = bytes([Ntf.PAUSE, int(reason)]) + addr_bytes + rs
    body = b"\x00" + payload  # seqno=0
    return struct.pack("<I", len(body)) + body


def parse_pause_ntf(payload: bytes) -> PauseNtf:
    """Parse the body *after* the seqno (i.e. starting at NTF_PAUSE byte)."""
    if len(payload) < 5 or payload[0] != int(Ntf.PAUSE):
        raise ValueError(f"not a PAUSE notification: {payload!r}")
    reason = BreakReason(payload[1])
    addr = payload[2] | (payload[3] << 8) | (payload[4] << 16)
    s = payload[5:]
    if s and s[-1] == 0:
        s = s[:-1]
    return PauseNtf(reason, addr, s.decode("ascii", errors="replace"))


def parse_registers(payload: bytes) -> Registers:
    """Parse a CMD_GET_REGISTERS response payload.

    Expected length: 24 (12 words) + 4 (R/I/IM/0) + 1 (slot count) + 8 (slots) = 37.
    """
    if len(payload) < 37:
        raise ValueError(f"registers payload too short: {len(payload)} bytes")
    words = struct.unpack_from("<12H", payload, 0)
    r, i, im, _reserved = struct.unpack_from("<4B", payload, 24)
    slot_count = payload[28]
    if slot_count != 8:
        # Be defensive but don't fail; some plugin versions could change this.
        slot_count = 8
    slots = list(payload[29:29 + 8])
    return Registers(
        PC=words[0], SP=words[1], AF=words[2], BC=words[3], DE=words[4],
        HL=words[5], IX=words[6], IY=words[7],
        AFp=words[8], BCp=words[9], DEp=words[10], HLp=words[11],
        R=r, I=i, IM=im, slots=slots,
    )


# --------------------------------------------------------------------------- #
# Client                                                                      #
# --------------------------------------------------------------------------- #


class DZRPError(RuntimeError):
    pass


class CSpectDZRP:
    """Synchronous DZRP client.

    Notifications (seqno=0) arriving while waiting for a response are placed in
    `self.notifications` and a callback is invoked if registered. The most
    common case is the PAUSE notification that arrives some time after a
    CONTINUE response, so the canonical pattern is::

        client.cont(tmp_bp1=0x01ED)
        ntf = client.wait_for_pause(timeout=10.0)
        regs = client.get_registers()
    """

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT,
                 timeout: float = 5.0) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: Optional[socket.socket] = None
        self._seqno = 0  # incremented before use; first req is seqno=1
        self.notifications: list[PauseNtf] = []
        self._lock = threading.Lock()

    # ---------- connection lifecycle ---------------------------------------

    def connect(self) -> None:
        s = socket.create_connection((self.host, self.port), timeout=self.timeout)
        s.settimeout(self.timeout)
        self._sock = s

    def close(self) -> None:
        if self._sock is not None:
            try:
                # Best-effort polite close.
                try:
                    self._send_only(Cmd.CLOSE)
                except Exception:
                    pass
                self._sock.close()
            finally:
                self._sock = None

    def __enter__(self) -> "CSpectDZRP":
        self.connect()
        self.init()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # ---------- low-level frame I/O ----------------------------------------

    def _next_seqno(self) -> int:
        # Skip 0 (reserved for notifications); wrap 1..255.
        self._seqno = (self._seqno % 0xFF) + 1
        return self._seqno

    def _send_only(self, cmd: Cmd, payload: bytes = b"") -> int:
        """Send a request, return the seqno used. No reply read."""
        if self._sock is None:
            raise DZRPError("not connected")
        seqno = self._next_seqno()
        frame = encode_request(seqno, int(cmd), payload)
        self._sock.sendall(frame)
        return seqno

    def _recv_exact(self, n: int) -> bytes:
        assert self._sock is not None
        buf = bytearray()
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise DZRPError("socket closed by peer")
            buf.extend(chunk)
        return bytes(buf)

    def _recv_frame(self) -> tuple[int, bytes]:
        """Read one frame. Returns (seqno, payload-after-seqno)."""
        hdr = self._recv_exact(4)
        (length,) = struct.unpack("<I", hdr)
        body = self._recv_exact(length)
        if len(body) < 1:
            raise DZRPError("empty body")
        seqno = body[0]
        return seqno, body[1:]

    def _request(self, cmd: Cmd, payload: bytes = b"") -> bytes:
        """Send request, drain notifications, return payload of matching response."""
        with self._lock:
            seqno = self._send_only(cmd, payload)
            while True:
                rxseq, rxpayload = self._recv_frame()
                if rxseq == 0:
                    # Notification — stash and keep reading for response.
                    self._handle_notification(rxpayload)
                    continue
                if rxseq != seqno:
                    raise DZRPError(
                        f"seqno mismatch: sent {seqno}, got {rxseq}"
                    )
                return rxpayload

    def _handle_notification(self, payload: bytes) -> None:
        if not payload:
            return
        if payload[0] == int(Ntf.PAUSE):
            ntf = parse_pause_ntf(payload)
            self.notifications.append(ntf)

    # ---------- public API: commands ---------------------------------------

    def init(self) -> InitInfo:
        p = self._request(Cmd.INIT)
        if len(p) < 5:
            raise DZRPError(f"INIT response too short: {len(p)}")
        err = p[0]
        ver = (p[1], p[2], p[3])
        machine = p[4]
        # Program name is a NUL-terminated string.
        name_bytes = p[5:]
        if name_bytes and name_bytes[-1] == 0:
            name_bytes = name_bytes[:-1]
        return InitInfo(err, ver, machine, name_bytes.decode("ascii", errors="replace"))

    def get_registers(self) -> Registers:
        return parse_registers(self._request(Cmd.GET_REGISTERS))

    def set_register(self, name: str, value: int) -> None:
        if name not in SET_REG_NUMS:
            raise ValueError(f"unknown register {name!r}")
        payload = bytes([SET_REG_NUMS[name]]) + struct.pack("<H", value & 0xFFFF)
        self._request(Cmd.SET_REGISTER, payload)

    def read_mem(self, address: int, size: int) -> bytes:
        if not (0 <= address <= 0xFFFF):
            raise ValueError("address must be 0..0xFFFF")
        if not (0 < size <= 0xFFFF):
            raise ValueError("size must be 1..0xFFFF")
        payload = bytes([0]) + struct.pack("<HH", address, size)
        data = self._request(Cmd.READ_MEM, payload)
        if len(data) != size:
            raise DZRPError(f"READ_MEM returned {len(data)} bytes, expected {size}")
        return data

    def write_mem(self, address: int, data: bytes) -> None:
        if not (0 <= address <= 0xFFFF):
            raise ValueError("address must be 0..0xFFFF")
        payload = bytes([0]) + struct.pack("<H", address) + data
        self._request(Cmd.WRITE_MEM, payload)

    def get_slots(self) -> list[int]:
        """Convenience: NR $50..$57 bank values. Sourced from GET_REGISTERS."""
        return self.get_registers().slots

    def set_slot(self, slot: int, bank: int) -> None:
        if not (0 <= slot <= 7):
            raise ValueError("slot must be 0..7")
        self._request(Cmd.SET_SLOT, bytes([slot, bank & 0xFF]))

    def get_tbblue_reg(self, reg: int) -> int:
        """Read a NextReg by index (CMD_GET_TBBLUE_REG)."""
        p = self._request(Cmd.GET_TBBLUE_REG, bytes([reg & 0xFF]))
        if len(p) != 1:
            raise DZRPError(f"GET_TBBLUE_REG returned {len(p)} bytes")
        return p[0]

    def read_port(self, address: int) -> int:
        p = self._request(Cmd.READ_PORT, struct.pack("<H", address & 0xFFFF))
        if len(p) != 1:
            raise DZRPError(f"READ_PORT returned {len(p)} bytes")
        return p[0]

    def write_port(self, address: int, value: int) -> None:
        self._request(Cmd.WRITE_PORT, struct.pack("<HB", address & 0xFFFF, value & 0xFF))

    def add_breakpoint(self, address: int, bank: int = 0) -> int:
        """Add a *persistent* breakpoint. Returns a non-zero ID.

        bank=0 means a 64K-address breakpoint; bank>0 selects a physical bank
        (the plugin uses bank-1 internally; we pass it through unchanged).
        """
        payload = struct.pack("<HB", address & 0xFFFF, bank & 0xFF)
        p = self._request(Cmd.ADD_BREAKPOINT, payload)
        if len(p) != 2:
            raise DZRPError(f"ADD_BREAKPOINT returned {len(p)} bytes")
        return struct.unpack("<H", p)[0]

    def remove_breakpoint(self, bp_id: int) -> None:
        self._request(Cmd.REMOVE_BREAKPOINT, struct.pack("<H", bp_id & 0xFFFF))

    def cont(self, tmp_bp1: Optional[int] = None,
             tmp_bp2: Optional[int] = None) -> None:
        """Send CMD_CONTINUE. Optional 64K-address temp breakpoints (max 2).

        Plugin response is empty; the actual stop arrives later as a PAUSE
        notification — call wait_for_pause() to block for it.
        """
        b1_en = 1 if tmp_bp1 is not None else 0
        b1 = tmp_bp1 if tmp_bp1 is not None else 0
        b2_en = 1 if tmp_bp2 is not None else 0
        b2 = tmp_bp2 if tmp_bp2 is not None else 0
        payload = struct.pack("<BHBH", b1_en, b1 & 0xFFFF, b2_en, b2 & 0xFFFF)
        self._request(Cmd.CONTINUE, payload)

    def pause(self) -> None:
        self._request(Cmd.PAUSE)

    def step_over_byte(self, n_bytes: int = 1) -> PauseNtf:
        """Convenience: continue until PC advances by `n_bytes`.

        Useful when you know the next instruction's length. For full step-into
        / step-over semantics, use CSpect's own step in the GUI; the plugin
        does not expose a true step command.
        """
        regs = self.get_registers()
        target = (regs.PC + n_bytes) & 0xFFFF
        self.cont(tmp_bp1=target)
        return self.wait_for_pause()

    def interrupt_on_off(self, enable: bool) -> None:
        self._request(Cmd.INTERRUPT_ON_OFF, bytes([1 if enable else 0]))

    # ---------- notifications ---------------------------------------------

    def wait_for_pause(self, timeout: float = 30.0) -> PauseNtf:
        """Block until a PAUSE notification arrives. Returns it."""
        deadline = time.monotonic() + timeout
        # First, serve any already-queued notification.
        if self.notifications:
            return self.notifications.pop(0)
        if self._sock is None:
            raise DZRPError("not connected")
        old_to = self._sock.gettimeout()
        try:
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("wait_for_pause timed out")
                self._sock.settimeout(remaining)
                seqno, payload = self._recv_frame()
                if seqno == 0:
                    self._handle_notification(payload)
                    if self.notifications:
                        return self.notifications.pop(0)
                # Stray response (shouldn't happen): silently drop.
        finally:
            try:
                self._sock.settimeout(old_to)
            except Exception:
                pass


# --------------------------------------------------------------------------- #
# CLI demo                                                                    #
# --------------------------------------------------------------------------- #


def _hexdump(addr: int, data: bytes, width: int = 16) -> str:
    lines = []
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        ascii_ = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{addr + i:04X}  {hexs:<{width * 3 - 1}}  {ascii_}")
    return "\n".join(lines)


def _main() -> int:
    """Tiny CLI: connect, run-to-PC, print regs+slots+memory@HL."""
    import argparse

    ap = argparse.ArgumentParser(description="Minimal DZRP demo client.")
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--run-to", type=lambda s: int(s, 0), default=None,
                    help="Run CSpect until this PC (hex ok, e.g. 0x01ED).")
    ap.add_argument("--mem", type=lambda s: int(s, 0), default=None,
                    help="Address to dump (default: HL).")
    ap.add_argument("--mem-size", type=int, default=64)
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()

    with CSpectDZRP(args.host, args.port) as c:
        info = c.init()
        print(f"# DZRP {'.'.join(map(str, info.dzrp_version))} / "
              f"machine={info.machine_type} / program={info.program_name!r}")

        if args.run_to is not None:
            print(f"# running until PC=0x{args.run_to:04X} ...")
            c.cont(tmp_bp1=args.run_to)
            ntf = c.wait_for_pause(timeout=args.timeout)
            print(f"# stopped: reason={ntf.reason.name} long_addr=0x{ntf.long_address:06X}"
                  f" {ntf.reason_string}")

        regs = c.get_registers()
        print(regs.format())

        target = args.mem if args.mem is not None else regs.HL
        data = c.read_mem(target, args.mem_size)
        print()
        print(f"# memory at 0x{target:04X} (size={args.mem_size}):")
        print(_hexdump(target, data))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
