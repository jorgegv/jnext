#!/usr/bin/env python3
"""Check a jnext bug report (GH #492) from the outside, with the standard library only.

    bug-report-check.py REPORT.zip [options]

    --has MEMBER           the zip has this member
    --lacks-prefix PREFIX  no member starts with PREFIX
    --equals MEMBER FILE   MEMBER's bytes equal FILE's
    --forbid TEXT          no member contains TEXT (a user name, the home directory)
    --manifest PATH VALUE  manifest.json at PATH (dots) equals VALUE, compared as text
    --json MEMBER PATH VALUE   the same for any JSON member
    --absent-key MEMBER PATH   the JSON member has no key at PATH (dots)
    --scan DIR             no 32-byte window of any file under DIR (windows taken every 32
                           bytes, periods of 4 or less excluded) occurs at any offset of any
                           member: the hard rule, that a report carries no firmware

Prints one line per failed check and exits 1; exits 0 when every check passed.
"""
import json
import os
import sys
import zipfile

WINDOW = 32


def load(argv):
    if len(argv) < 2:
        print(__doc__)
        sys.exit(2)
    return argv[1], argv[2:]


def dig(obj, path):
    for part in path.split("."):
        if isinstance(obj, list):
            obj = obj[int(part)]
        else:
            obj = obj[part]
    return obj


def is_fill(w):
    for period in range(1, 5):
        if all(w[i] == w[i + period] for i in range(len(w) - period)):
            return True
    return False


def windows_of(directory):
    wins = set()
    nfiles = 0
    for root, _dirs, files in os.walk(directory):
        for name in files:
            with open(os.path.join(root, name), "rb") as f:
                data = f.read()
            nfiles += 1
            for i in range(0, len(data) - WINDOW + 1, WINDOW):
                w = data[i:i + WINDOW]
                if not is_fill(w):
                    wins.add(w)
    return wins, nfiles


def main():
    report, args = load(sys.argv)
    bad = []
    z = zipfile.ZipFile(report)
    names = z.namelist()
    members = {}

    def member(name):
        if name not in members:
            members[name] = z.read(name)
        return members[name]

    i = 0
    while i < len(args):
        a = args[i]
        if a == "--has":
            if args[i + 1] not in names:
                bad.append("missing member " + args[i + 1])
            i += 2
        elif a == "--lacks-prefix":
            hit = [n for n in names if n.startswith(args[i + 1])]
            if hit:
                bad.append("members under %s: %s" % (args[i + 1], ", ".join(hit)))
            i += 2
        elif a == "--equals":
            if args[i + 1] not in names:
                bad.append("missing member " + args[i + 1])
            else:
                with open(args[i + 2], "rb") as f:
                    if member(args[i + 1]) != f.read():
                        bad.append("%s differs from %s" % (args[i + 1], args[i + 2]))
            i += 3
        elif a == "--forbid":
            text = args[i + 1].encode()
            if text:
                for n in names:
                    if text in member(n):
                        bad.append("%s contains %r" % (n, args[i + 1]))
            i += 2
        elif a in ("--manifest", "--json"):
            if a == "--manifest":
                name, path, want = "manifest.json", args[i + 1], args[i + 2]
                i += 3
            else:
                name, path, want = args[i + 1], args[i + 2], args[i + 3]
                i += 4
            try:
                got = dig(json.loads(member(name)), path)
            except (KeyError, IndexError, ValueError) as e:
                bad.append("%s %s: %r" % (name, path, e))
                continue
            got = json.dumps(got) if isinstance(got, (bool, type(None))) else str(got)
            want = {"true": "true", "false": "false"}.get(want, want)
            if got != want:
                bad.append("%s %s is %s, want %s" % (name, path, got, want))
        elif a == "--absent-key":
            try:
                dig(json.loads(member(args[i + 1])), args[i + 2])
                bad.append("%s has %s" % (args[i + 1], args[i + 2]))
            except (KeyError, IndexError):
                pass
            i += 3
        elif a == "--scan":
            wins, nfiles = windows_of(args[i + 1])
            if nfiles == 0 or not wins:
                bad.append("--scan %s: no files to scan (the check would prove nothing)" % args[i + 1])
            hits = 0
            for n in names:
                data = member(n)
                for off in range(len(data) - WINDOW + 1):
                    if data[off:off + WINDOW] in wins:
                        hits += 1
                        if hits <= 3:
                            bad.append("%s offset %d holds 32 bytes of a file under %s" % (n, off, args[i + 1]))
            if hits:
                bad.append("--scan %s: %d windows found" % (args[i + 1], hits))
            else:
                print("scan %s: %d files, %d windows, 0 found in %d members" % (args[i + 1], nfiles, len(wins), len(names)))
            i += 2
        else:
            print("unknown option " + a)
            sys.exit(2)
    for b in bad:
        print("FAIL " + b)
    sys.exit(1 if bad else 0)


main()
