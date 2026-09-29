#!/usr/bin/env python3
"""The client half of the `zrcp-*-func` regression rows (GH #280).

zrcp-frontend.md §6.2: a Python client drives a LIVE jnext — started with
`--zrcp-port 0` — and diffs the replies BYTE-EXACTLY against strings derived
from the design's [T] transcripts of ZEsarUX 12.0 (the jnext-specific fields,
the version and the register values, are read rather than guessed). It is an
independent reading of the wire: it knows ZRCP only from the transcripts and
from DeZog 3.7.4's parser (`zesaruxsocket.ts`: a reply ends at a last line that
starts with `command` and ends with `> `; `decodezesaruxdata.ts`: fixed widths
after each label), never from the server's source.

    zrcp-peer.py <scenario> <port> [args...]

Prints `PASS <summary>` and exits 0, or `FAIL <reason>` and exits 1. Every wait
is bounded: a silent server is a FAIL, never a hang.

THE MACHINE is `--machine 48k` with `--magic-breakpoint`. The scenario writes
its own program into RAM at 8000H with `write-memory-raw` and points PC at it,
so no row depends on a demo build:

    8000  00 00 00   NOP x3
    8003  ED FF      the magic breakpoint (jnext pauses after it)
    8005  18 FE      JR $
"""

import re
import socket
import struct
import sys
import time

HOST = "127.0.0.1"
TIMEOUT = 10.0
PROMPT = b"command> "
PROMPT_STEP = b"command@cpu-step> "
WELCOME = (b"Welcome to ZEsarUX remote command protocol (ZRCP)\n"
           b"Write help for available commands\n\ncommand> ")
RUNNING = b"Running until a breakpoint, key press or data sent, menu opening or other event\n"
PROGRAM = "000000EDFF18FE"

# DeZog 3.7.4 decodezesaruxdata.ts: each label is found once, then a fixed
# number of hex digits is read after it.
FIELDS = [("PC=", 4), ("SP=", 4), ("AF=", 4), ("BC=", 4), ("HL=", 4), ("DE=", 4),
          ("IX=", 4), ("IY=", 4), ("AF'=", 4), ("BC'=", 4), ("HL'=", 4), ("DE'=", 4),
          ("I=", 2), ("R=", 2)]


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


