# G12 — Nirvana-class attribute mux: status, conclusions, and the open hypothesis

> ## RESOLVED — 2026-07-13 night (Task 54)
>
> **BIFROST now renders logically pixel-identical to real FUSE (0/49152
> differing ZX colour indices, both demo screens, modulo FLASH phase),
> and Nirvana's bricks render yellow, matching FUSE.** NOTE the
> measurement protocol: a raw RGB diff will NOT read 0 — FUSE renders
> the "normal" intensity at 0xC0 where jnext uses 0xDB (RGB333); both
> use 0xFF for bright. Compare colour indices (quantize both to the
> ZX-16 palette), or map FUSE 0xC0→0xDB and raw-diff (then exactly 0).
> The per-level channel histograms match exactly on both sides (3395
> normal-level / 6678 bright-level samples per frame), so the BRIGHT
> bit agrees at every pixel. Two final bugs, found by measuring
> BIFROST's per-write beam positions against FUSE (`break write` +
> `ula:tstates`, the Task 50 oracle recipe):
>
> 1. **Contention stretch magnitudes were wrong at 5 of 6 phases**
>    (`src/memory/contention.cpp`, on main since G141): the classic
>    `{6,5,4,3,2,1,0,0}` table — FUSE's per-T-STATE pattern, period
>    8 T-states = 16 pixel ticks — was indexed `[hc & 7]` with `hc` in
>    PIXEL ticks, repeating it every 4 T-states. Correct per VHDL
>    (zxula.vhd:582-583 + zxnext_top_issue2.vhd:1024-1140 clock-stretch):
>    48K/128K `{0,0,0,6,6,5,5,4,4,3,3,2,2,1,1,0}[hc & 0xF]`, +3
>    `{1,0,0,7,7,6,6,5,5,4,4,3,3,2,2,1}[hc & 0xF]`. BIFROST is a
>    contention-LOCKED engine: FUSE holds its attribute writes at a
>    constant ts_in_line=80 for 144 straight scanlines; jnext with the
>    wrong magnitudes drifted ~2 T/line and locked 22 T early, pushing
>    the writes for char cols 16-18 across the ULA fetch boundary — the
>    user-visible "attributes one scanline early" band. This was also
>    the root cause of Task 52's "+32 T constant bias". Post-fix jnext
>    locks constant at ts_in_line=91 (residual +11 T vs FUSE — see the
>    Task 52 follow-up), wobble-free.
> 2. **The mux fetch-instant model was 8-12 ticks early and parity-blind**
>    (`AttributeMux::hc_fetch()`): the byte governing column C latches at
>    raw hc `c_min_hactive + 8C` (even C) / `c_min_hactive - 4 + 8C`
>    (odd C) per zxula.vhd:271-306 + :368-455 (sload_0/sload_1 consume
>    abyte00/abyte10 latched 1/5 ticks earlier). Round 4's uniform
>    `origin + col*8` — and its block comment claiming the sload stagger
>    "has no CPU-write-race observable" — were wrong.
>
> §4/§5 below are the pre-resolution analysis, kept for history. The §5
> instruction-boundary hypothesis was already refuted (0.71% of writes);
> the real answers were Task 50 (64-line window displacement) + Task 54
> (this entry).

**Date:** 2026-07-13 · **Branch:** `task8-nirvana` @ `027b9c65` — **NOT merged, do not merge as-is**
**Verdict:** the mechanism works, the *timing model is still wrong*. Both real engines render incorrectly.

> **This document supersedes the original plan**, kept alongside as
> [`task8-nirvana-2026-07-13.md`](task8-nirvana-2026-07-13.md). That plan was written *before* any of
> the findings below and its design assumptions did not survive contact: it predates the no-gate
> decision, the wrong-VRAM-bank bug, and the discovery that both real engines still render wrong.
> Read it for the pre-research (it correctly established that `Emulator::current_hc()` is
> instruction-boundary only while `fuse_z80_writebyte()` / `derive_hc_vc(tstates)` gives true
> per-byte beam position — **which is now the thread to pull on**), not for its conclusions.

---

## 1. What this is

Nirvana / BIFROST\* class multicolour: the CPU rewrites ULA **attribute** bytes *mid-scanline*,
racing the beam, so a character cell shows different attributes on different pixel rows. jnext
renders per-scanline and reads attributes from RAM **at render time**, so writes landing after the
ULA would already have fetched a cell still affect it — the effect renders wrong.

