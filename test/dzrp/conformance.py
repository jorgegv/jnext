#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# COPIED INTO JNEXT (GH #12, dzrp-frontend.md §11 WP-6) from the owner's
# dezogif_ng project — https://github.com/jorgegv/dezogif_ng, test/dzrp/conformance.py,
# at commit 709ae7d77d444e0e14d88d5c6114b2a7c18e2be6 (the files' last change:
# 4eb9359cce05b3e3e95234cedbebfe75997f9ada). GPLv3, as jnext is. A copy, not a
# submodule: it is jnext's regression now, and dezogif_ng stays untouched.
# Unchanged apart from this header. jnext's row runs the checks that apply to
# an emulator remote (dzrp-conformance-func explains which, and why not the rest).
# ---------------------------------------------------------------------------
"""End-to-end DZRP conformance checks against a remote given as a parameter.

    python3 test/dzrp/conformance.py --remote tcp:127.0.0.1:11000
    python3 test/dzrp/conformance.py --remote serial:/dev/ttyUSB0:921600

Run by `make test-dzrp REMOTE=...`. Issue #2.

WHY THIS EXISTS. Nothing else in this repository sends a DZRP command and
checks the reply: `make test` judges screenshots, `make test-mfselect` judges
files on an SD image, and the Z80 unit tests need VS Code. The deliverable of
this project is a protocol implementation, and the protocol had no test.

WHAT IT DELIBERATELY DOES NOT TEST. DeZog owns instruction-length calculation,
the DECISION of where the temporary breakpoints used to step off one belong,
and breakpoint-condition evaluation. Asserting any of those against the remote
would encode the wrong contract, and push whoever tried to make it pass into
building the very thing that fights DeZog at runtime. This suite behaves the
way DeZog behaves.

It does NOT mean the remote keeps no state: the stub necessarily stores the
opcode an RST 0 replaced, because it is the party that patches memory and so
the only one that can un-patch it. Substitution bookkeeping is the remote's;
the decisions are DeZog's.

A PARTIAL REMOTE IS LEGITIMATE. DZRP has 29 commands and remotes implement
different subsets — CSpect's DeZog plugin, for one, does not implement
CMD_LOOPBACK and closes the connection on it. So an unimplemented command is
reported as UNSUPPORTED, not failed, unless it is named in --require. Each
check therefore runs on its own connection: one refused command must not take
the rest of the suite down with it.
"""

import argparse
import os
import struct
import time
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import dzrp  # noqa: E402
# The screen reader hardware bench row H6 already uses, reused rather than
# rewritten: C24 asks whether the remote painted an error for a command it is
# meant to refuse quietly, and the error area is not observable any other way.
# It sends only CMD_READ_MEM and never a CMD_INIT — see _error_area.
import screen  # noqa: E402

PASS, FAIL, UNSUP = "PASS", "FAIL", "UNSUP"

# --------------------------------------------------------------------------
# Progress and colour
#
# A check can take 25 seconds (C12 waits out a timeout; C5 sweeps to 4096
# bytes), so a suite that prints nothing until it ends looks hung. Each check
# announces itself BEFORE it runs and its verdict lands when it finishes.
#
# THE ANNOUNCEMENT GOES TO STDERR, DELIBERATELY. stdout carries the result
# lines, and those are parsed — `hardware-check.py`'s classify() matches lines
# beginning "FAIL " and takes the check code from field 2. Interleaving
# progress chatter into that stream would be inviting the same class of
# breakage the length-convention bug came from. stderr shows up next to it on a
# terminal and stays out of the way of anything reading the results.
#
# COLOUR IS OFF WHEN STDOUT IS NOT A TTY, for the same reason: escape codes in
# front of "FAIL" would stop it starting with "FAIL". `hardware-check.py` runs
# this through a pipe and sets FORCE_COLOR when its own output is a terminal,
# and strips the codes before parsing — so the user still gets colour through
# the hardware bench without the parser ever seeing it.
# --------------------------------------------------------------------------

def _use_colour():
    if os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("FORCE_COLOR"):
        return True
    return sys.stdout.isatty()


COLOUR = _use_colour()

# Whether to draw the in-progress line at all.
#
# ONLY ON A TERMINAL, and the reason is not cosmetic: the verdict overwrites the
# progress line with a carriage return and an erase-to-end-of-line, which needs
# a console to mean anything. Down a pipe those control characters would land in
# a log or in front of a line something else is parsing, and `hardware-check.py`
# matches result lines beginning "FAIL ".
#
# So a pipe gets one clean result line per check and nothing else. Nothing is
# lost there: `hardware-check.py` streams the child's output line by line, so a
# verdict still appears the moment it lands, which is the same information at
# the same time — just without a spinner in front of it.
PROGRESS = sys.stdout.isatty()
_CODES = {PASS: "\033[1;32m", FAIL: "\033[1;31m", UNSUP: "\033[1;33m"}


def paint(status):
    """The status token, coloured when that is safe."""
    if not COLOUR:
        return "%-5s" % status
    return "%s%-5s\033[0m" % (_CODES.get(status, ""), status)

# Command names, for --require and for reporting.
NAMES = {
    "INIT": dzrp.CMD_INIT,
    "CLOSE": dzrp.CMD_CLOSE,
    "LOOPBACK": dzrp.CMD_LOOPBACK,
    "GET_REGISTERS": dzrp.CMD_GET_REGISTERS,
    "SET_REGISTER": dzrp.CMD_SET_REGISTER,
    "READ_MEM": dzrp.CMD_READ_MEM,
    "WRITE_MEM": dzrp.CMD_WRITE_MEM,
    "WRITE_BANK": dzrp.CMD_WRITE_BANK,
    "CONTINUE": dzrp.CMD_CONTINUE,
    "PAUSE": dzrp.CMD_PAUSE,
    "GET_SPRITES": dzrp.CMD_GET_SPRITES,
    "GET_SPRITE_PATTERNS": dzrp.CMD_GET_SPRITE_PATTERNS,
    "SET_BREAKPOINTS": dzrp.CMD_SET_BREAKPOINTS,
    "RESTORE_MEM": dzrp.CMD_RESTORE_MEM,
    "ADD_BREAKPOINT": dzrp.CMD_ADD_BREAKPOINT,
    "REMOVE_BREAKPOINT": dzrp.CMD_REMOVE_BREAKPOINT,
}

# The byte a DZRP breakpoint substitutes (RST 0), and the byte the ROM
# breakpoint check looks for something to CALL. Both are Z80 opcodes rather
# than anything protocol-level, which is why they are here and not in dzrp.py.
BP_OPCODE = 0xC7
RET_OPCODE = 0xC9

# Set from --remote. The execution-control checks need a SECOND connection to
# ask "is the remote still alive?" after an exchange that produced no answer,
# so that a silent remote and a dead one are reported as the different things
# they are (ERRORS.md: a check that can fail for a reason outside its own
# subject has to say so).
REMOTE_SPEC = None

# Set from --no-continue. See CONTROL, below.
NO_CONTINUE = False


class Unsupported(Exception):
    """The remote does not implement this command."""


class Precondition(Exception):
    """Something this check needs went wrong BEFORE its subject was reached.

    Reported as a failure — nothing was established, so it cannot be a pass —
    but worded so it is not read as evidence about the thing under test.
    """


# CMD_INIT is how every DZRP conversation starts. A remote that cannot answer
# it is broken, not partial, so it is never eligible to be excused as
# UNSUPPORTED however it fails.
ALWAYS_REQUIRED = {"INIT"}


def talk(d, cmd_id, payload=b""):
    """Send a command, mapping only a DELIBERATE refusal onto Unsupported.

    A remote that does not know a command closes the connection — that is what
    CSpect's plugin does, and it is not a protocol violation.

    A TIMEOUT IS NOT THAT. It means the remote is waiting for bytes we did not
    send, which is what a wrong length field looks like from this side, and
    treating it as "unimplemented" is how this suite once reported a completely
    broken handshake as 8 UNSUPPORTED checks and exit 0. Timeouts propagate as
    failures.
    """
    try:
        return d.command(cmd_id, payload)
    except dzrp.DzrpError as e:
        if "closed" in str(e):
            raise Unsupported(str(e))
        raise


# --------------------------------------------------------------------------
# checks — each returns (status, detail) and gets a fresh connection
# --------------------------------------------------------------------------


def chk_init(d):
    """Version negotiation: the first command DeZog itself sends."""
    body = talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    if len(body) < 6:
        return FAIL, "response is %d bytes, too short to decode" % len(body)
    err = body[0]
    ver = "%d.%d.%d" % (body[1], body[2], body[3])
    machine = body[4]
    name = body[5:].split(b"\x00")[0].decode("ascii", "replace")
    if err != 0:
        return FAIL, "error code %d (version %s, %r)" % (err, ver, name)
    return PASS, "DZRP %s, machine %d, %r" % (ver, machine, name)


# A check whose PASS is silence must wait LONGER than a normal command, not
# less. Every other check here treats a timeout as failure, which is the safe
# direction: too short only ever produces a loud false FAIL. This one inverts
# that, so too short produces a SILENT false PASS — a green line asserting
# something never established. Hence a multiple of the caller's own --timeout
# rather than a hardcoded number no caller can raise for a slow link.
SILENCE_FACTOR = 2.0


def chk_length_convention(d):
    """Prove the command length convention by violating it.

    An earlier version just sent a well-formed CMD_INIT and declared victory,
    which asserted nothing chk_init did not already assert — a reviewer called
    it a strictly weaker duplicate, correctly.

    So it now sends CMD_INIT with the WRONG length: payload + 2, the symmetric
    reading. A remote that counts the payload only is left waiting for two
    bytes that never arrive, and must not produce a valid response.

    Silence and refusal are reported as DIFFERENT observations, because they
    are: one means the remote is still waiting mid-frame, the other that it
    rejected the frame outright. Collapsing both into one message would
    overclaim what was seen.
    """
    payload = dzrp.init_payload()
    frame, seq = d.build_command(dzrp.CMD_INIT, payload, length=len(payload) + 2)
    wait = d.base_timeout * SILENCE_FACTOR
    d.send_raw(frame)
    d.t.set_timeout(wait)
    try:
        got_seq, _ = d._read_frame()
    except dzrp.Timeout:
        return PASS, "silent for %.0fs after an over-long length" % wait
    except dzrp.DzrpError as e:
        return PASS, "refused an over-long length outright (%s)" % e
    if got_seq == seq:
        return FAIL, "answered a length that counted seq and cmd too"
    # Something arrived that was neither our response nor silence. Say what it
    # was rather than quietly counting it as proof.
    return FAIL, "expected silence, got a frame with seq %d" % got_seq


def chk_preamble(d, expect):
    """Does the remote prefix frames with 0xA5?

    Not a bug hunt — a capability probe. Upstream extended DZRP by this byte
    for the serial link (doc/legacy/Design.md:30-31): DeZog "will wait on this
    byte before it recognizes messages coming from the Next", because a game
    that grabs the joy port makes the Next emit endless zeroes. DeZog's
    ZxNextSerialRemote strips byte 165; its CSpectRemote does not.

    So the correct answer differs by transport: present in UART mode, absent
    for a socket remote. That is why this reports rather than assumes, and why
    --expect-preamble exists for callers who know which they are talking to.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    seen = d.observed_start_byte
    desc = "0xA5" if seen == dzrp.START_BYTE else "none"
    if expect == "report":
        return PASS, "%s (recorded, not asserted)" % desc
    want = dzrp.START_BYTE if expect == "a5" else None
    if seen == want:
        return PASS, "%s, as expected" % desc
    return FAIL, "expected %s, observed %s" % (expect, desc)


def chk_init_consumes_payload(d):
    """The other half of issue #7, which C2 cannot reach.

    C2 over-declares the length and requires SILENCE, so it proves the remote
    reads at least as far as the length promised. It says nothing about the
    opposite direction: whether the remote stops reading at the end of the
    declared payload and leaves the stream where the next command begins.

    This sends a CMD_INIT whose length is HONEST — it counts every byte sent —
    but whose payload carries four bytes after the name's NUL. A remote that
    frames on the NUL instead of on the length consumes fewer bytes than it was
    given, the four survivors become the head of the next command's header, and
    the session is desynchronised from there on. So the assertion is not on this
    command's answer alone: a SECOND, entirely ordinary CMD_INIT follows on the
    same connection and has to come back carrying its own sequence number.

    It is also the "a well-formed CMD_INIT still works" check issue #7 asks for,
    in the only form that is not a weaker duplicate of C1: C1 already proves a
    minimal frame is answered, and would go on passing for a fix that consumed
    too little.

    Nothing on the wire here is malformed. DZRP frames on the length, and a
    client that declares what it sends is entitled to be understood.
    """
    payload = dzrp.init_payload() + b"\xEE" * 4
    body = talk(d, dzrp.CMD_INIT, payload)
    if len(body) < 6:
        return FAIL, "response is %d bytes, too short to decode" % len(body)
    if body[0] != 0:
        return FAIL, "error code %d on a correct length" % body[0]
    # The real assertion. A remote still standing four bytes back reads this
    # command's header out of the leftovers and answers the wrong thing, or
    # nothing at all — either way not a response carrying this sequence number.
    again = talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    if len(again) < 6 or again[0] != 0:
        return FAIL, ("the command after the padded one came back as %d bytes"
                      % len(again))
    return PASS, "4 bytes past the NUL consumed, next command in sync"


def chk_loopback(d):
    """Framing and byte order, without touching machine state. The project's
    dzrp skill puts this first for our own server: if it does not round-trip,
    nothing above it is worth debugging."""
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    data = bytes(range(1, 33))
    got = talk(d, dzrp.CMD_LOOPBACK, data)
    if got != data:
        return FAIL, "sent %d bytes, got %d back and they differ" % (len(data), len(got))
    return PASS, "32 bytes round-tripped unchanged"


# Sizes for the loopback boundary sweep, and every one of them is there for a
# reason:
#
#   0, 1        empty and minimal
#   255, 256    a byte counter's boundary
#   1024        comfortably past any small internal buffer
#   2047..2049  THE TRANSPORT CHUNK BOUNDARY. jnext splits inbound TCP into
#               `+IPD` frames of at most MAX_IPD_CHUNK = 2048 bytes
#               (esp01/include/esp01/esp_at.h:448), so anything larger arrives
#               as SEVERAL headers and the remote has to reassemble across them.
#               Real traffic crosses this routinely — DeZog pushes 8-16 KB per
#               CMD_WRITE_BANK when it loads a .nex — and the first version of
#               this list stopped at 1024, so every payload in the WiFi
#               transport's evidence had arrived in a single frame and its
#               reassembly path was untested.
#   4096        more than two frames, so a remote that handles exactly one split
#               is not mistaken for one that handles any number.
#   8192        THE CEILING, and the largest legal CMD_LOOPBACK there is. Our
#               stub buffers the whole payload into an 8 KB bank paged at
#               SWAP_ADDR before it sends any of it, so this is the last size
#               that fits and one more would land in the next slot — which is
#               where the debugger itself is. C18 is the other side of that
#               boundary. Nothing in the protocol says 8192; it is a property
#               of this remote, and a remote that streams instead of buffering
#               would simply pass both.
LOOPBACK_SIZES = (0, 1, 255, 256, 1024, 2047, 2048, 2049, 4096, 8192)


def chk_loopback_sizes(d):
    """Boundaries, including empty, the transport's frame split, and past it."""
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    for n in LOOPBACK_SIZES:
        payload = bytes((i * 7 + n) & 0xFF for i in range(n))
        got = talk(d, dzrp.CMD_LOOPBACK, payload)
        if got != payload:
            return FAIL, "%d bytes came back as %d and differ" % (n, len(got))
    return PASS, "%d sizes from %d to %d bytes, all exact" % (
        len(LOOPBACK_SIZES), LOOPBACK_SIZES[0], LOOPBACK_SIZES[-1])


