#!/usr/bin/env python3
"""Verify every emitted `.jns` with CPython's stdlib `zipfile`.

The second independent reader of the GH #27 stage-S1 exit gate (the first is
info-zip's `unzip -t`). Neither is jnext code, which is the whole point: a
writer checked only by its own reader cannot find a framing error the two
agree on, and that shared blind spot is precisely how the `.szx` defect
survived a green, mutation-tested suite
(doc/design/NEXT-SNAPSHOT-FORMAT.md §13.1, §13.2(2)).

This goes further than `unzip -t`, which only reports "no errors detected".
It asserts, from the FOREIGN reader's own parse, the properties the design
states as facts about the file (§6, §13.2(7)) — so these are assertions
against the SPECIFICATION, not against jnext's reader:

  * `testzip()` finds no bad member (every CRC-32 re-checked)
  * `manifest.json` is member 0, in the archive's own order
  * the archive comment is exactly `jnext-snapshot format=1`
  * every member uses compression method 0 or 8, and nothing else
  * every member path is relative, lower-case, `/`-separated and free of `..`
  * no member name appears twice
  * `manifest.json` parses as JSON and carries an integer `format_version`
  * every `members[p]` declaration matches the archive's own length and CRC
    for `p`, read out of zipfile's parse rather than out of ours
"""

import json
import os
import re
import sys
import zipfile

EXPECTED_COMMENT = b"jnext-snapshot format=1"
PATH_RE = re.compile(r"^[a-z0-9][a-z0-9._/-]*$")


def check_archive(path, is_snapshot):
    """Return a list of complaint strings; empty means the file is good.

    `is_snapshot` selects which claims apply. A `.jns` is a complete snapshot
    and gets the format's own rules (member 0, the archive comment, the blob
    declarations). A `.zip` is bare ZIP-layer output from the container rows
    and has no manifest — only its FRAMING is claimed, so only the framing is
    checked. Applying the snapshot rules to it would be checking a claim
    nobody made; skipping the framing rules would leave the container rows'
    own output unverified by anything foreign.
    """
    bad = []
    name = os.path.basename(path)

    try:
        zf = zipfile.ZipFile(path, "r")
    except Exception as exc:                       # noqa: BLE001
        return ["%s: zipfile could not open it: %s" % (name, exc)]

    with zf:
        broken = zf.testzip()
        if broken is not None:
            bad.append("%s: testzip() reports a bad member: %s" % (name, broken))

        names = zf.namelist()
        if not names:
            bad.append("%s: the archive has no members" % name)
            return bad

        if is_snapshot and names[0] != "manifest.json":
            bad.append("%s: member 0 is %r, not 'manifest.json'"
                       % (name, names[0]))

        if zf.comment != EXPECTED_COMMENT:
            bad.append("%s: archive comment is %r, not %r"
                       % (name, zf.comment, EXPECTED_COMMENT))

        if len(set(names)) != len(names):
            dupes = sorted({n for n in names if names.count(n) > 1})
            bad.append("%s: duplicate member names: %s" % (name, dupes))

        for info in zf.infolist():
            if info.compress_type not in (zipfile.ZIP_STORED,
                                          zipfile.ZIP_DEFLATED):
                bad.append("%s: member %r uses compression method %d"
                           % (name, info.filename, info.compress_type))
            if not PATH_RE.match(info.filename):
                bad.append("%s: member path %r fails the grammar"
                           % (name, info.filename))
            parts = info.filename.split("/")
            if any(p in ("", ".", "..") for p in parts):
                bad.append("%s: member path %r has a bad component"
                           % (name, info.filename))
            if info.flag_bits & 0x0008:
                bad.append("%s: member %r carries a data descriptor"
                           % (name, info.filename))

        if not is_snapshot:
            return bad

        try:
            manifest = json.loads(zf.read("manifest.json").decode("utf-8"))
        except Exception as exc:                   # noqa: BLE001
            bad.append("%s: manifest.json does not parse: %s" % (name, exc))
            return bad

        fv = manifest.get("format_version")
        if not isinstance(fv, int) or isinstance(fv, bool):
            bad.append("%s: manifest format_version is %r, not an integer"
                       % (name, fv))

        # The declaration chain of §5.3, checked from the FOREIGN parse: the
        # manifest declares a length and a CRC-32, and zipfile's own reading of
        # the archive must agree with both. This is the half a JSON Schema
        # cannot do, so it is done here rather than pretended about.
        by_name = {i.filename: i for i in zf.infolist()}
        for member, decl in (manifest.get("members") or {}).items():
            info = by_name.get(member)
            if info is None:
                bad.append("%s: manifest declares blob %r with no member"
                           % (name, member))
                continue
            if info.file_size != decl.get("bytes"):
                bad.append("%s: blob %r is %d bytes, manifest declares %r"
                           % (name, member, info.file_size, decl.get("bytes")))
            declared_crc = decl.get("crc32")
            if not isinstance(declared_crc, str) or \
                    not re.match(r"^[0-9a-f]{8}$", declared_crc):
                bad.append("%s: blob %r crc32 %r is not 8 lower-case hex digits"
                           % (name, member, declared_crc))
            elif int(declared_crc, 16) != info.CRC:
                bad.append("%s: blob %r has CRC 0x%08x, manifest declares %s"
                           % (name, member, info.CRC, declared_crc))

    return bad


def main(argv):
    if len(argv) != 2:
        print("usage: verify_zipfile.py <corpus-dir>", file=sys.stderr)
        return 2

    files = sorted(f for f in os.listdir(argv[1])
                   if f.endswith(".jns") or f.endswith(".zip"))
    snapshots = [f for f in files if f.endswith(".jns")]
    if not files or not snapshots:
        # A pass over nothing is the vacuous-pass failure mode, which turns the
        # strongest evidence this design has into the most confident lie. Both
        # halves are required: an all-`.zip` corpus would exercise the framing
        # and silently assert none of the format's own rules.
        print("FAIL  %s holds %d archives (%d snapshots); a pass here would be "
              "vacuous" % (argv[1], len(files), len(snapshots)))
        return 1

    complaints = []
    for f in files:
        complaints.extend(check_archive(os.path.join(argv[1], f),
                                        f.endswith(".jns")))

    if complaints:
        print("FAIL  CPython zipfile rejected the .jns writer's output:")
        for c in complaints:
            print("      " + c)
        return 1

    print("  python zipfile accepted %d archives (%d snapshots, %d bare)"
          % (len(files), len(snapshots), len(files) - len(snapshots)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
