# G46(b) #102 session 2 — TX-1696 freeze: CSpect differential + root-cause hypothesis

Status: **strong, VHDL-cited hypothesis; no fix applied** (tool-stability
issue cut the session short of a build-verified fix — see "Session note"
at the end).

## Recap

Session 1 (`doc/issues/g46b-102-tx1696-freeze.md`, landed on `main` at
v0.99.28) produced a 100%-deterministic repro: TX-1696 freezes solid
(CPU alive, every register + the rendered framebuffer byte-identical
every frame) a few thousand frames after a `space` (fire) press lands at
a specific point in the game's own state. Session 1 did **not** complete
the mandatory CSpect differential — this session's job.

The owner has since verified TX-1696 **runs correctly on real Next
hardware**. The game-bug hypothesis is dead; this is a jnext divergence.

## (a) CSpect verdict: RUNS FINE — does not freeze

CSpect's on-screen window never renders in this sandbox (Xvfb has no
GL/GLX; OpenTK silently fails to create a usable context, confirmed by
`xdotool search` finding no window at all on `:99`), so the planned
xdotool-driven Browser navigation was infeasible. Two workarounds:

1. **Synthetic keyboard injection via a new CSpect plugin**
   (`tools/cspect_plugin/G46b102KeyInject.cs`, built with `mcs`, installed
   at `/home/jorgegv/src/spectrum/CSpect3_1_0_0/G46b102KeyInject.dll` —
   *outside* the repo, per the existing `CSpectFullTrace.dll` precedent
   documented in that same directory's build/install header comment).
   CSpect's plugin API has no "inject keypress" call, but it does let a
   plugin register as the `Port_Read` handler for arbitrary ports
   (`i2C_Sample/i2C_NULL_Device.cs` upstream pattern). The plugin
   registers all 256 ports with low byte `$FE` (every ULA/keyboard row
   selector), reproduces jnext's `src/input/keyboard.cpp` row/col matrix
   and its `DOWN = CapsShift+'6'` compound fold, and — driven by its own
   `Tick()`-based frame counter (`Tick()` fires once per emulated video
   frame per the `Plugin.iPlugin` interface doc) — clears the
   scheduled columns for 5 frames per press, mirroring jnext's
   `--delayed-keypress-frames` "press for 5 frames" semantics exactly.
   The **exact same frame-numbered nav+spam schedule** as
   `tools/g46b_102_repro_stride.sh` (450 space / 700 enter / 6+4+15+5
   down-groups / space every 100 frames from 2000) is hard-coded into
   `BuildSchedule()`.
2. **DZRP live-connect screenshots stall CSpect's free-run.** First
   attempt used `tools/cspect_dzrp/g46b_102_live_screenshot.py` (reads
   `0x4000-0x5AFF` via `read_mem` without pausing) polled periodically —
   two screenshots 15 real-seconds apart came back byte-identical, and
   the frame counter logged by the plugin stopped advancing (700→700
   across a 45s window with 6 connect/disconnect cycles, vs. ~2000
   frames in 40s with zero DZRP traffic). Diagnosis: connecting a second
   DZRP client mid-run (even for a single read) pauses/stalls the
   emulation and it does not resume on disconnect. **Switched to an
   in-plugin raw dump mechanism** instead
   (`G46B102_KEYINJECT_DUMPFRAMES=<csv>` — dumps `Peek(0x4000,6912)` to
   `screen_<frame>.bin` from inside `Tick()`, no DZRP involved) +
   `tools/cspect_dzrp/g46b_102_render_dump.py` to render those files to
   PNG **after** the run. This is the reason no DZRP script is used for
   the actual differential capture below — the earlier
   `cspect-debug`/`dzrp-compare` skill docs don't cover this failure
   mode; recorded here for the next investigation.

With the plugin driving CSpect (no `--sdcard-readonly`; a scratch clone
of `~/.jnext/sdcard/cspect-next-1gb-fixed.img` under scratchpad,
`G46B102_KEYINJECT_SPAMEND=4200`), the raw-screen dumps confirm correct
navigation at the same frame numbers jnext uses (frame 100: "Welcome to
NextZXOS" splash; frame 760: file Browser at SD root, matching the 6
downs + enter into GAMES) — i.e. **CSpect's and jnext's frame counters
track the same wall-clock-equivalent boot milestones** at matching
absolute frame numbers, so comparing the two traces frame-for-frame is
methodologically valid to first order.

The plugin's own per-frame CSV trace (PC/AF/BC/DE/HL/SP/IX/IY + an
FNV-1a hash of the ULA screen + all 128 hardware sprite records, written
every `Tick()`) shows **continuous variation through frame 8100** — ~65
emulated seconds past the frame (3713, see below) where jnext's
equivalent run is already frozen solid. No convergence signature
appears anywhere in the captured window, including the exact frames
where jnext's own spam schedule presses `space` again (3700, 3800, ...,
4200) after jnext has already locked up.

**Conclusion: CSpect does not reproduce the freeze under the identical
input schedule.** Combined with the owner's real-hardware confirmation,
this is a jnext-specific divergence.