def chk_sequence(d):
    """A response must carry the sequence number of the command that asked for
    it; the client raises on a mismatch, so five clean exchanges is the
    assertion."""
    for _ in range(5):
        talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    return PASS, "5 consecutive commands, every sequence number echoed"


def chk_registers(d):
    """DeZog indexes into the register block, so a short answer is a hard
    failure rather than a partial one."""
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    body = talk(d, dzrp.CMD_GET_REGISTERS)
    if len(body) < 28:
        return FAIL, "register block is only %d bytes" % len(body)
    return PASS, "%d-byte register block" % len(body)


def chk_memory(d):
    """Write then read back, in RAM clear of the ROM and of the debugger's own
    slots 6 and 7."""
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    addr, n = 0x8000, 64
    data = bytes((i * 13 + 1) & 0xFF for i in range(n))
    talk(d, dzrp.CMD_WRITE_MEM, b"\x00" + addr.to_bytes(2, "little") + data)
    body = talk(d, dzrp.CMD_READ_MEM,
                b"\x00" + addr.to_bytes(2, "little") + n.to_bytes(2, "little"))
    if body != data:
        return FAIL, "wrote %d bytes at 0x%04X, read %d and they differ" % (n, addr, len(body))
    return PASS, "%d bytes at 0x%04X survived the round trip" % (n, addr)


# ==========================================================================
# Execution control: CMD_CONTINUE, NTF_PAUSE, CMD_PAUSE
#
# THIS IS THE HALF OF ISSUE #2 THAT WAS MISSING, AND IT IS THE ONE THAT
# MATTERS MOST. Everything above judges the debugger while it is STOPPED —
# framing, registers, a memory round trip — none of which needs the debuggee
# to ever run again. Until these checks existed, no evidence anywhere in this
# project showed the stub RESUMING a debuggee: not on hardware, not in the
# emulator. So the exit path and backup.asm's restoration were unexecuted
# code, and so was the AltROM patch, since nothing had ever run an RST 0.
#
# WHAT THEY DO NOT COVER, because the line is fine and easy to lose: the
# stackless-NMI RETURN ADDRESS. C9 sets PC itself with CMD_SET_REGISTER, so
# backup.pc never comes from save_nmi_return_address, which is the routine
# that reads NR 0xC2/0xC3. That runs only on an M1 press taken while
# prgm_state is PRGM_RUNNING — a SECOND NMI, landing after a CMD_CONTINUE —
# and jnext's --delayed-nmi counts emulated frames while this client counts
# wall clock, so scheduling one is a race and not a check. Plan §3.4 calls
# that the half that matters; what C9 establishes is the half it depends on,
# that restore_registers really hands the machine back.
#
# The existing bench check W2 does send a CMD_CONTINUE, and it must NOT be
# read as covering any of this: it resumes zeroed registers on purpose, so the
# machine crashes into a stray RST 0. That is a way of provoking an
# unprompted notification, not a demonstration that the return path works.
#
# HOW THE EVIDENCE IS ARRANGED. A fixture is written into the debuggee's
# memory, the debuggee's PC/SP/BC/IX are pointed at it with CMD_SET_REGISTER,
# and CMD_CONTINUE is sent with a TEMPORARY BREAKPOINT — which is exactly
# what DeZog does on every step, and therefore the mechanism this suite is
# allowed to use. The fixture then leaves three independent traces:
#
#   * a progress marker in memory, which can only be there if the debuggee
#     executed;
#   * the values of BC and IX AS THE DEBUGGEE SAW THEM, stored to memory by
#     its very first two instructions — evidence that restore_registers put
#     them back rather than that the debugger remembers what it was told;
#   * a byte written only by the instruction AFTER the breakpoint, which must
#     stay zero, so "stopped at the breakpoint" is distinguished from "ran
#     past it".
#
# plus the NTF_PAUSE itself and the register block read back afterwards.
#
# WHAT IS DELIBERATELY NOT ASSERTED. DeZog owns instruction lengths, the choice
# of where a temporary breakpoint goes, and condition evaluation. This suite
# therefore names the address itself, in CMD_CONTINUE's payload, exactly as the
# client does. It does NOT expect the remote to be stateless: un-patching an
# RST 0 requires the remote to have kept the byte it replaced.
# ==========================================================================

# All of these sit inside 0x8000-0x9FFF, which CMD_INIT maps to bank 4 (see
# cmd_init's "reset slots to ZX128 default"), clear of the ROM at 0x0000 and
# of the debugger's own slots 6 and 7. One bank, so nothing here depends on
# the SWAP window being right — C8 already covers memory access.
DBG_CODE = 0x8000       # the fixture
DBG_MARK = 0x9800       # its six marker bytes
DBG_STACK = 0x9F00      # the debuggee's SP while it runs
MARK_LEN = 6

# Values CMD_SET_REGISTER puts in before the resume; the fixture stores them
# to memory, so seeing them proves the RESUMED PROGRAM held them.
PRESET_BC = 0xC0DE
PRESET_IX = 0xFACE

# Values the fixture loads while running; seeing them in the register block
# proves the debugger captured the program's state on the way back in.
RUN_HL, RUN_DE, RUN_BC, RUN_A = 0x1234, 0x5678, 0x9ABC, 0x5A

# Written only by the instruction after the breakpoint. Must stay zero.
PAST_TRAP = 0xC3

# CMD_SET_REGISTER numbers, from the DZRP specification's table.
REG_PC, REG_SP, REG_BC, REG_IX = 0, 1, 3, 6


def _w(v):
    return v.to_bytes(2, "little")


# Hand-assembled, one instruction per line, and the trap's address is DERIVED
# from the length of the first half rather than written down twice.
_BEFORE_TRAP = (
    b"\xED\x43" + _w(DBG_MARK + 0) +        # ld (MARK+0),bc  what BC arrived as
    b"\xDD\x22" + _w(DBG_MARK + 2) +        # ld (MARK+2),ix  what IX arrived as
    b"\x21" + _w(RUN_HL) +                  # ld hl,0x1234
    b"\x11" + _w(RUN_DE) +                  # ld de,0x5678
    b"\x01" + _w(RUN_BC) +                  # ld bc,0x9ABC
    b"\x3E" + bytes([RUN_A]) +              # ld a,0x5A
    b"\x32" + _w(DBG_MARK + 4)              # ld (MARK+4),a   "I executed"
)
_AFTER_TRAP = (
    b"\x00" +                               # nop             <-- THE TRAP
    b"\x3E" + bytes([PAST_TRAP]) +          # ld a,0xC3
    b"\x32" + _w(DBG_MARK + 5) +            # ld (MARK+5),a   "I ran PAST it"
    b"\x18\xFE"                             # jr $            park, do not crash
)
DEBUGGEE = _BEFORE_TRAP + _AFTER_TRAP
DBG_TRAP = DBG_CODE + len(_BEFORE_TRAP)

# The order CMD_GET_REGISTERS returns, from the specification.
REGISTER_ORDER = ("PC", "SP", "AF", "BC", "DE", "HL",
                  "IX", "IY", "AF2", "BC2", "DE2", "HL2")


def decode_registers(body):
    """The 12 words, then R, I, IM, reserved. Slots follow and are ignored."""
    if len(body) < 28:
        raise Precondition("the register block is %d bytes, too short to index"
                           % len(body))
    regs = dict(zip(REGISTER_ORDER, struct.unpack("<12H", body[:24])))
    regs["R"], regs["I"], regs["IM"] = body[24], body[25], body[26]
    return regs


def _write_mem(d, addr, data):
    talk(d, dzrp.CMD_WRITE_MEM, b"\x00" + _w(addr) + data)


def _read_mem(d, addr, n):
    return talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(addr) + _w(n))


def _set_reg(d, num, value):
    talk(d, dzrp.CMD_SET_REGISTER, bytes([num]) + _w(value))


