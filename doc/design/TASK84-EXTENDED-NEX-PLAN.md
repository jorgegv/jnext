# Task 84 / issue #29 — Extended NEX (self-streamed payload) — Phase 1 evidence

## Implementation update (2026-07-24)

**Status:** implemented on `fix/atic-atac-next`; the full unit/FUSE/regression
triplet and interactive Atic Atac gameplay verification pass. Independent
review returned APPROVE before submission; the final branch was then rebased
onto v0.99.13 and its SD Nac-gap fix, with the full triplet repeated.

The full option from §8 was selected after issue #84 supplied a second practical
application: Atic Atac Next. The implementation now:

- reads only the header-described NEX region and keeps appended bytes in the
  host file;
- honours all three `file_handle` forms (`0`, `1`, and an address at or above
  `$4000`);
- provides read-only host-backed `F_READ`, `F_SEEK`, `F_FGETPOS`, `F_FSTAT`,
  `DISK_FILEMAP`, `DISK_STRMSTART`, and `DISK_STRMEND`;
- emits the SD/MMC data, CRC, and ready-token sequence through port `$EB`;
- permits one read-only, same-directory, non-symlink companion file, required
  for `ATICATAC.CFG`, while refusing absolute paths, parent traversal, symlinks,
  and write access; and
- gives a directly loaded NEX the initialized mounted-SDHC state that a normal
  NextZXOS browser launch has already established;
- serves the DivMMC ROM from its real physical SRAM page `$08`, so a direct
  loader's config-mode `NR $04=$04` writes are visible to the subsequent
  DivMMC NMI overlay; and
- implements NR `$C0` bit 3 stackless NMI execution: NMIACK changes SP without
  touching stack RAM, NR `$C3:$C2` captures the interrupted PC, and RETN uses
  the live register pair while restoring SP; and
- applies canonical RETN overlay clears at the completed-instruction boundary.
  This keeps DivMMC/Multiface mapped for RETN's own fetch, but removes them
  before JNext predecodes the returned-to instruction.

The unresolved `F_SEEK` point in §6 is now settled by the NextZXOS API contract:
the mode is in `IXL`; the observed `$80` was the low byte of the guest's `IX`
buffer pointer being misread as `L` during the evidence pass.

Acceptance uses only generated GPL-compatible fixtures in the repository. A
synthetic functional NEX proves both the file API and the block-stream/port
path end to end. The issue #29 runtime-only oracle, `NEXTEST.NEX`, also loads
directly and reaches its Spectrum Next 2MB Factory Test screen with the appended
26 MB payload available. The second runtime-only oracle is the freeware Atic
Atac Next release
(listed in [`../REFERENCES.md`](../REFERENCES.md)), whose published
requirements explicitly pair `ATICATAC.NEX` with `ATICATAC.CFG`, require SDHC
and NextZXOS 2.02+, and require the NEX file to be unfragmented.
The direct-load artwork is pixel-identical to CSpect's output; after the final
DivMMC/stackless-NMI and RETN-boundary fixes the game also leaves that artwork,
animates its story intro, accepts Enter to skip it, reaches the title/menu, and
enters stable interactive gameplay. ZEsarUX 13.0 with
`--sd-enable-sdhc-addressing` was the independent instruction-level oracle for
the `$0066` NMI sequence. No third-party game data is committed.

Stackless NMI was not an accidentally forgotten flag. The original NMI plan
deliberately deferred its CPU effect because it required modifying the FUSE Z80
core and there was then no reproducible user-visible consumer. Atic Atac Next
is precisely the second driver that plan required: without stackless NMI and
the SRAM-backed DivMMC ROM it remains frozen at the loading artwork.

The final gameplay handoff exposed a separate emulator-ordering problem. The
older DivMMC approximation cleared its overlay on the first M1 after RETN.
That is electrically reasonable, but too late for JNext: its CPU wrapper has
already read and classified that instruction before delivering the M1 callback.
If the stale overlay byte selects a wrapper-handled ED/Z80N opcode, FUSE cannot
repair the decision by refetching underlying RAM. The production hook now
latches canonical ED 45 during decode and applies DivMMC and Multiface clears
immediately after `Z80Cpu::execute()` returns. `atic_atac_nmi_test`
ATIC-NMI-03/04 pin the DivMMC and Multiface paths with deliberately different
overlay and underlying opcodes.

