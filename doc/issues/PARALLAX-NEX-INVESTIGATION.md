# parallax.nex rendering investigation

**Started**: 2026-04-25
**Last updated**: 2026-04-30 EOD
**Status**: **ROOT CAUSE IDENTIFIED** — `Emulator::run_frame` schedules the
line-interrupt event ONCE at frame start; the demo expects ~13 chained line
interrupts per frame via mid-frame `NR 0x23` rewrites. Fix shape designed
(see "Shape B" below); implementation deferred to a follow-on session.
**Driver file**: `../CSpect3_1_0_0/parallax.nex` (also at
`/home/jorgegv/src/spectrum/CSpect3_1_0_0/parallax.nex`).
**CSpect launch script**: `/home/jorgegv/src/spectrum/CSpect3_1_0_0/parallax.sh`
(`mono ./CSpect.exe -fullscreen -sound -w5 -60 -vsync -zxnext -mmc=./ parallax.nex`).
**Current jnext launch**: `./build/jnext --load ../CSpect3_1_0_0/parallax.nex`
(do NOT pass `--boot-rom` / `--divmmc-rom` — they hang the demo).

## Original symptom (2026-04-25)

Severe corruption: scene rendered as **two side-by-side copies of the
scene with a vertical black band in the middle**, ~30 px wide gap.
Captured at multiple time points in `/tmp/parallax-baseline.png`,
`/tmp/parallax-t1.png`, `/tmp/parallax-t3.png`, `/tmp/parallax-t6.png`
during the 2026-04-25 investigation. **No longer reproducible** as of
2026-04-26 — likely fixed incidentally by the contention/videotiming/
ULA video closures earlier in the week.

## 2026-04-26 evidence + fixes

### User-supplied ground-truth (file in repo root)
- `parallax-cspect-{1,2,3}.png` — CSpect at 5.5s / 5.2s / 5.0s
- `parallax-jnext-{1,2,3}.png` — jnext at the same moments

### Subsystems exercised by the demo (verified by trace)
- **NO sprites** initially claimed; later confirmed FALSE — 688 writes
  to port 0x303B, 54,240 to port 0x57, 18,688 to port 0x5B in 4 sec.
  Demo uses sprites HEAVILY.
- Layer 2 enabled via `port 0x123B = 0x02` (visible bit only).
- `NR 0x12 = 0x08`, `NR 0x13 = 0x0c` (L2 active + shadow page).
- Copper program: 6 MOVE instructions writing NR 0x16 at scanlines
  160/162/166/170/176/182 (bottom-band parallax pattern, identical
  shape to Beast).
- `NR 0x69` / `NR 0x6B` / `NR 0x70` / `NR 0x6E` never written —
  defaults rule.
- `NR 0x15` toggles `0x80`/`0x01` only ONCE at init — not per-line as
  the original 2026-04-25 trace claimed (LoRes path is unused at
  runtime; LoRes implementation NOT a parallax blocker).

### Bugs found + fixed in this investigation

1. **`fix(layer2): NR 0x18 clip_y2 default 0xBF`** (commit `4d13d14`)
   Layer-2 clip_y2 default was 255 in jnext, should be 0xBF (191) per
   `cores/zxnext/src/zxnext.vhd:4959-4962`. Fixed in `Layer2::reset()`
   + member-init list. Sibling NR 0x19/0x1A/0x1B clips verified
   already correct.

2. **`feat(sprites): per-scanline attribute replay`** (commit `b0a45a3`)
   The demo bulk-streams sprite-attribute bytes via Z80N DMA mid-frame
   (`A=mem(inc) → B=I/O(fixed at port 0x57)`). VHDL
   `cores/zxnext/src/video/sprites.vhd:327-470` defines 5 dual-port
   attribute RAMs (sync-write, async-read by FSM at scanline render
   time). jnext's renderer ran once per frame, so it saw only the
   end-of-frame snapshot — all 96 sprites clustered at y=160/176.
   Fix: per-scanline change-log replay mirroring the Beast L2-scroll
   fix (`f448b4f`) + palette pattern. `start_frame` /
   `set_current_line` / `rewind_to_baseline` /
   `apply_changes_for_line` API on `SpriteEngine`.

3. **`test(sprites): close per-scanline replay coverage gaps`**
   (commit `9dd5684`)
   Closed the 4 critic nits on `b0a45a3`: PSL-07 byte4/extended-attr,
   PSL-08 NR 0x75-0x79 path, PSL-09 end-to-end render, 8192 cap
   header note.

4. **`feat(sprites): per-scanline pattern replay`** (commit `603cbfc`)
   Demo also bulk-streams sprite-pattern bytes via Z80N DMA mid-frame
   (port 0x5B, 18,688 writes/sec ≈ 92/frame, 256-write peak burst).
   Phase-0 measurement saw 311 distinct scanline values across 4 sec
   trace + 443 distinct (scanline, pattern_index) pairs — clear mid-
   frame multiplexing. VHDL `cores/zxnext/src/video/sprites.vhd:561-
   572` declares the 16 KB `sdpbram_16k_8` pattern RAM with the same
   sync-write/async-read semantics as the attribute RAMs. Fix mirrors
   `b0a45a3` on the pattern side.

### Bugs investigated and ruled out

- **DMA byte-count truncation** (initially claimed by sprite agent;
  debunked by DMA-instrumentation agent). The "DMA transfer complete:
  16 bytes" log line reports per-burst, not total. With `len=80` the
  transfer takes 5 bursts of 16 each; full 80 bytes ARE delivered.
