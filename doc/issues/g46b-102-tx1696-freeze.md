# G46(b) #102 — TX-1696 display freeze shortly after PLAY (CPU alive)

Status: **root cause NOT closed** — solid, reproducible characterisation;
no VHDL-cited jnext divergence found yet. No fix applied (per this task's
mandate: a speculative fix is worse than none).

## Symptom (from the issue)

Launch `games/Next/TX-1696/main.nex` from the NextZXOS Browser, PLAY,
weapon-select screen, gameplay starts — the display then freezes solid a
few seconds in while the CPU keeps running (PC alive, IM 2, SD reads
advancing). Reported bit-identical screenshots minutes apart.

## Repro (fully reproduced, deterministic)

Two methodological fixes were required to get a *deterministic* repro —
without them, repeated runs of "the same" script diverged in ways that
first looked like the bug itself:

1. **`--rtc "2026-01-01 12:00:00"`** — jnext's I2C RTC
   (`src/peripheral/i2c.cpp:120`, `std::time(nullptr)`) reads the *host*
   wall clock when not pinned. Two runs of an identical key script take
   different real wall-clock time to reach the same emulated frame
   (headless throughput varies with host load), so an unpinned RTC makes
   the game see a different clock value at the same frame in different
   runs — and TX-1696 (or NextZXOS around it) is clock-sensitive enough
   that this alone changed the observed gameplay trajectory between runs.
2. **`--sdcard-readonly`** — jnext opens the SD image read-write and
   *persists guest writes*. Reusing the same on-disk image copy across
   sequential manual runs (as this session initially did) means each run
   starts from a *different* SD state than the last. Switched to
   `--sdcard-readonly` directly against the canonical
   `~/.jnext/sdcard/cspect-next-1gb-fixed.img` — no per-run copy needed,
   and every run starts from byte-identical SD state. (This does make
   jnext exit 1 when the game's incidental writes are rejected — expected,
   not a bug; screenshot/trace files are still written correctly before
   exit.)

With both fixed, the repro is **100% deterministic**: same key script ⇒
same freeze, same frame, same final register state, every time.

Repro scripts (this branch, not previously in the repo):