Plan rows: `G12-MUX-01..03` in `test/mmu/mmu_test.cpp` (was 3 skips; the branch expands to ~10 rows).

---

## 2. What was built (on the branch, unmerged)

- `src/memory/attribute_mux.{h,cpp}` — per-scanline change-log + replay, mirroring the established
  `PaletteManager` / `Layer2` / `Ula` change-log pattern already in the codebase.
- `Mmu::write()` hot-path detector: records every write into the attribute window
  (bank 5 page `0x0A` / bank 7 page `0x0E`, offsets `0x1800-0x1AFF`).
- Beam position per write from `fuse_z80_writebyte()` / `derive_hc_vc(tstates)`.
- ULA consumes the mux per scanline; wired into the debugger's per-row replay and into
  `save_state`/`load_state` for rewind.
- Round 4 (`027b9c65`) made resolution **column-accurate**: writes carry `(vc, hc)` and each column
  resolves against its own fetch instant rather than a whole-scanline value.

---

## 3. Conclusions that are SETTLED — do not re-litigate

### 3.1 No gate. Always on. (User decision, with numbers.)

Measured (beast.nex, headless, 2500 frames = 50 s emulated, release build, 3+ runs, noise ±2 s):

| Build | Time | vs main |
|---|---|---|
| `main` (no G12) | 38.5-40.6 s | — |
| Gated (detector on, mux never arms) | 39.8-42.8 s | ~+2-5% |
| **Always-armed (no gate)** | 42.7 s | **~+5-10%** |

**The gate bought nothing measurable.** The cost is the always-on **detector** on every plain-RAM
write; `record_write()` + replay only fire on real attribute writes (~1500-2600/frame) and are
trivial beside millions of instructions. User: *"do not gate it. Do it at all times, and we'll try
to optimize it later in the general optimization and profiling pass. 5% penalty can be assumed."*
→ Optimisation deferred to **Task 27**.

### 3.2 The arm heuristic was hiding a real bug — not optimising anything

The original `repeat >= 4` threshold never engaged on ordinary software, so the broken path was
never exercised. It was tuned until beast.nex stopped changing. **A heuristic tuned until the tests
stop failing is indistinguishable from a bug fix, right up until someone removes the heuristic.**
Removing it exposed the bug in §3.3 immediately.

Also proven: all three real demos would have **failed to arm** under `repeat >= 4` (they write each
band once per cell). The threshold had a false-negative for genuine racing — a silently wrong render.

### 3.3 FIXED — the ULA read the wrong VRAM bank (was a real, VHDL-cited bug)

`Ula::attr_vram_read()` derived its bank selector (`alt`, bank 5 vs 7) from **Timex screen mode
(port 0xFF)**. The VHDL drives ULA bank selection purely from the **7FFD shadow-screen bit**,
independent of Timex — `zxula.vhd:191`, `zxnext.vhd:6649-6656`. beast.nex enables the shadow screen
*without* touching Timex, so the mux read the empty bank 5 → `0x00` everywhere → black paper, black
ink, sky gradient and moon gone.

Fixed by using `vram_use_bank7_` (the shadow bit) for bank selection; `alt` retained only for
sub-window addressing. **`beast-demo` and `layers-beast-ula` are back to 0 pixel diff.**
Evidence: `evidence/jnext-beast-ula-correct.png` vs `evidence/jnext-beast-ula-BROKEN-bank-bug.png`.

### 3.4 The beast.nex reference was CORRECT. It is not stale. Never regenerate it.

Confirmed by inspection: the "mux on" render was visibly corrupt (attribute garbage over the sky),
not a plausible hardware rendering. The reference was right all along.

---

## 4. THE OPEN PROBLEM — both engines still render incorrectly

**As of `027b9c65`, with the column-accurate model and the bank bug fixed:**

- **BIFROST**: ~26-34% of the sprite region differs from the FUSE oracle.
- **Nirvana**: **also wrong** — user's words: *"much better than Bifrost, but it isn't still correct."*
- beast.nex: 0 diff (ordinary software is unaffected — good).

**This kills the reading that "Nirvana passes ⇒ the model is right".** It does not pass. The
`AttributeMux` resolution selects the wrong attribute for both engines; Nirvana merely *looks*
closer.

### Residual structure (measured, ruling out the easy explanations)