The remainder of this document is retained as the original evidence record. Its
status statements describe the 2026-07-18 evidence phase, not the implementation
above.

**Status:** evidence gathering only. **No emulator code was written or changed.**
**Date:** 2026-07-18
**Branch:** `task84-extended-nex` (off `main` @ `bb821f56`)

Every claim below is tagged:

- **PROVEN** — verified by reading the cited file, or by running a command whose output is quoted here.
- **INFERRED** — reasoned from proven facts; plausible, not directly verified.
- **ASSUMED** — taken on faith; no evidence gathered.
- **NOT PROVEN** — explicitly attempted or considered and *not* established.

---

## 0. Verdict up front

**The issue's central premise does NOT survive.**

Issue #29 assumes NEXTEST.NEX reaches its 26 MB appended payload through the esxdos
**file** API (`F_OPEN`/`F_READ`/`F_SEEK`/`F_CLOSE`), and that honouring the header's
file-handle field plus a host-backed `F_READ`/`F_SEEK` would therefore make it work.

**PROVEN by runtime trace:** it does not. NEXTEST uses the esxdos **low-level block
streaming API** — `DISK_FILEMAP` ($85) → `DISK_STRMSTART` ($86) → raw `INIR` from the SD
data port → `DISK_STRMEND` ($87). `F_SEEK`/`F_READ` appear only as the two-call *preamble*
the streaming API's own documentation mandates before `DISK_FILEMAP`, not as the transfer
mechanism.

The bulk 26 MB transfer therefore **never passes through the `RST $08` hook at all**. It is
a sequence of `IN A,(C)` reads from port `$EB` at the SPI/SD block-device layer.

This is the outcome issue #29 flagged as the scope-growing risk. It is not `M_P3DOS`
(that part of the premise *does* survive — NEXTEST never calls it), but the practical
consequence is the same or worse: **`DISK_FILEMAP` returns physical SD-card sector
addresses**, which are meaningless for a host file that is not on the emulated SD card.

Work items 1–4 as written in the issue would produce a loader that gets NEXTEST exactly as
far as `DISK_FILEMAP` and no further. **PROVEN** — see §3.5, where a forced `DISK_FILEMAP`
error makes NEXTEST print `DISK FILEMAP ERROR` and stop, with no fallback path.

---

## 1. Method — how the evidence was obtained

**LOUD DISCLOSURE: the runtime observations in §3 were made under a PATCHED BINARY.**

NEXTEST cannot be loaded by `--load` today (that is the issue). Driving the NextZXOS
Browser headlessly to launch it was **NOT attempted** (see §6 — this is a real gap in this
evidence). Instead three throwaway, env-gated patches were applied locally, built, run,
and then **reverted**. They are **not committed**; the only committed artefact of this task
is this document. `git status` was verified clean afterwards.

| Patch | Env gate | What it did |
|---|---|---|
| P1 | `JNEXT_TASK84_ALLOW_EXTENDED` | `nex_loader.cpp`: load the header-described region only, skip the extended-NEX rejection. Payload not read, no handle delivered. |
| P2 | `JNEXT_TASK84_DOSVERSION` | `emulator.cpp` `M_DOSVERSION`: override the reported version in `DE`. |
| P3 | `JNEXT_TASK84_HOSTFILE` | `emulator.cpp`: back `F_SEEK`/`F_READ`/`F_FSTAT`/`F_CLOSE` with a real `ifstream` on the loaded NEX, accepting any handle. |

**What the patched binary could distort.** P3 accepted *any* handle, so NEXTEST's file
operations succeeded even though no handle was ever delivered to `0xBFFE` — its handle byte
was garbage (`A=$00`). This is a *deliberate over-permission*: it lets the observation run
past the handle problem to reach the interesting part. It means the trace shows what
NEXTEST *does* when its file calls succeed, which is exactly the question. It does **not**
tell us whether a correctly-delivered handle would change the sequence — **INFERRED** that
it would not, since the handle is only an operand, not a control input.

P1 skipped the payload entirely, so the file NEXTEST streams from and the file jnext read
its banks from were the same host file — consistent, not a distortion for this purpose.

The observation ran with `--sdcard roms/nextzxos-1gb-fat32fix.img` present but with
`--load`, i.e. **not** booted through NextZXOS. NEXTEST ran as a bare NEX with jnext's
`--esxdos-stub` standing in for the OS.

