# G46(b) — GH #84: Atic Atac Next stall (SPI-poll probe session)

## Symptom

Atic Atac Next (9bitcolor, extended NEX that streams its own 111 MB asset
file via raw SD/SPI after one esxdos `DISK_FILEMAP`) freezes on its splash
screen. Deterministic repro (private SD clone, `ATICATAC.NEX`+`.CFG` at
root):

```
./build/jnext --headless --machine next --sdcard <clone>.img \
    --delayed-keypress-frames 400 space \
    --delayed-keypress-frames 470 enter \
    --delayed-keypress-frames 700 down \
    --delayed-keypress-frames 740 down \
    --delayed-keypress-frames 800 enter \
    --delayed-automatic-exit-frames 7000
```

Splash art renders by frame 2500; screenshots at frames 2500/6000/15000 are
byte-identical. No crash, no logged error.

## Probes added

All in `src/core/emulator.cpp`, all env-gated (single cached `getenv()`
pointer check at every call site — zero cost when unset). All are
diagnostic-only; none change behaviour when their env var is unset.

- **`JNEXT_G46B_EB_POLL`** (+ `_START` / `_END` frame-window bounds, default
  whole run) — counts port `$EB` (SPI data) reads/writes and port `$E7` (SPI
  CS) writes, with a per-PC histogram (count + first/last frame seen) per
  access kind, plus an UNCONDITIONAL once-per-frame PC sample
  (`frame_pc`) and an IFF1 census split at frame 888. Ticks its own
  `frame_counter` from `Emulator::begin_new_frame()` — **do not** reuse
  `Emulator::frame_num_`, which is rewind-buffer-only bookkeeping
  (incremented exclusively inside `rewind_buffer_ && rewind_enabled_`) and
  silently stays 0 for the entire run of any plain headless invocation
  without `--rewind-buffer-size`. Dumped at process exit via a **deliberately
  leaked** heap singleton (see the code comment on `g46b_eb_poll_state()`
  for why a plain function-local `static` object crashed at shutdown — a
  static-destruction-order race between the object and its own
  `std::atexit` dump callback).
- **`JNEXT_G46B_PCTRACE`** (+ `_LO`/`_HI` address range, default
  `0x1a00`-`0x1a30`; `_CAP`; `_MIN_FRAME`, requires `JNEXT_G46B_EB_POLL` for
  frame gating) — logs every M1-fetch PC in range with 3 opcode bytes + AF/
  BC/HL/SP/IFF1, up to a cap. `_MIN_FRAME` is essential: the default range is
  ALSO hit heavily during legitimate pre-stall firmware activity, which
  exhausts a small cap before the stall window is ever reached.
- **`JNEXT_G46B_EIDIHALT`** (+ `_MIN_FRAME`, `_CAP`) — logs every DI ($F3) /
  EI ($FB) / HALT ($76) opcode fetch, deduped on consecutive identical
  `(pc,op)` (a held `HALT` re-fetches the same instruction every T-state
  while waiting for an interrupt — correct Z80 semantics — so undeduped
  output is dominated by one wait).

All three coexist in one run (`JNEXT_G46B_EB_POLL=1 JNEXT_G46B_PCTRACE=1
JNEXT_G46B_EIDIHALT=1 ...`); `PCTRACE`/`EIDIHALT` read `frame_counter` from
the `EB_POLL` singleton.

## DZRP scripts

