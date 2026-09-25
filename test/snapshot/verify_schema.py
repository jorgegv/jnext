#!/usr/bin/env python3
"""Check the committed `.jns` JSON Schema with an implementation that is not ours.

Run by `make schema-check` (see verify-schema.sh for the skip/fail posture).

WHAT THIS ASSERTS, AND WHAT IT DOES NOT
---------------------------------------
Three things, in order of what each removes:

1. The committed file IS a valid JSON Schema (draft 2020-12), as judged by
   `jsonschema`. A generator that emitted a plausible-looking object with an
   invalid keyword would otherwise pass the byte-diff forever, and every later
   validation would silently do nothing.

2. A manifest built HERE, from the design document rather than from the C++
   writer, validates. This is the "second reader, written from the
   specification" of §13.2(6), scoped to the manifest.

3. A matrix of DELIBERATE faults, each one a single mutation of that manifest,
   is REJECTED — naming which keyword caught it. This is the part that stops
   (1) and (2) from being vacuous: a schema that accepts everything passes both
   and fails every row here.

It does NOT assert that the schema is semantically right about the emulated
machine. Nothing automated can (§9.3); that is what the committed-and-diffed
artefact and a human reviewer are for.
"""

import copy
import importlib.metadata
import json
import sys

import jsonschema


def manifest_from_the_spec():
    """A manifest written from §6/§7/§8, not read out of `manifest_to_json`.

    Deliberately transcribed rather than generated: a fixture produced by the
    writer under test shares the writer's blind spots, which is precisely the
    failure `.szx` had (§13.1).
    """
    return {
        "format_version": 1,
        "created": "2026-09-24T07:39:00Z",
        "producer": {
            "jnext_version": "1.0.24",
            "git_describe": "v1.0.24-0-gfd2d18ff",
            "platform": "linux-x86_64",
        },
        "model": {
            "state_model_revision": 1,
            "machine": "next",
            "ram_kb": 2048,
            "timing": "vga0",
            "cpu_speed_nr07": 3,
        },
        "capture": {"frame": 41291, "frame_boundary": True},
        "media": {
            "sdcard": {
                "mounted_path": "/mnt/sd/cspect-next-1gb-fixed.img",
                "read_only": False,
                "identity": {
                    "image_bytes": 1073741824,
                    "mbr_partition_table_sha256": "a" * 64,
                    "fat32_volume_id": "1a2b3c4d",
                    "partition_lba": 2048,
                },
                "informational": {"fat32_bs_vollab": "NEXT       "},
                "content_stamp": {"sha256": "b" * 64,
                                  "mtime_utc": "2026-09-24T07:00:00Z"},
            },
            # §8 / §10.2 P3 — ROM identity. Digests, never content (N3).
            "roms": {"source": "sdcard",
                     "sha256": {"48.rom": "c" * 64, "128.rom": "d" * 64}},
            "boot_rom_sha256": "e" * 64,
            # §8 / §10.2 P4 — tape identity, the esxDOS-handle shape.
            "tape": {"path": "/home/user/game.tzx", "sha256": "f" * 64,
                     "position_tstates": 123456789, "realtime": True},
            "esxdos_root": "/home/user/nextdev",
        },
        # §10.2 P5 — the preview image's declaration.
        "preview": {"path": "meta/preview.png", "width": 320, "height": 256},
        "members": {
            "mem/ram.bin": {"bytes": 2097152, "crc32": "8f3a21bd"},
            "mem/bank5-vram.bin": {"bytes": 16384, "crc32": "5511aa02"},
            "mem/bank7-bram.bin": {"bytes": 8192, "crc32": "77c3b118"},
        },
        "subsystems": ["clock", "cpu", "mmu", "nextreg"],
    }


def mutate(doc, path, value):
    """Return a copy of `doc` with one key set (or deleted, when value is None)."""
    out = copy.deepcopy(doc)
    node = out
    for key in path[:-1]:
        node = node[key]
    if value is None:
        del node[path[-1]]
    else:
        node[path[-1]] = value
    return out


