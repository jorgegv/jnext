#!/usr/bin/env python3
"""A mirror that answers slowly: the peer of `nextpi-func` fact 12 (GH #385).

Listens on 127.0.0.1 on an ephemeral port, writes the port number to <portfile>
(atomically: the file never exists half-written), and answers EVERY request,
once <delay_s> seconds have passed since it arrived, with

    HTTP/1.1 <status> Error
    Content-Length: 0
    Connection: close

and no body. That is a server that is reachable and slow, which is what a fetch
that takes longer than a second looks like to the GUI: the row needs the
NextPi release list to take 3 s so that the progress dialog, which appears only
for a slow fetch, is shown, and then to fail with an HTTP status.

If the client goes away during the delay (jnext gave up), the peer drops that
connection at once and takes the next one. It serves one connection at a time.

The lifetime is bounded: the peer ends by itself after 60 s whatever happens,
so a row that dies before it can kill the peer leaves nothing behind. The row
also kills it, and does not use a trap (the suite library owns the one EXIT
handler).

Usage: slow-http-peer.py <portfile> <delay_s> <status>
"""

import os
import socket
import sys
import time

LIFETIME_S = 60.0


def serve(conn, delay, status):
    """Read one request head, wait `delay` seconds, answer; give up if the client closes."""
    conn.settimeout(0.1)
    head = b""
    deadline = time.monotonic() + 5.0
    while b"\r\n\r\n" not in head:
        if time.monotonic() > deadline:
            return
        try:
            chunk = conn.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            return
        if not chunk:
            return
        head += chunk
    until = time.monotonic() + delay
    while time.monotonic() < until:
        try:
            if conn.recv(1) == b"":
                return                     # the client closed the connection: nothing to answer
        except socket.timeout:
            continue
        except OSError:
            return
    try:
        conn.sendall(
            ("HTTP/1.1 %d Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" % status).encode("ascii")
        )
    except OSError:
        pass


def main():
    if len(sys.argv) != 4:
        sys.stderr.write(__doc__)
        return 2
    portfile, delay, status = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    listener.settimeout(0.2)
    tmp = portfile + ".tmp"
    with open(tmp, "w") as f:
        f.write("%d\n" % listener.getsockname()[1])
    os.replace(tmp, portfile)
    end = time.monotonic() + LIFETIME_S
    while time.monotonic() < end:
        try:
            conn, _ = listener.accept()
        except socket.timeout:
            continue
        with conn:
            serve(conn, delay, status)
    return 0


if __name__ == "__main__":
    sys.exit(main())
