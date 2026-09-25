#!/usr/bin/env python3
"""An INDEPENDENT reader for the `.jns` snapshot format — GH #27 stage S9.

WRITTEN FROM THE SPECIFICATION, NOT FROM THE WRITER.  That is the entire value
of this file and the only rule about changing it:

    If you find yourself opening `src/core/emulator_jns.cpp` to make this
    reader work, THE SPECIFICATION IS WHAT NEEDS FIXING.  Fix
    doc/design/NEXT-SNAPSHOT-FORMAT.md, then come back here.

Every rule below cites the section it comes from, so a reviewer can check the
citation rather than the behaviour.  Nothing here imports, links or consults
jnext; it uses only the Python standard library.

WHAT THIS IS FOR.  §13.1: jnext's `.szx` saver once wrote RAM pages 0-111 and
its own loader accepted them, so save/load/compare passed byte-exact while
**every file jnext could produce** was rejected by real FUSE.  Saver and loader
shared a blind spot, which made the defect structurally invisible to the suite.
A second implementation, written from the document by someone who cannot see
the first, is the only thing that finds that class.

WHAT IT IS NOT.  §13.3: this is not a foreign reader.  It is a *second* reader
in the same repository, and an error in the DOCUMENT propagates into both it
and the writer.  It narrows the hole; it does not close it.  The genuinely
foreign adjudication is FUSE reading a `.szx` written at the same instant, and
that reaches only the CPU, the 64 KB map and 128K paging.

Usage:
    jns_reader.py FILE.jns              # parse, validate, print a summary
    jns_reader.py FILE.jns --json       # machine-readable extraction
    jns_reader.py FILE.jns --expect-machine 128k
"""

import argparse
import json
import re
import sys
import zipfile
import zlib


class JnsError(Exception):
    """A refusal.  Every one NAMES the offending thing (§16.1's `JNSM` rule:
    a refusal reading only 'invalid snapshot' is a failing row)."""


# ── §7.1 ──────────────────────────────────────────────────────────────────
# The grammar version this reader understands.  It versions the member
# namespace, the required manifest keys, the encoding rules and the
# unknown-member rule, and says nothing about the emulated machine.
FORMAT_VERSION = 1

# §6 — the EOCD comment, verbatim, so `unzip -z` identifies the file with no
# JSON parse and a TRUNCATED file is still classifiable.
ARCHIVE_COMMENT = b"jnext-snapshot format=1"

# §6 — member 0, always.
MANIFEST_MEMBER = "manifest.json"

# §12.1 — `state/` and `meta/` are OPEN namespaces: an unrecognised member
# there is ignored and logged.  `mem/` is CLOSED: an undeclared blob has no
# length or CRC to check, so it is not covered by §5.3's validation chain and
# the archive is refused.
STATE_PREFIX, MEM_PREFIX, META_PREFIX = "state/", "mem/", "meta/"

# §6.2 — the canonical patterns, per type.  NOT the single `^-?[0-9]+$` an
# earlier draft of the spec used: an unsigned field must not accept a sign.
RE_U64 = re.compile(r"^(0|[1-9][0-9]*)$")
RE_I64 = re.compile(r"^(0|-?[1-9][0-9]*)$")
RE_HEX = re.compile(r"^[0-9a-f]*$")

# §6.1 — the blob members, listed literally by the specification.  A reader
# built from the document knows these names; it does not derive them.
KNOWN_BLOBS = {
    "mem/ram.bin",
    "mem/bank5-vram.bin",
    "mem/bank7-bram.bin",
    "mem/sprite-patterns.bin",
    "mem/multiface-ram.bin",
}


def _u64_state(value, where):
    """A `u64`/`i64` in `state/*.json` travels as a STRING of decimal digits.

    §6.2 and §7.4: because JSON numbers are IEEE doubles and the `/INT` window
    exceeds 2^53 in *every* snapshot, so a JavaScript validator would silently
    corrupt it.  A reader that accepts a bare number there is one that will
    round somebody's snapshot.

    NOT for the manifest — see `_u64_manifest`.
    """
    if not isinstance(value, str):
        raise JnsError(f"{where} must be a decimal STRING (§6.2), got "
                       f"{type(value).__name__}")
    if not RE_U64.match(value):
        raise JnsError(f"{where} is not a canonical unsigned decimal string: "
                       f"{value!r}")
    return int(value)


