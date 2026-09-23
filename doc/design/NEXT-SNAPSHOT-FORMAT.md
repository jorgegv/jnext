# Next-Specific Snapshot Save/Load Format — design

> Status: **design only, not implemented**. Tracking issue:
> [#27](https://github.com/jorgegv/jnext/issues/27). Milestone v1.1.
>
> This document is a proposal for the owner to review before any code is
> written. §18 lists the questions it cannot answer on its own.
>
> Working name for the format: **JNS** (`.jns`), *jnext snapshot*.

---

## Table of contents

- [1. What is already settled](#1-what-is-already-settled)
- [2. Goals and non-goals](#2-goals-and-non-goals)
- [3. What exists today, and what it does not cover](#3-what-exists-today-and-what-it-does-not-cover)
- [4. The state, measured](#4-the-state-measured)
- [5. The container decision](#5-the-container-decision)
- [6. On-disk layout](#6-on-disk-layout)
- [7. The version field and its rules](#7-the-version-field-and-its-rules)
- [8. Identity and provenance](#8-identity-and-provenance)
- [9. Decoupling: one field list, two encodings, a generated schema](#9-decoupling-one-field-list-two-encodings-a-generated-schema)
- [10. The complete state inventory](#10-the-complete-state-inventory)
- [11. The SD card question](#11-the-sd-card-question)
- [12. Compatibility rules and the unknown-member rule](#12-compatibility-rules-and-the-unknown-member-rule)
- [13. Validation: what substitutes for a foreign reader](#13-validation-what-substitutes-for-a-foreign-reader)
- [14. Dependencies](#14-dependencies)
- [15. GUI and CLI integration surface](#15-gui-and-cli-integration-surface)
- [16. Test plan sketch](#16-test-plan-sketch)
- [17. Staged implementation plan and effort](#17-staged-implementation-plan-and-effort)
- [18. Open questions for the owner](#18-open-questions-for-the-owner)

---

## 1. What is already settled

The 2026-08-12 comment on issue #27 records a design conversation with Janko,
Ped7g and SevevFFF. Its conclusions are **binding inputs to this document**, not
open questions. Quoted here so the next reader does not reopen them:

1. **Not an interchange format.** "Much Next state is write-only on real
   hardware; KS1/KS2 have no FPGA room for snapshot support; and re-entering a
   paused copper/DMA at the exact cycle it was snapshotted is not feasible even
   if there were. The format is emulator-only by construction."
2. **Not shareable.** "A running Next holds NextZXOS/distro code in RAM. Old
   Spectrum snapshots never contained the OS — it was in ROM, under a different
   licence… This removes the reason to formalise anything and leaves the real
   use case intact: **save a running program and resume it later, locally.**"
3. **NEX is out as a base.** "NEX is a distribution/resource bundle, not machine
   state — hardly a quarter of what is needed… Extending it buys the wrong
   semantics and no useful tooling."
4. **The community-agreement gate is dropped.** "This is no longer an
   interchange format, just a normal feature."
5. **No cross-version guarantee.** "The MAME posture — snapshots deliberately
   invalidated by any internal-structure change — is acceptable for 'resume my
   game today'. jnext can do better than that, stable while the emulated core
   model does not change, but old snapshots will eventually fall out of spec, so
   the snapshot must stamp the jnext version and the core revision it models."
6. **Compression is fine.** "The ZX0-chunk idea answered that same now-absent
   constraint and is dropped. An uncompressed write mode is still worth having
   as a debugging convenience."
7. **Borrow SZX's container conventions, not SZX itself.** "Chunked, versioned,
   'unknown chunk types must be ignored' is the right shape and is what keeps
   subsystems addable later."

Two further constraints come from the owner, and are **firm requirements**:

- **F1 — an explicit version field**, frozen unless the format itself changes.
  §7 defines exactly what bumps it, what does not, and how a reader behaves on a
  version it does not know.
- **F2 — decoupling.** The on-disk format must not be a memory dump of whatever
  each subsystem's C++ struct happens to look like today. A subsystem must be
  able to change its in-memory representation without silently changing or
  breaking the file. §9 is the mechanism.

And one genuine exploration, with a recommendation required: **JSON + JSON
Schema inside a ZIP container.** §5 evaluates it, §14 prices it.

---

## 2. Goals and non-goals

### Goals

| # | Goal |
|---|---|
| G1 | Capture the **complete** emulated machine — everything a resumed program can observe — and restore it so the program continues as though nothing happened. |
| G2 | **Decoupled** on-disk representation: field names, not struct offsets (F2). |
| G3 | An explicit, frozen **format version** with defined reader behaviour (F1). |
| G4 | **Verifiable by something that is not jnext** — this is the owner's stated motive, and it is the direct answer to the `.szx` failure recorded in §13. |
| G5 | **Self-describing and inspectable** with tools a user already has (`unzip`, `jq`, a text editor), with no jnext-specific tool required. |
| G6 | **Provenance stamped**: jnext version, state-model revision, machine type, and the identity of every external resource the machine depends on (SD image, ROMs, tape). |
| G7 | **Additive evolution without a version bump**: a new subsystem, or a new field in an existing one, must be addable without invalidating old files or bumping the format version. |
| G8 | A **compressed default** with an **uncompressed debug mode** (settled point 6). |
| G9 | **Fail loudly, never silently wrong.** A snapshot that cannot be restored faithfully must say so and refuse, not restore something plausible. |

### Non-goals

| # | Non-goal | Why |
|---|---|---|
| N1 | Real-hardware restore | Settled point 1. |
| N2 | Interchange with CSpect / ZEsarUX / any other emulator | Settled points 1 and 2. Nothing here is designed for a foreign implementation to read, and no stability is promised to one. |
| N3 | Shareable / publishable snapshots | Settled point 2. **A `.jns` of a NextZXOS session contains NextZXOS and DivMMC ROM content in RAM.** Same rule as the warm-start cache: generated locally, never vendored, never committed, never packaged. Documented for the user in the user guide. |
| N4 | Archival / preservation format | The original media (`.tap`/`.tzx`/`.nex`) is the preservation copy. |
| N5 | Perpetual backward compatibility | Settled point 5. §7 and §12 define what *is* promised. |
| N6 | Extending or subsuming NEX | Settled point 3. |
| N7 | Sub-frame capture | Snapshots are taken at frame boundaries, exactly like the rewind ring, and for the same reason: the scheduler queue is empty there, so it never has to be serialised. See §10. |
| N8 | Replacing SNA/SZX/Z80 save | Those stay for classic-machine interchange. `.jns` is the Next-complete option; the existing **File ▸ Save Snapshot…** extension dispatch simply gains one more arm. |

---

## 3. What exists today, and what it does not cover

### 3.1 The `Saveable` layer (`src/core/saveable.h`)

Despite the file name there is **no `Saveable` base class**. There are two
concrete helpers and a convention:

- `StateWriter` writes into a pre-allocated buffer, or counts bytes when
  constructed with a null pointer (*measure mode*).
- `StateReader` reads back from a const buffer.
- Both are bounds-disciplined: an out-of-range write is suppressed, an
  out-of-range read is zero-filled, and either latches a sticky
  `overflow()` / `out_of_bounds()` flag (Task 60b).
- Each subsystem simply *has* `save_state(StateWriter&) const` and
  `load_state(StateReader&)`.

`Emulator::save_state()` calls the subsystems in a fixed order — **32 blocks,
covering 34 classes**, since `Renderer` carries `Ula` and `LoRes` — and writes a
`u32` sentinel (`kStateSentinelMagic ^ ordinal`) after each block, so a desync
is reported by name rather than silently deserialised.

**This is the foundation, and it is good at what it was built for.** It was
built for the rewind ring: an in-process, same-build, fixed-width, maximally
fast byte stream. Every property that makes it right there makes it wrong as a
file format:

| Property | Right for rewind | Wrong for a file |
|---|---|---|
| **Positional** — a field's identity is its offset, which is its position in the source | Zero framing overhead | Reordering two lines in a `.cpp` silently re-means an existing file. This is G66 exactly. |
| **Fixed-width** — `take_snapshot()` refuses any slot whose length differs from the construction-time measure | Makes ring slots possible | Forbids optional and variable-length fields, which a file wants |
| **Append-with-`eof()`-tolerance** — the only way to add a field | Cheap and workable in-process | Produces the tail seen in `Emulator::save_state` today: `port_ff_reg_`, `nr_10_coreid_`, the G55 IO-trap trio, `nr_2d_i2s_sample_`, `nr_a2_ctl`, `nr_a0_pi_peripheral_en_` — a dozen scalars whose position on disk is a chronology of when they were written, not a structure |
| **No magic, no version, no schema** | Never leaves the process | G66's entire complaint |

The G66 tombstone (`doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md:1771`) states
the hazard: "ANY change to subsystem `save_state` field order / type / count
silently corrupts older snapshots and may crash/UB on rewind reload." It was
reclassified to this issue on 2026-07-15 precisely because fixing it properly
*is* designing this format.

**What the stream does NOT contain** (from its own header comment, verified):
`rom_`, `contention_`, `port_`, `mixer_`, `debug_state_`, `trace_log_`,
`call_stack_`, `tape_`/`tzx_tape_`/`wav_tape_`, `video_recorder_`,
`rzx_player_`/`rzx_recorder_`, **`sd_card_`**, `boot_rom_`, `framebuffer_`, the
raster transients, `frame_ts_start_`, and the host-side input dispatchers.
§10 goes through each one and says whether a snapshot needs it.

### 3.2 The warm-start state cache (`src/core/warm_start_cache.{h,cpp}`, v1.0.15)

Landed 2026-09-22/23. It is the closest thing in the tree to what #27 needs: it
already turns the `Saveable` stream into a persisted, versioned,
zlib-compressed on-disk artefact with an identity header.

**What #27 should reuse, verbatim in spirit and mostly in code:**

- **Identity-before-payload ordering.** The 96-byte header is plain, the payload
  deflated, so every refusal (wrong magic, wrong machine, wrong digest, wrong
  length, truncation) is answerable before a byte of unvalidated input is
  inflated. A guard that runs after the thing it guards is not one.
- **Exact-length inflation.** `uncompress()` refuses a stream wanting *more*
  room than given, but is happy with one reaching `Z_STREAM_END` *short* — and a
  short stream is precisely the shape that deserialises into the wrong fields
  instead of failing. JNS inherits the rule: every member's inflated length is
  compared for equality with its declared length.
- **`sha256_file()` / `sha256_hex()`** (`src/core/sdcard_provisioner.h`),
  already cross-platform (OpenSSL on Linux/macOS, BCrypt/CNG on Windows).
- **A mismatch discards rather than adapts.** "A fixture is a file somebody
  keeps correct; a recording is one nothing can keep incorrect."
- **zlib, not zstd.** zlib is already a required dependency
  (`find_package(ZLIB REQUIRED)`, CMakeLists.txt:151); a new compressor for the
  last few percent is not a trade this project makes.

**What #27 must do differently:**

| Warm-start cache | JNS |
|---|---|
| Two version numbers, and the distinction is correct but was learned the hard way: the magic's trailing digit versions the **file layout**, `kFormatVersion` versions the **state stream**. Bumping one and not the other was a deliberate, documented call. | The same split, but named rather than encoded in a digit of a magic string, and with the rules written down *before* the first bump (§7). `format_version` and `state_model_revision` are both explicit manifest keys. |
| **Positional payload.** `kFormatVersion` exists exactly because the stream's meaning "lives in the code". A field repurposed or two slots swapped is invisible to the length check. | **Named payload** (§9). Swapping two fields is not expressible: they have names. This retires the class of failure `kFormatVersion` exists to catch, rather than versioning around it. |
| **Length is part of the identity** — refusing any stream whose length is not exactly this build's is what stops `Ram::load_state` writing past the live buffer. | Per-member declared lengths do the same job better: a length is declared *per buffer*, so a mismatch names the buffer instead of the whole file. |
| Single-purpose, single-file, one per machine type, overwritten in place. Identity = SD digest + machine + format version + stream length. | User-named files anywhere on disk; identity is provenance to be **reported**, not a key to be matched (§8). |
| **SD image hashed whole**, affordable because a boot does not write. | The same digest is *not* an adequate identity here, because a snapshot session does write. §11. |

**One thing the warm-start cache should eventually take back from JNS**: if the
descriptor layer of §9 lands, `kFormatVersion` can be derived from the
descriptors rather than hand-bumped, and the "a field repurposed is invisible"
hazard disappears there too. Out of scope for this issue; noted so it is not
lost.

### 3.3 The existing loaders and savers

`src/core/` holds `nex_loader`, `nex_saver`, `sna_loader`, `sna_saver`,
`szx_loader`, `szx_saver`, `z80_loader`, `tap_*`, `tzx_loader`, `wav_loader`,
`rzx_*`. Integration points JNS inherits for free:

- **Load by extension**: `Emulator::init()` lower-cases
  `std::filesystem::path::extension()` and dispatches (`emulator.cpp:1153`).
- **Save by extension**: `MainWindow::on_save_snapshot()`
  (`main_window.cpp:1763`) picks `SzxSaver` / `NexSaver` / `SnaSaver` from the
  chosen suffix, defaulting to `.sna`.
- **Headless**: `--delayed-snapshot FILE` + `--delayed-snapshot-frames N`,
  format again chosen by extension.

`SzxSaver` also carries the honesty precedent this design should follow: it
**refuses** the Next machine outright rather than writing a file that
misrepresents it, because `.szx` can only express 48K/128K/+2A/+3. JNS is the
other side of that coin — the format that *can* express the Next — and it should
refuse, symmetrically, anything it cannot represent faithfully.

---

## 4. The state, measured

Numbers matter here, because they decide §5. Measured against **this tree**
(`main` @ `15430513`) on 2026-09-23, by running `StateWriter` in measure mode
over `Emulator::save_state` — which is what `rewind_test` already prints as
`Snapshot size:` — and cross-checked against the `plain_bytes` header of the
warm-start cache on this box:

**The full `Emulator::save_state` stream is 2 292 965 bytes.**

Of that, the flat buffers are exactly (constants, not estimates):

| Buffer | Declared size | Bytes |
|---|---|---|
| `Ram::data_` | `2048 * 1024` + 8-byte count prefix | 2 097 160 |
| `DivMmc` RAM | `kRamSize` = 128 KB | 131 072 |
| `SpriteEngine::pattern_ram_` | `PATTERN_RAM_SZ` = 16 384 | 16 384 |
| `Multiface` RAM | `kRamSize` = 0x2000 | 8 192 |
| `PaletteManager` | 4 palettes × 2 banks × 256 × `u16` + 2 × 256 priority | 4 608 |
| `Copper::instructions_` | 1024 × `u16` | 2 048 |
| `SpriteEngine` attributes | 128 × 5 | 640 |
| `NextReg::regs_` | 256 | 256 |
| **Total flat buffers** | | **2 260 360** |

**Residual — every register, latch, FSM state, counter and flag in all 34
subsystems plus `Emulator`'s own scalars — is 32 605 bytes, 1.42 % of the
stream.** The eight buffers above are the other 98.58 %.

Two findings fall out of this, both worth acting on:

1. **≈ 139 KB of the stream is stored twice.** `Emulator::init()` does
   `divmmc_.set_ram_backing(ram_.page_ptr(16))` (emulator.cpp:279) and, on the
   machines that have one, `multiface_.set_ram_backing(ram_.page_ptr(0x0B))`
   (:301). Both subsystems then serialise through `ram_data()`, which returns
   the *external* pointer — i.e. a window into the same 2 MB `Ram` already
   written. The duplication is harmless in a ring buffer and free after deflate,
   but a named format must not inherit it: JNS references the RAM pages instead
   of copying them. (**To be verified during implementation** — the DivMMC
   backing looks unconditional, the Multiface one is machine-type gated.)
2. **The interesting state is small.** 32 KB of scalars is what a human,
   a schema, a diff and a validator actually want to look at. Encoding *that*
   as JSON costs a few hundred KB of text and is trivially affordable. Encoding
   the 2.26 MB of opaque guest memory as JSON is not, and buys nothing — a JSON
   Schema has nothing to say about the contents of RAM beyond "2 097 152 numbers
   in 0…255".

That asymmetry is the whole argument of §5.

---

## 5. The container decision

### 5.1 The options, honestly

**Option A — first-party chunked binary (SZX-style).**
`"JNS1"` magic, `u32` version, then `[tag u32][len u32][payload]` records.

- *Size*: smallest possible. *Speed*: fastest possible.
- *Dependencies*: **none.** zlib for the payloads, already required.
- *Effort*: small — ~150 lines. The warm-start cache is 80 % of it already.
- *Against*: nothing outside jnext can read it. Verification is our own code
  checking our own output — the exact shape of the `.szx` failure (§13). A diff
  of two snapshots is a hexdump. Inspecting one needs a tool we have to write,
  document and maintain. Decoupling (F2) is *possible* — tags are names — but it
  is not enforced by anything, so nothing stops a payload being a struct dump,
  which is how the current stream got where it is.

**Option B — pure JSON (whole state), ZIP- or gzip-compressed.**

- *Against, decisively*: the 2.26 MB of flat buffers become ~3.0 MB as base64 or
  ~6–7 MB as decimal arrays, before compression, and cost a parse of every one
  of two million values on load. The gain over Option C for those bytes is
  **zero**, because a schema cannot say anything useful about them. Paying
  megabytes and a parse for no verification is not a trade.
- A second, quieter problem: JSON has one number type. `nlohmann` keeps
  `uint64_t` exactly, but an *external* validator written in JavaScript (`ajv`)
  would silently round anything above 2^53. See §7.4 for the rule this imposes.

**Option C — ZIP container: JSON manifest + JSON per-subsystem state + binary
blob members for the flat buffers.** ← **recommended**

- *Size*: the 1.42 % that is scalars becomes ~200–400 KB of JSON text; the
  98.58 % stays binary. Both are deflated by the ZIP itself. Expected file:
  **~150–250 KB** for a full 2 MB-RAM Next snapshot, against the 126 KB the
  warm-start cache measures for the same machine as one opaque deflate stream.
  The delta is the price of the text, and it is a price worth paying.
- *Speed*: one deflate pass over the same bytes, plus a parse of a few hundred
  KB of JSON. Sub-100 ms, against a ~2.3 MB serialise that is already
  milliseconds.
- *Verifiability*: `unzip -l`, `unzip -t` (CRC-checks every member, using code we
  did not write), `unzip -p snap.jns manifest.json | jq`, and a JSON Schema
  judged by an independent validator. All five of G4/G5's tools already exist on
  the user's machine.
- *Compression mode*: the uncompressed debug mode (settled point 6) is the ZIP
  `STORED` method instead of `DEFLATE` — a flag on the member writer, **not a
  second code path**. That is a genuinely elegant consequence of picking ZIP,
  and it means the debug mode is exercised by the same reader.
- *Unknown-chunk rule*: ZIP members are **named**, so "ignore what you do not
  know" is the natural reading, and directory prefixes (`state/`, `mem/`,
  `meta/`) give the namespace SZX's four-character tags approximate with a
  registry. §12.
- *Extras that cost nothing*: a `meta/preview.png` thumbnail (jnext already
  writes PNG, `src/platform/screenshot.cpp`) makes every file manager a snapshot
  browser. A `meta/README.txt` can carry the "this contains firmware, do not
  share it" warning **inside the file**, where a user who has forgotten will
  actually meet it.
- *Against*: a ZIP reader/writer is ~600 lines of first-party code (§14), and
  the format is more moving parts than Option A. Both are real costs and neither
  is large.

**Option D — ZIP container, but all members binary (no JSON).** Gets the
inspectable container without the inspectable content. Rejected: the members
would still be struct dumps, so F2 is unmet and G4 is unmet. It would be Option
A wearing a ZIP.

### 5.2 Recommendation

**Option C.** The deciding argument is §4: the state divides cleanly, and
almost perfectly, into a tiny part that benefits enormously from being named,
typed and externally validatable, and a huge part that benefits not at all. A
hybrid is not a compromise here — it is the shape of the data.

The trade-off, stated plainly: **a `.jns` is roughly 1.5–2× the size of the
smallest possible binary encoding of the same machine, and JNS carries ~600
lines of container code plus one vendored JSON header that a first-party chunk
format would not need.** In exchange, a snapshot is something a user can open
with tools they already have, a test can validate with an implementation we did
not write, and a reviewer can read in a diff. For a local save-state measured in
hundreds of kilobytes, that is not a close call.

### 5.3 What the schema can and cannot do

The owner's motive is verifiability, so this needs to be exact.

**A JSON Schema validates**: the presence and absence of keys; types; numeric
ranges (`minimum`/`maximum` per hardware register width); string patterns
(exact-length hex strings, so a 2 KB fixed array is length-checked *by the
schema*); enumerations (machine types, FSM state names); array lengths; and —
via `additionalProperties: false` where we want it — that no unexpected key
appeared. It also makes the file **self-describing**: the schema ships beside
the format and answers "what is this field?" without reading jnext's source.

**A JSON Schema cannot validate a blob's contents.** Nothing can, short of
running the machine. What it *can* do, and what this design has it do, is
validate the blob's **declaration**: the manifest's `members` object names each
blob member, its exact byte length and its CRC-32, and the schema pins the shape
and the pattern of those declarations. The contents are then covered by two
independent mechanisms:

- the **ZIP's own per-member CRC-32**, checked by `unzip -t` and by Python's
  `zipfile.testzip()` — neither of which is our code; and
- **the reader's equality check** on the inflated length, per member.

So the chain is: schema validates the declaration, an independent ZIP reader
validates the bytes against the declaration, and only then does jnext interpret
them. What remains unvalidatable — whether the 2 MB of RAM is the *right* 2 MB —
is answered by the semantic round-trip tests of §16, not by any format
mechanism, and this document does not pretend otherwise.

---

## 6. On-disk layout

A `.jns` is a ZIP archive. Members, in write order:

```
snapshot.jns
├── manifest.json               REQUIRED, first member, always DEFLATE or STORED
├── state/clock.json            one per subsystem, named for the subsystem
├── state/mmu.json
├── state/nextreg.json
├── state/cpu.json
├── state/im2.json
├── state/palette.json
├── state/layer2.json
├── state/sprites.json          (attributes + registers; patterns are a blob)
├── state/tilemap.json
├── state/renderer.json
├── state/ula.json
├── state/lores.json
├── state/copper.json           (registers + the 1K instruction RAM as hex)
├── state/ctc.json
├── state/dma.json
├── state/spi.json
├── state/sdcard.json           NEW — see §11
├── state/i2c.json
├── state/rtc.json
├── state/uart.json
├── state/divmmc.json           (registers only; its RAM is a RAM window, §4)
├── state/multiface.json
├── state/nmi_source.json
├── state/beeper.json
├── state/turbosound.json       (3 × AY inside)
├── state/dac.json
├── state/i2s.json
├── state/keyboard.json
├── state/joystick.json
├── state/mouse.json
├── state/md6.json
├── state/membrane_stick.json
├── state/iomode.json
├── state/joy_uart.json         (present only when a cable is attached)
├── state/esxdos_hostfs.json    (handles + cwd + root)
├── state/emulator.json         (Emulator's own scalars, §10)
├── mem/ram.bin                 2 MB (or the configured size)
├── mem/sprite-patterns.bin     16 KB
├── meta/preview.png            OPTIONAL — the framebuffer at capture time
└── meta/README.txt             OPTIONAL — human-readable "do not share" notice
```

Rules:

- **`manifest.json` is member 0.** A reader parses it before touching anything
  else, exactly as the warm-start header is read before the payload is inflated.
- **The archive comment** (ZIP EOCD comment field) is
  `jnext-snapshot format=1`, so `unzip -z snapshot.jns` identifies the file and
  its grammar without a JSON parse, and a truncated file is still classifiable.
- **Member paths are lower-case, `/`-separated, no leading `/`, no `..`.** A
  reader refuses any archive containing a member path that does not match
  `^[a-z0-9][a-z0-9._/-]*$` — a snapshot is not an extraction target, and zip-slip
  is not a hazard we accept even in a private format.
- **Order is the write order above, but a reader must not depend on it.** It is
  chosen so `unzip -l` reads sensibly and so `manifest.json` is first.
- **Members may be `STORED` or `DEFLATE`.** Default `DEFLATE` level 9 (the
  warm-start cache's reasoning applies: written once, read often).
  `--snapshot-uncompressed` writes everything `STORED`, which is the settled
  point-6 debugging mode and also makes `.jns` files `zipdetails`-friendly.
- **ZIP64 is not written and is refused on read.** No member can approach 4 GB;
  writing ZIP64 would add a second framing to test for no reachable benefit.
  Rejecting it is one check.

### 6.1 Encoding rules inside the JSON

| Kind | Encoding | Schema can check |
|---|---|---|
| Boolean flag | `true` / `false` | type |
| Register / counter ≤ 32 bits | JSON number, decimal | type, `minimum`, `maximum` |
| Counter that may exceed 2^53 | JSON **string**, decimal digits | `pattern: "^[0-9]+$"` — see §7.4 |
| Enum / FSM state | JSON string from a closed set | `enum` — this is worth a lot: an FSM renumbering becomes a *name* change, visible in a diff |
| Fixed array ≤ 8 KB | one lower-case hex string, no separators | `pattern: "^[0-9a-f]{N}$"` with N literal — **exact length checked by the schema** |
| Variable-length list | JSON array of objects | `items`, `minItems`/`maxItems` |
| Opaque buffer > 8 KB | ZIP member, declared in `manifest.members` | the *declaration*, not the bytes (§5.3) |

The 8 KB threshold is a judgement, not a law: below it the hex string is small
enough to be diffed and schema-length-checked, above it the text doubles a
buffer nobody will read. `NextReg::regs_` (256 B), `Copper::instructions_`
(2 KB as 4096 hex chars), the palettes (4.6 KB) and the DivMMC/Multiface
register sets all land in JSON; `Ram` and the sprite pattern RAM are blobs.

A worked example — `state/copper.json`:

```json
{
  "pc": 312,
  "mode": "wait_for_vpos",
  "last_mode": "stop",
  "write_addr": 32,
  "write_data_stored": 195,
  "offset": 0,
  "instructions": "0000ffff1234...<4096 hex chars>"
}
```

and its manifest declaration of the one blob it does not hold:

```json
"members": {
  "mem/ram.bin":             { "bytes": 2097152, "crc32": "8f3a21bd" },
  "mem/sprite-patterns.bin": { "bytes": 16384,   "crc32": "0c19ee40" }
}
```

---

## 7. The version field and its rules

This is firm requirement **F1**. There are **two** numbers, and conflating them
is exactly the mistake the warm-start cache had to reason its way out of.

### 7.1 `format_version` — the frozen one

```json
"format_version": 1
```

It versions the **grammar**: the member namespace, the required manifest keys,
the encoding rules of §6.1, and the unknown-member rule of §12. It says nothing
about the emulated machine.

**It is frozen.** It changes only when a reader built for version N could not
correctly read a version N+1 file.

**Does NOT bump it** (this list is the point of the design — G7):

- adding a new `state/<subsystem>.json` member;
- adding a key to an existing subsystem object;
- removing a subsystem or a key (readers default it — §12);
- adding a new optional manifest key;
- adding a new blob member;
- changing what a field *means in the emulator* — that is `state_model_revision`;
- adding a machine type to the `machine` enum;
- switching a member between `STORED` and `DEFLATE`.

**Does bump it:**

- changing the type or meaning of an **existing** manifest key;
- changing an encoding rule (hex → base64; number → string for a field that was
  already a number);
- changing the member-naming scheme or the path grammar;
- changing the unknown-member rule itself;
- making a previously optional manifest key required.

**The golden rule that keeps it frozen: a name is never re-meant.** If a field's
meaning changes, it gets a new name and the old one is retired. That converts
almost every conceivable change into the "does NOT bump" column, which is what
makes a frozen version honest rather than aspirational.

### 7.2 `state_model_revision` — the moving one

```json
"model": { "state_model_revision": 1, "machine": "next", "ram_kb": 2048 }
```

It identifies the **emulated machine model**: which fields exist and what they
mean. It is a provenance stamp, not a compatibility contract (settled point 5).
It is bumped when a subsystem's semantics change in a way that would make an
older snapshot restore *wrong* rather than merely incomplete — an FSM
renumbered, a register repurposed, a counter's units changed.

Note that the named encoding removes most of the reasons it would need to move:
an FSM state serialised as the string `"wait_for_vpos"` does not care that its
enum ordinal changed, and a field added last week is simply absent from an older
file. What remains are genuine model changes, which is what the number is for.

### 7.3 Reader behaviour

| Condition | Behaviour |
|---|---|
| Not a ZIP, or no `manifest.json` | **Refuse.** "not a jnext snapshot". |
| `manifest.json` is not valid JSON, or lacks `format_version` | **Refuse**, naming the defect. |
| `format_version` **>** this build's | **Refuse**, and say both numbers and the `producer.jnext_version` that wrote it: *"snapshot format 2 was written by jnext 1.4.0; this build reads up to format 1."* Never attempt a best-effort read of a grammar we do not know. |
| `format_version` **<** this build's, and a reader for it is compiled in | Read it under that grammar's rules. |
| `format_version` **<** this build's, reader removed | **Refuse**, naming the version and the release that dropped it. |
| A **required** member or key (per the schema) is missing | **Refuse**, naming it. |
| `model.machine` ≠ the machine this build would construct | **Refuse** unless the user asked for it — a snapshot is restored onto the machine it was taken on, and jnext reconfigures itself to `model.machine` rather than requiring the user to get `--machine` right. |
| `model.state_model_revision` ≠ this build's | **Restore, loudly.** One log line and a GUI status-bar note naming both revisions and both jnext versions. `--snapshot-strict` turns this into a refusal. |
| An **unknown** member or key | Ignore it, and log one line per subsystem that had any (§12). |

**Policy on dropping an old reader**: a `format_version` bump ships with the
previous grammar's reader retained for at least one public minor release, and
its eventual removal is a ChangeLog line under *User Features*. This is the
"better than MAME, but no promise" posture settled point 5 asks for, stated as a
rule rather than an intention.

### 7.4 The 2^53 rule

JSON has one number type. `nlohmann/json` round-trips `uint64_t` exactly, and so
does Python's `json` (arbitrary-precision ints) — but a JavaScript validator
(`ajv`) silently rounds above 2^53, which would make an external validation pass
on a file a JavaScript reader had already corrupted.

**Rule: any field whose value can exceed 2^53 is encoded as a decimal
*string*,** with `"pattern": "^[0-9]+$"` in the schema.

Which fields qualify today: **none.** The largest counters are
`monotonic_tstates()` and `Clock::cycle_`, which at 28 MHz reach 2^53 after
about ten years of continuous emulation. The rule exists so a *future* field
does not break external validation silently, and so the choice is recorded
rather than rediscovered. The descriptor layer (§9) is where it is enforced: a
`u64` declared through it is emitted as a string, full stop, and the schema
generator writes the pattern.

---

## 8. Identity and provenance

`manifest.json`, in full:

```json
{
  "format_version": 1,
  "created": "2026-09-23T10:11:12Z",

  "producer": {
    "jnext_version": "1.0.17",
    "git_describe": "v1.0.17-0-g15430513",
    "platform": "linux-x86_64"
  },

  "model": {
    "state_model_revision": 1,
    "machine": "next",
    "ram_kb": 2048,
    "timing": "next",
    "cpu_speed_nr07": 3
  },

  "capture": {
    "frame": 41291,
    "frame_boundary": true,
    "paused": true
  },

  "media": {
    "sdcard": { ... see §11 ... },
    "roms":   { "source": "sdcard", "sha256": { "48.rom": "…", "128.rom": "…" } },
    "boot_rom_sha256": "…",
    "tape":   { "path": "…/game.tzx", "sha256": "…", "position_tstates": 0,
                "realtime": false },
    "esxdos_root": "/home/user/nextdev"
  },

  "members": {
    "mem/ram.bin":             { "bytes": 2097152, "crc32": "8f3a21bd" },
    "mem/sprite-patterns.bin": { "bytes": 16384,   "crc32": "0c19ee40" }
  },

  "subsystems": [ "clock", "ram", "mmu", "nextreg", "cpu", "…" ]
}
```

Notes on the parts that carry weight:

- **`producer`** is provenance only: a mismatch is never itself a refusal. It is
  what turns "this snapshot behaves oddly" into a bisectable fact.
- **`model.machine` drives reconstruction**, not validation. Restoring a `.jns`
  reconfigures the emulator to the machine the snapshot was taken on, the same
  way loading a `.szx` does. The user should not have to remember `--machine`.
- **`media.roms`** closes a real gap. For `--machine 48k/128k/plus3` the ROM
  lives in the separate `Rom rom_` object, which is **not serialised** — so a
  snapshot restored against a different `48.rom` runs different code with no
  indication. Recording the digests makes that detectable. (On the Next the ROM
  content sits in SRAM pages inside `ram_`, so it travels in the snapshot
  already; the digest is still recorded, for provenance.)
- **`members`** is the blob declaration §5.3 rests on: the schema pins its shape,
  the ZIP's own CRC-32 checks the bytes, the reader checks the length.
- **`subsystems`** is the writer's own list of what it wrote. Its purpose is to
  let a reader distinguish *"this subsystem was deliberately not saved"* from
  *"this member is missing/corrupt"*, which are different failures and must not
  be conflated. A subsystem in the list with no member is a refusal.

---

## 9. Decoupling: one field list, two encodings, a generated schema

This is firm requirement **F2**, and it is the part of the design with real
engineering cost. It deserves the most scrutiny.

### 9.1 The rejected shortcut

The cheap route is to put the existing positional stream into the ZIP as
`state/legacy.bin` and write the manifest around it. It would work, it would
ship in days, and it would **ship exactly the coupling this feature exists to
remove**. The `state_model_revision` would then have to be bumped on every
`save_state` edit, every old file would die with it, and the "temporary" blob
would be load-bearing within a release. Rejected.

### 9.2 The mechanism: a field descriptor

Each subsystem declares its fields **once, by name**:

```cpp
// src/peripheral/divmmc.cpp
void DivMmc::describe_state(StateDesc& d) {
    d.u8   ("bank",              bank_);
    d.u8   ("control_reg",       control_reg_);
    d.bytes("entry_points",      entry_points_, 4);      // -> hex string
    d.enum8("automap_state",     automap_state_, kAutomapStateNames);
    d.ram_window("ram", /* page */ 16, kRamSize);        // -> a RAM reference
    // …
}
```

`StateDesc` is an interface with several realisations over the *same*
declaration:

| Realisation | Produces | Replaces |
|---|---|---|
| `MeasureDesc` / `BinWriteDesc` / `BinReadDesc` | the positional byte stream, in declaration order | today's hand-written `save_state`/`load_state` — same bytes, same speed, same fixed width |
| `JsonWriteDesc` / `JsonReadDesc` | the named-key JSON of §6 | — |
| `SchemaDesc` | the JSON Schema for that subsystem | — |

Consequences, and they are the argument:

1. **One field list.** The rewind stream and the snapshot cannot disagree about
   which fields exist, because there is only one declaration. Today they would be
   two lists that drift.
2. **Changing a field's in-memory representation changes the accessor, not the
   name.** A `uint8_t` that becomes a bitfield, an array that becomes a
   `std::array`, a member that moves into a nested struct: the declaration line
   changes, the on-disk key does not. That is F2, mechanically enforced.
3. **Reordering two declarations is now visible.** It changes the *binary*
   stream (which is versioned by length and by `kFormatVersion` today) and
   leaves the JSON identical. That is the correct outcome: the JSON is what
   files are made of.
4. **The schema is generated, never hand-written**, and therefore cannot drift
   from the code — the same discipline `cli_options.h`, `doc/man/jnext.1`,
   `USAGE.md`, the developer guide and `TRACEABILITY-MATRIX.md` already live
   under.
5. **G66 is retired as a side effect**, for the rewind ring as well as for
   files: a positional stream generated from named declarations is still
   positional, but the names are now available to check against.

### 9.3 The honest caveat about a generated schema

A schema generated from the writer, validated against a file produced by the
same writer, proves only self-consistency. `feedback_self_consistent_generated_data`
is explicit: **idempotence is not accuracy.**

So the schema is treated exactly like every other generated-and-committed
artefact in this repo:

- `doc/formats/jns-snapshot.schema.json` is **generated** (`make docs-schema`)
  and **committed**;
- a staleness gate (`schema-check`, alongside `docs-check` /
  `traceability-check`) regenerates it and fails if the committed copy differs;
- therefore **every change to a field declaration appears as a schema diff in
  the commit**, where a human reviewer sees it.

That review is where semantics are checked. The validator's job is narrower and
still worth having: it catches encoding faults — wrong type, malformed JSON, a
hex string of the wrong length, a value outside a register's range, a duplicate
key — using an implementation we did not write. §13 says what else is needed.

### 9.4 Where the descriptor does not fit

Not every subsystem is a flat bag of scalars, and pretending otherwise would
produce a descriptor with an escape hatch used everywhere. Three kinds get
hand-written `to_json`/`from_json` beside the descriptor:

- **Variable-length lists** — the esxDOS host-FS handle table (path, offset,
  mode per open handle) and the joystick-cable cursor.
- **Fields written relative to something the stream does not carry** — the CPU's
  `/INT` window, written as a delta against the FUSE T-state counter that
  `load_state` re-seeds at the next frame start.
- **Cross-subsystem re-syncs** — the `Ula`'s copies of the palette-bank
  selectors and the attribute mux, rebuilt from `PaletteManager` and VRAM after
  a load (GH #261). These are *derived*, so they belong in neither encoding;
  they are recomputed, and the rule is that a derived field is never written.

Each such case is named in the subsystem's doc-comment, so "hand-written" is a
declared exception rather than a habit.

---

## 10. The complete state inventory

Every subsystem, its serialisation today, and what JNS must do. "Covered" means
the existing `save_state`/`load_state` is believed complete for that subsystem;
JNS re-expresses it through the descriptor and does not add state.

### 10.1 Covered by the existing stream — re-express, do not extend

| Area | Class(es) | Notes for JNS |
|---|---|---|
| Clock | `Clock` | scalars |
| RAM | `Ram` | → `mem/ram.bin` blob; size from `model.ram_kb` |
| MMU | `Mmu` | 8 slot page numbers + 128K/+3 port shadows |
| NextREG | `NextReg` | all 256 registers as one hex string + the shadow scalars |
| CPU | `Z80Cpu` | registers, IFF, IM, halt, the stackless-RETN latch, the `/INT` window (§9.4) |
| Interrupts | `Im2Controller` | + `save_timing` (GH #265) |
| Palettes | `PaletteManager` | 4 × 2 × 256 `u16` + priority; JSON, not a blob (§6.1) |
| Layer 2 | `Layer2` | registers + clip window |
| Sprites | `SpriteEngine` | 128 × 5 attribute bytes in JSON; 16 KB pattern RAM → blob |
| Tilemap | `Tilemap` | registers + clip window |
| LoRes | `LoRes` | registers |
| ULA / renderer | `Ula`, `Renderer` | the per-scanline change logs are **not** written — `load_state` rebuilds them from the restored registers (GH #261). JNS keeps that rule. |
| Copper | `Copper` | 1K instruction RAM as hex + PC/mode |
| CTC | `Ctc` | 4 channels + `save_timing` |
| DMA | `Dma` | FSM + registers |
| SPI | `Spi` | master FSM |
| I2C + RTC | `I2c`, `I2cRtc` | bit-bang state + RTC registers |
| UART | `Uart` | FIFOs + prescaler |
| DivMMC | `DivMmc` | registers; its 128 KB RAM is a **window into `Ram`** (§4) — JNS references, does not copy |
| Multiface | `Multiface` | FFs; its 8 KB RAM is likewise a RAM window on the machines that back it externally |
| NMI | `NmiSource` | + `prev_nmi_generate_n_` |
| Audio | `Beeper`, `TurboSound` (3 × `AyChip`), `Dac`, `I2s` | chip registers, envelope/LFSR phase |
| Input | `Keyboard`, `Joystick`, `KempstonMouse`, `Md6ConnectorX2`, `MembraneStick`, `IoMode` | Task 60c. Host-side `JoystickDispatcher`/`MouseDispatcher` are platform-owned and re-seeded by the `on_input_state_restored` callback — JNS keeps that. |
| Joystick cable | `JoyUartSource` | optional; present only when attached (GH #251) |
| esxDOS host FS | hand-rolled in `Emulator` | (path, offset, mode) per handle + cwd. **The precedent for §11**: an external resource is recorded by reopenable identity, not copied. |
| `Emulator` scalars | ~40 fields | `frame_cycle_`, monotonic T-states, `frame_num_`, `boot_hold_frames_remaining_`, `esp_frames_`, `cpu_parked_`, the PSG/sample Bresenham phases, the IM2 enable/status/DMA-delay registers, the four clip-window write indices, `port_ff_reg_`, `nr_10_coreid_`, the G55 IO-trap trio, `nr_2d_i2s_sample_`, `nr_a0/a2`, `nr_02_bus_reset_`, `prev_pulse_int_n_`. In JNS these are **named keys**, so the append-order chronology they carry today disappears. |

### 10.2 Gaps a snapshot must close

| # | Gap | Severity | Recommendation |
|---|---|---|---|
| **P1** | **`SdCardDevice` has no `save_state` at all.** Its own header says so and enumerates what would be needed: `multi_block_`, `multi_block_sector_`, `state_`, `resp_buf_`, `resp_idx_`, `data_idx_`, `data_crc_count_`, `data_block_`. A snapshot taken mid-CMD18 stream restores a card that is not streaming. | **High** — this is the one that silently corrupts a running loader | **Serialise the FSM** into `state/sdcard.json`, plus the mounted path, the read-only flag and the read-overlay window. The field list is already written down in the header comment. |
| **P2** | **SD image contents.** §11. | High | §11. |
| **P3** | **`rom_` is not serialised** — for 48K/128K/+3, ROM content comes from the SD image at load time and never travels. | Medium | Record `media.roms` digests (§8) and refuse on mismatch under `--snapshot-strict`, warn otherwise. Do **not** embed 64 KB of ROM: it is firmware, and N3 applies. |
| **P4** | **Tape state.** `tape_`/`tzx_tape_`/`wav_tape_` are excluded by design (tape position is independent of CPU rewind). A snapshot taken *during* a tape load restores a machine waiting for a tape that is not playing. | Medium | Record `media.tape` = (path, sha256, position in T-states, realtime flag) and reopen on restore — the esxDOS-handle shape. If the file is absent, warn and restore without it. |
| **P5** | **Framebuffer.** Regenerated by the next render, so a snapshot restored *paused* shows the previous frame until the user steps. | Low | `meta/preview.png` doubles as the restore-time paused image. Free — jnext already writes PNG. |
| **P6** | **Mixer integration accumulator.** Deliberately not snapshotted; the first sample after a restore averages a short window. | Negligible | Keep the existing decision; document it. |
| **P7** | **Scheduler queue.** Empty at a frame boundary, which is why it is never serialised. | — | **Constraint, not a gap**: a `.jns` may only be written at a frame boundary. `capture.frame_boundary` records it; a writer that finds itself elsewhere refuses. |
| **P8** | **Host input dispatchers.** Platform-owned, hold their own shadow of the connector/wheel/button vector that would stomp a restore. | — | Keep the `on_input_state_restored` callback. |
| **P9** | **Debug state**: breakpoints, watches, trace log, call stack, rewind ring. | Low | **Out of scope.** Not machine state. Worth an explicit sentence in the user guide, because "my breakpoints vanished" is a predictable support question. |
| **P10** | **RZX / video recorder.** | — | Out of scope; a recording in progress is a host activity. A `.jns` written during one records nothing about it. |
| **P11** | **`contention_`, `port_`.** Rebuilt from `model.machine` and by `init()`. | — | Not written. |
| **P12** | **ESP-01 / network.** `esp_frames_` travels; the socket does not. | Low | Document: a restored snapshot has a fresh ESP association. |

P1 is the one that must not be deferred: it is the difference between "resume my
program" working and working *except* for programs that stream from the card,
which is most of them on a Next.

---

## 11. The SD card question

Settled point 4 calls this "the one real open design question", names three
options, and sets a floor: **"At minimum the snapshot must record the base
image's identity so a mismatch is detected rather than silently wrong."**

### 11.1 The complication

jnext **opens the SD image read-write and persists guest writes** (CLAUDE.md is
explicit; `--sdcard-readonly` is the opt-out, GH #77). So "the same image" is not
a stable thing. A digest of the whole image — which is exactly the right
identity for the warm-start cache, where a boot provably does not write — is the
**wrong** identity here: it changes the first time NextZXOS touches a directory
entry, and a snapshot would then report a mismatch against the very card it was
taken on, for a reason that may be entirely irrelevant to the restored program.

An identity that cries wolf gets ignored, and an ignored identity is worse than
none.

### 11.2 The three options

**(a) Include the image.** 1 GB raw, perhaps 100–200 MB deflated, per snapshot.
It also puts the whole NextZXOS distro inside a file users will inevitably try to
share (N3). Rejected on size and on N3.

**(b) Store a delta against a named base image.** This is the option that sounds
right and is not. A delta needs a pristine base to be a delta *of*, and jnext
does not keep one — it writes through to the mounted image. The only pristine
copy is the provisioner's `cspect-next-1gb.img`, which is a *different* image
from the patched `-fixed.img` actually mounted, and which the user may have
deleted. A delta against a base that has itself drifted is a delta against
nothing. Rejected — with the note that it becomes viable if jnext ever gains
copy-on-write card mounting, which is a separate feature with independent merit
(see §18).

**(c) Ignore the card contents, record its identity.** Recommended, with the
identity split in two so it can be both stable and informative.

### 11.3 Recommendation: a two-tier identity

```json
"media": {
  "sdcard": {
    "mounted_path": "/home/user/.jnext/sdcard/cspect-next-1gb-fixed.img",
    "read_only": false,

    "identity": {
      "image_bytes": 1073741824,
      "mbr_sha256": "…",
      "fat32_volume_id": "1a2b3c4d",
      "fat32_volume_label": "NEXT       ",
      "partition_lba": 2048
    },

    "content_stamp": {
      "sha256": "…",
      "mtime_utc": "2026-09-23T09:58:41Z"
    }
  }
}
```

**Tier 1 — `identity`: "is this the same card?"** Derived from fields a file
write does not touch: the image size, the MBR partition table, and the FAT32
boot sector's volume serial (`BS_VolID`, BPB offset 0x43) and label
(`BS_VolLab`, 0x47). jnext already parses the MBR and BPB host-side in
`src/core/sd_rom_extractor.cpp` (`find_fat32_partition_lba`, `parse_bpb`), so
this is a two-field addition, not new machinery.

**Tier 2 — `content_stamp`: "has it changed since?"** The whole-image SHA-256
and mtime, the warm-start cache's existing mechanism reused verbatim.

**Restore behaviour:**

| Condition | Behaviour |
|---|---|
| No card mounted now, snapshot had one | **Refuse.** Naming the path and the volume label. |
| `identity` differs | **Refuse.** This is the silently-wrong case settled point 4 demands be caught. `--snapshot-force-sdcard` overrides, with a warning that names both identities. |
| `identity` matches, `content_stamp` differs | **Restore, with one warning line** naming the snapshot's digest and the current one. Legitimate and common — the card drifts. |
| Both match | Silent. |
| The SD FSM (P1) was mid-transfer at capture | Restore it (P1), and additionally require `content_stamp` to match — a half-finished sector read against changed bytes is exactly the "streams garbage" failure. Mismatch here is a **refusal**, not a warning. |

That last row is the part worth defending: the identity check is tiered because
most of the time the card's drift is irrelevant, but when the machine is *in the
middle of reading a sector* it is not, and the design should be strict exactly
where strictness is earned.

**Cost**: the Tier-2 digest is ~0.5 s warm, ~1.2 s cold on a 1 GB image
(measured for the warm-start cache). Paid once per save and once per load. For a
manual save-state that is acceptable; if it proves annoying, it is cheap to make
the digest lazy — Tier 1 alone on load, Tier 2 only when Tier 1 matches and the
FSM was mid-transfer. Recommend shipping it eager and measuring.

---

## 12. Compatibility rules and the unknown-member rule

SZX's convention (settled point 7) is "unknown chunk types must be ignored". ZIP
member names express the same rule with more room, and the rule is stated at two
levels:

**Members.** A reader **ignores any member whose path it does not recognise**,
including whole unknown directory prefixes. It logs one line listing them, at
`debug` level, so a newer file read by an older jnext says what it dropped. The
one exception is `manifest.json`, whose absence is a refusal.

**Keys.** Within a `state/*.json` object, a reader **ignores unknown keys** and
**defaults missing keys to the value the subsystem's `reset()` establishes**. It
logs one line per subsystem that had either. Required keys — those the schema
marks `required` — are a refusal when missing, and the schema marks a key
required only when no honest default exists.

**Why this is stricter than it looks.** "Default the missing key" is safe only
because the default is `reset()`'s value, which is the power-on state the VHDL
specifies. It is never a zero chosen for convenience. A subsystem whose
power-on state is not expressible (an FSM with no idle state) must mark its
fields required instead.

**Forward compatibility** (old jnext reads new file): works for additive
changes, which §7.1 makes the common case. Refuses cleanly on a
`format_version` bump.

**Backward compatibility** (new jnext reads old file): works for additive
changes. A `state_model_revision` mismatch restores with a warning, or refuses
under `--snapshot-strict`. This is the whole of what is promised, and it is
deliberately less than "forever" (settled point 5).

**What is explicitly NOT promised:** that a snapshot taken by jnext 1.1 restores
correctly in jnext 2.0. The stamp exists so that when it does not, the failure is
legible.

---

## 13. Validation: what substitutes for a foreign reader

### 13.1 The lesson this must answer

jnext's `.szx` saver once wrote RAM pages 0–111. Its own loader accepted any
`uint8_t` page with no upper bound, so save → load → compare passed byte-exact,
with discriminative, mutation-tested assertions — all green, all worthless:
libspectrum's `read_ramp_chunk()` hard-rejects any page > 63, so **every `.szx`
file jnext could produce** failed to load in real FUSE. Saver and loader shared
the blind spot, which made the defect *structurally invisible* to the suite as
written. It was caught only because a reviewer's brief demanded proof
independent of jnext's loader, and the reviewer installed FUSE.

For a private format there is no foreign reader **by definition**. So the design
must name what replaces one. Five things do, and they are ordered by how much of
the blind spot each removes.

### 13.2 The five substitutes

**(1) An independent ZIP reader, on every written file.** `unzip -t` and
Python's `zipfile.testzip()` verify the central directory, the local headers,
the sizes and the per-member CRC-32 — none of it our code. This removes the
entire *container framing* class, which is where the `.szx` bug lived (a
structural field our own reader was too permissive about).

**(2) An independent JSON Schema validator, on every written file.**
`check-jsonschema` / Python `jsonschema` reading the committed
`jns-snapshot.schema.json`. Removes the *encoding* class: wrong type, missing
required key, hex string of the wrong length, register value out of range,
duplicate key, malformed UTF-8. This is the strongest single argument for the
owner's suggestion, and it is why the schema must be a real, committed,
independently-consumable artefact rather than documentation.

Its limit is §9.3's: the schema is generated from the same declarations the
writer uses, so it cannot catch a *semantic* error both sides share. Which is
why:

**(3) The schema is committed and staleness-gated.** `make schema-check`
regenerates and byte-diffs, exactly as `docs-check` does for the man page. Every
field-declaration change therefore surfaces as a schema diff in the commit, and a
human reviewer reads it. This is the mechanism that turns the generated schema
from self-consistency theatre into a review surface.

**(4) A second reader, written from the specification.** A small Python script
in `test/` that opens a `.jns` and reconstructs a handful of named values —
PC, SP, the NR 0x15 layer priority, the MMU slot map, three bytes of RAM at
known addresses — **written from this document, not from the C++ writer**, and
compared against those same values read out of the emulator by a different path
(the debugger interface / `--delayed-snapshot` of a `.sna` of the same moment).
It is not a full foreign reader, but it is a genuinely independent one for the
fields that matter most, and it is the closest thing a private format can have.

**(5) Byte-level assertions against this document, not against our loader.** The
memo's rule, applied: tests assert that member 0 is named `manifest.json`, that
the archive comment is `jnext-snapshot format=1`, that `mem/ram.bin` is exactly
`ram_kb * 1024` bytes, that no member path escapes the archive. These are
assertions about the *spec*, and they survive a misreading shared by writer and
reader.

### 13.3 What none of them prove

That the restored machine is the machine that was saved. That is a **semantic
round-trip** question and it is answered by the tests of §16 — save, restore,
run N frames, compare the framebuffer against a run that never saved — not by
any format mechanism. The distinction is worth keeping sharp, because "the file
validates" and "the snapshot works" are different claims and this document
should not be read as conflating them.

---

## 14. Dependencies

Stated plainly, because the project is conservative about them and should be.

### 14.1 Runtime — linked into jnext

| # | Need | Proposal | New? |
|---|---|---|---|
| 1 | **JSON write + parse** | Vendor **nlohmann/json** (single header, MIT) at `third_party/nlohmann-json/`. | **YES — one new vendored header** |
| 2 | **ZIP read + write** | **First-party**, ~600 lines over zlib: local file headers, central directory, EOCD, `STORED` + raw `DEFLATE` (`deflateInit2` with `windowBits = -15`). | No new dependency |
| 3 | **CRC-32** | zlib `crc32()` | Already linked |
| 4 | **DEFLATE** | zlib | Already linked (`find_package(ZLIB REQUIRED)`, CMakeLists.txt:151) |
| 5 | **SHA-256** | `sha256_file()` / `sha256_hex()` in `src/core/sdcard_provisioner.h` | Already present, already cross-platform |
| 6 | **PNG** (optional preview) | `src/platform/screenshot.cpp` | Already present |

**Justifying #1.** A JSON library is a real new dependency and the project
should be asked to accept it explicitly. In favour: the repo already vendors
four third-party trees (`spdlog`, `fatfs`, `fuse-z80`, `zot`), of which spdlog
is far larger; nlohmann is a single MIT header with no build system to fight;
and it round-trips `uint64_t` exactly, which a naive parser does not. Against:
it is a heavy header to compile. **Mitigation: confine it to the two or three
translation units of the snapshot module** — nothing else in jnext includes it,
and the emulator core's "no platform headers" discipline is unaffected (JSON is
not a platform dependency).

**The narrower alternative, if the owner prefers it**: a first-party JSON writer
is genuinely trivial (~150 lines — we control every value we emit), and the
*parser* is the risky half. A hand-rolled parser would be ~500 lines with escape
handling, UTF-8, and number edge cases, and its bugs would be invisible until a
hand-edited file arrived — which this format explicitly invites ("easier to
verify"). If a vendored header is unacceptable, **picojson** (BSD-2, one
~2 500-line header) is a much smaller middle ground. Recommendation stands at
nlohmann; picojson is the fallback if compile time proves a problem.

**Justifying #2 over minizip.** `minizip` / `minizip-ng` would save ~600 lines
and cost a new `Requires:` in `packaging/rpm/jnext.spec`, `packaging/debian/`,
the Flatpak manifest, the Homebrew macOS leg and the MinGW Windows cross-build —
five packaging surfaces, with two incompatible upstream APIs in circulation. The
project has hand-rolled a FAT32 reader, a TAP parser and an SD-card SPI
back end for less reason. Write the ZIP code.

### 14.2 Test-time only — never linked, never packaged

| # | Need | Tool | Posture |
|---|---|---|---|
| 7 | **Independent JSON Schema validation** | `python3-jsonschema` (Fedora) / `python3-jsonschema` (Debian) | **Skip if absent locally; hard-fail in CI** — the exact posture `docs-check` uses for pandoc and mkdocs |
| 8 | **Independent ZIP verification** | `unzip`, and Python's stdlib `zipfile` | Same |

Neither adds a runtime dependency, a `Requires:` line or a package. CI already
runs in `fedora:44` and already installs pandoc, mkdocs-material and graphviz for
exactly this class of check; `python3-jsonschema` and `unzip` join that list.

**Net: one new vendored header at runtime, two packaged tools at test time, zero
new package dependencies.**

---

## 15. GUI and CLI integration surface

### 15.1 CLI

`src/core/cli_options.h` is the **single source of truth** for the flag set
(issue #43): `main.cpp` dispatches from the table, and `cli_options_test` diffs
it against the OPTIONS section of `doc/man/jnext.1.md` in **both** directions.
Implemented-but-undocumented and documented-but-unimplemented are both hard
failures of `make cli-check`. So each flag below means: an `OptId`, a row in the
table, a `case` in `main.cpp`'s switch (enforced by `-Wswitch`), and an entry in
`doc/man/jnext.1.md`.

| Flag | Args | Purpose |
|---|---|---|
| `--load FILE` | — | **No new flag.** `.jns` joins the existing extension dispatch (`emulator.cpp:1153`). The man page's `--load` list gains `.jns`. |
| `--delayed-snapshot FILE` | — | **No new flag.** `.jns` joins the existing extension dispatch. Its man-page sentence — "the format is chosen by the extension of *FILE*: `.szx`, `.nex`, anything else `.sna`" — must be updated, and `cli-check` will not catch that, because it checks the flag set, not the prose. |
| `--snapshot-uncompressed` | 0 | Write every member `STORED`. Settled point 6's debugging mode. |
| `--snapshot-strict` | 0 | Turn the `state_model_revision` / ROM-digest / tape warnings into refusals. |
| `--snapshot-force-sdcard` | 0 | Override the Tier-1 SD identity refusal (§11.3). Deliberately verbose, because it is the flag that lets a user create the silently-wrong case the design exists to prevent. |

Five new flags is more surface than this feature deserves; the last three are
all "make a warning a refusal" or "override a refusal". An alternative worth the
owner's opinion: collapse them into one `--snapshot-mode strict|normal|force`.
Listed in §18.

### 15.2 GUI

| Surface | Change |
|---|---|
| **File ▸ Save Snapshot…** (`main_window.cpp:1763`, Alt+Shift+S) | Add `.jns` to the filter, **make it the default suffix on the Next**, keep `.sna` as the default on classic machines. The existing extension dispatch gains one arm. |
| **File ▸ Load…** | Add `*.jns` to the file dialog's filter string (`main_window.cpp:1137`). |
| **Status bar** | On restore: the one-line provenance note when `state_model_revision` or the SD `content_stamp` differ (§7.3, §11.3). It must be visible, not log-only — a user who ignores a mismatch should have had to ignore it. |
| **Error reporting** | Refusals go through the existing `QMessageBox::warning` path `on_save_snapshot()` already uses for `SzxSaver`'s machine refusal, with the reader's reason string verbatim. |

### 15.3 Documentation

- `doc/man/jnext.1.md` — the new flags, and the amended `--load` /
  `--delayed-snapshot` prose. Regenerate `doc/man/jnext.1` and `USAGE.md`
  (`make docs-man`) and commit both, or `docs-check` fails the next test run.
- `src/doc/user-guide` — a section under the loading/saving chapter, which
  **must** say that a `.jns` contains NextZXOS and DivMMC ROM content and is not
  to be shared (N3), and that debugger state does not travel (P9). Re-render
  (`make docs-userguide`) and commit.
- `src/doc/developer-guide/02-architecture/05-save-state-and-rewind.md` — that
  chapter currently describes the positional stream as *the* mechanism. The
  descriptor layer changes that description, and a stale paragraph there is the
  same class of defect as a stale man page with the difference that no gate can
  see it.
- `doc/formats/jns-snapshot.schema.json` — generated and committed (§9.3).
- `FEATURES.md` — a user-visible feature; add it.
- `ChangeLog` — under `Unreleased`, *User Features*.

---

## 16. Test plan sketch

Consistent with `doc/testing/UNIT-TEST-PLAN-EXECUTION.md`, with one structural
note that must be handled up front:

> **There is no VHDL counterpart.** A snapshot format is a jnext-internal
> artefact, so there is no `*-TEST-PLAN-DESIGN.md` derived from the FPGA source
> and no per-row VHDL citation. That is the position `rewind_test` and
> `sdcard_test` are already in: they carry per-suite tombstones
> (`(jnext-internal)`, `(SD SPI spec)`) and their planned rows live in
> `test/traceability-exceptions.conf`. **`snapshot_test` needs the same
> treatment, added in the same change**, or `make traceability-check` will
> refuse. The tombstone should read `(jnext-internal)` and cite this document as
> its specification.

Manifests to update in the same change (a missing test is a loud failure, never
a silent skip):

- `test/unit-tests.conf` — `snapshot_test` with its **exact** pinned row count.
- `test/00regression/functional_tests.conf` — the functional rows below.
- `test/traceability-exceptions.conf` — the tombstone and any planned rows.

### 16.1 Unit rows — `test/snapshot/snapshot_test.cpp`

| Group | IDs | What |
|---|---|---|
| **Container** | `JNSC-01…` | ZIP round-trip through our writer and reader; `manifest.json` is member 0; archive comment is exact; `STORED` and `DEFLATE` both read; member-path grammar refusals (absolute, `..`, upper case, empty); ZIP64 refused; truncated archive refused; a member whose CRC-32 disagrees refused. |
| **Version** | `JNSV-01…` | Every row of §7.3's table, both directions: `format_version` too new → refuse with both numbers in the message; too old with reader present → reads; too old with reader absent → refuse; missing → refuse; non-integer → refuse. |
| **Unknown-member / unknown-key** | `JNSU-01…` | An added unknown member is ignored and logged; an added unknown key is ignored; a missing optional key takes the `reset()` default (asserted *against* `reset()`, not against a literal); a missing **required** key refuses. |
| **Identity** | `JNSI-01…` | SD Tier-1 mismatch refuses; Tier-2 mismatch warns and restores; Tier-2 mismatch **with the SD FSM mid-transfer** refuses; no card mounted refuses; ROM digest mismatch warns / refuses under strict; tape file absent warns. |
| **Encoding** | `JNSE-01…` | Hex strings are exactly the declared length; a `u64` declared through the descriptor emits a string; enum fields emit names, and an unknown name on read refuses rather than defaulting (a wrong FSM state is not a safe default). |
| **Descriptor** | `JNSD-01…` | For every subsystem: the JSON encoding and the binary encoding, fed the same machine, restore to identical machines. **Each encoding is the other's oracle for content** — this is the cross-check that makes §9's "one field list" claim testable. |
| **Blob framing** | `JNSB-01…` | `mem/ram.bin` is exactly `ram_kb * 1024`; a short blob refuses; a long blob refuses; the declared CRC is the actual CRC. |
| **Refusal messages** | `JNSM-01…` | Every refusal names the offending thing. A refusal that says only "invalid snapshot" is a failing row: G9 is a testable property, not a slogan. |

Every row is mutation-tested by its author before review
(`feedback_mutate_the_branch_not_the_helper`): revert the behavioural branch the
row exists for and confirm the row fails.

### 16.2 Functional / regression rows — `test/00regression/`

| Row | What |
|---|---|
| `snapshot-roundtrip-func` | Headless: boot, run to frame N, `--delayed-snapshot out.jns`; restart with `--load out.jns`, run M more frames, screenshot; compare **pixel-exact** against a single uninterrupted run of N+M frames. This is the semantic round-trip §13.3 says nothing else proves. |
| `snapshot-schema-func` | Validate the written file with `python3 -m jsonschema` against the committed schema, and `unzip -t` it. **Skips if the tools are absent; hard-fails in CI** — the `docs-check` posture. |
| `snapshot-foreign-read-func` | The §13.2(4) spec-written Python reader: extract PC, SP, NR 0x15, the MMU slot map and three known RAM bytes from the `.jns`, and compare against the same values obtained by a different path. |
| `snapshot-uncompressed-func` | The same round-trip with `--snapshot-uncompressed`; assert every member is `STORED` (read by `zipfile`, not by us) and the restore is pixel-identical to the compressed one. |
| `snapshot-sdcard-mismatch-func` | Save, mutate a sector of a *copy* of the card, restore against it: assert the Tier-2 warning appears and the run proceeds; then mutate the volume label and assert the Tier-1 refusal and a non-zero exit. |

`JNEXT_TEST_JOBS=4` on every regression invocation, as always.

### 16.3 Staleness gate

`make schema-check` — regenerate `doc/formats/jns-snapshot.schema.json` and
byte-diff it against the committed copy. Wire it as a prerequisite of
`make unit-test` beside `docs-check` and `traceability-check`, and as a declared
suite so a missing gate is loud.

---

## 17. Staged implementation plan and effort

Effort is in *agent-sessions of focused work*, the unit this project has
historical calibration for. Every stage ends with an independent review by an
agent that did not write it, on its own branch and worktree.

| Stage | Content | Effort | Gate to the next |
|---|---|---|---|
| **S0 — Owner review of this document** | §18's questions answered; container and SD decisions confirmed | — | Written answers |
| **S1 — Container** | First-party ZIP reader/writer over zlib; `manifest.json` grammar; `format_version` rules; member-path refusals; the §16.1 `JNSC`/`JNSV` rows; `unzip -t` in the suite | **M** (2–3) | All container rows green **and** an independent ZIP reader accepting every file we write |
| **S2 — Descriptor layer** | `StateDesc` + the binary/JSON/schema realisations; **`kFormatVersion` behaviour preserved byte-for-byte** for the rewind ring; the schema generator + `schema-check` gate | **L** (4–6) | `rewind_test` unchanged and green; the generated binary stream byte-identical to today's for an unchanged build |
| **S3 — Subsystem migration, group 1** | Core: clock, RAM, MMU, NextREG, CPU, IM2 (+ `save_timing`) | **M** (3–4) | `JNSD` cross-encoding rows for these subsystems |
| **S4 — Subsystem migration, group 2** | Video: palette, layer2, sprites, tilemap, lores, ULA, renderer, copper | **M** (3–4) | as above |
| **S5 — Subsystem migration, group 3** | Peripherals + audio + input: ctc, dma, spi, i2c, rtc, uart, divmmc, multiface, nmi, beeper, turbosound, dac, i2s, and the six input classes | **M** (3–4) | as above |
| **S6 — The gaps** | P1 `SdCardDevice` serialisation (the enumerated field list); P3 ROM digests; P4 tape identity; P5 preview; `Emulator`'s own scalars | **M** (2–3) | P1 proven by a mid-CMD18 save/restore row |
| **S7 — SD identity** | Tier-1 from MBR+BPB (extend `sd_rom_extractor`), Tier-2 reuse, the refusal/warning matrix, `JNSI` rows | **S** (1–2) | `snapshot-sdcard-mismatch-func` |
| **S8 — Integration** | CLI table + man page + `cli-check`; GUI save/load/filter/status bar; user guide; developer guide chapter; FEATURES; ChangeLog | **S** (1–2) | `make cli-check`, `docs-check`, full triplet |
| **S9 — Validation** | The spec-written Python reader; the schema validation row; the full functional set; **CI tool install** | **S** (1–2) | All §16.2 rows green in CI |

**Total: roughly 20–30 focused sessions.** The bulk is S2–S5, and that is the
price of firm requirement F2. It should be understood as such: **most of this
project is not the file format, it is removing the coupling between the file and
the structs.** A `legacy.bin` shortcut would cut it to about a quarter — and
would deliver the thing the owner asked not to have (§9.1).

**Shipping posture.** There is no partial-restore value: a snapshot that
restores half a machine is not a snapshot. So `.jns` stays behind the
`--snapshot-*` flags and out of the GUI's default suffix until S6 lands, and the
`format_version` is not frozen — i.e. not promised — until S9 is green. The
first public release that mentions `.jns` is the one that freezes it.

---

## 18. Open questions for the owner

1. **Container: confirm Option C** (ZIP + JSON manifest/state + binary blobs) over
   Option A (first-party chunked binary, zero new dependencies, ~1/4 the
   container effort, and no external inspectability)? §5 recommends C; A is a
   defensible answer if the dependency and the ~600 lines of ZIP code are
   unwelcome.

2. **nlohmann/json as a vendored header** — accepted? If not: picojson (smaller,
   BSD-2) or a first-party writer plus a first-party parser (§14.1 argues
   against the last one).

3. **The decoupling cost.** S2–S5 is ~13–18 sessions of migrating 34 subsystems
   to a descriptor. Confirm that is the intended scope of F2, rather than a
   narrower reading (e.g. named keys only for the ~32 KB of scalars, with the
   flat buffers going straight to blobs and no descriptor at all — which would
   cut S2–S5 roughly in half and still satisfy "not a struct dump" for
   everything a human would ever look at).

4. **File extension**: `.jns`? Alternatives considered: `.jnx` (too close to
   `.nex`), `.nxs`, `.jsnap`. The warm-start cache uses `.jwss`, so `.jns` is
   consistent with it.

5. **CLI surface**: five flags, or collapse `--snapshot-strict` /
   `--snapshot-force-sdcard` into one `--snapshot-mode strict|normal|force`
   (§15.1)?

6. **SD card**: confirm the two-tier identity of §11.3, and specifically the
   asymmetry — a *content* drift warns normally but **refuses** when the SD FSM
   was mid-transfer. The alternative is to warn in both cases and let the user
   find out, which is simpler and less safe.

7. **Copy-on-write card mounting** (out of scope here, noted in §11.2): if jnext
   ever mounts the SD image copy-on-write, the rejected "delta against a base
   image" option becomes viable and the whole SD question changes shape. Worth
   its own issue, or not worth raising?

8. **Debugger state (P9)**: confirm breakpoints, watches and the rewind ring do
   **not** travel in a snapshot. A case can be made for breakpoints — they are
   cheap and a user resuming a debugging session would want them — but they are
   not machine state and mixing them in weakens the format's definition.

9. **Does the warm-start cache eventually migrate onto this?** §3.2 notes that
   the descriptor layer would let `kFormatVersion` be derived rather than
   hand-bumped, retiring the "a field repurposed is invisible" hazard there too.
   Out of scope for #27; worth a follow-up issue, or deliberately left alone?

10. **Priority.** The issue itself records "priority stays low: the debugging
    value is already shipped as backward execution, and Next software commonly
    provides its own state preservation." At ~20–30 sessions this is one of the
    larger remaining v1.1 items. Confirm it is wanted now, at this cost, against
    the other open features.

---

*Design document for issue #27. Nothing here is implemented. The measurements in
§4 are from the current build on 2026-09-23; the code references are to
`main` @ `15430513`.*
