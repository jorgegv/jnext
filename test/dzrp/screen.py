# ---------------------------------------------------------------------------
# COPIED INTO JNEXT (GH #12, dzrp-frontend.md §11 WP-6) from the owner's
# dezogif_ng project — https://github.com/jorgegv/dezogif_ng, test/dzrp/screen.py,
# at commit 709ae7d77d444e0e14d88d5c6114b2a7c18e2be6 (the files' last change:
# 4eb9359cce05b3e3e95234cedbebfe75997f9ada). GPLv3, as jnext is. A copy, not a
# submodule: it is jnext's regression now, and dezogif_ng stays untouched.
# Unchanged apart from this header: conformance.py imports it (C24's error-area
# reader, which jnext never reaches: jnext honours ADD_BREAKPOINT).
# ---------------------------------------------------------------------------
"""Read the stub's OWN SCREEN back over DZRP, and say what is on it.

    from screen import read_screen, Screen
    scr = read_screen(conv)                 # conv is a live dzrp.Dzrp
    scr.error_pixels()                      # 0 when the error area is clean
    scr.row_text(8)                         # "Session opened - CMD_INIT"

WHY THIS EXISTS.

`doc/HARDWARE-TESTING.md` has carried, since the first hardware session, a list
of things only a human at the machine can see — and the first item on it is the
Next's own screen, with the note that `CMD_READ_MEM 0x4000,6912` would fetch it
and "nobody has written that". This is that.

It was written because an observation was LOST. On 2026-08-06 probe A was run
against a real Next, found the ceiling, and nobody watched the screen; asked
afterwards, the user reported no red errors. **That answer cannot carry the
weight**, and the reason is mechanical rather than a doubt about the reporter:
see NEVER SEND CMD_INIT below.

===========================================================================
NEVER SEND CMD_INIT FROM A READER. IT DESTROYS THE THING BEING READ.
===========================================================================

`commands.asm` (cmd_init) does `xor a` / `ld (last_error),a` — **every CMD_INIT
clears the error**. It then calls `show_ui`, which repaints, so the error area
is blank on the very next frame. A reader that opened a connection and
introduced itself with CMD_INIT, the way every other client here does, would
wipe the evidence and then report the blank screen it had just created.

That is not hypothetical: probe A's own reclaim phase opens fresh connections
after the ceiling, each sending CMD_INIT, and either would have erased an error
raised while the walk was running.

**A bare CMD_READ_MEM on a fresh connection is answered.** `cmd_loop`
(message.asm) reads a header and dispatches through `cmd_call` with no session
gate of any kind — there is no state in which a command is refused for not
having been preceded by CMD_INIT. Measured, not read: see MEASURED below.

===========================================================================
THE SCREEN IS AT 0x4000 BEFORE ANY CMD_INIT, AND THAT IS NOT OBVIOUS
===========================================================================

`cmd_init` writes `REG_MMU+2,10` and `REG_MMU+3,11`, which is what
HARDWARE-TESTING.md cites for the screen being reachable — so the honest worry
is that the mapping is a CMD_INIT side effect and a reader that skips it sees
somebody else's memory. It is not. Those writes RESTORE the ZX128 default that
is already in place: no code the stub runs by itself touches slots 2 or 3
(`commands.asm:137-138` is the only such `nextreg` outside a command handler,
and the NMI entry path touches slots 0, 1 and 7 only — `main.asm:68-69`,
`mf_rom.asm`), and the stub's own `show_ui` writes to `SCREEN` = 0x4000
(`zx/zx.inc:14`) through that same mapping — which is why the UI is visible at
all on the first M1 press with no client attached (bench T6).

**A CLIENT CAN STILL MOVE IT, and that bound is stated rather than glossed.**
`cmd_set_slot` (`commands.asm`, CMD_SET_SLOT) writes whichever slot it is
given, slots 2 and 3 included, so a DZRP session that has remapped 0x4000 will
make this reader fetch whatever it mapped there. Nothing that uses this reader
does that — the probes and the hardware bench never send command 10, and any
`CMD_INIT` puts the default back — but a reader pointed at a live DeZog session
is outside what is checked here. `validate_reader()` is the backstop: row 12
would not say `R = Reset`.

MEASURED against jnext, 2026-08-06, WiFi ROM build 000F, one connection that
sent no CMD_INIT ever:

  * CMD_READ_MEM 0x4000,6912 answered in 256 ms with 6912 bytes.
  * the 768 attribute bytes were EXACTLY {7: 480, 66: 288} — 15 rows of
    WHITE+(BLACK<<3) then 9 rows of RED+BRIGHT, which is `show_ui`'s own
    MEMFILL (ui.asm:156-158) and nothing else's.
  * a CMD_INIT was then sent and the screen re-read: **the attributes were
    byte-identical and the only display row that changed was row 8**, the
    session line CMD_INIT is supposed to change. So CMD_INIT altered the
    CONTENT of one line and did not alter the MAPPING at all.

===========================================================================
THE FONT COMES OFF THE WIRE TOO
===========================================================================

Decoding a row needs the ZX character set. `test/screen-text.py` takes it from
the 48.rom on the SD image the emulator boots — correct there, and unavailable
for a Next on the other side of a WiFi link.

It does not have to be. While the debugger is stopped, slot 0 holds `ROM_BANK`
(`main.asm:68-69`), so **`CMD_READ_MEM 0x3D00,768` fetches the font out of the
machine under test's own ROM** — the same bytes `text.asm`'s `font_address`
prints with. Verified against jnext: the 768 bytes off the wire are
byte-identical to `/machines/next/48.rom` at 0x3D00 on the image it booted.

So `read_font` is preferred and `load_font_file` is the fallback. A wrong font
is self-announcing rather than silently wrong — every glyph decodes to '?', not
to a plausible other letter — which is why the fallback is safe to offer.

===========================================================================
ERROR DETECTION IS AN ATTRIBUTE TEST, NOT OCR
===========================================================================

`error_pixels()` counts pixels that RENDER AS BRIGHT RED, which is deliberately
the same predicate `run-probes-jnext.sh`, W2, P2 and N3 apply to a PNG: jnext
renders a non-bright component as 182 and bright as 255, `ui.asm` colours the
bottom nine rows RED+BRIGHT and nothing else on that screen is red, and
`out (BORDER),a` carries no bright bit. So the two measures are the SAME
NUMBER computed from two independent views, which is what makes them worth
comparing (see `test/screen-agreement.py`).

It is a count of pixels rather than a boolean because that is what the existing
benches record, so a reading here is comparable with numbers already in
MEMORY.md — **once the scale is accounted for**. Those figures (824, 848, 1044)
are PHYSICAL pixels in a PNG jnext rendered at an integer scale, normally 2, so
`824 bright-red pixels of TX Timeout` is **206** here. `test/screen-agreement.py`
checks that relationship rather than leaving it to be rediscovered.

`error_text()` is on top of the count and says WHICH error, which no pixel
count can — and the next paragraph is why that matters more than it looks.

===========================================================================
A CLIENT DISCONNECT CAN PAINT `RX Timeout`. MEASURED — AND SO WAS ITS LIMIT.
===========================================================================

Against jnext, build 000F: a first-ever connection sees a clean error area; a
second client then sends CMD_INIT and closes CLEANLY, and **one second later
the error area reads `Last Error: / RX Timeout` — 212 pixels — and stays that
way** (checked at 1, 3, 6 and 10 s, and again from a third connection).

That is the WiFi build's known behaviour, not a new defect: parked in
`cmd_loop`'s wait, the `<id>,CLOSED` the module emits when a peer hangs up ends
the wait and then fails to parse as a `+IPD` header, reaching `drain_main`
(MEMORY.md, issue #16, which describes exactly this and leaves it to M3).

**IT IS NOT UNCONDITIONAL, AND A FIRST DRAFT OF THIS SAID IT WAS.** Probe A's
own A5 and A6 read a CLEAN area in the same emulator, on a run whose walk had
closed a refused connection and whose reclaim phase had opened and closed two
more. So whether the disconnect lands as an error depends on what arrives next
and when — the sweep above left the stub idle for a second with nothing else
coming, and the probe does not. The honest statement is the measurement:
**a disconnect followed by silence produces it; a disconnect followed promptly
by another connection was observed not to.** Which of the two a given run gets
is not established here, and no caller should depend on either.

**THE CONSEQUENCE FOR CALLERS is that a clean error area is good news and
`RX Timeout` on its own is not necessarily bad news** — so the useful question
is whether the text is something OTHER than that, which is `only_error_is()`
below, and which is answerable only because the area is decoded rather than
counted.

`RX Timeout` is NOT always benign, and callers must not treat it as noise: a
failed AT chain at bring-up reports the same string, because bring-up failure
has no error code of its own (MEMORY.md 2026-08-05). It is benign *in the
context of* a run that has just hung up on somebody. Context the reader does
not have and the caller does.
"""

