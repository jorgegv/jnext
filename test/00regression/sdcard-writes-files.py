#!/usr/bin/env python3
"""List the kept-file records of a jnext SD-writes file: the FOREIGN reader.

    python3 -I sdcard-writes-files.py <writes-file>

Written from the format table in the developer guide (media chapter, "The SD
card's write journal"), not from the C++ that writes the file:

    0   8   magic "JNEXTSDW"
    8   4   format version = 1          (little-endian)
    12  4   record payload size = 512
    16  8   image size in bytes
    24  8   image mtime (0 when bound by digest)
    32  32  image SHA-256 (all zero = bound by size + mtime)
    64  records, in commit order: a sector record (516 bytes: sector u32 <
        0xFFFFFFFF + 512 bytes) or a kept-file record (0xFFFFFFFF u32, L u32,
        n u16, the name, the file's bytes: L - 2 - n of them)

A later kept-file record supersedes an earlier one (there is one kept file per
journal; the file on disk may hold superseded ones until the next bind compacts it).
Prints "sectors=<count of sector records>" and then, when there is a kept file,
the last one: "file <name> <bytes in hex>". Exit status 1 on anything the table
does not allow.
"""
import struct
import sys


def main():
    with open(sys.argv[1], "rb") as f:
        j = f.read()
    if len(j) < 64 or j[:8] != b"JNEXTSDW":
        sys.exit("not a journal")
    version, payload = struct.unpack_from("<II", j, 8)
    if version != 1 or payload != 512:
        sys.exit("unknown version or payload size")
    sectors = 0
    files = []
    pos = 64
    while pos < len(j):
        (first,) = struct.unpack_from("<I", j, pos)
        if first != 0xFFFFFFFF:
            if pos + 516 > len(j):
                sys.exit("a partial record")
            sectors += 1
            pos += 516
            continue
        (length,) = struct.unpack_from("<I", j, pos + 4)
        if length < 2 or pos + 8 + length > len(j):
            sys.exit("a bad kept-file record")
        (n,) = struct.unpack_from("<H", j, pos + 8)
        if n > length - 2:
            sys.exit("a bad name length")
        name = j[pos + 10 : pos + 10 + n].decode("ascii")
        data = j[pos + 10 + n : pos + 8 + length]
        files = [(name, data)]
        pos += 8 + length
    print("sectors=%d" % sectors)
    for name, data in files:
        print("file %s %s" % (name, data.hex()))


if __name__ == "__main__":
    main()
