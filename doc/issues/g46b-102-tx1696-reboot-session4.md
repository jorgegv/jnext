# G46(b) #102 session 4 — TX-1696 reboot regression from the session-3 fix

Status: **FIXED**. The v0.99.29 fix (commit `2e16105f`, merge `45c73c6d`) for
the TX-1696 DMA/IM2 deadlock introduced a regression: TX-1696 now soft-resets
to the NextZXOS Welcome screen a few thousand frames in, well before reaching
gameplay — strictly worse than the original freeze it replaced. Root-caused
via direct instrumentation (PCTRACE + ITRACE + `--log-level emulator=debug`),
traced to a specific VHDL-cited granularity gap, fixed, and verified
end-to-end (weapon select → level load → gameplay → game over, no reboot, no
freeze, matching the pre-2e16105f binary's own early trajectory almost
exactly).

## Symptom

Reported by the owner: launching TX-1696 (previous sessions' repro) now
reboots to (what turned out to be) the NextZXOS boot sequence instead of
reaching the game menu.

## Reproduction

`tools/g46b_102_repro_stride.sh <sd> <out.png> <sframe> <eframe> 2000 8000 100`
(same script/schedule sessions 1-3 used) against the v0.99.29 binary. Screenshot
bisection:

| Frame | Content |
|-------|---------|
| 2000  | Gameplay in progress (ship + starfield) |
| 2080-2180 | TX-1696's own title/attract screen ("TX-1696 / PLAY / CREDITS / SETTINGS") — normal |
| 2185  | Black screen |
| 2200-2250 | Grey blank (boot-time default border, before any layer enabled) |
| 3000, 4000 | Static NextZXOS main menu ("NextZXOS 3.5MHz > Browser/Command Line/...", "©1982, 1986, 1987 Amstrad Plc" splash) — **this is a reboot**, not game progress |

Bisecting further with `--log-level emulator=debug` located two `Soft reset
triggered via NextREG 0x02` log lines. The **first** (`PC=0x6d31,
rom_bank=0x00`, ~1 second / ~frame 257 into any run) also appears on a
completely bare vanilla boot with **no navigation at all** — confirmed by a
separate `--delayed-screenshot-frames 400` run with zero keypresses, same log
line at the same relative point. That one is normal TBBlue boot handoff
behaviour and is not the bug. The **second** (`PC=0x3bf5, rom_bank=0x03`,
landing at ~frame 2184-2185) is the regression.

## Probes used (all pre-existing, session 3's own instrumentation)

- `JNEXT_G46B_PCTRACE` [`_START`/`_FRAMES`] — per-frame PC/regs/fb_hash +
  `dma_state`/`dma_counter`/`dma_block_len`/`im2_dma_delay`/`devstates`
  (`src/platform/headless_app.cpp`).
- `JNEXT_G46B_ITRACE` [`_START`/`_FRAMES`] — per-instruction PC/opcode
  bytes/regs, with a `# FRAME N` marker per frame (same file;
  `Z80Cpu::set_g46b_itrace`).
- `--log-level emulator=debug` — surfaces the existing (pre-session-4)
  `Log::emulator()->debug("Soft/Hard reset triggered via NextREG 0x02 ...")`
  lines in `src/core/emulator.cpp`'s NR 0x02 write handler; not normally
  visible at the default `info` level.

No new probes were needed this session.

## Mechanism — instruction-level evidence

`JNEXT_G46B_PCTRACE` over frames 2170-2200 shows the divergence bracket
precisely:

```
frame  pc    sp    im  iff1  dma_state  im2_dma_delay
2184   c317  5ffa  2   1     1(TRANSFERRING) 0
2185   0168  ffff  0   0     0               0     <- deep in boot-ROM
                                                       memory-clear loop
2186..2192   0168  ffff 0    0    (pinned for 8 samples: LDDR loop)
```

`SP=0xFFFF, IM=0, IFF1=0, PC=0x0168` is the FPGA boot ROM's early
memory-clear routine — the signature of a fresh reset, reached at the very
next frame boundary after `request_hard_reset()`/`soft_reset()` deferral
(Task 70 — a hard reset reconstructs the whole `Emulator`; a soft reset is
synchronous within the current instruction stream, so its downstream effect
only becomes visible in the PCTRACE row of the frame where it actually fired).

`JNEXT_G46B_ITRACE` for the game's own frame (frame index 2185, tstates 0
through the transition) decodes the exact sequence:

```
tstate=59258  c35d: C5            PUSH BC        ; BC=0xC35F (return addr for
                                                    the classic "PUSH ret; JP
                                                    (HL)" == CALL(HL) idiom)
                                                    HL=0x6464 at this point
              <IM2 interrupt accepted here, between PUSH BC and JP (HL)>
tstate=59291  8000: ED 91 56 02   NEXTREG 0x56,0x02   ; page MMU slot 6
tstate=59320  8005: ED 91 57 03   NEXTREG 0x57,0x03   ; page MMU slot 7
tstate=59344  8009: F5            PUSH AF
tstate=59356  800A: CD EA C0      CALL 0xC0EA          ; DAC/Specdrum sample
                    C0EA: PUSH HL; LD HL,0xF001; LD A,(HL); OUT (0xDF),A;
                          advance pointer; POP HL; RET
tstate=59501  800D: 3A 1C 80      LD A,(0x801C)        ; saved bank value
tstate=59518  8010: ED 92 56      NEXTREG_A 0x56        ; restore slot 6
tstate=59538  8013: 3C            INC A
tstate=59543  8014: ED 92 57      NEXTREG_A 0x57        ; restore slot 7=A+1
tstate=59563  8017: F1            POP AF
tstate=59576  8018: FB            EI
tstate=59581  8019: ED 4D         RETI
tstate=59599  c35e: E9            JP (HL)     ; HL is STILL 0x6464 — the ISR
                                                 never touched HL
tstate=59604  6464: 01 10 01 10   <not code — structured, period-11-repeating
                                    byte pattern, decoded as garbage
                                    instructions for ~4700 more instructions>
tstate=66393  6943: FF            RST 38      ; landed on 0xFF "by accident"
tstate=66405  0038: F5 E5 2A 78   <further garbage, eventually an OUT that
                                    happens to write a byte with bit 0 set to
                                    NextREG port 0x02, register 2 (left
                                    selected by earlier ordinary NEXTREG
                                    traffic) -> soft_reset()>
```

`HL=0x6464` is not garbage produced by the interrupt handler — it was already
the intended jump-table target when `PUSH BC` executed, before the interrupt
even landed, and the ISR at `0x8000` never touches `HL`. Address `0x6464`
falls inside a region that decodes as a long, highly regular period-11 byte
pattern — the signature of structured game **data**, not code — that
eventually happens to contain the byte `0xFF` (`RST 38`), triggering a wild
jump through further garbage until an accidental `OUT` write to NextREG port
`0x02` (register select left at `2` by earlier normal traffic) sets bit 0 and
fires `soft_reset()`.

Separately, `JNEXT_G46B_ITRACE`/`JNEXT_G46B_PCTRACE` show TX-1696 (or a
library routine linked into it) driving the Z80-DMA controller through a
**generic "block copy" idiom**: a fixed 16-byte WR-command table gets three
`LD (table+n),rr` stores (`HL`→source address, `BC`→length, `DE`→destination)
immediately before an `OTIR` streams `RESET`+registers+`ENABLE` to port
`0x0B` — decoded from `PC=0x89E2..0x89F4` in this session's trace. This
routine fires roughly every ~2500 T-states — **hundreds of times within a
single video frame** — and its `dma_state`/`im2_dma_delay` PCTRACE columns
flicker between deferred and live on almost every sampled frame around
2172-2184. Address `0x6464` is very likely a destination this same routine
is expected to have already filled with real code/data by the time the
foreground `JP (HL)` dispatch runs.

## VHDL verdict — the actual granularity bug

**`zxnext.vhd:2001-2010`**:

```vhdl
process (i_CLK_CPU)
begin
   if rising_edge(i_CLK_CPU) then
      if reset = '1' then
         im2_dma_delay <= '0';
      else
         im2_dma_delay <= (im2_dma_int) or (nmi_activated and nr_cc_dma_int_en_0_7)
                            or (im2_dma_delay and dma_delay);
      end if;
   end if;
end process;
```