import dzrp

SCREEN_ADDR = 0x4000
DISPLAY_LEN = 6144               # 0x4000-0x57FF, the bitmap
ATTR_LEN = 768                   # 0x5800-0x5AFF, one byte per character cell
SCREEN_LEN = DISPLAY_LEN + ATTR_LEN

FONT_ADDR = 0x3D00               # in the machine's own ROM, slot 0 while stopped
FONT_LEN = 768                   # 96 glyphs, ' '(32)..127, eight bytes each
FONT_FIRST = 32

COLS, ROWS = 32, 24

# ui.asm's own layout. The error area is the bottom nine rows, filled
# RED+BRIGHT by show_ui; everything above is WHITE on BLACK.
ERROR_FIRST_ROW = 15
SESSION_ROW = 8                  # issue #14's session line, WiFi builds only
VALIDATION_ROW = 12              # "R = Reset" — see validate_reader()
VALIDATION_TEXT = "R = Reset"

# What a client hanging up leaves behind. See the docstring: this is measured
# behaviour of the WiFi build, so any caller that has closed a connection must
# expect it. It is the same string a failed bring-up shows, which is why this
# is a name to compare against and NOT a list of errors to ignore.
DISCONNECT_ERROR = "RX Timeout"

# jnext's component values, so a render can be compared with its screenshot.
DIM, FULL = 182, 255