def _u64_manifest(value, where):
    """A `u64` in `manifest.json`, which is a plain JSON number.

    THIS DISTINCTION IS WHAT THIS READER FOUND.  Written from §6.2 and §7.4
    alone, it applied the string rule to `manifest.capture.frame` and refused
    every file jnext produces — because §7.4 said "every 64-bit field,
    unconditionally" while §8's own example showed `"frame": 41291` as a
    number, and §6.2 never said its table governs `state/*.json` only.

    The spec now says so (§6.2's SCOPE paragraph, added by S9), and this
    function is the reader's half of that resolution.  It still refuses a
    float, a bool or a negative, because "plain number" is not "anything".
    """
    if isinstance(value, bool) or not isinstance(value, int):
        raise JnsError(f"{where} must be a JSON integer (§8), got "
                       f"{type(value).__name__}")
    if value < 0:
        raise JnsError(f"{where} is negative: {value}")
    return value


def _i64_open(value, where):
    """§6.2 / §7.4 — an open-ended sentinel is the JSON string `"open"`."""
    if value == "open":
        return None
    if not isinstance(value, str) or not RE_I64.match(value):
        raise JnsError(f"{where} must be a signed decimal string or \"open\" "
                       f"(§6.2), got {value!r}")
    return int(value)


def _need(obj, key, where):
    if key not in obj:
        raise JnsError(f"{where} is missing the required key {key!r}")
    return obj[key]


def _hexstr(value, where, nybbles=None):
    """§6.2 — a fixed array is ONE lower-case hex string, no separators.

    The length is checked against the DECLARATION, never taken from the file.
    §6.2 also records what this cannot see: for multi-byte elements the element
    ORDER is undeclared (§18.2(7)), so a reader can verify the width and not
    the endianness.
    """
    if not isinstance(value, str):
        raise JnsError(f"{where} must be a hex string (§6.2)")
    if not RE_HEX.match(value):
        raise JnsError(f"{where} is not lower-case hex without separators: "
                       f"{value[:32]!r}")
    if nybbles is not None and len(value) != nybbles:
        raise JnsError(f"{where} is {len(value)} hex chars, expected "
                       f"{nybbles}")
    return bytes.fromhex(value)