def load_debuggee(d):
    """Put the fixture in memory and aim the debuggee's registers at it.

    The marker area is cleared FIRST and read back, because a stale byte left
    by an earlier check would let "the debuggee ran" pass without it running.
    Anything failing here raises Precondition: it is a memory or register
    fault, and reporting it as a resume fault would be a false attribution.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())

    _write_mem(d, DBG_MARK, bytes(MARK_LEN))
    _write_mem(d, DBG_CODE, DEBUGGEE)

    back = _read_mem(d, DBG_CODE, len(DEBUGGEE))
    if back != DEBUGGEE:
        raise Precondition("the fixture did not land at 0x%04X (%d/%d bytes)"
                           % (DBG_CODE, len(back), len(DEBUGGEE)))
    mark = _read_mem(d, DBG_MARK, MARK_LEN)
    if mark != bytes(MARK_LEN):
        raise Precondition("the marker area at 0x%04X did not clear (%s)"
                           % (DBG_MARK, mark.hex()))

    _set_reg(d, REG_PC, DBG_CODE)
    _set_reg(d, REG_SP, DBG_STACK)
    _set_reg(d, REG_BC, PRESET_BC)
    _set_reg(d, REG_IX, PRESET_IX)


def resume_to_trap(d):
    """CMD_CONTINUE with a temporary breakpoint at the trap; wait for NTF_PAUSE.

    Returns the notification body, or None if none arrived.

    Under --no-continue the CMD_CONTINUE is skipped and the wait is NOT: a
    notification arriving with nothing having asked for one would be a far
    more interesting finding than the one this control is looking for.
    """
    payload = (bytes([1]) + _w(DBG_TRAP) +      # breakpoint 1, enabled
               bytes([0]) + _w(0) +             # breakpoint 2, off
               bytes([0]) +                     # no alternate command
               _w(0) + _w(0))                   # no step-over range
    if not NO_CONTINUE:
        talk(d, dzrp.CMD_CONTINUE, payload)
    try:
        return d.wait_notification()
    except dzrp.Timeout:
        return None


def _remote_still_answers():
    """Can a NEW connection get an answer out of the remote right now?

    Used only to describe a silence accurately. CMD_GET_REGISTERS rather than
    CMD_INIT, so the probe does not reset the remote's session state on its
    way past.
    """
    if REMOTE_SPEC is None:
        return None
    try:
        t = dzrp.open_remote(REMOTE_SPEC, timeout=5.0)
    except (OSError, dzrp.DzrpError):
        return False
    probe = dzrp.Dzrp(t, base_timeout=5.0)
    try:
        return len(probe.command(dzrp.CMD_GET_REGISTERS)) >= 28
    except (OSError, dzrp.DzrpError):
        return False
    finally:
        probe.close()


def _liveness():
    """Three words saying which kind of silence this was.

    "No reply" has three quite different meanings and a verdict line that
    collapses them is a check failing for a reason outside its own subject
    (ERRORS.md):

      remote still serving      it swallowed the command and carried on, so a
                                client blocks for ever — issue #8's shape
      remote stopped answering  this run cannot separate "no reply" from "the
                                command killed it"
      liveness not probed       --remote was not set, so we do not know

    THREE WORDS EACH, DELIBERATELY. Every caller appends this to its own detail,
    so whatever it returns is paid for inside that caller's twenty-word budget;
    a longer phrase here spends words in four checks at once. The meanings are
    written out in doc/DZRP-TESTING.md, which is where a reader has room for
    them.
    """
    alive = _remote_still_answers()
    if alive is True:
        return "remote still serving"
    if alive is False:
        return "remote stopped answering"
    return "liveness not probed"


def chk_continue_resumes(d):
    """THE ONE THAT MATTERS: does CMD_CONTINUE actually restart the debuggee?"""
    load_debuggee(d)
    ntf = resume_to_trap(d)
    if ntf is None:
        return FAIL, ("no NTF_PAUSE within %.0fs: not resumed, or never reached "
                      "0x%04X" % (d.base_timeout, DBG_TRAP))
    if len(ntf) < 6:
        return FAIL, "notification is %d bytes, too short for NTF_PAUSE" % len(ntf)
    if ntf[0] != dzrp.NTF_PAUSE:
        return FAIL, "notification id is %d, expected %d" % (ntf[0], dzrp.NTF_PAUSE)

    reason, addr, bank = ntf[1], int.from_bytes(ntf[2:4], "little"), ntf[4]
    mark = _read_mem(d, DBG_MARK, MARK_LEN)

    problems = []
    if addr != DBG_TRAP:
        problems.append("broke at 0x%04X, not 0x%04X" % (addr, DBG_TRAP))
    if mark[4] != RUN_A:
        problems.append("marker 0x%04X is 0x%02X: the debuggee never executed"
                        % (DBG_MARK + 4, mark[4]))
    if mark[5] != 0:
        problems.append("0x%04X is 0x%02X: it ran past the breakpoint"
                        % (DBG_MARK + 5, mark[5]))
    # The break reason is REPORTED, not asserted. The specification allows
    # 0/1/2/3/4/255 and DeZog does not require a particular one here; our stub
    # answers 0 (no reason) for a temporary breakpoint, which is what a step
    # is. Asserting a value would encode one remote's choice as the protocol.
    if reason not in (0, 1, 2, 3, 4, 255):
        problems.append("break reason %d is not in the specification" % reason)
    if problems:
        # DELIBERATELY OVER THE ~20-WORD BUDGET when more than one axis broke.
        # Each problem above fits it on its own; they COMPOUND, and the worst
        # case here — wrong address, no marker, ran past the breakpoint and an
        # illegal reason all at once — is a 29-word detail. It is not truncated,
        # because a badly broken resume path is exactly what fails on several
        # axes at once, and which ones they are is the diagnosis. Dropping any
        # of them returns the reader to a bare "it did not resume". Same
        # reasoning as hardware-check.py's H3 composite; see doc/DZRP-TESTING.md.
        return FAIL, "; ".join(problems)
    return PASS, ("stopped on the breakpoint at 0x%04X (reason %d, bank %d)"
                  % (addr, reason, bank))


def chk_continue_state(d):
    """Was the debuggee's state RESTORED across the resume, or corrupted?

    Two directions, and both are needed. Inward: BC and IX are read out of
    memory where the resumed program itself put them, so this is what the
    DEBUGGEE held, not what the debugger remembers being told. Outward: the
    register block afterwards must show what the program computed while it
    ran, and an SP that came back to where it started — plan §4.1 warns the
    debuggee's SP can point anywhere, and every RST 0 moves it.
    """
    load_debuggee(d)
    ntf = resume_to_trap(d)
    if ntf is None:
        return FAIL, "no NTF_PAUSE, so no state can be read; see the C10 line"

    mark = _read_mem(d, DBG_MARK, MARK_LEN)
    regs = decode_registers(talk(d, dzrp.CMD_GET_REGISTERS))

    problems = []
    for name, got, want in (
            ("BC", int.from_bytes(mark[0:2], "little"), PRESET_BC),
            ("IX", int.from_bytes(mark[2:4], "little"), PRESET_IX)):
        if got != want:
            problems.append("the debuggee saw %s = 0x%04X, not 0x%04X"
                            % (name, got, want))
    for name, want in (("PC", DBG_TRAP), ("SP", DBG_STACK), ("HL", RUN_HL),
                       ("DE", RUN_DE), ("BC", RUN_BC), ("IX", PRESET_IX)):
        if regs[name] != want:
            problems.append("%s came back 0x%04X, expected 0x%04X"
                            % (name, regs[name], want))
    if regs["AF"] >> 8 != RUN_A:
        problems.append("A came back 0x%02X, expected 0x%02X"
                        % (regs["AF"] >> 8, RUN_A))
    if problems:
        # DELIBERATELY OVER THE ~20-WORD BUDGET, and the worst here is the
        # longest detail either harness can print: nine independent faults — BC
        # and IX inward, six registers outward, and A — joined at 58 words.
        # Each is separately load-bearing: "the state was corrupted" is not a
        # finding, "SP came back somewhere else while everything else survived"
        # is. Truncating would leave the reader unable to tell a restore fault
        # from a capture fault. See chk_continue_resumes above.
        return FAIL, "; ".join(problems)
    return PASS, "BC/IX reached the debuggee, and PC/SP/AF/BC/DE/HL/IX came back as left"


def chk_pause_while_stopped(d):
    """CMD_PAUSE, sent while the remote is already stopped.

    SCOPE, STATED UP FRONT. This is NOT a test of PC-initiated break — but the
    reason has changed, and the old one is struck rather than deleted because it
    was true for the whole life of this check.

    IT USED TO SAY: "Breaking into a FREELY RUNNING program is milestone M2 and
    is not built: mf_rom.asm's nmi66h serves button NMIs only (bench check T4
    asserts that decline deliberately), so with the debuggee running there is
    nothing polling for a byte from the PC and no CMD_PAUSE can be received at
    all. That is a design state, not a bug, and no check here can pass until M2
    changes it."

    EVERY CLAUSE OF THAT IS NOW FALSE (issue #22, built 2026-08-10). nmi66h
    serves a SOFTWARE Multiface NMI as well, raised by a two-instruction Copper
    list that the DEBUGGER installs at cmd_init on a first attach, or that the
    debugged program installs itself if it uses the Copper and so overwrites the
    debugger's (doc/ASYNCHRONOUS-BREAK-DESIGN.md §0.1 — this docstring said only
    the second until 2026-08-15, which is what W10 exists to tell apart); the
    poll reads the link while the debuggee runs; and a check that breaks into a freely running program exists
    and passes — bench W8, test/dzrp/pause-running.py, driven by
    test/run-dzrp-stub.sh. T4 still asserts a decline, and its verdict is
    unchanged, but its reason is not: the software cause is now SERVED and
    correctly declined at a machine where no debugger image is in MAIN_BANK.

    WHAT SURVIVES IS THE SCOPE OF THIS CHECK, which is the sentence that
    mattered: C12 is about CMD_PAUSE arriving while the remote is ALREADY
    STOPPED, which is the other half of the command and reaches cmd_pause rather
    than the poll. The two states are now both reachable and both must work.

    What CAN be asked today is the narrow protocol question: the specification
    gives CMD_PAUSE a Length=1 response — the sequence number and nothing else
    — with no exemption for a remote that is already stopped. So the check is
    simply "is it answered". A remote that refuses the command by closing the
    connection is a legitimate partial remote and reports UNSUPPORTED; SILENCE
    is neither, because DeZog waits for the response and would block.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    try:
        body = talk(d, dzrp.CMD_PAUSE)
    except dzrp.Timeout:
        return FAIL, "no response within %.0fs; %s" % (d.base_timeout, _liveness())
    if body:
        return FAIL, "answered, but with a %d-byte payload" % len(body)
    return PASS, "answered with the sequence number and an empty payload"


SPRITE_COUNT = 4


def _chk_sprite_family(d, cmd, per_item, name):
    """CMD_GET_SPRITES / CMD_GET_SPRITE_PATTERNS: answered, at the exact length.

    WHY THE LENGTH IS NOT NEGOTIABLE, and why "answer something short" is not an
    option for a remote that cannot supply the data. DeZog asserts the size in
    the client — `assert(count*5 == r.length)` for sprites and
    `assert(r.length == 256*count)` for patterns, in the installed 3.7.4 — and
    then slices the reply into fixed-size records. A short reply is a desync,
    not a refusal, and DZRP has no error response to send instead.

    A ZX Next genuinely cannot read either back: ports 0x57 and 0x5B have no
    read decode in the FPGA at all (zxnext.vhd:651-652, and neither appears in
    the read mux at :2803-2806). So the only honest answer at the required
    length is zeros, whose visible bit is clear — an empty sprite view. An
    EMULATOR-side remote answers these for real, because its sprite state is
    host memory; that is a capability difference between targets, and this check
    accepts either as long as the length is right.

    SCOPE. This asserts the two things a client actually suffers — a hang, and a
    desync. It does NOT see the third thing issue #9 reports, that the old
    handler jumped to drain_main and re-initialised the debugger underneath the
    client: `prgm_state` is not observable over the socket. What is observable
    is that the session goes on working, which is asserted by the second command
    below.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    try:
        body = talk(d, cmd, bytes([0, SPRITE_COUNT]))
    except dzrp.Timeout:
        return FAIL, "no response within %.0fs; %s" % (d.base_timeout, _liveness())

    want = SPRITE_COUNT * per_item
    if len(body) != want:
        return FAIL, ("answered with %d bytes, not the %d the client asserts"
                      % (len(body), want))

    # The session must survive it. The handler this replaces reached drain_main,
    # which re-initialises the debugger, so "answered" alone is not the whole
    # question.
    try:
        regs = talk(d, dzrp.CMD_GET_REGISTERS)
    except (dzrp.DzrpError, OSError) as e:
        return FAIL, "right length, but the next command failed (%s)" % e
    if len(regs) < 28:
        return FAIL, ("right length, but the next command came back as %d bytes"
                      % len(regs))

    return PASS, ("%d %s, %d bytes%s, next command in sync"
                  % (SPRITE_COUNT, name, len(body),
                     " of zeros" if not any(body) else " with data"))


def chk_get_sprites(d):
    return _chk_sprite_family(d, dzrp.CMD_GET_SPRITES, 5, "sprite attributes")


def chk_get_sprite_patterns(d):
    return _chk_sprite_family(d, dzrp.CMD_GET_SPRITE_PATTERNS, 256,
                              "sprite patterns")


# ==========================================================================
# Large inbound transfers — the payload sizes a real DeZog session moves.
#
# EVERY CHECK ABOVE STOPS AT 4096, AND THE PRODUCT'S LARGEST INBOUND PAYLOAD IS
# FOUR TIMES THAT. DeZog pushes a bank at a time when it loads a .nex, which is
# 8 KB per CMD_WRITE_BANK, and nothing here had ever sent one — so the biggest
# frame the stub meets in ordinary use was the one frame no check exercised.
#
# WHY NOT SIMPLY EXTEND THE LOOPBACK SWEEP, which was the obvious move. Two
# reasons, and the second is a property of this remote rather than of DZRP:
#
#   * a CMD_LOOPBACK exercises RECEIVE AND SEND TOGETHER, and it is the receive
#     path alone that governs how fast this link can go — the per-byte receive
#     cost is what brackets the UART ceiling between 470 and 610 T-states
#     (MEMORY.md, issue #25). A check that moves 16 KB inward and reads it back
#     in separate commands measures the interesting half on its own.
#   * 8192 IS THE CEILING FOR LOOPBACK on this stub, because it buffers the
#     whole payload into one 8 KB bank before replying. C5 now ends exactly
#     there and C18 is the other side of it.
#
# WHAT THEY ARE NOT. Not timing measurements — nothing here is a benchmark, and
# the emulator is not where throughput is established (H5 on hardware is). They
# assert that the bytes arrive intact at sizes nothing else reaches.
# ==========================================================================

# CMD_INIT maps bank 4 at slot 4 and bank 5 at slot 5 — 0x8000-0xBFFF — which is
# clear of the ROM at 0x0000, of the display file at 0x4000, and of the
# debugger's own slots 6 and 7. So a bank written here can be read straight back
# through an address, with no CMD_SET_SLOT in the middle to go wrong.
BANK_AT_8000 = 4
BANK_SIZE = 8192
BIG_MEM_ADDR = 0x8000
BIG_MEM_LEN = 16384             # slots 4 and 5 together


def _pattern(n, seed):
    """A payload with no run of repeats, so a short write cannot look complete.

    A block of one value would let a remote that dropped the tail pass on
    whatever the bank already held; this makes every offset distinguishable.
    """
    return bytes(((i * 31 + seed) ^ (i >> 8)) & 0xFF for i in range(n))


def chk_write_bank_full(d):
    """A full 8 KB bank in, and read back — the frame DeZog sends on every F5."""
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    data = _pattern(BANK_SIZE, 0x5A)
    body = talk(d, dzrp.CMD_WRITE_BANK, bytes([BANK_AT_8000]) + data)
    # The response is an error byte and a NUL-terminated string; a non-zero code
    # is the remote refusing, which is a different finding from wrong bytes.
    if not body or body[0] != 0:
        return FAIL, "CMD_WRITE_BANK refused a legal 8192-byte bank, error %d" % (
            body[0] if body else -1)
    got = talk(d, dzrp.CMD_READ_MEM,
               b"\x00" + _w(BIG_MEM_ADDR) + _w(BANK_SIZE))
    if got != data:
        return FAIL, "%d bytes written, %d read back and they differ at %s" % (
            BANK_SIZE, len(got), _first_diff(data, got))
    return PASS, "a full %d-byte bank went in and came back byte-identical" % BANK_SIZE


def chk_write_mem_large(d):
    """16 KB in one CMD_WRITE_MEM — the largest single inbound payload there is.

    Larger than any CMD_WRITE_BANK, and unlike CMD_LOOPBACK it is received and
    nothing is sent back in the same command, so what it exercises is the
    receive path on its own. It spans TWO slots, which is the other reason it is
    worth having: memory_loop's banking is what carries it across 0xA000, and no
    other check writes across a slot boundary in one command.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    data = _pattern(BIG_MEM_LEN, 0xA5)
    talk(d, dzrp.CMD_WRITE_MEM, b"\x00" + _w(BIG_MEM_ADDR) + data)
    got = talk(d, dzrp.CMD_READ_MEM,
               b"\x00" + _w(BIG_MEM_ADDR) + _w(BIG_MEM_LEN))
    if got != data:
        return FAIL, "%d bytes written, %d read back and they differ at %s" % (
            BIG_MEM_LEN, len(got), _first_diff(data, got))
    return PASS, "%d bytes crossed a slot boundary in one command, intact" % BIG_MEM_LEN


def _first_diff(a, b):
    """Where two byte strings first differ, as a short string for a verdict."""
    if len(a) != len(b):
        return "length %d vs %d" % (len(a), len(b))
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return "0x%04X" % i
    return "nowhere"