**Instrument sanity check (PROVEN).** The Task 85 tracer was confirmed working on the
existing `esxdos-chain-red-func` case before being pointed at NEXTEST:

```
[esxdos] [trace] -> $8F M_EXECCMD    AF=0044 BC=00FE DE=000F HL=0031 IX=8251
[emulator] [info] esxdos stub: requested .RUN '.../red.nex'
[esxdos] [trace] <- $8F M_EXECCMD    ok    A=00 BC=00FE DE=000F HL=0031
```

---

## 2. NEXTEST.NEX header — PROVEN by direct byte read

Read from the TBBlue oracle checkout's `extras/nextest/NEXTEST.NEX` (26 025 651 bytes,
proprietary — **never copied into the repo**):

| Field | Offset | Value |
|---|---|---|
| magic / version | 0 / 4 | `Next` / `V1.2` |
| `num_banks` | 9 | 3 |
| `screen_flags` | 10 | `0x00` (no screen block) |
| `sp` | 12 | `0xBFFE` |
| `pc` | 14 | `0xBF00` |
| bank bitmap | 18 | banks 0, 2, 5 present (3, agrees with `num_banks`) |
| `entry_bank` | 139 | 2 |
| **`file_handle`** | **140** | **`0xBFFE`** |

Header-described size = `512 + 0 + 3 × 16384` = **49 664**; appended payload =
**25 975 987** bytes. Matches the issue's quoted rejection message exactly.

Note `sp` and `file_handle` are the *same* address, `0xBFFE`. **INFERRED:** the handle is
written to the top of the program's stack area, below `SP`, and read from there.

---

## 3. The runtime trace — the decisive evidence

### 3.1 Unpatched jnext, tracing only, no stub (PROVEN)

```
[emulator] [info] esxdos syscall tracing enabled (--log-level esxdos=trace)
[esxdos] [trace] -> $88 M_DOSVERSION AF=0A09 BC=253B DE=3000 HL=0203 IX=0000
[esxdos] [trace] <- $88 M_DOSVERSION NOT IMPLEMENTED by jnext — falling through to $0008
```

The very first esxdos call is the OS version check. Nothing follows it.

### 3.2 With `--esxdos-stub` (reports NextZXOS 1.94) — PROVEN

```
[esxdos] [trace] -> $88 M_DOSVERSION AF=0A09 BC=253B DE=3000 HL=0203 IX=0000
[esxdos] [trace] <- $88 M_DOSVERSION ok    A=00 BC=4E58 DE=0194 HL=6E65
```

**Exactly one syscall**, then nothing. Screenshot at frame 400 shows NEXTEST's own message:

> `NEXTZXOS 2.01 OR ABOVE REQUIRED`

**PROVEN:** the issue's second flagged risk is real and is a **hard gate**. The stub's
`DE = 0x0194` (`emulator.cpp:940`) is rejected. The required version is **2.01**, not the
2.1 the issue guessed.

### 3.3 With the reported version raised to `0x0201` — PROVEN

```
[esxdos] [trace] <- $88 M_DOSVERSION ok    A=00 BC=4E58 DE=0201 HL=6E65
[esxdos] [trace] -> $9F F_SEEK       AF=0010 BC=0000 DE=0000 HL=9F80 IX=0000
[esxdos] [trace] <- $9F F_SEEK       ERROR A=05 BC=0000 DE=0000 HL=9F80
```

Past the gate, the **first file operation is `F_SEEK`, not `F_OPEN`**. NEXTEST never opens
its own file — it expects to be handed an already-open handle, which is precisely what the
header's `file_handle` field at offset 140 is for. `A=$00` here is the garbage handle,
since nothing was written to `0xBFFE`.

`0x0201` was chosen to satisfy the on-screen "2.01 or above" demand; that the program then
proceeded is the evidence the encoding is right. **NOT PROVEN:** the exact encoding rule of
the `DE` version word (BCD? packed decimal?) was not derived from an oracle.

### 3.4 With host-backed `F_SEEK`/`F_READ` (patch P3) — PROVEN, and this is the crux

