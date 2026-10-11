#!/usr/bin/env python3
"""Name where a NextSync reply went wrong, from the byte digests both sides log (GH #319).

    nextsync-digest.py <peer.log> <jnext.log>            the failure dump: a verdict
    nextsync-digest.py --check <peer.log> <jnext.log>    a GREEN run: every hop's digests must agree
    nextsync-digest.py --selftest                        the FNV-1a/32 test vector

The peer and jnext both log, for the bytes they handle, `digest <stream> off=<n> fnv=<h>`
(a running FNV-1a/32 at every 256-byte checkpoint; src/esp01/include/esp01/stream_digest.h
and nextsync-peer.py hold the twin implementations) and `digest-exact <stream> off=<n>
fnv=<h> [queued=<q>]` at moments that matter: the end of each frame, when the ESP's
output queue drains, and when the guest starts a command (`esp-out-at-cmd` also says how
many bytes the engine still held for the guest, `queued`).

THE HOPS, in the order the bytes travel (checkpoints compared where both sides have one):
  1 network     peer-sent       -> sock-in          (the socket)
  2 ESP buffer  sock-in         -> ipd-framed       (the engine's receive buffer)
  3 ESP -> UART esp-out         -> uartN-fifo-in    (the adapter, the RX FIFO, overflow)
  4 UART -> CPU uartN-fifo-in   -> uartN-guest-read (what the guest's IN returned)

THE CHECKPOINTS HAVE A BLIND SPOT (the last < 256 bytes of a stream), which is why the
verdict is built from the EXACT records taken when the guest sent the command that
followed (the Restart/Retry): how many bytes each stage had passed on by then, and how
many the ESP output queue still held. Nothing here assumes a cause; it states which
stage's count is short.
"""
import re
import sys

DIG = re.compile(r"digest (\S+) off=(\d+) fnv=([0-9a-f]{8})")
EXACT = re.compile(r"digest-exact (\S+) off=(\d+) fnv=([0-9a-f]{8})(?: queued=(\d+))?")
TS = re.compile(r"\[(\d\d):(\d\d):(\d\d)\.(\d\d\d)\]")
PEER_TS = re.compile(r"^(?:ev|digest\S*)? ?(\d\d):(\d\d):(\d\d)\.(\d\d\d)")
HOPS = [("1 network (peer-sent -> sock-in)", "peer-sent", "sock-in"),
        ("2 ESP receive buffer (sock-in -> ipd-framed)", "sock-in", "ipd-framed"),
        ("3 ESP output -> UART RX FIFO (esp-out -> uart-fifo-in)", "esp-out", "uart-fifo-in"),
        ("4 UART RX FIFO -> guest read (uart-fifo-in -> uart-guest-read)", "uart-fifo-in", "uart-guest-read")]


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return "%08x" % h


def ms_of(m):
    h, mi, se, mm = (int(x) for x in m.groups())
    return ((h * 60 + mi) * 60 + se) * 1000 + mm


def fmt(ms):
    return "%02d:%02d:%02d.%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def load(path):
    """checkpoints {stream: {off: fnv}}, exact records [(ms, stream, off, fnv, queued)], other lines [(ms, text)]."""
    cps, exact, lines = {}, [], []
    with open(path, errors="replace") as f:
        for line in f:
            m = EXACT.search(line)
            ts = TS.search(line) or PEER_TS.match(line)
            t = ms_of(ts) if ts else None
            if m:
                exact.append((t, m.group(1), int(m.group(2)), m.group(3), None if m.group(4) is None else int(m.group(4))))
                continue
            m = DIG.search(line)
            if m:
                cps.setdefault(m.group(1), {})[int(m.group(2))] = m.group(3)
                continue
            lines.append((t, line.rstrip()))
    return cps, exact, lines


def normalise(streams):
    """uartN-fifo-in / uartN-guest-read of the busiest channel become uart-fifo-in / uart-guest-read."""
    out = dict(streams)
    for suffix in ("-fifo-in", "-guest-read"):
        cands = [n for n in streams if n.startswith("uart") and n.endswith(suffix)]
        if cands:
            out["uart" + suffix] = streams[max(cands, key=lambda n: len(streams[n]))]
    return out