Alignment was calibrated on the **plain-ULA text** ("BIFROST\* ENGINE DEMO", "PRESS ANY KEY"), which
the mux does not touch — not on the sprites under test (that would be circular). Exact 4.0000× fit,
crop origin (126, 98) in the 1276×957 oracle.

- **NOT a crop artefact** — ±4 px shift search: (0,0) is strictly the best alignment.
- **NOT an off-by-one column** — no shift improves the match, so the fetch formula is not off by a
  fixed amount.
- **NOT animation phase** — jnext at t=9/11/20 s is byte-identical; the t=10 wobble is 0.3%, two
  orders of magnitude below the 9.7% full-frame gap.
- **NOT periodic** — no every-Nth-row / every-Nth-column pattern.
- **IS**: correct shapes, correct positions, **wrong colour per cell** — jnext shows cyan where FUSE
  shows yellow, yellow where FUSE shows green. Scattered, not a clean permutation.

→ Points at **which recorded write is selected as "current"** for a given (scanline, column), i.e.
the write-timing model — not geometry, not the replay plumbing.

---

## 5. THE HYPOTHESIS TO TEST NEXT SESSION (user has agreed to this)

**jnext's attribute-write timestamps are probably not T-state-accurate.**

If the beam position for a write is derived from the T-state counter at **instruction boundary**
rather than at the exact **write M-cycle**, every write is displaced by up to ~10-20 T-states.
That is sub-scanline — but **wider than a character cell (4 T-states)** — so the write lands in the
wrong cell.

This single hypothesis explains **every** observation:
- BIFROST writes *right at the beam*, cell by cell → a few-T-state error lands in the wrong cell →
  heavy, scattered colour corruption. **Worst hit.**
- Nirvana writes *further ahead of the beam* → the same error usually still lands in the correct
  cell → **looks nearly right but is not**. Matches the user's judgement exactly.
- beast.nex writes attributes without racing → unaffected → **0 diff**.
- No fixed shift and no periodicity fits, because the error depends on *where in the instruction*
  each write falls — it is not a constant offset.

**Where to look:** `fuse_z80_writebyte()` / `derive_hc_vc(tstates)` in `src/cpu/z80_cpu.cpp`, and how
`tstates` advances within an instruction. FUSE's core does track per-M-cycle timing (it is how
contention works) — so the accurate timestamp may already be available and simply not used at the
write site. **Check that first; it may be a small fix.**

**How to verify:** instrument `record_write()` to log `(pc, tstates, vc, hc, addr, val)` and compare
against a known-good per-cell trace, or against FUSE's own contention/timing model for the same
program. Do not fix blind.

---

## 6. Method rules learned the hard way this session (all cost real time)

1. **FUSE is the oracle for BIFROST. ZEsarUX is NOT** — the user verified ZEsarUX renders it
   incorrectly too. CSpect is unvalidated for this. Validating against another wrong renderer
   manufactures false confidence. (Same trap as the `.szx` bug: never validate against something
   that shares your blind spot.)
2. **Crop before diffing.** jnext writes 640×512 (logical 320×256 @2×, border included); FUSE's
   window capture was 1276×957 (~4×). Compare the **256×192 display area only**, both sides
   point-sampled (`-sample`, never an interpolating `-resize` — it invents colours). Sanity-check the
   crop: a 1 px misalignment makes ~100% of pixels "differ" and looks like catastrophe.
3. **Calibrate alignment on something the feature does not touch** (the plain-ULA text), never on the
   thing under test.
4. **A demo passing is not proof the model is correct.** Nirvana "looked right" for two rounds while
   the model was wrong. BIFROST is the discriminating demo; that is why the user supplied all three.
5. Demo NEX/TAP runs take `--machine next|48k --load <file>` and **no `--sdcard`/boot ROM** — see
   `feedback_demo_nex_no_boot_rom`. Booting NextZXOS wastes ~8 emulated seconds and can produce fake
   failures.
