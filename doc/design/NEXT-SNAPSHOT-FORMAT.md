# Next-Specific Snapshot Save/Load Format — design

> Status: **design approved, not implemented**. Tracking issue:
> [#27](https://github.com/jorgegv/jnext/issues/27). Milestone v1.1.
>
> The owner approved the container (Option C), the vendored JSON dependency,
> full-scope decoupling and the two-tier SD identity on 2026-09-23 — §18.1.
> §18.2 lists what is still open, §18.3 the defects this design found in shipped
> code.
>
> **Revision 3** (2026-09-23), after two rounds of independent review.
> Revision 2 replaced every derived number in §4 with a per-block measurement,
> correcting the scalar residual by a factor of ~16 and *strengthening* the
> container argument, and added four flat buffers, three per-scanline histories
> and two 64-bit-overflowing fields that revision 1 had missed. Revision 3 splits
> the history primitive in two (the two layouts differ in three ways the
> byte-identity gate tests), states the buffer-classification rule as three cases
> with an owned size threshold, gates the declared defaults against `reset()`,
> adds stage S5b, and follows the owner's decision on mid-frame saves. Each
> correction is marked in place rather than quietly overwritten.
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

`Emulator::save_state()` calls the subsystems in a fixed order and writes a
`u32` sentinel (`kStateSentinelMagic ^ ordinal`) after each block, so a desync
is reported by name rather than silently deserialised. **Measured on this tree
(§4): 33 sentinel-delimited blocks, ordinals 0-32** — 27 of them a single
subsystem's `save_state` call, and 6 `Emulator`-owned groupings (the scalar
block, `nextreg_appends`, `tail`, `input`, `int_timing`, `esxdos_hostfs`).

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

- **Load by extension**: there are **three** dispatch sites, and a new format
  must be added to each — `src/main.cpp:1490` (the CLI `--load` arm that routes
  `.sna`/`.szx`/`.z80`), `Emulator::load_snapshot_buffer()`
  (`emulator.cpp:8060`, the RZX-embedded-snapshot path, which does
  `init(config_)` then `apply()`), and the GUI dialog's filter. The
  `extension()` call at `emulator.cpp:1153` is **not** the dispatcher — it only
  pre-detects `.nex` to arm `direct_nex_load`.
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

Numbers matter here, because they decide §5. Everything below is **measured
against this tree** (`main` @ `15430513`) on 2026-09-23, not estimated: a
temporary `fprintf` in `Emulator::save_state`'s `put_sentinel` lambda printed
`w.position()` deltas per block, `rewind_test` was rebuilt and run, and the
instrumentation was then reverted from a `cp` backup. The blocks sum, with the
sentinels, to exactly the size `rewind_test` prints.

**The full `Emulator::save_state` stream is 2 292 965 bytes**, in **33
sentinel-delimited blocks**:

| # | Block | Bytes | # | Block | Bytes |
|---|---|---|---|---|---|
| 0 | `clock` | 12 | 17 | `uart` | **2 330** |
| 1 | `ram` | **2 097 160** | 18 | `divmmc` | **131 089** |
| 2 | `mmu` | **24 634** | 19 | `beeper` | 3 |
| 3 | `nextreg` | 262 | 20 | `turbosound` (+3 × `AyChip`) | 152 |
| 4 | `cpu` | 45 | 21 | `dac` | 4 |
| 5 | `im2` | 149 | 22 | *emulator scalars* | 72 |
| 6 | `palette` | **4 622** | 23 | `i2s` | 4 |
| 7 | `layer2` | 12 | 24 | `nmi_source` | 29 |
| 8 | `sprites` | **17 039** | 25 | *nextreg appends* | 8 |
| 9 | `tilemap` | 26 | 26 | `multiface` | **8 201** |
| 10 | `renderer` (+`Ula`, +`LoRes`) | **3 688** | 27 | *tail* | 2 |
| 11 | `copper` | **2 057** | 28 | *input* (6 classes) | 450 |
| 12 | `ctc` | 40 | 29 | *stackless NMI* | 1 |
| 13 | `dma` | 43 | 30 | `joy_uart` | 1 |
| 14 | `spi` | 3 | 31 | *int timing* | 609 |
| 15 | `i2c` | 13 | 32 | *esxdos hostfs* | 4 |
| 16 | `rtc` | 69 | | **33 sentinels × 4** | **132** |

### 4.1 The flat buffers — fifteen rows, not eight

The buffers inside those blocks, exactly (constants, cross-checked against the
measured block sizes). The **Bytes** column is the buffer's *stream footprint*,
so a count prefix counts toward the row that carries it.

| Buffer | Declared size | Bytes |
|---|---|---|
| `Ram::data_` | `2048 * 1024` + 8-byte count prefix | 2 097 160 |
| `DivMmc` RAM | `kRamSize` = 128 KB | 131 072 |
| `Mmu::bank5_vram_` | 16 KB bank-5 VRAM (`mmu.cpp:983`) | 16 384 |
| `SpriteEngine::pattern_ram_` | `PATTERN_RAM_SZ` | 16 384 |
| `Mmu::bank7_bram_` | 8 KB bank-7 BRAM (`mmu.cpp:980`) | 8 192 |
| `Multiface` RAM | `kRamSize` = 0x2000 | 8 192 |
| `PaletteManager` | 4 palettes × 2 banks × 256 × `u16` + 2 × 256 priority | 4 608 |
| `Ula::port_ff_log_` | `MAX_CHANGES_PER_FRAME` 1024 × (`u16`+`u8`) + 2-byte count prefix | 3 074 |
| UART FIFOs | 2 ch × (RX 512 × `u16` + TX 64 × `u8`), padded to capacity | 2 208 |
| `Copper::instructions_` | 1024 × `u16` | 2 048 |
| `SpriteEngine` attributes | 128 × 5 | 640 |
| `Keyboard::auto_queue_` | `MAX_AUTO_TYPE_KEYS` 16 × 5 × `i32`, padded, + 4-byte count prefix | 324 |
| `Renderer::fallback_per_line_` | 320 × `u8` | 320 |
| `NextReg::regs_` | 256 | 256 |
| `Ula::border_per_line_` | `FB_HEIGHT` 256 × `u8` | 256 |
| **Total flat buffers** | | **2 291 118** |

> Two of those rows **changed on 2026-09-24**, both measured on the S1–S5
> branch rather than re-derived. `Keyboard::auto_queue_` was
> missing entirely. `Ula::port_ff_log_` was listed at 3 072, which is its
> element payload without the `u16` count the same stream carries: the
> `renderer` block's declared widths — `Ula` 3 357 + `Renderer` 327 + `LoRes` 4
> — sum to **3 688**, exactly the measured block, only with the count included.
> Both bytes were previously inside §4.2's residual, so the stream total never
> moved; what moved is the classification.

Five of those rows are **per-scanline / in-flight history**, not static memory,
and they split into two CLASSES that must not be conflated:

