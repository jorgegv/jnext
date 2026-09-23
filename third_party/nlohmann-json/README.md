# nlohmann/json — vendored single header

Used by the `.jns` snapshot container (`src/save/jns_container.cpp`) to write
and parse `manifest.json` and, from stage S2 onward, the per-subsystem
`state/*.json` members. Design rationale:
[`doc/design/NEXT-SNAPSHOT-FORMAT.md`](../../doc/design/NEXT-SNAPSHOT-FORMAT.md)
§14.1.

| | |
|---|---|
| Upstream | <https://github.com/nlohmann/json> |
| Version | **3.11.3** |
| File | `nlohmann/json.hpp` (the upstream `single_include` amalgamation) |
| SHA-256 | `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6` |
| Licence | MIT — `LICENSE.MIT`, unmodified |
| Local changes | **None.** Byte-identical to upstream. |

The header was fetched from two independent upstream URLs — the `v3.11.3` tag
in the source tree and the `v3.11.3` release asset — and the two were compared
byte for byte before being committed. Re-verify with:

```sh
sha256sum third_party/nlohmann-json/nlohmann/json.hpp
```

## Why vendored rather than a package dependency

The same reason the ZIP container is first-party: a `Requires:` line would have
to be added to `packaging/rpm/jnext.spec`, `packaging/debian/`, the Flatpak
manifest, the Homebrew macOS leg and the MinGW Windows cross-build, and
`nlohmann-json` is packaged under different names across them. It is one
self-contained MIT header with no build system, which is the cheapest possible
thing to vendor. The tree already vendors four third-party trees (`spdlog`,
`fatfs`, `fuse-z80`, `zot`), of which spdlog is far larger.

## Why not a hand-rolled parser

The *writer* half is genuinely trivial — jnext controls every value it emits.
The *parser* is the risky half: escape handling, UTF-8 and number edge cases
are about 500 lines whose bugs stay invisible until a hand-edited file arrives,
which this format explicitly invites (a `.jns` is meant to be inspectable and
editable with `jq` and a text editor). nlohmann also round-trips `uint64_t`
exactly, which a naive parser does not.

## Confinement

This is a heavy header to compile, so it is included by **exactly one
translation unit**, `src/save/jns_container.cpp`. Nothing else in jnext
includes it, and no public header exposes an `nlohmann::` type — `Manifest` and
everything around it are plain C++ structs. Grep before adding a second
include site:

```sh
grep -rn 'nlohmann' --include='*.cpp' --include='*.h' src/ test/
```
