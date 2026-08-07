# Warm-Start State Cache — running NEX files from a real post-boot machine

> Status: **design proposal**, not implemented. Milestone v1.1.
> Tracking issue: [#234](https://github.com/jorgegv/jnext/issues/234).

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
