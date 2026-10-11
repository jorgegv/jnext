#!/usr/bin/env python3
"""Name the first hop that damaged a NextSync reply (GH #319).

    nextsync-digest.py <peer.log> <jnext.log>

The peer and jnext both log `digest <stream> off=<n> fnv=<h>` lines: a running
FNV-1a/32 of a byte stream at every 256-byte checkpoint (src/esp01/include/esp01/
stream_digest.h; nextsync-peer.py has the twin). Two streams that should carry the
same bytes carry the same lines, so the first checkpoint where they differ is the
first hop that changed a byte. The hops, in the order the bytes travel:

  1 network     peer-sent       -> sock-in          (the socket, Winsock / wine)
  2 ESP buffer  sock-in         -> ipd-framed       (the engine's receive buffer)
  3 ESP -> UART esp-out         -> uart0-fifo-in    (the adapter, the UART RX FIFO, overflow)
  4 UART -> CPU uart0-fifo-in   -> uart0-guest-read (what the guest's IN actually returned)

What it cannot see is named too: damage between `ipd-framed` and `esp-out` (the +IPD
output assembly), and anything inside the last 256 bytes of a stream. The verdict
also gives the byte range of the reply the peer sent last before the first
Restart/Retry, so the checkpoint can be placed against it.
"""
import re
import sys

TS = re.compile(r"\[(\d\d):(\d\d):(\d\d)\.(\d\d\d)\]")
PEER_TS = re.compile(r"^ev (\d\d):(\d\d):(\d\d)\.(\d\d\d)")


def ms_of(m):
    h, mi, se, mm = (int(x) for x in m.groups())
    return ((h * 60 + mi) * 60 + se) * 1000 + mm


def fmt(ms):
    return "%02d:%02d:%02d.%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def stall_report(peer_path, jnext_path):
    """A STALL is not corruption: the digests match at every hop, but the ESP stopped
    releasing bytes toward the guest while some were still queued. Seen as a long gap
    between consecutive `esp-out` checkpoints ending at the guest's Restart (a healthy
    exchange has them a few ms apart: the pacer releases a byte per UART byte time)."""
    t_restart = None
    with open(peer_path, errors="replace") as f:
        for line in f:
            m = PEER_TS.match(line)
            if m and re.search(r"RX b'(Retry|Restart)'", line):
                t_restart = ms_of(m)
                break
    if t_restart is None:
        return None
    stamps = []
    with open(jnext_path, errors="replace") as f:
        for line in f:
            if "digest esp-out " in line:
                m = TS.search(line)
                if m:
                    stamps.append(ms_of(m))
    before = [t for t in stamps if t <= t_restart + 20]     # the Restart's own reply may be stamped in the same ms
    if len(before) < 5:
        return None
    gaps = sorted(((b - a, a, b) for a, b in zip(before, before[1:])), reverse=True)
    typical = sorted(g[0] for g in gaps)[len(gaps) // 2]
    worst, t_a, t_b = gaps[0]
    msg = ("  esp-out checkpoints: %d up to the first Retry/Restart; the typical gap between two is %d ms, the LARGEST %d ms "
           "(between %s and %s, the Restart reached the peer at %s)"
           % (len(before), typical, worst, fmt(t_a), fmt(t_b), fmt(t_restart)))
    if worst > 200 and worst > 5 * max(typical, 10):
        msg += ("\n  STALL: the ESP released NO byte toward the guest for %d ms (%s .. %s) before the guest gave up and "
                "sent its Restart. Every hop's bytes matched, so nothing was corrupted: the ESP stopped releasing output, the "
                "guest (missing the tail of the reply, < 256 bytes after the last checkpoint before the gap) timed out, and "
                "the pump ran again only when the guest's next command arrived. Look at AtEngine::tick / the tick gate (refresh_tick_gate, "
                "EspUartAdapter::mirror_gate) and ThreadedEsp::tick's try-lock." % (worst, fmt(t_a), fmt(t_b)))
    return msg

DIG = re.compile(r"digest (\S+) off=(\d+) fnv=([0-9a-f]{8})")
HOPS = [("1 network (peer-sent -> sock-in)", "peer-sent", "sock-in"),
        ("2 ESP receive buffer (sock-in -> ipd-framed)", "sock-in", "ipd-framed"),
        ("3 ESP output -> UART RX FIFO (esp-out -> uart0-fifo-in)", "esp-out", "uart0-fifo-in"),
        ("4 UART RX FIFO -> guest read (uart0-fifo-in -> uart0-guest-read)", "uart0-fifo-in", "uart0-guest-read")]


def load(path):
    streams = {}
    with open(path, errors="replace") as f:
        for line in f:
            m = DIG.search(line)
            if m:
                streams.setdefault(m.group(1), {})[int(m.group(2))] = m.group(3)
    return streams


def busiest(streams, suffix):
    """The uartN stream ending in `suffix` with the most checkpoints (the channel the ESP is on)."""
    cands = [n for n in streams if n.startswith("uart") and n.endswith(suffix)]
    return max(cands, key=lambda n: len(streams[n])) if cands else suffix


def main(argv):
    peer, jn = load(argv[1]), load(argv[2])
    allst = dict(jn)
    allst.update(peer)
    fifo_in, guest_read = busiest(allst, "-fifo-in"), busiest(allst, "-guest-read")
    hops = [(n.replace("uart0-fifo-in", fifo_in).replace("uart0-guest-read", guest_read),
             a.replace("uart0-fifo-in", fifo_in).replace("uart0-guest-read", guest_read),
             b.replace("uart0-fifo-in", fifo_in).replace("uart0-guest-read", guest_read)) for n, a, b in HOPS]
    out = []
    first_bad = None
    for name, a, b in hops:
        sa, sb = allst.get(a, {}), allst.get(b, {})
        common = sorted(set(sa) & set(sb))
        diff = [o for o in common if sa[o] != sb[o]]
        out.append("  hop %s: %d checkpoints in %s, %d in %s, %d common, %s" % (
            name, len(sa), a, len(sb), b, len(common),
            ("FIRST DIFFERENCE at stream offset %d (bytes %d..%d): %s=%s %s=%s" % (
                diff[0], diff[0] - 256, diff[0] - 1, a, sa[diff[0]], b, sb[diff[0]]))
            if diff else "all equal"))
        if diff and first_bad is None:
            first_bad = (name, diff[0])
    # the reply the peer sent last before the first Restart / Retry
    reply = None
    prev = None
    with open(argv[1], errors="replace") as f:
        for line in f:
            m = re.search(r"TX seq=(\d+) len=(\d+).*frame=(\d+) sent_through=(\d+)", line)
            if m:
                prev = (int(m.group(1)), int(m.group(3)), int(m.group(4)))
            if re.search(r"RX b'(Retry|Restart)'", line):
                reply = prev
                break
    if reply:
        out.append("  the reply sent last before the first Retry/Restart: seq %d, %d bytes, peer stream bytes %d..%d"
                   % (reply[0], reply[1], reply[2] - reply[1], reply[2] - 1))
    stall = stall_report(argv[1], argv[2])
    if stall:
        out.append(stall)
    if first_bad:
        out.append("  VERDICT: the first hop whose bytes differ is %s, at checkpoint offset %d."
                   % first_bad)
    else:
        out.append("  VERDICT: no hop differs at any common 256-byte checkpoint. Then either the damage lies between"
                   " ipd-framed and esp-out (the +IPD output assembly), or within the last 256 bytes of a stream, or"
                   " the guest rejected intact bytes (a timing/protocol event, not corruption).")
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
