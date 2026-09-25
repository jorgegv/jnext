#!/usr/bin/env python3
"""Compare libspectrum's extraction of a `.szx` with the spec-written reader's
extraction of a `.jns` — GH #27 stage S9, design §13.2(1).

Two readers, NEITHER of them the writer: `szx_probe` links libspectrum (which
jnext did not write) and `jns_reader.py` is built from the specification. This
file only lines their answers up.

Prints `AGREE`, or one `field: libspectrum=X jns=Y` clause per disagreement.
"""

import json
import sys


def main(argv):
    if len(argv) != 3:
        print("usage: compare_szx_jns.py FOREIGN.txt MINE.json", file=sys.stderr)
        return 2

    with open(argv[1]) as fh:
        foreign = dict(
            line.split("=", 1) for line in fh.read().splitlines() if "=" in line
        )
    with open(argv[2]) as fh:
        mine = json.load(fh)["cpu"]

    if "pc" not in foreign:
        print("libspectrum produced no pc= line", file=sys.stderr)
        return 2

    # A `.szx` carries A and F separately; a `.jns` carries AF as one u16.
    # That encoding difference is BRIDGED here rather than skipped, because
    # dropping the two fields a comparison finds awkward is how a comparison
    # stops discriminating.
    af = mine.get("af")
    hi = None if af is None else (af >> 8) & 0xFF
    lo = None if af is None else af & 0xFF

    checks = {
        "pc": (int(foreign["pc"]), mine.get("pc")),
        "sp": (int(foreign["sp"]), mine.get("sp")),
        "bc": (int(foreign["bc"]), mine.get("bc")),
        "de": (int(foreign["de"]), mine.get("de")),
        "hl": (int(foreign["hl"]), mine.get("hl")),
        "i":  (int(foreign["i"]),  mine.get("i")),
        "im": (int(foreign["im"]), mine.get("im")),
        "a":  (int(foreign["a"]),  hi),
        "f":  (int(foreign["f"]),  lo),
    }

    bad = [f"{k}: libspectrum={v[0]} jns={v[1]}"
           for k, v in checks.items() if v[0] != v[1]]
    print("; ".join(bad) if bad else "AGREE")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
