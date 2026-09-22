# Warm-Start State Cache — running NEX files from a real post-boot machine

> Status: **implemented, opt-in** (`--warm-start`). Milestone v1.1.
> Tracking issue: [#234](https://github.com/jorgegv/jnext/issues/234).
>
> §10 below is the implementation record: what the measurement the design
> demanded actually found, including the two places this document was wrong.

## 1. The problem

`--load foo.nex` does not boot the machine. `Emulator::init()` skips the boot-ROM
overlay whenever a load file is present (it would clobber the program's own
reset vector), so `nextboot.rom` never runs, `TBBLUE.FW` never runs, and
NextZXOS never runs. The NEX is applied to a machine that was assembled by C++
rather than by firmware.

That machine is not merely *different* from hardware; parts of it are in states
hardware cannot reach. GH #226 is the proof: nothing ever writes NextREG `$03`
on that path, so `nr_03_config_mode` stays at its power-on `true` for the whole
session. On hardware the IPL clears that bit within microseconds of power-on and
it is `0` for the rest of the machine's life. The consequences were three dead
hotkeys — F4 (soft reset, gated by `zxnext.vhd:6370`), F9 and F10 (both NMI
sources, force-cleared while config mode is held, `zxnext.vhd:2102-2105`) — none
of which announced itself.

GH #226 fixes that one bit by synthesising the single `NR $03` commit the
firmware would have made. It is the right fix for that defect and it is honest
about its scope: it repairs *one* known divergence. It says nothing about the
divergences nobody has looked for yet, and it cannot, because the reference —
what the firmware actually leaves behind — exists only as a running machine.

A second, user-visible consequence: a firmware-less boot has no NextZXOS ROM
(the SRAM ROM pages are seeded from the 48K image), so a soft reset from a
`--load` session lands in 48K BASIC. Faithful to a machine with no firmware,
and nothing like what the same key does on hardware.

## 2. The proposal

Boot the machine for real, once, and keep the result.

1. On the first `--load` (or on demand), jnext performs a full native cold boot
   from the mounted SD image: `nextboot.rom` → `TBBLUE.FW` → NextZXOS, exactly
   as it does today when no load file is given, and exactly as the
   `boot-nextzxos-*` regression rows already exercise.
2. When the machine reaches a defined idle point, jnext serialises the complete
   emulator state through the existing `Saveable` interface — the same
   `save_state`/`load_state` implemented by every subsystem for the rewind
   buffer — and writes it to a **cache file next to the SD image**.
3. Every later `--load` restores that state and applies the NEX on top, instead
   of assembling a machine from defaults.

The machine a NEX meets is then, by construction, one the firmware produced.
Not an approximation of one: a recording of one.

## 3. Which moment to capture

"After firmware initialisation" names two very different machines, and the
choice decides whether this works.

| Capture point | What the NEX gets | Verdict |
|---|---|---|
| After `TBBLUE.FW`, before NextZXOS | A correctly configured Next with no OS resident | Fixes the NextREG/MMU divergences; still cannot serve `RST $08` |
| **After NextZXOS reaches idle** | **NextZXOS resident, its ROM paged, system variables set, esxDOS/DivMMC live** | **Recommended** |

On hardware a NEX is loaded *by* NextZXOS — from the Browser, from a dot
command, from `nexload2` (itself a dot command, and the behavioural oracle for
V1.3). Anything that performs file I/O, calls `M_EXECCMD`, or chain-loads a
sibling expects the OS underneath it. Capturing before NextZXOS reproduces the
hardware's *registers* while still lying about its *services*.

The idle point must be defined precisely enough to be reproducible — the
NextZXOS main-menu idle `HALT` (`PC=0x0C8F` with `IFF1=1, IM=1`, as measured
during the GH #226 investigation) is a candidate, but the implementer should
establish the criterion empirically rather than pinning a PC that may move
between NextZXOS versions.

## 4. Where the snapshot lives — generated locally, never vendored

**The cache is produced on the user's machine and stored beside the SD image
(`~/.jnext/`), keyed by a hash of that image. It is not committed to the
repository and not shipped in any package.**

Two independent reasons, either sufficient:

**Licensing.** A post-NextZXOS snapshot contains NextZXOS and DivMMC ROM
content in RAM. Committing it would redistribute copyrighted firmware. This is
precisely why `roms/` holds only `nextboot.rom`, why jnext downloads the SD
image rather than shipping it, and why the PR protocol requires licence-clean
fixtures. A vendored snapshot fails that rule outright.

**Correspondence.** The snapshot is a derived artefact of one SD image and one
firmware version. Ship it, and a user who mounts a newer NextZXOS runs a machine
claiming to be post-firmware-X on top of firmware-Y — a *new* class of
unreachable state, subtler than the one this design exists to remove, and
without even the excuse of being obviously synthetic. Deriving it locally from
the image actually mounted makes the correspondence structural.

**Invalidation.** The cache records the hash of the SD image it came from, the
machine type, and a state-format version. A mismatch on any of the three
discards it and re-boots. Hash first, boot only on a miss — the same shape the
regression suite's SD-provision gate already uses (`scripts/01-sdcard-provision.sh`).

## 5. Prior art in this repo, and why this is not it

`--bypass-tbblue-fw` (Task 18, 2026-05-17) did something superficially similar:
it performed `boot.c::main`'s Z80-side work — SRAM ROM load, NextREG init, the
`NR $03` machine commit — directly in C++ and handed control to NextZXOS with
post-reset state pre-established. It was **removed on 2026-07-11** once native
boot worked, on the judgment that approximating firmware in code is worse than
running it. Its design survives for reference in
[FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md](FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md).

This proposal does not fall to that objection: it does not model what the
firmware does, it records what the firmware did. The state is the firmware's own
output, byte for byte.

But it inherits the warning. The moment a recording stops corresponding to what
the current firmware would produce, it becomes hand-maintained fiction with no
gate to catch it — which is exactly what the hash-keyed invalidation in §4 is
for. A cache that cannot go stale is a recording; one that can is a fixture.

## 6. What this buys, beyond correctness

- **Startup latency.** A native boot to the NextZXOS idle costs several seconds;
  restoring a state is a deserialise. `--load` becomes fast *and* faithful,
  which today are alternatives.
- **A sane reset contract.** With NextZXOS resident, a soft reset from a `--load`
  session lands back in NextZXOS — what the key does on hardware — instead of
  48K BASIC.
- **Determinism for the suite.** Every `--load` regression row starts from one
  recorded machine rather than from whatever `init()` assembles, which also
  removes the boot-time RTC as a variable.

## 7. Risks and what must be measured before committing

1. **Blast radius on existing references.** Every `--load` screenshot row today
   starts from the synthetic state. Starting from a real post-boot machine
   changes NextREG defaults, palette contents and timing context, so references
   may move. A moved reference must be *understood*, not regenerated —
   regenerating without permission is forbidden, and a reference that moved for
   an unexplained reason is a finding, not a chore. **Measure this on a branch
   before committing to the design.**
2. **State-format coupling.** The snapshot rides on the same serialisation the
   rewind buffer uses. A full state is on the order of a couple of megabytes
   (the rewind buffer's measured per-frame cost is ~2.29 MB, identical across
   machine types). That is unremarkable as a local cache file and would have
   been another argument against vendoring. Any change to a subsystem's
   `save_state` invalidates existing caches — hence the format version in §4.
3. **Capture-point stability.** If the idle criterion is pinned too tightly
   (an exact PC), a NextZXOS update breaks capture silently. Prefer a criterion
   with slack, and fail loudly rather than capturing the wrong moment.
4. **Non-Next machine types.** `--machine 48k/128k/plus3` never install the boot
   ROM either. They have their own version of this problem (GH #226 fixes their
   config-mode bit too), but there is no firmware to record — the warm-start
   cache is a Next-only mechanism, and the design should say so rather than
   quietly doing nothing there.
5. **First-run cost.** The first `--load` on a fresh machine pays a full boot.
   That must be visible (a log line, or the existing busy mechanism), not a
   mysterious pause.

## 8. Alternatives considered

- **Vendor the snapshot as a repo asset.** Rejected: licensing (§4) and the
  correspondence failure it creates.
- **Synthesise the post-firmware state in C++.** Rejected: this is
  `--bypass-tbblue-fw`, already tried and already removed (§5).
- **Boot natively on every `--load`.** Correct but slow; it makes the regression
  suite pay a multi-second boot per row. The cache is exactly this option with
  the cost amortised, which is why it is preferred rather than opposed.
- **Fix divergences one at a time as they are found.** This is the status quo,
  and GH #226 shows what it costs: the divergence was found by a user reporting
  a frozen screen, months after the path was written. There is no enumeration of
  what else differs, and no way to produce one except by comparison with a real
  boot — which is this proposal.

## 9. Open questions for the implementer

1. Does the NEX apply cleanly on top of a NextZXOS-resident machine, or does it
   need the OS torn down first (the way `nexload` does on hardware)? This is the
   central unknown and should be answered before anything is built.
2. Should the cache be per machine type, per SD image, or both?
3. Should `--load` be able to opt out (`--no-warm-start`) for users who want
   today's behaviour or who are debugging the boot path itself?
4. Does the same cache serve TAP/TZX/SNA/Z80 loading, where the same
   firmware-less-machine argument applies?
5. Where does this leave the `--machine 48k/128k/plus3` paths — documented as
   out of scope, or given their own smaller treatment?

---

## 10. Implementation record (2026-09-22)

Everything here is measured. Where it contradicts §1-§9, this section wins —
§1-§9 are the argument that was made before the machine was built.

### 10.1 The gating question, answered

> "Does the NEX apply cleanly on top of a NextZXOS-resident machine, or does it
> need the OS torn down first (the way `nexload` does on hardware)?" (§9.1)

**It applies cleanly, and no teardown has to be added: `NexLoader::apply()`
already IS the teardown.** That routine models what `nexload.asm` /
`nexload2.asm` do before handing over — the NextREG reset block, the palette
and clip windows, the loading screen, the bank placement, the entry registers —
and it runs unchanged on a restored machine. The oracle for the question is the
loader, not the OS, and jnext already had it.

Evidence, in the order it was taken:

1. **The hardware path works in jnext.** Booting NextZXOS headless, selecting
   *Command Line* from the main menu and typing `.nexload s.nex` (the distro's
   own `/DOT/NEXLOAD`, against a copy of `/demos/show512/show512.nex` placed at
   the card root) runs the program. That is the reference image every later
   comparison is against, and it is the real loader on the real OS.
2. **A warm-started `--load` of the same file is PIXEL-IDENTICAL to the
   committed `show512` reference** — 0 pixels differ. Applying a NEX on top of
   a restored NextZXOS neither crashes nor changes what that program renders.
3. **The whole `--load` corpus was measured**, not one file: all 37 `next` +
   `.nex` screenshot rows of the regression suite, run twice (synthetic and
   `--warm-start`) and pixel-diffed against their committed references. Result
   after the fix in §10.3: **27 of 37 identical, 10 moved**, and every one of
   the 10 is accounted for in §10.4. The synthetic path is byte-for-byte
   unchanged (`cold_vs_ref = 0` on all 37), which is what makes `--warm-start`
   safe to ship opt-in.

### 10.2 A recording must be a COLD boot, and re-`init()`ing is not one

The first implementation re-`init()`ed the live emulator with `load_file`
cleared. It produced a machine that ran 500 frames and **drew nothing**: four
RAM pages changed, none of them the screen, and the ROM window still held
exactly the `48.rom` bytes `init()` seeds.

The cause is a preserved bit. `nr_03_config_mode` has no reset clause in the
VHDL (`zxnext.vhd:1102`) and `NextReg::reset()` faithfully preserves it, so the
firmware-less NR `$03` commit that the *first* `init()` makes (GH #226) left
config mode **clear** — and with it clear, `TBBLUE.FW`'s ROM streaming through
the NR `$04` window (`zxnext.vhd:3044-3050`) never reaches the ROM area at all.

So the recording is taken on a **separate, freshly constructed `Emulator`**,
which is at power-on state by construction, exactly as
`platform/emulator_boot.h::emulator_cold_boot()` is for F1. It also means a
failed recording cannot leave the live machine half-booted.

This is a correction to §2, which said "jnext performs a full native cold boot"
without noticing that the object it would perform it on had already been
initialised once.

### 10.3 What the warm start SURFACED, and what was fixed

`NexLoader::apply()` did not model MMU0-5 at all. It relied on `init()`'s reset
defaults happening to hold bank 5 at `$4000` and bank 2 at `$8000`. They do on
a machine `init()` assembled; they do not on one NextZXOS has been running in.
A warm-started load measured **MMU3 = page 17** — Layer 2's bank 8, which
NextZXOS's welcome screen had paged in — so every write a program made to
`$6000-$7FFF` landed in the wrong bank.

Symptom, before the fix: `tilemap-demo`, `stencil-demo`, `stencil-layers-tiles`
and `odemo` rendered a **black frame**, and `lores-demo` lost the bottom half
of its picture (the half that lives in bank 5's second 8 KB).

Both reference loaders set these registers explicitly, and — as for
NR `$07`/`$15`/`$42`/`$43` (GH #166, GH #171) — they gate them differently:

| Register | `nexload.asm` (<= V1.2) | `nexload2.asm` (V1.3) |
|---|---|---|
| MMU2-5 = 10, 11, 4, 5 | `:280-283`, UNCONDITIONAL prologue, above the `DONTRESETNEXTREGS` gate at `:323` | `:913-918` `nextRegResetData`, inside the PRESERVENEXTREG gate |
| MMU0/1 = `$FF` (ROM) | `:406-407`, inside the gated block | same table, same gate |

`apply()` now writes them under exactly those gates. It is a **no-op on the
synthetic path** (the reset defaults already match), which the 37-row
measurement confirms, and it is pinned by `NEXMMU-01..07` in `nex_loader_test`.

MMU6/7 are deliberately NOT modelled although both tables set them to 0,1:
step 6 overwrites them with the entry bank on every path, and the only path
that reads the pre-entry value back is the load-only one (header PC = 0), which
restores what it found. On hardware `.returnToBasicTidy` restores BANKM
(`$5B5C`) — the OS's own `$C000` bank — which under a warm start is exactly
what was found. Writing the loader's scratch 0,1 there would make that path
LESS faithful.

### 10.4 The residual reference movement — measured, explained, NOT regenerated

With the MMU fix in, 10 of 37 rows still differ under `--warm-start`. **No
reference was regenerated.** Two mechanisms account for all of them:

**(a) The ULA palette — issue [#70](https://github.com/jorgegv/jnext/issues/70),
8 rows.** The dominant changed pixel pair is always `219 -> 182` in a channel:
RGB333 level **6** becoming level **5**. `kDefaultUlaRgb333`
(`src/video/palette.cpp:32`) holds level 6 for the non-bright colours; the
firmware writes level 5 (tbblue's classic palette has white at `$B6` = 101 101
10, which expands to 5,5,5). So a warm start makes jnext's ULA palette match
the firmware's, and the committed references — taken on the synthetic
machine — are the side that moves. Rows: `sprite-scaling`, `sprite-anchor`,
`magic-bp-demo`, `magic-port-demo`, `stencil-demo`, `stencil-layers-ula`,
`layers-beast-ula`, `beast-demo`.

This is the **blast radius of #70**, now quantified: fixing that table on the
cold path moves the same eight references.

**(b) Animation phase, 2 rows.** `celeste` (1440 px, 0.9%) and `celeste2` (890
px) render the same scene with small particle/sparkle details in a different
phase. The program starts on a machine whose RAM holds NextZXOS's leftovers
and whose `$0038` handler is NextZXOS's rather than the 48K ROM's — which is
what happens on hardware too. Nothing is wrong with either frame; the phase is
simply not pinned.

### 10.5 What the warm start does NOT fix

`show512` under real `.nexload` shows a **black** background behind its text;
under `--load` — warm or cold — it shows **magenta**, 11.7% of the frame. The
warm start does not close this, and should not be expected to: the cause is a
LOADER gap, not a firmware-state gap. `nexload.asm:396-399` repaints all 256
ULA entries with its own `DefaultPalette` (entry `$18` ends at `$00`, black),
and jnext models no sweep, so the `$E3` that `apply()` writes at ULA index
`$18` (`nexload.asm:269-270`) survives — `$E3` is exactly the magenta measured
(RGB333 7,0,7 = 255,0,255).

This is already a known, deliberately recorded divergence: row
`NEXPR-ALWAYS-08` in `nex_loader_test` "records what jnext does, not what
hardware does". Closing it means modelling the sweep, which moves the
`show512` reference, and is therefore out of scope here.

### 10.6 Answers to the remaining open questions (§9)

2. **Per machine type, per SD image, or both?** Both, plus two more. The
   identity is the SD image's SHA-256, the machine type, a state-format
   version AND the exact state-stream length. The length is not bookkeeping:
   `Ram::load_state` reads a count-prefixed blob straight into the live RAM
   buffer, so refusing any stream whose length is not exactly what this build
   writes is what stops a foreign recording writing past it. The file is named
   per machine type and replaced in place, so a changed image leaves no orphan.
   Whole-image hashing is affordable because a boot does not write: a 600-frame
   NextZXOS boot leaves the image byte-identical (measured).
3. **Opt out?** Inverted: the feature is **opt-in** (`--warm-start`), because
   §7.1's blast radius is real (§10.4) and moving ten committed references is
   the repository owner's decision, not a side effect of adding a mechanism.
   `--warm-start-regenerate` forces a fresh recording.
4. **TAP/TZX/SNA/Z80 too?** No, and not by omission. A snapshot replaces the
   whole machine, so there is nothing for a warm start to contribute. A tape
   is loaded by `LOAD ""` in BASIC, and jnext's fast-load trap is an address in
   the 48K ROM — on a NextZXOS-resident machine that ROM is not the one paged
   in, and NextZXOS does not boot to a BASIC prompt anyway. Warm-starting
   those would break loading, not improve it.
5. **`--machine 48k/128k/plus3`?** Out of scope, loudly: `ensure_warm_start_state()`
   warns "only the Next boots firmware, so there is nothing to record" and
   declines. §7.4 asked for exactly this.

### 10.7 Where the first-run cost shows

A cache miss costs 500 frames of emulated boot — about 3.5 s headless on the
development machine. It is announced on `info` before it starts ("cold-booting
the firmware … This happens once per SD image") and again when it succeeds,
per §7.5. In the regression suite the cache lands in the run's own
`$JNEXT_CONFIG_DIR`, so a full run pays it at most once.

### 10.8 The capture point, in practice

§3 recommended "after NextZXOS reaches idle" and warned (§7.3) against pinning
a PC. What is implemented is a **frame budget plus a positive residency
check**: 500 frames (the `boot-nextzxos-welcome` row pins the welcome screen as
rendered by frame 400, plus slack), then `Emulator::nextzxos_resident()` must
agree or nothing is recorded and the load falls back to the synthetic machine
with an error.

That check asks three questions, and the third is the load-bearing one: the
boot-ROM overlay is off, `nr_03_config_mode` is clear, and a NextZXOS marker
string is present in SRAM ROM pages 0-7. **A machine that never booted answers
the first two exactly as a booted one does** — `init()` commits NR `$03` itself
on the firmware-less path (GH #226) and installs no overlay — so a criterion
that stopped at two would happily record an empty machine and cache it. Row
`WSR-RES-02` is that case.

No keypress is injected to reach the main menu or the command line. Driving a
menu at a fixed frame number is precisely the silent-wrong-capture failure §7.3
warns about, and it turned out to be unnecessary: the loader establishes the
memory map (§10.3) and zeroes bank 5, so what screen NextZXOS happened to be
showing does not reach the program.