# ==========================================================================
# Breakpoints where ROM is mapped — issue #27.
#
# A DZRP breakpoint is a byte patched into memory: the remote replaces the
# opcode at the address with an RST 0 and keeps the byte it replaced. That
# works wherever the address is writable, and 0x0000-0x3FFF on a stopped Next
# is NOT — which no check here had ever asked, because every fixture in this
# file lives at 0x8000 in a RAM bank.
#
# WHY IT IS NOT WRITABLE, and it is one line of the FPGA. In the normal
# ROM-serving branch of the slot-0/1 decode, `sram_pre_rdonly <= not
# (nr_8c_altrom_en and nr_8c_altrom_rw)` (zxnext.vhd:3056) — NR 0x8C bits 7
# and 6 — and `sram_pre_rdonly` is what gates the physical SRAM cycle at
# :3154. src/altrom.asm:55 leaves NR 0x8C at 10000000b for the whole debug
# session: bit 7 set so the patched Alt ROM SERVES READS, bit 6 clear so a
# write is discarded outright. It is not mismapped and nothing is corrupted;
# the byte simply never reaches memory, and the remote reports success because
# neither breakpoint path reads back what it wrote.
#
# TWO CHECKS, AND THEY ARE NOT THE SAME CHECK TWICE. C19 asks whether the byte
# lands, which localises the fault to the write; C20 asks whether the
# breakpoint FIRES, which is what a user suffers. A fix that made writes land
# but broke the restore would pass C20 and fail C19, and one that patched the
# wrong image would pass C19 and fail C20.
#
# EACH CARRIES ITS OWN CONTROL, IN THE SAME RUN, and that is the whole reason
# they can be believed. A breakpoint that does not fire is indistinguishable
# from a resume that never worked — that confound is what wrecked the one
# attempt to test this at the machine (issue #27's own record of it) — so both
# checks do the identical thing at a RAM address alongside, and report a
# Precondition rather than a verdict if the RAM half misbehaves.
# ==========================================================================

# An ordinary 48K ROM address, nothing special about it: the point is that it
# is somewhere ROM is mapped, not that it is anywhere in particular. The
# trampoline at 0x0000/0x0066 is deliberately NOT used here — a breakpoint
# there is a separate defect with a separate fix, and using it would conflate
# "ROM is not writable" with "the debugger's own code is in the firing line".
ROM_BP_ADDR = 0x1234
# The control, in the bank CMD_INIT maps at 0x8000. Same address C8/C10 use.
RAM_BP_ADDR = 0x8000
ROM_BANK = 255          # what MMU slot 0 holds while the debugger is stopped


def _slot0(d):
    """Which bank MMU slot 0 holds, read back out of CMD_GET_REGISTERS.

    Byte 28 of the payload is the slot COUNT and byte 29 is slot 0 — an
    off-by-one here reads the constant 8 and concludes the ROM is not mapped,
    which is a check that passes for the wrong reason.
    """
    body = talk(d, dzrp.CMD_GET_REGISTERS)
    if len(body) < 37:
        raise Precondition("the register block is %d bytes, too short for the "
                           "slot list" % len(body))
    return body[29]


def _set_bps(d, addrs):
    """CMD_SET_BREAKPOINTS for a list of 64K addresses; returns the old opcodes.

    The reply is one byte per breakpoint — the opcode the remote found before
    it wrote, which is exactly what it must keep in order to un-patch later.
    """
    body = talk(d, dzrp.CMD_SET_BREAKPOINTS,
                b"".join(_w(a) + b"\x00" for a in addrs))
    if len(body) != len(addrs):
        raise Precondition("CMD_SET_BREAKPOINTS answered %d bytes for %d "
                           "breakpoints" % (len(body), len(addrs)))
    return body


def _restore_bps(d, pairs):
    """CMD_RESTORE_MEM, four bytes per entry: address, bank+1, value."""
    talk(d, dzrp.CMD_RESTORE_MEM,
         b"".join(_w(a) + b"\x00" + bytes([v]) for a, v in pairs))


def chk_rom_breakpoint_lands(d):
    """Does the RST 0 of a breakpoint in ROM space actually reach memory?

    Read the byte, set a breakpoint on it, read it again. The RAM breakpoint
    alongside is the control and is asserted as a PRECONDITION: if the byte
    at 0x8000 did not become an RST 0 either, then this run says nothing
    about ROM and reporting it as a ROM finding would be a false attribution.

    THE RESTORE IS PART OF THE SUBJECT, not tidying up. A breakpoint that can
    be set and not removed is worse than one that cannot be set at all — it
    leaves an RST 0 in the image the debuggee executes for the rest of the
    session — so a landed write whose restore does not land is a failure here,
    and the check says which of the two happened.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())

    slot0 = _slot0(d)
    if slot0 != ROM_BANK:
        raise Precondition("MMU slot 0 holds bank %d, not the ROM: 0x%04X is "
                           "not ROM space in this run" % (slot0, ROM_BP_ADDR))

    before = talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(ROM_BP_ADDR) + _w(1))[0]
    if before == BP_OPCODE:
        raise Precondition("0x%04X already reads 0x%02X, so setting a "
                           "breakpoint there proves nothing"
                           % (ROM_BP_ADDR, BP_OPCODE))

    old = _set_bps(d, (ROM_BP_ADDR, RAM_BP_ADDR))
    rom_now = talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(ROM_BP_ADDR) + _w(1))[0]
    ram_now = talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(RAM_BP_ADDR) + _w(1))[0]
    # Put both back before judging anything, so a red cannot leave the machine
    # patched for the checks below.
    _restore_bps(d, ((ROM_BP_ADDR, before), (RAM_BP_ADDR, old[1])))
    rom_back = talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(ROM_BP_ADDR) + _w(1))[0]

    if ram_now != BP_OPCODE:
        raise Precondition("the RAM control at 0x%04X read back 0x%02X, not "
                           "0x%02X" % (RAM_BP_ADDR, ram_now, BP_OPCODE))
    if rom_now != BP_OPCODE:
        return FAIL, ("0x%04X still reads 0x%02X: the RST 0 was discarded while "
                      "RAM took one" % (ROM_BP_ADDR, rom_now))
    if rom_back != before:
        return FAIL, ("the breakpoint landed at 0x%04X but would not come out "
                      "(0x%02X)" % (ROM_BP_ADDR, rom_back))
    return PASS, ("a breakpoint at 0x%04X in ROM was set and removed, RAM "
                  "control too" % ROM_BP_ADDR)


def chk_rom_breakpoint_fires(d):
    """Does a temporary breakpoint in ROM space stop the debuggee?

    THE FIXTURE IS BUILT SO THAT BOTH OUTCOMES ARE POSITIVE OBSERVATIONS, which
    is what makes this better than waiting out a timeout. The ROM address is
    one whose byte is 0xC9 — a RET — FOUND BY READING THE MACHINE rather than
    hardcoded, so nothing here depends on which ROM is paged in. The debuggee
    CALLs it:

        call <rom>      ; RST 0 there if the breakpoint landed
        nop             ; <- the RAM control
        jr $

    If the breakpoint landed, the RST 0 runs and the debugger is entered at the
    ROM address. If it was discarded, the RET runs, control returns, and the
    RAM control catches it one instruction later. An NTF_PAUSE arrives either
    way and its address says which happened — so a silence is a third thing
    again, and means the resume itself failed rather than the breakpoint.

    Both breakpoints go in one CMD_CONTINUE, so the control is not merely in
    the same run but in the same resume.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())

    slot0 = _slot0(d)
    if slot0 != ROM_BANK:
        raise Precondition("MMU slot 0 holds bank %d, not the ROM: there is no "
                           "ROM to break in" % slot0)

    # 0x0100 upward: clear of the RST vectors and of the trampoline the stub
    # patches into the Alt ROM at 0x0000 and 0x0066, so the byte found is a
    # plain ROM byte and not one of ours.
    rom = talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(0x0100) + _w(0x1F00))
    if bytes([RET_OPCODE]) not in rom:
        raise Precondition("no 0x%02X byte anywhere in 0x0100-0x1FFF, so this "
                           "check has nothing to call" % RET_OPCODE)
    rom_addr = 0x0100 + rom.index(RET_OPCODE)

    fixture = b"\xCD" + _w(rom_addr) + b"\x00" + b"\x18\xFE"
    ram_trap = DBG_CODE + 3
    _write_mem(d, DBG_CODE, fixture)
    if _read_mem(d, DBG_CODE, len(fixture)) != fixture:
        raise Precondition("the fixture did not land at 0x%04X" % DBG_CODE)
    _set_reg(d, REG_PC, DBG_CODE)
    _set_reg(d, REG_SP, DBG_STACK)

    payload = (bytes([1]) + _w(rom_addr) +      # the subject
               bytes([1]) + _w(ram_trap) +      # the control
               bytes([0]) + _w(0) + _w(0))
    if not NO_CONTINUE:
        talk(d, dzrp.CMD_CONTINUE, payload)
    try:
        ntf = d.wait_notification()
    except dzrp.Timeout:
        return FAIL, ("neither breakpoint fired within %.0fs: the resume "
                      "failed, not the breakpoint" % d.base_timeout)
    if len(ntf) < 4 or ntf[0] != dzrp.NTF_PAUSE:
        return FAIL, "the notification is not an NTF_PAUSE (%s)" % ntf.hex()

    addr = int.from_bytes(ntf[2:4], "little")
    if addr == rom_addr:
        return PASS, "the debuggee stopped on the ROM breakpoint at 0x%04X" % addr
    if addr == ram_trap:
        return FAIL, ("the ROM breakpoint at 0x%04X never fired; the RAM control "
                      "did" % rom_addr)
    return FAIL, ("stopped at 0x%04X, which is neither the ROM breakpoint nor "
                  "the control" % addr)


# The two blocks copy_modify_altrom patches into the Alt ROM: 8 bytes at 0x0000
# (the RST 0 entry and the return path) and 14 at 0x0066 (dbg_enter). They are
# the only reason an RST 0 reaches the debugger at all, so a breakpoint must
# never be planted on them. Taken from src/breakpoints.asm's own DISP blocks.
TRAMPOLINE = tuple(range(0x0000, 0x0008)) + tuple(range(0x0066, 0x0074))

# THE BYTES IMMEDIATELY OUTSIDE, AND THEY ARE THE HALF THAT DISCRIMINATES.
# Asserting only that the trampoline is spared cannot tell a guard that refuses
# 22 bytes from one that refuses far more — and the first version of this fix
# refused 116, the whole of 0x0000-0x0073, because a `ret c` returned with the
# carry its own contract read as "refuse". That silently swallowed RST 8,
# RST 0x10 through RST 0x30 and 0x0038, the IM1 handler, and — since
# set_tmp_breakpoint shares the guard — made stepping run away in that window,
# which is the very failure #27 exists to fix. C21 did not see it, because it
# constrained the guard at two addresses and nowhere else.
#
# 0x0065 is included as well as 0x0008 and 0x0074: it is the byte below the
# SECOND block, so a guard that got only the first boundary right still fails.
TRAMPOLINE_ADJACENT = (0x0008, 0x0065, 0x0074)


def _runs(xs, cap=8):
    """Consecutive addresses as ranges, so a verdict can name a set briefly.

    CAPPED, because the caller's detail line has a twenty-word budget and this
    is the only part of it whose length depends on data. Every guard shaped like
    a range test yields one or two runs; a contrived scattered refusal could
    yield twelve and push the line over. The overflow is counted rather than
    dropped, so the reader still knows the set was bigger than what is named.
    """
    out, i, xs = [], 0, sorted(xs)
    while i < len(xs):
        j = i
        while j + 1 < len(xs) and xs[j + 1] == xs[j] + 1:
            j += 1
        out.append("0x%04X" % xs[i] if i == j
                   else "0x%04X-0x%04X" % (xs[i], xs[j]))
        i = j + 1
    if len(out) > cap:
        return " ".join(out[:cap]) + " and %d more" % (len(out) - cap)
    return " ".join(out)


