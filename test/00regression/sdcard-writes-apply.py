#!/usr/bin/env python3
"""Lay a jnext SD-writes file over a copy of its image: the FOREIGN reader.

    python3 -I sdcard-writes-apply.py <image-copy> <writes-file>

Written from the format table in the developer guide (media chapter, "The SD
card's write journal"), not from the C++ that writes the file:

    0   8   magic "JNEXTSDW"
    8   4   format version = 1          (little-endian)
    12  4   record payload size = 512
    16  8   image size in bytes
    24  8   image mtime (0 when bound by digest)
    32  32  image SHA-256 (all zero = bound by size + mtime)
    64  516 x n  records: sector u32 + 512 bytes; a later record wins

The image copy is rewritten in place. Exit status 1 on anything the table does
not allow.
"""
import struct
import sys


def main():
    img_path, jf_path = sys.argv[1], sys.argv[2]
    with open(jf_path, "rb") as f:
        j = f.read()
    if len(j) < 64 or j[:8] != b"JNEXTSDW":
        sys.exit("not a journal")
    version, payload = struct.unpack_from("<II", j, 8)
    if version != 1 or payload != 512:
        sys.exit("unknown version or payload size")
    (size,) = struct.unpack_from("<Q", j, 16)
    body = j[64:]
    if len(body) % 516:
        sys.exit("a partial record")
    with open(img_path, "r+b") as img:
        img.seek(0, 2)
        if img.tell() != size:
            sys.exit("the image is not the size the journal was made for")
        for off in range(0, len(body), 516):
            (sector,) = struct.unpack_from("<I", body, off)
            img.seek(sector * 512)
            img.write(body[off + 4 : off + 516])


if __name__ == "__main__":
    main()