```
[esxdos] [trace] -> $9F F_SEEK       AF=0010 BC=0000 DE=0000 HL=9F80 IX=0000
[emulator] [info] TASK84 F_SEEK mode=128 off=0 -> pos=0
[esxdos] [trace] <- $9F F_SEEK       ok    A=00 BC=0000 DE=0000 HL=9F80
[esxdos] [trace] -> $9D F_READ       AF=0010 BC=0001 DE=0000 HL=9F80 IX=8000
[emulator] [info] TASK84 F_READ buf=0x9f80 want=1 got=1 from=0
[esxdos] [trace] <- $9D F_READ       ok    A=00 BC=0001 DE=0000 HL=9F81
[esxdos] [trace] -> $85 DISK_FILEMAP AF=0010 BC=0001 DE=0002 HL=9F81 IX=6887
[esxdos] [trace] <- $85 DISK_FILEMAP NOT IMPLEMENTED by jnext — falling through to $0008
```

**Seek to offset 0 → read exactly 1 byte → `DISK_FILEMAP`.**

This is not an ad-hoc sequence. It is the streaming preamble verbatim as documented in the
oracle, `tbblue/src/asm/streaming/stream.asm:88-93`:

> *"Note that this call (DISK_FILEMAP) should be made directly after opening the file - no
> other file access calls should be made first. (If the file has been accessed, the
> filepointer should be reset to the start using F_SEEK, and a single byte read (with
> F_READ) before making this call. This will ensure that the current sector information
> maintained by the OS is correctly pointing to the first sector of the file.)"*

The identical idiom appears in `src/asm/dot_commands/fragmentation.asm:125-139`
(`f_seek` → `f_read` 1 byte *"to ensure starting cluster"* → `disk_filemap`).

The observed register state also matches the oracle's documented `DISK_FILEMAP` convention
(`stream.asm:119-123`): `A`=handle, **`IX`**=buffer address, `DE`=buffer size in 6-byte
entries. Trace shows `IX=0x6887`, `DE=0x0002` — a 2-entry filemap buffer. **PROVEN
consistent.**

### 3.5 Forcing a clean `DISK_FILEMAP` error — PROVEN, no fallback exists

```
[esxdos] [trace] -> $85 DISK_FILEMAP AF=0010 BC=0001 DE=0002 HL=9F81 IX=6887
[emulator] [info] TASK84 DISK_FILEMAP -> forced error A=5
[esxdos] [trace] <- $85 DISK_FILEMAP ERROR A=05 BC=0001 DE=0002 HL=9F81
```

Screenshot shows NEXTEST's own message:

> `DISK FILEMAP ERROR`

and execution stops. **PROVEN: NEXTEST has no `F_READ`-based fallback path.** Streaming is
mandatory, not an optimisation.

### 3.6 Static corroboration — `RST $08` opcode scan (PROVEN)

The issue's static scan looked for bare code bytes. A sharper scan of the same 3 loaded
banks for the actual two-byte call form `CF <code>` (`RST $08` + `DEFB`, the calling
convention at `esxapi.def:10-13`) finds these call sites:

| Code | Name | Sites | First offsets |
|---|---|---|---|
| `$85` | `DISK_FILEMAP` | 1 | `0x22FF` |
| `$86` | `DISK_STRMSTART` | 1 | `0x2351` |
| `$87` | `DISK_STRMEND` | 1 | `0x2357` |
| `$88` | `M_DOSVERSION` | 1 | `0x8456` |
| `$8B` | `M_TAPEIN` | 1 | `0x754C` |
| `$8E` | `M_GETDATE` | 1 | `0x377F` |
| `$8F` | `M_EXECCMD` | 1 | `0x6563` |
| `$9A` | `F_OPEN` | 1 | `0x22C3` |
| `$9B` | `F_CLOSE` | 1 | `0x2327` |
| `$9D` | `F_READ` | 1 | `0x22F0` |
| `$9F` | `F_SEEK` | 2 | `0x22E1`, `0x4B11` |
| `$A3` | `F_OPENDIR` | 1 | `0x7114` |
| `$A8` | `F_GETCWD` | 2 | `0x5959`, `0x5CE2` |

`F_OPEN`/`F_SEEK`/`F_READ`/`DISK_FILEMAP`/`F_CLOSE`/`DISK_STRMSTART`/`DISK_STRMEND` form
one tight cluster at `0x22C3`–`0x2357` — a single streaming routine. **`$94 M_P3DOS` has
zero call sites**, corroborating the trace.

Bank order on disk is `5, 2, 0` (`nex_loader.h:235-236`), so this cluster is in bank 5.