- **Count-prefixed and padded to full capacity** — `Ula::port_ff_log_`, the
  UART's four `FifoBuffer`s (2 channels × RX + TX) and `Keyboard::auto_queue_`,
  **six buffers in all**. The padding exists because `RewindBuffer` requires a
  constant width; the count says how much of it is live. Within this one class
  there are **three distinct byte layouts**, differing in count width, element
  form and order (§6.2's table). Two of them get a descriptor primitive each;
  the third — `Keyboard::auto_queue_`, a `u32` count then 16 × 5 × `i32` in raw
  slot order — fits neither, and S5 resolved it by §9.4 loop collapse rather
  than by a third primitive (§9.5(1)).
- **Plain fixed arrays** — `Ula::border_per_line_` and
  `Renderer::fallback_per_line_` are written with a bare `write_bytes` of the
  whole array, no count. Ordinary declarations; they are listed here only
  because they are *history* and §10.3 corrects a claim that they do not travel.

### 4.2 The three-way split

| Part | Bytes | Share |
|---|---|---|
| Flat buffers (the 15 rows above, across 12 owners) | 2 291 118 | 99.919 % |
| Framing sentinels (33 × 4) | 132 | 0.006 % |
| **Genuine scalar/register/FSM state, all 34 classes** | **1 715** | **0.075 %** |

**One thousand seven hundred and fifteen bytes.** That is every register, latch,
FSM state, counter and flag in the whole machine outside a flat buffer.

> **The residual is a SUBTRACTION, and that is its weakness.** It is
> `2 292 965 − buffers − sentinels`, so it can never disagree with the buffer
> table: a buffer the table omits does not show up as an inconsistency, it
> silently inflates the residual. Both of this table's corrections arrived that
> way — an earlier draft put the residual at 32 605 bytes / 1.42 %, wrong by a
> factor of ~16 because it omitted four buffers (30 432 bytes) and folded the
> sentinels in; and the 2 041 that replaced it was still 326 bytes high, because
> §4.1 was missing `Keyboard::auto_queue_` (324) and the `port_ff_log_` count
> prefix (2). Nothing in the arithmetic could have caught either. Only
> enumerating the buffers against the code can, which is what §4.1's note
> records. 2 291 118 + 132 + 1 715 = 2 292 965 exactly.

Every correction has moved the same way, and each makes the Option C argument in
§5 **stronger, not weaker**: the part worth naming, typing, diffing and
schema-validating is not 1.4 % of the stream, it is 0.075 % of it. There is no
size argument against encoding under 2 KB of scalars as JSON, and no
verification argument for encoding 2.29 MB of opaque buffers that way.

### 4.3 Three findings, all measured

> **(1) and (2) below describe the PRE-S5b stream and are both FIXED** — see
> §17.0's "What S5b did". They are left as written because they are the
> measurement S5b acted on, and because both predictions were confirmed against
> the pre-S5b golden byte for byte before the code moved.

1. **131 072 bytes are stored twice, on every machine.**
   `Emulator::init()` does `divmmc_.set_ram_backing(ram_.page_ptr(16))`
   (`emulator.cpp:279`) **unconditionally**, and `DivMmc::save_state` writes
   `ram_data()` (`divmmc.cpp:643`), which returns the external pointer — a
   window into the same 2 MB `Ram` already written. Free after deflate, harmless
   in a ring buffer, but JNS must reference the RAM pages rather than copy them.

2. **The Multiface is the opposite case, and an earlier draft of this document
   got it backwards.** `multiface_.set_ram_backing(ram_.page_ptr(0x0B))` is
   gated on `cfg.type == MachineType::ZXN_ISSUE2` (`emulator.cpp:301`) — but
   `Multiface::save_state` writes `ram_.data()` (`multiface.cpp:373`), the
   **private** array, not `ram_data()`. So on the **Next** the private array is
   never written by the machine and those 8 192 bytes are **dead zeros** in
   every snapshot (the live MF RAM is page 0x0B, already inside `mem/ram.bin`);
   on **48K/128K/+3** there is no backing and the same 8 192 bytes are
   **genuine private state JNS must carry**. The consequence for §6 and §10: the
   Multiface RAM member is **machine-type conditional**, present only on the
   standalone machines, and absent (not zero-filled) on the Next.

3. **`Multiface::mf_type_` is knowingly lossy — a direct G1 violation.**
   `Multiface::load_state` (`multiface.cpp:383-400`) reconstructs `mf_type_`
   from three mode booleans, and its own comment states that "save state from a
   session running `mf_type=10` will lose the bit", accepted at the time rather
   than bumping the schema. A rewind can absorb that; a snapshot the user
   expects to resume cannot. §10.2 carries it as a gap JNS must close by
   serialising the 2-bit value.

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

- *Against, decisively*: the 2.29 MB of flat buffers become ~3.1 MB as base64 or
  ~6–7 MB as decimal arrays, before compression, and cost a parse of every one
  of two million values on load. The gain over Option C for those bytes is
  **zero**, because a schema cannot say anything useful about them. Paying
  megabytes and a parse for no verification is not a trade.
- A second, quieter problem: JSON has one number type. `nlohmann` keeps
  `uint64_t` exactly, but an *external* validator written in JavaScript (`ajv`)
  would silently round anything above 2^53. See §7.4 for the rule this imposes.

**Option C — ZIP container: JSON manifest + JSON per-subsystem state + binary
blob members for the flat buffers.** ← **recommended**

- *Size*: the **0.075 %** that is genuine scalars (1 715 bytes, §4.2) becomes
  roughly **15–30 KB** of JSON text — key names dominate, not values. The
  99.9 % stays binary. Both are deflated by the ZIP itself. Expected file:
  **~130–140 KB** for a full 2 MB-RAM Next snapshot, against the **128 657
  bytes** the warm-start cache measures for the same machine as one opaque
  deflate stream. **The text costs single-digit kilobytes, not the 1.5–2× an
  earlier draft of this document estimated from the wrong residual.** Two of
  the padded-to-capacity history buffers (§4.1) shrink rather than grow in JSON,
  because JSON carries `count` entries where the binary carries `capacity`.
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

**Option C** — confirmed by the owner, 2026-09-23. The deciding argument is
§4: the state divides cleanly, and almost perfectly, into a tiny part that
benefits enormously from being named, typed and externally validatable
(**0.089 %**), and a huge part that benefits not at all (**99.9 %**). A hybrid
is not a compromise here — it is the shape of the data, and the measurement is
far more lopsided than it first appeared.

The trade-off, stated plainly, and **smaller than an earlier draft claimed**:
size is essentially a wash — ~130–140 KB against the 128 657 bytes an opaque
deflate of the same machine measures, because JSON is carrying two kilobytes of
scalars, not thirty-two. **The real cost is code and dependency: ~600 lines of
first-party container code plus one vendored JSON header that Option A would
not need.** In exchange, a snapshot is something a user can open with tools they
already have, a test can validate with an implementation we did not write, and a
reviewer can read in a diff. At a few kilobytes of overhead, that is not a close
call.

### 5.3 What the schema can and cannot do

The owner's motive is verifiability, so this needs to be exact.

**A JSON Schema validates**: the presence and absence of keys; types; numeric
ranges (`minimum`/`maximum` per hardware register width); string patterns
(exact-length hex strings, so a 2 KB fixed array is length-checked *by the
schema*); enumerations (machine types, FSM state names); array lengths; and —
via `additionalProperties: false` where we want it — that no unexpected key
appeared. It also makes the file **self-describing**: the schema ships beside
the format and answers "what is this field?" without reading jnext's source.

> **Correction (S2, 2026-09-24): a JSON Schema does NOT catch a duplicate
> key,** and an earlier revision of this section listed one among the things it
> does (the claim survives in §13.2(3)'s list, which is wrong for the same
> reason). No validator can: JSON is parsed to a document *before* a schema is
> applied, and every mainstream parser has already resolved `{"x":1,"x":2}` to
> one member by then — nlohmann resolves it to `{"x":2}`, measured. The reader
> is therefore the only layer that can refuse it, and `JsonReadDesc` does, via
> a parse-time key-set guard. It matters for the same reason §12.4 refuses
> duplicate ZIP *member* names: two implementations may legitimately disagree
> which wins.
>
> The same guard bounds DOCUMENT DEPTH at 16. Not as a stack-overflow
> defence — nlohmann's parser and its DOM destructor are both iterative,
> measured clean to 5 000 000 levels — but because a member of `[` characters
> becomes one DOM node per byte, and S1's central-directory cap lets a member
> be 64 MB. That is the 167-byte-archive class one layer down.

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
├── manifest.json               REQUIRED, first member
├── state/clock.json            one per subsystem, named for the subsystem
├── state/mmu.json              (slots + ports; its two RAM buffers are blobs)
├── state/nextreg.json          (256 registers as hex + the shadow scalars)
├── state/cpu.json
├── state/im2.json              (+ the int_timing block, §9.4)
├── state/palette.json          (4 × 2 × 256 entries — JSON, not a blob)
├── state/layer2.json
├── state/sprites.json          (attributes + registers; patterns are a blob)
├── state/tilemap.json
├── state/renderer.json         (+ fallback_per_line)
├── state/ula.json              (+ border_per_line + the port-0xFF change log)
├── state/lores.json
├── state/copper.json           (registers + the 1K instruction RAM as hex)
├── state/ctc.json              (+ its chained-trigger timing, §9.4)
├── state/dma.json
├── state/spi.json
├── state/sdcard.json           NEW — the SPI FSM, §10.2 P1
├── state/i2c.json
├── state/rtc.json
├── state/uart.json             (FIFOs as count-length arrays, not padded)
├── state/divmmc.json           (registers only; its RAM is a RAM window, §4.3)
├── state/multiface.json        (registers + mf_type; RAM only when private)
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
├── state/joy_uart.json         present only when a cable is attached
├── state/esxdos_hostfs.json    handles + cwd + root
├── state/emulator.json         Emulator's own scalars, §10
├── mem/ram.bin                 2 097 152 B
├── mem/bank5-vram.bin          16 384 B  (Mmu::bank5_vram_)
├── mem/sprite-patterns.bin     16 384 B
├── mem/bank7-bram.bin           8 192 B  (Mmu::bank7_bram_)
├── mem/multiface-ram.bin        8 192 B  — 48K/128K/+3 ONLY, see §4.3(2)
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
  reader refuses any archive containing a path that does not match
  `^[a-z0-9][a-z0-9._/-]*$` **and** that fails any of the component rules
  below. A snapshot is not an extraction target, and zip-slip is not a hazard
  we accept even in a private format.

  > **The regex ALONE is not sufficient, and an earlier draft of this section
  > implied it was.** Both `.` and `/` are inside the character class, so
  > `state/../../etc/passwd` **matches** `^[a-z0-9][a-z0-9._/-]*$` and is still
  > a traversal. Anyone implementing from the regex and the prose "no `..`"
  > would ship the hazard this bullet exists to refuse. Found while
  > implementing S1; the rules that actually close it are therefore stated,
  > not left to the reader:
  >
  > 1. the path is non-empty and at most 255 bytes;
  > 2. its first character is `[a-z0-9]` — this, not the character class, is
  >    what rejects an absolute path, a leading `.` and a leading `-`;
  > 3. every character is in `[a-z0-9._/-]` — this is what rejects a
  >    backslash (which Windows tooling treats as a separator, so it is a
  >    traversal in disguise), every upper-case letter, and every control or
  >    non-ASCII byte;
  > 4. **no component is empty** — which is what rejects a trailing `/` (a ZIP
  >    directory entry, never written), a leading `/` and a doubled `//`;
  > 5. **no component is exactly `.` or `..`** — the rule the regex cannot
  >    express at all.
  >
  > The grammar applies to the manifest's own `members` keys as well as to the
  > archive's entries, and is checked on the writer's side too, so a bad path
  > cannot enter an archive in the first place.
- **Duplicate member names are a REFUSAL.** ZIP permits them and real readers
  disagree about which one wins — some take the first central-directory entry,
  some the last, some the last *local* header. That is the `.szx` failure shape
  exactly: a file our reader resolves one way and an external tool resolves
  another, with both believing they succeeded. The writer never emits one and
  the reader refuses the archive outright rather than picking.
- **Order is the write order above, but a reader must not depend on it.** It is
  chosen so `unzip -l` reads sensibly and so `manifest.json` is first.
- **Members may be `STORED` or `DEFLATE`.** Default `DEFLATE` level 9 (the
  warm-start cache's reasoning applies: written once, read often).
  `--snapshot-uncompressed` writes everything `STORED` — the settled point-6
  debugging mode, and one flag on the member writer rather than a second code
  path.
- **ZIP64 is not written and is refused on read.** No member can approach 4 GB;
  writing ZIP64 would add a second framing to test for no reachable benefit.
- **A single member may declare at most 64 MB, compressed or uncompressed, and
  a larger declaration is refused before anything is allocated from it.**
  This is a format rule, not an implementation detail, and it was added after
  S1's review measured what its absence costs.

  A ZIP member's uncompressed length is a bare 32-bit field in the central
  directory, and **nothing in the archive's structure relates it to the
  compressed bytes actually present**. A 161-byte archive can legitimately
  declare a 4 GB member; a reader that sizes its output buffer from that
  declaration allocates 4 GB (measured: 4 198 256 KB resident, 1 048 798 minor
  faults) on a file that fits in a packet, and on a memory-constrained host
  throws `std::bad_alloc` instead — which, uncaught, **terminates the
  emulator**. That is the inversion of G9: a hostile file must be refused
  loudly, never abort and never restore something plausible.

  The ceiling is checked in the central-directory walk, so it refuses at open
  time, before a member is read and before any consumer of the entry list can
  size an allocation from it. 64 MB against a largest legitimate member of
  2 097 152 bytes (`mem/ram.bin`, §6.1) is about 32x headroom, so no file
  jnext writes can approach it.

  A *ratio* bound — uncompressed ≤ compressed × DEFLATE's maximum expansion —
  was considered and rejected: 2 MB of zero-filled guest RAM, the single most
  likely real blob, deflates at a ratio near 1000:1, close enough to the
  1032:1 theoretical maximum that the bound would risk refusing legitimate
  files. A bound that rejects real snapshots is worse than a generous one that
  merely caps the damage.
- **The reader accepts only the exact ZIP subset the writer emits; any other
  feature is a refusal.** This is a rule rather than a list, and it is deliberate:
  ZIP has a long tail — a local header whose CRC or sizes disagree with the
  central-directory entry for the same member, a member carrying a data
  descriptor (general-purpose flag bit 3), encryption, multi-disk spanning,
  compression methods other than 0 and 8 — and enumerating it invites the next
  feature to arrive undecided. A `.jns` is not a general archive and there is no
  interoperability to preserve (settled points 1 and 2), so the permissive
  direction has nothing to buy and a silently-differing interpretation to lose.

### 6.1 What goes in JSON, and what goes in a blob

**Three cases, and the middle one has an honest size threshold.**

> 1. **Guest memory in the CPU address space → a BLOB**, whatever its size.
> 2. **Guest memory that ALIASES another blob → a REFERENCE**, stored once.
> 3. **Peripheral stores reachable only through ports or NextREG → JSON below
>    8 KB, a blob at or above it.** For this class **size is the operative
>    criterion**, not a tie-breaker.
>
> Everything else — register files, FSM state, per-frame history — is JSON
> whatever its size.

| Buffer | Case | Result |
|---|---|---|
| `Ram`, `Mmu::bank5_vram_`, `Mmu::bank7_bram_` | 1 | blob |
| `Multiface` private RAM (48K/128K/+3) | 1 | blob, machine-conditional |
| DivMMC RAM (a window onto `Ram` page 16) | **2** | **reference** |
| `SpriteEngine::pattern_ram_` 16 384 B (port `0x5B`) | 3, ≥ 8 KB | blob |
| `Copper::instructions_` 2 048 B (NR `0x60`-`0x63`) | 3, < 8 KB | JSON |
| `PaletteManager` 4 608 B (NR `0x40`-`0x44`) | 3, < 8 KB | JSON |
| `NextReg::regs_` 256 B | 3, < 8 KB | JSON |

An earlier draft stated only "guest-writable memory is a blob, size is a
tie-breaker", **and that rule cannot decide two of the fifteen buffers**: the
Copper instruction RAM and the sprite pattern RAM are the same class — peripheral
stores outside the CPU address space, written by the guest only through ports —
yet §6 correctly puts one in JSON and the other in a blob. The only thing
separating them *is* size, so the threshold is now stated rather than disowned.
It also missed the reference case entirely, although §4.3(1) depends on it.

The 8 KB line is a judgement, and the reason to draw it there: below it a hex
string still diffs usefully and its exact length is checkable *by the schema*;
above it the text doubles a buffer nobody reads. `Mmu::bank7_bram_` is exactly
8 192 and would sit on the line — but it is case 1, in the CPU address space, so
the threshold never applies to it.

### 6.2 Encoding rules inside the JSON

| Kind | Encoding | Schema can check |
|---|---|---|
| Boolean flag | `true` / `false` | type |
| Unsigned ≤ 32 bits | JSON number, decimal | type, `minimum`, `maximum` |
| **Signed 32-bit (`i32`)** | JSON number, may be negative | `minimum: -2147483648` |
| **`u64` / `i64`** | JSON **string** of decimal digits, sign allowed | `pattern` — see §7.4 and the note below |
| **Open-ended sentinel** (`INT64_MAX`) | the JSON string `"open"` | `enum` alongside the numeric pattern |
| Enum / FSM state | JSON string from a closed set | `enum` — an FSM renumbering becomes a *name* change, visible in a diff |
| Fixed array, not guest memory | one lower-case hex string, no separators — **element order UNDECLARED for multi-byte elements, §18.2(7)** | `pattern: "^[0-9a-f]{N}$"` with N literal — **exact length checked by the schema**, but blind to element order |
| **Count-prefixed history** (see below) | JSON array of exactly `count` items | `maxItems` = the binary capacity |
| Variable-length list | JSON array of objects | `items`, `minItems`/`maxItems` |
| Guest memory | ZIP member, declared in `manifest.members` | the *declaration*, not the bytes (§5.3) |

**The patterns are per-type and CANONICAL, not the one `^-?[0-9]+$` an earlier
revision gave for both** (S2 correction). A `u64` cannot be negative, so its
pattern is `^(0|[1-9][0-9]*)$` and an `i64`'s is `^(0|-?[1-9][0-9]*)$`: a shared
signed pattern would let a schema accept `"-1"` for an unsigned field, which is
a hole in the direction that matters. Both forbid leading zeros and `-0`, so
there is exactly ONE spelling per value — otherwise two documents could mean
the same state and differ under a byte-diffed gate. `JsonReadDesc` accepts
exactly the same grammar, deliberately: a schema looser than the reader makes
the validator lie in the more dangerous direction, by passing a file jnext
refuses.

**`i32` and `i64` are not optional additions.** The tree makes **11 `write_i32`
calls** today — `Clock::cpu_divisor_`, `Im2Controller::last_acked_`,
`Uart::select_`, `Ula::flash_counter_`, `Z80Cpu`'s
`interrupts_enabled_at`, and six in `Keyboard`'s auto-type queue — and an
encoding table with no signed type would have forced every one of them through
an unsigned reinterpretation, which is precisely the defect §7.4 describes.

**The count-prefixed-history primitives — `d.log()` and `d.fifo()`, not one
`d.history()`.** **Six** buffers (§4.1) are written `count`-first and then
**padded to full capacity**, because `RewindBuffer` requires every snapshot to be
exactly the width it measured at construction. The binary encoding **must** stay
padded; the JSON encoding **must not** be, or a snapshot's text would carry 1 024
entries to express three.

A single signature cannot reproduce the byte layouts, and the byte-identity gate
(§17.1) tests every one of the differences. There are **three** layouts, not two:

| | `Ula::port_ff_log_` (`ula.cpp:1586-1590`) | UART `FifoBuffer` (`uart.h:53-57`) | `Keyboard::auto_queue_` (`keyboard.cpp:679-691`) |
|---|---|---|---|
| Count width | **`u16`** | **`u64`** | **`u32`** |
| Element | struct: `u16 line` + `u8 value` = 3 B, unpadded | scalar via `write_elem` → `u8` (TX) / `u16` (RX) | 5 × `i32` per slot = 20 B |
| Order | **raw array order**, `port_ff_log_[i]` | **ring-normalised**, `buf_[(tail_+i) % Capacity]`, oldest first | **raw slot order**, `auto_queue_[i]` |
| Padding past `count` | whatever was there — **stale entries**, ignored on load | **`T{0}`** | **`AutoKey{}`** — zeros |
| Capacity | 1 024 | 512 (RX) / 64 (TX) | **16** |

Two of the three get a primitive, each pinning its own layout:
`d.log(name, entries, count, Capacity)` — `u16` count, raw order, stale
tail — and `d.fifo(name, ring, elem)` — `u64` count, ring-normalised,
zero-padded. Both emit exactly `count` items in JSON.

The third gets **no primitive at all**. A single parameterised
`d.history(count_type, elem, pad_policy, ring_or_raw)` was named here as the
alternative "if a third shape ever appears"; one has, and S5 declined it. The
auto-type queue's capacity is **16**, so §9.4's loop collapse spells it out as
80 ordinary `i32` declarations and costs nothing but a key table — whereas a
third primitive would buy one 324-byte buffer a fourth parameter axis in a
vocabulary already hard to read at a call site. **That answer is a function of
the capacity, not of the shape**: at 512 slots the same reasoning inverts, and
the parameterised `d.history()` becomes the right call. See §9.5(1).

The hazard behind that requirement is on record: `AttributeMux`
(`src/memory/attribute_mux.h:216-235`) documents that serialising its
variable-length log "was tried first and was the actual bug behind a
`free(): invalid size` heap-corruption crash", because slot width varied frame
to frame. The padding is load-bearing, not stylistic.

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

and the manifest's declaration of the blobs it does not hold:

```json
"members": {
  "mem/ram.bin":             { "bytes": 2097152, "crc32": "8f3a21bd" },
  "mem/bank5-vram.bin":      { "bytes": 16384,   "crc32": "5511aa02" },
  "mem/sprite-patterns.bin": { "bytes": 16384,   "crc32": "0c19ee40" },
  "mem/bank7-bram.bin":      { "bytes": 8192,    "crc32": "77c3b118" }
}
```

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

**And that rule has a cost that must be paid in the same design, or it becomes
a data-loss machine.** Renaming is now the *routine* way to change a field —
while §12's reader rule is "ignore unknown keys, default missing ones". Compose
the two and every rename is a silent, total loss of that field: the old file's
key is unknown to the new reader, the new reader's key is missing from the old
file, and nothing anywhere reports it. The fix is cheap and must be committed
alongside the schema:

> **`doc/formats/jns-retired-names.json`** — a table mapping every retired key
> to either its successor (`{"old": "state/uart.sel", "new": "state/uart.select"}`)
> or an explicit tombstone (`{"old": "state/ula.armed", "new": null,
> "reason": "removed Task 8 round 3; no successor"}`).

The reader consults it before applying §12's ignore-unknown rule: a key in the
retired table is **migrated**, not ignored, and a tombstoned key is ignored
*deliberately and silently*. A key in neither table is what "unknown" then
actually means. The file is generated-adjacent but **hand-written**, because
only a human knows whether a rename was a rename or a replacement — and it is
therefore reviewed, which is the point.

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
| `model.machine` ≠ the machine currently constructed | **Reconfigure and restore.** The reader calls `init()` with `model.machine` and then applies — exactly what `Emulator::load_snapshot_buffer` already does for `.sna`/`.szx`/`.z80` (`emulator.cpp:8066-8083`, GH #239). The user must not have to get `--machine` right to reload their own save. (An earlier draft of this row said "Refuse" while justifying reconfiguration in the same sentence, and contradicted §8; the refusal is deleted.) |
| `model.state_model_revision` ≠ this build's | **Restore, loudly.** One log line and a GUI status-bar note naming both revisions and both jnext versions. `--snapshot-strict` turns this into a refusal. |
| An **unknown** member or key | Ignore it, and log one line per subsystem that had any (§12). |

**Policy on dropping an old reader**: a `format_version` bump ships with the
previous grammar's reader retained for at least one public minor release, and
its eventual removal is a ChangeLog line under *User Features*. This is the
"better than MAME, but no promise" posture settled point 5 asks for, stated as a
rule rather than an intention.

### 7.4 The 2^53 rule — and the fields that already break it

JSON has one number type. `nlohmann/json` round-trips `uint64_t` exactly, and so
does Python's `json` (arbitrary-precision ints) — but a JavaScript validator
(`ajv`) silently rounds above 2^53, which would let an external validation pass
on a file a JavaScript reader had already corrupted.

**Rule: any `u64` or `i64` field is encoded as a decimal *string*,** with
`"pattern": "^-?[0-9]+$"` in the schema. Not "any field that might one day be
large" — every 64-bit field, unconditionally, because the alternative is a
per-field judgement that goes stale.

> An earlier draft of this document asserted that **no field qualifies today**,
> reasoning that `monotonic_tstates()` and `Clock::cycle_` need about ten years
> of continuous emulation to reach 2^53. That reasoning was correct and the
> conclusion was **false**, because it only considered fields that *count up*.

**Measured, in every snapshot this build writes.** `emulator.cpp:11794-11798`
writes the CPU's `/INT` window as **signed deltas reinterpreted as `uint64_t`**:

```cpp
const int64_t now_ts = static_cast<int64_t>(*fuse_z80_tstates_ptr());
const int64_t last   = cpu_.int_window_last_ts();
w.write_u64(static_cast<uint64_t>(cpu_.int_window_first_ts() - now_ts));
w.write_u64(static_cast<uint64_t>(last == INT64_MAX ? last : last - now_ts));
```

A window that opened in the past is a **negative** delta, and the cast makes it
a value just under 2^64. Real values from the live stream: `int_window_first =
18446744073708986683` (= -564 933 signed) and `int_window_last =
18446744073708986714`. Both are roughly **2 000× above 2^53**, in *every*
snapshot, not in some rare future one.

Three consequences, all now in §6.2:

1. **Signed types are first-class.** `i32` and `i64` exist in the encoding
   table. The `/INT` window is `i64` and emits `"-564933"`, which is both
   correct and legible — the unsigned reinterpretation exists only because the
   binary stream had no signed 64-bit primitive, and a named format has no
   reason to inherit it.
2. **`INT64_MAX` gets a name.** It is written verbatim as an open-ended-window
   sentinel, and as a 19-digit string it is indistinguishable from a real value
   to every reader including a human. In JSON it is the string **`"open"`**, and
   the schema accepts `"open"` or the decimal pattern. A sentinel that looks
   like data is a bug waiting for someone to do arithmetic on it.
3. **The rule is enforced by the descriptor, not by discipline.** A `u64` or
   `i64` declared through §9's `StateDesc` emits a string and the schema
   generator writes the pattern. There is no site where a developer chooses.

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
    "frame_boundary": true
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
  calls `init()` with it and then applies, the same way
  `Emulator::load_snapshot_buffer` already does for `.sna`/`.szx`/`.z80`
  (`emulator.cpp:8066-8083`). The user should not have to remember `--machine`.
- **`model.ram_kb` is always 2048 today, and the key exists anyway.** There is
  no configurable RAM size: `Ram ram_;` (`emulator.h:1126`) takes the
  `2048 * 1024` default and no CLI flag or `EmulatorConfig` field changes it.
  The key is written because it is what makes `mem/ram.bin`'s declared length
  *checkable* by an external validator (§13's constraint overlay asserts
  `ram_kb * 1024 == members["mem/ram.bin"].bytes`), and because the hardware's
  768K/2048K distinction means a future flag is plausible. A reader refuses any
  value it cannot construct rather than assuming 2048.
- **`capture.frame_boundary` is always `true`, or the file was not written.**
  It is recorded as a fact a reader can check, not as a mode. There is
  deliberately **no `"paused"` key**: see §10.2 P7 — a mid-frame pause is a
  state a `.jns` cannot be written from at all, so a flag saying so would
  describe a file that does not exist.
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
    d.ram_window("ram", ram_ext_, kRamSize, /* page */ 16);  // -> a reference
    // …
}
```

**`ram_window` vs `blob` is a STATIC declaration, and that is a decision, not
an oversight.** The property it describes — whether a buffer is a window into
main RAM or a private array — is established at run time by `init()`
(`emulator.cpp:279`, `:301`), so a descriptor could in principle be asked to
express "window when `ram_ext_ != nullptr`, blob otherwise". **It is not**,
because a run-time-shaped field list is the complexity this whole layer exists
to avoid, and because the two cases are each unconditionally true today:

- **DivMMC is always a window.** `set_ram_backing` is unconditional and
  `save_state` writes `ram_data()`. Declared `ram_window`, always. **Since S5b
  that declaration is load-bearing in the binary encoding too**: at machine
  level a `ram_window` emits no bytes, the restore resolving it through the
  `ram` block (§17.0).
- **Multiface is always private *in the stream*.** `save_state` writes
  `ram_.data()` regardless of backing (§4.3(2)). On the Next that array is dead
  zeros and the live 8 KB already travels inside `mem/ram.bin`; on
  48K/128K/+3 it is the real thing. Declared `blob`, and **emitted only on the
  machines where it is live** — the manifest's `subsystems` list already
  expresses a conditionally-present member, so the Next simply has no
  `mem/multiface-ram.bin` rather than 8 KB of zeros.

The static claim is then **asserted at run time** so it cannot go stale: a
`ram_window` declaration checks `ram_ext_ != nullptr` and fails loudly if it is
null. A comment claiming "always" with nothing checking it is how an earlier
draft of §4 came to state the Multiface case backwards.

> **That assertion is still unable to FIRE, and S5b left it that way on
> purpose** (§17.0). `DivMmc` derives `machine_level` from the very pointer the
> assertion would check, which makes it a tautology; the resulting branch is
> fail-safe instead — an unbacked window at "machine level" is not a machine
> level walk at all, so the bytes travel inline as they always did. It becomes
> real for the Emulator-driven JSON realisation in S6, where the same fault
> would write a reference to bytes no member carries. `snapshot_test` rows
> `JNSD-J08`/`J09` keep the mechanism under test meanwhile.

**Scope that assertion to machine-level saves, or it breaks a shipped test.**
Row `DA-09` in `divmmc_test.cpp:1184-1191` builds a `DivMmc` from the suite's
`make_divmmc()` helper — which calls `reset()`, `set_enabled()`,
`set_nr_0a_4_enable()` and `set_entry_timing_0()`, and **never
`set_ram_backing()`** — then calls `save_state` on it directly. A bare null-check
inside the descriptor fires on that row. Round-tripping a subsystem standalone is
a legitimate and useful thing for a unit test to do, so the assertion belongs to
the **`Emulator`-driven** realisation of the descriptor, where `init()` has
provably run, and not to the declaration itself. (`multiface_test.cpp:419-422`
does the same thing and is unaffected: Multiface is declared `blob`, not
`ram_window`.)

`StateDesc` is an interface with several realisations over the *same*
declaration:

| Realisation | Produces | Replaces |
|---|---|---|
| `MeasureDesc` / `BinWriteDesc` / `BinReadDesc` | the positional byte stream, in declaration order | today's hand-written `save_state`/`load_state` — same bytes, same speed, same fixed width |
| `JsonWriteDesc` / `JsonReadDesc` | the named-key JSON of §6 | — |
| `SchemaDesc` | the JSON Schema for that subsystem | — |

Two signature corrections S2 made while implementing that table, both because
the binary realisation needs something the §9.2 sketch above left out.
`ram_window` gained the buffer POINTER (the binary encoding writes the bytes
inline, which is exactly the duplication S5b removes), and `MeasureDesc` is not
a third class: it is `BinWriteDesc` over a measure-mode `StateWriter`, so it
cannot drift from the writer it measures — it *is* the writer.

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

### 9.4 How much of the tree the descriptor actually covers

The migration was sized by classifying **all 532 `StateWriter` call sites** in
the tree, not by sampling:

| Class | Sites | Treatment |
|---|---|---|
| A bare member variable | **404 (76 %)** | one `d.<type>("name", member_)` line, mechanical |
| Fixed arrays | ~30 | `d.bytes(...)` / `d.blob(...)` |
| Array elements inside loops | 29 sites in 25 loops | **collapse** — `palette.cpp`'s 10 loops become 5 declarations |
| Enum casts | 23 sites over ~20 enums | one `d.enum8(...)` each, plus a name table per enum |
| Accessor calls (no member to bind) | 7 | a get/set descriptor pair |
| **Framing sentinels** | **33** | **not fields at all** — see below |
| Genuinely non-descriptor | ~20 sites in 8 places | hand-written, §9.5 |

Three consequences the estimate in §17 rests on: three quarters of the work is
a mechanical one-line-per-field transcription; the loop sites *reduce* the
declaration count rather than adding to it; and the enum sites are the only ones
that need net-new artefacts (a name table apiece), which is also where the
format gains the most — an FSM renumbering becomes a visible name change.

**The 33 sentinels are the one class that cannot be a declaration.** They are
framing, not state: they must *survive* in the binary encoding (where they are
the only thing that localises a desync) and must *vanish* in JSON (where member
and key names do that job structurally). So `BinWriteDesc` emits a sentinel at
each block boundary and `JsonWriteDesc` emits nothing — a property of the
realisation, not of any declaration.

### 9.5 Where the descriptor does not fit — all eight places

An earlier draft named three of these. All eight, from the classification:

1. **Count-prefixed-and-padded history** — `Ula::port_ff_log_`, the UART's
   four FIFOs **and `Keyboard::auto_queue_`**: six buffers, three layouts
   (§4.1, §6.2). **Two primitives, `d.log()` and `d.fifo()`**, because those
   two layouts differ in count width, element form and padding policy
   (§6.2's table) and the byte-identity gate tests every one of those
   differences. The binary form must stay padded to capacity because
   `RewindBuffer` requires constant width; the JSON form must carry exactly
   `count` items. **One declaration, two ENCODINGS**, so these are explicit
   primitives — `d.log("port_ff_log", entries, count_, MAX_CHANGES_PER_FRAME)`
   and `d.fifo("rx_fifo", ring, FifoElem::U16)` — not something a plain array
   descriptor can express. (An earlier revision gave a single
   `d.history(...)` here, contradicting the two-primitive decision the same
   paragraph makes and §6.2 restates; corrected at S2, which implemented the
   two.) `entries`/`ring` are non-owning ACCESSORS rather than raw arrays, so
   migrating the ULA is one declaration line and not a rewrite of the struct
   the renderer indexes into. The hazard is on record:
   serialising a variable-length log "was tried first and was the actual bug
   behind a `free(): invalid size` heap-corruption crash"
   (`src/memory/attribute_mux.h:216-235`).

   **That two-primitive decision was sized against FIVE buffers and two
   layouts, and the sixth was not in the inventory it was sized against** —
   §4.1 omitted `Keyboard::auto_queue_` entirely and this paragraph named only
   the UART's four beside the ULA's one. S5 found the third layout (a `u32`
   count, then `MAX_AUTO_TYPE_KEYS` × 5 × `i32` in raw slot order, 324 bytes)
   and confirmed against the **pre-migration** source
   (`git show 00aef129^:src/input/keyboard.cpp`) that the hand-written pair
   already wrote exactly that — so it is a gap in the inventory, not a shape
   the migration introduced. **S5 did not add a third primitive.** The queue is
   declared as **80 ordinary `i32` fields** — §9.4's loop collapse, marshalled
   through a local staging array — plus the `u32` count and a literal key
   table. That answer turns entirely on `MAX_AUTO_TYPE_KEYS` being **16**:
   eighty declarations and eighty key literals stay legible, and the same
   treatment at 512 slots would not, at which point §6.2's parameterised
   `d.history()` becomes the right call instead. Recorded as
   **capacity-dependent, not as a precedent**.
2. **A second serialisation entry point per subsystem.**
   `Im2Controller::save_timing` and `Ctc::save_timing` are called from a
   *different block* than those subsystems' own — block 31, `int_timing`, at the
   very end of the stream (GH #265), not blocks 5 and 12. One `describe_state`
   cannot put its fields in two blocks, and S2's byte-identity gate (§17)
   forbids moving them. **Answer: two describe methods**, `describe_state` and
   `describe_timing`, each bound to its own block, with the JSON side free to
   merge `describe_timing`'s keys into `state/im2.json` and `state/ctc.json`
   where they belong logically.
3. **Values written relative to something the stream does not carry** — the
   CPU's `/INT` window, a delta against the FUSE T-state counter that
   `load_state` re-seeds at the next frame start (§7.4).
4. **Variable-length lists** — the esxDOS host-FS handle table (path, offset,
   mode per open handle) and the joystick-cable cursor.
5. **Presence-flag-gated optional blocks** — `joy_uart` writes a `bool` and then
   a whole subsystem only when a cable is attached. In JSON this is simply an
   absent member, so the flag itself does not travel.
6. **Reconstructed-on-load fields** — `Multiface::mf_type_`, today rebuilt from
   three booleans and knowingly lossy (§4.3(3)). JNS serialises the 2-bit value
   directly, which makes this a *fix* rather than an exception, and the three
   booleans stay as ordinary declarations.
7. **Cross-subsystem re-syncs** — the `Ula`'s copies of the palette-bank
   selectors and the attribute mux, rebuilt from `PaletteManager` and VRAM after
   a load (GH #261). These are *derived*: a derived field is never written, in
   either encoding.
8. **Rebuilt-every-frame state that is deliberately absent** — `AttributeMux`'s
   log/baseline/current, rebuilt by `start_frame()` before any CPU execution.
   Not a descriptor exception so much as a documented non-participant, recorded
   here so "complete" in §10 is true.

Each is named in its subsystem's doc-comment, so "hand-written" stays a declared
exception rather than a habit.

## 10. The complete state inventory

Every subsystem, its serialisation today, and what JNS must do. "Covered" means
the existing `save_state`/`load_state` is believed complete for that subsystem;
JNS re-expresses it through the descriptor and does not add state.

### 10.1 Covered by the existing stream — re-express, do not extend

| Area | Class(es) | Notes for JNS |
|---|---|---|
| Clock | `Clock` | scalars |
| RAM | `Ram` | → `mem/ram.bin` blob; size from `model.ram_kb` |
| MMU | `Mmu` | 8 slot page numbers + 128K/+3 port shadows + the attribute-mux scanline cursor. **Plus two guest-RAM buffers an earlier draft missed entirely**: `bank5_vram_` (16 KB) and `bank7_bram_` (8 KB), both blob members (§6.1). |
| NextREG | `NextReg` | all 256 registers as one hex string + the shadow scalars |
| CPU | `Z80Cpu` | registers, IFF, IM, halt, the stackless-RETN latch, the `/INT` window (§9.4) |
| Interrupts | `Im2Controller` | + `save_timing` (GH #265) |
| Palettes | `PaletteManager` | 4 × 2 × 256 `u16` + priority; JSON, not a blob (§6.1) |
| Layer 2 | `Layer2` | registers + clip window |
| Sprites | `SpriteEngine` | 128 × 5 attribute bytes in JSON; 16 KB pattern RAM → blob |
| Tilemap | `Tilemap` | registers + clip window |
| LoRes | `LoRes` | registers |
| ULA / renderer | `Ula`, `Renderer` | **Three per-scanline histories ARE written and must travel**: `Ula::port_ff_log_` (count + 1024 padded entries, 3 072 B), `Ula::border_per_line_` (256 B) and `Renderer::fallback_per_line_` (320 B). Everything else in the render history — the change logs with their frame-start baselines and the per-line snapshot arrays — is rebuilt by `load_state` from the restored registers (GH #261). An earlier draft of this row asserted the blanket "not written" rule and was wrong; see §10.3. |
| Copper | `Copper` | 1K instruction RAM as hex + PC/mode |
| CTC | `Ctc` | 4 channels + `save_timing` |
| DMA | `Dma` | FSM + registers |
| SPI | `Spi` | master FSM |
| I2C + RTC | `I2c`, `I2cRtc` | bit-bang state + RTC registers |
| UART | `Uart` | prescaler + the two FIFOs per channel, written count-prefixed and **padded to capacity** (2 208 B) — the `history` primitive of §9.5(1) |
| DivMMC | `DivMmc` | registers; its 128 KB RAM is a **window into `Ram`** (§4.3(1)), unconditionally — JNS references, does not copy |
| Multiface | `Multiface` | FFs + the mode booleans. Its 8 KB RAM is **private, not a window**, in the stream (§4.3(2)): dead zeros on the Next, real state on 48K/128K/+3, so the blob member is machine-conditional. `mf_type_` must become a real field (§10.2 P13). |
| NMI | `NmiSource` | + `prev_nmi_generate_n_` |
| Audio | `Beeper`, `TurboSound` (3 × `AyChip`), `Dac`, `I2s` | chip registers, envelope/LFSR phase |
| Input | `Keyboard`, `Joystick`, `KempstonMouse`, `Md6ConnectorX2`, `MembraneStick`, `IoMode` | Task 60c. Host-side `JoystickDispatcher`/`MouseDispatcher` are platform-owned and re-seeded by the `on_input_state_restored` callback — JNS keeps that. |
| Joystick cable | `JoyUartSource` | optional; present only when attached (GH #251) |
| esxDOS host FS | hand-rolled in `Emulator` | (path, offset, mode) per handle + cwd. **The precedent for §11**: an external resource is recorded by reopenable identity, not copied. |
| **Deliberately excluded, documented in place** | `AudioMute` (`src/audio/audio_mute.h:20-26`), `AttributeMux` (`src/memory/attribute_mux.h:216-235`) | Named here so "complete" is true. `AudioMute` is the user's volume knob, not machine state — "a rewind must not silently un-mute", **and that reasoning transfers verbatim to a file snapshot**: restoring somebody's save must not move their mute settings. `AttributeMux` is rebuilt from live RAM by `start_frame()` before any CPU execution; serialising its log caused a heap-corruption crash (§9.5(1)). Neither travels in a `.jns`. |
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
| **P7** | **Scheduler queue — and the mid-frame pause it forbids.** `emulator.cpp:11871` states snapshots "are only ever taken at a frame boundary (`begin_new_frame()`), so a restored machine has no frame in flight". The queue is empty exactly there and nowhere else. **But the debugger breaks MID-frame**, so a paused machine is normally not at a boundary — and that is precisely when a developer reaches for File ▸ Save Snapshot. The bug that proves the stakes is on record at `emulator.cpp:9144` (Task 40, `beast.nex`): stepping a machine past a mid-frame point cleared the per-scanline change logs and the Copper's palette gradient vanished, rendering a flat sky. | **High** — it is the *debugging* save that is most likely to hit it | **One rule — always advance to the next frame boundary; never refuse** (owner decision, 2026-09-23). A **running** machine's save is queued to the next `begin_new_frame()`, which is required anyway because the GUI cannot serialise from inside `run_frame()`. A machine **paused mid-frame is advanced** to the next `begin_new_frame()` and saved there. There is no refusal path, no unavailable menu item and no failure mode. **The consequence, stated plainly: the restored machine is up to one frame past the moment the user paused at.** That is the accepted trade — a save that always works beats one that is sometimes unavailable, and a developer who needs the exact mid-frame instant has the rewind buffer, which exists for precisely that. §15.2 carries the implementation note that makes the advance safe. |
| **P13** | **`Multiface::mf_type_` is knowingly lossy.** `multiface.cpp:383-400` rebuilds it from three mode booleans and its own comment states a session running `mf_type=10` "will lose the bit". | Medium — a **G1 violation**, silent | Serialise the 2-bit value directly. A rewind can absorb a lost bit; a save the user expects to resume cannot. Cheap, and it makes §9.5(6) a fix rather than an exception. |
| **P8** | **Host input dispatchers.** Platform-owned, hold their own shadow of the connector/wheel/button vector that would stomp a restore. | — | Keep the `on_input_state_restored` callback. |
| **P9** | **Debug state**: breakpoints, watches, trace log, call stack, rewind ring. | Low | **Out of scope.** Not machine state. Worth an explicit sentence in the user guide, because "my breakpoints vanished" is a predictable support question. |
| **P10** | **RZX / video recorder.** | — | Out of scope; a recording in progress is a host activity. A `.jns` written during one records nothing about it. |
| **P11** | **`contention_`, `port_`.** Rebuilt from `model.machine` and by `init()`. | — | Not written. |
| **P12** | **ESP-01 / network.** `esp_frames_` travels; the socket does not. | Low | Document: a restored snapshot has a fresh ESP association. |

P1 and P7 are the two that must not be deferred. P1 is the difference between
"resume my program" working and working *except* for programs that stream from
the card, which is most of them on a Next. P7 decides whether the menu item a
developer reaches for is safe or silently destroys a frame's raster history.

### 10.3 The per-scanline histories — a correction

An earlier draft of §10.1 stated that "the per-scanline change logs are **not**
written — `load_state` rebuilds them from the restored registers (GH #261)", and
built the JNS rule on it. **That is false for three of them**, and the developer
guide already said so: the render history is out of the stream *"bar the NR 0x4A
fallback, the border and the port 0xFF log"*
(`src/doc/developer-guide/02-architecture/05-save-state-and-rewind.md`).

Measured, all three are present in every snapshot:

| History | Owner | Bytes | Why it is in the stream |
|---|---|---|---|
| Port-0xFF change log (count + padded) | `Ula::port_ff_log_` | 3 072 | S5-PSL.05 / issue #42 require a round-trip that preserves the **in-flight** log |
| Border colour per line | `Ula::border_per_line_` | 256 | replayed by the compositor |
| NR 0x4A fallback per line | `Renderer::fallback_per_line_` | 320 | replayed by the compositor |

A JNS built on the blanket rule would **silently lose mid-frame port-0xFF raster
splits on restore** — the exact defect class GH #261 and Task 40 exist to
prevent, reintroduced by a format that believed its own simplification. The
three histories are ordinary declared state in JNS, through the `history`
primitive of §9.5(1), which is also what keeps the JSON from carrying 1 024
entries to express three.

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
      "partition_lba": 2048
    },
    "informational": {
      "fat32_bs_vollab": "NEXT       "
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
boot sector's **volume serial** (`BS_VolID`, BPB offset 0x43).

**`BS_VolLab` is deliberately NOT part of the refusal test**, although an
earlier draft put it there. It is a *stale copy*: the authoritative FAT32 volume
label is the root-directory entry carrying `ATTR_VOLUME_ID`, which jnext's own
FAT walk explicitly skips (`sd_rom_extractor.cpp:292`). The two disagree
routinely — a label set through the directory entry leaves `BS_VolLab`
untouched (harmless: same card, no false refusal), but a tool that rewrites
`BS_VolLab` (several do, writing both) would produce a **false refusal on the
same physical card**. That is the cries-wolf failure §11.1 exists to avoid, so
the label is carried as `informational` and never compared. `BS_VolID` is the
genuinely stable identifier and is what the refusal rests on.

**Cost, stated accurately.** jnext already parses the MBR and BPB host-side in
`src/core/sd_rom_extractor.cpp` (`find_fat32_partition_lba:75`, `parse_bpb:103`)
— but both live inside an **anonymous namespace** (`:17`-`:328`), so this is
*two fields plus a new exported entry point*, not a pure two-field addition. A
small, honest difference worth writing down, because "already parses it" and
"already exposes it" are not the same claim.

**Tier 2 — `content_stamp`: "has it changed since?"** The whole-image SHA-256
and mtime, the warm-start cache's existing mechanism reused verbatim.

**Restore behaviour:**

| Condition | Behaviour |
|---|---|
| No card mounted now, snapshot had one | **Refuse.** Naming the path and the volume label. |
| `identity` differs (size, MBR or `BS_VolID`) | **Refuse.** This is the silently-wrong case settled point 4 demands be caught. `--snapshot-force-sdcard` overrides, with a warning that names both identities. |
| Only `informational.fat32_bs_vollab` differs | **Nothing.** Not compared, not warned on. See above. |
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
member names express the same rule with more room, and it needs to be stated at
three levels, not one.

### 12.1 Members

A reader **ignores any member whose path it does not recognise**, including
whole unknown directory prefixes, and logs one `debug` line listing them, so a
newer file read by an older jnext says what it dropped. `manifest.json` is the
only member whose absence is a refusal.

### 12.2 Keys — and where the default comes from

Within a `state/*.json` object a reader **ignores unknown keys** and **supplies
a declared default for missing ones**, logging one line per subsystem that had
either. Required keys — those the schema marks `required` — are a refusal when
missing, and a key is marked required only when no honest default exists.

**The default is a per-field constant in the descriptor, NOT the result of
calling `reset()`.** An earlier draft said "default to the value `reset()`
establishes", which reads well and is unimplementable:

- **`Emulator::load_state` does not reset anything.** It reads into live
  objects. Introducing a reset pass would be a behaviour change to the rewind
  path, which S2's byte-identity gate (§17) exists to forbid.
- **Some resets are destructive.** `Multiface::reset(bool hard)` wipes its RAM;
  a hard reset of the machine is not what "this key was absent" should mean.
- **Reset values are not always power-on values.** `NextReg::reset()` faithfully
  *preserves* `nr_03_config_mode` (no reset clause in `zxnext.vhd:1102`), so
  "the value reset() establishes" is not even a well-defined constant for it.

So `d.u8("bank", bank_, /*default=*/0x00)` carries the default in the
declaration, the schema generator writes it as `"default"`, and a field with no
honest default is declared required instead. The reader never calls `reset()`.

**That creates a second copy of every power-on value, and it must be gated.**
The value now exists twice — in `reset()`, where it carries its VHDL citation,
and in `describe_state` — with nothing comparing them. If a future VHDL audit
corrects a `reset()` value, the declared default silently keeps the old one and
every snapshot missing that key restores the pre-audit machine. That is
`feedback_single_source_means_every_consumer`, and it is the exact shape of the
`--help` defect (GH #246): a source of truth is only one if every consumer reads
it.

The gate is a `JNSX` row asserting, per field, **declared default == the value
`reset()` leaves**, with the exemptions declared **in the table itself** and
never as a checker exclusion:

| Exempt field | Why |
|---|---|
| `NextReg::nr_03_config_mode` | no reset clause in the VHDL (`zxnext.vhd:1102`); `reset()` faithfully **preserves** it, so there is no "value reset establishes" to compare against |
| `NextReg` machine type / machine timing | same class — preserved across reset, not established by it |
| `Multiface` RAM | `reset(bool hard)` **wipes** it; running the comparison would destroy state, and the buffer has no scalar default anyway |

The row runs only where `reset()` is non-destructive and value-establishing,
which is the large majority; anything else is listed above, in the table a
reviewer reads.

### 12.3 Retired names

Checked **before** the ignore-unknown rule, against the committed
`doc/formats/jns-retired-names.json` (§7.1): a key listed there is **migrated**
to its successor, or ignored *deliberately* if tombstoned. Only a key in neither
the current schema nor the retired table is "unknown". Without this, §7.1's
never-re-mean-a-name rule and §12.2's ignore-unknown rule compose into silent
total data loss on every rename.

### 12.4 The full reader-rule table

Every condition an implementer will actually meet, with its verdict. Anything
not listed here is a gap in this document, not a licence to improvise.

| Condition | Verdict |
|---|---|
| Duplicate member names in the archive | **Refuse.** ZIP permits them, readers disagree which wins (§6). |
| Manifest `members[p].crc32` disagrees with the ZIP's own CRC for `p` | **Refuse**, naming both. The ZIP CRC is authoritative for *integrity*; a disagreement means the manifest and the archive were not written together, which is a torn file however it happened. |
| Blob declared in `members` but the member is absent | **Refuse**, naming the path. |
| Blob member present but absent from `members` | **Refuse.** An undeclared blob has no length or CRC to check, so it is not covered by §5.3's chain. This is stricter than the ignore-unknown rule on purpose: `mem/` is a closed namespace. |
| A subsystem listed in `manifest.subsystems` whose member is absent | **Refuse.** The list exists exactly to separate "deliberately not saved" from "missing or corrupt". |
| A `state/` member present but absent from `subsystems` | Ignore (§12.1), and log it. |
| Inflated length ≠ declared `bytes` | **Refuse**, naming the member — the warm-start cache's exact-length rule (§3.2). |
| Snapshot had a card, none mounted now | **Refuse** (§11.3). |
| Snapshot had **no** card, one is mounted now | **Restore, with a warning.** The machine did not depend on it; a card that appeared is not a reason to refuse, but it is a reason the run may diverge. |
| `media.sdcard.read_only` differs from the current mount | **Warn.** A snapshot taken read-only restoring onto a writable mount is legal and common; the reverse means writes the snapshot's program made were never persisted. Named in the warning, not refused. |
| `model.ram_kb` is a size this build cannot construct | **Refuse** (§8 — never assume 2048). |
| *(no row)* A save attempted while paused mid-frame | **Not a refusal condition.** The writer advances to the next frame boundary (§10.2 P7), so no file and no reader ever sees this state. Listed as absent so the omission is deliberate rather than overlooked. |
| `format_version` unknown / `state_model_revision` mismatch / machine mismatch | §7.3. |

### 12.5 What is and is not promised

**Forward compatibility** (old jnext reads new file): works for additive
changes, which §7.1 makes the common case; refuses cleanly on a
`format_version` bump. **Backward compatibility** (new jnext reads old file):
works for additive changes and for renames covered by the retired-name table; a
`state_model_revision` mismatch restores with a warning, or refuses under
`--snapshot-strict`.

**Not promised:** that a snapshot taken by jnext 1.1 restores correctly in jnext
2.0. The stamp exists so that when it does not, the failure is legible.

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

For a private format there is no foreign reader for the *whole* machine. But
"private format" is not the same as "no foreign reader at all" — an earlier
draft of this document conceded too much there, and §13.2(1) recovers most of
what it gave away. Seven things stand in, ordered by how much of the blind spot
each actually removes.

### 13.2 The substitutes

For a private format there is no foreign reader by definition — but that is not
a licence to call a process control a technical one. Ordered by how much of the
blind spot each actually removes.

**(1) A REAL foreign reader, for the core of the machine — via FUSE.** This is
the strongest one available and an earlier draft of this document missed it.
**A `.jns` of a 48K/128K machine expresses a strict superset of what `.szx`
expresses**, and this project already uses real FUSE headless as a proven
oracle: `/usr/bin/fuse` with `--debugger-command`, driven under Xvfb, located
Tasks 50 and 54 (`technique_fuse_headless_oracle`). So the row is:

> Write a `.jns` **and** a `.szx` at the same instant on a 48K or 128K machine.
> Load the `.szx` in **real FUSE**, extract registers, RAM and paging through its
> debugger. Assert the independent reader's extraction from the `.jns` agrees.

Three mechanics an implementer needs, all verified on this box (FUSE 1.6.0):

- **`--debugger-command` is real but is NOT in `--help`** — it is in
  `man fuse` (checked: 0 hits in `--help`, 2 in the man page). Cite the man page,
  or the next reader concludes the option was removed. It is also the only way
  to pass multi-line debugger input.
- **It runs BEFORE emulator startup**, so a straight "dump state" does nothing
  useful. The idiom is a breakpoint plus an explicit output channel:
  `break`, then `commands N` / `print` / `continue` / `end`, with the output
  captured from the process. `fuse` on this box is a shell function wrapping
  `/usr/bin/fuse`; call the binary and set `GDK_BACKEND=x11` under `Xvfb`.
- **The row MUST assert it received non-empty output from FUSE before
  comparing.** A FUSE invocation that produces nothing — wrong option, X not up,
  a breakpoint never hit — would otherwise compare an empty extraction against an
  empty expectation and **pass vacuously**, in the strongest substitute this
  design has. That is the failure mode most worth pinning, because it converts
  the best evidence in §13.2 into the most confident lie.

That is a foreign implementation adjudicating jnext's state for the CPU, the
64 KB address space and the 128K paging — the part of a Next snapshot most
software actually depends on. It does not reach Layer 2, sprites, the Copper or
the NextREG file, because `.szx` cannot express them; nothing can, and §13.3
says so plainly instead of implying otherwise.

**(2) An independent ZIP reader, on every written file.** `unzip -t` and
Python's `zipfile.testzip()` verify the central directory, the local headers,
the sizes and every per-member CRC-32 — none of it our code. This removes the
whole *container framing* class, which is where the `.szx` bug lived: a
structural field our own reader was too permissive about.

**(3) An independent JSON Schema validator, on every written file.**
`check-jsonschema` / Python `jsonschema` against the committed
`jns-snapshot.schema.json`. Removes the *encoding* class: wrong type, missing
required key, hex string of the wrong length, register out of range, malformed
UTF-8. **Not a duplicate key** — see §5.3's correction: the duplicate is gone
before any validator sees the document, so the READER refuses it and nothing
else can.

**(4) A hand-written constraint overlay, merged into the generated schema.**
This is what stops (3) being circular. The generated half cannot express an
invariant the generator does not know; the overlay is **external knowledge**,
written by a human from this document, and merged in at generation time:

- `ram_kb * 1024 == members["mem/ram.bin"].bytes`
- `members["mem/bank5-vram.bin"].bytes == 16384` and
  `members["mem/bank7-bram.bin"].bytes == 8192` — literals, so a descriptor that
  silently resized a buffer fails validation rather than re-describing itself
- `mem/multiface-ram.bin` present **iff** `model.machine != "next"` (§4.3(2))
- cross-field register invariants (e.g. a sprite's `pattern` index against the
  4-bit/8-bit mode bit; the MMU slot map against `model.ram_kb`)
- `capture.frame_boundary == true`, always (§8)

External knowledge is the only kind that catches a blind spot the writer and the
generated schema share. The overlay is small, committed, and reviewed; it is the
technical answer where (5) is only a procedural one.

**(5) The schema is committed and staleness-gated.** `make schema-check`
regenerates and byte-diffs, exactly as `docs-check` does for the man page, so
every field-declaration change surfaces as a schema diff a human reviews. **This
is a process control, not a technical one**, and it is listed after the overlay
for that reason. It makes a semantic error *visible*; it does not make one
*detectable*.

**(6) A second reader, written from the specification.** A Python script in
`test/` that opens a `.jns` and reconstructs named values — PC, SP, NR 0x15, the
MMU slot map, known RAM bytes — **written from this document, not from the C++
writer**. It is the reader (1) uses to compare against FUSE, and on a Next
machine it is the only independent extraction there is. Its honest limit: it is
one reader over a few dozen of ~2 000 fields, so it is a spot check, not
coverage.

**(7) Byte-level assertions against this document, not against our loader.** The
memo's rule, applied: member 0 is named `manifest.json`; the archive comment is
exactly `jnext-snapshot format=1`; `mem/ram.bin` is exactly `ram_kb * 1024`
bytes; no member path escapes the archive; no duplicate member names. These are
assertions about the *spec*, and they survive a misreading shared by writer and
reader.

### 13.3 What none of this proves — the hole is NARROWED, not closed

Two limits, stated rather than implied.

**The `.szx` hole is not closed for Next state.** Substitute (1) is a genuine
foreign reader for the CPU, the 64 KB map and 128K paging. Nothing external can
adjudicate Layer 2, the sprite engine, the Copper, the tilemap, the NextREG file
or the SD FSM, because no foreign implementation reads a Next snapshot — that is
settled point 1, and it is the price of the format being emulator-only. For that
majority of the machine, (2), (3) and (4) reduce the risk to *semantic* errors
that survive an external structural check, and (5) puts a human in front of
every one of them. **That is narrower than "we have a foreign reader", and this
document should not be read as claiming otherwise.**

**And the `.szx` in that comparison is written by jnext.** `SzxSaver` and the
JNS writer read the same `Emulator` accessors, so an error *shared* between them
— an accessor that returns the wrong thing — produces a `.szx` and a `.jns` that
agree with each other, and FUSE confirms the agreement. Substitute (1) is a
foreign reader for the *format*, not for the *state*. What it does catch, and
what nothing else catches, is JNS mis-encoding a value `SzxSaver` gets right —
which is the whole class of writer bug this format could introduce.

**Validation is not restoration.** "The file validates" and "the snapshot
works" are different claims. Whether the restored machine *is* the machine that
was saved is a semantic round-trip question, answered by §16's tests — save,
restore, run N frames, compare the framebuffer against a run that never saved —
and by nothing in the format itself.

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

**Justifying #1 — accepted by the owner, 2026-09-23.** Recorded anyway, because
the reasoning outlives the decision. In favour: the repo already vendors
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
| 9 | **Foreign snapshot reader** (§13.2(1)) | **real FUSE** — `/usr/bin/fuse` + `Xvfb`, the `technique_fuse_headless_oracle` recipe | Same. Already the project's oracle for timing and rendering disputes; `fuse` and `xorg-x11-server-Xvfb` join the CI container's install list. |

Neither adds a runtime dependency, a `Requires:` line or a package. CI already
runs in `fedora:44` and already installs pandoc, mkdocs-material and graphviz for
exactly this class of check; `python3-jsonschema` and `unzip` join that list.

**Net: one new vendored header at runtime, three packaged tools at test time,
zero new package dependencies.**

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
| **File ▸ Save Snapshot…, while the machine is RUNNING** | The write is **queued to the next `begin_new_frame()`** — under one frame (20 ms at 50 Hz), invisible to the user, and required regardless: the GUI cannot serialise from inside `run_frame()`, and §10.2 P7 rules out a mid-frame capture. |
| **File ▸ Save Snapshot…, while PAUSED mid-frame** | **Advance to the next `begin_new_frame()` and save there.** Always available, never refused (owner decision). The machine is left at that boundary, i.e. up to one frame past where the user paused — a status-bar line says so once, rather than the move being silent. |
| **What makes the advance safe** | It must complete the in-flight frame through the ordinary path, **not** re-run `begin_new_frame()` on a frame already in progress. That is exactly the Task 40 defect (`emulator.cpp:9144`, `beast.nex`): re-entering frame start mid-frame cleared the per-scanline change logs and the Copper's palette gradient vanished, rendering a flat sky. The `if (!frame_in_progress_)` guard there is the fix, and the snapshot advance rides on it rather than reimplementing it. **A row must pin this**, because a save that quietly wipes a frame's raster history is worse than one that refuses. |
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
> and no per-row VHDL citation. `make traceability-check` refuses unless every
> declared suite is accounted for, so `snapshot_test` must be placed in the
> generator in the same change that adds it.
>
> **It goes in `%NO_MATRIX_SECTION`, NOT in `%TOMBSTONE`.** An earlier draft of
> this section prescribed the `rewind_test` / `sdcard_test` treatment — a
> per-suite citation tombstone plus planned rows in
> `test/traceability-exceptions.conf` — and that was wrong. The two mechanisms
> are not interchangeable, and the generator's own header comments
> (`test/refresh-traceability-matrix.pl:395-402`, `:933-947`) say which is
> which:
>
> - **`%TOMBSTONE`** applies to a suite that *has* a per-row matrix section —
>   one traced against a plan — and gives its uncited rows a standing
>   `(jnext-internal)` citation instead of a bare `—`. `rewind_test` and
>   `sdcard_test` are in that position because they carry historical
>   planned-but-unimplemented rows, which is what `traceability-exceptions.conf`
>   records.
> - **`%NO_MATRIX_SECTION`** applies to a suite whose oracle is a file format,
>   a host API or jnext-internal policy, with no VHDL-derived plan row to map
>   and nothing planned. It is still fully declared, counted and run.
>
> `snapshot_test` is the second: its authority is this document, every row is
> implemented, and nothing is planned-but-missing. The precedent is exact —
> `warm_start_test` (jnext's other own on-disk format), `nex_loader_test`,
> `fat32_image_test`. Putting it in `%TOMBSTONE` would have claimed a matrix
> section it does not have, and adding rows to `traceability-exceptions.conf`
> would have manufactured a backlog that does not exist. Corrected here after
> S1 implemented it and an independent reviewer adjudicated the deviation.

Manifests to update in the same change (a missing test is a loud failure, never
a silent skip): `test/unit-tests.conf` (with the **exact** pinned row count
**and** the `# expect:` suite count), `test/00regression/functional_tests.conf`
once functional rows exist, and the generator's `%NO_MATRIX_SECTION` entry
above. `test/refresh-subsystem-status.sh` needs the suite's friendly name too,
or the dashboard emits a TODO.

### 16.1 Unit rows — `test/snapshot/snapshot_test.cpp`

| Group | IDs | What |
|---|---|---|
| **Container** | `JNSC-01…` | ZIP round-trip; `manifest.json` is member 0; archive comment exact; `STORED` and `DEFLATE` both read; **duplicate member names refused**; member-path grammar refusals (absolute, `..`, upper case, empty); ZIP64 refused; truncated archive refused; a member whose CRC-32 disagrees refused. |
| **Version** | `JNSV-01…` | Every row of §7.3, both directions: `format_version` too new → refuse naming both numbers; too old with reader present → reads; too old with reader absent → refuse; missing → refuse; non-integer → refuse. |
| **Reader rules** | `JNSR-01…` | **One row per line of §12.4** — that table is the row list. Blob declared but absent; blob present but undeclared; manifest CRC vs ZIP CRC disagreement; subsystem listed but member absent; member present but unlisted; inflated length ≠ declared; card-present/absent both directions; `read_only` mismatch; unconstructible `ram_kb`. |
| **Unknown / retired names** | `JNSU-01…` | Unknown member ignored + logged; unknown key ignored; missing optional key takes its **declared default** (asserted against the declaration, never against a literal, and never against `reset()` — §12.2); missing required key refuses; a **retired** key is migrated to its successor, not ignored; a tombstoned key is ignored deliberately; a key in neither table is "unknown". |
| **Identity** | `JNSI-01…` | SD Tier-1 mismatch refuses; Tier-2 mismatch warns and restores; Tier-2 mismatch **with the SD FSM mid-transfer** refuses; `BS_VolLab` differing alone changes **nothing** (§11.3); no card mounted refuses; ROM digest mismatch warns / refuses under strict; tape file absent warns. |
| **Encoding** | `JNSE-01…` | Hex strings exactly the declared length; every `u64`/`i64` emits a **string**; a negative `i64` round-trips (**the `/INT` window with its real measured value, −564 933**, §7.4); `INT64_MAX` emits `"open"` and round-trips; `i32` round-trips negative; enums emit names, and an unknown name on read **refuses** rather than defaulting — a wrong FSM state is not a safe default. |
| **History primitive** | `JNSH-01…` | The binary encoding of `port_ff_log_` and the UART's four FIFOs is **padded to capacity** (constant width, per `RewindBuffer`); the JSON encoding carries exactly `count` items; a round-trip through JSON with 3 in-flight entries restores 3, not 1 024; **an in-flight port-0xFF log survives save→restore and the replayed frame is pixel-identical** (the §10.3 defect, pinned). |
| **Descriptor** | `JNSD-01…` | For every subsystem: the JSON and binary encodings, fed the same machine, restore to identical machines. |
| **Completeness** | `JNSX-01…` | **See below — this is the row group that JNSD cannot be.** Plus the §12.2 gate: **declared default == post-`reset()` value**, per field, with the three exemptions read from the declared table (never from a checker exclusion). |
| **Blob framing** | `JNSB-01…` | `mem/ram.bin` is exactly `ram_kb * 1024`; `bank5-vram` 16 384; `bank7-bram` 8 192; a short blob refuses; a long blob refuses; declared CRC == actual CRC; `mem/multiface-ram.bin` present **iff** the machine is not the Next (§4.3(2)). |
| **Refusal messages** | `JNSM-01…` | Every refusal names the offending thing. A refusal that says only "invalid snapshot" is a **failing** row: G9 is a testable property, not a slogan. |

**Why `JNSD` is not a completeness oracle.** "Each encoding is the other's
oracle" is a real cross-check for *content*, and **circular for omissions**: a
field left out of `describe_state` is absent from the JSON *and* from the
binary, and every `JNSD` row passes. The completeness oracle has to come from
outside the descriptor, and it is nearly free:

- **Stream length.** The migrated `BinWriteDesc` must still measure exactly
  **2 292 965** bytes. Any dropped field changes it.
- **A golden byte image.** Capture the *pre-migration* stream once and `cmp`
  against it (§17's S2-S5 gate). A field silently dropped, reordered or
  re-typed fails immediately.
- **After S5b, the re-baselined lengths are pinned**: `JNSX-S5B-LENGTHS`
  asserts **2 153 701** on the Next and **2 161 893** on 48K/128K/+3, so the
  one deliberate change to the stream is a number in a test rather than a fact
  in a commit message. It lives in **`rewind_test`**, not here: the row builds
  four real `Emulator`s, and `snapshot_test` links `jnext_save` alone —
  deliberately, so the descriptor layer's own rows cannot come to depend on the
  emulator core.

Every row is mutation-tested by its author before review: revert the
behavioural branch the row exists for and confirm the row fails.

**Two groups S2 added, recorded here after the fact** — the table above was
written before the descriptor layer existed and named neither:

| Group | IDs | What |
|---|---|---|
| **Schema** | `JNSS-01…` | The GENERATED SHAPE, per §6.2's table: a field without a declared default is `required` and one with it carries `default`; unsigned widths become `minimum`/`maximum`; a `u64` becomes a string with a canonical pattern; `i64_open` carries the `"open"` alternative; a fixed array's length is a LITERAL in the pattern; an enum is a closed name set; a `log`/`fifo` carries the BINARY capacity as `maxItems`; a `blob` contributes no property and a `ram_window` a `const` reference; `additionalProperties: false`; and the output is deterministic and ORDER-INDEPENDENT. Plus: a declared default outside its own enum's name set fails generation. |
| **Golden / byte identity** | `JNSG-01…` | §17.1's extractor as tested code rather than a shell pipeline (`snapshot_test --extract-golden IN OUT`): `JNEXTWS2` deflated and `JNEXTWS1` plain, a header whose `plain_bytes` lies, an unknown magic, a file shorter than the 96-byte header. Plus the sentinel encoding the golden's 33-block framing rests on. |

The **adversarial** rows are `JNSA-*`, a group the table above also did not
name. They exist because reviewing a spec is not reviewing a parser: S1's
design passed two review rounds and S1's *implementation* review still found a
zip-slip and a 167-byte archive that forced a 4.29 GB allocation. Every length,
count and index a document supplies is fed back hostile — truncated, oversized,
wrong-typed, non-canonical, duplicated, over-deep, and self-referential.

**What S2's rows do NOT prove, stated because the gate above invites the
assumption.** S2 migrates no subsystem, so no row can claim the real
2 292 965-byte stream is unchanged — there is nothing yet to compare. What
`JNSD-B01` proves is narrower and is the precondition S3-S5 rest on: for every
primitive, `BinWriteDesc` emits the same bytes a hand-written `save_state` in
the tree's idiom emits. The golden itself was verified by hand at S2 time
(2026-09-24): the recipe above yields exactly **2 292 965** bytes, in which all
**33** of `Emulator::save_state`'s sentinels appear **exactly once**, in ordinal
order, the last ending at byte 2 292 965 with **zero** bytes left over. That
confirms §17.1's stated header facts and §4's block measurements — the DivMMC
block is 131 089 bytes and the Multiface block 8 201, so §17.0's post-S5b Next
length of 2 153 701 follows arithmetically.

### 16.2 Functional / regression rows — `test/00regression/`

| Row | What |
|---|---|
| `snapshot-roundtrip-func` | Save at frame N, restart with `--load out.jns`, run M more frames, screenshot, compare **pixel-exact** against one uninterrupted run of N+M frames. **Named workloads, because a quiescent 48K boot passes for a neighbouring reason**: `beast.nex` (per-scanline change logs + Copper gradient — the §10.3 class), `copper-demo` (Copper PC mid-list), and a run captured **mid-CMD18 SD stream** (P1). A pass on any one of those means something; a pass on a BASIC prompt does not. |
| `snapshot-foreign-fuse-func` | §13.2(1): on a **128K** machine, write `.jns` and `.szx` at the same instant; load the `.szx` in **real FUSE** headless (`/usr/bin/fuse` + Xvfb + `--debugger-command`, which is documented in `man fuse`, not `--help`); assert the spec-written Python reader's extraction from the `.jns` agrees with FUSE on registers, paging and sampled RAM. **The row FAILS if FUSE produced no output** — asserted before any comparison, because an empty-vs-empty comparison would pass vacuously. Skips without FUSE/Xvfb locally; hard-fails in CI. |
| `snapshot-schema-func` | Validate the written file with Python `jsonschema` against the committed schema **plus the constraint overlay**, and `unzip -t` it. Skips if the tools are absent; hard-fails in CI. |
| `snapshot-uncompressed-func` | The same round-trip with `--snapshot-uncompressed`; assert every member is `STORED` (read by `zipfile`, not by us) and the restore is pixel-identical to the compressed one. |
| `snapshot-sdcard-mismatch-func` | Save; mutate a sector of a **copy** of the card; restore → assert the Tier-2 warning and that the run proceeds. Then mutate `BS_VolID` → assert the Tier-1 refusal and a non-zero exit. Then mutate **only** `BS_VolLab` → assert **no** warning and **no** refusal (§11.3). |
| `snapshot-paused-advance-func` | Pause mid-frame in the debugger (on `beast.nex`, which has a live per-scanline Copper gradient), save, and assert three things: the save **succeeds**; the restored machine replays the frame **pixel-identically** — i.e. the advance did not wipe the change logs, the Task 40 defect (§15.2); and the live machine is left at the following frame boundary. The workload is `beast.nex` specifically because a quiescent screen cannot distinguish a preserved raster history from a destroyed one. |

`JNEXT_TEST_JOBS=4` on every regression invocation, as always.

### 16.3 The staleness gate, specified

`make schema-check` regenerates `doc/formats/jns-snapshot.schema.json` and
byte-diffs it against the committed copy, as a prerequisite of `make unit-test`
beside `docs-check` and `traceability-check`.

**The generator output must be deterministic, and that is a requirement on the
generator, not a hope.** Stable key order (sorted, not hash order), no
timestamp, no absolute path, no build id, no jnext version, fixed float
formatting, `\n` line endings. `docs-check` learned this the hard way when
mkdocs stamped `sitemap.xml.gz` with the build date and the gate went red on an
unchanged tree. Produced by a dedicated `tools/gen-snapshot-schema` target
running `SchemaDesc` over every subsystem and merging the hand-written overlay
(§13.2(4)), so exactly one command produces the committed artefact.

**The gate has a second half, and it is the half that is not bookkeeping**
(added S2). Regenerate-and-diff proves the committed file is CURRENT; it proves
nothing about whether it is a valid schema, or whether it accepts and rejects
the right documents — a schema with no constraints at all would pass the diff
forever. So `schema-check` then hands the committed file to Python
`jsonschema`, an implementation that is not ours
(`test/snapshot/verify_schema.py`), which asserts three things: it IS a valid
draft 2020-12 schema; a manifest transcribed **from this document** rather than
from `manifest_to_json` validates (§13.2(6), scoped to the manifest); and a
matrix of single-mutation faults is REJECTED, each naming the constraint that
caught it. Skip/fail posture is `docs-check`'s: skip when the validator is
absent locally, hard-fail in CI.

**In S2 the subsystem registry is EMPTY**, so the committed schema covers the
manifest and declares no `state/*.json` yet. That is the correct state, not an
omission: the gate exists before the first migration so each of S3-S5's 34
subsystems arrives as a schema diff, where §13.2(5) wants a human to see it. A
gate added after the thirty-fourth would have missed every diff it exists for.

## 17. Staged implementation plan and effort

Effort is in *agent-sessions of focused work*, the unit this project has
historical calibration for. Every stage ends with an independent review by an
agent that did not write it, on its own branch and worktree.

| Stage | Content | Effort | Gate to the next |
|---|---|---|---|
| **S0 — Owner review** | §18's questions answered | — | Written answers (container, JSON dependency, decoupling scope and the SD scheme are **answered**, 2026-09-23) |
| **S1 — Container** | First-party ZIP reader/writer over zlib; `manifest.json` grammar; `format_version` rules; the §12.4 reader rules; `JNSC`/`JNSV`/`JNSR` rows | **M** (2–3) | All container rows green **and** an independent ZIP reader accepting every file we write |
| **S2 — Descriptor layer** | `StateDesc` + the binary/JSON/schema realisations; the `history` and `enum` primitives; two describe methods (§9.5(2)); schema generator + `schema-check` | **M** (2–3) | **The byte-identity gate, below** |
| **S3 — Migration, group 1** | Core: clock, RAM, MMU (incl. both blobs), NextREG, CPU, IM2 (+ timing) | **S–M** (1.5–2.5) | byte-identity holds after each subsystem |
| **S4 — Migration, group 2** | Video: palette, layer2, sprites, tilemap, lores, ULA (incl. the three histories), renderer, copper | **S–M** (1.5–2.5) | as above |
| **S5 — Migration, group 3** | Peripherals + audio + input: ctc, dma, spi, i2c, rtc, uart (FIFOs), divmmc, multiface, nmi, beeper, turbosound, dac, i2s, and the six input classes | **S–M** (1.5–2.5) | as above |
| **S5b — Remove the duplicated RAM** ✓ **DONE** | D3 + D4: the DivMMC window becomes a *reference* and the Multiface private array is dropped on the Next. Golden re-baselined **once**, with the diff explained field by field. | **XS** (~0.5) | `JNSX-S5B-LENGTHS` pins 2 153 701 / 2 161 893; the diff is §17.0's table |
| **S6 — The gaps** | P1 `SdCardDevice`; P13 `mf_type_`; P3 ROM digests; P4 tape identity; P5 preview; P7's two save rules; `Emulator`'s own scalars | **M** (2–3) | P1 proven by a mid-CMD18 save/restore row; P7 by `snapshot-paused-refusal-func` |
| **S7 — SD identity** | Tier 1 from MBR + BPB `BS_VolID` (a new exported entry point in `sd_rom_extractor`), Tier 2 reuse, the refusal/warning matrix, `JNSI` rows | **S** (1) | `snapshot-sdcard-mismatch-func`, all three legs |
| **S8 — Integration** | CLI table + man page + `cli-check`; the three load-dispatch sites; GUI save/load/filter/status bar/grey-out; user guide; developer guide chapter; FEATURES; ChangeLog | **S** (1–2) | `make cli-check`, `docs-check`, full triplet |
| **S9 — Validation** | The spec-written Python reader; **the FUSE foreign-reader row**; the constraint overlay; the full functional set; CI tool install | **S** (1–2) | All §16.2 rows green in CI |

**Total: 14–23 focused sessions; S2–S5 is 7–10 of them, S5b about half of one.**

### 17.0 Why S5b exists, and why it is not deferred

D3 and D4 (§18.3) put **131 072 + 8 192 = 139 264 bytes** into every snapshot
that do not need to be there — **6.1 % of every rewind slot**, so a 300-frame
ring carries about 41 MB of duplicated DivMMC RAM. Revision 2 of this document
said they were "not worth a separate change to the binary stream, because S2's
byte-identity gate requires that stream to stay exactly as it is."

**That was wrong, and in an instructive way.** The gate is a *migration
scaffold*. It is true and load-bearing while it is doing its job — proving a
transcription changed nothing — and treating it as a permanent contract is the
same error §9.1 rejects when it refuses `legacy.bin`: a temporary mechanism
quietly becoming the reason a defect cannot be fixed.

The moment immediately after S5 is the **safest this will ever be**: every field
is named, so a golden diff is explainable field by field rather than as an opaque
byte shift. Re-baselining once, there, costs about half a session.

Post-S5b stream lengths, derived from §4's measurement:

| Machine | Length | Why |
|---|---|---|
| Next | **2 153 701** | −131 072 (DivMMC window) −8 192 (Multiface dead zeros) |
| 48K / 128K / +3 | **2 161 893** | −131 072 only; the Multiface array is genuine private state there (§4.3(2)) |

**The length becomes machine-dependent for the first time, and that is fine.**
`RewindBuffer` measures its slot width once at construction and needs it constant
only *within* a run — and the width already varies between runs today: the
`joy_uart` block is one byte with no cable attached (measured, §4) and larger
with one. A `JNSX` row pins both numbers so the re-baselined golden is itself
gated rather than merely recorded.

**If the owner prefers not to touch the stream**, the allowed alternative is to
commit the pre-S5b golden as a permanent fixture with a `JNSX` row pinning it.
**Doing neither must not ship**: without one or the other, `ram_window`'s binary
realisation exists solely to reproduce a known defect, with nothing pinning the
bytes it is reproducing.

#### What S5b did — measured, 2026-09-24

**The predicted 2 153 701 was measured exactly**, on a Next warm-start
recording of a booted NextZXOS machine, extracted the §17.1 way. `JNSX-S5B-LENGTHS`
(`rewind_test`) confirms it by an independent path — four `Emulator`s it builds
itself — and pins **2 161 893** on 48K/128K/+3 in the same row.

**The mechanism is one rule, applied to two subsystems.** `machine_level` now
means "this walk is Emulator-driven, so this stream already carries the `ram`
block", and `BinWriteDesc::ram_window` emits nothing when it is set. `DivMmc`
derives the flag from `ram_ext_ != nullptr`, which is exactly "an `Emulator`
built me" — emulator.cpp:279 is the one line in the tree that sets it, and it
is unconditional. The `Multiface` array is a `blob` and not a window (a
`ram_window` would make the JSON encoding emit a reference to page 0x0B on a
48K machine, where that page holds nothing of the sort), so §9.2's *presence*
branch carries it: declared only when `ram_ext_ == nullptr`.

**The diff, field by field.** The new stream is the old one with exactly two
contiguous ranges excised and **every other byte identical in place** —
verified as a splice, not as a hash:

| Range in the old stream | Bytes | What | Verdict |
|---|---|---|---|
| `[0, 2 152 291)` | 2 152 291 | blocks 0-17 + the DivMMC block's 15 leading scalars | unchanged, in place |
| `[2 152 291, 2 283 363)` | **131 072** | `divmmc.ram` — the `ram_window` | **removed** |
| `[2 283 363, 2 283 678)` | 315 | the two DivMMC levers … the Multiface block's 8 booleans | unchanged, shifted −131 072 |
| `[2 283 678, 2 291 870)` | **8 192** | `multiface.ram` — the private array | **removed** |
| `[2 291 870, 2 292 965)` | 1 095 | the `multiface` sentinel … end of stream | unchanged, shifted −139 264 |

2 152 291 + 315 + 1 095 = 2 153 701, and every one of the old 2 292 965 bytes
is in exactly one row.

**Both removals were proved redundant on the PRE-S5b golden, before the code
changed** — which is what makes this a removal of duplication rather than a
removal of state:

- the 131 072 bytes at `2 152 291` were **byte-identical** to that same
  stream's own `Ram` page 16 at `131 096`, across all of them;
- the 8 192 bytes at `2 283 678` were **all zero**, confirming §4.3(2)'s "dead
  zeros" by measurement rather than by reading `emulator.cpp:301`.

Neither fact is provable by a row (both need a pre-S5b build to have produced
the image), so what the rows carry instead is that the bytes still **arrive**:
`S5B-DIVMMC-RESTORE` and `S5B-MF-NEXT-RESTORE` write through each device's
window, save the whole machine, destroy the physical page, restore, and read
the value back out of the device. A shorter stream that lost state would pass
a length row and fail those two.

**The old golden is kept**, beside the new one rather than overwritten, so the
diff above stays reproducible: `golden-savestate.bin` (2 292 965) and
`golden-savestate-s5b.bin` (2 153 701).

**`warm_start::kFormatVersion` went 1 → 2.** The recording's identity also
carries the stream length, which moved by 139 264 bytes, so a pre-S5b cache
would have been discarded anyway; the bump is made because that constant's own
rule says to make it whenever `save_state` changes shape, and a version bumped
only when nothing else would catch the change is one nobody can reason about.

**Two things S5b did NOT do**, both deliberate. §9.2's run-time null assertion
on `ram_window` is still unable to fire, and stays S6's: with the flag derived
from the pointer it would guard, an assertion on that pointer is a tautology,
and the branch is fail-safe in the direction that matters — were
emulator.cpp:279 removed, the window would simply travel inline again, a wider
stream with no data loss. It becomes load-bearing for the Emulator-driven JSON
realisation, where an unbacked window would emit a reference to bytes no member
carries — a dangling pointer in a FILE, which no fallback can repair. And
`SdCardDevice`'s missing `save_state` (D1) and `mf_type_` (D2) remain S6's.

> Earlier drafts said 20–30 total and 13–18 for S2–S5. The
> re-estimate follows the call-site classification in §9.4, which did not exist
> when the first number was written: **404 of 532 sites (76 %) are a bare member
> variable** — one mechanical line each — the 29 loop sites *collapse* into 5
> declarations rather than 29, and the 33 sentinel sites are not fields at all.
> The migration is a transcription with a byte-exact oracle, not a redesign.
> Only ~20 sites in 8 places (§9.5) need judgement.

### 17.1 The byte-identity gate — and why `rewind_test` is not it

S2–S5 rest on one property: **the migrated `BinWriteDesc` produces the same
bytes as today's hand-written `save_state`.** Get that, and the rewind ring is
provably untouched and the migration cannot have silently dropped or reordered a
field.

**`rewind_test` does not prove it, and an earlier report of this design said it
did.** `rewind_test.cpp:241-263` measures the size, saves to `buf1`, loads,
saves to `buf2`, and `memcmp`s **`buf1` against `buf2`**. That is
save→load→save **idempotence**, plus a size assertion. A migration that
*consistently* reordered two fields — writing and reading them in the same new
order — passes every row: the size is unchanged, and both passes agree with each
other because they share the new layout. The oracle has to be a byte image
captured **before** the migration.

That image is nearly free, because the machine already writes one:

```bash
# BEFORE the migration, on a build of current main.
# NO --rtc. Not on this run, and not on any run compared against it (below).
jnext --headless --machine next --warm-start-regenerate ... # records the cache
# JNEXTWS2 (current): 96-byte plain header, then a DEFLATE payload.
python3 -c "import zlib,sys; d=open(sys.argv[1],'rb').read(); \
            sys.stdout.buffer.write(zlib.decompress(d[96:]))" \
        ~/.jnext/warm-start/warm-start-m0.jwss > golden-savestate.bin
# AFTER each migrated subsystem:
#   re-record, inflate the same way, and `cmp` against golden-savestate.bin
```

Two notes an implementer needs. The header is **96 bytes, plain**, and its
`plain_bytes` field (offset 16, u64 LE) states the exact expected length — check
it. And a **`JNEXTWS1`** file (pre-compression, as still found on this box) has
the payload *uncompressed*, so `d[96:]` is already the stream; check the magic
rather than assuming. Because the cache is regenerable and machine-keyed, the
golden should be **copied somewhere stable** before the migration starts, not
left in `~/.jnext` where the next run replaces it.

**Omit `--rtc` — on the golden run and on every run compared against it.**
Three separate agents rediscovered this the hard way, each time reading the
resulting diff as a migration fault. `Emulator::record_warm_start_state()`
carries `--rtc` into the recording boot *deliberately* — it copies `config_`
into `boot_cfg` and clears only the per-load and per-host fields, with the
reason stated at `emulator.cpp:7417` ("the SD image, `--rtc`, the machine type
… left alone on purpose") — and `I2cRtc::set_fixed_time()` calls
`snapshot_time()`, which encodes the pinned date into the DS1307 BCD registers —
so they travel in the stream, in block 16.

Measured on the S1–S5 branch (2026-09-24): without `--rtc` those seven registers
are **all zero**. `snapshot_time()` has exactly three callers — the `I2cRtc`
constructor, `set_fixed_time()` and `start()` (an I2C START) — and
`Emulator::init()` calls `rtc_.reset()` at `emulator.cpp:318`, whose
`regs_.fill(0)` wipes what the constructor put there, *before* reaching the
`set_fixed_time()` call at `:6601`. So with no `--rtc` the registers are only
ever repopulated by a guest I2C transaction, and a 500-frame NextZXOS boot
issues none. **`--rtc` does not record "the time you recorded at" — it
POPULATES registers that are otherwise zero**, which is why the host clock does
not leak into a golden and why two no-`--rtc` recordings agree. (If a future
boot did touch the RTC inside the recorded window, that would stop being true,
and the two-recordings-agree check below is what would catch it.)

With `--rtc` they hold sec/min/hour/weekday/date/month/year at **0-based
offsets 2 149 871–2 149 877** (`regs_[0..6]`, two bytes into the 69-byte `rtc`
block, which starts at 2 149 869 — read off the stream's own sentinel chain,
not computed from §4's table).
**The number of differing bytes is data-dependent, not a constant**:
a pinned time ending `:00:00` matches the zero baseline in two of the seven and
differs in **five** (2 149 873–2 149 877), one ending `:00` differs in **six**,
any other in all **seven**. Two no-`--rtc` recordings taken seconds apart are
byte-identical, and so are two recordings pinned to the same time, so a diff
here is never noise.

Two things that are *not* traps, both measured the same day: the `--load` file
does not reach the recording (it is taken on a fresh `Emulator` with
`load_file` cleared — `blue.nex` and `beast.nex` give byte-identical streams),
and the pinned *value* changes nothing outside those seven bytes. One thing that
is: the cache directory comes from `$JNEXT_CONFIG_DIR` (default `~/.jnext`),
**not** from `--sdcard`, so `--sdcard /elsewhere/sd.img` still writes
`~/.jnext/warm-start/`. Set `JNEXT_CONFIG_DIR` per run to keep two goldens
apart and to leave the user's own cache alone.

The gate is then: `cmp` clean at every step, and the stream length still exactly
**2 292 965**. Both are cheap enough to run per subsystem, which is what turns
S3–S5 into a transcription with immediate feedback rather than a big-bang
rewrite.

### 17.2 Shipping posture

There is no partial-restore value: a snapshot that restores half a machine is
not a snapshot. So `.jns` stays behind the `--snapshot-*` flags and out of the
GUI's default suffix until **S6** lands, and `format_version` is not frozen —
i.e. not promised — until **S9** is green. The first public release that
mentions `.jns` is the one that freezes it.

## 18. Open questions for the owner

### 18.1 Answered — 2026-09-23, do not reopen

| Q | Answer |
|---|---|
| Container | **Option C** — ZIP + JSON manifest/state + binary blobs. Proceed. |
| JSON dependency | **nlohmann/json, vendored.** Proceed. |
| Decoupling scope | **Full** — the field descriptor across all subsystems. The narrower "scalars only" reading is declined. |
| SD card | **The two-tier identity of §11.3**, with the §11.3 refusal/warning matrix. |
| Effort | Re-measured from the §9.4 call-site classification: **S2–S5 is 7–10 sessions**. §17 updated (whole project 14–23, incl. S5b). |
| **Mid-frame save** | **Always advance to the next frame boundary; never refuse.** This overruled the recommendation in revision 2, which was to refuse while paused. §10.2 P7, §15.2 and §16.2 follow the decision; the cost — the machine ends up to one frame past where the user paused — is documented rather than hidden. |

### 18.2 Still open

1. **File extension**: `.jns`? Alternatives considered: `.jnx` (too close to
   `.nex`), `.nxs`, `.jsnap`. The warm-start cache uses `.jwss`, so `.jns` is
   consistent with it.

2. **CLI surface**: five flags, or collapse `--snapshot-strict` /
   `--snapshot-force-sdcard` into one `--snapshot-mode strict|normal|force`
   (§15.1)?

3. **Debugger state (§10.2 P9)**: confirm breakpoints, watches and the rewind
   ring do **not** travel. A case can be made for breakpoints — cheap, and a
   user resuming a debugging session would want them — but they are not machine
   state, and mixing them in weakens the format's definition. Note that
   `AudioMute` is already settled the same way, in the code and for the same
   reason (§10.1).

4. **Copy-on-write SD mounting** (out of scope here, §11.2): it would make the
   rejected "delta against a base image" option viable and change the whole SD
   question. Worth its own issue, or not worth raising?

5. **Does the warm-start cache migrate onto this?** §3.2 notes the descriptor
   layer would let `kFormatVersion` be derived rather than hand-bumped, retiring
   the "a field repurposed is invisible" hazard there too. Out of scope for #27;
   follow-up issue, or deliberately left alone?

6. **Priority.** The issue records "priority stays low". At 14–23 sessions this
   is still one of the larger remaining v1.1 items — confirm it is wanted now,
   at that cost, against the other open features.

7. **The JSON hex string for a MULTI-BYTE-element array has no declared
   element order — for S9.** *Recorded by S4's review, 2026-09-24. Not to be
   fixed before S6 defines `JsonWriteDesc`: it is a hole in §6.2's
   specification, not a defect in shipped code.*

   §6.1 keeps `PaletteManager` (4 608 B) and `Copper::instructions_` (2 048 B)
   in JSON, and §6.2's only array encoding is "one lower-case hex string, no
   separators". Both of those are `uint16_t` arrays — and they are the **only**
   multi-byte-element `d.bytes()` collapses in the tree (checked: of the
   eighteen `d.bytes()` call sites, every other one is a `uint8_t` array, and
   `SpriteEngine::sprites_` is a `SpriteAttr[128]` whose five members are each
   one byte, so its `static_assert` is about *padding*, not byte order).

   **The binary side has no such hole, by construction.** `write_bytes` memcpys
   the host representation and `write_u16` *is* a `write_bytes` of 2
   (`saveable.h:36-38`), and a `std::array<uint16_t, N>` is contiguous and
   unpadded — so N sequential `write_u16`s and one `write_bytes` of 2N copy the
   same bytes in the same order on any host, big-endian included. S4 pinned the
   contiguity half with `static_assert`s (`palette.cpp:724-728`,
   `copper.cpp:338`), so a member that changed type or gained an element fails
   to compile rather than shifting the stream under the byte-identity gate.

   **The JSON side has the hole.** A hex string of a `uint16_t` array is the
   HOST byte image, so `0x1234` writes `3412` on this box and `1234` on a
   big-endian one, and §6.2's `pattern: "^[0-9a-f]{N}$"` accepts both — the
   schema cannot see the difference either. §6.2's worked example
   (`"instructions": "0000ffff1234…"`) is an instance of the unspecified case.
   §13's independent spec-written reader is written from §6.2 and will hit this
   the first time it decodes one of the five arrays.

   Three ways out, none chosen here: declare the hex string **little-endian per
   element** and have `JsonWriteDesc` byte-swap on a big-endian host; declare it
   a **JSON array of numbers** for multi-byte elements, which also lets the
   schema bound each value; or add a `d.words()` primitive so the declaration
   states the element width instead of the encoder inferring it from a byte
   count. The choice belongs with whoever writes `JsonWriteDesc`.

### 18.3 Defects this design surfaced in shipped code

Not questions — findings, to be fixed inside this work per the no-deferral rule,
and listed together so none is lost:

| # | Defect | Where |
|---|---|---|
| D1 | `SdCardDevice` has no `save_state` at all; a snapshot or rewind taken mid-CMD18 restores a card that is not streaming | §10.2 P1 |
| D2 | `Multiface::mf_type_` is knowingly lossy — rebuilt from three booleans, and `mf_type=10` loses a bit | §4.3(3), §10.2 P13 |
| D3 | 131 072 bytes (the DivMMC RAM window) are serialised twice in every snapshot | §4.3(1) |
| D4 | 8 192 bytes of dead zeros (the Multiface private array) are serialised on every Next snapshot | §4.3(2) |
| D5 | The CPU `/INT` window is written as a signed delta reinterpreted as `uint64_t`, and `INT64_MAX` as a bare sentinel | §7.4 |

Disposition:

- **D1, D2** — correctness defects, fixed in **S6**.
- **D3, D4** — **FIXED in S5b** (2026-09-24), immediately after the migration,
  when a golden diff was still explainable field by field — and it was, as a
  two-range splice with every other byte identical in place (§17.0). Together
  they were 6.1 % of every rewind slot, which was too much to leave behind a
  scaffold.
- **D5 — deferred permanently, by design.** The same eight bytes round-trip:
  `save_state` casts `int64_t`→`uint64_t` and `load_state` casts straight back
  (`emulator.cpp:12287-12289`), so the binary stream has **no observable
  defect** and there is nothing to fix there. It is recorded because it is a
  defect of **legibility and external validation** — no human or external
  validator reading `18446744073708986683` can tell it means −564 933 — and JNS
  removes it by emitting the signed value (§7.4). Stated as "permanently, by
  design" rather than "not worth changing", which invites someone to revisit it
  and change the one stream the gate depends on.

---

*Design document for issue #27. **Stages S1–S5b are implemented** — the
descriptor layer and the binary realisations (§9), the migration of every
subsystem onto them, and the removal of the duplicated RAM (§17.0); S6 onward
is not. The §4 measurements were taken
on 2026-09-23 against `main` @ `15430513`, i.e. the PRE-migration tree the
byte-identity gate (§17.1) uses as its oracle, and the code references are to
that commit; the §4.1/§4.2 corrections and the §17.1 `--rtc` measurement were
re-taken on the S1–S5 branch on 2026-09-24 and are marked where they appear.*