def hop_table(cps):
    rows, first_bad = [], None
    for name, a, b in HOPS:
        sa, sb = cps.get(a, {}), cps.get(b, {})
        common = sorted(set(sa) & set(sb))
        diff = [o for o in common if sa[o] != sb[o]]
        rows.append((name, a, b, len(sa), len(sb), len(common), diff[0] if diff else None, sa, sb))
        if diff and first_bad is None:
            first_bad = (name, diff[0])
    return rows, first_bad


def check(peer_path, jnext_path):
    pc, _, _ = load(peer_path)
    jc, _, _ = load(jnext_path)
    cps = normalise({**jc, **pc})
    rows, _ = hop_table(cps)
    bad = []
    for name, a, b, na, nb, ncommon, diff, _, _ in rows:
        if ncommon == 0:
            bad.append("hop %s: no common checkpoint (%s has %d, %s has %d): a digest hook is missing or moved"
                       % (name, a, na, b, nb))
        elif diff is not None:
            bad.append("hop %s: the digests DIFFER at offset %d: a hop changed a byte on a run that otherwise passed"
                       % (name, diff))
    return bad


def verdict(peer_path, jnext_path):
    pc, pex, plines = load(peer_path)
    jc, jex, jlines = load(jnext_path)
    cps = normalise({**jc, **pc})
    out = []
    rows, first_bad = hop_table(cps)
    for name, a, b, na, nb, ncommon, diff, sa, sb in rows:
        out.append("  hop %s: %d checkpoints / %d, %d common, %s" % (
            name, na, nb, ncommon,
            ("FIRST DIFFERENCE at stream offset %d (bytes %d..%d): %s=%s %s=%s" % (
                diff, diff - 256, diff - 1, a, sa[diff], b, sb[diff])) if diff is not None else "all equal"))
    # the peer's first Retry/Restart, and the reply before it
    t_r, t_reply, reply, prev = None, None, None, None
    with open(peer_path, errors="replace") as f:
        for line in f:
            m = re.search(r"TX seq=(\d+) len=(\d+).*frame=(\d+) sent_through=(\d+)", line)
            if m:
                ts = PEER_TS.match(line)
                prev = (int(m.group(1)), int(m.group(3)), int(m.group(4)), ms_of(ts) if ts else None)
            # the first anomaly: a Retry/Restart, or bytes that are not a NextSync command at all
            if re.search(r"RX b'(Retry|Restart)'", line) or re.search(r"RX b'(?!(?:Sync3|Next|Neex|Get|Gee|Bye)')", line):
                ts = PEER_TS.match(line)
                t_r = ms_of(ts) if ts else None
                reply = prev
                t_reply = prev[3] if prev else None
                break
    if reply:
        out.append("  the reply sent last before the first Retry/Restart: seq %d, %d bytes, peer stream bytes %d..%d, sent at %s"
                   % (reply[0], reply[1], reply[2] - reply[1], reply[2] - 1, fmt(reply[3]) if reply[3] is not None else "?"))
    if first_bad:
        out.append("  VERDICT: bytes were CHANGED: the first hop whose digests differ is %s, at checkpoint offset %d."
                   % first_bad)
        return "\n".join(out)
    if t_r is None:
        out.append("  VERDICT: no hop differs at any common checkpoint, and the peer saw no Retry/Restart.")
        return "\n".join(out)
    # what each stage had passed on when the guest started the command that the peer received as the Restart
    at_cmd = [e for e in jex if e[1] == "esp-out-at-cmd" and e[0] is not None and e[0] <= t_r]
    if not at_cmd:
        out.append("  VERDICT: no hop differs at any common checkpoint; no `esp-out-at-cmd` record before the Restart "
                   "(the exact-offset hooks are missing).")
        return "\n".join(out)
    t_c, _, e_off, _, queued = at_cmd[-1]
    # Any guest command started between the last reply and the Restart that found bytes still queued in the ESP.
    stuck = [e for e in at_cmd if t_reply is not None and e[0] >= t_reply and e[4]]
    if stuck:
        first = stuck[0]
        drained = [e for e in jex if e[1] == "esp-out-drained" and e[0] is not None and e[0] >= first[0]]
        out.append("  VERDICT: no byte was changed, but the ESP output queue still held %d bytes when the guest started a command at %s "
                   "(%d ms after the peer's last reply)%s. The reply was NOT fully delivered: the output side did not finish releasing it "
                   "before the guest gave up." % (
                       first[4], fmt(first[0]), first[0] - t_reply,
                       ("; the queue drained at %s, %d ms later" % (fmt(drained[0][0]), drained[0][0] - first[0])) if drained else ""))
        lock_lines = [text for t, text in jlines if re.search(r"held the core lock|ran into the worker's lock", text)]
        out.append("  ESP worker/lock evidence in jnext's log (debug lines when the worker held the core lock >= 20 ms, or "
                   "tick() lost >= 20 ms to it): %s" % (("%d line(s), first: %s" % (len(lock_lines), lock_lines[0])) if lock_lines else "none"))
        return "\n".join(out)

    def last_exact(stream, before):
        c = [e for e in jex + pex if e[1] == stream and e[0] is not None and e[0] <= before]
        return c[-1] if c else None
    uart_in = [s for s in {e[1] for e in jex} if s.startswith("uart") and s.endswith("-fifo-in")]
    uart_rd = [s for s in {e[1] for e in jex} if s.startswith("uart") and s.endswith("-guest-read")]
    peer_sent = last_exact("peer-sent", t_c)
    sock_in = last_exact("sock-in", t_c)
    framed = last_exact("ipd-framed", t_c)
    fifo_in = last_exact(uart_in[0], t_c) if uart_in else None
    guest_rd = last_exact(uart_rd[0], t_c) if uart_rd else None
    out.append("  at %s the guest started the command the peer received as its first Retry/Restart. Exact counts then:" % fmt(t_c))
    out.append("    peer sent      %s bytes    jnext received (sock-in) %s    framed into +IPD (ipd-framed) %s"
               % (peer_sent[2] if peer_sent else "?", sock_in[2] if sock_in else "?", framed[2] if framed else "?"))
    out.append("    ESP released   %d bytes (esp-out), still queued in the ESP: %s" % (e_off, queued))
    out.append("    UART FIFO took %s    the guest had read %s" % (fifo_in[2] if fifo_in else "?", guest_rd[2] if guest_rd else "?"))
    why = None
    if peer_sent and sock_in and sock_in[2] < peer_sent[2]:
        why = "jnext had received only %d of the %d bytes the peer had sent (the network/receive side)" % (sock_in[2], peer_sent[2])
    elif sock_in and framed and framed[2] < sock_in[2]:
        why = "%d received bytes had not been framed into +IPD (the ESP receive buffer)" % (sock_in[2] - framed[2])
    elif queued:
        why = "the ESP output queue still held %d bytes for the guest when the guest started the command" % queued
    elif fifo_in and fifo_in[2] < e_off:
        why = "the ESP had released %d bytes the UART RX FIFO had not taken" % (e_off - fifo_in[2])
    elif guest_rd and fifo_in and guest_rd[2] < fifo_in[2]:
        why = "%d bytes sat in the UART RX FIFO unread by the guest" % (fifo_in[2] - guest_rd[2])
    if why:
        out.append("  VERDICT: no byte was changed. The stage that was short: %s." % why)
    else:
        out.append("  VERDICT: no byte was changed and every stage had passed on everything (ESP queue empty, FIFO fully "
                   "read): the guest HAD all the bytes and still sent its Restart. The cause is in the guest's handling "
                   "or its timing, not in the transport.")
    lock_lines = [text for t, text in jlines if re.search(r"held the core lock|ran into the worker's lock", text)]
    out.append("  ESP worker/lock evidence in jnext's log (debug lines when the worker held the core lock >= 20 ms, or "
               "tick() lost >= 20 ms to it): %s" % (("%d line(s), first: %s" % (len(lock_lines), lock_lines[0])) if lock_lines else "none"))
    return "\n".join(out)


def selftest():
    # The FNV-1a/32 test vectors (isthe.com/chongo/tech/comp/fnv): "" = 811c9dc5, "a" = e40c292c, "foobar" = bf9cf968.
    ok = (fnv(b"") == "811c9dc5" and fnv(b"a") == "e40c292c" and fnv(b"foobar") == "bf9cf968")
    print("selftest: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        return selftest()
    if len(argv) == 4 and argv[1] == "--check":
        bad = check(argv[2], argv[3])
        for b in bad:
            print("  " + b)
        return 1 if bad else 0
    print(verdict(argv[1], argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