## (b) jnext's own transition, pinned exactly

Re-running jnext with the **identical** schedule
(`tools/g46b_102_repro_stride.sh ... 2000 4200 100`) via
`JNEXT_G46B_PCTRACE` (per-frame) confirms: fb_hash + every register
change every sampled frame from before 3690 through **frame 3712**,
then **frame 3713 onward is byte-for-byte identical forever** (verified
through frame 4400 in one run, and separately through frame 4300 in an
independent `--sdcard-readonly` run with a slightly different absolute
onset frame, 3800 — the onset frame is schedule/state-dependent, not
fixed, matching session 1's stride experiment).

At the frame boundary the CPU is parked at `PC=$89d1` (`RET`) with
`AF=1553 BC=000b DE=00ff HL=89e2 SP=5ff2` — this exact tuple recurs
**every** frame, live or frozen (it is the main loop's per-frame
landing point, not itself anomalous, exactly as session 1 found). The
only registers that differ between the live occurrences are **IX/IY**
(entity/list-walker pointers): `cf3f/9857` is the walker's "rest"
value once it reaches the end of a list; `a898/4b78` etc. appear when a
walk is still in progress at the sample instant. From frame 3713
onward IX/IY are `cf3f/9857` **at every single sample**, i.e. the
per-frame entity-processing walk always reaches (and stops at) the same
rest point — and, since the fb_hash is also frozen, so is *everything
downstream of it* for the rest of that frame too, not just the sampled
boundary.

## (c) Root-cause hypothesis: `floating_bus_read()` misused as the generic unmapped-port default

**`src/core/emulator.cpp:8447-8526`, `Emulator::floating_bus_read()`**,
is registered as `PortDispatch`'s default handler for **every port with
no matching handler** (`src/core/emulator.cpp:547-550`,
`port_.set_default_read(...)`). But the function's actual content is
VHDL `zxnext.vhd:2813`'s **port-0xFF-specific** read mux
(`port_ff_rd_dat`) — the Timex-video/floating-bus signal, which per
`zxnext.vhd:4513` is explicitly gated to 48K/128K timing only. jnext's
own header comment for the function says so plainly
(`src/core/emulator.h:545-547`):

> `/// Only active in 48K/128K modes. Returns 0xFF when outside active
> display or in Next/Pentagon modes.`

And an existing comment at `src/core/emulator.cpp:4088-4109` (from the
already-closed GH #52 investigation) independently documents that
**under NextZXOS, both gates the Timex arm needs are set**
(`NR 0x08 bit 2` and `NR 0x82 bit 0`), so `floating_bus_read()`'s
*first* branch fires for every unmatched-port read, returning
`Ula::get_screen_mode_reg()` — **the byte last explicitly written to
port `0xFF`** — not a value that varies with raster position, T-state,
or contention.

TX-1696 executes a very dense pattern of `IN A,(C)` (`ED 78`) reads with
`BC` in the `0x1E00-0x1FFF` range (`JNEXT_G46B_ITRACE` around the
transition, `awk -F, '$3=="ed" && $4=="78"'`), overwhelmingly
`BC=$1F54` (9143 hits in a 20-frame/282k-instruction window),
`$1EB2`/`$1EBA`/`$1F10`/`$1E9A`/... Per `ports.txt`, the *only*
documented decode in that high-byte range is the CTC
(`0x183B-0x1F3B`, 4 channels implemented, `src/peripheral/ctc.h:10-13`
confirms only `$183B/$193B/$1A3B/$1B3B`), and **that decode requires
low byte `$3B`** — none of the observed low bytes (`$54,$B2,$BA,$10,
$9A,$1E,...`) are `$3B`. These are genuinely undecoded ports, landing on
the default handler.

The resulting `A` values are consistent with "current Timex register",
not floating bus: `BC=$1EB2` resolves to `A=$01` on every one of 9+
sampled occurrences in the transition window; `BC=$1F10` resolves to
`A=$33` then later `A=$34` (i.e. changes only when *something* writes
port `$FF`, not per-read). **This is exactly the "the game reads a
value it expects to vary, and jnext hands it a value that's pinned
until an unrelated event changes it" shape** that would explain a
state-triggered, then-permanent fixed point: if TX-1696's fire/entity
logic uses one of these undecoded-port reads as an entropy or
synchronisation source (a well-known ZX-era idiom for exploiting
floating-bus jitter), and the value jnext hands back stops changing at
exactly the moment the newly-fired bullet's logic needs it to differ,
every subsequent frame re-derives the identical decision from the
identical stale input — producing the observed byte-for-byte repeat.

**This is not a hidden bug — it is a self-documented scope gap.** The
`port_.set_default_read(...)` call site's own comment
(`src/core/emulator.cpp:547`) says *"unmatched port reads return ULA
bus value in **48K/128K modes**"* while the function is wired as the
default for **every** machine mode including Next. VHDL does not give
undecoded Next-mode ports a single dedicated "return the Timex
register" arm — the real bus is an OR-reduction of every decoder's own
driven byte, undriven when nothing claims the address, and its
resting/floating value is a bus-electrical phenomenon (contention,
refresh, whatever last drove it), not "whatever Timex mode is
currently set to."

## What is NOT yet confirmed

- **Which exact port(s) TX-1696 depends on**, and what decision the
  read result feeds. Not identified — would need a source-level/memory
  disassembly around the `IN A,(C)` sites in the `$89xx/$8Axx/$C3xx-
  $C6xx` game-code banks (the ITRACE data has the PCs; not yet
  correlated against the game's own logic).
- **Whether this specific mechanism, not some other floating-bus-
  adjacent path, is the actual trigger.** The evidence (self-documented
  Next-mode limitation + dense undecoded-port reads in the game's own
  code + a plausible “stale-value-collapses-the-branch” mechanism
  matching the observed byte-for-byte freeze) is strong and
  circumstantial, not a confirmed single-instruction culprit. A CSpect
  BP-spray or a jnext-side "trap on IN A,(C) with BC matching the
  undecoded set, log the caller PC" probe would close the gap — not
  done this session (see below).
- **A cross-emulator PC-level diff was not obtained.** CSpect and jnext
  have independent frame-boundary sampling conventions and (per the
  earlier finding) different PC values line up only approximately, so
  a literal "first divergent PC" between the two traces was not
  extracted — the differential evidence here is at the *behavioural*
  level (freezes vs. doesn't), not instruction-for-instruction.

## Session note — tool outage

Partway through the ITRACE cross-check (isolating which `BC` port
values are genuinely constant vs. which vary, to separate a real
undecoded-port hit from CTC/other legitimate handlers), the sandbox's
Bash tool became persistently unreliable (empty output / exit 1-2 on
nearly every invocation, including trivial `echo`, across many retries
over an extended period, with only brief windows of recovery). This is
a session/host issue, not a code or investigation issue, but it means:

- No production code was changed and **no fix was attempted** (would be
  irresponsible to write a speculative fix without being able to build
  or test it).
- The remaining verification steps above (confirm the exact triggering
  port + the game-code decision it feeds) were not completed.
- **This document and the plugin/DZRP-script source files were written
  via the Read/Write/Edit tools, which remained available throughout,
  but Bash (needed for `git add`/`git commit`, further `grep`
  correlation, and any build/test) was not reliably available for the
  rest of the session.** If a commit did not land, these files are
  present on disk in this worktree, uncommitted.

## Next-session priority

1. Confirm the exact caller PC(s) of the undecoded-port `IN A,(C)`
   reads via `JNEXT_G46B_ITRACE` correlated against the game's own
   disassembly (the reads happen from PCs in the `$89xx-$8Bxx`
   entity-logic bank based on this session's window).
2. Add a narrow env-gated probe
   (`JNEXT_G46B_UNDECODED_PORT_TRACE`) in `PortDispatch::read()`'s
   `default_read_` branch: log `(pc, port, value)` whenever the
   default/floating-bus path is taken with a port that isn't `0xFF`
   itself — this directly answers "does TX-1696's stuck state coincide
   with this path returning a value the game didn't get last frame,
   but this frame it does (or vice versa)?"
3. If confirmed: the VHDL-faithful fix is almost certainly **not**
   "make `floating_bus_read()` vary for Next mode" (inventing new
   floating-bus behaviour VHDL doesn't specify would violate the
   VHDL-faithful mandate) but rather auditing whether **each** of these
   undecoded ports is *actually* undecoded on real Next hardware — i.e.
   whether jnext is simply missing a real peripheral's port
   registration (the CTC's ports 4-7, `$1C3B-$1F3B`, are explicitly
   *not implemented* per `src/peripheral/ctc.h` and the VHDL doc's own
   "temporarily reduced to four channels" note — if TX-1696 targets one
   of those, the fix is implementing the missing 4 CTC channels, not
   touching the floating-bus function at all).
4. Re-run the CSpect differential (this session's plugin + schedule are
   reusable) once a fix candidate exists, to confirm CSpect's
   continued-animation behaviour is preserved/matched.

## Files (this session, worktree `fix-102b`, uncommitted at outage time)

- `tools/cspect_plugin/G46b102KeyInject.cs` — CSpect keyboard-injection +
  per-frame trace plugin (see file header for build/install/env-var
  docs). Built `.dll` was installed at
  `/home/jorgegv/src/spectrum/CSpect3_1_0_0/G46b102KeyInject.dll`
  (outside the repo, same convention as the pre-existing
  `CSpectFullTrace.dll`/`JnextG46bTrace.dll`/`Task18Snapshot.dll` — not
  removed at session end due to the tool outage; safe to leave (inert
  unless CSpect is launched) or delete).
- `tools/cspect_dzrp/g46b_102_live_screenshot.py` — live DZRP ULA-screen
  PNG renderer (**caveat**: connecting it mid-run stalls CSpect's
  free-run; use only for one-shot before/after checks, never polling).
- `tools/cspect_dzrp/g46b_102_render_dump.py` — offline renderer for the
  plugin's `G46B102_KEYINJECT_DUMPFRAMES` raw screen dumps.
- This document.

No production code paths were changed.
