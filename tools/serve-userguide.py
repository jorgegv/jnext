#!/usr/bin/env python3
"""Serve the rendered user guide on the first free port at or above --port.

The port is picked before anything is printed, so the URL reported is always
the one actually bound — a busy 8000 must not send the reader to a dead link.
"""

import argparse
import contextlib
import functools
import http.server
import os
import socket
import sys

MAX_TRIES = 50


def first_free_port(host, start, tries):
    """Bind-test ports upward from `start`; return the first that is free."""
    for port in range(start, start + tries):
        with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
            # No SO_REUSEADDR on the probe, so it also rejects a port left in
            # TIME_WAIT by a recently stopped server. That is stricter than
            # "someone is listening" — stop and immediately restart and you
            # will hop to the next port — but erring toward a definitely-free
            # port is the right trade for a helper whose whole job is printing
            # a URL that works.
            try:
                s.bind((host, port))
                return port
            except OSError:
                continue
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--directory", required=True)
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--bind", default="127.0.0.1")
    args = ap.parse_args()

    if not os.path.isfile(os.path.join(args.directory, "index.html")):
        print(f"error: {args.directory}/index.html not found", file=sys.stderr)
        return 1

    port = first_free_port(args.bind, args.port, MAX_TRIES)
    if port is None:
        print(
            f"error: no free port in {args.port}..{args.port + MAX_TRIES - 1}",
            file=sys.stderr,
        )
        return 1

    if port != args.port:
        print(f"port {args.port} is busy — using {port} instead")

    handler = functools.partial(
        http.server.SimpleHTTPRequestHandler, directory=args.directory
    )
    try:
        httpd = http.server.ThreadingHTTPServer((args.bind, port), handler)
    except OSError as exc:
        # The probe closed this port before the real bind, so another process
        # can win the gap. Rare, and a retry is the user's to make — but say so
        # instead of dumping a traceback.
        print(f"error: port {port} was taken before we could bind it: {exc}",
              file=sys.stderr)
        return 1

    with httpd:
        print(f"user guide at http://localhost:{port}/  (Ctrl+C to stop)", flush=True)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