- **NEX bank → SRAM page mapping mismatch for L2 source**. Verified
  correct: NEX banks 8/9/10 → SRAM pages 16-21, NR 0x12=0x08 reads
  pages 16-21. (parallax.nex has banks 8/9/10 all-zero in the file —
  L2 image is populated at runtime via DMA paged copies.)
- **Layer 2 renderer page selection** (NR 0x12 vs NR 0x13). Verified
  VHDL-faithful: NR 0x12 only feeds the renderer; NR 0x13 is for CPU
  paging only.
- **MMU CPU-write/read consistency for L2 source population**. MMU
  consistency agent could not reproduce a divergence; mathematical
  audit shows write/read paths mirror-symmetric.
- **NR 0x18 clip_y2 default** — drift fixed (item #1) but does not
  affect the visible parallax area.

### Remaining gap (NOT fixed)

CSpect ground-truth shows multi-tier rocky platforms (3-4 horizontal
beams), full-height vertical lava columns, and crystal/skull-stone
detail tiles. jnext renders the cave/spire background, the bottom
parallax band, the upper stone-tile platform (after `b0a45a3`), and a
slight pattern-multiplex delta (after `603cbfc`) — but NOT the multi-
tier platforms / lava columns / crystals.

The pattern-replay fix (`603cbfc`) produced only ~213 pixels of
difference at frame 330 vs the attribute-only baseline (and zero diff
at f300/f312). This means the missing content is **NOT** in the
sprite pattern domain.

Subsystems likely responsible for the residual delta (in priority
order):
1. **Layer 2 mid-frame source-bank repointing** — Copper-driven NR
   0x12 mid-frame writes are NOT yet captured by per-scanline replay.
   If the demo points L2 at different banks for different bands of
   the screen, each band would carry different artwork and jnext
   would see only the last bank's content. (Cat-A item in
   `doc/design/PER-SCANLINE-DISPLAY-STATE-AUDIT.md`.)
2. **LoRes layer** — PARKED earlier; per the 2026-04-25 trace LoRes
   bit toggling was claimed but the 2026-04-26 trace shows only ONE
   init-time write to NR 0x15 bit 7. So LoRes is NOT an active driver
   here, but LoRes-related sprite priority bits in NR 0x15 may be
   relevant.
3. **Tilemap pattern multiplexing** — Tilemap is NOT enabled by the
   demo per the trace, so unlikely.
4. **Copper-driven NR re-pointing of layer base addresses** — Copper
   trace shows only NR 0x16 writes; no NR 0x12 / NR 0x13 / port-0x123B
   mid-frame writes. So this candidate is also unlikely unless other
   ports are involved.

The most plausible next step is **Layer 2 per-scanline source-bank
replay** (NR 0x12 + NR 0x13 mid-frame change-log) — but the trace
does NOT currently show such writes from this demo. Either the demo
uses a different multiplex mechanism we haven't identified yet, or
the multi-tier platforms come from a single L2 image with lots of
DMA-driven runtime population that's not landing where we expect.

### Recommendation

Status: ship the four fixes; treat the residual visual gap as a
**follow-on investigation**. The fixes are VHDL-faithful, fully
tested, and benefit other demos (any sprite-multiplexing program).
The residual gap requires fresh diagnosis — likely a new investigation
journal under `doc/issues/`.

### 2026-04-26 late-evening findings (after .nex disassembly + nex_init audit)

End-of-day investigation went two more passes that change the diagnosis.

**Pass A: Disassembly of parallax.nex** (via `z88dk-dis` against the
extracted bytes). PC=0x8000 entry code in NEX bank 2 lo:
```
DI; NEXTREG NR 0xB8/B9/BA/BB; NEXTREG NR 0x50-0x53 (slots 0-3 ← 0x0C-0x0F);
RST 0
```
RST 0 jumps to 0x0000 which is now NEX bank 6 lo (paged in by NR 0x50).
Bank 6 lo at 0x0053: `LD HL, 0x05E2; LD (0x0047), HL` (install IM1
vector); main loop spins waiting for IM1 then does the per-frame
work. **0x0053 is NOT a DivMMC/esxDOS hook** — it's the demo's own
entry. The demo does not require boot/DivMMC ROMs (and explicitly
hangs if those are loaded — likely because of unintended automap-trap
collisions).

**Pass B: byte-level verify of L2 + MMU mapping.** The earlier FNV
mismatch at runtime page 0x24 vs file's NEX bank 2 lo was a **red
herring** — first 8 bytes of page 0x24 = `f3 ed 91 b8 00 ed 91 b9`
EXACTLY matching the demo's init code. The FNV diverged because the
demo's stack lives at SP=0x80a1 (= page 0x24 high offset) and stack
push/pop overwrites some bytes within the same 8 KB page. The LOADER's
byte-level placement is correct everywhere checked. **MMU mapping is
NOT the bug.**

**Pass C: nex_init_machine vs tbblue's official `nexload.asm`.**
Comparison surfaced **6 missing NR writes** in jnext's
`Emulator::nex_init_machine`:

| Register | tbblue value | jnext init | Effect of missing write |
|----------|--------------|------------|--------------------------|
| **NR 0x07** | **0x03 (28 MHz turbo)** | not set (default 3.5 MHz) | Demo runs 8× slower until it sets its own NR 0x07 — **major timing skew** |
| NR 0x06 | peripheral 2 — DivMMC autopage / Multiface / AY config | not set | Different peripheral state |
| NR 0x08 | peripheral 3 — paging lock, contention, Timex, TurboSound | not set | Different peripheral state |
| NR 0x42 | 0x0F (palette format / ULA transparency) | not set | Possibly wrong ULA palette format |
| NR 0x44 | palette extended | not set | Possibly missing extended palette init |
| NR 0x61, 0x62 | 0x00 (stop copper) | not set | Stale copper program could fire before demo programs it |