class Snapshot:
    """A parsed `.jns`."""

    # EVERY library exception a hostile file can raise becomes a REFUSAL.
    #
    # A reader handed a corrupt file must say what is wrong with it, not print
    # a traceback: a traceback names a line of Python, which is not the
    # offending thing (§16.1's `JNSM` rule). This list was not written from
    # imagination — `snapshot-spec-reader-func` corrupted one byte of a member
    # and `zipfile.testzip()` itself raised, because a damaged DEFLATE stream
    # fails inside the decompressor rather than returning the member's name.
    _HOSTILE = (zipfile.BadZipFile, zlib.error, EOFError, ValueError,
                UnicodeDecodeError, OSError)

    def __init__(self, path):
        self.path = path
        self.ignored_members = []
        try:
            self._open()
        except JnsError:
            raise
        except self._HOSTILE as exc:
            raise JnsError(f"{path}: {type(exc).__name__}: {exc}") from exc

    # ── Container (§5, §6) ────────────────────────────────────────────────
    def _open(self):
        try:
            zf = zipfile.ZipFile(self.path)
        except zipfile.BadZipFile as exc:
            raise JnsError(f"{self.path} is not a ZIP archive: {exc}") from exc

        with zf:
            # §5.3 — the validation chain's outermost link, and it is NOT our
            # code: Python's `zipfile` decompresses and checks every member's
            # CRC-32.
            #
            # PER MEMBER, not `zf.testzip()`. `testzip()` returns the name of
            # the first bad member — but only when the decompressor finishes;
            # a DAMAGED DEFLATE STREAM makes it RAISE instead, and the caller
            # then has an exception that does not say which member. That is a
            # refusal which fails to name the offending thing (§16.1), and it
            # is what `snapshot-spec-reader-func` caught. Reading each member
            # in its own `try` recovers the name in every case.
            for info in zf.infolist():
                try:
                    with zf.open(info) as fh:
                        while fh.read(1 << 16):
                            pass
                except self._HOSTILE as exc:
                    raise JnsError(
                        f"member {info.filename!r} is corrupt: "
                        f"{type(exc).__name__}: {exc}") from exc

            if zf.comment != ARCHIVE_COMMENT:
                raise JnsError(
                    f"archive comment is {zf.comment!r}, expected "
                    f"{ARCHIVE_COMMENT!r} (§6) — this is what makes a "
                    f"truncated file classifiable")

            names = zf.namelist()
            if not names:
                raise JnsError("the archive is empty")
            if names[0] != MANIFEST_MEMBER:
                raise JnsError(
                    f"member 0 is {names[0]!r}, not {MANIFEST_MEMBER!r} "
                    f"(§6: a reader parses it before touching anything else)")
            if len(set(names)) != len(names):
                dupes = sorted({n for n in names if names.count(n) > 1})
                raise JnsError(f"duplicate member names: {dupes}")

            for n in names:
                if n.startswith("/") or ".." in n.split("/"):
                    raise JnsError(f"member path {n!r} is absolute or escapes")

            self.manifest = json.loads(zf.read(MANIFEST_MEMBER))
            self._check_manifest()
            self._check_members(zf, names)
            self._read_state(zf, names)
            self.blobs = {n: zf.read(n) for n in names
                          if n.startswith(MEM_PREFIX)}

    # ── Manifest (§7, §8) ─────────────────────────────────────────────────
    def _check_manifest(self):
        m = self.manifest
        if not isinstance(m, dict):
            raise JnsError("manifest.json is not a JSON object")

        # §7.3 — the version rules, in the order the spec states them.
        fv = _need(m, "format_version", "manifest")
        if not isinstance(fv, int) or isinstance(fv, bool):
            raise JnsError(f"manifest.format_version must be an integer, got "
                           f"{fv!r}")
        if fv > FORMAT_VERSION:
            raise JnsError(
                f"manifest.format_version is {fv}; this reader understands "
                f"{FORMAT_VERSION} (§7.3: refuse NAMING BOTH numbers)")
        self.format_version = fv

        model = _need(m, "model", "manifest")
        self.machine = _need(model, "machine", "manifest.model")
        self.ram_kb = _need(model, "ram_kb", "manifest.model")
        if not isinstance(self.ram_kb, int) or self.ram_kb <= 0:
            raise JnsError(f"manifest.model.ram_kb is {self.ram_kb!r}")

        capture = _need(m, "capture", "manifest")
        # §8 — always true, or the file was not written.  Recorded as a fact a
        # reader can CHECK, not as a mode.
        if capture.get("frame_boundary") is not True:
            raise JnsError(
                "manifest.capture.frame_boundary is not true — a .jns is only "
                "ever written at a frame boundary (§8)")
        self.frame = _u64_manifest(
            _need(capture, "frame", "manifest.capture"),
            "manifest.capture.frame")

        self.subsystems = m.get("subsystems", [])
        if not isinstance(self.subsystems, list):
            raise JnsError("manifest.subsystems is not an array")

    def _check_members(self, zf, names):
        """§5.3 / §12.4 — the blob declaration chain."""
        declared = self.manifest.get("members", {})
        if not isinstance(declared, dict):
            raise JnsError("manifest.members is not an object")

        info = {i.filename: i for i in zf.infolist()}

        for path, decl in declared.items():
            if path not in info:
                raise JnsError(f"manifest declares {path!r} but the archive "
                               f"does not contain it")
            want = _u64_manifest(
                _need(decl, "bytes", f"manifest.members[{path!r}]"),
                f"manifest.members[{path!r}].bytes")
            got = info[path].file_size
            if got != want:
                raise JnsError(f"{path} is {got} bytes, manifest declares "
                               f"{want}")

        # §12.4 — `mem/` is CLOSED: a blob nobody declared has no length and no
        # CRC to check against, so it is outside the validation chain.
        for n in names:
            if n.startswith(MEM_PREFIX) and n not in declared:
                raise JnsError(f"{n} is an UNDECLARED blob; mem/ is a closed "
                               f"namespace (§12.1)")
            if n.startswith(MEM_PREFIX) and n not in KNOWN_BLOBS:
                # Not a refusal: the spec's list is what THIS reader knows, and
                # a newer writer may add one.  Reported, per §12.1.
                self.ignored_members.append(n)

    def _read_state(self, zf, names):
        """§6.1 — one `state/<name>.json` per subsystem."""
        self.state = {}
        for n in names:
            if not n.startswith(STATE_PREFIX):
                if not n.startswith((MEM_PREFIX, META_PREFIX)) \
                        and n != MANIFEST_MEMBER:
                    self.ignored_members.append(n)
                continue
            if not n.endswith(".json"):
                raise JnsError(f"{n} is under state/ but is not .json (§6.1)")
            key = n[len(STATE_PREFIX):-len(".json")]
            doc = json.loads(zf.read(n))
            if not isinstance(doc, dict):
                raise JnsError(f"{n} is not a JSON object")
            self.state[key] = doc

        # §8 — the writer's own list exists so a reader can distinguish "this
        # subsystem was deliberately not saved" from "this member is missing or
        # corrupt".  They are different failures and must not be conflated.
        for name in self.subsystems:
            if name not in self.state:
                raise JnsError(f"manifest lists subsystem {name!r} but "
                               f"state/{name}.json is absent")

    # ── The extraction FUSE adjudicates (§13.2(1)) ───────────────────────
    def cpu(self):
        """The Z80 state, as §6.2 says it is encoded.

        Key names are read from `state/cpu.json` and NOT guessed: the spec
        fixes the ENCODING of a field, not its name, so a reader built from the
        document must tolerate the names it finds.  What it does check is that
        the values obey §6.2 — which is the claim §13.2(1) tests against FUSE.
        """
        doc = self.state.get("cpu")
        if doc is None:
            raise JnsError("state/cpu.json is absent")
        out = {}
        for k, v in doc.items():
            if isinstance(v, bool):
                out[k] = v
            elif isinstance(v, int):
                out[k] = v
            elif isinstance(v, str):
                if v == "open":
                    out[k] = None
                elif RE_I64.match(v):
                    out[k] = int(v)
                elif RE_HEX.match(v):
                    out[k] = v
                else:
                    out[k] = v
        return out

    def ram(self):
        """`mem/ram.bin`, whole.  §6.1 case 1."""
        if "mem/ram.bin" not in self.blobs:
            raise JnsError("mem/ram.bin is absent")
        blob = self.blobs["mem/ram.bin"]
        # §13.2(4)'s first overlay invariant, checked HERE as well as in the
        # schema, because a reader that trusts the manifest has not verified
        # anything the manifest could be wrong about.
        want = self.ram_kb * 1024
        if len(blob) != want:
            raise JnsError(f"mem/ram.bin is {len(blob)} bytes; "
                           f"model.ram_kb={self.ram_kb} implies {want}")
        return blob

    def check_overlay_invariants(self):
        """§13.2(4) — the EXTERNAL knowledge, restated by a second reader.

        These are the constraints a generated schema cannot express because the
        generator does not know them.  They are checked here as well so the
        claim does not rest on one mechanism.
        """
        problems = []
        sizes = {p: len(b) for p, b in self.blobs.items()}

        if sizes.get("mem/bank5-vram.bin") not in (None, 16384):
            problems.append(f"mem/bank5-vram.bin is "
                            f"{sizes['mem/bank5-vram.bin']}, expected 16384")
        if sizes.get("mem/bank7-bram.bin") not in (None, 8192):
            problems.append(f"mem/bank7-bram.bin is "
                            f"{sizes['mem/bank7-bram.bin']}, expected 8192")

        # §4.3(2) — the Multiface private array is dead zeros on the Next,
        # where the store is a window into Ram, and real state elsewhere.  So
        # the member is present IFF the machine is not the Next.
        has_mf = "mem/multiface-ram.bin" in self.blobs
        if self.machine == "next" and has_mf:
            problems.append("mem/multiface-ram.bin is present on a Next; "
                            "§4.3(2) says the array is a window there")
        if self.machine != "next" and not has_mf:
            problems.append(f"mem/multiface-ram.bin is absent on a "
                            f"{self.machine}; §4.3(2) says it is real state "
                            f"there")
        return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("file")
    ap.add_argument("--json", action="store_true",
                    help="machine-readable extraction on stdout")
    ap.add_argument("--expect-machine",
                    help="refuse unless model.machine is this")
    args = ap.parse_args(argv)

    try:
        snap = Snapshot(args.file)
        problems = snap.check_overlay_invariants()
        if problems:
            raise JnsError("; ".join(problems))
        if args.expect_machine and snap.machine != args.expect_machine:
            raise JnsError(f"model.machine is {snap.machine!r}, expected "
                           f"{args.expect_machine!r}")
        ram = snap.ram()
        cpu = snap.cpu()
    except JnsError as exc:
        print(f"REFUSED: {exc}", file=sys.stderr)
        return 1
    except Snapshot._HOSTILE as exc:
        # Anything the accessors above can still raise on a file that parsed.
        print(f"REFUSED: {args.file}: {type(exc).__name__}: {exc}",
              file=sys.stderr)
        return 1

    if args.json:
        json.dump({
            "format_version": snap.format_version,
            "machine": snap.machine,
            "ram_kb": snap.ram_kb,
            "frame": snap.frame,
            "subsystems": sorted(snap.state),
            "ram_bytes": len(ram),
            "ignored_members": snap.ignored_members,
            "cpu": cpu,
        }, sys.stdout, indent=2, sort_keys=True)
        print()
    else:
        print(f"OK {args.file}")
        print(f"  format_version {snap.format_version}")
        print(f"  machine        {snap.machine} ({snap.ram_kb} KB)")
        print(f"  frame          {snap.frame}")
        print(f"  subsystems     {len(snap.state)}")
        print(f"  mem/ram.bin    {len(ram)} bytes")
        if snap.ignored_members:
            print(f"  ignored        {snap.ignored_members}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