class Zrcp:
    def __init__(self, port):
        self.s = socket.create_connection((HOST, port), timeout=TIMEOUT)
        self.buf = b""
        self.closed = False

    def _fill(self, deadline, closing=False):
        self.s.settimeout(max(0.01, deadline - time.time()))
        try:
            chunk = self.s.recv(65536)
        except socket.timeout:
            return False
        if not chunk:
            if closing:
                self.closed = True
                return False
            raise Fail("the server closed the connection")
        self.buf += chunk
        return True

    def reply(self, timeout=TIMEOUT):
        """Everything up to and including the next prompt (DeZog's rule)."""
        deadline = time.time() + timeout
        while True:
            for p in (PROMPT, PROMPT_STEP):
                if self.buf.endswith(p):
                    out, self.buf = self.buf, b""
                    return out
            if time.time() >= deadline or not self._fill(deadline):
                raise Fail("no prompt within %.1f s; got %r" % (timeout, self.buf[-200:]))

    def quiet_for(self, seconds):
        """What arrives within `seconds` (must usually be nothing)."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            self._fill(deadline)
        out, self.buf = self.buf, b""
        return out

    def goodbye(self, seconds=TIMEOUT):
        """`quit`: what arrives before the server closes — which it must."""
        self.closed = False
        deadline = time.time() + seconds
        while time.time() < deadline and not self.closed:
            self._fill(deadline, closing=True)
        check(self.closed, "the server did not close the connection after quit")
        out, self.buf = self.buf, b""
        return out

    def cmd(self, line, timeout=TIMEOUT):
        self.s.sendall(line.encode() + b"\n")
        return self.reply(timeout)

    def close(self):
        self.s.close()


def dezog_regs(line):
    """The register values DeZog reads from a register line."""
    text = line.decode("latin1")
    out = {}
    for label, width in FIELDS:
        i = text.find(label)
        check(i >= 0, "register line has no %s: %r" % (label, text))
        out[label] = int(text[i + len(label):i + len(label) + width], 16)
    m = re.search(r" IM([0-2]) ", text)
    check(m is not None, "no IM<n> in the register line")
    check("  F=" in text, "not two spaces before F=")
    check(re.search(r" MMU=[0-9a-f]{32}$", text) is not None, "no 8-slot MMU= at the end")
    return out


def stop_shape(reply, pc, prompt=PROMPT_STEP, fired=None):
    """[fired line] + register line with ` TSTATES: n` + disassembly at PC +
    exactly one prompt, as [T3]/[T5] show it."""
    text = reply
    if fired is not None:
        head = b"Breakpoint fired: " + fired + b"\n"
        check(text.startswith(head), "no %r line first: %r" % (head, text[:120]))
        text = text[len(head):]
    else:
        check(b"Breakpoint fired" not in text, "an unexpected fired line: %r" % text[:200])
    check(text.endswith(prompt) and text.count(b"> ") == 1,
          "not exactly one prompt at the end: %r" % text[-80:])
    lines = text[:-len(prompt)].split(b"\n")
    check(len(lines) == 3 and lines[2] == b"", "not two lines + prompt: %r" % text)
    regs_line, dis = lines[0], lines[1]
    check(re.search(rb" TSTATES: [0-9]+$", regs_line) is not None,
          "no ' TSTATES: n' after the register line: %r" % regs_line[-60:])
    regs = dezog_regs(regs_line[:regs_line.rindex(b" TSTATES:")])
    check(regs["PC="] == pc, "PC=%04x in the stop, wanted %04x" % (regs["PC="], pc))
    check(dis.startswith(b"  %04X " % pc), "disassembly line not at PC: %r" % dis)
    check(b"$" not in dis, "a $ in the disassembly: %r" % dis)
    return dis


def load_program(z):
    check(z.cmd("write-memory-raw 32768 " + PROGRAM) == b"\n" + PROMPT_STEP,
          "write-memory-raw did not answer empty")
    check(z.cmd("read-memory 32768 7") == PROGRAM.encode() + b"\n" + PROMPT_STEP,
          "the program did not read back")
    r = z.cmd("set-register PC=8000H")
    check(dezog_regs(r[:-len(b"\n" + PROMPT_STEP)])["PC="] == 0x8000,
          "set-register PC=8000H did not set PC")


def sc_m1(port, dzrp_port=None):
    """§6.2 items 1-4, and — with a DZRP port — the two servers side by side."""
    z = Zrcp(port)

    # 1. welcome + prompt; get-version coerces to >= 10.3 (and < 12.1).
    check(z.reply() == WELCOME, "the welcome is not ZEsarUX's, byte for byte")
    ver = z.cmd("get-version")
    m = re.match(rb"^(\d+)\.(\d+)", ver)
    check(m is not None and ver.endswith(b"\n" + PROMPT), "get-version: %r" % ver)
    major, minor = int(m.group(1)), int(m.group(2))
    check((major, minor) >= (10, 3) and (major, minor) < (12, 1),
          "get-version %r does not coerce into [10.3, 12.1)" % ver)

    # 2. enter-cpu-step -> the prompt suffix; cpu-step's reply shape (column 7);
    #    get-registers at DeZog's widths.
    check(z.cmd("enter-cpu-step") == b"\n" + PROMPT_STEP, "enter-cpu-step: not the step prompt")
    load_program(z)
    dis = stop_shape(z.cmd("cpu-step"), 0x8001)
    check(dis[7:11] == b"NOP", "column 7 of the step's disassembly is %r" % dis[7:11])
    regs = z.cmd("get-registers")
    check(regs.endswith(b"\n" + PROMPT_STEP), "get-registers: no prompt")
    check(dezog_regs(regs[:-len(b"\n" + PROMPT_STEP)])["PC="] == 0x8001,
          "get-registers after the step: PC is not 8001")

    # 3. run: "Running until" FIRST and alone, then the stop with its fired
    #    line, and PC in the register line is where the machine stopped. The
    #    whole byte sequence is compared, so a prompt sent early is a FAIL
    #    whatever the timing. (A slot breakpoint is WP-4's; this milestone's
    #    named stop is the magic breakpoint, §2.4.)
    z.s.sendall(b"run\n")
    stop = z.reply()
    check(stop.startswith(RUNNING), "run's first line is not ZEsarUX's: %r" % stop[:100])
    stop_shape(stop[len(RUNNING):], 0x8005, fired=b"Magic breakpoint")

    # 4. run then a bare newline: the stop without a fired line, and the blank
    #    line NOT executed — exactly one prompt, nothing after it.
    z.s.sendall(b"run\n")
    first = z.quiet_for(0.4)
    check(first == RUNNING, "run on JR $: wanted the first line alone, got %r" % first)
    z.s.sendall(b"\n")
    stop_shape(z.reply(), 0x8005)
    extra = z.quiet_for(0.4)
    check(extra == b"", "the interrupting newline was executed: %r" % extra)
    check(z.cmd("about") == b"jnext ZRCP remote command protocol\n" + PROMPT_STEP,
          "the session did not serve on after the stop")

    summary = "welcome, get-version %s, cpu-step at column 7, run -> magic stop at 8005, " \
              "run + newline -> one plain stop" % ver.split(b"\n")[0].decode()

    if dzrp_port is not None:
        # The DZRP server of the same process answers CMD_LOOPBACK (15) while
        # the ZRCP client holds the machine.
        d = socket.create_connection((HOST, int(dzrp_port)), timeout=TIMEOUT)
        d.sendall(struct.pack("<IBB", 2, 9, 15) + b"hi")
        got = b""
        deadline = time.time() + TIMEOUT
        while len(got) < 7 and time.time() < deadline:
            chunk = d.recv(64)
            check(chunk != b"", "the DZRP server closed the connection")
            got += chunk
        check(got == struct.pack("<IB", 3, 9) + b"hi", "DZRP loopback beside ZRCP: %r" % got)
        d.close()
        summary += "; DZRP loopback beside it"

    z.s.sendall(b"quit\n")
    bye = z.goodbye()
    check(bye == b"Sayonara baby\n", "quit: %r" % bye)
    z.close()
    return summary


def sc_smoke(port):
    """A loop owner registers the server: welcome, a step, back out, quit."""
    z = Zrcp(port)
    check(z.reply() == WELCOME, "the welcome is not ZEsarUX's, byte for byte")
    check(z.cmd("enter-cpu-step") == b"\n" + PROMPT_STEP, "enter-cpu-step: not the step prompt")
    load_program(z)
    stop_shape(z.cmd("cpu-step"), 0x8001)
    check(z.cmd("exit-cpu-step") == b"\n" + PROMPT, "exit-cpu-step: not the plain prompt")
    z.s.sendall(b"quit\n")
    check(z.goodbye() == b"Sayonara baby\n", "quit did not say goodbye")
    z.close()
    return "welcome, enter-cpu-step, cpu-step to 8001, exit-cpu-step, quit"


SCENARIOS = {
    "m1": sc_m1,
    "smoke": sc_smoke,
}


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in SCENARIOS:
        print("usage: zrcp-peer.py {%s} <port> [args...]" % "|".join(SCENARIOS))
        return 2
    try:
        summary = SCENARIOS[sys.argv[1]](int(sys.argv[2]), *sys.argv[3:])
    except Fail as e:
        print("FAIL %s" % e)
        return 1
    except (OSError, ValueError) as e:
        print("FAIL %s: %s" % (type(e).__name__, e))
        return 1
    print("PASS %s" % summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