def main(argv):
    schema = json.load(open(argv[1]))

    cls = jsonschema.validators.validator_for(schema)
    try:
        cls.check_schema(schema)
    except jsonschema.exceptions.SchemaError as e:
        print("FAIL  the committed .jns schema is not a valid JSON Schema:")
        print("      " + str(e).splitlines()[0])
        return 1
    validator = cls(schema)

    # §12.1 — a reader IGNORES a member it does not recognise, including a
    # whole unknown directory prefix, so the ARCHIVE level must stay open even
    # though each document is closed. An extra member here asserts that
    # directly: a newer jnext's file must still validate against this schema.
    good = {
        "manifest.json": manifest_from_the_spec(),
        "state/from-the-future.json": {"anything": 1},
        "meta/README.txt": "not even JSON, and not our business",
    }
    errs = list(validator.iter_errors(good))
    if errs:
        print("FAIL  a manifest written FROM THE DESIGN DOCUMENT does not")
        print("      validate against the committed schema. Either the schema")
        print("      or this transcription is wrong; both are worth knowing.")
        for e in errs[:6]:
            print("      %s: %s" % (list(e.absolute_path), e.message))
        return 1

    # Each entry: (why it must be rejected, the mutated document).
    # Every one is a SINGLE mutation of the document above, so a rejection can
    # only be the constraint named.
    cases = [
        ("§8: capture.frame_boundary is always true, or the file was not written",
         mutate(good["manifest.json"], ["capture", "frame_boundary"], False)),
        ("§13.2(4): bank5-vram is 16384 bytes by hardware, a literal in the overlay",
         mutate(good["manifest.json"], ["members", "mem/bank5-vram.bin"],
                {"bytes": 16000, "crc32": "5511aa02"})),
        ("§13.2(4): bank7-bram is 8192 bytes",
         mutate(good["manifest.json"], ["members", "mem/bank7-bram.bin"],
                {"bytes": 4096, "crc32": "77c3b118"})),
        ("§13.2(4): ram_kb * 1024 == members['mem/ram.bin'].bytes",
         mutate(good["manifest.json"], ["model", "ram_kb"], 1024)),
        ("§8: a machine this build cannot construct",
         mutate(good["manifest.json"], ["model", "machine"], "spectrum16")),
        ("§7.1: format_version is an integer, not a string",
         mutate(good["manifest.json"], ["format_version"], "1")),
        ("a CRC-32 must be exactly 8 lower-case hex digits",
         mutate(good["manifest.json"], ["members", "mem/ram.bin"],
                {"bytes": 2097152, "crc32": "8F3A21BD"})),
        ("a blob declaration must carry both bytes and crc32",
         mutate(good["manifest.json"], ["members", "mem/ram.bin"],
                {"bytes": 2097152})),
        ("§5.3: mem/ is a CLOSED namespace; a declaration outside it is not one",
         mutate(good["manifest.json"], ["members"],
                {"state/cpu.json": {"bytes": 10, "crc32": "00000000"}})),
        ("an unexpected top-level manifest key is a writer defect",
         mutate(good["manifest.json"], ["surprise"], 1)),
        ("NR 0x07 selects one of four speeds",
         mutate(good["manifest.json"], ["model", "cpu_speed_nr07"], 9)),
        ("§8: producer.jnext_version is required",
         mutate(good["manifest.json"], ["producer", "jnext_version"], None)),
        ("§11.3: a Tier-1 volume id is 8 hex digits or absent, never junk",
         mutate(good["manifest.json"],
                ["media", "sdcard", "identity", "fat32_volume_id"], "nope")),
        ("§8: subsystems is a list of names, and they are unique",
         mutate(good["manifest.json"], ["subsystems"],
                ["clock", "clock"])),
        ("§10.2 P3: a ROM digest is 64 lower-case hex digits or absent",
         mutate(good["manifest.json"],
                ["media", "roms", "sha256", "48.rom"], "deadbeef")),
        ("§10.2 P3: media.roms takes source and sha256 and nothing else",
         mutate(good["manifest.json"], ["media", "roms", "embed"], True)),
        ("§10.2 P4: a tape record without its position is not one",
         mutate(good["manifest.json"],
                ["media", "tape", "position_tstates"], None)),
        ("§10.2 P4: the realtime flag is a boolean, not the string 'true'",
         mutate(good["manifest.json"], ["media", "tape", "realtime"], "true")),
        ("§10.2 P5: the preview is meta/preview.png, at that exact path",
         mutate(good["manifest.json"], ["preview", "path"], "meta/thumb.png")),
        ("§10.2 P5: a preview with no pixels is not a preview",
         mutate(good["manifest.json"], ["preview", "width"], 0)),
        ("§12.1: media is a closed key set; an unknown member of it is a "
         "writer defect",
         mutate(good["manifest.json"], ["media", "cassette"], {})),
        # §6.2's bound, on the SCHEMA side. The C++ reader enforces it too
        # (JNSN-29); this case is what stops the two drifting apart, and it
        # exists because for a while NEITHER of them enforced it while §6.2
        # said both did.
        ("§11.3/§6.2: partition_lba is a 32-bit MBR start LBA, so 2^32 is "
         "past what an MBR can name",
         mutate(good["manifest.json"],
                ["media", "sdcard", "identity", "partition_lba"], 4294967296)),
        ("the manifest itself is required",
         None),
    ]

    failures = []
    for why, mutated in cases:
        doc = {"manifest.json": mutated} if mutated is not None else {}
        if not list(validator.iter_errors(doc)):
            failures.append(why)

    if failures:
        print("FAIL  the committed .jns schema ACCEPTED a document it must")
        print("      reject. A schema that accepts everything passes the")
        print("      staleness byte-diff forever, which is why this matrix")
        print("      exists (§13.2(3)/(4)):")
        for why in failures:
            print("      - " + why)
        return 1

    print("PASS  the committed .jns schema is a valid draft 2020-12 schema,")
    print("      accepts a manifest written from the design document, and")
    print("      rejects %d deliberate faults (python jsonschema %s)"
          % (len(cases), importlib.metadata.version("jsonschema")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