### 3.7 Direct answers to the questions posed

| Question | Answer | Confidence |
|---|---|---|
| Which esxdos calls, in what order? | `M_DOSVERSION` → `F_SEEK`(0) → `F_READ`(1 byte) → `DISK_FILEMAP` → (would be `DISK_STRMSTART` → raw port reads → `DISK_STRMEND`) | **PROVEN** to `DISK_FILEMAP`; the tail is **INFERRED** from the oracle + §3.6 call sites |
| Does it call `M_P3DOS` ($94)? | **No.** Zero call sites statically, none in the trace. | **PROVEN** (for the path reached) |
| Raw sector / IDE path? | **Yes** — via `DISK_STRMSTART`, then `INIR` from the SD data port, bypassing `RST $08` entirely | **PROVEN** that `DISK_FILEMAP` is reached and mandatory; the port-level tail is **INFERRED** from `stream.asm` |
| Streams from its own file? Handle route? | Yes. It never calls `F_OPEN` on itself — it expects the handle pre-delivered. Header offset 140 = `0xBFFE` ⇒ write the handle to `0xBFFE`. | **PROVEN** (no `F_OPEN` before `F_SEEK`); the `0xBFFE` semantics are per the NEX spec, **not** independently verified — see §6 |
| Startup OS/SD version check? | **Yes, hard gate.** Rejects the stub's 1.94 with `NEXTZXOS 2.01 OR ABOVE REQUIRED`, after exactly one syscall. | **PROVEN** |
| Launched via Browser: does its I/O appear in the trace? | **NOT PROVEN — not attempted.** See §6. | — |

---


## 3.8 RESOLVED (2026-07-18) — the `M_DOSVERSION` encoding IS in the oracle

Recorded as NOT PROVEN above; the citation was simply not found at the time. It is in the
tbblue dot-command sources, `src/asm/dot_commands/makelnk.asm:30-36`:

```asm
        ld      hl,'N'<<8+'X'
        sbc     hl,bc                   ; check NextZXOS signature
        jr      nz,bad_nextzxos
        ld      hl,$0206
        ex      de,hl
        sbc     hl,de                   ; check version number >= 2.06
        jr      c,bad_nextzxos
```

The same pattern appears in `crc32.asm:61-65` and `$.asm:26-30`. Therefore:

- **`BC` = `'N'<<8 | 'X'` = `$4E58`** is the NextZXOS signature. jnext already returns this
  (`emulator.cpp:939`) and it is correct.
- **`DE` = version, major in hex, minor in BCD.** `$0206` is compared as "2.06", so `$0194`
  is 1.94 and `$0201` is 2.01. The empirically-chosen `$0201` in §3.3 was right.
- Callers test with `sbc hl,de` + `jr c` — i.e. a `>=` threshold comparison.

**The gate is not a file-access problem.** NEXTEST issues `M_DOSVERSION` first and stops
before any file call if the version is too low, which is why item 6 of issue #29 is
independent of the streaming work. Raising `emulator.cpp:940` is a one-line change, but the
value is a **behaviour claim**: a program that sees 2.01 may assume 2.01 semantics from a
stub servicing a handful of calls. It should be raised deliberately, to a version jnext can
honour, not to get past a check.

---

## 3.9 DOWNGRADED (2026-07-18) — the `F_SEEK` "mode=0x80" finding is weakly grounded

Recorded above as an open unknown ("`L=0x80` matches none of `set`/`fwd`/`bwd`"). On
re-examination it is **probably an artifact**, and in any case does not block
implementation.

**Neither oracle documents a bit 7.** `esxapi.def:143-145` and `esxdos.def:62-64` define
exactly three modes (`set=0`, `fwd=1`, `bwd=2`), and every in-tree caller loads one of them
directly (`fragmentation.asm:129`, `defrag.asm:143-149`, `createp3d.asm:125`,
`uninstall.asm:381`). `L` is genuinely the mode register — that part is correct.

**Why the observation is suspect.** Across the three traced calls `HL` reads
`9F80` → `9F80` → `9F81`: it increments by one after a one-byte read. That is the signature
of a **pointer NEXTEST is carrying in `HL`**, not a mode byte — code intending a mode emits
`ld l,0`, leaving `HL=xx00`. Additionally the patch logged `F_READ buf=0x9f80` while the
same trace line shows `IX=8000`, and jnext's stub takes the F_READ buffer from `IX`; those
disagree, so the throwaway patch may have misread parameters, and `mode=128` came from that
same patch. The patch was reverted and never committed, so it cannot be re-inspected.

