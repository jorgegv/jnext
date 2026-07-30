# NextSync 1.2 — vendored test fixture

Third-party software under test, not jnext code. Used by the `nextsync-func`
regression row (GH #167) to prove the emulated ESP-01 (`--esp`) carries a real
NextSync file transfer end to end.

## Provenance

| | |
|---|---|
| Program | NextSync 1.2, by Jari Komppa (Sol_HSA) |
| Upstream | `jarikomppa/specnext` — see [doc/REFERENCES.md](../../../doc/REFERENCES.md) |
| Obtained from | release asset `nextsync12.zip`, tag `nextsync_v1.2`, published 2022-11-23 |
| `nextsync12.zip` | `sha256:2357fd4a6979be515f228142b9abef422cb2dcfe243302d652844c4595d415a1` |
| Vendored | 2026-07-30, unmodified |

Contents, byte-for-byte as shipped in that zip:

| File | Size | sha256 |
|---|---|---|
| `dot/sync` | 7604 | `a20e381729c84f4603c0b282d26be995d8b39c9edfe8df103f795550522e1b46` |
| `dot/syncfast` | 7604 | `def65371a9a259e95c618b609aca217523609cb7268557f10978dfc2a1aaaaa2` |
| `dot/syncslow` | 7416 | `0a0e6fd56369ca099099901af02befac90f06bcd3c073a914435434aaefab78b` |
| `server/nextsync.py` | 11763 | `0a9e8c5dc6186d6fac94e18fca130264aa5cc761c951a1b3c1c68bcb57a7b951` |

`LICENSE` is the upstream repository's own licence file.

## Licence

**The Unlicense** — public domain. It grants distribution "either in source code
form or as a compiled binary", which is exactly what is vendored here, so the
three dot commands are as licence-clean as the Python. GPLv3-compatible, so it
satisfies the PR protocol's licence-clean-fixtures rule.

## Why binaries, and why unmodified

The three dot commands are compiled Z80 and **nothing in-tree can rebuild them**
— they need SDCC plus upstream's own build script. That makes them a
no-new-dependency exception, approved by the owner for this fixture on
2026-07-30.

Keeping the dot commands byte-identical to the release is the point of the row:
it tests jnext against the software people actually run. Do not "fix" or
reformat anything in this directory — if it drifts from the released bytes, the
row stops proving what it claims.

**What the row runs, and what it does not.** The `sync` dot command is the
software under test and runs unmodified inside the emulator.
`server/nextsync.py` is *not* executed by the row: it binds the wildcard address
(`s.bind(("", PORT))`, with no option to narrow it), which would expose a
listener on every interface of the developer's machine, and running it as a
subprocess would give the row a child process to orphan. Instead
`test/00regression/nextsync-peer.py` speaks the same protocol directly, binding
only the host's discovered RFC1918 address. This file remains the **protocol
reference** that peer was written from, and it is what the manual recipe in
[NEXTSYNC-VERIFICATION.md](../../../doc/testing/NEXTSYNC-VERIFICATION.md) runs —
so the real server is still exercised, by hand, against the same dot.

That split does not weaken the row. jnext's ESP emulation only ever sees a TCP
byte stream, and the dot verifies a checksum and a packet number on every packet
— a peer that framed anything wrongly would show up immediately as retries or a
bad hash, both of which the row asserts against.

## The three variants

They differ only in the UART speed they ask the ESP for. All three were verified
by hand (see [NEXTSYNC-VERIFICATION.md](../../../doc/testing/NEXTSYNC-VERIFICATION.md));
the row runs `sync`, the default.

| dot | `AT+UART_CUR=` |
|---|---|
| `sync` | 1152000 |
| `syncfast` | 2000000 |
| `syncslow` | *(none — stays at 115200)* |