def read_mem(conv, addr, size):
    """CMD_READ_MEM. Payload is <n:1><addr:2><size:2>, little-endian."""
    payload = bytes([0, addr & 0xFF, (addr >> 8) & 0xFF,
                     size & 0xFF, (size >> 8) & 0xFF])
    body = conv.command(dzrp.CMD_READ_MEM, payload)
    if len(body) != size:
        raise ValueError("CMD_READ_MEM %#06x,%d returned %d bytes"
                         % (addr, size, len(body)))
    return body


def read_screen(conv, glyphs=None):
    """The stub's screen, over a conversation the caller already holds.

    NO CMD_INIT IS SENT, here or anywhere in this module. See the docstring.
    The caller passes a live conversation on purpose: opening one costs a slot
    on the module, and this reader must not perturb a measurement of how many
    slots there are.
    """
    return Screen(read_mem(conv, SCREEN_ADDR, SCREEN_LEN), glyphs=glyphs)


def read_font(conv):
    """The ZX character set out of the machine under test's own ROM."""
    return glyph_table(read_mem(conv, FONT_ADDR, FONT_LEN))


def glyph_table(font_bytes):
    """{eight-byte bitmap: character}, for reversing a cell into a letter."""
    if len(font_bytes) < FONT_LEN:
        raise ValueError("a font needs %d bytes, got %d" % (FONT_LEN, len(font_bytes)))
    glyphs = {}
    for i in range(FONT_LEN // 8):
        # setdefault: several ROM glyphs share a bitmap (space and others are
        # blank), and the FIRST wins so a blank cell reads as ' '.
        glyphs.setdefault(bytes(font_bytes[i * 8:i * 8 + 8]), chr(FONT_FIRST + i))
    return glyphs


def load_font_file(path, offset=FONT_ADDR):
    """Fallback: the font from a local ROM image.

    Only for a remote that cannot be asked — read_font is preferred everywhere,
    because it takes the font from the machine whose screen is being read
    rather than from a file that is merely believed to match it.
    """
    data = open(path, "rb").read()
    if len(data) < offset + FONT_LEN:
        raise ValueError("%s: too short for a font at %#06x" % (path, offset))
    return glyph_table(data[offset:offset + FONT_LEN])


def rgb(colour, bright):
    """A ZX colour as jnext renders it. Bits are BRG: 0=blue, 1=red, 2=green."""
    level = FULL if bright else DIM
    return ((level if colour & 2 else 0),       # red
            (level if colour & 4 else 0),       # green
            (level if colour & 1 else 0))       # blue


BRIGHT_RED = rgb(2, True)                        # (255, 0, 0)


class Screen:
    """6912 bytes of display file and attributes, read back as observations."""

    def __init__(self, raw, glyphs=None):
        if len(raw) != SCREEN_LEN:
            raise ValueError("a screen is %d bytes, got %d" % (SCREEN_LEN, len(raw)))
        self.raw = bytes(raw)
        self.display = self.raw[:DISPLAY_LEN]
        self.attrs = self.raw[DISPLAY_LEN:]
        self.glyphs = glyphs or {}

    # --- geometry ---------------------------------------------------------

    def attr(self, row, col):
        return self.attrs[row * COLS + col]

    def cell(self, row, col):
        """The eight bitmap bytes of one character cell.

        The display file's thirds-then-rows-then-scanlines order: a cell's top
        line is at (row//8)*2048 + (row%8)*32 + col and each line below it is
        256 bytes further on.
        """
        base = (row // 8) * 2048 + (row % 8) * 32 + col
        return bytes(self.display[base + line * 256] for line in range(8))

    # --- the error area ---------------------------------------------------

    def error_pixels(self):
        """Pixels rendering as bright red — the same measure the PNG benches take.

        Counted over the whole screen rather than over rows 15-23, so it stays
        equal to a whole-image PNG count even if an error is ever drawn
        elsewhere. Both ink and paper are considered: a bright red PAPER would
        put bright red pixels on the screen just as ink does, and a count that
        looked only at ink would disagree with the picture.
        """
        total = 0
        for row in range(ROWS):
            for col in range(COLS):
                a = self.attr(row, col)
                ink_red = rgb(a & 7, a & 0x40) == BRIGHT_RED
                paper_red = rgb((a >> 3) & 7, a & 0x40) == BRIGHT_RED
                if not ink_red and not paper_red:
                    continue
                set_bits = sum(bin(b).count("1") for b in self.cell(row, col))
                if ink_red:
                    total += set_bits
                if paper_red:
                    total += 64 - set_bits
        return total

    def flashing(self):
        """Cells with the FLASH bit set.

        Reported rather than ignored: the ULA swaps ink and paper on those every
        16 frames, so a render of them is only true for half the time and a
        comparison against a screenshot would be a coin toss. `show_ui` sets it
        nowhere, so this is normally 0 — and if it is ever not, the comparison
        must say so instead of failing mysteriously.
        """
        return sum(1 for a in self.attrs if a & 0x80)

    # --- text -------------------------------------------------------------

    def row_text(self, row):
        """One character row as text, '?' for any cell not in the font."""
        return "".join(self.glyphs.get(self.cell(row, col), "?")
                       for col in range(COLS))

    def error_text(self):
        """The non-blank lines of the error area, top to bottom."""
        out = []
        for row in range(ERROR_FIRST_ROW, ROWS):
            text = self.row_text(row).rstrip()
            if text:
                out.append(text)
        return out

    def validate_reader(self):
        """Is this reader reading? (ok, what_row_12_said)

        Row 12 is `R = Reset`, drawn by the same code in the same font as
        everything else here and not in question. Checked BEFORE any verdict is
        taken from this screen, so a wrong font, wrong geometry or a truncated
        read reports itself as a broken instrument instead of as a clean error
        area — which is the failure mode that matters, since 'no error' is what
        a reader that cannot read anything says too.

        Same guard, and the same row, as run-client-status.sh applies to its
        screenshots.
        """
        got = self.row_text(VALIDATION_ROW).strip()
        return got == VALIDATION_TEXT, got

    # --- the whole picture ------------------------------------------------

    def render_rgb(self):
        """256x192 RGB bytes, as jnext would draw this screen.

        Exists for one purpose: to be compared against jnext's own screenshot,
        which is the only way to check this reader against something that did
        not come through it. See test/screen-agreement.py.
        """
        out = bytearray(256 * 192 * 3)
        for row in range(ROWS):
            for col in range(COLS):
                a = self.attr(row, col)
                ink = rgb(a & 7, a & 0x40)
                paper = rgb((a >> 3) & 7, a & 0x40)
                bits = self.cell(row, col)
                for line in range(8):
                    y = row * 8 + line
                    byte = bits[line]
                    for bit in range(8):
                        px = ink if byte & (0x80 >> bit) else paper
                        at = (y * 256 + col * 8 + bit) * 3
                        out[at:at + 3] = bytes(px)
        return bytes(out)

    # --- one line for a probe's output ------------------------------------

    def observation(self):
        """What a probe prints. One clause, in the house style."""
        ok, _ = self.validate_reader()
        if not ok:
            # "a broken reader, not a clean screen" is the load-bearing half: a
            # reader that cannot read anything otherwise reports something
            # indistinguishable from "no error".
            #
            # WHAT ROW 12 ACTUALLY SAID IS DELIBERATELY NOT INTERPOLATED HERE.
            # It is up to 32 characters of arbitrary text, so a line carrying it
            # has no bounded length — a first version measured 19 words against
            # a one-token placeholder and 21 against the real thing. This line
            # is the same length every time; the diagnostic belongs in
            # `validate_reader()`, which screen-client.py prints in full.
            return ("the reader failed its row-%d check — a broken reader, "
                    "not a clean screen" % VALIDATION_ROW)
        red = self.error_pixels()
        if not red:
            return "the error area is CLEAN — 0 bright-red pixels on the stub's own screen"
        return "the error area holds %d bright-red pixels: %s" % (
            red, " / ".join(self.error_text()) or "(no readable text)")

    def only_error_is(self, text=DISCONNECT_ERROR):
        """Is `text` the only thing in the error area besides the `Last Error:`
        heading?

        For callers that have just disconnected a client and so EXPECT
        `RX Timeout` — see the module docstring. It answers "is there anything
        here I did not cause", which is the question a probe actually has, and
        it can only be asked because the area is read as text.
        """
        lines = [ln for ln in self.error_text() if not ln.startswith("Last Error:")]
        return bool(lines) and all(ln.strip() == text for ln in lines)

    def status_block(self):
        """Rows 6-8: the transport's status block and the session line.

        What doc/HARDWARE-TESTING.md's S4 asks for. Composed at run time from
        AT+CIFSR, so it is the one part of this screen a build cannot predict.
        """
        return [self.row_text(r).rstrip() for r in (6, 7, SESSION_ROW)]


def observe(conv, disconnected=True):
    """Read the screen over an EXISTING conversation and describe it in one line.

    For probes. It never raises: a screen read that fails must degrade to a
    stated absence, because it is an observation bolted onto a measurement that
    has already been made and must not be able to destroy it. The broad except
    is deliberate and is the whole point of the function.

    The caller passes a conversation it already holds, so this costs NO
    additional connection — see the module docstring and probe A's walk().

    `disconnected` says whether the caller has already hung up on somebody. It
    defaults to True because every caller here has: both issue #15 probes and
    the hardware bench open and close connections as their whole method. When
    it is set, an error area holding nothing but `RX Timeout` is reported as
    what this run itself produced, rather than as a finding about the module —
    the distinction the module docstring measures and which a pixel count on
    its own cannot make.
    """
    try:
        scr = read_screen(conv, glyphs=read_font(conv))
    except Exception as e:                      # noqa: BLE001 — see above
        return ("the screen could not be read (%s: %s) — a fact about this read"
                % (type(e).__name__, e))

    if disconnected and scr.only_error_is():
        return ("the error area holds only %r (%d px), which is what this run's "
                "own disconnects leave behind" % (DISCONNECT_ERROR, scr.error_pixels()))
    return scr.observation()