def chk_rom_breakpoint_spares_trampoline(d):
    """The trampoline is refused, and the bytes on either side of it are not.

    THIS CHECK ONLY EXISTS BECAUSE C19 PASSES. While writes into ROM space were
    discarded, a breakpoint aimed at the trampoline was harmless; the moment
    they land, an RST 0 over dbg_enter's first byte makes every breakpoint
    re-enter itself and walk the stack down through memory. The guard and the
    writability fix are therefore one change, and this is the half that says
    the guard is there rather than assumed.

    IT ASSERTS THE EXTENT, IN BOTH DIRECTIONS, and the second direction is the
    one that earns it. A version of this check that constrained the guard only
    at 0x0000 and 0x0066 passed green against a guard refusing the whole of
    0x0000-0x0073 — 116 bytes including RST 8, the RST 0x10..0x30 vectors and
    0x0038, the IM1 handler — which made STEPPING RUN AWAY in that window,
    because set_tmp_breakpoint shares the guard. So every byte of both blocks
    must be refused, and the bytes immediately outside must be TAKEN.

    THE CONTROL IS A PRECONDITION AND IT IS ALSO THE POINT. On a remote where
    ROM writes are discarded the trampoline is untouched for the wrong reason
    and every assertion here holds vacuously — so an ordinary ROM address goes
    in the same command and must have taken the breakpoint. Without it this
    check passes against exactly the ROM it was written to distinguish from.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())

    slot0 = _slot0(d)
    if slot0 != ROM_BANK:
        raise Precondition("MMU slot 0 holds bank %d, not the ROM: the "
                           "trampoline is not mapped" % slot0)

    must_take = TRAMPOLINE_ADJACENT + (ROM_BP_ADDR,)
    addrs = TRAMPOLINE + must_take
    before = {a: talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(a) + _w(1))[0]
              for a in addrs}
    old = dict(zip(addrs, _set_bps(d, addrs)))
    after = {a: talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(a) + _w(1))[0]
             for a in addrs}
    # Put back only what actually moved, before judging anything, so a red
    # cannot leave an RST 0 in the Alt ROM for the checks below.
    changed = [(a, before[a]) for a in addrs if after[a] != before[a]]
    if changed:
        _restore_bps(d, changed)
    left = [a for a, v in changed
            if talk(d, dzrp.CMD_READ_MEM, b"\x00" + _w(a) + _w(1))[0] != v]

    if after[ROM_BP_ADDR] != BP_OPCODE:
        raise Precondition("the ROM control at 0x%04X did not take a breakpoint, "
                           "so nothing here is proven" % ROM_BP_ADDR)
    planted = [a for a in TRAMPOLINE if after[a] != before[a]]
    if planted:
        return FAIL, ("a breakpoint was planted on the debugger's trampoline at %s"
                      % _runs(planted))
    spared = [a for a in must_take if after[a] == before[a]]
    if spared:
        return FAIL, ("the guard also refused %s, which is ordinary ROM"
                      % _runs(spared))
    # The client is still told what was there, so it is not lied to about the
    # address; it simply gets no breakpoint.
    wrong = [a for a in TRAMPOLINE if old[a] != before[a]]
    if wrong:
        return FAIL, ("the refused address 0x%04X was reported as the wrong "
                      "opcode" % wrong[0])
    if left:
        return FAIL, "a breakpoint at %s could not be removed" % _runs(left)
    return PASS, ("all %d trampoline bytes refused, and 0x0008/0x0065/0x0074 "
                  "taken" % len(TRAMPOLINE))


# ---------------------------------------------------------------------------
# C22 and C23 — the 64K address form inside the DEBUGGER's own slot
# ---------------------------------------------------------------------------
#
# DZRP GIVES AN ADDRESS TWO FORMS AND THIS SUITE HAD ONLY EVER SENT THE SECOND
# ONE LOW. CMD_SET_BREAKPOINTS and CMD_RESTORE_MEM each carry a 16-bit address
# plus a bank+1 byte: non-zero names a bank outright, zero means "the 64K
# address as the debuggee currently sees it". The stub answers the second form
# by asking whether the address is at or above 0xE000, because that range is
# MAIN_SLOT — where the debugger itself is executing — so reaching the
# DEBUGGEE's memory there means paging its bank into the swap window first.
#
# BOTH HANDLERS MADE THAT DECISION ON THE WRONG REGISTER (issue #38): the `cp
# HIGH MAIN_ADDR` ran with A still holding the bank+1 byte, which on this path
# is zero by definition, so the test was always 0 < 0xE0 and the direct-write
# branch was always taken. A 64K-form address at 0xE000 or above therefore
# wrote into MAIN_BANK — the bank the debugger is running out of. Same family
# as C18, one command along: a client-controlled write over the running
# debugger.
#
# NOTHING HERE HAD EVER FIRED IT, which is why a green suite stayed green.
# DeZog sends long addresses. C19 and C21 do send the 64K form — _set_bps has
# always used it — but at addresses from 0x0000 to 0x8000, where the direct
# branch is the CORRECT one: C21 covers 0x0000-0x0008, 0x0065-0x0074 and
# 0x1234, and 0x8000 is C19's RAM control. Enumerated over every _set_bps and
# _restore_bps call site; none was at or above 0xE000. The defect lived in the
# one input nobody sent.

# The probe address, and every part of it is chosen.
#
#  - IN MAIN_SLOT, or there is no wrong branch to take.
#  - ABOVE ANY BYTE THE DEBUGGER OCCUPIES IN MAIN_BANK, so that a remote which
#    still has the defect writes into DEAD SPACE and the red is a repeatable
#    reading rather than a crash somewhere in the debugger's own code. It is
#    above the end of the SAVEBIN image itself: ROM_MAGIC_ADDR is 0xFE70 and
#    ROM_MAGIC_SIZE is 32, so the image ends at 0xFE90 — and only
#    main_end-MAIN_ADDR bytes are ever COPIED into the bank at all (mf_rom.asm's
#    MEMCOPY). main.asm:279 bounds main_end at 0xFF00 outright and the assert
#    below it bounds it by ROM_MAGIC_ADDR, which only ever moves DOWN, 16 bytes
#    at a time, as the MF ROM half grows. So 272 bytes of margin at worst case,
#    structurally, now and after M2 — and no new ASSERT is needed here.
#  - NOT 0xFFFF, so nothing here rests on the last byte of a bank.
#
# The stray byte a defective remote leaves in MAIN_BANK survives the run and is
# inert; the debuggee's own byte is put back below.
SLOT7_ADDR = 0xFF80
SLOT7_SEED = 0x5A       # seeded into the DEBUGGEE's bank. Not BP_OPCODE.
SLOT7_VALUE = 0xA5      # what CMD_RESTORE_MEM is asked to write. Not the seed.

# constants.asm:15 — the bank the debugger executes from. A second rendering of
# a value in the Z80 source with nothing keeping the two in step, which is the
# practice BP_OPCODE and TRAMPOLINE already follow here.
#
# IT NOW GATES TWO THINGS, so a stale value fails in two ways at once and both
# are silent: _peek_main_bank would page in the WRONG bank, whose byte cannot be
# touched by the handler either, giving a happily-equal before/after; and
# _seed_slot7's "the debuggee's slot 7 must not be the debugger's own" refusal
# would stop discriminating. Both directions turn a real defect green.
MAIN_BANK = 94

# THE SECOND VIEW OF THE SAME PHYSICAL BYTE, and it is what makes the checks
# below self-sufficient instead of resting on an argument.
#
# The verdict is a read-back through memory_loop's PHASE 1 — the swap window —
# which is the same routing decision the handlers get wrong. So a memory_loop
# broken the identical way would send the seed and the verdict to MAIN_BANK
# too, and both checks would pass against a defective remote. Borrowing a slot
# closes that: MMU slot 5 is 0xA000-0xBFFF, so MAIN_BANK's offset 0x1F80
# appears at 0xBF80 — BELOW 0xE000, so CMD_READ_MEM takes memory_loop's PHASE
# 2, the direct path, and reaches the byte without the window the verdict
# depends on. Two routes, one byte, and the checks assert the debugger's own
# bank was NOT written.
#
# SLOT 5 AND NOT SLOT 4, and the argument is about ORDERING as much as about
# which addresses matter. Slot 4 is 0x8000-0x9FFF, where this suite's fixture
# (DBG_CODE), its marker area (DBG_MARK) and BIG_MEM_ADDR all live, so a restore
# that went wrong there would be maximally damaging — and would be this issue's
# own defect, self-inflicted. Slot 5 is named by no check.
#
# It is not untouched, though, and an earlier version of this comment said
# "nothing in this suite uses 0xA000-0xBFFF", which is FALSE: C17 writes 16384
# bytes from 0x8000 and therefore spans it — its own docstring says so, "it
# spans TWO slots ... memory_loop's banking is what carries it across 0xA000".
# What makes that harmless is the order and the access: C17 has finished long
# before C22 runs, the borrow below only ever READS through the slot and puts it
# back, and no check after C22/C23 reads that region.
#
# The explicit restore is belt and braces in any case, and the reason is narrow
# enough to be worth stating exactly rather than generally: cmd_init resets MMU
# slots 1-6 outright (commands.asm), and the only checks that run after C22 and
# C23 are C18 and C15, both of which send a well-formed CMD_INIT as their first
# command. (That is NOT true of the suite at large — C2's CMD_INIT is
# deliberately over-declared, so cmd_init blocks in .inner and never reaches the
# slot reset, and C13/C14 send none at all. The narrow claim is the one the
# safety argument needs and the one that holds.)
PROBE_SLOT = 5
PROBE_ADDR = PROBE_SLOT * 0x2000 + (SLOT7_ADDR & 0x1FFF)


def _peek_main_bank(d):
    """Read SLOT7_ADDR's byte out of MAIN_BANK, off memory_loop's phase-1 path.

    The slot is put back from what CMD_GET_REGISTERS reported for it, which for
    slots 0-6 is read LIVE from the MMU rather than from the stub's bookkeeping.
    That the restore really happens is asserted by _probe_slot_returned, not
    assumed here.
    """
    body = talk(d, dzrp.CMD_GET_REGISTERS)
    if len(body) < 37:
        raise Precondition("the register block is %d bytes, too short for the "
                           "slot list" % len(body))
    was = body[29 + PROBE_SLOT]
    talk(d, dzrp.CMD_SET_SLOT, bytes([PROBE_SLOT, MAIN_BANK]))
    try:
        return _read_mem(d, PROBE_ADDR, 1)[0]
    finally:
        talk(d, dzrp.CMD_SET_SLOT, bytes([PROBE_SLOT, was]))


def _probe_slot_returned(d):
    """Did the check before this one give MMU slot PROBE_SLOT back?

    NOTHING ELSE OBSERVES THAT, and a failed restore is invisible in every
    direction that matters: the next _peek_main_bank would read `was` as
    MAIN_BANK, put MAIN_BANK back, and still report a correct before/after
    pair, while C18's and C15's CMD_INIT tidied the slot up behind it. So the
    borrow would go on working and the machine would quietly be left with the
    debugger's own bank mapped at 0xA000 for the rest of the run. A property
    evidenced only by a scratch probe is a property nobody will notice breaking.

    IT MUST RUN BEFORE THIS CHECK'S OWN CMD_INIT, which is the whole of why it
    works: cmd_init resets MMU slots 1-6 outright (commands.asm), so one sent
    first would erase the evidence. CMD_GET_REGISTERS reports slots 0-6 live
    from the MMU and resets nothing — the same property _remote_still_answers
    already relies on.

    IT IS A PRECONDITION AND NOT THE SUBJECT: what it reports is that the
    instrument left the machine as it found it, which says nothing about the
    64K address form either way.

    Scope, said rather than hidden. It observes the borrow made by the check
    BEFORE this one — C22, in the default order, where the two are adjacent.
    Under `--only C23` there was no borrow and this is vacuously satisfied.
    C23's own borrow is the last one and nothing standing looks at it; what
    makes that harmless is that only C18 and C15 follow, and both send a
    well-formed CMD_INIT first.
    """
    body = talk(d, dzrp.CMD_GET_REGISTERS)
    if len(body) < 37:
        raise Precondition("the register block is %d bytes, too short for the "
                           "slot list" % len(body))
    if body[29 + PROBE_SLOT] == MAIN_BANK:
        raise Precondition("MMU slot %d still holds bank %d: the borrow was not "
                           "given back" % (PROBE_SLOT, MAIN_BANK))


def _slot7(d):
    """Which bank the DEBUGGEE's slot 7 holds, per the remote's own bookkeeping.

    Byte 36 of CMD_GET_REGISTERS: 28 register bytes, the slot count at 28,
    slots 0-6 read LIVE from the MMU at 29-35, and slot 7 at 36 — which our
    stub reports from slot_backup.slot7 instead, because while the debugger is
    stopped the MMU's slot 7 holds the debugger's own bank and reporting that
    would be answering a different question.
    """
    body = talk(d, dzrp.CMD_GET_REGISTERS)
    if len(body) < 37:
        raise Precondition("the register block is %d bytes, too short for the "
                           "slot list" % len(body))
    return body[36]


def _seed_slot7(d):
    """Put a known byte at SLOT7_ADDR in the debuggee's bank; return the original.

    THIS IS WHAT STOPS THE TWO CHECKS BELOW PASSING VACUOUSLY.

    If the debuggee's slot 7 held MAIN_BANK, the swap window and the direct
    write would reach the SAME memory and no observation could tell the two
    branches apart. That is refused rather than measured.

    And the seed has to be shown to be there. CMD_WRITE_MEM and CMD_READ_MEM
    reach 0xE000+ through memory_loop, which makes the same decision correctly
    (backup.asm:326, `ld a,h` before the compare), so a round trip says that
    route works in THIS run — and a red below is then about the handler rather
    than about the address being unreachable by anything.

    memory_loop being a SHARED dependency of the seed and the verdict is what
    _peek_main_bank exists for; see there. What is NOT closed by any of this is
    a memory_loop wrong in some way other than routing 0xE000+ to MAIN_BANK,
    which is a different hazard and not the one these checks are exposed to.

    THE ORIGINAL IS RETURNED SO THE CALLER CAN PUT IT BACK — C19's precedent,
    where the restore is part of the subject rather than tidying up. This runs
    against a live NextZXOS bank on hardware, where SLOT7_ADDR is not obviously
    inert, and one CMD_READ_MEM is the whole cost of not having to argue that.
    """
    slot7 = _slot7(d)
    if slot7 == MAIN_BANK:
        raise Precondition("the debuggee's slot 7 holds bank %d, the debugger's "
                           "own, so both branches write one place" % MAIN_BANK)
    original = _read_mem(d, SLOT7_ADDR, 1)
    if len(original) != 1:
        raise Precondition("0x%04X answered %d bytes, not one"
                           % (SLOT7_ADDR, len(original)))
    _write_mem(d, SLOT7_ADDR, bytes([SLOT7_SEED]))
    back = _read_mem(d, SLOT7_ADDR, 1)
    if back != bytes([SLOT7_SEED]):
        raise Precondition("0x%04X reads %s after being seeded 0x%02X: nothing "
                           "can write there" % (SLOT7_ADDR, back.hex(), SLOT7_SEED))
    return original[0]


def chk_slot7_breakpoint_uses_swap_window(d):
    """A 64K-form breakpoint at 0xE000+ must reach the debuggee, not the debugger.

    THE OBSERVATION IS POSITIVE AND HAS THREE OUTCOMES, which is worth more than
    waiting to see whether the remote dies — a one-byte write into the running
    debugger may or may not be fatal depending on where it lands, so "it
    crashed" is a weak and flaky signal for this defect. CMD_READ_MEM reaches
    the same address through memory_loop's swap window, so afterwards it reads:

      the RST 0     - the write went where the client asked;
      the seed      - it went somewhere else, and the only other place a 64K
                      address at 0xE000+ can land is MAIN_BANK: the defect;
      anything else - a third thing, reported as a third thing.

    AND MAIN_BANK IS READ DIRECTLY, by a second route, which is what turns "the
    write went where asked" into "the debugger's own bank was not written" —
    the property the issue is actually about. See _peek_main_bank. It is judged
    FIRST because it is the direct statement of harm and is deterministic.

    THE REPLY BYTE IS A THIRD OBSERVATION of the same routing: the remote must
    report the opcode it FOUND, which on the swap path is the seed. It is judged
    last because it is the weakest — a defective remote reads an uninitialised
    byte out of MAIN_BANK and could match the seed by chance, one time in 256.

    SURVIVAL IS ASSERTED IN BAND rather than on a fresh connection as C18 does:
    the read-back is itself an exchange the remote has to serve AFTER the
    offending write, so a remote that overwrote itself fails here as a timeout,
    which main() reports as the connection fault it is.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    original = _seed_slot7(d)

    main_before = _peek_main_bank(d)
    old = _set_bps(d, (SLOT7_ADDR,))
    now = _read_mem(d, SLOT7_ADDR, 1)[0]
    main_after = _peek_main_bank(d)
    # Put the DEBUGGEE's own byte back, with CMD_WRITE_MEM and NOT
    # CMD_RESTORE_MEM: that handler is C23's subject and carries the same
    # defect, so on a red remote the restore would go to the wrong bank and
    # leave this one behind. Before judging anything, so a red cannot leave the
    # machine patched for the checks below.
    _write_mem(d, SLOT7_ADDR, bytes([original]))

    if main_after != main_before:
        return FAIL, ("the debugger's own bank was written: MAIN_BANK 0x%04X "
                      "went 0x%02X to 0x%02X"
                      % (SLOT7_ADDR, main_before, main_after))
    if now == SLOT7_SEED:
        return FAIL, ("0x%04X is untouched: the RST 0 went to the debugger's "
                      "own bank" % SLOT7_ADDR)
    if now != BP_OPCODE:
        return FAIL, ("0x%04X reads 0x%02X, neither the breakpoint nor the seed"
                      % (SLOT7_ADDR, now))
    if old[0] != SLOT7_SEED:
        return FAIL, ("the opcode reported for 0x%04X was 0x%02X, not the seed"
                      % (SLOT7_ADDR, old[0]))
    return PASS, ("a 64K-form breakpoint at 0x%04X reached the debuggee's bank"
                  % SLOT7_ADDR)


