# parallax.nex rendering investigation

**Started**: 2026-04-25
**Last updated**: 2026-04-26 EOD
**Status**: PARTIALLY RESOLVED — visible content significantly improved, but
still NOT visually correct vs CSpect. Remaining gap is in a different
subsystem (LoRes / tilemap / copper-driven layer base re-pointing).
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

### Recommended fix

Patch `Emulator::nex_init_machine` to add the 6 missing NR writes,
in tbblue's order, with VHDL-citation comments. Verify visually:
re-run parallax.nex, capture at 5 s (frame 250); if scene now matches
CSpect-3.png — case closed. If not, drill into the remaining gap.

Pending in a follow-up branch (`fix/nex-init-tbblue-faithful`).

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

## State of tests after this investigation

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