**Why it does not block implementation.** `0x80 & 0x03 == 0` = `esx_seek_set`, which is
exactly the documented intent of the preamble: "the filepointer should be reset to the start
using F_SEEK" (`stream.asm:88-93`), with offset `BC:DE = 0`.

**Guidance for issue #29:** mask `L` to its low two bits, and **log loudly** whenever any
high bit is set. That yields correct behaviour for the observed case while turning a genuine
undocumented flag — if one exists — into a visible diagnostic instead of silent divergence.

---

## 4. What this means for issue #29's four work items

| # | Issue's item | Verdict against the evidence |
|---|---|---|
| 1 | Stop rejecting; load header region, record payload offset | **Still valid and necessary**, but on its own gets NEXTEST to a `DISK FILEMAP ERROR` screen. Value is for *other* extended NEX files, not this one. |
| 2 | Honour `file_handle` (BC, or write to named address) | **Still valid**, and more central than the issue implies: NEXTEST never calls `F_OPEN`, so without the delivered handle there is no file at all. |
| 3 | Host-backed `F_READ`/`F_SEEK`/`F_CLOSE` over an `ifstream` | **Valid but NOT SUFFICIENT for NEXTEST.** It satisfies the 2-call preamble and nothing else. **PROVEN** by §3.4–3.5. |
| 4 | Sandbox via `resolve_sibling()` | **Still valid**, unchanged. |
| **5 (NEW)** | `DISK_FILEMAP` / `DISK_STRMSTART` / `DISK_STRMEND` + a card-address space the streamed port reads can serve | **This is the actual blocker.** Absent from the issue. |

### 4.1 Why item 5 is hard

`DISK_FILEMAP` returns *physical card addresses* — 6-byte entries of
`{4-byte card address, 2-byte sector count}` (`fragmentation.asm:141-152`,
`stream.asm:94-118`). `DISK_STRMSTART` then arms the SD controller at that card address and
the guest reads bytes **straight off port `$EB`** with `INIR`, checking SD data tokens
(`$FE`) and skipping per-block CRCs (`stream.asm:219-259`). No further `RST $08` occurs
until `DISK_STRMEND`.

So a host-file-backed design has nowhere to put the data: there is no card address that
corresponds to a host file. Two options, neither small:

- **(a) Synthesise a card-address space.** Invent a private address range, have
  `DISK_FILEMAP` hand it out, and have the SPI/SD layer serve port-`$EB` reads for that
  range out of the host file. Contained, but it means the streaming path and the real
  SD path coexist in `src/peripheral/sd_card.cpp`, and the guest's SD-protocol expectations
  (token `$FE`, 2-byte CRC per 512-byte block) must be honoured byte-for-byte.
- **(b) Put the file on the emulated SD card.** Then `DISK_FILEMAP` is answerable honestly
  and streaming needs no special case — but this is the "synthetic FAT32 block device"
  conclusion Task 89 reached independently, and is a materially larger piece of work.

**Note the convergence:** Task 89 concluded from a different direction (NextZXOS Browser
I/O bypasses `RST $08`) that the `RST $08` layer is insufficient and a block-device layer
is required. This task reaches the same structural conclusion for NEXTEST specifically.
Task 89's Browser claim remains **INFERRED**; this task did not test it (§6).

### 4.2 A defensible smaller scope

Items 1–4 alone are still worth doing, **provided the issue's acceptance criterion changes**
from "NEXTEST runs" to "extended NEX files that stream via the esxdos *file* API run".
NEXTEST would keep a clear, honest error rather than today's up-front rejection. Whether
any such file exists in practice is **NOT PROVEN** — NEXTEST is the only extended NEX
examined, and it does not qualify.

The version gate is a second, independent blocker: the stub reports 1.94 and NEXTEST wants
≥ 2.01. Raising the reported version is a one-line change but is a **behaviour claim about
what jnext implements**, and should not be made just to get past a check jnext cannot then
honour.

---

## 4.3 CORRECTION (2026-07-18, user challenge) — `DISK_FILEMAP` redirection is cheap

§4.1 framed answering `DISK_FILEMAP` as "either synthesise a card address space that the
SPI layer serves, or use the synthetic FAT32 block device". That is wrong: it presents two
options as comparable when they differ by an order of magnitude.