def chk_slot7_restore_uses_swap_window(d):
    """CMD_RESTORE_MEM, 64K form, at 0xE000+ — the same defect one handler along.

    NOT A COPY OF C22, and the difference is why it is its own check: the two
    handlers carry SEPARATE copies of that decision — cmd_set_breakpoints and
    cmd_restore_mem in commands.asm — so a fix to one leaves the other exactly
    as it was, and a single check covering both would not say which.

    This is also the worse of the pair, because the byte written is the
    CLIENT'S rather than a fixed RST 0: an arbitrary value at an arbitrary
    offset of the running debugger's bank.

    Same read-back and same direct MAIN_BANK view as C22, with SLOT7_VALUE in
    place of the breakpoint. There is no reply to corroborate them with —
    CMD_RESTORE_MEM answers Length=1 and carries no payload — so this one has
    two observations where C22 has three.

    IT ALSO CARRIES THE ONLY STANDING CHECK ON THE INSTRUMENT ITSELF: whether
    C22 gave MMU slot PROBE_SLOT back. That has to happen before this check's
    own CMD_INIT, which would erase the evidence — see _probe_slot_returned.
    """
    _probe_slot_returned(d)
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    original = _seed_slot7(d)

    main_before = _peek_main_bank(d)
    _restore_bps(d, ((SLOT7_ADDR, SLOT7_VALUE),))
    now = _read_mem(d, SLOT7_ADDR, 1)[0]
    main_after = _peek_main_bank(d)
    _write_mem(d, SLOT7_ADDR, bytes([original]))

    if main_after != main_before:
        return FAIL, ("the debugger's own bank was written: MAIN_BANK 0x%04X "
                      "went 0x%02X to 0x%02X"
                      % (SLOT7_ADDR, main_before, main_after))
    if now == SLOT7_SEED:
        return FAIL, ("0x%04X is untouched: the byte went to the debugger's "
                      "own bank" % SLOT7_ADDR)
    if now != SLOT7_VALUE:
        return FAIL, ("0x%04X reads 0x%02X, neither the value sent nor the seed"
                      % (SLOT7_ADDR, now))
    return PASS, ("a 64K-form CMD_RESTORE_MEM at 0x%04X reached the debuggee's "
                  "bank" % SLOT7_ADDR)


# ==========================================================================
# The breakpoint commands the cspect remote sends, and this stub does not
# implement.
#
# DeZog HAS TWO BREAKPOINT APIS AND THIS STUB SPEAKS THE OTHER ONE. It
# implements CMD_SET_BREAKPOINTS (13) / CMD_RESTORE_MEM (14), which is what the
# ZxNextSerialRemote drives and what C19-C23 verify. The cspect remote — the
# only one that can reach a Next over a socket at all — sends CMD_ADD_BREAKPOINT
# (40) and CMD_REMOVE_BREAKPOINT (41) instead. Those are outside the stub's
# jump table, so a breakpoint set in the VS Code editor reached
# cmd_not_supported: NO RESPONSE AT ALL, a ~50 s stall, "No response received
# from remote" in DeZog's log, and a red dot that silently never fired.
# Measured on a real Next 2026-08-11 at build 00.21. Issue #41.
#
# WHAT THESE TWO CHECKS ASSERT IS AN HONEST REFUSAL, NOT BREAKPOINTS. That is
# the whole of option A in doc/DEZOG-BREAKPOINTS-DESIGN.md and it is deliberately
# the smaller claim: nothing here says a breakpoint works, and a remote that
# passes C24 will still not stop a debuggee at an address the editor names.
# ==========================================================================

# The 2-byte breakpoint id DeZog reads out of a CMD_ADD_BREAKPOINT response, and
# the value it reads as "refused". `DzrpBufferRemote.sendDzrpCmdAddBreakpoint`
# does `e.bpId = getWord(i, 0)` and `DzrpRemote.setBreakpoint` does
# `e.bpId === 0 && (e.longAddress = -1)`, after which VS Code shows the
# breakpoint unverified. Read out of DeZog 3.7.4's own bundle.
#
# THE TWO CLASSES ARE DIFFERENT AND BOTH ARE DELIBERATE. `setBreakpoint` really
# is `DzrpRemote`'s — the assert(false) stub for it is `RemoteBase`'s, one level
# further up. The command-sending half is `DzrpBufferRemote`'s, the class that
# owns `sendDzrpCmd`; `DzrpRemote`'s own is an assert(false) stub. Corrected
# 2026-08-13 against both the bundle and the unmodified v3.7.4 source.
BP_ID_LEN = 2
BP_ID_REFUSED = 0

# TWO addresses, and asking for both is what makes C24 an assertion rather than
# a reading. See its docstring: a remote that hands out real ids must hand out
# DIFFERENT ones for different addresses, because DeZog removes breakpoints by
# id and nothing else. Both are ordinary RAM, where a remote that did implement
# breakpoints would have somewhere legitimate to put one — C19 uses 0x8000 as
# its own RAM control for the same reason.
BP_LONG_ADDRESSES = (0x8000, 0x8010)


def _add_breakpoint_payload(long_address, condition=""):
    """The wire payload of CMD_ADD_BREAKPOINT.

    Three address bytes little-endian, then a NUL-terminated condition string.
    A captured frame from a real session: `Cmd: 40, Length: 4, Data: 149 128 5
    0` — 0x058095 and an empty condition, which is one NUL and not zero bytes.
    """
    return (bytes([long_address & 0xFF,
                   (long_address >> 8) & 0xFF,
                   (long_address >> 16) & 0xFF])
            + condition.encode("ascii") + b"\x00")


def _error_area(d):
    """(judged, text) — what the remote's own error area says, if we can read it.

    When judged is False, text is a SHORT phrase naming which kind of abstention
    this was, because the two are different findings and a verdict that
    collapsed them would hide one of them. "not this stub's screen" is the
    expected answer from CSpect, which `make test-dzrp` points this suite at;
    "could not be read" against our own stub is a degradation nobody would
    otherwise notice, since both look like a clean area from here. Three or four
    words each, deliberately: the caller pays for them inside its own budget.
    The exception's own text is dropped rather than interpolated — it is
    unbounded, and a verdict line whose length depends on its data cannot be
    held to a word budget at all.

    JUDGED IS FALSE WHENEVER THIS IS NOT OUR STUB'S SCREEN, and that gate is a
    positive test rather than a guess: screen.validate_reader() requires row 12
    to read "R = Reset", which ui.asm draws in the same font by the same code
    and which is the same guard run-client-status.sh applies to its screenshots.
    A remote that is not this stub — CSpect, which `make test-dzrp` points this
    suite at — has no such row, so the error area is NOT JUDGED there rather
    than judged wrongly. That is the difference between abstaining and passing
    vacuously, and it is why the caller reports which of the two happened.

    No CMD_INIT is sent. screen.py never sends one, deliberately: cmd_init
    clears last_error, so a reader that introduced itself would erase the very
    thing it was run to read.
    """
    try:
        glyphs = screen.read_font(d)
        scr = screen.read_screen(d, glyphs=glyphs)
    except (dzrp.DzrpError, OSError, ValueError):
        return False, "could not be read"
    ok, _ = scr.validate_reader()
    if not ok:
        return False, "not this stub's screen"
    return True, " / ".join(scr.error_text())


def chk_add_breakpoint_refused(d):
    """CMD_ADD_BREAKPOINT is answered, and the breakpoint id it gives is honest.

    THREE THINGS ARE ASSERTED AND EACH FAILS DIFFERENTLY.

    (1) A RESPONSE ARRIVES, at Length=3 — the sequence byte and a two-byte id.
    Silence is the defect: DeZog's sendDzrpCmd waits for a frame whatever the
    remote thinks of the command, so a handler that consumes the frame and sends
    nothing blocks the client. That is issue #8's shape exactly, one command
    along, and it is why the length is not negotiable: getWord(i, 0) reads two
    bytes and a shorter body would be read out of whatever followed it.

    (2) THE PAIR OF IDS IS COHERENT, and this is the half that took a
    measurement to get right. The obvious check — "the id must be 0" — is wrong
    for this suite, because a remote that IMPLEMENTS breakpoints answers with a
    real one and is not defective for doing so. Measured rather than assumed:
    CSpect's DeZog plugin, which the Makefile names as the way to validate this
    suite against a reference, carries CMD_ADD_BREAKPOINT and
    CMD_REMOVE_BREAKPOINT as implemented commands (its DLL's own symbols). A
    strict id-0 assertion would have made C24 a knowingly-red check against a
    supported target, which is the one thing this project's benches may not be.

    So TWO breakpoints are asked for, at different addresses, and the pair is
    judged:

      0 and 0        refused honestly, which is what THIS stub does and what
                     DZRP's own vocabulary for "not there" is: DeZog's
                     setBreakpoint reads `e.bpId === 0 && (e.longAddress = -1)`
                     and marks the breakpoint unverified
      two DIFFERENT  the remote implements breakpoints; correct, and reported
      non-zero ids
      anything else  FAIL. The same non-zero id for two addresses, or one id
                     and one refusal, is a promise the remote cannot keep:
                     DeZog removes a breakpoint BY ID and nothing else, so two
                     breakpoints wearing one id can never both be removed.

    That last row is what keeps this an assertion against our own stub. A
    regression answering a constant non-zero id — the plausible way to get this
    wrong, since it is what "just return something" produces — is caught. A
    regression inventing DISTINCT ids for breakpoints it never sets would pass,
    and that is stated rather than hidden: no PC-side check can tell a real id
    from an invented one without watching a debuggee stop on it, which is the
    feature this remote does not have.

    (3) THE SESSION SURVIVES IN SYNC. The payload is three address bytes plus a
    NUL-terminated condition string whose length only the frame declares, so a
    handler that consumed the wrong number of bytes leaves the stream offset for
    every command after it — silently, and for the rest of the session (issue
    #7). An ordinary CMD_GET_REGISTERS afterwards is what proves it did not: a
    stream one byte out cannot answer a further command at the right length. It
    is the DISCRIMINATING half, because a handler that ignored the length
    entirely would still satisfy (1) and (2) on these two frames alone.

    AND ONE THING IS OBSERVED RATHER THAN ASSERTED WHEREVER IT CANNOT BE. A
    refusal is expected behaviour, not a fault, so the Next's error area must
    stay clean — the old handler stored ERROR_CMD_NOT_SUPPORTED and went to
    drain_main, which paints "Last Error: Command not supported" on a machine
    with nothing wrong with it and re-initialises the debuggee's saved state on
    the way past. The CMD_INIT above clears last_error, so anything found here
    was written by the command under test and by nothing before it. It is read
    only on the REFUSED path, because "did refusing paint an error" is not a
    question about a remote that did not refuse. Against a remote whose screen
    this reader cannot validate the area is NOT JUDGED — see _error_area — and
    the verdict says which of the two abstentions happened rather than claiming
    a clean area.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())

    ids = []
    for addr in BP_LONG_ADDRESSES:
        try:
            body = talk(d, dzrp.CMD_ADD_BREAKPOINT, _add_breakpoint_payload(addr))
        except dzrp.Timeout:
            return FAIL, "no response within %.0fs; %s" % (d.base_timeout, _liveness())
        if len(body) != BP_ID_LEN:
            return FAIL, ("answered with %d bytes, not the %d-byte breakpoint id"
                          % (len(body), BP_ID_LEN))
        ids.append(body[0] | (body[1] << 8))

    refused = all(i == BP_ID_REFUSED for i in ids)
    honoured = all(i != BP_ID_REFUSED for i in ids) and len(set(ids)) == len(ids)
    if not refused and not honoured:
        return FAIL, ("two breakpoints at different addresses came back as ids "
                      "%s, which cannot both be removed" % (tuple(ids),))

    try:
        regs = talk(d, dzrp.CMD_GET_REGISTERS)
    except (dzrp.DzrpError, OSError) as e:
        return FAIL, "the stream desynchronised: the next command failed (%s)" % e
    if len(regs) < 28:
        return FAIL, ("the next command came back as %d bytes: the payload was "
                      "not consumed exactly" % len(regs))

    if honoured:
        return PASS, ("the remote implements breakpoints: ids %s, next command "
                      "in sync" % (tuple(ids),))

    judged, text = _error_area(d)
    if not judged:
        return PASS, "both refused with id 0, in sync; error area %s" % text
    if text:
        return FAIL, "refused and in sync, but the remote reported an error: %s" % text
    return PASS, "both refused with id 0, next command in sync, error area clean"


def chk_remove_breakpoint_answered(d):
    """CMD_REMOVE_BREAKPOINT is answered, and the stream stays in sync.

    WHY IT NEEDS ITS OWN CHECK RATHER THAN RIDING ON C24. DeZog sends 41 for
    every breakpoint it removes, including on disconnect, and
    sendDzrpCmdRemoveBreakpoint IGNORES the response body — but sendDzrpCmd
    still WAITS for a frame, so silence blocks the client exactly as it does for
    40. The two commands reach two separate handlers, so a fix to one leaves the
    other's silence in place and only a check naming 41 would say so.

    THE ASSERTION IS LENGTH=1 AND SYNC. The specification gives it the sequence
    number and nothing else, which is cmd_pause's shape (C12). The body being
    ignored client-side is not a licence to send a wrong-length one: a longer
    frame is bytes the client will read as the next response.

    THE ID REMOVED IS ONE THE REMOTE ITSELF JUST HANDED OUT, rather than a
    constant, and that is what lets one check cover both kinds of remote. For
    this stub it is 0 — the only id it ever gives — and a client cannot remove
    one it was not given. For a remote that implements breakpoints it is a live
    id, so the removal is the ordinary case rather than a lookup of something
    that was never there. Sending a bare 0 to such a remote would have been
    asking it to remove a breakpoint nobody set, which is a different question
    and not one this check is about.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    try:
        added = talk(d, dzrp.CMD_ADD_BREAKPOINT,
                     _add_breakpoint_payload(BP_LONG_ADDRESSES[0]))
    except dzrp.Timeout:
        raise Precondition("CMD_ADD_BREAKPOINT was not answered, so there is "
                           "no id to remove — see C24")
    if len(added) != BP_ID_LEN:
        raise Precondition("CMD_ADD_BREAKPOINT gave %d bytes, not an id — see C24"
                           % len(added))
    try:
        body = talk(d, dzrp.CMD_REMOVE_BREAKPOINT, added)
    except dzrp.Timeout:
        return FAIL, "no response within %.0fs; %s" % (d.base_timeout, _liveness())
    if body:
        return FAIL, "answered, but with a %d-byte payload" % len(body)

    try:
        regs = talk(d, dzrp.CMD_GET_REGISTERS)
    except (dzrp.DzrpError, OSError) as e:
        return FAIL, "answered, but the stream desynchronised: the next command failed (%s)" % e
    if len(regs) < 28:
        return FAIL, ("answered, but the next command came back as %d bytes: "
                      "the id was not consumed" % len(regs))
    return PASS, "answered with the sequence number alone, next command in sync"


