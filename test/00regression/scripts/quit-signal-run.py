#!/usr/bin/env python3
"""Launcher for quit-signal-func / quit-signal-rzx-func (GH #364).

usage: quit-signal-run.py PIDFILE RCFILE READYFILE DZRP_PORT TOOLS_DIR -- JNEXT ARGV...

Starts jnext as a CHILD (SIGINT restored to SIG_DFL: a row's `&` job inherits
it ignored, and jnext correctly keeps that), writes its PID to PIDFILE so the
row signals jnext itself, then writes Popen.returncode to RCFILE once it ends.
A negative returncode (-15, -2) means jnext was terminated BY that signal
(WIFSIGNALED / WTERMSIG); bash reports both that and exit(143) as 143, so the
row reads this instead.

With DZRP_PORT != 0 it also attaches a DZRP client, pauses the machine, checks
the registers stand still, and only then writes READYFILE: the signal then
arrives while a remote client holds the machine (headless paused-with-a-remote
path). TOOLS_DIR is tools/cspect_dzrp.
"""
import os, signal, subprocess, sys, time


def atomic_write(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.rename(tmp, path)


def main():
    pidfile, rcfile, readyfile, port, tools = sys.argv[1:6]
    argv = sys.argv[7:]
    p = subprocess.Popen(argv, preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_DFL))
    atomic_write(pidfile, str(p.pid))
    if int(port):
        sys.path.insert(0, tools)
        import cspect_dzrp as dz
        deadline = time.monotonic() + 30
        c = None
        while c is None and p.poll() is None and time.monotonic() < deadline:
            try:
                c = dz.CSpectDZRP("127.0.0.1", int(port), timeout=10)
                c.connect()
            except OSError:
                c = None
                time.sleep(0.05)
        if c is not None:
            c.init(name="quit-signal")
            c.pause()
            r1 = c.get_registers()
            time.sleep(0.3)
            r2 = c.get_registers()
            if (r1.PC, r1.R) == (r2.PC, r2.R):
                atomic_write(readyfile, "paused")
    atomic_write(rcfile, str(p.wait()))


main()