6. The user's FUSE screenshots arrive on the **clipboard**, not on disk: `wl-paste --type image/png >
   out.png` retrieves them (this box is Wayland, so `xdotool`/`import` cannot capture windows).

---

## 7. Evidence (in `evidence/`)

| File | What |
|---|---|
| `fuse-bifrost-intro-ORACLE.png` | **The oracle.** FUSE's BIFROST intro, captured by the user. 1276×957. |
| `jnext-bifrost-intro-027b9c65.png` | jnext's same screen. Right shapes, wrong colours. |
| `jnext-nirvana-027b9c65.png` | jnext's Nirvana. Closer, but per the user still not correct. |
| `jnext-beast-ula-correct.png` | beast's ULA layer rendered correctly (sky gradient + moon). |
| `jnext-beast-ula-BROKEN-bank-bug.png` | The same layer under the bank bug — black, garbage. Fixed. |

---

## 8. State to resume from

- Branch **`task8-nirvana` @ `027b9c65`**, unmerged, worktree `.claude/worktrees/task8-nirvana`.
- `main` is green and pushed: unit 4544 / 0 fail / 24 skip (47 suites) · FUSE 1356/1356 ·
  regression 63/0/0 · harness-selftest 26/26.
- The branch's own suite is green **except** that BIFROST/Nirvana are visually wrong — which
  **no automated test currently catches**. That is itself a finding: the three real TAPs
  (`test/00regression/tap/{bifrost,nirvana,nirvanap}.tap`, committed by the user) should become
  regression tests **once the render is correct** — with user authorisation for the reference images.

---

## 9. Task 52 postscript — the residual +11 T lock offset vs FUSE (CLOSED 2026-07-14)

Kept here as the reference for any future "jnext and FUSE disagree by a constant few T-states"
symptom. Task 52 began as "+32 T constant bias on all 8 phases" (found by the Task 50 review);
the bulk of it was the Task 54 wrong-period stretch table. What remained, measured after the fix:

- **jnext locks BIFROST's attribute writes at ts_in_line = 91; FUSE locks at 80. Both are
  perfectly stable — zero wobble over 144 consecutive scanlines.** The offset is a property of
  where each emulator's contention grid lets a contention-locked loop settle, not drift.

Decomposition of the 11 T, and why it is CLOSED rather than fixed:

1. **The grid anchor differs between the two MACHINES, not the two emulators.**
   `zxula.vhd:580-581` (the Next core's own comment): *"the zx next is a synchronous machine and
   decides whether the z80 clock will be allowed to go low in the previous i_hc cycle (contend
   3-14); the original spectrums are combinatorial, they will OR a one into the clock in the
   current cycle (contend 4-15)."* jnext transcribes the Next's gate (3-14). FUSE models the
   ORIGINAL 48K (its classic anchor: first 6-stretch at ts 14335, 1 T before display). At even
   T-state phases the two windows produce the same magnitudes, but the anchor differs by ~1 T —
   a real hardware difference between the Next's 48K mode and an original 48K, not an emulation
   error on either side.
2. **The octet-level component is equilibrium selection.** A contention-locked loop's stable
   phases repeat every 8 T (one contention octet); which equilibrium the engine settles into
   follows from its approach dynamics after the INT. jnext's INT→display distance is 14342 T
   (c_int_h=116 at raw vc 0, per the VHDL transcription) vs FUSE's 14336 T — enough to select a
   neighbouring octet. Again: consistent-with-hardware, not an error.
3. **Nothing visible depends on it.** With Task 54 in place, BIFROST and Nirvana render
   pixel-identical to FUSE (colour-index compare) despite the 11 T offset — the demos' write
   margins are wider than the offset at every fetch boundary.

**Reopen condition:** a demo that races the beam at sub-octet precision AND renders wrong.
If that happens, do NOT tune against FUSE — settle the anchor against **real Next hardware**
(FUSE is the oracle for original-48K behaviour, but the Next's 48K mode is its own machine).

**Diagnostic recipe for "constant small T offset" symptoms** (all proven in Tasks 50/52/54):
1. Instrument the same write on both sides (`break write ADDR` + `print ula:tstates` in FUSE;
   the mux log or gdb on `tstates` in jnext) and compare per-scanline sequences, not averages.
2. A **drifting-then-locking** offset (changes for N lines, then constant) = wrong per-phase
   contention magnitudes (the Task 54 signature). A **constant, wobble-free** offset from the
   first line = grid anchor / INT placement / equilibrium selection (this section).
3. Check the pattern PERIOD before the pattern VALUES: the Task 54 bug was FUSE's 8-T-state
   table indexed with pixel ticks (period 4 T) — every value "looked right" in isolation.

## 10. Task 59 — BIFROST under `--machine next`: single-column +1-scanline offset (CLOSED 2026-07-14, no code change)

**Symptom** (user-filed): running `bifrost.tap` with `--machine next`, one attribute column
shows its colour one scanline early. Not present in 48k / 128k / plus3 modes.

**Reproduction** (frame 300, FLASH phase A, vs `bifrost-screen1-reference.png` — the
48k/FUSE-verified truth):

- `--machine 48k` / `128k` / `plus3`: **0 pixels diff** (all three, measured 2026-07-14).
- `--machine next`: **179 native pixels diff**, ALL inside character column 18
  (pixels x=144–151; the user's "column 20" is the same cell under 1-based/border-inclusive
  counting), scattered scanlines in char rows 1–6 and 8. Every differing cell is the
  "attribute applied one scanline early" pattern.

**What `--machine next` actually runs**: the SD image's own NextZXOS boots and tape-loads the
demo; **NextZXOS itself** commits NR 0x03 machine timing **+3** (tim_sel = TimingPlus3) for
its tape-load environment. Measured in-process at the instant of every col-18 attribute write
(temporary env-gated probe in `fuse_z80_writebyte`, since reverted): timing=+3, 228 T/line,
3.5 MHz (`cpu_speed=0`), contention **enabled** (`NR 0x08 bit 6 = 0`). Per the VHDL this is a
contended configuration: `zxnext.vhd:4481` (`i_contention_en = (not eff_nr_08_contention_disable)
and (not machine_timing_pentagon) and (not cpu_speed(1)) and (not cpu_speed(0))`) and
`zxnext.vhd:4489-4493` (`machine_timing_p3` → banks ≥ 4 contended; the 0x5800 attribute plane
in bank 5 is contended). jnext models exactly these gates.

**Measurement** (write instants at attribute offset 594 = col 18, raw-frame pixel ticks;
column-18 fetch boundary per the Task 54 parity-aware formula `origin + 12 + 8·18`):

| machine | tim_sel | T/line | origin | fetch(18) | locked write hc | margin |
|---------|---------|--------|--------|-----------|-----------------|--------|
| 48k     | 48K     | 224    | 116    | 272       | 282             | +10 → applies next scanline (reference behaviour) |
| 128k    | 128K    | 228    | 124    | 280       | 290             | +10 → next scanline (matches reference) |
| plus3   | +3      | 228    | 124    | 280       | 292             | +12 → next scanline (matches reference) |
| next    | +3      | 228    | 124    | 280       | **276**         | **−4 → applies SAME scanline = 1 scanline early** |

All four locks are perfectly stable — zero wobble across 144+ consecutive scanlines and
across frames. This is the §9 signature: **equilibrium selection**, not contention-magnitude
error (which would drift before locking).

**The decisive comparison is next-vs-plus3**: identical grid physics (same committed tim_sel,
same 228 T line, same contention enable/speed/origin — all measured at write time), yet the
engine settles one full 8-T contention octet apart (276 vs 292, 16 pixel ticks). With
identical per-line physics, both hc values are fixed points of the same per-line iteration
map; which basin the engine lands in is selected by the approach dynamics of the surrounding
software environment — bare +3 BASIC ROM vs the live NextZXOS environment (both are REAL
Z80 code running under the same transcribed physics; jnext does not choose the equilibrium).
Column 18 is the one column whose write margin (±≈8 T in this demo) straddles its fetch
boundary, so it alone tips; every other column has wider margin in both basins.

**Verdict: not a demonstrable jnext bug — no code change.**
1. The physics of the configuration (contention gates, +3 grid, fetch instants) are
   VHDL-transcribed and independently locked by the 48k-mode FUSE pixel-identity
   (bifrost-screen1/2 regression rows, Task 54).
2. Which equilibrium a contention-locked engine selects in a given software environment is
   not predicted by the VHDL alone; per §9 the only oracle for it is **real Next hardware**
   (FUSE is not an oracle for Next-native mode). Nobody has that measurement.
3. BIFROST is documented by its author as requiring **48K timing**; under +3 timing it is out
   of spec, and the fact that jnext's 128k/plus3 modes happen to render reference-identical
   is margin luck, not a guarantee the engine gives.

**Disposition: run BIFROST (and Nirvana-class 48K multicolour engines) in `--machine 48k`
(or 128k/plus3) mode.** The regression rows correctly pin `48k`. Reopen only with a real-Next
hardware capture of this demo loaded through the NextZXOS tape loader; if that capture shows
the plus3-basin rendering, the question becomes "which environment cycle count differs" —
start from the INT-acceptance history after NextZXOS's tape-loader handoff, not from the
contention tables (those are proven by the 48k identity).
