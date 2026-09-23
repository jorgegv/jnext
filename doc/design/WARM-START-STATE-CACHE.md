# Warm-Start State Cache — running NEX files from a real post-boot machine

> Status: **implemented, and it is the behaviour — there is no enable flag**.
> Milestone v1.1. Tracking issue:
> [#234](https://github.com/jorgegv/jnext/issues/234).
>
> §10 below is the implementation record: what the measurement the design
> demanded actually found, including the two places this document was wrong.
> §11 records the follow-up decision that removed `--warm-start` and made the
> recording the default, the compression that came with it, and the ROM
> selection the default surfaced (§11.5, which falsifies §10.8).

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

   It is also **CLI-only**: no `jnext.conf` key, no Preferences checkbox. A
   saved preference is one the user does not re-read, and while §10.4's ten
   references are unresolved the worst place for this switch is a file that
   turns it on for a run nobody remembers configuring. The GUI still honours
   it — a `jnext --warm-start` session that then loads a NEX from **File >
   Load NEX File…** gets the warm machine, because `Emulator::load_nex()`
   reads the same config — so nothing is unreachable from the window, only
   unsaveable.
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

---

## 11. The default (2026-09-23) — and the compressed payload

§10.3 shipped this as opt-in `--warm-start`, on the explicit ground that
moving committed references is the repository owner's decision and not a side
effect of adding a mechanism. That decision has now been made, in the owner's
own words:

> "I did not intend for warm-start to be an option. Just to be a quick way of
> launching NEX files with exactly the same state as NextZXOS+nexload would."

So the flag is gone. §10.6's answer to open question 3 ("Inverted: the feature
is opt-in") is **superseded by this section**.

### 11.1 Why a flag was the wrong shape

An option whose default is "start the program on a machine hardware cannot
produce" has the wrong default. The recorded state is not a mode; it is what
`--load` should always have met. GH #226 is what the other default cost — a
bit no code ever wrote, three dead hotkeys, and months before anyone noticed —
and the whole argument of §1 is that such divergences cannot be enumerated
except by comparison with a real boot. Putting that comparison behind a flag
leaves the enumeration undone for everybody who does not type it.

`--warm-start-regenerate` **stays**, unchanged in meaning: the debug / refresh
lever for a card whose `TBBLUE.FW` or NextZXOS has been replaced and which the
user wants re-recorded deliberately. It reaches only the `.nex` load path, so
a run that cannot get there warns that it has nothing to regenerate rather
than silently doing nothing.

### 11.2 What "falls back loudly" now means

With no flag on the command line there is nothing left to remind a user that a
warm start was even attempted, so every fallback has to say so itself. The
levels were re-chosen on that basis:

| Path | Level | Why |
|---|---|---|
| Non-Next machine (`48k`/`128k`/`plus3`) | `debug` | NOT a refusal. There is no firmware to record, nothing was asked for and nothing was denied. On `warn` it would print on every legacy run forever, which is how a log stops being read. §7.4 asked for "out of scope, loudly"; with the flag gone, loud here is wrong. |
| No SD image mounted | `error` | A Next that could have had a recording and does not. Unreachable from the CLI (`main.cpp` exits first) but reachable from the library and the GUI. |
| SD image cannot be digested | `error` | Same. |
| Recording boot did not end NextZXOS-resident | `error` | Already was. |
| Recording length disagrees with this build | `error` | Already was. |
| Cached state will not deserialise | `error` | Already was. |
| Restored machine is not NextZXOS-resident | `error` | Already was. |
| **A verdict already latched for this image** | `error` (NEW) | The latch stops the second load paying another 500-frame boot; it must not stop the second load being *explained*. Rows `WSR-LATCH-01/02`. |
| Recorded but could not be cached | `warn` | Not a fallback at all — this run got its warm machine; only the next one pays again. |

### 11.3 The payload is deflated; the header is not

Measured on the real recording: **2 293 061 bytes**, of which RAM is 91.5 %
(`Ram::save_state` writes `data_.data()` verbatim and the default RAM is
2048 KB), 89.5 % of the whole file is zero and 88.5 % is zero in runs of >= 256.
Deflate at level 9 takes it to **128 753 bytes — 5.6 %**. (zstd -19 reaches
98 966, and is not worth a new dependency for the last 30 KB of a local cache
file: **zlib is already required** — `find_package(ZLIB REQUIRED)`,
`CMakeLists.txt:151`, used by `rzx.h`, `szx_loader.cpp` and
`sdcard_provisioner.cpp`.)

**The 96-byte header stays plain.** It carries the identity the loader
validates *before* it is willing to trust a byte of the payload. Compressing
it would mean inflating ~2.3 MB of a file not yet shown to be this build's,
this machine's or this card's — expensive work on unvalidated input, with the
output buffer sized from a number read out of that same input. Every refusal
(wrong magic, wrong machine type, wrong digest, wrong plain length,
truncation, empty payload) is answerable from 96 plain bytes, and that
ordering — establish identity, *then* decompress — is what keeps the length
guard meaningful; a guard that runs after the thing it guards is not one.
zlib pulls the same way mechanically: `uncompress()` wants the exact output
size up front, and that number lives in the plain header.

**Two lengths, and they are not allowed to be confused.**

| Offset | Field | Meaning | Compared against |
|---|---|---|---|
| 16 | `plain_bytes` | uncompressed stream length | what THIS BUILD's `save_state` produces |
| 88 | `stored_bytes` | bytes of deflate stream on disk | the file's own size, and nothing else |

The identity guard is the *plain* length, and it is load-bearing exactly as
§10.6 described: `Ram::load_state` reads a count-prefixed blob straight into
the live RAM buffer. `Identity::state_bytes` was therefore **renamed
`plain_bytes`** — the rename is the mechanism, not the documentation: it made
the compiler walk every former use site when the second length appeared,
rather than leaving a name whose meaning had quietly moved. The compressed
length is deliberately NOT in `Identity` at all: it is a property of the zlib
build that wrote the file, not of the machine the recording is of.

Inflation additionally asserts that the stream produces **exactly**
`plain_bytes`. zlib's `uncompress()` refuses a stream wanting more room than
it was given, but is perfectly happy with one that reaches `Z_STREAM_END`
short of the buffer — and a short stream is precisely the shape that
deserialises into the wrong fields instead of failing, since `load_state`
reads a sequence of sized slots.

**Magic bumped, `kFormatVersion` not.** `JNEXTWS1` -> `JNEXTWS2`: the magic's
trailing digit is the FILE-layout generation and the file layout is what
changed. `kFormatVersion` versions the *state stream*, and not one byte of
that stream moved; bumping it would have been a false claim about
`Emulator::save_state` and would have left the next reader unable to tell
which mechanism answers which question. A v1 file fails at the magic — the
first and cheapest check — instead of being handed to the inflater.

### 11.4 The cost, measured

| | |
|---|---|
| synthetic `--load` (unchanged) | 0.11-0.16 s |
| warm `--load`, cache hit | 0.64-0.66 s |
| warm `--load`, cache miss (records) | ~2.7-4.3 s |
| cache file | 128 753 B (was 2 293 061 B) |

The ~0.5 s a cache hit adds is **the SD image's SHA-256**, not the restore
(the compressed file is 126 KB and `load_state` is milliseconds). It is ~1.2 s
when the image is cold in the page cache. That cost is paid deliberately: the
cheap alternatives — a `(size, mtime)` key, a hashed prefix, a named subset of
files — all ACCEPT a cached recording without reading the image, and every one
of them can serve a recording of a different card while reporting success.
That is the worst failure this mechanism can have, because the machine it
produces looks booted (§4, §10.6). Within a process the digest is paid once:
the restored stream stays in `warm_start_state_` for the session, so a GUI
**File > Load NEX File…** after a CLI `--load` costs nothing.

In the regression suite the cache lands in the run's own `$JNEXT_CONFIG_DIR`,
so a full run pays the recording at most once (~3.4 s) plus the per-row digest
on each `--load` row.

### 11.5 What the default SURFACED: the handover leaves 48 BASIC paged

Turning the warm start on for every `--load` moved four references, not two.
Two of them — `magic-bp-demo` and `magic-port-demo` — came out with the text
rendered as **noise**: correct layout, correct line positions, garbage glyphs.
Both read the character set straight out of ROM (`ROM_CHARSET 0x3C00`,
`demo/magic_bp_demo.c`). That is a defect, not a rebase.

**The oracle.** Booting NextZXOS in jnext, choosing *Command Line* and typing
`.nexload b.nex` against a copy of the demo on the card root — the §10.1
method, the real loader on the real OS — renders the text **correctly** and is
**pixel-identical (0 differing pixels) to the committed reference**. So the
reference is right and the warm path was wrong.

**The mechanism**, measured by probing `port_7ffd` / `port_1ffd` /
`current_sram_rom()` / `read(0x3C00)` every 5 frames through that same run:

| Phase | 7FFD | 1FFD | `sram_rom` |
|---|---|---|---|
| NextZXOS splash, menu, command line | `0x00` / `0x07` | `0x00` | **0** (NextZXOS's own ROM) |
| From the program's first instruction | `0x10` | `0x06` | **3** (48 BASIC) |

`nexload.asm` never writes either port — grep it. Its last instruction is
`rst $20` (`nexload.asm:587`), NextZXOS's "leave this dot command and jump to
HL", and **that** handover is what selects ROM 3. It is OS behaviour the
loader inherits rather than performs, which is exactly why
`NexLoader::apply()` does not model it and must not: `apply()` also runs on
the synthetic path, where the SRAM ROM pages hold only the 48K image and the
selection means something different.

The synthetic machine got this right by having no alternative — the 48K image
is the only ROM it has. The recording, taken at the NextZXOS menu, carries the
OS's four-ROM set with **ROM 0** selected, so the program read NextZXOS's code
as font data.

**The fix** is two lines in `Emulator::init_for_load_from_file()`, after the
restore and its residency re-check: set 7FFD bit 4 and 1FFD bit 2, the two
bits that compose the ROM bank (`Mmu::current_rom_bank()`, VHDL
`zxnext.vhd:2994`). Only those two — 7FFD's low bits also select the bank at
`0xC000` (which `apply()` overwrites from the entry bank regardless) and 1FFD
bit 1 is the +3 disk motor, which none of this is about. After it, the
restored machine reads `7FFD=0x10 1FFD=0x04 sram_rom=3`, `0x3C00 = FF FF 00
00` — byte-identical to the oracle's handover state — and both references
match at **0 pixels** again.

It is confined to the warm path on purpose, so the synthetic path stays
byte-for-byte what it was, which is the property §10.1's 37-row measurement
rested on.

**This falsifies §10.8's claim** that "what screen NextZXOS happened to be
showing does not reach the program". It does reach it, through the 128K/+3 ROM
selection. The capture point is still the right one — driving a menu at a
fixed frame remains the failure §7.3 warns about — but the machine the
recording holds is the OS's, and the handover the OS performs on top of it has
to be modelled explicitly rather than assumed away.

Row `G` of `warm-start-func` is the regression: it warm-starts
`magic_bp_demo.nex` and requires 0 pixels against the committed reference. The
`D` row's `tilemap-demo` does not read the ROM and passed throughout, which is
why `G` is a separate run rather than another assertion on the same one.

### 11.6 Reference movement, verified before regeneration

§10.4 measured ten moved rows: eight from the ULA palette and two from
animation phase. The eight are gone — GH #70 landed on `main` (commit
`6d73b61c`, "seed the ULA palette from the boot chain's table, not level 6"),
which moved the cold path onto the same table the firmware writes, so the warm
and cold paths now agree there. The two ROM-font rows of §11.5 were a defect
and are back at 0 px.

That leaves the two §10.4 named: **`celeste` (1440 px) and `celeste2`
(1520 px)**, both animation phase, and the evidence for "animation phase" is:

1. **Every differing cluster is one particle.** In emulated-pixel space the
   changes form 37 (celeste) / 39 (celeste2) 8-connected clusters whose sizes
   are only 4, 8 and 16 — a 2x2 snowflake, two touching ones, a 4x4. There is
   no cluster larger than a single particle sprite anywhere in either frame.
2. **Only the snow palette moves.** All changes swap among five colours: sky
   blue, white, light grey, dark grey, black. The HUD, the timer (`00:00:00`),
   the `100 M` marker, the player sprite and the whole level geometry are
   identical.
3. **It is deterministic.** Two warm runs of each are 0 px apart, so the
   regenerated reference is a stable target rather than a sample.
4. **The reference is not the privileged phase.** Running `celeste` through
   the real chain — boot NextZXOS, Command Line, `.nexload c.nex` — differs
   from the committed reference by 688 px of exactly the same particle
   clusters. The hardware path does not reproduce the synthetic phase either;
   the phase simply is not pinned, because the program starts on a machine
   holding NextZXOS's RAM leftovers, which is what happens on hardware too.

Those two references were regenerated with the owner's explicit sign-off, in a
commit of their own, after confirming that no third row moved.

### 11.7 Still not vendored

Nothing here weakens §4. The recording is still produced on the user's machine
from the image they mounted, still stored under `~/.jnext`, still never
committed and never packaged. Compression changes its size, not its contents:
a 126 KB file holding NextZXOS and DivMMC ROM content is a file holding
firmware exactly as a 2.2 MB one is.