# One byte over would be the boundary and is not what this asks. C5's 8192 holds
# the boundary; this asks whether a frame that is decisively too big is SURVIVED,
# and a size well past the window is what makes the unfixed failure unambiguous
# rather than a single byte landing on the debugger's first instruction.
OVERSIZE_LOOPBACK = 12288


def chk_oversize_payload(d):
    """A payload too big for the remote's buffer must not take the remote with it.

    THE DEFECT THIS GUARDS IS A CLIENT-CONTROLLED WRITE OVER THE RUNNING
    DEBUGGER. Our stub buffers a CMD_LOOPBACK into a bank paged at SWAP_ADDR,
    an 8 KB window, and walked upward from there for as many bytes as the FRAME
    DECLARED — a number the client chooses. One slot further on is MAIN_SLOT,
    where the debugger is executing. Nothing bounded it, from the fork until the
    check below existed; CMD_WRITE_BANK had the identical hole into the identical
    window.

    IT SURVIVED FIVE YEARS BECAUSE NOTHING EVER SENT ONE. DeZog's loopback is a
    handful of bytes and this sweep stopped at 4096, so the one thing that would
    have found it is a suite pushing 8 KB — and adding C16 and C17 is what did.

    WHAT IS ASSERTED IS SURVIVAL, NOT AN ANSWER, and that is the honest reading
    of what a remote can do here. DZRP has no error response for CMD_LOOPBACK —
    the reply IS the data — so a remote that cannot honour the frame has nothing
    correct to say, and a reply of the wrong length would desynchronise the
    stream for every command after it. Our stub reports on its own screen and
    goes quiet. A DIFFERENT REMOTE MAY LEGITIMATELY ANSWER, and this check
    accepts that too: what it refuses is a remote that stops serving.

    So the verdict is taken on a FRESH CONNECTION afterwards. A remote that
    overwrote its own code would fail that, and one that merely declined to
    answer passes — which is the distinction that matters and the only one
    observable from here.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    payload = _pattern(OVERSIZE_LOOPBACK, 0x3C)
    answered = False
    try:
        d.command(dzrp.CMD_LOOPBACK, payload)
        answered = True
    except (OSError, dzrp.DzrpError):
        # EVERY WAY THIS CAN FAIL IS AN EXPECTED OUTCOME HERE, INCLUDING THE
        # ONES THAT ARE NOT DzrpError. A refusal expressed by hanging up, a
        # timeout, and a send that dies part-way through are all "the remote did
        # not honour an oversize frame", which is what this check provokes on
        # purpose — the verdict is taken on a fresh connection below.
        #
        # OSError has to be in that list and was missing. TcpTransport.write is
        # a bare sock.sendall (dzrp.py), so a peer that resets or closes mid-send
        # raises BrokenPipeError or ConnectionResetError, neither of which is a
        # DzrpError — and main()'s per-check loop does not catch OSError either,
        # so it would have escaped as a traceback and taken every check BELOW
        # this one with it. That includes C15, which this suite requires to run
        # and to run last. jnext drains the payload fast enough that it has never
        # happened here; a real ESP-01's backpressure is not the same thing.
        #
        # C16-C18 HAVE now run on hardware (2026-08-09, build 00.19, 18 of 18),
        # and this branch was still never taken there: the oversize send
        # completed and the remote declined it in-band. So the OSError arm
        # remains unexercised everywhere, which is why it is written to be
        # correct rather than to be tested.
        pass

    # The verdict is taken on a NEW connection, because the one above may
    # legitimately have been dropped, and with CMD_INIT because no remote may
    # refuse it. Same route as _remote_still_answers, so a run without --remote
    # says so rather than inventing a verdict.
    if REMOTE_SPEC is None:
        return FAIL, "not judged: --remote was not set, so nothing could re-connect"
    try:
        t = dzrp.open_remote(REMOTE_SPEC, timeout=d.base_timeout)
    except (OSError, dzrp.DzrpError):
        return FAIL, "%d-byte payload left the remote unreachable" % OVERSIZE_LOOPBACK
    fresh = dzrp.Dzrp(t, start_byte=d.start_byte, base_timeout=d.base_timeout)
    try:
        body = fresh.command(dzrp.CMD_INIT, dzrp.init_payload())
    except (OSError, dzrp.DzrpError):
        return FAIL, "%d-byte payload left the remote not serving" % OVERSIZE_LOOPBACK
    finally:
        fresh.close()
    if len(body) < 6 or body[0] != 0:
        return FAIL, "after the oversize payload CMD_INIT came back %d bytes" % len(body)
    return PASS, "a %d-byte payload was %s and the remote served on" % (
        OVERSIZE_LOOPBACK, "answered" if answered else "declined")


def chk_close(d):
    """CMD_CLOSE is answered, and the remote goes on serving afterwards.

    NOTHING ELSE IN THIS SUITE SENDS COMMAND 2. Every check above takes a fresh
    connection and simply drops it, which is a TCP event and not a DZRP one: the
    remote is never told the session ended, so the one command DeZog uses to say
    so had no coverage at all.

    TWO ASSERTIONS, AND THE SECOND IS THE INTERESTING ONE.

    The response first. The specification gives CMD_CLOSE a Length=1 response —
    the sequence number and nothing else, exactly as CMD_PAUSE has — and DeZog
    awaits it: `sendDzrpCmdClose()` is
    `await this.sendDzrpCmd(2, undefined, this.initCloseRespTimeoutTime)` in the
    installed 3.7.4, in `DzrpBufferRemote` — the class that owns `sendDzrpCmd` —
    and NOT in `DzrpRemote`, whose own `sendDzrpCmdClose` is likewise an
    `assert(false)` stub (`dzrpremote.ts:1852`; `RemoteBase` has none at all).
    Silence there blocks the client, which is issue #8's shape.

    Then that the remote is still there. This is the only command our stub
    answers and then LEAVES through `jp main` (src/commands.asm), which runs
    main's destructive prologue — `prgm_state` to `PRGM_IDLE`, `backup.speed`,
    `backup.interrupt_state`, `slot_backup.slot0` — and then `transport_activate`
    and `show_ui` before reaching `main_loop` again. So the response is written
    BEFORE all of that and proves none of it; only a further command, answered,
    shows the stub came out of the other side.

    CMD_INIT is the follow-up, for two reasons. It is what DeZog's own driver
    does — the first entry of its stress `cmdList` is `sendDzrpCmdClose()`
    immediately followed by `sendDzrpCmdInit()`, on the same remote — and no
    remote is entitled to refuse it, so a failure there cannot be a capability
    difference wearing this check's name.

    IT IS SENT WITHOUT talk(), DELIBERATELY. talk() maps a closed connection onto
    Unsupported, and this check's cmd_name is CLOSE — so a remote that answered
    CMD_CLOSE perfectly and then hung up would be reported as not implementing
    the command it had just implemented. The closed case gets its own verdict
    below instead.

    WHAT IT DELIBERATELY DOES NOT CLAIM. Not that any of that state was reset:
    `prgm_state` and the backup fields are not observable over a socket, and all
    this sees is that the remote answers again. Not the repaint either — that
    CMD_CLOSE redraws the screen is bench run-client-status.sh's N3, read off the
    Next's own display. And not the socket's fate: DZRP is silent on whether the
    transport survives CMD_CLOSE, so a remote that hangs up is reported as its
    own observation rather than folded into "no answer".

    IT RUNS LAST because it resets the debugger. Anything after it would be
    talking to a re-initialised stub.
    """
    talk(d, dzrp.CMD_INIT, dzrp.init_payload())
    try:
        body = talk(d, dzrp.CMD_CLOSE)
    except dzrp.Timeout:
        return FAIL, "no response within %.0fs; %s" % (d.base_timeout, _liveness())
    if body:
        return FAIL, "answered, but with a %d-byte payload" % len(body)

    try:
        again = d.command(dzrp.CMD_INIT, dzrp.init_payload())
    except dzrp.Timeout:
        # THE SECOND ASSERTION'S FAILURE, and the one control (B) produces. Kept
        # inside the line budget: which connection went quiet, and what that
        # implies, is in this docstring and in doc/DZRP-TESTING.md rather than
        # here. "remote still serving" here is the interesting case — a NEW
        # connection is served while this one is not, and DeZog reuses this one.
        return FAIL, "answered, but the next command was not; %s" % _liveness()
    except dzrp.DzrpError as e:
        if "closed" in str(e):
            return FAIL, "answered, then hung up: this connection is not served"
        raise
    if len(again) < 6 or again[0] != 0:
        return FAIL, "answered, but the next CMD_INIT was %d bytes" % len(again)
    return PASS, "Length=1 response, and a CMD_INIT after it answered in sync"


# A CHECK'S VERDICT IS ONE SHORT SENTENCE of about twenty words. The reasoning
# behind each check lives in its docstring and in doc/DZRP-TESTING.md, which is
# where a reader can afford to read it; a verdict a reviewer has to scroll is a
# verdict nobody reads.
#
# WHAT THE BUDGET COUNTS IS THE `detail` — the sentence a check writes about
# what happened — AND NOT THE LABEL. The label is the check's fixed title from
# the CHECKS table below, written once and never varying at run time, and it is
# not prose the check chose. Two things force this reading, and both are
# checkable rather than matters of taste:
#
#   - hardware-check.py renders `"  %-4s %s %s" % (tag, paint(status), detail)`,
#     where the tag is a bare "H3". Its one agreed exception, H3's failure
#     composite, is described as ~25 words — so that budget is being counted on
#     a detail, because there is no label there to count.
#   - the labels here run 3 to 9 words. Charging them to the budget would give
#     C3 seventeen words of prose and C15 eleven for the same nominal rule,
#     which is not one rule.
#
# Measured mechanically on 2026-08-06 — every reachable detail in both
# harnesses, 81 of them, representative values substituted and worst-case joins
# expanded: single-cause details run 1 to 14 words, median 8. THE LONGEST IS 14,
# main()'s REQUIRED-refusal branch, so the budget is comfortably kept and there
# is no single-cause exception anywhere.
#
# FOUR BRANCHES DELIBERATELY EXCEED IT, all of them joins of several INDEPENDENT
# faults: C11 at 58 words, hardware-check.py's two H2 composites at 38 and 27,
# and C10 at 29. Each is marked at its call site with why. Truncating a compound
# diagnostic is the regression, not the fix: it is the shape that once cost this
# project eight hours with nothing but "no reply".
#
# THE ID IS PART OF THE INTERFACE AND NEVER CHANGES. test/run-dzrp-stub.sh's W3
# asserts its negative control with `grep '^FAIL  C10 '`, hardware-check.py's
# classify() takes the code from field 2 of every FAIL line, and every document
# and issue refers to these by number. Shorten the prose after the id, never the
# id itself.
CHECKS = [
    ("C1 CMD_INIT negotiates a version", chk_init, "INIT"),
    ("C2 a command's length counts payload only", chk_length_convention, "INIT"),
    ("C3 frame preamble", None, "INIT"),            # needs the expect argument
    ("C4 CMD_LOOPBACK round-trips", chk_loopback, "LOOPBACK"),
    ("C5 CMD_LOOPBACK is exact at size boundaries", chk_loopback_sizes, "LOOPBACK"),
    ("C6 sequence numbers echo back", chk_sequence, "INIT"),
    ("C7 CMD_GET_REGISTERS returns a register block", chk_registers, "GET_REGISTERS"),
    ("C8 memory write/read round-trip", chk_memory, "WRITE_MEM"),
    # C9 was written to be LAST, because it is the one check that deliberately
    # leaves bytes in flight and a remote that mishandles them would be broken
    # for whatever ran next. It now proves it left the stream in sync itself,
    # with a second CMD_INIT on the same connection, so the checks below can
    # follow it — and each of those opens its own connection regardless. If C9
    # ever goes red again, read the reds under it as suspect until it is green.
    ("C9 CMD_INIT consumes the declared payload", chk_init_consumes_payload, "INIT"),
    ("C10 CMD_CONTINUE resumes the debuggee and it runs", chk_continue_resumes,
     "CONTINUE"),
    ("C11 the debuggee's state survives the resume", chk_continue_state,
     "CONTINUE"),
    ("C12 CMD_PAUSE while stopped is answered", chk_pause_while_stopped, "PAUSE"),
    ("C13 CMD_GET_SPRITES is answered at the asserted length",
     chk_get_sprites, "GET_SPRITES"),
    ("C14 CMD_GET_SPRITE_PATTERNS is answered at the asserted length",
     chk_get_sprite_patterns, "GET_SPRITE_PATTERNS"),
    ("C16 a full 8 KB bank round-trips", chk_write_bank_full, "WRITE_BANK"),
    ("C17 16 KB in one CMD_WRITE_MEM round-trips", chk_write_mem_large,
     "WRITE_MEM"),
    # C19 AND C20 ARE ORDERED MECHANISM-THEN-CONSEQUENCE, deliberately. If both
    # go red, C19's line says the write never reached memory and C20's says what
    # that costs; read in the other order the second looks like a resume fault.
    # Neither leaves the machine patched — C19 restores what it set and C20's
    # temporary breakpoints are cleared by the remote on its way back in — so
    # they are safe above C18 and C15.
    ("C19 a breakpoint in ROM space is written to memory",
     chk_rom_breakpoint_lands, "SET_BREAKPOINTS"),
    ("C20 a breakpoint in ROM space stops the debuggee",
     chk_rom_breakpoint_fires, "CONTINUE"),
    # C21 IS ONLY MEANINGFUL WHILE C19 PASSES, and it says so itself: its
    # control is an ordinary ROM address that must have taken a breakpoint in
    # the same command, so against a remote where ROM writes are discarded it
    # reports a Precondition rather than a vacuous green.
    ("C21 a breakpoint spares the debugger's own trampoline",
     chk_rom_breakpoint_spares_trampoline, "SET_BREAKPOINTS"),
    # C22 AND C23 ARE TWO CHECKS BECAUSE THE DEFECT HAS TWO SEPARATE COPIES —
    # one in cmd_set_breakpoints, one in cmd_restore_mem — so a fix to either
    # alone must still leave a red naming the other. They sit here, above C18,
    # because on a defective remote the mis-routed write lands in dead space
    # above main_end and leaves the remote serving; nothing below them depends
    # on that being true, and C18 and C15 keep their own places.
    ("C22 a 64K breakpoint above 0xE000 uses the swap window",
     chk_slot7_breakpoint_uses_swap_window, "SET_BREAKPOINTS"),
    ("C23 a 64K CMD_RESTORE_MEM above 0xE000 uses the swap window",
     chk_slot7_restore_uses_swap_window, "RESTORE_MEM"),
    # C24 AND C25 CHANGE NO STATE THE CHECKS BELOW CARE ABOUT — they send
    # CMD_INIT, one refused command, and a CMD_GET_REGISTERS — so their position
    # is free. They sit here because the two things they refuse are breakpoints,
    # and C19-C23 above are where the breakpoints this stub DOES implement are
    # verified: read in that order, "and these two it does not" follows the
    # evidence that the other pair works, rather than preceding it.
    #
    # They must stay ABOVE C18 and C15, which both reset the remote.
    ("C24 CMD_ADD_BREAKPOINT is answered and its id is honest",
     chk_add_breakpoint_refused, "ADD_BREAKPOINT"),
    ("C25 CMD_REMOVE_BREAKPOINT is acknowledged",
     chk_remove_breakpoint_answered, "REMOVE_BREAKPOINT"),
    # C18 IS SECOND-TO-LAST, and for a weaker version of C15's reason. Our stub
    # answers an oversize frame by reporting on its own screen and going to
    # drain_main, which re-initialises the debugger — so anything below it would
    # be talking to a reset stub. It cannot go BELOW C15, which resets it too
    # and must stay last.
    ("C18 an oversize payload does not take the remote with it",
     chk_oversize_payload, "LOOPBACK"),
    # C15 IS LAST, AND MUST STAY LAST. It is the only check that deliberately
    # resets the remote: CMD_CLOSE leaves our stub through `jp main`, which
    # re-initialises prgm_state and the debuggee's saved state. Anything below it
    # would be running against a re-initialised stub, which is a different
    # subject from the one it thinks it is testing.
    ("C15 CMD_CLOSE is answered and the remote serves on", chk_close, "CLOSE"),
]


def leave_session_closed(args):
    """Send a bare CMD_CLOSE last, so the remote is left saying so.

    NOT a check, and deliberately cannot change the verdict — C15 already
    asserts that CMD_CLOSE is answered and that the remote serves on. This
    exists for the Next's SCREEN: issue #14's status line reports the last
    session event it actually observed, so a run whose final act is C15's
    follow-up CMD_INIT correctly leaves the machine reading "Session opened",
    which is a confusing thing to walk up to after a completed test run.

    Nothing may follow this. It closes the socket and sends no further command,
    because a CMD_INIT afterwards would reopen the session it just closed —
    which is exactly the mistake this teardown exists to stop making.

    Failures are reported and swallowed. A remote that cannot be reached here
    has already been measured by fifteen checks that could reach it, and
    inventing a sixteenth verdict out of a teardown would be a check nobody
    designed.
    """
    try:
        transport = dzrp.open_remote(args.remote, timeout=args.timeout)
    except (OSError, dzrp.DzrpError) as e:
        print("  (teardown: could not reconnect to send CMD_CLOSE: %s)" % e)
        return
    d = dzrp.Dzrp(transport, start_byte=args.start_byte, base_timeout=args.timeout)
    dzrp.send_close_quietly(d)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--remote", required=True,
                    help="tcp:<host>:<port> or serial:<device>:<baud>")
    ap.add_argument("--start-byte", default="auto", choices=["auto", "a5", "none"],
                    help="frame preamble the remote uses (default: autodetect)")
    ap.add_argument("--expect-preamble", default="report",
                    choices=["report", "a5", "none"],
                    help="assert the preamble rather than only reporting it")
    ap.add_argument("--require", default="",
                    help="comma-separated commands whose absence is a FAILURE "
                         "rather than UNSUPPORTED, e.g. INIT,LOOPBACK")
    ap.add_argument("--only", default="",
                    help="comma-separated check ids to run, e.g. C10,C11")
    ap.add_argument("--no-continue", action="store_true",
                    help="NEGATIVE CONTROL: perform the execution-control "
                         "checks' whole setup but never send CMD_CONTINUE. "
                         "They MUST then fail; if they pass, they are not "
                         "measuring the resume.")
    ap.add_argument("--timeout", type=float, default=5.0)
    args = ap.parse_args()

    global REMOTE_SPEC, NO_CONTINUE
    REMOTE_SPEC = args.remote
    NO_CONTINUE = args.no_continue

    required = {s.strip().upper() for s in args.require.split(",") if s.strip()}
    unknown = required - set(NAMES)
    if unknown:
        print("ERROR: --require names unknown commands: %s" % ", ".join(sorted(unknown)),
              file=sys.stderr)
        return 2

    ids = [label.split()[0] for label, _, _ in CHECKS]
    only = {s.strip().upper() for s in args.only.split(",") if s.strip()}
    unknown = only - set(ids)
    if unknown:
        print("ERROR: --only names unknown checks: %s (have: %s)"
              % (", ".join(sorted(unknown)), ", ".join(ids)), file=sys.stderr)
        return 2

    sb = {"auto": "auto", "a5": dzrp.START_BYTE, "none": None}[args.start_byte]

    print("== DZRP conformance against %s" % args.remote)
    if required:
        print("   required commands: %s" % ", ".join(sorted(required)))
    if only:
        print("   only: %s" % ", ".join(sorted(only)))
    if NO_CONTINUE:
        # Loud, because an output that looks like a normal run but is missing
        # the one command under test is exactly the artefact ERRORS.md warns
        # about being reported as a finding.
        print("   *** NEGATIVE CONTROL: --no-continue. No CMD_CONTINUE is sent.")
        print("   *** C10 and C11 MUST fail. This is NOT a conformance result.")
    print("")

    counts = {PASS: 0, FAIL: 0, UNSUP: 0}
    ran = 0

    for label, fn, cmd_name in CHECKS:
        if only and label.split()[0] not in only:
            continue
        ran += 1
        # Announce before running. On a terminal this is written WITHOUT a
        # newline and the verdict overwrites it, so the console ends up with
        # exactly one line per check rather than two.
        if PROGRESS:
            print("      %s ..." % label, end="\r", flush=True)
        # Each check gets a fresh connection, so one refused command cannot
        # take the rest down. A remote that serves one client at a time needs
        # a moment to start listening again between them — CSpect's plugin
        # logs "Disconnected / Waiting for a connection" — so retry briefly
        # rather than calling that transient gap an unreachable remote.
        transport = None
        for attempt in range(20):
            try:
                transport = dzrp.open_remote(args.remote, timeout=args.timeout)
                break
            except (OSError, dzrp.DzrpError):
                time.sleep(0.25)
        if transport is None:
            print("ERROR: cannot reach %s after 5s of retries" % args.remote,
                  file=sys.stderr)
            return 2
        d = dzrp.Dzrp(transport, start_byte=sb, base_timeout=args.timeout)
        try:
            if fn is None:                       # the preamble check needs a flag
                status, detail = chk_preamble(d, args.expect_preamble)
            else:
                status, detail = fn(d)
        except Unsupported as e:
            if cmd_name in required or cmd_name in ALWAYS_REQUIRED:
                status = FAIL
                # The longest single-cause detail either harness prints, at 14
                # words — INSIDE the budget, which counts the detail and not the
                # label. It is the longest because it carries three facts that
                # are each load-bearing: which command was refused, that it was
                # REQUIRED rather than optional, and the transport-level reason
                # the remote gave. That combination is what separates "a partial
                # implementation" from "a broken remote". Not an exception.
                detail = "CMD_%s is REQUIRED but the remote does not implement it (%s)" % (
                    cmd_name, e)
            else:
                status = UNSUP
                detail = "remote does not implement CMD_%s" % cmd_name
        except Precondition as e:
            # A failure, because nothing was established — but named as what it
            # is, so it is not read as evidence about this check's subject.
            status = FAIL
            # ONE WORD, and it is a label rather than a sentence: everything
            # after it is the fault itself, and the fault has to fit the line
            # budget beside it. What PRECONDITION *means* — the setup broke, so
            # nothing here is evidence about this check's subject — is in
            # doc/DZRP-TESTING.md. The long form spent six words saying it on
            # every occurrence and pushed three C10/C11 branches past the
            # budget, which is what measuring the FAIL paths turned up.
            detail = "PRECONDITION: %s" % e
        except dzrp.DzrpError as e:
            status, detail = FAIL, str(e)
        except OSError as e:
            # THE CONNECTION WENT AWAY MID-CHECK — the peer reset it, closed it,
            # or a send died part-way. `TcpTransport.write` is a bare
            # `sock.sendall` (dzrp.py), so that surfaces as BrokenPipeError or
            # ConnectionResetError, and NEITHER is a DzrpError.
            #
            # Without this clause such a check does not fail — it escapes main()
            # as a traceback and takes EVERY CHECK BELOW IT with it, including
            # C15, which this suite requires to run and to run last. So the
            # failure mode was "the suite silently stops covering CMD_CLOSE",
            # which is worse than any single red. Issue #33.
            #
            # Kept separate from DzrpError rather than folded into it, because
            # the two say different things and the detail should too: DzrpError
            # is the remote answering wrongly; this is the remote not being
            # there. A check that provokes a disconnect ON PURPOSE catches it
            # locally and never arrives here — chk_oversize_payload does — so
            # anything reaching this clause did not expect it, and FAIL is the
            # right verdict rather than UNSUP.
            status, detail = FAIL, "the connection failed mid-check: %s" % e
        finally:
            d.close()

        counts[status] += 1
        # flush, because this is usually a pipe and Python would otherwise hold
        # every line until the suite ended — which is the whole problem this
        # streaming exists to fix.
        # Erase whatever the progress line left on this row before writing the
        # verdict over it — the labels differ in length, so without this the
        # tail of a longer previous line survives past the shorter new one.
        if PROGRESS:
            print("\033[K", end="")
        print("%s %s%s" % (paint(status), label, " — %s" % detail if detail else ""),
              flush=True)

    leave_session_closed(args)

    print("")
    summary = "%d passed, %d failed, %d unsupported, of %d checks" % (
        counts[PASS], counts[FAIL], counts[UNSUP], ran)
    print("DZRP conformance: " + summary)
    # Everything-unsupported is not a pass. A remote that answered nothing at
    # all would otherwise exit 0, which is precisely the "green result that
    # cannot distinguish success from noise" ERRORS.md already warns about.
    if counts[PASS] == 0:
        print("  (nothing passed — treating that as a failure, not a partial remote)")
        return 1
    return 1 if counts[FAIL] else 0


if __name__ == "__main__":
    sys.exit(main())
