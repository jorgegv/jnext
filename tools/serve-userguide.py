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
            # No SO_REUSEADDR: we want the bind to fail exactly when a live
            # server holds the port, which is the condition we are probing for.
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
    with http.server.ThreadingHTTPServer((args.bind, port), handler) as httpd:
        print(f"user guide at http://localhost:{port}/  (Ctrl+C to stop)", flush=True)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