`im2_dma_delay` is a synchronous latch clocked on `i_CLK_CPU` — it updates
**every CPU clock cycle**, not once per video frame. `dma.vhd:267-281`'s
`START_DMA` defer gate and `zxnext.vhd`'s `dma_holds_bus <= '1' when
z80_busak_n = '0'` (the CPU-stall signal) are both still correctly modeled by
`Dma::tick_arbitration()`/`Dma::dma_holds_bus()` — session 3's fix did not
touch those and they remain correct.

The bug is entirely in **how often `Dma::set_dma_delay()` is fed a fresh
value**. `Im2Controller::step_dma_delay()` (`src/cpu/im2.cpp:1414`, called
from `im2_.tick()`, itself called once per CPU instruction from
`Emulator::step_one_instruction()`) already tracks `im2_dma_delay_latched_`
with per-instruction freshness — that half was always correct. But before
this session, the **only** call that pushed that value into the `Dma` object
was `dma_.set_dma_delay(im2_.dma_delay())` inside `Emulator::begin_new_frame()`
— run **once per video frame** (~70 000 T-states at 3.5 MHz, up to
~567 000+ at 28 MHz, the speed TX-1696 runs at per this session's trace).

This coarse granularity was a **deliberate, explicitly-flagged** decision,
made 2026-04-21 (`doc/design/TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md`,
"Resolved open question — DMA delay granularity (Q4, 2026-04-21)"), with its
own prescient caveat on option A (per-frame):

> "If the delay signal is briefly asserted and cleared within a single frame
> (e.g. RETI immediately before a DMA trigger), we miss the window and either
> over-stall (if latched high) or under-stall (if only sampled at frame top)
> ... If a concrete regression or new software later demonstrates a
> divergence, we upgrade to per-tick in a targeted follow-up (one branch, one
> critic) — incremental, reversible."

TX-1696 (GH #102) is exactly that "concrete regression". Before session 3's
fix (`2e16105f`), the staleness was **hidden**: `Dma::is_active()` froze the
*entire* CPU (including `im2_.tick()`) for the whole duration of any defer,
so nothing else ever ran to observe a stale `dma_delay_` — the freeze itself
was the visible bug, independent of sampling granularity. Session 3's fix
correctly stopped freezing the CPU during a merely-deferred `ENABLE`, which is
what let the underlying per-frame staleness become directly observable for
the first time: because `dma_delay_` could not refresh mid-frame, a
genuinely brief real-hardware defer (which VHDL clears within a handful of
T-states once the pending device is serviced) instead persisted for up to a
**full video frame** in jnext. During that artificially-elongated window,
TX-1696's high-frequency DMA-copy routine kept re-arriving (and re-deferring)
while far more foreground/interrupt-driven code ran than real hardware ever
would during that same nominal window — desynchronising the `JP (HL)`
jump-table dispatch from a still-pending DMA-driven update, with the wild-jump
consequence traced above.

## The fix

**`src/core/emulator.cpp`**:

1. `Emulator::step_one_instruction()` — added, at the very top (before the
   `dma_.state() == Dma::State::TRANSFERRING` branch):
   ```cpp
   dma_.set_dma_delay(im2_.dma_delay());
   ```
   This resamples the live, per-instruction-fresh `im2_.dma_delay()` latch
   every CPU instruction — the finest granularity jnext's instruction-level
   model can offer, matching VHDL's per-`i_CLK_CPU`-cycle update as closely
   as the architecture allows (exactly "option B" from the 2026-04-21 design
   doc).
2. `Emulator::begin_new_frame()` — removed the now-superseded once-per-frame
   `dma_.set_dma_delay(im2_.dma_delay())` call (the per-instruction call
   above already covers a frame's first instruction too); comment updated to
   point at the new call site.

This **preserves** the original #102 deadlock fix intact: `execute_burst()`
is still called unconditionally every step while `state_==TRANSFERRING`
(progressing arbitration every step regardless of stall), the CPU is stalled
only when `transferred>0 || dma_holds_bus()`, and while merely deferred the
normal instruction path (including `im2_.tick()`) still runs every step. The
**only** change is that the *input* driving the defer decision is now fresh
every instruction instead of stale for up to a whole frame — a genuine defer
now clears exactly as soon as (and only as soon as) it VHDL-genuinely should,
instead of lingering for tens or hundreds of thousands of extra T-states.

## Test

**`test/dma/dma_test.cpp` row 23.7** rewritten. The old version manually
called `Dma::set_dma_delay(true)` once and expected it to survive an
`Emulator::execute_single_instruction()` call unmolested — exactly the
behaviour this session proved is no longer representative: a hand-forced
value is now clobbered by the very next per-instruction resample. The row
now drives a **genuine** pending IM2 condition through a full `Emulator`
(CTC0 raised to `S_REQ` with its `dma_int_en` bit set via NR 0xC0 IM2-mode +
`Im2Controller::set_dma_int_en_mask`/`raise_req`/`tick`, mirroring the live
TX-1696 trigger and the existing `group15_dma_int`/`group20_dma_delay` idiom
in `test/ctc/ctc_test.cpp`), asserts the CPU still executes normally while
genuinely deferred, then services the condition by clearing CTC0's
`dma_int_en` bit (equivalent to the interrupt being handled) and asserts the
transfer completes on the next step.

**Mutation-tested**: reverting just the `step_one_instruction()` resample
line (via a `cp`-backup revert/restore, never `git checkout`) fails **only**
23.7 (156/157 pass, PC never advanced: `cpu_ran=0(pc=8100)`, the deferred
1-byte transfer completed same-step instead of staying deferred — exactly
the bug this session diagnoses); restoring the fix returns 157/157. No other
row in `dma_test.cpp` (or any other suite, per the full triplet below) is
affected by this change.

`test/unit-tests.conf` row count for `dma_test` is unchanged (157 → 157 —
this session edited an existing row's content, it did not add or remove
rows). `doc/testing/DMA-TEST-PLAN-DESIGN.md` row 23.7's description updated
to match.

## Verification — milestone screenshots

Same repro schedule (`SPSTART=2000 SPEND=8000 stride=100`), fixed binary:

| Frame | Fixed binary (this session) | Pre-fix binary (`920e5866`, before 2e16105f) |
|-------|------------------------------|-----------------------------------------------|
| 2200  | TX-1696 weapon-select screen | **Same screen** (minor cosmetic icon-highlight difference from ~2200 frames of accumulated timing drift) |
| 3000  | "LEVEL 1 LOADING" | **Same screen**, pixel-for-pixel layout |
| 5000  | Black (loading transition) | — |
| 8000  | "GAME OVER" (full playthrough) | — |

The fixed v0.99.29+session-4 binary's early trajectory (frames 2200/3000)
matches the **pre-session-3-fix** binary almost exactly — the regression is
fully resolved and the game follows its original, correct path through
weapon-select and level-loading. By frame 8000 the game has played all the
way through to "GAME OVER" with **no reboot and no freeze anywhere** in the
window.

## Verification — deadlock non-regression (row 23.7's production analogue)

`JNEXT_G46B_PCTRACE` over the full frames 0-15000 (14 101 samples,
same schedule) with the `dma_state`/`dma_counter`/`dma_block_len`/
`im2_dma_delay`/`devstates` columns:

```
max consecutive "non-clean" (dma_state!=0 or im2_dma_delay!=0) run:  2 frames
total non-clean rows:                                                 1647 / 14101
max consecutive identical framebuffer hash:                          226 frames
unique framebuffer hashes across the whole window:                  12229 / 14101
```

No permanent stuck state anywhere (max non-clean run is 2 *frames*, not
"forever" as the original #102 bug produced), and the framebuffer keeps
varying continuously throughout (226 frames is an ordinary loading pause, the
same order of magnitude as session 3's own benign-pause finding of 225
frames) — the original DMA/IM2 deadlock fix from session 3 is fully preserved.

## Full triplet (post-fix)

- `make clean && make gui-release`: clean.
- `make unit-test`: **5686/5686 passed, 0 failed, 0 skipped** (76 suites,
  all declared/registered); `dma_test` 157/157.
- `./build/test/fuse_z80_test build/test/fuse`: **1356/1356**.
- `JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh` (logged, not
  piped): **106/106 passed, 0 failed, 0 skipped**.

## Files (branch `fix/102-tx1696-reboot`, worktree `fix-102c`)

- `src/core/emulator.cpp` — `step_one_instruction()` per-instruction
  `dma_.set_dma_delay(im2_.dma_delay())` resample; `begin_new_frame()`
  once-per-frame call removed.
- `test/dma/dma_test.cpp` — row 23.7 rewritten to drive a genuine IM2
  pending condition through a full `Emulator`.
- `doc/testing/DMA-TEST-PLAN-DESIGN.md` — row 23.7 description updated.
- This document.

No probe files were needed this session — session 3's `JNEXT_G46B_PCTRACE`/
`JNEXT_G46B_ITRACE` instrumentation (already on `main`) was sufficient.

## Residual, out-of-scope findings for a future session

- The self-modifying generic "DMA block-copy" utility TX-1696 uses
  (`PC≈0x89E2-0x89F4`, `HL`=source/`BC`=length/`DE`=destination patched into
  a fixed 16-byte WR-command table before `OTIR` to port `0x0B`) was not
  further reverse-engineered — its exact purpose (code-overlay streaming?
  scrolling buffer shift?) is not identified, only its *frequency* (~every
  2500 T-states) and its role in this bug (repeatedly re-arriving during the
  now-correctly-brief defer window). Not needed for this fix, but would be
  useful context for any future TX-1696-adjacent investigation.
- `doc/design/TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md`'s "DMA delay
  granularity (Q4)" section still describes option A (per-frame) as the
  landed decision — should be updated to record that option B (per-tick /
  per-instruction) is now what's implemented, with a pointer to this
  document. Not done this session (scope: fix the regression, not rewrite
  historical planning docs) — flagged for whoever next touches that file.