Re-reading `stream.asm`, **three of the four streaming steps are `RST $08` calls jnext
already intercepts** — `DISK_FILEMAP` ($85), `DISK_STRMSTART` ($86), `DISK_STRMEND` ($87).
Only the `INIR` pump reads port `$EB`.

- Card addresses are **opaque cookies**. The only arithmetic the guest is documented to do
  is `+512` or `+1` per block per bit 1 of the flags jnext itself returns
  (`stream.asm:115-119`), so synthetic addresses that are plain file offsets work.
- One filemap entry spanning the whole file is legitimate — files are "often unfragmented,
  and so will use only 1 entry" (`stream.asm:107-110`), and a host file is contiguous by
  construction.
- `BC` (block count) on `DISK_STRMSTART` is explicitly ignored by the SD/MMC protocol
  (`stream.asm:176-178`).
- The per-block stream on `$EB` is fully specified at `stream.asm:241-259`:
  **512 data bytes, 2 CRC bytes (read but never checked), then a `$FE` token**
  (guest polls, `$FF` = not ready, anything else = error).

**No boot sector, no FAT tables, no directory entries, no cluster chains, and NextZXOS's
own block driver is never involved** — the real ROM code for those three calls never runs.
This is NOT the synthetic FAT32 volume of
[TASK89-ESXDOS-HOST-FILESYSTEM.md](TASK89-ESXDOS-HOST-FILESYSTEM.md) §2.3 and carries none
of its boot-path risk.

Remaining blockers, none of them retired by this correction: jnext needs a hook in the port
`$EB` read path (`src/peripheral/spi.cpp`, **unexamined**); the v2.01 "bit 7 = don't wait
for token" mode moves the token wait into the guest (`stream.asm:187-193`); the 2.01
version gate is independent; and the multi-entry `refill_map` path must still behave.

**Read off the oracle, not verified by a running experiment.** Deferred to milestone v1.1.

**Scope consequence (user decision, 2026-07-18):** issue #29 was re-scoped a second time to
**include the streaming API** — `DISK_FILEMAP` / `DISK_STRMSTART` / `DISK_STRMEND` plus the
port `$EB` diversion — which puts NEXTEST.NEX back in reach. The `$EB` hook point was
subsequently checked and is clean: `Spi::read_data()` (`src/peripheral/spi.h:90`) is the
single entry point for every port `$EB` read, so the diversion has one chokepoint rather
than scattered call sites.

This does NOT reverse the §0.1 decision in the Task 89 note: the synthetic FAT32 block
device stays declined. Streaming is served by intercepting three `RST $08` calls jnext
already hooks and diverting one port read — no FAT structures, no involvement of NextZXOS's
own block driver.

**"Streaming works" still does not imply "NEXTEST runs":** the NextZXOS ≥ 2.01 version gate
is an independent blocker, and NEXTEST's startup OS/SD verification remains uncharacterised
beyond the version string. NEXTEST is a stretch goal; the acceptance criteria rest on
synthetic fixtures.

---

## 5. Sandboxing — reusing the `resolve_sibling()` precedent

`resolve_sibling()` (`src/core/emulator.cpp:914-927`) is the existing precedent, currently
used only by `M_EXECCMD`. It resolves a guest-supplied name against the **directory of the
active NEX** and rejects anything that escapes:

```cpp
if (target.empty() || target.is_absolute() || target.has_parent_path() ||
    extension != ".nex")
    return std::string{};
```

For this task the constraint is *narrower and therefore safer*: the only file that needs
opening is **the loaded NEX itself**. No guest-supplied path is involved at all — the
handle is delivered by the loader, not requested by name. Recommended posture:

- Open exactly one host file: `config_.load_file` / `active_nex_path_`, opened **read-only**.
- Deliver its handle per header offset 140; the guest can only ever name that handle.
- Do **not** widen `F_OPEN` to arbitrary paths as part of this work. If it is ever widened,
  reuse `resolve_sibling()`'s rules with the `.nex` extension check relaxed, and keep the
  absolute/parent-path rejections intact.

---

## 6. What was NOT done — stated loudly

