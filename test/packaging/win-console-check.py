#!/usr/bin/env python3
"""Run a Windows jnext.exe from a REAL Win32 console under wine and echo what
the console received (GH #212).

jnext.exe is a GUI-subsystem binary, so a console launch gives it no stdio at
all; main() calls win_attach_parent_console() to get it back. That path is only
exercised from a genuine Win32 console -- piping `wine jnext.exe --help` from a
Unix shell does NOT exercise it, because wine then hands the child the shell's
own fds and AttachConsole is never reached.

A Win32 console needs a tty, so this drives `wine cmd` on a pty (stdlib only --
no expect/socat) and writes everything the console received to stdout, ANSI and
all. The caller does the asserting.

Getting into and out of that session is fiddly in two specific ways, both of
them measured rather than guessed:

  * Waiting for cmd's prompt is how we know the session is up -- a cold wine
    prefix spends a minute in wineboot first, and typing into a console that is
    not listening yet mangles the command line.
  * The prompt is NOT how we know the command finished. cmd does not wait for a
    GUI-subsystem child, so it prompts again immediately while jnext is still
    loading its Qt DLLs. Completion is silence.

usage: win-console-check.py <exe-basename> [args...]
       cwd must be the directory holding the exe and its bundled DLLs.
exit:  0 command ran (console output on stdout), 1 could not drive the console.
"""

import os
import pty
import select
import sys
import time

# cmd's default prompt is the working directory, which the console wraps across
# lines and which shares characters with ordinary output — useless as a marker.
# Set an unmistakable one instead and synchronise on that. `$G` is cmd's escape
# for '>', so the echo of the `prompt` command itself does not contain the
# marker and cannot be miscounted as a prompt.
MARKER = b"JNEXTRDY>"
PROMPT_CMD = "prompt JNEXTRDY$G"
BOOT_TIMEOUT_S = 180.0   # cold prefix: wineboot runs before cmd's first prompt
SETTLE_S = 3.0           # cmd prints its prompt before its line reader listens
PROMPT_TRIES = 4
PROMPT_TIMEOUT_S = 20.0
RUN_TIMEOUT_S = 120.0    # ceiling for the command under test
OUTPUT_QUIET_S = 12.0    # a GUI child prints nothing while its DLLs load
EXIT_DRAIN_S = 5.0


class Console:
    """A `wine cmd` session on a pty."""

    def __init__(self) -> None:
        self.captured = bytearray()
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execvp("wine", ["wine", "cmd"])
            os._exit(127)

    def read_some(self, timeout: float) -> bool:
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        try:
            chunk = os.read(self.fd, 65536)
        except OSError:
            return False
        if not chunk:
            return False
        self.captured.extend(chunk)
        return True

    def wait_for(self, needle: bytes, count: int, timeout: float) -> bool:
        """Read until `needle` has been seen `count` times, or time out."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.captured.count(needle) >= count:
                return True
            self.read_some(0.2)
        return self.captured.count(needle) >= count

    def drain_quiet(self, quiet_s: float, max_s: float) -> None:
        """Read until the console has been silent for `quiet_s`."""
        deadline = time.monotonic() + max_s
        last = time.monotonic()
        while time.monotonic() < deadline:
            if self.read_some(0.2):
                last = time.monotonic()
            elif time.monotonic() - last > quiet_s:
                return

    def send(self, line: str) -> None:
        os.write(self.fd, (line + "\r").encode())

    def close(self) -> None:
        try:
            os.close(self.fd)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except OSError:
            pass


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: win-console-check.py <exe-basename> [args...]", file=sys.stderr)
        return 2

    ran = False
    con = Console()
    try:
        # cmd is ready once it has printed its first prompt; on a cold prefix
        # that is on the far side of a wineboot.
        if not con.wait_for(b">", 1, BOOT_TIMEOUT_S):
            sys.stdout.write(con.captured.decode("utf-8", "replace"))
            print("\nwin-console-check: wine cmd never printed a prompt", file=sys.stderr)
            return 1
        # cmd prints its prompt slightly before its line reader is listening, so
        # the very first keystrokes can be dropped. Settle, then retry until the
        # marker prompt actually takes -- which doubles as the proof that the
        # session is accepting input at all.
        time.sleep(SETTLE_S)
        for _ in range(PROMPT_TRIES):
            con.send(PROMPT_CMD)
            if con.wait_for(MARKER, 1, PROMPT_TIMEOUT_S):
                break
        else:
            sys.stdout.write(con.captured.decode("utf-8", "replace"))
            print("\nwin-console-check: prompt marker never appeared", file=sys.stderr)
            return 1
        # A retry whose keystrokes were merely LATE, not lost, still runs and
        # prints another prompt. Let every such straggler land before baselining
        # the prompt count, or the command under test is declared finished by a
        # prompt that predates it -- and `exit` gets typed while it is still
        # starting up.
        con.drain_quiet(SETTLE_S, RUN_TIMEOUT_S)
        before = con.captured.count(MARKER)

        con.send(" ".join(sys.argv[1:]))
        # The returning prompt means cmd ACCEPTED the line, and nothing more:
        # cmd does not wait for a GUI-subsystem child, so it prompts again
        # immediately while jnext is still loading its Qt DLLs. If the prompt
        # never returns the session swallowed the line -- a harness fault, which
        # must NOT be reported as "the program printed nothing".
        ran = con.wait_for(MARKER, before + 1, RUN_TIMEOUT_S)
        # Actual completion is therefore silence, not a prompt.
        con.drain_quiet(OUTPUT_QUIET_S, RUN_TIMEOUT_S)

        con.send("exit")
        deadline = time.monotonic() + EXIT_DRAIN_S
        while time.monotonic() < deadline and con.read_some(0.2):
            pass
    finally:
        con.close()

    sys.stdout.write(con.captured.decode("utf-8", "replace"))
    if not ran:
        print("\nwin-console-check: command never completed in the console",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
