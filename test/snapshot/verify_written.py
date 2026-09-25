#!/usr/bin/env python3
"""Validate REAL `.jns` files against the committed schema — GH #27 stage S9.

§13.2(3): "an independent JSON Schema validator, ON EVERY WRITTEN FILE".
`make schema-check` validates a manifest transcribed BY HAND from the design
document, which proves the schema is well-formed and discriminating; it does
not prove jnext writes a file the schema accepts. This does.

§13.2(4): the committed schema carries the hand-written constraint overlay, so
this also exercises the EXTERNAL knowledge a generator cannot supply — and the
mutation at the end is what stops the whole thing being vacuous, because a
schema with no constraints validates everything.

Prints `ALL-OK` on success, or a diagnostic. Exit non-zero on any failure.
"""

import copy
import json
import sys
import zipfile

try:
    import jsonschema
except ImportError:
    print("jsonschema not installed", file=sys.stderr)
    sys.exit(2)


def manifest_of(path):
    with zipfile.ZipFile(path) as z:
        return json.loads(z.read("manifest.json"))


def main(argv):
    if len(argv) < 3:
        print("usage: verify_written.py SCHEMA FILE.jns [FILE.jns...]",
              file=sys.stderr)
        return 2

    schema = json.load(open(argv[1]))
    files = argv[2:]
    problems = []
    by_machine = {}

    for path in files:
        try:
            manifest = manifest_of(path)
        except (OSError, KeyError, zipfile.BadZipFile,
                json.JSONDecodeError) as exc:
            problems.append(f"{path}: {exc}")
            continue
        by_machine[manifest.get("model", {}).get("machine")] = manifest
        # The schema describes the ARCHIVE, whose one validated member is the
        # manifest (§16.3: the registry is empty, so `state/*.json` has no
        # schema — S9's own limit, recorded in §13.3's S9 append).
        try:
            jsonschema.validate({"manifest.json": manifest}, schema)
        except jsonschema.ValidationError as exc:
            problems.append(f"{path}: {exc.message}")

    # THE MUTATION. §4.3(2): `mem/multiface-ram.bin` is present IFF the machine
    # is not the Next — on a Next the store is a window into Ram, so a private
    # copy would be 8 192 dead zeros. Relabelling a Next manifest as 48K must
    # therefore be rejected, and if it is not, every validation above was
    # decorative.
    if "next" in by_machine:
        forged = copy.deepcopy(by_machine["next"])
        forged["model"]["machine"] = "48k"
        try:
            jsonschema.validate({"manifest.json": forged}, schema)
            problems.append(
                "MUTATION NOT CAUGHT: a Next manifest relabelled 48k still "
                "validates — the §4.3(2) overlay invariant is not biting, so "
                "the validations above prove nothing")
        except jsonschema.ValidationError:
            pass
    else:
        problems.append("no Next manifest supplied; the mutation leg needs one")

    if problems:
        for p in problems:
            print(f"FAIL {p}", file=sys.stderr)
        return 1
    print("ALL-OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