- `tools/g46b_102_repro.sh` — full browser nav (space/enter/down×N per the
  issue's script) + a "blanket" of `space` keypresses every 100 frames
  from 2000–14000 (needed — see below) + configurable screenshot/exit
  frame.
- `tools/g46b_102_repro_sparse.sh` — same nav, but only the *exact* two
  keypresses from the issue text (`space@7200` PLAY, `space@9700` weapon
  screen). **Never reaches gameplay** in this session's nav timing — stays
  on the weapon-select screen forever (confirmed static 10000→25000).
  The blanket is not a bug-inducing artifact; it is compensating for the
  weapon-select-screen confirm apparently needing more than one isolated
  keypress to register (this session's down/enter cadence through the
  Browser differs from the issue's, shifting all downstream timings).
- `tools/g46b_102_repro_spamend.sh` — blanket ends at a caller-supplied
  frame instead of always 14000. Used to find the actual freeze frame.
- `tools/g46b_102_repro_stride.sh` — blanket keys at a caller-supplied
  start/end/**stride** instead of the fixed every-100-frames. Used to test
  whether the freeze frame moves with a differently-timed key schedule
  (game-state-driven) or stays pinned to one absolute frame
  (script/timing artifact).

## Probes added (env-gated, zero cost when unset)

- `src/platform/headless_app.cpp` — `JNEXT_G46B_PCTRACE=<path>`
  [`_START=N` `_FRAMES=N`]: one CSV line per **frame boundary**
  (PC/opcode/AF/BC/DE/HL/SP/IX/IY/halted/IM/IFF1 **plus an FNV-1a hash of
  the actual rendered framebuffer**, forcing a real `render_frame()` call
  every probed frame — headless normally skips rendering most frames for
  performance, so a naive `get_framebuffer()` read is stale outside a
  screenshot's own frame; see "false leads" below).
- `src/platform/headless_app.cpp` / `src/cpu/z80_cpu.{h,cpp}` —
  `JNEXT_G46B_ITRACE=<path>` [`_START=N` `_FRAMES=N`]: one CSV line per
  **instruction** (tstates/PC/opcode bytes/AF/BC/DE/HL/SP/IX/IY/halted/
  IFF1) via `Z80Cpu::set_g46b_itrace(FILE*)`, flipped on/off by
  `headless_app.cpp` only for the requested frame window (per-instruction
  volume is far too high to log unconditionally).

Both probes are a single pointer/bool check per call site when unset —
same cost class as the existing `JNEXT_G46B_AUTOMAP_3DXX_TRACE` pattern in
`src/peripheral/divmmc.cpp`.

## False leads ruled out (worth recording — each looked like "the bug" at first)

1. **Stale framebuffer read.** `Emulator::run_frame()`
   (`src/core/emulator.cpp:7312`) only calls `renderer_.render_frame()`
   when `render_enabled_ || video_recorder_.is_recording() ||
   debug_state_.active()`. `render_enabled_` **defaults true** and
   headless never touches it, so in practice this gate was not itself the
   issue — but it means naively hashing `get_framebuffer()` once per frame
   without forcing a fresh render is only valid for frames the frontend
   actually asked to see rendered. First attempt at a per-frame hash
   showed the SAME hash for 12000 straight frames (18000–30000) purely
   because of a **different** mistake (see #2), which briefly looked like
   "already frozen at 18000" until the render-forcing fix (above) and a
   narrower, earlier window (below) placed the real onset at frame 3712.
2. **Non-determinism (RTC + mutated SD image), see repro section above** —
   this is what actually produced the misleading "frozen by 18000, still
   frozen at 60000" *and* a contradicting "still moving at 30000" result
   from consecutive runs before the fix.
3. **Test-harness key-spam as the cause.** The working repro presses
   `space` every 100 frames from 2000–14000. Since the freeze in the first
   deterministic run landed at frame 3712 — very close to one of those
   spam presses (frame 3700) — the leading suspicion was that repeatedly
   mashing `space` (likely the in-game fire key) was itself corrupting
   something, i.e. a harness artifact rather than an organic bug. **Ruled
   out on two counts:**
   - Ending the spam blanket at frame 3600 (skipping the 3700 press
     entirely) — the game kept animating cleanly through frame 3895+ (the
     end of that trace window), i.e. **no freeze** without the 3700 press.
   - Re-running with the blanket on a completely different schedule
     (`stride=137` instead of `100`, so no press lands anywhere near
     3700) — the game **still froze**, this time at frame 3957 instead of
     3712, with slightly different but structurally identical final
     register state (same PC=`0x89d1`/opcode `C9`=RET fixed point).

   Together these show the freeze is **triggered by a `space` press
   landing at a specific point in the game's own state** (not by a fixed
   frame count, and not by the mere fact of repeated spamming) — i.e. it
   looks like a genuine, state-dependent condition a real player could
   also hit by firing at the wrong moment, not a jnext-headless-only
   artifact of this test driver.

## Findings: the freeze itself

- **First divergence (from "alive" to "frozen"), pinned exactly**: in the
  `spamend=6000` (stride 100) run, `JNEXT_G46B_PCTRACE` shows the
  per-frame framebuffer hash changing on every sampled frame from 1400
  through 3711, then **becoming and staying bit-identical from frame 3712
  onward** (confirmed persisting through frame 60000 — 56000+ frames,
  ~18 real minutes of emulated time, completely static).
- At the freeze, **every** CPU-visible quantity sampled at the frame
  boundary is fixed forever: PC=`0x89d1` (opcode `C9`=RET), AF/BC/DE/HL/
  SP/IX/IY all constant, IM=2, IFF1=1, halted=0.
- `0x89d1` (`RET`) is **not itself anomalous** — it is the address the CPU
  is parked at, at the frame boundary, during *ordinary, still-animating*
  gameplay too (visible throughout the "alive" portion of the trace). It
  looks like the landing point of the game's per-frame "HALT, service
  interrupt, RET back out" main-loop convergence. What changes at the
  freeze is that the **entire register state reached at that landing
  point becomes identical every single frame**, not just the address.
- IX/IY (`cf3f`/`9857` in the frame-3712-onward trace) look like
  entity/list-walker pointers — they are already at their final frozen
  value one frame *before* full freeze (frame 3707), while other
  registers are still changing, consistent with some per-frame
  object/sprite processing settling into a fixed path just before the
  whole frame becomes reproducible byte-for-byte.
- The frozen screenshot always shows: ship, one bullet in flight, 5 gold
  coins, and a red "5" HUD digit — same picture in every independent run
  (different absolute freeze frame, same on-screen game state), which is
  itself circumstantial support for a state threshold around "5" (coins?
  ammo? a shared counter) rather than an arbitrary frame count.
- Per-instruction trace across the transition (`JNEXT_G46B_ITRACE`,
  512904 instructions over 15 frames) shows extremely dense Z80N
  `NEXTREG` traffic throughout (`ED 91`/`ED 92`, ~4300/frame) — mostly NR
  `0x38` ("Sprite Attribute 3") writes, consistent with ordinary
  per-frame sprite-attribute updates, not an obviously anomalous access
  pattern by itself. The previously-fixed "`NEXTREG` opcode clobbers the
  `$243B` select latch" defect class (2026-07-22 EOD, #50/#54) is
  confirmed **not** reintroduced — `src/cpu/z80n_ext.cpp:480-520` still
  routes `NEXTREG_NN`/`NEXTREG_A` around the I/O bus, never through
  `$243B`/`$253B`.
- "CPU alive... SD reads advancing" (2026-07-22 observation, matches this
  session) is consistent with the frozen main-loop state: since the exact
  same instruction path executes identically every frame, any
  *background* streaming (audio/level-data reads over SD, driven from a
  separate, unaffected part of the ISR) would keep progressing
  identically each time it's reached — the freeze is a fixed point of the
  *foreground* game-state processing, not a literal CPU halt.

## What is NOT yet determined

**Whether this is a jnext emulation bug or a genuine TX-1696 game bug that
would reproduce identically on real hardware / CSpect.** The G46(b)
methodology's mandatory next step — a CSpect differential comparison
(BP-spray / PC-stream diff at the same input sequence) — was **not**
completed this session: replicating the ~60-keypress NextZXOS Browser
navigation through CSpect's DZRP interface (no direct "load NEX and jump
into gameplay" path exists for this game — `test/nex-nonworking/
TX-1696.nex` confirms a bare `--load` doesn't work, it needs the full
NextZXOS/DivMMC context) was judged too large an effort to fit this
session's remaining budget without rushing a wrong conclusion. No VHDL
citation is offered for a divergence because none has been located — the
evidence so far is equally consistent with an in-game entity/counter bug
(e.g. an off-by-one on a 5-slot pool exhausting when firing at the wrong
moment) that has nothing to do with jnext.

## Next-session priority

1. **CSpect differential comparison** (mandatory before any fix is
   attempted): drive CSpect via DZRP through the identical browser nav +
   key schedule (`tools/g46b_102_repro_stride.sh`'s frame list is a ready
   template) against the same pinned-RTC, read-only SD image, and check
   whether it also produces a bit-identical stuck frame. If CSpect does
   **not** freeze under the same inputs, BP-spray around `0x89d1` /
   `0x8019` (RETI) to find the first PC/register divergence between the
   two traces — that divergence is very likely one step upstream of the
   actual defect. If CSpect **also** freezes, this is very likely a
   TX-1696 game bug, not a jnext bug, and should be reported upstream /
   closed as not-a-jnext-issue.
2. If jnext-specific: use `JNEXT_G46B_ITRACE` around the confirmed onset
   window (this session's data already narrows it to instructions
   executed within frames 3699-3712 of the `spamend=6000` repro,
   `itrace1.csv` if regenerated) to find the exact write/branch that
   diverges, then trace it to the VHDL-cited jnext behaviour it should
   have matched.
3. If a game bug: nothing to fix in jnext; consider closing #102 with
   this write-up as the terminal finding (owner's call — this is their
   issue, not mine to close).

## Files (this branch, `fix/102-tx1696-freeze`)

- `src/platform/headless_app.cpp` — `JNEXT_G46B_PCTRACE` /
  `JNEXT_G46B_ITRACE` probes.
- `src/cpu/z80_cpu.h`, `src/cpu/z80_cpu.cpp` —
  `Z80Cpu::set_g46b_itrace()`.
- `tools/g46b_102_repro.sh`, `tools/g46b_102_repro_sparse.sh`,
  `tools/g46b_102_repro_spamend.sh`, `tools/g46b_102_repro_stride.sh`.
- This document.

No production code paths were changed; only additive, env-gated probes.
