#!/usr/bin/env python3
"""The client half of the `zrcp-*-func` regression rows (GH #280).

zrcp-frontend.md §6.2: a Python client drives a LIVE jnext — started with
`--zrcp-port 0` — and diffs the replies BYTE-EXACTLY against strings derived
from the design's [T] transcripts of ZEsarUX 12.0 (the jnext-specific fields,
the version and the register values, are read rather than guessed). The
committed fixture test/fixtures/zrcp/zesarux-12.0-exchanges.txt is the source
of the welcome and `run` bytes, and the `fixture` scenario replays every one of
its scenes (ZEsarUX 12.0's own recorded replies) byte for byte. It is an
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

import os
import re
import socket
import struct
import sys
import time

HOST = "127.0.0.1"
TIMEOUT = 10.0
PROMPT = b"command> "
PROMPT_STEP = b"command@cpu-step> "
FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "fixtures", "zrcp",
                       "zesarux-12.0-exchanges.txt")
WELCOME = None  # both read from FIXTURE by main(): ZEsarUX 12.0's own bytes
RUNNING = None
BUSY = b"Error. Another ZRCP client is"
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

    def until_quiet(self, quiet=0.5, cap=TIMEOUT):
        """Everything that arrives before `quiet` seconds of silence or the
        server closing (the fixture's rule for a reply with no prompt)."""
        self.closed = False
        end = time.time() + cap
        while not self.closed and time.time() < end:
            if not self._fill(min(end, time.time() + quiet), closing=True):
                break
        out, self.buf = self.buf, b""
        return out

    def cmd(self, line, timeout=TIMEOUT):
        self.s.sendall(line.encode() + b"\n")
        return self.reply(timeout)

    def close(self):
        self.s.close()


def unquote(text, where):
    """`"..."` with the four escapes the fixture allows: \\n \\r \\" \\\\."""
    check(len(text) >= 2 and text[0] == '"' and text[-1] == '"', "%s: not quoted" % where)
    out, i, body = bytearray(), 0, text[1:-1]
    while i < len(body):
        ch = body[i]
        check(ch != '"', "%s: bare quote" % where)
        if ch == "\\":
            i += 1
            check(i < len(body) and body[i] in 'nr"\\', "%s: bad escape" % where)
            ch = {"n": "\n", "r": "\r"}.get(body[i], body[i])
        out += ch.encode("latin1")
        i += 1
    return bytes(out)


def load_fixture():
    """[(name, welcome, [(command, reply), ...]), ...] from FIXTURE."""
    scenes = []
    with open(FIXTURE, encoding="latin1") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            where = "%s:%d" % (os.path.basename(FIXTURE), n)
            if line.startswith("@scene "):
                scenes.append((line.split()[1], bytearray(), []))
                continue
            check(scenes and line[:2] in ("> ", "< "), "%s: malformed" % where)
            val = unquote(line[2:], where)
            if line[0] == ">":
                scenes[-1][2].append((val.decode("latin1"), bytearray()))
            elif not scenes[-1][2]:
                scenes[-1][1].extend(val)
            else:
                scenes[-1][2][-1][1].extend(val)
    return [(name, bytes(w), [(c, bytes(r)) for c, r in ex]) for name, w, ex in scenes]


def fixture_constants():
    """The welcome every scene opens with, and what `run` answers first."""
    scenes = load_fixture()
    welcomes = {w for _, w, _ in scenes}
    check(len(welcomes) == 1, "the fixture's scenes disagree on the welcome")
    runs = [r for _, _, ex in scenes for c, r in ex if c == "run"]
    check(len(runs) == 1, "the fixture has not exactly one `run` exchange")
    return welcomes.pop(), runs[0]


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


def put(z, addr, hexbytes):
    check(z.cmd("write-memory-raw %d %s" % (addr, hexbytes)) == b"\n" + PROMPT_STEP,
          "write-memory-raw %04x did not answer empty" % addr)


def regs_set(z, **kv):
    for k, v in kv.items():
        r = z.cmd("set-register %s=%XH" % (k, v))
        check(r.endswith(b"\n" + PROMPT_STEP), "set-register %s: %r" % (k, r[-80:]))


def run_fired(z, pc, fired):
    z.s.sendall(b"run\n")
    stop = z.reply()
    check(stop.startswith(RUNNING), "run's first line is not ZEsarUX's: %r" % stop[:100])
    stop_shape(stop[len(RUNNING):], pc, fired=fired)


def sc_m2(port):
    """§6.2 items 3, 5 and 6 (WP-4): DeZog's breakpoint sequence, a condition
    hit, a memory breakpoint, and DeZog's step-over / step-out conditions."""
    z = Zrcp(port)
    check(z.reply() == WELCOME, "the welcome is not ZEsarUX's, byte for byte")
    check(z.cmd("enter-cpu-step") == b"\n" + PROMPT_STEP, "enter-cpu-step: not the step prompt")
    # DeZog's init: clear-membreakpoints, enable-breakpoints.
    check(z.cmd("clear-membreakpoints") == b"\n" + PROMPT_STEP, "clear-membreakpoints")
    check(z.cmd("enable-breakpoints") == b"\n" + PROMPT_STEP, "enable-breakpoints")
    check(z.cmd("enable-breakpoints") == b"Error. Already enabled\n" + PROMPT_STEP,
          "a second enable-breakpoints is not ZEsarUX's error")
    # ZEsarUX's own grouping, through the DSL's evaluator.
    check(z.cmd("evaluate 9-3-1") == b"7\n" + PROMPT_STEP, "evaluate 9-3-1 is not 9-(3-1)")

    # 3. A plain source breakpoint, set as DeZog sets it (three commands).
    put(z, 0x8000, "00000000" + "18FE")          # NOP x4; JR $
    regs_set(z, PC=0x8000, SP=0xFF00, IFF1=0, IFF2=0)
    for line in ("set-breakpointaction 1", "set-breakpoint 1 PC=08002h", "enable-breakpoint 1"):
        check(z.cmd(line) == b"\n" + PROMPT_STEP, "%s did not answer empty" % line)
    run_fired(z, 0x8002, b"PC=8002H")
    check(z.cmd("get-breakpoints 1") == b"Breakpoints: On\nEnabled 1: PC=8002H\n\n" + PROMPT_STEP,
          "get-breakpoints 1 does not list the slot as ZEsarUX re-prints it")
    check(z.cmd("disable-breakpoint 1") == b"\n" + PROMPT_STEP, "disable-breakpoint 1")

    # A condition hit: PC=<n> AND <condition> stops only where it holds.
    put(z, 0x8000, "3E05" + "00" + "3E07" + "00" + "18FE")  # LD A,5; NOP; LD A,7; NOP; JR $
    regs_set(z, PC=0x8000)
    check(z.cmd("set-breakpoint 2 PC=8005H AND A=5") == b"\n" + PROMPT_STEP, "slot 2")
    check(z.cmd("set-breakpoint 3 PC=8002H AND A=5") == b"\n" + PROMPT_STEP, "slot 3")
    run_fired(z, 0x8002, b"PC=8002H AND A=5")
    check(z.cmd("disable-breakpoint 2") == b"\n" + PROMPT_STEP, "disable 2")
    check(z.cmd("disable-breakpoint 3") == b"\n" + PROMPT_STEP, "disable 3")

    # 5. A memory breakpoint on the address the program writes.
    put(z, 0x8000, "3EAA" + "320190" + "18FE")   # LD A,AAH; LD (9001H),A; JR $
    regs_set(z, PC=0x8000)
    check(z.cmd("set-membreakpoint 9000h 2 2") == b"\n" + PROMPT_STEP, "set-membreakpoint")
    run_fired(z, 0x8005, b"Memory Breakpoint Write Address: 9001H")
    check(z.cmd("set-membreakpoint 9000h 0 2") == b"\n" + PROMPT_STEP, "membreakpoint removal")

    # 6. DeZog's step-over (SP>=, decimal) across a CALL, then its step-out.
    put(z, 0x8000, "CD1080" + "00" + "18FE")     # CALL 8010H; NOP; JR $
    put(z, 0x8010, "0000C9")                     # NOP; NOP; RET
    regs_set(z, PC=0x8000, SP=0xFF00)
    check(z.cmd("set-breakpointaction 100") == b"\n" + PROMPT_STEP, "action 100")
    check(z.cmd("set-breakpoint 100 SP>=65280") == b"\n" + PROMPT_STEP, "slot 100")
    check(z.cmd("enable-breakpoint 100") == b"\n" + PROMPT_STEP, "enable 100")
    run_fired(z, 0x8003, b"SP>=65280")
    check(z.cmd("disable-breakpoint 100") == b"\n" + PROMPT_STEP, "disable 100")
    put(z, 0xFEFE, "0380")                       # the return address the CALL pushed
    regs_set(z, PC=0x8010, SP=0xFEFE)
    check(z.cmd("set-breakpoint 100 PC=PEEKW(SP-2) AND SP>=65280") == b"\n" + PROMPT_STEP,
          "step-out slot")
    run_fired(z, 0x8003, b"PC=PEEKW(SP-2) AND SP>=65280")

    # DeZog's disconnect.
    check(z.cmd("clear-membreakpoints") == b"\n" + PROMPT_STEP, "clear-membreakpoints")
    check(z.cmd("disable-breakpoints") == b"\n" + PROMPT_STEP, "disable-breakpoints")
    check(z.cmd("exit-cpu-step") == b"\n" + PROMPT, "exit-cpu-step: not the plain prompt")
    z.s.sendall(b"quit\n")
    check(z.goodbye() == b"Sayonara baby\n", "quit did not say goodbye")
    z.close()
    return ("breakpoint at 8002, PC=8002H AND A=5 hit, write at 9001H, "
            "SP>=65280 step-over and PEEKW step-out at 8003")


def make_sna48(path, program, at=0x8000, sp=0xFF00):
    """A 48K .sna (27-byte header + 48 KB of RAM) whose PC, popped from the
    stack by the loader, is `at`, with `program` there, interrupts off."""
    ram = bytearray(48 * 1024)
    ram[at - 0x4000:at - 0x4000 + len(program)] = program
    sp_saved = sp - 2
    ram[sp_saved - 0x4000] = at & 0xFF
    ram[sp_saved - 0x4000 + 1] = at >> 8
    header = struct.pack("<BHHHHHHHHHBBHHBB", 0x3F, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                         0x00, 0, 0xFFFF, sp_saved, 1, 7)
    with open(path, "wb") as f:
        f.write(header + bytes(ram))


def sc_m3(port, tmpdir):
    """§6.2 item 7 and WP-5 live: smartload of a .sna, cpu-history (get 0 after
    three steps), coverage, extended-stack, load-binary / save-binary, and a
    snapshot-save / snapshot-load round trip."""
    import os
    z = Zrcp(port)
    check(z.reply() == WELCOME, "the welcome is not ZEsarUX's, byte for byte")
    check(z.cmd("enter-cpu-step") == b"\n" + PROMPT_STEP, "enter-cpu-step")

    # smartload a .sna: 8000 CALL 8010 / LD A,5 / NOP / JR $ ; 8010 NOP / RET
    sna = os.path.join(tmpdir, "zrcp-m3.sna")
    prog = bytes([0xCD, 0x10, 0x80, 0x3E, 0x05, 0x00, 0x18, 0xFE]) + bytes(8) + bytes([0x00, 0xC9])
    make_sna48(sna, prog)
    check(z.cmd('smartload "%s"' % sna) == b"\n" + PROMPT_STEP, "smartload of a .sna")
    r = z.cmd("get-registers")
    regs = dezog_regs(r[:-len(b"\n" + PROMPT_STEP)])
    check(regs["PC="] == 0x8000 and regs["SP="] == 0xFF00,
          "after smartload PC=%04x SP=%04x, wanted 8000 / ff00" % (regs["PC="], regs["SP="]))

    # DeZog's initAfterLoad.
    check(z.cmd("cpu-code-coverage enabled yes") == b"\n" + PROMPT_STEP, "coverage on")
    check(z.cmd("cpu-code-coverage clear") == b"\n" + PROMPT_STEP, "coverage clear")
    for line in ("cpu-history enabled yes", "cpu-history set-max-size 10000", "cpu-history clear",
                 "cpu-history started yes", "cpu-history ignrephalt yes",
                 "cpu-history ignrepldxr yes"):
        check(z.cmd(line) == b"\n" + PROMPT_STEP, "%s did not answer empty" % line)
    check(z.cmd("extended-stack enabled no") == b"Error. Already disabled\n" + PROMPT_STEP,
          "extended-stack enabled no (DeZog's first, error suppressed)")
    check(z.cmd("extended-stack enabled yes") == b"\n" + PROMPT_STEP, "extended-stack on")

    # Step CALL / NOP / RET; the call is typed on the stack after the first.
    stop_shape(z.cmd("cpu-step"), 0x8010)
    check(z.cmd("extended-stack get 1") == b"8003H call\n\n" + PROMPT_STEP,
          "extended-stack get 1 after the CALL is not the typed return address")
    stop_shape(z.cmd("cpu-step"), 0x8011)
    stop_shape(z.cmd("cpu-step"), 0x8003)

    # 7. cpu-history get 0 is the third step (RET at 8011), as DeZog reads it.
    h = z.cmd("cpu-history get 0")
    check(h.endswith(b" \n" + PROMPT_STEP), "cpu-history get 0: no trailing space: %r" % h[-40:])
    line = h[:-len(b"\n" + PROMPT_STEP)].decode("latin1")
    check(line.startswith("PC=8011 "), "cpu-history get 0 is not the newest step: %r" % line[:40])
    check(re.search(r" \(PC\)=c9[0-9a-f]{6} ", line) is not None, "(PC)= not RET's bytes: %r" % line)
    check(" (SP)=8003 " in line, "(SP)= not the return address: %r" % line)
    check(re.search(r" MMU=[0-9a-f]{32} $", line) is not None, "no MMU= field: %r" % line)
    check(z.cmd("cpu-history get 3") == b"ERROR: index out of range\n" + PROMPT_STEP,
          "cpu-history get 3 past the end")

    # Coverage: the three executed addresses, ascending, then clear.
    check(z.cmd("cpu-code-coverage get") == b"8000 8010 8011 \n" + PROMPT_STEP,
          "cpu-code-coverage get is not the three executed addresses")

    # load-binary / save-binary through files on the host.
    src = os.path.join(tmpdir, "zrcp-m3-in.bin")
    out = os.path.join(tmpdir, "zrcp-m3-out.bin")
    with open(src, "wb") as f:
        f.write(bytes([0x12, 0x34, 0x56]))
    check(z.cmd('load-binary "%s" 36864 0' % src) == b"\n" + PROMPT_STEP, "load-binary")
    check(z.cmd("read-memory 36864 3") == b"123456\n" + PROMPT_STEP, "load-binary landed wrong")
    check(z.cmd('save-binary "%s" 36864 3' % out) == b"\n" + PROMPT_STEP, "save-binary")
    with open(out, "rb") as f:
        check(f.read() == bytes([0x12, 0x34, 0x56]), "save-binary wrote the wrong bytes")

    # snapshot-save at a frame boundary (a pause while running lands on one),
    # a step, then snapshot-load brings PC back.
    check(z.cmd("exit-cpu-step") == b"\n" + PROMPT, "exit-cpu-step")
    time.sleep(0.2)
    check(z.cmd("enter-cpu-step") == b"\n" + PROMPT_STEP, "enter-cpu-step again")
    pc0 = dezog_regs(z.cmd("get-registers")[:-len(b"\n" + PROMPT_STEP)])["PC="]
    saved = z.cmd("snapshot-save state.zsf")
    check(saved == b"\n" + PROMPT_STEP, "snapshot-save at a pause: %r" % saved)
    z.cmd("set-register PC=8010H")
    check(z.cmd("snapshot-load state.zsf") == b"\n" + PROMPT_STEP, "snapshot-load")
    pc1 = dezog_regs(z.cmd("get-registers")[:-len(b"\n" + PROMPT_STEP)])["PC="]
    check(pc1 == pc0, "snapshot-load: PC %04x, saved at %04x" % (pc1, pc0))

    # DeZog's disconnect.
    for l in ("cpu-history enabled no", "cpu-code-coverage enabled no", "extended-stack enabled no"):
        check(z.cmd(l) == b"\n" + PROMPT_STEP, "%s" % l)
    z.s.sendall(b"quit\n")
    check(z.goodbye() == b"Sayonara baby\n", "quit did not say goodbye")
    z.close()
    return ("smartload .sna at 8000, history get 0 = RET at 8011 with (SP)=8003, coverage "
            "8000 8010 8011, CALL typed, load/save-binary, snapshot round trip at %04x" % pc0)


def sc_fixture(port):
    """Every scene of FIXTURE, each on a new connection, byte for byte."""
    scenes = load_fixture()
    total = 0
    for name, welcome, ex in scenes:
        deadline = time.time() + TIMEOUT
        while True:  # the last scene's hang-up may not be processed yet
            z = Zrcp(port)
            while not (z.buf.endswith(PROMPT) or z.closed) and time.time() < deadline:
                z._fill(deadline, closing=True)
            first, z.buf = z.buf, b""
            if not first.startswith(BUSY) or time.time() >= deadline:
                break
            z.close()
            time.sleep(0.1)
        check(first == welcome, "scene %s: the welcome is %r" % (name, first))
        for command, want in ex:
            z.s.sendall(command.encode("latin1") + b"\n")
            if want.endswith(PROMPT) or want.endswith(PROMPT_STEP):
                got = z.reply()
            else:
                got = z.until_quiet()
            check(got == want, "scene %s, %r: got %r, want %r" % (name, command, got[:200],
                                                                  want[:200]))
            total += 1
        z.close()
    return "fixture: %d scenes, %d exchanges byte-identical to ZEsarUX 12.0" % (len(scenes),
                                                                              total)


SCENARIOS = {
    "fixture": sc_fixture,
    "m1": sc_m1,
    "m3": sc_m3,
    "m2": sc_m2,
    "smoke": sc_smoke,
}


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in SCENARIOS:
        print("usage: zrcp-peer.py {%s} <port> [args...]" % "|".join(SCENARIOS))
        return 2
    global WELCOME, RUNNING
    try:
        WELCOME, RUNNING = fixture_constants()
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
