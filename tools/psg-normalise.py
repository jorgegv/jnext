#!/usr/bin/env python3
"""Cut a .psg register stream down to its comparable body.

    psg-normalise.py IN OUT

Layout read (Fuse 1.6.0's AY log): a 16-byte header; then 0xFF is one empty frame, "0xFE n"
is 4n empty frames, a byte below 0x10 is a register followed by its value byte; anything else
is an error. The body is the raw byte slice from the SENTINEL to the end of the last register
pair, written to OUT. The sentinel is a block made of exactly one pair (07, 3F) followed by
separators totalling exactly 9 frames; exactly one must exist. What precedes it (the lead-in
before the recording was in step with the program) and the trailing run (which depends on where
the recording was stopped) are cut; every separator in between is kept as encoded.

The C++ twin is psg_normalise() in test/debug/audio_debug_test.cpp (PSF-01..03); both are pinned
to the same body length. Exit status: 0 written, 2 a parse error or a sentinel count other than
one (with a message on stderr), 1 an I/O error.
"""
import sys


def parse(data):
    if len(data) < 16:
        raise ValueError("shorter than the 16-byte header")
    toks = []   # (is_separator, frames, begin, end, reg, value)
    i = 16
    while i < len(data):
        c = data[i]
        if c == 0xFF:
            toks.append((True, 1, i, i + 1, 0, 0))
            i += 1
        elif c == 0xFE:
            if i + 1 >= len(data):
                raise ValueError("truncated separator at byte %d" % i)
            toks.append((True, 4 * data[i + 1], i, i + 2, 0, 0))
            i += 2
        elif c < 0x10:
            if i + 1 >= len(data):
                raise ValueError("truncated register pair at byte %d" % i)
            toks.append((False, 0, i, i + 2, c, data[i + 1]))
            i += 2
        else:
            raise ValueError("byte 0x%02X at %d is neither a register nor a separator" % (c, i))
    return toks


def normalise(data):
    toks = parse(data)
    found = []
    for i, t in enumerate(toks):
        if t[0] or t[4] != 7 or t[5] != 0x3F:
            continue
        if i > 0 and not toks[i - 1][0]:
            continue                              # not alone: a pair before it
        if i + 1 >= len(toks) or not toks[i + 1][0]:
            continue                              # a pair after it, or the end
        frames = 0
        j = i + 1
        while j < len(toks) and toks[j][0]:
            frames += toks[j][1]
            j += 1
        if frames == 9:
            found.append(i)
    if len(found) != 1:
        raise ValueError("%d sentinels found, want exactly 1" % len(found))
    last = len(toks)
    while last > 0 and toks[last - 1][0]:
        last -= 1
    return data[toks[found[0]][2]:toks[last - 1][3]]


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: psg-normalise.py IN OUT\n")
        return 2
    try:
        with open(argv[1], "rb") as f:
            data = f.read()
    except OSError as e:
        sys.stderr.write("psg-normalise: %s\n" % e)
        return 1
    try:
        body = normalise(data)
    except ValueError as e:
        sys.stderr.write("psg-normalise: %s: %s\n" % (argv[1], e))
        return 2
    try:
        with open(argv[2], "wb") as f:
            f.write(body)
    except OSError as e:
        sys.stderr.write("psg-normalise: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
