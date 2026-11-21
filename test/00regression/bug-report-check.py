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
    --viewer SCRIPT        run SCRIPT (tools/jnext-bug-view.py) as `python3 -I SCRIPT --text REPORT` with a minimal
                           environment: it must exit 0 and print machine.json's PC as the report has it, and
                           say "memory: included" or "memory: not included" as the manifest does
    --scan DIR             no 32-byte window of any file under DIR (windows taken every 32
                           bytes; periods of 4 or less, windows of fewer than 8 distinct values and counting sequences excluded) occurs at any offset of any
                           member, as raw bytes and as what it decodes to (JSON hex strings and integer arrays, nested
                           zips); the only member skipped is a program proved byte-equal to the row's
                           own file with --equals, and only when it is the one program: the hard
                           rule, that jnext puts no firmware in a report

Prints one line per failed check and exits 1; exits 0 when every check passed.
"""
import io
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
    # Low-entropy windows (a repeating pattern, or fewer than 8 distinct byte values) are tables and
    # padding, and register files legitimately equal one: NextZXOS writes registers from tables.
    if len(set(w)) < 8:
        return True
    if len({(w[i + 1] - w[i]) % 256 for i in range(len(w) - 1)}) == 1:
        return True   # a counting sequence: the register numbers of machine.json are one
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


def small_ints(vals):
    return [v for v in vals if isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= 255]


def flat_ints(obj):
    """The integers 0..255 of a nested array of integer arrays in order, or None if it is anything else."""
    if isinstance(obj, bool):
        return None
    if isinstance(obj, int):
        return [obj] if 0 <= obj <= 255 else None
    if isinstance(obj, list) and obj:
        out = []
        for e in obj:
            f = flat_ints(e)
            if f is None:
                return None
            out += f
        return out
    return None


def json_views(obj, out):
    """The bytes a JSON value stands for: hex strings, integer arrays, the integers of an array of objects."""
    if isinstance(obj, str):
        if len(obj) >= 2 and len(obj) % 2 == 0:
            try:
                out.append(bytes.fromhex(obj))
            except ValueError:
                pass
    elif isinstance(obj, list):
        if obj and len(small_ints(obj)) == len(obj) and all(isinstance(v, int) for v in obj):
            out.append(bytes(obj))
        flat = flat_ints(obj)
        if flat is not None and any(isinstance(e, list) for e in obj):
            out.append(bytes(flat))   # arrays of integer arrays, concatenated: pieces under a window add up
        if obj and all(isinstance(e, dict) for e in obj):
            keys = []
            for e in obj:
                keys += [k for k in e if k not in keys]
            for k in keys:   # one view per key: the values of a log() entry, in order
                seq = small_ints([e[k] for e in obj if k in e])
                if seq:
                    out.append(bytes(seq))
        for e in obj:
            json_views(e, out)
    elif isinstance(obj, dict):
        for v in obj.values():
            json_views(v, out)


def views(name, data):
    """Every byte string a member is: its raw bytes, its decoded JSON arrays, the members of a nested zip."""
    yield name, data
    if name.endswith(".json"):
        try:
            found = []
            json_views(json.loads(data), found)
        except ValueError:
            found = []
        for v in found:
            yield name + " (decoded)", v
    if data[:4] == b"PK\x03\x04":
        try:
            inner = zipfile.ZipFile(io.BytesIO(data))
            for n in inner.namelist():
                yield from views(name + "!" + n, inner.read(n))
        except zipfile.BadZipFile:
            pass


def main():
    report, args = load(sys.argv)
    bad = []
    proved = set()   # program members the row has proved equal to a file it made itself
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
                    else:
                        proved.add(args[i + 1])
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
        elif a == "--viewer":
            import subprocess
            env = {"LANG": "C", "PATH": "/usr/bin:/bin"}
            r = subprocess.run([sys.executable, "-I", args[i + 1], "--text", report], capture_output=True, text=True, env=env)
            if r.returncode != 0:
                bad.append("viewer exit %d: %s" % (r.returncode, r.stderr.strip()))
            else:
                pc = json.loads(member("machine.json"))["registers"]["PC"]
                want = "PC = 0x%04X (%d)" % (pc, pc)
                if want not in r.stdout.splitlines():
                    bad.append("viewer did not print %r" % want)
                inc = json.loads(member("manifest.json"))["memory"]["included"]
                if ("memory: included" if inc else "memory: not included") not in r.stdout:
                    bad.append("viewer's memory line does not match the manifest")
            i += 2
        elif a == "--scan":
            wins, nfiles = windows_of(args[i + 1])
            if nfiles == 0 or not wins:
                bad.append("--scan %s: no files to scan (the check would prove nothing)" % args[i + 1])
            hits = 0
            try:
                nprog = len(json.loads(member("manifest.json")).get("programs", []))
            except ValueError:
                nprog = -1
            for n in names:
                if n in proved and nprog == 1:
                    continue   # the one program, proved byte-equal to the row's own file: not jnext's output
                for vn, data in views(n, member(n)):
                    for off in range(len(data) - WINDOW + 1):
                        if data[off:off + WINDOW] in wins:
                            hits += 1
                            if hits <= 3:
                                bad.append("%s offset %d holds 32 bytes of a file under %s" % (vn, off, args[i + 1]))
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
