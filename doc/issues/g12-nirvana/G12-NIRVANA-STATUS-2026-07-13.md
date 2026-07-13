# G12 — Nirvana-class attribute mux: status, conclusions, and the open hypothesis

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