- **The NextZXOS Browser launch was NOT attempted.** NEXTEST.NEX is not on
  `roms/nextzxos-1gb-fat32fix.img`, and adding a 26 MB proprietary file to the shared test
  image was judged out of bounds. Consequently:
  - The question *"if launched via the Browser, does its file I/O appear in the `RST $08`
    trace?"* is **NOT PROVEN**.
  - Task 89's inference that Browser I/O bypasses `RST $08` is **NOT independently tested**
    by this task.
  - It is **NOT PROVEN** that NEXTEST behaves identically under real NextZXOS; the traced
    behaviour is under `--esxdos-stub` with a patched version response.
- **`DISK_STRMSTART` / `DISK_STRMEND` were never reached at runtime.** Their role is
  **INFERRED** from `stream.asm` plus the call sites in §3.6. The port-`$EB` streaming loop
  was never observed executing.
- **The `F_SEEK` mode byte is unexplained.** Observed `L = 0x80`. `esxapi.def:143-145`
  defines only `esx_seek_set=0`, `esx_seek_fwd=1`, `esx_seek_bwd=2`. `0x80` matches none.
  The throwaway patch treated it as absolute-seek and NEXTEST proceeded, but **the meaning
  of bit 7 is NOT PROVEN** and must be resolved from an oracle before any real `F_SEEK` is
  implemented.
- **The `M_DOSVERSION` `DE` encoding is NOT PROVEN** (see §3.3).
- **No test suite was run.** No product code changed, so there was nothing to regress; this
  is stated rather than implied.

---

## 7. Test strategy (for the implementation phase, if it proceeds)

The 26 MB proprietary NEXTEST.NEX **can never enter the repo or CI**. Tests must use a
small synthetic fixture.

**Synthetic extended NEX generator.** A build/test-time helper producing a NEX with:
- a valid `V1.2` header, one bank (bank 5), `screen_flags = 0`;
- `file_handle` (offset 140) set to a known address, e.g. `0xBFFE`, to exercise the
  write-to-memory delivery route;
- a small deterministic appended payload (a few KB — e.g. a counting byte pattern) so a
  read at any offset has a checkable expected value;
- variants: `file_handle = 0` (close after load), `file_handle = 1` (handle in `BC`), and
  `file_handle = 0xBFFE` (write to address) — the three cases the NEX spec defines.

**Unit rows (`nex_loader` / esxdos stub):**
- header region loads; declared banks applied; appended payload **not** slurped into memory;
- payload start offset recorded correctly for each screen-flags combination;
- handle delivered per each of the three `file_handle` cases, asserted at the named address
  / in `BC` / not delivered;
- host-backed `F_SEEK` absolute/forward/backward against the known pattern, including seek
  past EOF (clamping — `nextzxos-changelog.txt:321` documents clamp-to-size);
- `F_READ` across the header/payload boundary returns the pattern bytes, short read at EOF
  reports the actual count;
- `F_CLOSE` then `F_READ` fails;
- sandbox: the stub can be made to open nothing but the loaded NEX.

**Functional row:** a tiny hand-written Z80 test NEX (in `test/00regression/nex/`, built
like the existing `menu/red/blue.nex`) that reads its own appended payload via the delivered
handle and paints a colour keyed on the bytes it got — asserted by screenshot, in the style
of the existing `esxdos-chain-*` rows. This is the row that proves the path end-to-end
without any proprietary binary.

**Explicitly NOT a test row:** "NEXTEST runs". Per §3–4 it cannot, without item 5.

**Manifest discipline:** every added row updates its count in `test/unit-tests.conf` /
`test/00regression/functional_tests.conf` — the harness refuses to run otherwise.

---

## 8. Recommendation

Before writing any loader code, **issue #29 should be re-scoped against §3–4** and a
decision taken by the user:

1. **Narrow** — implement items 1–4, acceptance = "extended NEX via the esxdos *file* API",
   accept that NEXTEST still fails (with a better error). Small, honest, but of **NOT
   PROVEN** practical value since no qualifying file has been identified.
2. **Full** — additionally take on item 5 (streaming API + a card-address space). This is
   the only route that runs NEXTEST from the CLI, and it overlaps heavily with Task 89's
   block-device conclusion. Should be planned jointly with Task 89, not separately.
3. **Close as won't-fix** — keep the current rejection, on the grounds that these files are
   designed to run under NextZXOS from the SD card and the streaming API is inseparable
   from a real card.

The evidence does not by itself select among these; it does rule out the issue's stated
plan achieving its stated goal.