**None added.** A CSpect differential was considered (see "Next-session
priority" below) but not attempted this session — the mechanism was already
pinned down precisely enough with the probes above that a CSpect run's
marginal value (confirming CSpect takes a different branch at the specific
PC, without visibility into *why*) did not justify the setup cost (menu
navigation has no jnext-equivalent `--delayed-keypress-frames` convenience
over DZRP; would need an intermediate snapshot or a bespoke key-injection
script) within this session's budget.

## Session 2 (2026-07-24) — probes added

Continuing directly from "Next-session priority" #1-2 above. All new probes
follow the same env-gated, zero-cost-when-unset pattern; all coexist with
the Session 1 probes and with each other. Whole-run verified: `make
unit-test` after these changes → **5527/5527 pass, 0 fail, 0 skip** (no
regression from touching `src/memory/mmu.h`, a widely-shared header — see
the mmu_test link note below).

- **`JNEXT_G46B_RETCAP`** (+ `_PC` hex default `0xE3D9`, `_MIN_FRAME`
  default 900) — one-shot: the first time the target PC (the RET
  terminating `wait_frames(100)`) is fetched at/after `_MIN_FRAME`, reads
  the return address off `(SP)` (the RET hasn't executed yet at its own M1
  fetch, so SP still points at the two bytes it's about to pop) and stores
  it in the shared `G46bEbPollState` for `JNEXT_G46B_EB_POLL_SNAPSHOT_FRAME`
  to disassemble later.
- **`JNEXT_G46B_CALLCAP`** (+ `_PC` hex default `0x1A00`, `_MIN_FRAME`
  default 940, `_COUNT` instructions default 48, `_MAX_HITS` default 1,
  `_WATCH_ADDR` hex default `0x9D38`) — **live** (not delayed) capture of
  the caller of a given CALL target: at the M1 fetch of the target PC,
  `(SP)` already holds the return address the enclosing CALL pushed.
  Disassembles **forward** from it (never backward — Z80 opcodes are
  variable-length, so walking backward from an arbitrary byte offset is
  unsound; a first attempt at backward disassembly from `ret_addr-3`,
  assuming a 3-byte `CALL nn`, produced a plausible-looking but WRONG
  decode — see Findings). Also dumps NR 0x50-0x57 (all 8 MMU slot pages)
  and a 32-byte window around `_WATCH_ADDR`, live, at the same instant.
  Repeats up to `_MAX_HITS` times, every hit (not deduped to one per frame
  — a first pass showed the target is entered multiple times per frame).
- **`JNEXT_G46B_WRITEWATCH`** (+ `_ADDR` hex default `0x9D38`) —
  whole-run (no frame gating) watch on one CPU address through the MMU's
  **normal** (non-overlay) write path only; logs every write with a
  running counter, frame, PC, and value. Lives in `src/memory/mmu.h`'s
  `Mmu::write()` (an inline hot-path method), not `emulator.cpp` — this is
  where the session's one real infrastructure bug surfaced (see below).
- **`JNEXT_G46B_PCTRACE`** extended: now also prints `DE` (was previously
  missing — AF/BC/HL/SP/IFF1 only — which cost a full extra probe cycle
  when tracing a routine whose branch depends on `DE`).
- **`g46b_disasm_dump()`** — a new shared free function in `emulator.cpp`
  (anonymous namespace hoisted to file scope) wrapping
  `src/debug/disasm.h`'s `disasm_one()`, used by both
  `JNEXT_G46B_CALLCAP` (live) and the existing
  `JNEXT_G46B_EB_POLL_SNAPSHOT_FRAME` (delayed — now also disassembles the
  `JNEXT_G46B_RETCAP` return address and the `frame_pc` hot address, in
  addition to its pre-existing raw hex dump). Reusing the project's own
  Z80N-aware disassembler is far more reliable than the ad-hoc hand-decoding
  Session 1 did.

**Infrastructure bug found and fixed during this session** (not the game
bug — a defect in the probe code itself, caught by `mmu_test`'s build, not
by inspection): the first `JNEXT_G46B_WRITEWATCH` implementation declared
`uint32_t g46b_current_frame_for_probes()` / `uint16_t
g46b_last_pc_for_probes()` in `emulator.cpp` (part of the `jnext_core`
library) and forward-declared them in `mmu.h`. `mmu.h` is included by
targets that link `jnext_memory` but **not** `jnext_core` (e.g.
`mmu_test`), so this was a real link-time hazard, not just style —
`mmu_test` failed to link with `undefined reference to
g46b_current_frame_for_probes()`. Fixed by moving the storage into
`src/memory/mmu.cpp` (two plain `extern` globals,
`g46b_probe_frame_counter` / `g46b_probe_last_pc`, part of `jnext_memory`,
which everything using `mmu.h` already links) and having
`Emulator::on_m1_prefetch` / `Emulator::begin_new_frame()` write them
directly. Second bug caught in self-review before any run was trusted: the
first `JNEXT_G46B_WRITEWATCH` env check was `if (const char* x =
std::getenv(...))` **without** `static` — i.e. it called `getenv()` on
*every* `Mmu::write()`, not once, breaking the "zero cost when unset"
contract the methodology requires. Fixed to the established `static const
char* x = std::getenv(...)` cached-pointer pattern used by every other
G46B probe in this file. Both fixes are in the committed code; a full
`cmake --build build` (all targets) and `make unit-test` (5527/5527) were
run clean *after* both fixes, not just after the first one.

## Findings

**H1 confirmed, H2 disproven, H3 resolved (not a tracer gap).**

- **H2 disproven**: `JNEXT_G46B_EB_POLL` (unrestricted window) shows the
  LAST port `$EB`/`$E7` touch of the entire run at **frame 888** (PC
  `$E3F1`). Zero touches of either port from frame 888 through frame 10000
  tested (2+ minutes of run time). The CPU is not blocked on a SPI/SD
  wait loop.
- **H3 resolved — not a tracer gap**: the esxdos RST-$08 hook fires
  correctly (confirmed `$85 DISK_FILEMAP` traced). `$86`/`$87`
  (DISK_STRMSTART/END) never fire because the game's own asset-streaming
  code talks to the SD SPI ports directly (raw CMD17/CMD18 protocol bytes
  clocked via ports `$E7`/`$EB`), exactly as `tbblue/src/asm/streaming/
  stream.asm` documents is the Next-only-optimised alternative to the
  esxdos streaming API ("your code could be slightly faster and simpler if
  writing a Next-only program" — stream.asm:207-209). Confirmed directly:
  re-running with genuine `--log-level sdcard=trace` (this session's first
  actual trace-level SD capture — all prior captures, this session's and
  the parked one's, were at `debug` level and never enabled the `CMD18 next
  block` per-block trace line) shows the 14th CMD18 stream (`sector=196895`)
  delivering blocks **196895 through 197168 — exactly 273 blocks, matching
  the parked note's figure** — via jnext's per-block re-prime path
  (`sd_card.cpp:457-519`), cleanly, with **no error token, no anomaly**. The
  game's own `$EB` reads simply stop after that — consistent with the game
  having read exactly as much as it wanted, not with jnext under-delivering.
- **H1 confirmed and localised precisely**:
  - The full stream-closing sequence at `$E3EB`-`$E3F5` (a `wait_token`-style
    loop: `DEC BC / LD A,B / OR C / JP Z,timeout / IN A,($EB) / CP $FE / JR
    NZ,retry`) is stream.asm's documented idiom with an added BC-timeout —
    and it works correctly (273 successful `$FE` token waits, matching the
    273 delivered blocks).
  - **`$1A00`** is a small unpack/copy subroutine containing a tight DJNZ
    loop `$1A12`-`$1A1D` (fully decoded): `LD A,(DE) / INC E / LD (HL),A /
    INC HL / LD A,(DE) / INC DE / LD (HL),A / ADD HL,$000B / DJNZ $1A12` —
    reads 2 bytes from `(DE)` per pass, writes them at `(HL)`/`(HL+1)`, then
    advances HL by a further 11 (net stride 12 bytes/pass — a de-interleave/
    unpack-into-strided-records pattern).
  - **Definitive proof of zero cumulative progress**: `$1A00` is invoked with
    **byte-for-byte identical starting register state** (`BC=$5000 HL=$A350`,
    and matching AF at every matching BC) at frame 1500 and again at frame
    5000 — a 3500-frame gap. It restarts from scratch every single
    invocation; nothing persists progress across frames.
  - **`$E3D0`-`$E3D9`** is a `wait_frames(BC)` primitive: `LD HL,($E833) /
    XOR A / SBC HL,BC / JR C,$E3D0` (busy-poll a 16-bit counter at `$E833`
    — almost certainly ISR-incremented once per vblank — until it reaches
    target `BC`), **then `DI` / `RET`** on success. Traced end to end:
    entered at frame 888 with `($E833)=51, BC=100` (borrow, loops), runs
    correctly with **interrupts ON** throughout (confirmed: `EI` at frame
    850, no DI/EI/HALT event in between), succeeds at **frame 936** (`DI`
    fires, `PCTRACE` on `$E3D0` alone shows exactly one contiguous
    busy-wait, entirely within frames 888-936, zero hits from frame 940
    onward through frame 10000). This DI is very likely *intentional* on
    the game's part (hand exclusive access to the caller, who is expected
    to `EI` after its own critical section) — **not itself a bug**.
  - **The actual defect boundary**: after that `RET` (frame 936, interrupts
    now off), **no DI/EI/HALT of any kind ever executes again** through
    frame 10000 (confirmed non-deduped). The IFF1 census
    (`JNEXT_G46B_EB_POLL` summary) shows only 49 of 9114 post-888
    once-per-frame samples with IFF1=1 — all concentrated in the 850-936
    window, zero after. From roughly frame 942 onward, the CPU repeats
    ONLY: call `$1A00` (same starting state every time) → (implicitly)
    return → repeat next frame, forever, with interrupts permanently
    masked and no `HALT`-based interrupt wait either — a pure busy spin.

**Root cause, at the mechanism level (not yet the ultimate jnext-side
cause)**: the code that runs immediately after `$E3D9`'s `RET` — the
*caller* of the `wait_frames(100)` primitive — never re-enables interrupts
and never breaks out of re-invoking `$1A00`. Given the game is
known-working on ZEsarUX and real hardware, *something* that caller checks
(most plausibly a flag/counter this session did not identify — did not
disassemble past the `RET` at `$E3D9`, since that requires either the
game's own symbols/source or unwinding the return address off the stack at
exactly the frame-936 transition) must evaluate differently under jnext.
**This was NOT traced to a specific jnext line or VHDL divergence** — per
the STOP condition, no fix was attempted.

## Session 2 findings — the caller of `wait_frames(100)`, fully decoded

Session 2 completed items 1 and 3 of the Session-1 priority list above
(quoted verbatim in git history) in full, and definitively ruled out item 4
(the 50/60Hz hypothesis). Item 2 (CSpect DZRP) was evaluated as
premature — see "What remains open" below — and not attempted.

**The full call chain from `$E3D9`'s `RET` to the stuck retry, all
instruction-boundary-verified (never guessed backward through arbitrary
bytes):**

1. `JNEXT_G46B_RETCAP` (`PC=0xE3D9`, `MIN_FRAME=900`) fires once, at
   frame 936: `ret_addr=$31AE`. Same result for `JNEXT_G46B_CALLCAP`
   (`PC=0x1A00`) at frames 940/941/942/etc. — SP is **always exactly
   `$FDFF`** at every `$1A00` entry (byte-identical), and the return
   address is **always exactly `$31AE`**.
2. Live disassembly (`JNEXT_G46B_CALLCAP`, forward from `$31AE` — see the
   `disasm.h`-vs-hand-decode note below) plus a full single-frame
   instruction trace (`JNEXT_G46B_PCTRACE LO=0000 HI=FFFF MIN_FRAME=941
   CAP=20000`, one complete "loop period") gives the exact loop:
   - `$31AE` onward runs a short sequence culminating in `$31B4: CALL
     $30C0` → `$34E9` (a subroutine that `OR`s 3 bytes at `(HL)`,
     `(HL+1)`, `(HL+2)` together, `RET NZ` if any is non-zero, else `SCF;
     RET` — i.e. sets Carry iff all three are zero) → `$31B7: JR
     C,$31AD`.
   - **Confirmed live** (trace #1044-1045, frame 941): the branch is
     **taken** — Carry is set **every single time**, deterministically —
     landing on `$31AD`, whose byte is `$CF` = `RST $08` (1-byte opcode;
     confirms the address really is a genuine instruction boundary reached
     via a real conditional jump, not a mis-aligned guess).
   - `RST $08` pushes the return address `$31AE` (`$31AD`+1, matching
     exactly what `JNEXT_G46B_CALLCAP` had already captured — see point 1)
     and jumps to **`$0008`**.
   - **`$0008` is a documented DivMMC automap trigger address**
     (`Divmmc Entry Points 0`, NR `0xB8` bit 1 — `nextreg.txt:987-996`:
     *"bit 1 = 1 to enable automap on address 0x0008 (instruction
     fetch)"*). The `$E186`-onward machine-init sequence that runs right
     after `wait_frames(100)` succeeds (disassembled via
     `JNEXT_G46B_EB_POLL_SNAPSHOT_FRAME`) writes `NEXTREG $B8,$00` —
     **explicitly disabling** automap on `$0008` (bit 1 clear), among
     other entry points, moments before the CPU ever reaches `RST $08`.
   - With automap on `$0008` disabled, the CPU executes whatever is
     genuinely mapped there: NR `0x50` (MMU slot 0, `$0000-$1FFF`)
     currently holds physical page `0x14`, and that page reads **all
     zero** from offset `$0008` through `$19FF` — 6600+ contiguous `$00`
     bytes. The CPU therefore "NOP-slides" (executes each zero byte as a
     1-byte NOP, `PC++` each time) all the way up to `$1A00`, where real
     code resumes (the `$1A12`-`$1A1D` DJNZ unpack loop from Session 1).
     `$1A00` re-uses the stack slot `RST $08` pushed — its own `RET`
     therefore lands back at `$31AE`, closing the loop with **zero net
     stack drift** (confirmed: SP is `$FDFF` at every single `$1A00`
     entry).
   - **PC=`$1A00` is entered 3 times within one ~20000-instruction
     "period"** (trace indices #245, #7694, #15143 — not once per frame as
     first assumed), with 17597 of those 20000 traced instructions
     (88%) being the literal `$00`-as-NOP execution described above.
     `88%` of the observed CPU time in the stuck loop is spent walking a
     blank memory page, not doing anything else.

3. **The actual gate: `mem[$9D38..$9D3A]`.** At the `$34E9` check, `HL`
   (after the `EX DE,HL` at `$3296`) is `$9D38`. **Zero bytes at
   `$9D38-$9D3A` is why Carry is always set.** Confirmed a `JNEXT_G46B_
   CALLCAP`-live memory window dump: all-zero across a wide margin
   (`$9D30-$9D57`).

4. **`JNEXT_G46B_WRITEWATCH`** (extended, PC-attributed) on `$9D38`,
   `$9D39`, `$9D3A`, `$9D3B` (whole run, no frame gate) shows the complete
   history:
   - Frames 17-837: **real, varying content** IS written to all four
     bytes, repeatedly, from **PC `$1F70`** — decoded live
     (`JNEXT_G46B_PCTRACE LO=1F00 HI=1FA0`): `op=ED B2` = **`INIR`**, the
     standard Z80 block-I/O instruction (`IN (HL),(C); HL++; B--; repeat`).
     `C` is `$EB` (SPI data port) throughout — this is the raw SD/SPI
     byte-stream reader depositing incoming bytes directly into memory at
     an ever-increasing `HL`, one INIR burst per SD command.
   - **The last write of real (non-forced-zero) content to any of the
     four bytes is at frame 837** (val `$08` at `$9D38`), immediately
     followed one write later, same frame, by `val=$00` on all four —
     `INIR` faithfully storing whatever the SPI port returned at that
     instant.
   - **A 100-frame silence follows** (838-936: zero writes to any of the
     four bytes) — covering both the tail of whatever preceded it and the
     entire `wait_frames(100)` window (888-936).
   - **From frame 937 onward, ALL writes are `val=$00`, from `PC=$32C2`**
     (`LD (HL),B`, part of the `$329E-$32C4` "recompute and store a
     3-byte checksum of `$9D39-$9D3B` back into `$9D38-$9D3A`" routine —
     traced with `DE` now visible in `JNEXT_G46B_PCTRACE`: `DE` computed
     from a `$2D7F` XOR/SUB/ADC chain over `$9D39-$9D3B`'s content comes
     out `$0000` every time, because those bytes are *also* zero, so the
     "checksum" of zero is zero and the store is a no-op re-write).
   - **Cross-checked against `--log-level sdcard=trace`, correlated by
     wall-clock line position**: the frame-824-837 `INIR` burst that
     fills `$9D38-3B` runs during a sequence of **431+ individual `CMD17`
     (single-block) reads**, CS toggled between *every one*, over
     **highly scattered sector numbers** that **loop back and re-visit
     the same range** (`196767` appears at the start of the captured
     window and again immediately after `198110`) — the signature of a
     directory/FAT-chain **search/walk**, not the documented linear
     `CMD18` asset stream (which starts separately, at sector `196895`,
     around frame 838-841 in this same trace — i.e. **after** this
     search has already stopped).
   - **SD-image content verified byte-for-byte at the exact position**:
     read sector `198109` (the sector active at the very last write,
     frame 837) directly from the raw `.img` file (`python3`, no jnext
     involved) — `358/512` non-zero bytes, with the **same** scattered
     `$00`/`$08` pattern `WRITEWATCH` recorded. **jnext delivered exactly
     what is really on the disk image at that position — no content
     corruption, no protocol anomaly, matching Session 1's independent
     `CMD18`-level "clean, no error token" finding for the big stream.**

5. **50/60Hz formally ruled out** (Session-1 item 4): the retry gate is a
   **pure memory-content check** — the complete opcode-byte histogram of
   the entire stuck-loop instruction stream (20000-instruction trace,
   Session 2) contains **zero** `IN`/`OUT`/`NEXTREG` instructions of any
   form except the one-time `$E186`-`$E246` init sequence (which
   completes successfully and is not revisited). NR `0x05` is never read
   anywhere in the retry loop. Same evidence rules out NR `0x1E`/`0x1F`
   raster reads, DMA busy/completion polling, and Layer2/copper status as
   the retry gate — **none of those ports/registers are touched inside
   the loop at all.**

**Root cause is now fully mechanised down to a single fact**: on this SD
image, at this exact point in the boot/load sequence, a directory/FAT
search issues 431+ `CMD17` single-sector reads, never writes non-zero
content to `$9D38-$9D3B`, and then **stops issuing any further SD/SPI
command of any kind, forever** (confirmed to frame 10000 in Session 1).
jnext's SD emulation is verified content-correct (byte-for-byte disk match)
and protocol-correct (Session 1) at every point checked. **What is NOT yet
determined is why the search terminates without finding a non-zero
match** — i.e. whether jnext or the game decides "stop searching" one
step too early, and if jnext, which read/status/timing signal it computes
differently from real hardware to reach that decision.

## Addendum — a second `$85 DISK_FILEMAP` exists nearby, not yet reconciled

A last, cheap check with `--log-level esxdos=trace` (RST `$08` interception,
`src/core/esxdos_trace.h` — this traces genuine esxDOS/NextZXOS firmware
calls, separate from the game's own raw-SPI code characterised above) found
a **second** `$85 DISK_FILEMAP` call (the first was the well-known one from
early boot, Session 1's H3). Binary-searched by `--delayed-automatic-exit-
frames` (no new probe needed — plain repeated runs) to **frame 835-836**:
inside the 804-837 `CMD17` burst window, but **very late in it**, and the
`->`/`<-` trace pair for `DISK_FILEMAP` itself appears to resolve near-
instantly once reached (no evidence it spans the whole 30+-frame burst on
its own). **This does not cleanly confirm or replace the "game's own scan"
characterisation above** — it may be a small, unrelated call (its `BC=1
DE=5` parameters look like a small file handle/entry, not the 111 MB asset)
that merely happens to land inside the same wall-clock window, or the two
could be related in a way not yet traced (e.g. the game's own scan finding
what it needs and *then* calling `DISK_FILEMAP` for a second, smaller
file). Flagged here rather than asserted as resolved — needs the item 1
trace below to settle which code (game or firmware) is actually issuing
the bulk of the 431+ reads.

## Session 3 (2026-07-24) — the burst is `$85 DISK_FILEMAP` itself, root cause fully mechanised, no jnext divergence found

Continuing directly from "Next-session priority" #1 above. **No new probe
code was needed or added this session** — every finding below came from
reusing the existing `JNEXT_G46B_EB_POLL`/`JNEXT_G46B_CALLCAP`/
`JNEXT_G46B_WRITEWATCH` probes at new target PCs/`WATCH_ADDR` values,
cross-checked against an independent, jnext-free Python FAT32 parser run
directly against the raw `.img` file. Fresh reflink SD clone for this
session: `/tmp/claude-1000/.../scratchpad/atic84s3/atic-sd-s3.img`, a
`cp --reflink=auto` of the session-2 clone; a freshly-captured
`--log-level sdcard=trace` run against it reproduced the exact same
`862` total `CMD17`s session 2 recorded, confirming no cross-session
image drift.

**Correction to the addendum's "second `$85 DISK_FILEMAP`" framing**: a
clean, whole-run `--log-level esxdos=trace` capture (frame 0-900, no
window) shows **exactly one** `$85 DISK_FILEMAP` call in the entire run,
not two. Args: `AF=03B8 BC=0001 DE=0005 HL=E84E IX=E84D`. `IX` is the
esxDOS calling convention's filename pointer; a live memory dump at
`$E840` (via `JNEXT_G46B_CALLCAP` `WATCH_ADDR=E840`, any hit in the
frame-836 window) reads `41 54 49 43 41 54 41 43 2e 4e 45 58 00` =
**`"ATICATAC.NEX\0"`**. The game is asking esxDOS to file-map **itself**
— consistent with `stream.asm`'s documented pattern (map your own big
asset file once, then stream it directly over raw SPI, bypassing the
slower `DISK_STRMSTART` API).

**The 431+-read burst is not a mystery loop — it is this single
`DISK_FILEMAP` call's own execution**, running for real, unmodified
firmware/ROM code (jnext's `on_esxdos_call` hook explicitly does **not**
service `$85` — `--esxdos-stub` was never passed in any session's repro
— so every one of these reads is genuine firmware, not a jnext
reimplementation):

- **Two stack contexts, cleanly separated by `SP`.** `JNEXT_G46B_CALLCAP`
  at the low-level read's `RET` (`$1F84`) across all 445 hits in the
  burst shows `SP` step through **three** ranges in strict order: hits
  1-9 (frames 810-821, `SP≈$FE-FFxx`, the caller's normal stack) → hits
  10-433 (frames 821-837, `SP≈$20xx`, a **much lower, ROM-internal
  stack** — DivMMC/esxDOS routines conventionally switch to their own
  stack on entry, precisely so they don't depend on the caller's stack
  headroom) → hits 434-445 (frames 837-842, back to `SP≈$FDxx`). The
  `SP≈$20xx` block is **421 of the 445 total reads** — it *is* the bulk
  of the burst, and its start/end frames (821/837) box in `$85
  DISK_FILEMAP`'s entire execution window precisely.
- **Root-directory search, byte-verified.** Early in the `SP≈$20xx`
  window, `JNEXT_G46B_CALLCAP` at `$11AE` (a shared "is this a live
  32-byte FAT directory entry?" check: `LD A,(HL) / AND A / SCF / RET Z`
  for `0x00`=end-of-directory, then `CP $E5 / SCF / RET Z` for
  `0xE5`=deleted, VHDL-external but standard FAT32 semantics) shows `HL`
  walking a copy of the root directory's two sectors at 32-byte strides
  from `$2340`. **An independent, jnext-free Python FAT32 parser** (MBR
  → BPB → FAT walk, no jnext involved, run directly against the raw
  `.img`) finds `ATICATAC.NEX`'s real 8.3 entry at **root-directory byte
  offset 608** (sector 2144, in-sector offset 96) — i.e. relative to the
  scratch copy's second-sector base `$2540`, offset `608-512=96=$60`,
  predicting `HL=$25A0`. **The live trace hits `HL=$25A0` and there
  transitions to a deeper call (`ret_addr` changes from `$124F` to
  `$17C3`, `SP` drops a further 18 bytes)** — i.e. **jnext's firmware
  finds the correct directory entry at exactly the position independent
  ground truth predicts.** This is concrete, positive proof the SD/FAT
  emulation is feeding the firmware correct data, not just an absence of
  visible errors.
- **Cluster-chain walk, byte-verified.** The same independent Python
  parser walks `ATICATAC.NEX`'s full FAT chain from its start cluster
  (12166): **13632 clusters, all contiguous, one single run**, from LBA
  196767 (matches the burst's `NextV1.2` NEX-header sector found in
  session 1/2) through LBA 414878. The observed CMD17 sequence's tail —
  reads at 198109, 198110, back to 196767, then FAT sector 190, then
  191-296 linearly, then `2143-2144` again, then **exactly sector
  414879** (one past the file's independently-computed last data
  sector) — matches this real chain exactly, including the "one sector
  past the end" boundary probe. **jnext's firmware-driven FAT walk
  discovers the file's true, complete extent, correctly, byte for
  byte.**
- **`$9D38` is not a result/status field — it is inside a reused, cyclic
  16-sector (one-cluster, `$8000`-`$9FFF`) scratch buffer.**
  `JNEXT_G46B_CALLCAP` at `$1F6B` (the one-shot, per-call point right
  after `POP HL` sets the destination pointer — **not** `$1F6E`/`$1F70`,
  which are the two `INIR` opcodes themselves and fire once per
  *byte*, an off-by-one this session made and caught before trusting
  the data) shows the `SP≈$20xx` phase's destination address first climb
  linearly `$4000→$BE00` (one-time ~32 KB load, 64 sectors), then hold at
  a single sector (`$2100`, ~32 repeats — plausibly a retry or
  fixed-position re-check), then **cycle repeatedly through exactly 16
  slots, `$8000,$8200,...,$9E00`, wrapping back to `$8000` every 16 —
  one whole 8 KB cluster's worth, reloaded ~20 times** (frames 829-837,
  matching the cluster-chain-walk timing). `$9D38` sits at
  offset `$1D38` = byte 312 of the 15th of those 16 sub-sectors — i.e.
  its content is simply **whichever raw byte of ATICATAC.NEX's own file
  data happened to land there from the *last* cluster cycled through
  this scratch window before `$85` returned**, not a designed "map
  succeeded/failed" flag. A direct live dump of `$9CD0`-`$9D70` at frame
  950 (long after the freeze) reads **all zero across the whole
  window** — there is no recognisable extent-list record (no
  `0x0003009F`/`0x00035400`-style encoded sector/length pair) anywhere
  near it, ruling out the "it's the 2nd-of-N extents, legitimately empty
  for a 1-run file" hypothesis floated mid-session; it is unstructured,
  reused scratch content.

**Verdict — jnext's SD/FAT/protocol layer is exonerated at every level
checked; the ultimate mechanism is not conclusively pinned to a specific
jnext line, and no fix was attempted (per the STOP condition):**

- Byte-for-byte disk content delivery: re-confirmed (sessions 1, 2, 3).
- SD/SPI protocol framing (6-byte CMD17 frame, token wait, 512+2-byte
  data phase via two 256-iteration `INIR`s, CS handling): matches the
  textbook SD SPI single-block-read sequence exactly; **zero failures
  across all 445 low-level reads in the burst** (`Carry` set at every
  single `$1F84` hit, including the last one).
- FAT chain computation: **exact match** against an independent,
  jnext-free ground truth for both the root-directory entry position and
  the full 13632-cluster single-run extent of `ATICATAC.NEX`.
- Given jnext explicitly does **not** reimplement `$85` (confirmed via
  the `esxdos=trace` `"not serviced by jnext — dispatched to the ROM at
  $0008"` line — this is genuine, unmodified NextZXOS/esxDOS ROM code
  executing, the same binary CSpect or real hardware would run), and
  given every input to that code (disk bytes, protocol timing/success)
  is independently verified correct, **there is no remaining mechanism
  by which jnext could cause this firmware to compute a different result
  than real hardware would, given the identical disk image.**

**What is NOT fully resolved**: the *precise semantic purpose* of the
`SP≈$20xx` execution's multiple passes over the root directory (a
second, later pass reaches all the way to the last entry rather than
stopping at `ATICATAC.NEX`; a third pass stops early at entry 3, `DOT`)
was not decoded to completion — time-boxed out after the primary
question (byte/protocol/FAT-chain correctness) was answered with strong,
convergent, multi-angle evidence. This residual is almost certainly more
firmware/NEX-loader archaeology, not a new suspect area — nothing in it
touches SD/SPI I/O (all further `IN`/`OUT`/`NEXTREG` activity inside the
loop was already ruled out in session 2's opcode histogram) or memory
content jnext computes independently.

**Most important residual finding, orthogonal to all of the above**:
**no session (1, 2, or 3) has ever actually run this repro against
CSpect or real hardware.** The "G46(b)" classification of this issue —
"jnext boots/loads differently from CSpect" — has been an *assumption*
carried by the issue's categorisation, not an established fact for this
specific repro + SD image. Given how strongly the evidence above points
away from a jnext-side computational error, **confirming or refuting
that CSpect (given the exact same SD image) also freezes here is now the
single highest-value open action** — more valuable than further firmware
disassembly, because it can either (a) close the issue outright as
"not a jnext bug, real-hardware-reproducible content/firmware
interaction" or (b) prove a genuine, still-unlocated jnext divergence
exists despite every check above passing, redirecting the next
session's effort productively.

## What remains open / next-session priority (superseding all earlier lists)

1. **Run the CSpect comparison — never yet attempted, now the clear
   top priority.** Two viable approaches, in order of preference:
   - **(a) Native CSpect menu navigation.** Launch CSpect (`mono
     CSpect.exe -mmc=<same s3 image> -remote=127.0.0.1:11000`, per
     `reference_cspect_dzrp_launch.md`), connect via
     `tools/cspect_dzrp/cspect_dzrp.py`, and drive the same
     space/enter/down/down/enter sequence used by jnext's
     `--delayed-keypress-frames`. DZRP has no documented "inject a
     keystroke" command in `cspect_dzrp.py` (checked this session — only
     `WRITE_PORT`, which cannot fake an `IN` read of the keyboard matrix
     CSpect drives from its own SDL/host key state) — this needs either
     CSpect's own scripting/window automation, or **poking the ZX
     keyboard's host-visible state some other way DZRP does support**;
     survey `cspect_dzrp.py`'s full command set once more before
     assuming this is a dead end — it wasn't exhaustively checked this
     session.
   - **(b) Snapshot handoff (higher risk, not attempted — flagged, not
     recommended without care).** jnext supports `--delayed-snapshot
     FILE.sna` / `.szx` — in principle a snapshot saved right after the
     last `enter` keypress (frame ~800) could be loaded directly into
     CSpect, skipping menu navigation entirely. **Explicitly assessed
     and set aside this session**: a plain `.sna` does not capture
     Next-specific state (NextREG, DivMMC bank, MMU slots); whether
     jnext's `.szx` writer includes the Next-specific chunks CSpect's
     reader needs, and whether cross-emulator state transfer is faithful
     enough not to introduce its own artefacts, is unverified. A
     mismatch here would produce a comparison that looks decisive but
     isn't — worse than no comparison. If attempted, verify round-trip
     fidelity (jnext→own SZX→jnext, same screenshot) before trusting a
     cross-emulator load.
   - Whichever mechanism works: set a DZRP breakpoint at `$0008` (RST 8,
     confirmed DivMMC automap trigger) or directly at `$1F1E` (the
     shared low-level SD read routine's CS-assert entry, session 3's
     PC), and diff CSpect's CMD17 sector sequence against this session's
     `cmd17seq`-style capture from the identical image. Convergent
     result (CSpect also stops at sector 198109-ish / reads the same
     ~421-sector pattern and also ends up with a zero-filled `$9D38`)
     closes the issue as not-a-jnext-bug. Divergent result (CSpect reads
     *more*, or the root-directory/cluster-chain walk takes a materially
     different shape) reopens the investigation with a real, now
     narrowly-scoped target: the SD/SPI protocol-framing layer
     specifically (byte counts, wait-state timing around token/CRC
     phases), since content and FAT-chain computation are both already
     verified correct.
2. **If (1) confirms a genuine divergence**, the search space is now
   narrow and well-instrumented: `JNEXT_G46B_CALLCAP` at `$1F1E` (CS
   assert) / `$1F84` (per-read RET) / `$1F6B` (per-read destination
   pointer) already give complete per-transaction visibility; add a
   DZRP-side per-transaction log (command byte, R1, token-wait iteration
   count, first/last data byte) matching the same granularity for an
   apples-to-apples diff.
3. **A pristine-image cross-check** remains cheap insurance (carried
   over, still not done): confirm the `.img` master this session's clone
   ultimately derives from has not itself drifted from the canonical SD
   image `sync-version`/download pipeline produces.

## Session 4 (2026-07-24) — RAM-size/MMU-banking hypothesis: a real MMU
## remap found, but it does NOT touch $9D38; the true unresolved question
## moved one level deeper (what's in physical page 0x5F BEFORE frame 883)

Session 4 was asked to check the "RAM-size / MMU-banking" hypothesis
first (cheap, decisive) before attempting CSpect. **CSpect was NOT
attempted this session** — the MMU hypothesis turned up a concrete,
worth-recording mechanism and the session was interrupted (owner
handover) before it could be run to ground or before CSpect could be
started.

### Probes added (session 4)

Both env-gated, zero-cost-when-unset, coexist with all prior probes.
**Not yet committed as of this write-up — see "State" below.**

- **`JNEXT_G46B_WRITEWATCH` extended** (`src/memory/mmu.h`,
  `Mmu::write()`): the existing per-write log line now also reports
  `slot=<N> page=<hex>` — the MMU slot number (`addr>>13`) and
  `slots_[slot]` (the physical-page value `rebuild_ptr()`/`to_sram_page()`
  actually resolved, i.e. what's really backing the write), not just
  frame/PC/addr/val as before. This is what let this session catch the
  page-number change across the write-at-837 vs. read-at-937+ boundary
  directly, from one log.
- **`JNEXT_G46B_MMUWRITE_TRACE`** (+ `_SLOT` filter, `_DISASM` flag) —
  new probe, `src/core/emulator.cpp`, hooked into the existing
  `nextreg_.set_write_handler(0x50+i, ...)` installer (one lambda per
  MMU slot, already the single dispatch point for every genuine NR
  0x50-0x57 write). Whole-run, no frame gate. Logs `frame`, `pc`, `sp`,
  `slot`, `old_page` (`mmu_.get_page(i)` before the write),  `new_val`,
  and `AF/BC/DE/HL` for every NR 0x50-0x57 write, optionally restricted
  to one slot (`_SLOT`). `_DISASM` additionally calls the existing
  `g46b_disasm_dump()` forward from the CPU's current PC on every hit —
  added because `JNEXT_G46B_CALLCAP`'s disasm is anchored on `(SP)` (the
  *caller's* return address), which is the wrong anchor for "what does
  the code AT the PC that just issued this NEXTREG write actually do
  next" — a different question CALLCAP cannot answer without a CALL
  boundary at exactly the right spot.

### Finding 1 — the RAM/MMU wraparound bug is real but already fixed,
### everywhere, and provably not the mechanism here

`src/memory/ram.h`: `Ram` is **always** constructed at its default size
(`Ram ram_;` in `src/core/emulator.h:816`, no explicit size argument
anywhere in the tree) — 2048×1024 bytes = 256 8K pages = the **maximum**
Next configuration, regardless of `--machine` or any CLI flag. So jnext
never models an "unexpanded" (768K) Next for this repro; RAM extent
itself is not a live suspect.

`Mmu::to_sram_page(logical)` (`src/memory/mmu.h:1326`) computes
`static_cast<uint8_t>(logical + 0x20)` when `rom_in_sram_` (real Next
boot). The VHDL (`zxnext.vhd:2964`, `mmu_A21_A13 <= ("0001" +
('0'&page(7:5))) & page(4:0)`, confirmed by reading the FPGA source
directly) computes a **9-bit** value whose top bit is an explicit
inactive/no-SRAM-response flag for logical pages `0xE0..0xFF`. jnext's
8-bit `+0x20` arithmetic instead **wraps** those same pages onto SRAM
`0x00..0x1F` — the ROM-image area — a real, distinct divergence from
VHDL if ever reached unguarded.

It is not reachable unguarded: `to_sram_page()` has exactly 3 call
sites (`mmu.cpp:318` in `rebuild_ptr()`, and the Layer 2 read/write
paths at `mmu.h:320`/`453`), and **all 3 already have an explicit
`page >= 0xE0` / `(sum & 0x70) == 0x70` guard above them** (documented
in-code as "Verify4/9/10/11-memory class-(a)/(c) fix" — evidently closed
by an earlier MMU/Layer2 SKIP-reduction pass, not this issue). The
guarded behaviour for slots 2-7 is `read_ptr_[slot]=write_ptr_[slot]=
nullptr`, and `Mmu::read()`'s null-pointer fallback is `return 0xFF;`
(`mmu.h:368`) — i.e. even an *unguarded* hit of this bug would read back
as `0xFF` (floating bus), not `0x00`. Session 3 established the observed
$9D38 value is `0x00`. **This rules out the wraparound bug as the
mechanism for this specific symptom, independent of the guards** — even
a still-undiscovered 4th unguarded call site couldn't produce a `0x00`
read through this code path.

### Finding 2 — there IS a real MMU remap of slot 4 between write (837)
### and read (937+), and it's fully characterised — but it does not
### explain $9D38

Ground truth, `WRITEWATCH`+`MMUWRITE_TRACE` (fresh runs, this session,
`/tmp/.../scratchpad/atic84s4/logs/s4-0{1,2,3}-*.err.log`):

- Frame 837, PC `$E285` (the tail of the `$85 DISK_FILEMAP` scratch-load
  loop already characterised in session 3): last real-content write to
  `$9D38`, **`slot=4 page=54`**.
- Frame 837, PC `$E29E` (immediately after): NR `$54` restored
  `old_page=55 new_val=04` — `$85 DISK_FILEMAP` cleanly puts slot 4 back
  to page `$04`, its value throughout the rest of boot up to this point.
  (Corrects a session-3 mischaracterisation in passing: the "cycle
  through 16 sub-addresses `$8000,$8200,...,$9E00`" during frames
  829-837 is **not** offset-cycling within one fixed page — it is 20
  genuine, sequential MMU page increments, `$42` through `$55`, one NR
  `$54` write per SD sector loaded, each landing at the SAME CPU address
  window `$8000-9FFF` because the destination pointer resets each pass.
  Cosmetic correction only; does not change session 3's conclusions
  about that loop.)
- **Frame 883, PC `$E371`: `old_page=04 new_val=5f`.** This is a
  **different** write site from the DISK_FILEMAP loop (different PC,
  46 frames later, well inside the `wait_frames(100)` window that runs
  888-936). Confirmed via `WRITEWATCH`'s new `page=` field: NR `$54`
  stays at `page=5f` continuously from here through frame 1101 (the
  full extent this session checked) — **this is the final, settled
  mapping** the frame-936+ retry loop reads `$9D38` through.
- **Live disassembly at `$E371`** (`JNEXT_G46B_MMUWRITE_TRACE_DISASM=1`,
  `s4-03-mmuwrite-disasm.err.log:3487-3511`):
  ```
  e371: LD HL,$8000
  e374: XOR A
  e375: LD (HL),A        ; zero-fill loop
  e376: INC HL
  e377: BIT 4,H
  e379: JP Z,$E375        ; loops while H&0x10==0, i.e. zeroes $8000..$8FFF
  e37c: LD (HL),A         ; + one more byte at $9000
  e37d: LD IX,$E3DA       ; parameter-block pointer
  e381: LD B,$01
  e383: LD A,$2A
  e385: RST $08           ; esxDOS/supervisor call (trailing byte = fn id,
                           ; NOT literally "SBC A,D" — same disassembler-vs-
                           ; data caveat as every other RST 8 site this
                           ; investigation has hit; not decoded to a named
                           ; esxDOS function this session)
  e387: JR C,$E392         ; error path
  e389: LD IX,$8000
  e38d: LD BC,$1000        ; 4096
  e390: RST $08            ; read call: IX=dest, BC=count → reads ≤4096
                           ; bytes into $8000-$8FFF
  e392: LD A,($E83F)
  ...
  ```
  **Neither the explicit zero-fill (bounded to `$8000-$9000`) nor the
  subsequent read (`IX=$8000, BC=$1000` → bounds `$8000-$8FFF`) reaches
  `$9D38`** (offset `$1D38` = 7480 decimal into the page, i.e.
  `$8000+7480=$9D38` — 3384 bytes past the read's own upper bound).
  **`$9D38`'s value after this routine runs is therefore whatever
  physical page `0x5F` already held at that offset before this routine
  mapped it in** — this routine neither zeroes nor populates it, on
  jnext or (per the identical VHDL/firmware logic, since this is
  unmodified ROM code jnext does not reimplement) on real hardware
  either.

### What this changes about session 3's "verified byte-for-byte" claim

**Important correction, not a contradiction**: session 3's independent
disk-byte verification (`sector 198109` matching `WRITEWATCH`'s
recorded content) was checking the content that landed in **physical
page `0x54`'s** SRAM region at frame 837 — a *different* physical page
from `0x5F`, the one actually live when the retry loop reads `$9D38` at
frame 937+. That verification is correct as far as it goes, but it does
**not** establish that page `0x5F`'s content is correct — nothing this
session or prior sessions traced has shown *any* write of real content
to physical page `0x5F` at offset `$1D38` at all, on jnext or otherwise.
The open question has moved one level deeper: **is page `0x5F` supposed
to hold non-zero data at `$9D38` by the time `$E371` maps it in, and if
so, who is supposed to have written it, and did jnext execute that write
correctly?**

### State (session 4 end — owner-directed handover, stopped mid-step)

- Worktree `/home/jorgegv/src/spectrum/jnext-worktrees/wt-84b`, branch
  `fix/84-atic-cmd18`. Uncommitted at handover:
  `src/core/emulator.cpp` (+43 lines, `JNEXT_G46B_MMUWRITE_TRACE`) and
  `src/memory/mmu.h` (+12/-2, `WRITEWATCH` slot/page fields) — see git
  history on this branch for whether a follow-up commit landed these
  (the session-4 agent was directed to commit or revert before
  stopping; check `git log` / `git diff` against this doc's description
  above to see which happened).
- SD image for session 5: reflink a **fresh** copy of
  `/tmp/claude-1000/-home-jorgegv-src-spectrum-jnext/2ae33cfc-eec4-4a23-b3bd-498d667a3869/scratchpad/atic84s4/atic-sd-s4.img`
  (this session's clone; a reflink of session 3's `atic-sd-s3.img`). Do
  not reuse the `.run2`/`.run3`-suffixed copies in that same directory
  directly — those are already-run (potentially guest-mutated) working
  copies from this session's own probe runs, kept only as an audit
  trail.
- **CSpect was never attempted this session** (or any prior session —
  still true). Still the #1 open structural question per session 3's
  handover: is this even a jnext-vs-CSpect divergence at all?

### Next-session priority (supersedes nothing above; adds to it)

1. **Finish the MMU thread first — it's cheap and nearly closed.**
   Determine what physical page `0x5F` held at offset `$1D38` (=
   `$9D38` when mapped at slot 4) *before* frame 883. Cleanest probe:
   watch the **physical backing byte**, not a CPU address — i.e. hook
   `Ram::write()` (or add a check keyed on `to_sram_page(page)==
   to_sram_page(0x5F)` inside `Mmu::write()`) for absolute RAM offset
   `to_sram_page(0x5F)*0x2000 + 0x1D38`, whole run, no frame gate. This
   sidesteps the "which CPU slot has it mapped right now" ambiguity
   that made the CPU-address-keyed `WRITEWATCH` blind to writes made
   while some *other* slot had page `0x5F` mapped elsewhere (e.g. via a
   completely different CPU address if page `0x5F` was ever mapped at
   slot 0/1/2/3/5/6/7 instead of slot 4). Two outcomes:
   - **Never written (still zero from `Ram`'s zero-initialisation)**:
     then the question becomes whether page `0x5F` is the CORRECT page
     for this purpose at all — i.e. whether real hardware/CSpect would
     have mapped a *different*, already-populated page here instead
     (which would point at a genuine jnext divergence upstream of
     `$E371` — in whatever chose page `0x5F` as the NR `$54` operand,
     not in `$E371` itself, which is unmodified ROM code). Trace NR
     `$54`'s operand value `0x5F` back to where it's computed (probably
     a fixed constant in ROM, or derived from a file handle/descriptor
     table slot — `$E371`'s own `LD A,$2A / RST $08` open call and its
     parameter block at `$E3DA` are the next things to disassemble/
     decode, along with identifying esxDOS function IDs `$2A`/(trailing
     byte after `$385`, printed as data but not decoded this session)
     and `$390`'s trailing byte).
   - **Written with real content, later lost**: a genuine jnext bug —
     narrow the write down the same way this session narrowed the
     remap (frame + PC + disasm), then find why it doesn't survive.
2. **Then CSpect**, per session 3's priority list (unchanged, still not
   attempted): confirms or refutes the G46(b) premise itself. If item 1
   above closes cleanly first (page `0x5F` legitimately/correctly zero
   there, matching what real hardware would also compute from the same
   ROM code and disk image), CSpect becomes confirmatory rather than
   exploratory and the issue may be closeable as not-a-jnext-bug
   without it — but per house rules, run it before asserting that
   conclusion in the issue.

## Session 5 (2026-07-24) — ROOT CAUSE FOUND AND FIXED: missing Nac gap
## byte between R1 and the 0xFE data token shifted the game's whole CMD18
## stream by one sector

### Step 1 — session 4's question answered: page 0x5F is NEVER written

New probe `JNEXT_G46B_PHYSWATCH` (`src/memory/mmu.h`, all four
ram_-reaching branches of `Mmu::write()`): watches ONE absolute physical
RAM byte, keyed on the RESOLVED destination pointer (not the CPU
address), so it fires regardless of which slot/overlay maps the page.
Control run on physical page 0x74 (= logical 0x54) reproduced session
4's frame-837 write exactly (1 hit, pc=$1F70 INIR, val=$08). Main run
on physical page 0x7F (= logical 0x5F) offset $1D38: **first-ever write
is frame 937, PC $32C2, val=$00** — the retry loop's own
checksum-of-zeros store. Nothing ever put content there. Page-wide
census (`_PAGEWIDE`): page 0x7F receives ONLY the $E375 zero-fill
(offsets 0-$1000, frames 883-884), ~195 bytes of ATICATAC.CFG (bucket
0, pc=$15F9 = F_READ's INIR), and the retry loop's writes at $1D00-1DFF
from frame 937. Session 4's "never written" decision branch confirmed.

### Step 2 — what the $E36A routine actually is

Widened `JNEXT_G46B_EB_POLL_SNAPSHOT` with `_LO`/`_HI` (hex CPU range)
and dumped $E360-$E440: `NEXTREG $54,$5F` is a **hard-coded immediate in
the game's own loader stub** ($E36D: ED 91 54 5F), followed by
zero-fill $8000-$9000, `F_OPEN` (RST 8 fn $9A) of "ATICATAC.CFG" (name
at $E3DA), `F_READ` (fn $9D) of ≤$1000 bytes into $8000. Page 0x5F is
the game's config-scratch page; the gate at $9D38 = offset $1D38 is
beyond both the zero-fill and the config read — it must already hold
data when $E36A runs.

### Step 3 — where the gate data was SUPPOSED to come from: the game's
### own CMD18 blob loader, streaming into slot 0

$E3A5-$E3BA (same dump): `LD C,$52 / CALL $E808` sends **CMD18** with
32-bit sector = ($EA75):($EA73) + $80, then a loop `NEXTREG $50,A` for
A=$04..$14 reads 16 sectors (8K) per page into CPU $0000-$1FFF — a
**136 KB blob into logical pages $04-$14 via SLOT 0** (per-physical-page
write census frames 838-960: pages 0x24-0x34 get exactly 8192 writes
each; 0x2A/0x2B/0x2E land in the bank5/bank7 BRAMs). The loop exits
leaving NR $50=$14 — which is why the stall-time RST 8 executed page
$14's bytes. ($EA73)=0x0003009F=196767 = the file's true start LBA
(verified session 3), so the CMD18 argument 196895 is CORRECT.

### Step 4 — the smoking gun: the delivered stream is shifted one sector

The stall-time content of CPU $1A00-$1AFF (real code, the "$1A12 DJNZ
unpack loop") exists in the raw .img at exactly ONE offset: file byte
0x31C00 = **stream sector 270** — but it sat in RAM at the position
stream sector **269** should occupy. RAM[k] = file[k+1] for the whole
blob: jnext delivered the stream shifted one block early. The blob's
page-$14 file region is zero at offsets 8-$19FF and has code at $1C00+
(which landed at $1A00 in RAM); the gate bytes at $9D38 fell in a
zero span of the SHIFTED data. Nothing was wrong with WHAT jnext read —
everything was wrong with WHERE it landed.

### Step 5 — root cause in jnext: no Nac gap byte before the first 0xFE

`sd_card.cpp` first-block response for CMD17/CMD18 was
`{FF, FF, R1, 0xFE}` — the data token immediately after R1 with ZERO
gap. SD Physical Layer Simplified Spec § 7.5.2 (Nac): a real card
always has ≥1 idle (0xFF) byte of read-access time between R1 and the
start-of-block token. The game's command sender at $E808 (dumped and
decoded live) polls for R1 then **clocks exactly one extra byte before
RET** ($E82E: IN A,($EB)) — on real hardware that eats a 0xFF gap
byte; on jnext it ate the 0xFE token. The token-wait at $E3E7 then
consumed all 512+CRC bytes of block 0 as "not token" (sector 196895
contains no 0xFE byte), synced on block 1's token, and the entire
stream shifted. Firmware (FatFs rcvr_datablock) polls with no extra
clock, which is why boot/NEX loading never exposed this.

**Fix (2 bytes): insert one kIdle between R1 and 0xFE in both
`cmd17_read_single_block()` and `cmd18_read_multiple_block()`.**
The between-blocks path already emitted a 0xFF filler before each
subsequent token (deliberately, for the token-poll idiom) — only the
first block lacked it.

### Verification

- Post-fix, the retry gate never fires: ZERO writes to $9D38 from
  $32C2 (pre-fix: 6262), no RST 8 NOP-slide, no $1A00 re-entries.
- The game leaves the loader and runs a live main loop from frame 961:
  raster-wait on NR $1F for line $D3, then per-frame logic at $A000
  (input collection: keyboard rows, NR $B0, ports $37/$1F — all read
  clean $00). Screenshots at frames 2500/6000/15000 show the splash
  art with a running CPU (pre-fix: same art with a dead CPU).
- `sdcard_test` 40/40 and `divmmc_test` 146/146 both green with the
  fix — no existing row pinned the zero-gap sequence.

### Oracle status (updated live during the session)

- ZEsarUX 12.0/13.0-B2 headless oracle attempts failed before the game
  could load ("Error initializing SD card!" from tbblue_loader without
  --enable-divmmc-ports; black screen/no NextZXOS menu within ~4 min
  with it). Not pursued further.
- **CSpect (run by the owner, live): the game shows its splash then its
  own "SD CARD CMD ERROR" message** — i.e. the game's raw-SD streamer
  FAILS OUTRIGHT on CSpect (its error UI at $ECxx fires). CSpect is a
  non-oracle for this title; post-fix jnext gets strictly further than
  CSpect (byte-exact stream, no error path).
- Whether the splash should auto-advance (and after how long) is only
  answerable on ZEsarUX-with-working-SD or real hardware — open.

### What remains open

1. The static splash post-fix: expected behavior unknown (see oracle
   status). The game polls input every frame with all inputs reading
   idle, menu-input flag ($F916 b7) clear, no stream request pending
   ($F955/56=0) — consistent with an intro/attract state that may need
   a specific key, a joystick, or simply more patience than 5 emulated
   minutes.
2. The proper fix must land via the standard flow: fresh branch off
   main, the 2-byte sd_card.cpp change, a NEW discriminative test row
   (DIVMMC-SPI plan: "≥1 idle byte between R1 and 0xFE on CMD17/CMD18
   first block", spec § 7.5.2 citation), manifest count bump,
   independent review. The wt-84b copy of the fix is proof-of-mechanism
   only.

## Session 5b (2026-07-24 evening) — post-fix stall at the B/W loading
## screen: fully characterised, root cause NOT yet located

With the Nac-gap fix in, the game loads everything it asks for
(byte-exact) and runs: colour splash (frame ~900), B/W title (~970),
then a deliberate 50 Hz two-buffer alternation (NR $12 flip 1/frame;
odd frames = B/W art, even = red-tinted copy — flicker-blend; the
"corruption" the owner reported is this alternation not fusing at the
emulator's ~49.4 fps). The game stays there forever, input disabled.

Facts established (probes: G46B_E3W in divmmc.cpp write_control,
PHYSWATCH, WRITEWATCH on $F997/$F955/$F90D/$F916, full-frame PCTRACE
at 1040 vs 1200, NR-write histogram diff 1060/1120/1300, DMA log):

- All SD streaming (18 CMD18 streams) completes by frame ~948; the
  loader's bank-cycling port-$E3 writer (pc=$DBDF) parks at $0C via
  pc=$DF4B at frame 941. ZERO port-$E3 writes after that.
- The per-frame sequencer ($CF7F) early-returns unless flag $F997 is
  set; the only game-phase setter is $CFCE, gated on the $E3 readback
  bit 1 CHANGING between per-frame polls ($CFBA: IN A,($E3); AND $02;
  self-modified CP). With $E3 static, $F997 is never set (verified:
  zero game-phase writes) → the whole sequence starves.
- Frame paths at 1040 and 1200 are IDENTICAL (zero PC-set diff) — the
  stall state is entered at ~941, well before the visible change
  (which is a flip-parity inversion, not new execution).
- Eliminated: NMI machinery (copper-driven NR $02=0x04 ~450/frame,
  instant automap at $0066 per NR $BB=0x02, stub PUSH AF/POP AF/RETN
  confirmed both in jnext AND in the real enNxtmmc.rom — the NMI does
  nothing by design); DMA (sprite upload $30E0→port $57 completes;
  burst 1-byte-per-tick is modelled behaviour); 50/60 Hz (game adapts
  to 50); port decodes (NR $82-$85 = $FF, $E3 read claimed); VHDL
  port_e3_dat = static register bits (zxnext.vhd:4190) — no live bits.
- A crude SD-latency experiment (JNEXT_G46B_SD_NAC_DELAY=200 extra
  idle bytes per data token) only shifted the park 941→954; same stall.

Open: on real HW / ZEsarUX (the game's supported emulator) something
must still be writing port $E3 (or otherwise producing the tick) after
the load parks. Next session: obtain the working-ZEsarUX oracle
(owner's setup; headless attempts failed at ZEsarUX SD init), observe
how long the load phase lasts and what $E3 traffic looks like, then
differential against jnext. The G46B_E3W probe is the comparison hook.