**Working hypothesis**: the visible delta is **CPU-speed-induced
timing skew**, not a rendering bug. CSpect at 5 s wall-clock has
executed many more demo cycles than jnext at 5 s wall-clock; CSpect
shows the rich game scene while jnext is still in the intro/title
state. Both render correctly given their respective demo states.

**This may also explain NextZXOS boot delays** — same missing init
slows boot ROM execution.

### Recommended fix — TESTED, REVERTED

Tried patching `Emulator::nex_init_machine` with the most critical
missing write: **`NR 0x07 = 0x03` (turbo 28 MHz)**. Result:

- Build green, unit tests 3384/3384/0/0
- jnext correctly logged `CPU speed changed to 28 MHz (NextREG 0x07=0x03)`
- **parallax.nex visual at frame 250: UNCHANGED** vs pre-patch (still
  shows the simple cave + bottom-band scene, NOT CSpect's rich content)
- Beast.nex still renders correctly
- **`tilemap-demo` regression: BLACK SCREEN** post-patch (user-confirmed
  via interactive test). Reverting the `NR 0x07` write restores
  tilemap-demo to its working render

**Two separate findings from this experiment:**

1. **CPU-speed-induced timing skew is NOT the cause of parallax's
   visual gap.** The demo's per-frame state at frame 250 is the same
   whether the CPU runs at 3.5 MHz or 28 MHz — the demo's main loop
   is IM1-frame-driven, not CPU-cycle-driven. The 5 s wall-clock
   difference between CSpect-rich-scene and jnext-intro is therefore
   NOT explained by CPU speed alone.

2. **`NR 0x07 = 0x03` triggers a latent bug** somewhere in jnext
   — most likely the turbo handler interacts with timing-sensitive
   demo code (e.g., a tight scanline-synchronized loop calibrated for
   3.5 MHz that misses its target at 28 MHz), or jnext's effective
   T-state accounting at 28 MHz has a defect. **NEW BACKLOG ITEM** —
   investigate in isolation: build a minimal NEX demo that writes
   `NR 0x07 = 0x03` and observe what visibly fails vs CSpect.

5 of the 6 originally-failing screenshot regressions (palette-demo,
floating-bus, contention-test, dapr-sprite, dapr-tilemap_00) showed
visually-correct renders that align more with CSpect post-patch — those
would have justified rebaselining. But `tilemap-demo` going black is
a hard regression, so the patch is reverted. The rebaseline candidates
are paused until the underlying turbo issue is fixed.

### Open status

The visual gap between CSpect and jnext on `parallax.nex` remains
**unexplained**. All concrete hypotheses tested in this session have
been disconfirmed:

- ❌ MMU bank/page mapping (byte-perfect verified)
- ❌ Sprite anchor/relative chains (demo doesn't use them)
- ❌ L2 image data byte-difference (NEX file has only spire+lava data)
- ❌ Sprite multiplexing across scanlines (attr table identical across
     frame; no mid-frame writes)
- ❌ CPU-speed timing skew (turbo doesn't change per-frame state)
- ❌ **Refresh-rate mismatch (50 vs 60 Hz)** — user verified 2026-04-27:
     running CSpect with `-50` flag (matching jnext's default 50 Hz)
     **still produces the rich scene** (just renders it slower in
     wall-clock terms). jnext at 50 Hz renders the sparse scene. So
     refresh rate is NOT the cause of the visual delta.

### Next-session pickup (planned for 2026-04-27)

User-directed plan, two phases:

**Phase A — Sprite priority / transparency at pixel level.** The 96
sprites at y=112/y=128 DO render in jnext, but maybe their priority
vs L2 / ULA is wrong, hiding additional sprite content that should
appear elsewhere on screen. Investigate: NR 0x4A (sprite transparency
index), NR 0x4C (sprite/tilemap palette select), NR 0x6F (sprite-over-
border), NR 0x68 (ULA blend mode), and the SLU/LSU priority modes via
NR 0x15 bits. Compare jnext's compositor priority handling to VHDL
`zxnext.vhd` carefully. Possibly add per-pixel tracing in the
compositor for parallax frame 250 to identify where layer choices
differ from CSpect's expected output.

**Phase B — Decode bank 6's main loop more thoroughly.** Disassembly
covered the IM1 vector install at 0x0053 and the per-frame loop at
0x0090, but the loop's per-frame work calls (CALL 0x011E, 0x01EF,
0x0213, 0x0269, 0x02BF, 0x00F8, 0x0469) were NOT yet traced. Decode
each: identify which writes the demo does each frame to sprites /
patterns / palette / L2 scroll. Cross-reference against the captured
trace to find any NR or port write jnext mishandles. The Z80N NEXTREG
instruction (`ED 91 nn vv`) is 4 bytes, but z88dk-dis treats it as
2-byte NOP — manual realignment required for any disassembly past an
`ED 91`.

Investigation pause; commit findings; resume next session per Phase A.

### Critic findings on `603cbfc` (independent review, APPROVE-WITH-NITS)

VHDL citations re-verified at `sprites.vhd:561-572` (pattern RAM),
`:744` (port-0x5B write enable), `:728-743` (auto-increment),
`:962, 967-971` (FSM read). Pattern fidelity vs `b0a45a3` confirmed.
Vblank catch-up logic sound (no double-application). Sizing 8192
adequate (16384 = pathological worst case, documented as known limit).
Save-state correctly omits ephemeral fields. No regression risk.

Three non-blocking nits to track:

1. **Latent vblank-catch-up bug on the attribute side** —
   `b0a45a3`'s `start_frame()` does NOT flush attribute-log entries
   tagged at line >= 256 the way `603cbfc` does for pattern. Demos
   that DMA-stream attributes across the visible/blanking boundary
   would silently lose the late bytes. In-code comment in
   `sprites.cpp` `start_frame()` flags this. Promote the catch-up
   when a demo surfaces it.
2. **Missing overflow-clear-on-`start_frame` test** — PSL-PAT-04
   covers reset clearing the overflow flag, but no test directly
   exercises `pattern_overflow_warned_` clearing across a normal
   `start_frame()` call.
3. **No save-state version byte** — pre-existing limitation, NOT
   introduced by this commit. As the per-scanline architecture grows
   (palette + layer2 + sprite-attr + sprite-pattern), a version byte
   protecting against silently-wrong-shape loads is increasingly
   justified. Already in the 86-gap doc as G66.

## 2026-04-30 session — ROOT CAUSE IDENTIFIED

Five reviewer-approved architectural fixes landed today; **none of them
moved parallax** (frame-250 screenshot MD5 byte-identical pre/post each).
Each fix was real (each had a diagnostic test verified to fail without
the fix), but none was the parallax bug. Phase B disassembly of bank 6
then identified the actual bug.

### Today's landed fixes — all VHDL-faithful, none parallax-relevant

| Commit | Fix | Diagnostic | Did it move parallax? |
|---|---|---|---|
| `45863fd` | NR 0x68 bit-3 read composes live `port_ff3b_ulap_en` per VHDL `:6093` (was returning cached snapshot) | 4 ULAP-* tests; 2 verified to fail without fix | No |
| `8dc8df5` | Sprite pattern change-log cap `8192 → 81920` (one entry per visible pixel). Pre-fix, parallax overflowed at scanline 188 and silently dropped post-188 pattern writes from per-scanline replay | PSL-PAT-09 added (8200-write parallax-class density); fails under old cap | No |
| `341f011` | `--compositor-trace FILE [--compositor-trace-frame N]` CLI flag — debug tool that dumps one CSV row per pixel for a target frame | n/a — instrumentation | n/a |
| `5745801` | G117 cycle-accurate Copper scheduler (per-cycle iteration in `tick_copper_for_master_cycles`) + G65 CPU-wins-tied-edge defer (CPU NR writes deferred to instruction-end) | G117-MPC-01 (16 MOVEs in 3 instructions) + G65-PRI-01 (Copper writes `0x55`, CPU writes `0xAA` same instruction → `0xAA` wins). Both verified to fail without their fix | No |

So the gap-doc's hypothesis that **G117 was a parallax contributor** was
wrong for this demo. parallax's Copper program is only 6 MOVEs/frame
across 6 scanlines (not dense enough to surface G117's gap). Trust the
trace, not the documented hypothesis.

### Phase A — compositor exonerated

Per-pixel compositor trace at frame 250 via the new `--compositor-trace`
flag. 81920 pixels analysed; the compositor's chosen pixel matches the
SLU priority chain on every single pixel (100% internally consistent —
zero phantom transparency, zero priority misroutings).

Pixel distribution at frame 250:
- 69% pixels = `0xff000000` (black) — the dominant result colour
- 56% L2 opaque, 9% sprite opaque (rows 128-159), TM unused
- 0% `l2_prio` branch (parallax never sets per-pixel L2-priority bit)

**The 69% black is L2 *sampling* black bytes from its source banks —
not a compositor decision.** That's the strongest remaining signal.

NR audit covered five candidates (`0x4A`, `0x4C`, `0x6F`, `0x68`,
`0x15`) against VHDL. Only deviation found: NR 0x68 bit-3 read
(landed as `45863fd` above). All other NR handlers byte-exact with
VHDL for the values parallax writes — including all 6 SLU/LSU/SUL/LUS/
USL/ULS layer orderings and the 2 blend modes.

### Phase B — bank-6 disassembly via z88dk-dis -mz80n

User added the critical tooling tip: **`z88dk-dis -mz80n`** correctly
decodes Z80N opcodes (NEXTREG, SWAPNIB, etc.) without manual
realignment after every `ED 91`. Default `-mz80` silently misaligns.

Bank 6 is at NEX file offset `0x18200` (mapped to slots 0+1 at
runtime). Extracted:

```bash
dd if=test/00regression/nex/parallax.nex of=/tmp/parallax-investigation/bank6.bin \
   bs=1 skip=$((0x18200)) count=16384
z88dk-dis -mz80n -o 0x0000 -s 0xADDR -e 0xEND /tmp/parallax-investigation/bank6.bin
```

#### Init code (PC=0x8000 → bank 6 paged in → 0x0053)

```
0x0053  install IM1 vector at 0x0047 → handler at 0x05E2
0x0059  NEXTREG 0x22, 0x06        ; line interrupt control
0x005D  NEXTREG 0x23, 0xC6        ; line interrupt at line 198
0x0061  NEXTREG 0x14, 0xE7        ; global transparency (NOT 0xE3 default)
0x0065  NEXTREG 0x07, 0x03        ; CPU 28 MHz turbo
0x0069  NEXTREG 0x08, 0x40        ; port enable
0x006D  NEXTREG 0x12, 0x08        ; L2 active = bank 8 (pages 16..21)
0x0071  CALL 0x0193                ; not yet decoded
0x0076  OUT (0xFE), 0              ; border = black
0x0078  CALL 0x037B                ; not yet decoded
0x007B  NEXTREG 0x15, 0x01        ; SLU + sprite enable
0x007F  NEXTREG 0x19, 0x08        ; L2 clip x1 = 8     ┐
0x0083  NEXTREG 0x19, 0xF7        ; L2 clip x2 = 247  │ NR 0x19 cycles
0x0087  NEXTREG 0x19, 0x00        ; L2 clip y1 = 0    │ x1/x2/y1/y2
0x008B  NEXTREG 0x19, 0xC0        ; L2 clip y2 = 192  ┘
0x008F  EI
0x0090  main loop: wait for IRQ flag, dispatch CALLs
```

#### Per-frame work calls (from main loop at 0x0090)

- **`CALL 0x011E`** — animation-state tween table walk at 0x0143
  (8-entry × 4-byte table at 0x016A). Updates 5 bytes inside the
  Copper instruction template at 0x0794 / 0x0798 / 0x079C / 0x07A0 /
  0x07A4. RAM-only, no NR/port writes.
- **`CALL 0x01EF`** — increments scratch counter; calls `0x04CD`
  (Copper instruction uploader: NR 0x60 ×24 bytes, mode 0 stop, then
  mode 11 restart). The 24-byte template at 0x078F is:
  ```
  16 00 80 A0   MOVE NR 0x16, 0     WAIT vpos=160
  16 00 80 A2   MOVE NR 0x16, X1    WAIT vpos=162
  16 00 80 A6   MOVE NR 0x16, X2    WAIT vpos=166
  16 00 80 AA   MOVE NR 0x16, X3    WAIT vpos=170
  16 00 80 B0   MOVE NR 0x16, X4    WAIT vpos=176
  16 00 81 90   MOVE NR 0x16, X5    WAIT vpos=400 (HALT)
  ```
  Where X1..X5 are tweened values from 0x011E. This is the
  **bottom-band parallax** (lines 160-176 only) — the 6-MOVE Copper
  pattern noted in the 2026-04-26 trace.
- **`CALL 0x0213`** — pages NR 0x57 = `0x21` or `0x22` into slot 7,
  pages NR 0x56 = `0x25` into slot 6, then strided memcpy from
  `0xC0XX` (slot 6) → `0xE003+offset` (slot 7) with 16-byte chunks
  spaced by 5 bytes per byte, repeated 12 times per call. Writes
  pixel data into **alternate L2 source banks** (NOT the active
  bank 8 from NR 0x12).
- **`CALL 0x0269`** — analogous to 0x0213, but pages `0x1D` / `0x1E`
  into slot 7 with counter at `(0x0267)`, page `0x27` into slot 6.
  Different alternate-bank target.
- **`CALL 0x02BF` / `0x00F8` / `0x0469`** — not yet decoded but
  follow the same idiom (per the prompt's prior decode notes).
- **`CALL 0x0193` / `0x037B`** (init only) — not yet decoded.

#### IRQ chain (the smoking gun)

The IM1 handler at `0x05E2` runs at line 198 (vsync IRQ). It:

1. Sets a flag at `(0x80AC)` to wake the main loop (which then runs
   the per-frame work at 0x009A onward).
2. **Writes `NEXTREG 0x23, 0xF4`** — re-arms line interrupt at
   line **244** (this frame).
3. Self-modifies the IM1 vector at `0x0047` → next IRQ goes to a
   different handler at `0x062E`.
4. Returns via `JP 0x003D`.

The handler at `0x062E` (called at line 244) does the actual
mid-frame L2 source-bank update. Per fire it:

1. Saves current `NR 0x57`.
2. Writes a new bank into `NR 0x57`, `CALL 0x0543` (DMA / memcpy with
   stride 5, 12 × 16 = 192 bytes per region).
3. Repeats steps 1-2 for **3 different slot-7 pages** (3 alternate
   banks updated per IRQ fire).
4. Restores old `NR 0x57`.
5. **Adds `0x10` to the current line-int target, writes back to
   NEXTREG 0x23** — re-arms next line interrupt.
6. If the new target reaches `0xB4` (180), switch back to the vsync
   handler (`0x05E2`) and write `NEXTREG 0x23, 0xC0`.

**Lines fired in order per frame:** 198, 244, 4, 20, 36, 52, 68, 84,
100, 116, 132, 148, 164. Then 180 stops the chain. Note `244 → 4`
wraps via 8-bit `ADD 0x10` overflow — line 4 is in the *next* frame.

### What jnext does wrong

[`emulator.cpp:2991-3016`](../../src/core/emulator.cpp#L2991-L3016)
schedules the line-interrupt event ONCE per frame at frame start in
`run_frame`, based on the line-int target valid at that moment.

[`emulator.cpp:864-868`](../../src/core/emulator.cpp#L864-L868) — the
NR 0x23 write handler updates `video_timing_.line_interrupt_target()`
but **does not re-schedule** for the current frame. The new target
only takes effect at the NEXT frame's start.

VHDL [`zxula_timing.vhd:577`] fires line-int every cycle when
`(hc==255 AND cvc==int_line_num)` — fully dynamic. The target is
read live; mid-frame writes immediately re-target.

So jnext fires the FIRST line-int per frame (line 198), the IRQ
handler runs and writes `NR 0x23 = 0xF4` to chain, but jnext
silently swallows the chain — the next ~12 IRQs the demo expects
never fire. Handler `0x062E` runs **0 times per frame** in jnext, so
banks `0x1D / 0x1E / 0x21 / 0x22` never get their per-frame
DMA-paged pixel data → L2 reads zero bytes from those banks → 69%
black at frame 250. **This explains the dominant signal in the
compositor trace.**

### Fix shape — Shape B (recommended)

No scheduler API change required.

1. Track `pending_line_int_target_at_schedule_time_` on `Emulator`
   alongside the schedule call.
2. When NR 0x22 or NR 0x23 is written: if
   `video_timing_.line_interrupt_enable()` is true, schedule a NEW
   `EventType::CPU_INT` for the new target's
   `frame_cycle + master_cycle_offset`. If the new target's offset
   has already passed in the current frame (parallax's `0xF4 + 0x10
   = 0x04` 8-bit-wrap case), schedule for `next_frame_cycle +
   offset`.
3. The fired event's callback checks
   `pending_line_int_target_at_schedule_time_ ==
   video_timing_.line_interrupt_target()` — if changed, no-op (the
   previously-scheduled event was superseded by the rewrite). This
   avoids needing a scheduler `cancel()` API.

**Edge cases:** new target with no enable bit → no new schedule;
multiple writes per instruction → each schedules, all but last become
no-ops; same target re-written → still re-schedule (idempotent).

### Test gap

- `videotiming_test` (27 rows) — VT-22..VT-26 cover the line-int
  target → master-cycle offset math (G106/G107/G109/G71). **None
  exercise mid-frame retarget.** All are frame-start-only.
- `nextreg_test` G56-CR-22 / G56-CR-23 skips are about NR 0x22/0x23
  *read* composition (different concern from scheduling on *write*).
- **No entry in `KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md`** matching this.
  G106 was closed for the math fix but the dynamic case was never
  gap-tracked.

**Proposed:** add new gap entry **G163 — line interrupt schedule does
not re-evaluate on mid-frame NR 0x22/0x23 writes**. Add 2-3 new VT-*
rows: (a) write NR 0x23 mid-frame, expect new line-int fires same
frame; (b) write a target that's already passed → expect fire at
next frame's line N; (c) write the disable bit → no new schedule.

### Verification protocol after the fix lands

1. Build, run unit-test (expect 3734/3550/0/184 → +2-3 from new VT
   rows).
2. Run regression (expect 32/0/0 — copper-demo, beast.nex, dapr-*
   should be stable; line-int fixes affect frequency, not semantics).
3. Compare parallax frame-250 screenshot MD5 vs the cached
   `/tmp/parallax-investigation/parallax-frame250.png` baseline.
   **Expected to differ this time** — that's the success criterion.
4. Per-frame `--compositor-trace` re-run: top result-colour
   distribution should shift away from 69% black toward more diverse
   colours.

### Tooling artefacts cached for follow-on work

- `/tmp/parallax-investigation/parallax-frame250.csv` — 81920-pixel
  CSV trace at current main HEAD.
- `/tmp/parallax-investigation/parallax-frame250.png` — pre-fix
  baseline screenshot.
- `/tmp/parallax-investigation/parallax-postg117.png` /
  `parallax-postg65.png` / `parallax-postcap.png` — all
  MD5-identical to baseline (each fix verified non-impacting).
- `/tmp/parallax-investigation/bank6.bin` — extracted bank 6 binary.
- `/tmp/parallax-investigation/analyze.py` — CSV analyser
  (per-row branch + transparency aggregates).

### Companion memory

- `~/.claude/projects/-home-jorgegv-src-spectrum-jnext/memory/project_parallax_line_int_root_cause.md` — full root-cause analysis with line refs, fix sketch, and edge-case notes (mirrors this section but in the memory store).
- `~/.claude/projects/-home-jorgegv-src-spectrum-jnext/memory/reference_z88dk_dis_z80n.md` — tooling tip.

## State of tests after this investigation

### As of 2026-04-30 EOD

- Unit: **3734/3550/0/184** (33 suites, all green); 6 new test rows
  added today (4 ULAP-* in `nextreg_integration_test`, 1 PSL-PAT-09
  in `sprites_test`, 2 in new `copper_integration_test` —
  G117-MPC-01 + G65-PRI-01).
- Regression: **32/0/0** — zero rebaselines required for any of
  today's 5 fixes.
- parallax frame 250 MD5: byte-identical pre/post each of today's
  fixes (intentional — each fix is real but orthogonal to parallax;
  the parallax bug is the line-int-chain root cause identified in
  Phase B above and not yet implemented).

### As of 2026-04-30 EOD continuation — G163 LANDED, parallax now scrolls

- Unit: **3738/3554/0/184** (+4 from G163 VT-* rows).
- Regression: **32/0/0** (zero rebaseline).
- Main HEAD: `db908a1` (merge); branch chain `1bfd32d` →
  `22a5d52` → `cfa5219` → `aa0ff49` (4 atomic commits, single
  rework for NR 0xC4 wiring).
- **Visual outcome (GUI, interactive)**: top + bottom parallax
  strips now scroll smoothly with some artifacts; **middle band
  remains static**. This is the breakthrough Shape B was designed
  to deliver.
- Independent reviewer APPROVE after one rework. Strict-binary
  rule held.

## 2026-04-30 — G163 fix landed + Phase B disassembly complete

### Shape B implementation

`Emulator::reschedule_line_interrupt()` helper added (`src/core/emulator.cpp` + `.h`):

1. Returns early if `video_timing_.line_interrupt_enable()` is false.
2. Computes `line_offset = video_timing_.line_int_master_cycle_offset()`.
3. Range-guards `line_offset >= timing_.master_cycles_per_frame`.
4. Computes `fire_cycle = frame_cycle_ + line_offset`.
5. Past-this-frame roll-forward: `if fire_cycle <= clock_.get()` →
   `fire_cycle += timing_.master_cycles_per_frame`. Handles
   parallax's 8-bit `ADD 0x10` overflow (line 244 → line 4 next frame).
6. Bumps a **generation counter** `++line_int_schedule_gen_`.
7. Schedules a `CPU_INT` lambda that checks the captured gen at
   fire time; superseded events no-op without raising the IRQ.

Wired from FOUR call sites:
- NR 0x22 write (`emulator.cpp:864`).
- NR 0x23 write (`emulator.cpp:875`).
- NR 0xC4 write (`emulator.cpp:1248`) — bit-1 mirror of NR 0x22 bit 1
  per VHDL `zxnext.vhd:5607-5610` and `:6752` (caught by reviewer).
- `run_frame()` frame-start (replaces the prior inline schedule).

Generation-counter chosen over the doc'd target-comparison: it's a
strict superset that handles the rare "rewrite same target value"
case (each new schedule supersedes ALL prior pending events for that
device).

### Test coverage — VT-G163-* rows in videotiming_test Section 8

| Row | Stimulus | Expected | Diagnostic-verified |
|---|---|---|---|
| MIDRETARGET-01 | NR 0x22 enable + target=200; mid-frame NR 0x23 to still-future line | 2 fires same frame | yes |
| WRAP-02 | enable + target=200; mid-frame NR 0x23 to already-passed line | 1 fire this frame, +1 next frame | yes |
| DISABLE-03 | enable + target=200; mid-frame NR 0x22 bit 1 = 0 | 0 fires | yes |
| C4-DISABLE-04 | enable + target=200; mid-frame NR 0xC4 bit 1 = 0 | 0 fires | yes |

Each row was verified to FAIL without the corresponding wiring (commenting out the helper's call site, observing the test row fail, restoring). The VHDL traceability anchors each row to `zxula_timing.vhd:577` (fire predicate), `:566-570` (target-cvc map), and (for C4-04) `zxnext.vhd:5607-5610` / `:6752` (FF mirror).

### Phase B disassembly — all 9 untraced CALLs decoded

Per-frame CALL map (parallax bank 6, mapped to slots 0+1):

| CALL | Purpose | Banks/ports |
|---|---|---|
| `0x011E` | Tween table walk (8 entries × 4 bytes at 0x016A) → writes deltas into Copper program data slots `0x0794-0x07A4` | RAM |
| `0x01EF` | Increments counter 0x0209; uploads 24-byte Copper program at 0x078F via NR 0x60; NR 0x61=0; NR 0x62=0xC0 (mode-11 restart) | NR 0x60/0x61/0x62 |
| `0x0213` | Stride-5 memcpy 16 × 12 into slot 7 with NR 0x57 = 0x21/0x22 | banks 0x21/0x22 (BOTTOM strip) |
| `0x0269` | Analogous to 0x0213 with NR 0x57 = 0x1D/0x1E and counter at (0x0267) | banks 0x1D/0x1E (TOP strip) |
| **`0x02BF`** | Stride-5 memcpy 16 × 12 into slot 7 with NR 0x57 = 0x19/0x1A; counter at (0x02BD); NR 0x56 = 0x29 (slot-6 source) | **banks 0x19/0x1A — likely middle** |
| **`0x00F8`** | Throttled (every 2 frames): cycles counter (0x00F7) mod 8; NR 0x57 = 0x18; CALLs 0x056F memcpy from DE = 0x0127 | **bank 0x18 — likely middle init/refresh** |
| `0x0469` | Keyboard scan: ports 0xFEFE rows; unpacks to bytes at 0x049D | port 0xFEFE |
| `0x037B` | `OUT (0x123B), 0x02` — Layer 2 enable | port 0x123B |
| `0x0193` (init) | NR 0x16/0x17 = 0; NR 0x1C = 0x0F; NR 0x18 × 4 (L2 clip 0x08/0xF7/0x00/0xBF); NR 0x19 × 4 (sprite clip 0x08/0xF7/0x00/0xC0); NR 0x15 = 0x80; CALLs 0x06B8 with HL = 0/1, A = 0x10/0x16 (L2 image-load); NR 0x56 = 0x16 | many |

The chained line-IRQ handler 0x062E (which Shape B unblocked) drives banks **0x21/0x22 + 0x1D/0x1E** → matches the user-confirmed "top + bottom strips scrolling".

The per-vsync main-loop CALLs `0x02BF` + `0x00F8` drive banks **0x18 + 0x19 + 0x1A** — these were ALREADY running pre-fix and still run post-fix, so the "middle stays static" symptom is **downstream of those CALLs**, not in the chain.

### Open leads — investigated 2026-04-30 EOD continuation

#### Point 3 — Copper mid-frame NR 0x12 writes — **RULED OUT**

Copper program at 0x078F decoded (24 bytes = 12 instructions, 6 MOVE + 6 WAIT pairs):

```
[MOVE NR 0x16 = 0x00] [WAIT line=0xa0]
[MOVE NR 0x16 = 0x00] [WAIT line=0xa2]
[MOVE NR 0x16 = 0x00] [WAIT line=0xa6]
[MOVE NR 0x16 = 0x00] [WAIT line=0xaa]
[MOVE NR 0x16 = 0x00] [WAIT line=0xb0]
[MOVE NR 0x16 = 0x00] [WAIT line=0x90]
```

Copper writes NR 0x16 (L2 X-scroll LSB) = 0 at six scanlines. **No NR 0x12 writes anywhere in the Copper program.** The L2 active page (NR 0x12 = 0x08) is set ONCE at startup and never switched mid-frame. Per-scanline NR 0x12 handling is irrelevant to parallax.

The per-frame `CALL 0x011E` tween writes deltas into the WAIT operands at offsets 0x0794 / 0x0798 / 0x079C / 0x07A0 / 0x07A4 — animating which scanlines the L2 X-scroll is reset, which is how the parallax scroll-strip boundaries move from frame to frame.

#### Point 2 — sprite vs L2 attribution for the strips — **CLARIFIED**

Audit of all NR writes in bank 6 (the entry bank):
- NR 0x12 (L2 active page) = 0x08, written ONCE at 0x006D.
- NR 0x14 (global transparency) = 0xE7, written ONCE at 0x0061.
- NR 0x15 = 0x80 inside CALL 0x0193 (LoRes-en + sprites-disabled), then NR 0x15 = 0x01 at 0x007B (sprites-en, layer-priority=SLU). Final value 0x01.
- **No sprite-related NR writes in bank 6** (NR 0x4A / 0x4B / 0x4C / 0x4D / 0x6E / 0x69 all unmodified). Sprite transparency / palette / pattern bank use defaults.
- No mid-frame writes to NR 0x12 / NR 0x14.

User direct confirmation (interactive GUI): cave background + lava bands + small corner details visible both pre- and post-fix; the post-fix improvement is the **scrolling animation** of the top + bottom regions, not new visual content.

So:
- Cave background = ULA (rendered both pre/post).
- Top + bottom strips = L2 layer (banks 0x1D/0x1E + 0x21/0x22, populated by chain handler 0x062E — this is what Shape B unblocked).
- Middle band = ULA cave (intended static).
- **Game-element content visible in CSpect but missing in jnext** (platforms, lava pillars, characters) = likely **sprites**, but their absence is unrelated to G163 (was missing pre-fix too).

#### Point 1 — NR 0x57 paging audit — **MATH CONSISTENT**

Walked through the SRAM mapping for the chain-handler banks:
- `Mmu::to_sram_page(0x21)` = 0x21 + 0x20 = 0x41 in Next mode. SRAM byte = 0x41 × 8192 = 0x82000.
- `Mmu::to_sram_page(0x19)` = 0x39. SRAM byte = 0x72000.
- L2 active bank = 0x08 reads via `compute_ram_addr` with +16 shift in 16K-bank → physical bank 24..28 → SRAM bytes 0x60000..0x74000.

L2 active region (0x60000..0x74000) is **disjoint** from the chain-handler write region (0x72000+ for bank 0x19, 0x82000+ for bank 0x21). The math is consistent — chain writes don't overlap L2 active reads.

So **the missing-game-content delta is NOT explained by NR 0x57 paging stomping on L2**. Likely candidates:
- The demo's missing game elements are **sprite patterns** uploaded via dedicated sprite-pattern ports (0x303B / 0x315B), not via NR 0x57 at all. Need to instrument those ports.
- Or the demo uses **per-frame tilemap updates** that jnext doesn't replicate.
- Or **DMA stride-5** memcpy via `CALL 0x0543` has a jnext-DMA-emulation gap.

This is now a separate investigation from the line-int chain. Suggest a follow-on session that:
1. Instruments port 0x303B / 0x315B sprite-pattern uploads — log all writes during a 5-second parallax run; compare to CSpect-equivalent expected sequence.
2. Dumps SRAM at L2 active bank 0x08 (physical 0x60000..0x74000) at frame 250 in both pre-fix and post-fix runs — verify content matches between runs (this isolates "did chain handler corrupt the active bank").
3. Inspects the `CALL 0x0543` (DMA stride-5 memcpy) — disassemble + verify each underlying DMA operation lands at the expected destination.

### As of 2026-04-26 EOD (historical baseline)

- Unit: 3384/3384/0/0 (32 suites, ZERO skips); +48 new test rows
  (G16 PSL-07/08/09, G17 PSL-PAT-01..07).
- Regression: 34/0/0; one screenshot rebaselined
  (`test/img/dapr-sprite-reference.png`) for VHDL-faithful timing
  shift on `b0a45a3` — justified per critic.
- Beast.nex regression: full forest scene renders perfectly post-fix.

## Companion docs
- `doc/design/PER-SCANLINE-DISPLAY-STATE-AUDIT.md` — Cat-A list of
  remaining per-scanline replay candidates (NR 0x12/0x13 L2 bank,
  NR 0x14 transparency, NR 0x15 sprite/LoRes priority, NR 0x18-0x1B
  clip windows, NR 0x70 L2 mode, port 0xFF Timex screen, NR 0x26/0x27
  ULA scroll, NR 0x68 ULA blend, NR 0x44/0x4B/0x4C transparency
  index).
- `~/.claude/projects/-home-jorgegv-src-spectrum-jnext/memory/project_per_scanline_pattern_reusable.md`
  — canonical pattern shape for future per-scanline change-log work.
- `doc/issues/BEAST-NEX-INVESTIGATION.md` — companion investigation
  (RESOLVED).
