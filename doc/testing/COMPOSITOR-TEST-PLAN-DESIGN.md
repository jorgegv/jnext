# Compositor Subsystem — VHDL-Derived Compliance Test Plan

Systematic compliance test plan for the video compositor / layer mixing
subsystem of the JNEXT ZX Spectrum Next emulator, derived exclusively from
the authoritative VHDL source
(`cores/zxnext/src/zxnext.vhd`, stage 2 of the video pipeline).

## Plan Rebuilt 2026-04-14 — RETRACTION OF PRIOR 74/74 CLAIM

The previous revision of this document (see git history) reported a
74/74 test pass rate. **That claim is retracted.** The Task 4 critical
review found the plan and its corresponding test code
(`test/compositor/compositor_test.cpp`) were suffering from three
structural problems that together made the pass rate coverage theatre:

1. **Wrong transparency model.** The test runner used ARGB alpha
   (`0xFF000000` vs `0x00000000`) as the transparency discriminator for
   every layer (see `compositor_test.cpp` line 69: `TRANSPARENT =
   0x00000000; // alpha=0`). The VHDL compositor does **not** look at
   ARGB alpha at all. It compares the upper 8 bits of the 9-bit palette
   output (`*_rgb_2(8 downto 1)`) against `transparent_rgb_2`
   (NR 0x14, 8 bits) for ULA, Tilemap (text mode only) and Layer 2, and
   uses the layer-engine-produced `pixel_en` signal for Sprites, Tilemap
   (non-text) and Layer 2 (empty pixels). Tests that rely on alpha
   therefore cannot detect the class of real bug where a layer pixel
   whose palette output equals NR 0x14 is erroneously treated as opaque,
   nor the inverse.

2. **Anti-tests locking in blend-mode bugs.** Several prior rows
   asserted that the U-layer delivered to blend modes 110/111 was the
   opaque ULA post-stencil RGB. The VHDL at lines 7141–7178 shows the
   blend path for `ula_blend_mode_2 = "00"` uses `ula_mix_rgb`
   (the raw, pre-stencil, pre-ulatm-merge ULA signal) — not
   `ula_final_rgb`. Those tests were pinning the wrong signal.

3. **~70% coverage missing.** The prior plan listed categories but
   never expanded them into per-row preconditions/stimulus/expected.
   It did not cover: the L2 priority bit promoting L2 **above the blend
   result** in modes 110/111 (line 7300, 7342); the non-trivial
   `mix_top/mix_bot` selection per `ula_blend_mode_2` (lines 7141–7177);
   the `ulatm_rgb` selector interaction with `tm_pixel_below_2`
   (line 7116); stencil AND semantics (line 7113); the "border
   exception" condition clause shared across modes 011/100/101; and the
   9-bit RGB extension `fallback_rgb & (fallback_rgb(1) or
   fallback_rgb(0))` on line 7214.

This rebuild derives every expected value from a specific line in
`zxnext.vhd`. No expected value is taken from the C++ implementation.

**Current status (2026-04-28, Task 8 t1):** compositor_test 140/136/0/4 (97%);
compositor_integration_test 7/7/0/0 (100%). Net 9 SKIP closures this session:

* **G108 (PFF-G108-01/02/03)** re-homed to compositor_integration_test
  with full Emulator harness. New `Emulator::port_ff_reg_` field +
  fan-out wiring in NR 0x69 (b5:0), NR 0x22 (b2), NR 0xC4 (b0
  inverted) handlers; port-0xFF write latches whole byte.
  Author of plan-comment correction at NR 0xC4 handler. 4 rows added
  (G108-01/02/02b/03/04 — last covers the port-FF priority).

* **G27 (BLANK-G27-01)** converted from skip() to a passing
  combinational-lockstep witness: the jnext compositor model is
  combinational, so `rgb_blank_n_6` vs `rgb_blank_n` cannot drift.
  Test pins the invariant via two adjacent columns (active/blank
  stylised edge) each picking up their own layer state.

* **G93 (L2P-19)** converted from skip() to a passing native-640
  witness: when L2 is rendered natively at 640px, both even and odd
  pixel columns observe priority promotion. Tests
  `composite_scanline(width=640)` with distinct stimuli.

* **G04 (PSCAN-G04-01)** — NR 0x14 transparent_rgb per-scanline
  snapshot landed on Renderer (`transparent_rgb_per_line_`); G04-02
  (NR 0x4B) and G04-03 (NR 0x4C) deferred to Sprites/Tilemap suites
  per cross-bucket gap-doc ownership.

* **G11 (PSCAN-G11-01/02/03)** — NR 0x68 b0 (stencil), b6:5
  (blend mode), b3 (ULA+) per-scanline snapshots landed on
  Renderer / Ula. Standard snapshot/init/getter idiom matching
  `fallback_per_line_` / `ula_enabled_per_line_` pattern.

* **G26 (UB-G26-01/02)** remain skip() — blocked on FPGA-team
  oracle confirmation (see Open Questions §1, §2 below). No code
  change is appropriate until the semantic question is resolved.

* **2026-07-13 (Task 43 + Task 45) — per-line snapshot *consumption*
  now covered, not just bookkeeping.** `stencil_mode_per_line_` /
  `blend_mode_per_line_` (Task 43) and `transparent_rgb_per_line_`
  (Task 45) each existed with a snapshot/init/getter trio but zero
  call sites in `composite_scanline`, so a mid-frame Copper MOVE to
  NR 0x68 b0/b6:5 or NR 0x14 took effect one scanline too early. Fixed
  in `src/video/renderer.cpp`/`src/core/emulator.cpp`; new deferral
  rows **TR-50/51**, **STEN-20/21**, **UTB-50/51** (Group TR/STEN/UTB
  below) prove the fix — see the note above Group BLANK for how they
  differ from the pre-existing PSCAN-G04-01/G11-01/G11-02 bookkeeping
  rows. `compositor_test` 187→189.

* **2026-07-13 (Task 46) — sixth and last instance of the same bug
  class, and the only one with a user-visible rendering consequence.**
  `Layer2::render_scanline` read a SEPARATE live member
  (`PaletteManager::global_transparency()`), not
  `Renderer::transparent_rgb_per_line_`, so it did not benefit from
  Task 45's fix at all. Worse than TR-50/51's case: Layer2
  *skip-writes* transparent pixels (`layer2.cpp`'s `continue` paths
  leave `dst[]` at the caller's TRANSPARENT sentinel), so a pixel
  Layer2 wrongly judged transparent during the stale-live window was
  DESTROYED before `composite_scanline`'s own NR 0x14 check
  (`renderer.cpp`'s `l2_transp` clause) ever saw it — that check can
  only narrow an already-written pixel to transparent, never recover
  one that was never written. Fixed by giving
  `Layer2::render_scanline` a mandatory `transparent_rgb` parameter
  and having `Renderer::render_row` pass
  `transparent_rgb_for_line(row)`. New rows **TR-52/53** (Group TR
  below) exercise the REAL `Layer2` via `Renderer::render_row` (not a
  hand-set `layer2_line_`), because the bug is one pipeline stage
  before `composite_scanline`.

  Also proved (exhaustive property check, all 512 RGB333 palette
  values × all 256 NR 0x14 values, 0 violations) that once Layer2 and
  the compositor read the SAME snapshot, `renderer.cpp`'s
  `(l2_px & 0xFFFFFF) == nr14_rgb` redundant check is provably
  unreachable whenever `is_transparent(l2_px)` is false — documented
  in place, not deleted (harmless, and a future change to either
  RGB-expansion function would silently resurrect a real bug if the
  guard were gone). **Independent review flagged that the initial
  check was a one-off scratch calculation with no committed
  artefact** — a comment cannot enforce an invariant. Committed as a
  permanent **L2EQ-01** row (Group L2EQ below) that re-derives the
  proof through the real production API
  (`PaletteManager::write_9bit`/`layer2_rgb8`/`layer2_colour`,
  `Renderer::rrrgggbb_to_argb`); re-running it under review caught a
  bug in the *test's own* first draft (an over-claimed biconditional
  that does not actually hold — the converse direction is genuinely
  false at `r3=g3=0, b3=1, X=0x00`) before it could ship, then
  mutation-confirmed the corrected one-directional row fails when
  either expansion function is deliberately broken.

  **Also, `Layer2::render_scanline_debug` initially shipped an
  INCORRECT architectural claim** — that the debugger's Layer 2
  panel views had "no meaningful per-line snapshot to defer to" and
  should therefore read the live NR 0x14 value on purpose. Independent
  review found this false: `src/debugger/video_panel.cpp`'s
  `replay_rewind()`/`replay_line()` already perform a genuine per-row
  replay of the paused frame for everything Layer2 itself owns
  (bank/scroll/clip); NR 0x14 was simply the one input this function
  still took from the live register instead of from that replay.
  Fixed by threading `transparent_rgb` through
  `render_scanline_debug` too and having the panel pass
  `Renderer::transparent_rgb_for_line(row)` at both the
  `LAYER2_ACTIVE` and `LAYER2_SHADOW` call sites — mirroring the
  `BACKGROUND` view's pre-existing `fallback_for_line(row)` read.
  Regression-guarded by new **DVP-21/21a/21b** in
  `test/debugger/video_panel_test.cpp` (`debugger_video_panel_test`
  80→83) — see that file's own header comment for detail; not
  duplicated here since it is a debugger-panel suite, not this one.

  `compositor_test` 189→192 (TR-52, TR-53, L2EQ-01).

* **2026-07-23 (GH #73) — NR 0x15 per-scanline replay (G02) WIRED; was
  dormant.** `Renderer::write_nr15` / `start_frame_nr15` /
  `apply_changes_for_line_nr15` existed with ZERO callers — only the
  change-log type had been built, never the wiring, so a mid-frame
  NR 0x15 write (layer priority b4:2 / sprite enable b0) collapsed to
  the frame-end value. Now wired end to end: the NR 0x15 dispatcher
  calls `write_nr15`, `Emulator::begin_new_frame` calls
  `start_frame_nr15`, `on_scanline` tags, and `render_frame` does the
  standard rewind/apply/flush. The line-by-line audit of the dormant
  code found it was missing the vblank drain every sibling log has —
  `flush_remaining_changes_nr15()` added (a one-time NR 0x15 write
  during vblank was otherwise lost forever). The sprite render gate in
  `render_row` now consumes the per-line-replayed `sprite_en_` instead
  of the engine's end-of-frame live flag (VHDL zxnext.vhd:6819/
  6906-6907/6934/7118 — sprites.vhd has no b0 input; the pixel gate
  lives in the video pipeline). New Group PSCAN sub-group **G02**
  rows PSCAN-G02-01..05 (see below) drive the real `render_frame` end
  to end; each was mutation-tested (unwired apply / zeroed baseline /
  removed flush / reverted gate / reordered cap check — each target
  row red, restore green). `compositor_test` 225→230.

* **2026-07-24 — the last 3 skips resolved; suite at 0 skips.** All
  three were `LR` rows owned by
  [LORES-TEST-PLAN-DESIGN.md](LORES-TEST-PLAN-DESIGN.md) (group 7/8
  there for the full record). **LR-140** skip→check: the "unobservable"
  ruling was a jnext gap, not a hardware unreachability — the missing
  `ula_select_bgnd` consumer (zxnext.vhd:6986-6991) was implemented in
  `Ula`'s ULAnext render branches, with `Renderer::render_row` pushing
  the per-line NR 0x4A ARGB; the live row asserts the LoRes bypass and
  carries a stimulus-validity guard leg (both legs mutation-tested:
  consumer reverted → guard leg red; LoRes substitution suppressed
  under ULAnext → LoRes leg red; restore → green). **LR-127/LR-128**
  retired-row tombstone `skip()`s removed under the owner-authorized
  zero-skip mandate; the retirement record lives in the LoRes plan.
  `compositor_test` 230→228 (2 tombstone rows gone; LR-140 now counted
  as pass), 0 fail, **0 skip**.

---

**Pre-Task 8 baseline (2026-04-17, commit `3fda139`):** **114/114 pass (100%), 0 fail, 0 skip.**
All 15 previously-failing tests fixed this session:
- (3) Stencil mode (NR 0x68 bit 0) implemented with tm_enabled gate.
- (5) L2 palette bit 15 priority promotion added across all 8 modes.
- (6) Border exception for modes 011/100/101 with correct 3-way VHDL condition.
- (7) NR 0x15[0] global sprite_en gating at compositor boundary.
- BL-27 test oracle corrected (VHDL 7314 gates sub formula on mix_rgb_transparent).
- Save/load state extended for sprite_en_, stencil_mode_, tm_enabled_.
- Per-scanline clear + hi-res doubling for layer2_priority_ and ula_border_ arrays.

**Known runtime wiring gaps** (compositor logic correct, data pipeline incomplete):
- layer2_priority_[] not populated at runtime (PaletteManager discards bit 15).
- ula_border_[] not populated at runtime (ULA render path doesn't mark border pixels).
- Both are driven directly by tests; full wiring deferred to a follow-up task.

### NR 0x15 bit-field clarification

The task brief claimed "NR 0x15 bits 2:0" control the priority mode.
**This is incorrect according to VHDL** and is noted here to prevent
future confusion. The authoritative mapping is `nr_wr_dat(4 downto 2)`
(`zxnext.vhd:5232`), stored into
`nr_15_layer_priority` (`zxnext.vhd:1141`), which is a
`std_logic_vector(2 downto 0)`:

| NR 0x15 bits | Function | Source |
|--------------|----------|--------|
| 7 | `nr_15_lores_en` (LoRes enable) | `zxnext.vhd:5229` |
| 6 | `nr_15_sprite_priority` (sprite zero on top) | `zxnext.vhd:5230` |
| 5 | `nr_15_sprite_border_clip_en` | `zxnext.vhd:5231` |
| 4:2 | `nr_15_layer_priority` (3-bit mode selector) | `zxnext.vhd:5232` |
| 1 | `nr_15_sprite_over_border_en` | `zxnext.vhd:5233` |
| 0 | `nr_15_sprite_en` (global sprite enable) | `zxnext.vhd:5234` |

The 8 encodings of the 3-bit mode are:

| `layer_priorities_2` | Mnemonic | Top → Bottom | VHDL `case` line |
|----------------------|----------|--------------|------------------|
| `000` | SLU | Sprite → Layer 2 → ULA | 7218 |
| `001` | LSU | Layer 2 → Sprite → ULA | 7230 |
| `010` | SUL | Sprite → ULA → Layer 2 | 7240 |
| `011` | LUS | Layer 2 → ULA → Sprite | 7252 |
| `100` | USL | ULA → Sprite → Layer 2 | 7262 |
| `101` | ULS | ULA → Layer 2 → Sprite | 7274 |
| `110` | Blend add | L2 + U additive (clamped to 7) | 7286 |
| `111` | Blend sub | L2 + U − 5 (clamped to [0,7]) | 7312 |

## Out-of-scope (delegated to other subsystem plans)

The following NR 0x15 bits and ULA sub-modes affect pixels that arrive at
the compositor but their **internal semantics** belong to other subsystem
test plans. The compositor plan only verifies that the signal crosses the
boundary correctly (see the explicit boundary rows in the PRI-BOUND and
TR groups).

| Feature | Delegated to | Reason |
|---------|--------------|--------|
| NR 0x15 bit 7 `nr_15_lores_en` (LoRes enable) | ULA / LoRes plan | LoRes is folded into the ULA pixel path before stage 2; the compositor only sees `ula_rgb_2`. See Open Question 3 and 5 below. |
| NR 0x15 bit 6 `nr_15_sprite_priority` (sprite-0 on top among sprites) | Sprites plan | Resolved inside `sprites.vhd` before `sprite_pixel_en_o` reaches the compositor. |
| NR 0x15 bit 5 `nr_15_sprite_border_clip_en` | Sprites plan | Gates the sprite clip window inside `sprites.vhd`; compositor only sees the resulting `sprite_pixel_en_2`. |
| ULA hi-res mode (NR 0x11, timing 256×192 at doubled pixel clock) | ULA plan | Produces a regular `ula_rgb_2` stream; compositor is resolution-agnostic at the ULA input boundary. |
| ULA hi-colour / ULAplus / ULANext attribute modes | ULA plan | Produce a regular `ula_rgb_2` stream through the ULA/TM shared palette; compositor sees only the post-palette 9-bit RGB. See boundary row TR-15. |
| Sprite transparent-index comparison (NR 0x4B) | Sprites plan | Comparison is inside `sprites.vhd:1067`; compositor verified at TRI-10/TRI-11 only. |
| Tilemap transparent-index nibble (NR 0x4C) | Tilemap plan | Comparison is inside `tilemap.vhd`; compositor verified at TRI-20 only. |

## Authoritative VHDL Source

All line numbers refer to `cores/zxnext/src/zxnext.vhd` unless stated
otherwise.

| Stage | Lines | Function |
|-------|-------|----------|
| Stage 0 | 6730–6832 | Per-scanline parameter capture |
| Stage 0.5 | 6834–6934 | Pixel data holding |
| Stage 1 | 6936–7005 | Dual-port palette RAM lookup |
| Stage 1.5 | 7006–7089 | Parameter propagation |
| Stage 2 | 7091–7356 | Transparency, SLU ordering, blend |
| Stage 3 | 7358–7415 | Output delay, blanking |

Supporting VHDL:
- `cores/zxnext/src/video/layer2.vhd` — Layer 2 pixel_en production.
- `cores/zxnext/src/video/sprites.vhd` — Sprite pixel_en production, including
  the sprite transparent-index comparison (NR 0x4B) at line 1067 and the
  clip/over-border gating.
- `cores/zxnext/src/video/tilemap.vhd` — Tilemap pixel_en and pixel_below
  production, text-mode attribute.
- `cores/zxnext/src/video/lores.vhd` — LoRes pixel generation (multiplexed
  into the ULA pixel data path).

## Transparency Oracle (The Historical Bug Epicentre)

There are **two distinct transparency mechanisms** in the Next video
pipeline and they must not be confused:

### 1. Index-based transparency (inside layer engines, pre-palette)

For sprites and tilemaps, the transparent **index** registers are
compared against the raw pixel index before palette lookup. These
comparisons live in `sprites.vhd` / `tilemap.vhd` and feed the
`pixel_en_o` output of those layers. Relevant NextREGs:

| NR | Signal | Comparison | Line |
|----|--------|------------|------|
| 0x4B | `nr_4b_sprite_transparent_index` (8 bit) | vs sprite index | `zxnext.vhd:4339` ties it into sprites.vhd `transp_colour_i` |
| 0x4C | `nr_4c_tm_transparent_index` (4 bit) | vs tilemap index (nibble) | `zxnext.vhd:4395` |

The sprite engine also uses the clip window and over-border gate to
produce `pixel_en_o` (`sprites.vhd:1067`). Layer 2 produces pixel_en
from its own bank mapping logic (see Layer 2 test plan).

### 2. RGB-based transparency (inside compositor, post-palette)

For ULA, Tilemap in **text mode**, and Layer 2, a second
transparency check compares the **upper 8 bits** of the 9-bit palette
output RAM word against the 8-bit NR 0x14 value
(`nr_14_global_transparent_rgb`, default `0xE3`):

| Layer | VHDL line | Formula |
|-------|-----------|---------|
| ULA (mix) | 7100 | `(ula_rgb_2(8 downto 1) = transparent_rgb_2) OR ula_clipped_2` |
| ULA (final) | 7103 | `ula_mix_transparent OR NOT ula_en_2` |
| Tilemap | 7109 | `(NOT tm_pixel_en_2) OR (tm_pixel_textmode_2 AND (tm_rgb_2(8 downto 1) = transparent_rgb_2)) OR (NOT tm_en_2)` |
| Sprite | 7118 | `NOT sprite_pixel_en_2` (no RGB compare — the index compare already ran upstream) |
| Layer 2 | 7121 | `(layer2_rgb_2(8 downto 1) = transparent_rgb_2) OR (NOT layer2_pixel_en_2)` |

Bit 0 of `*_rgb_2` (the synthesised LSB from the palette 9-bit
extension) is **not** compared. Tests must therefore distinguish two
cases that the prior alpha-based tests cannot:

- **Case A.** Palette output upper 8 bits equal NR 0x14 → **transparent**
  regardless of how the test runner synthesises "alpha".
- **Case B.** Palette output upper 8 bits differ from NR 0x14 → **opaque**
  even if the pixel was produced at a palette entry whose corresponding
  ARGB would have alpha=0 on the host platform.

### ARGB alpha plays no role

The ARGB32 surface output used by the JNEXT renderer is a convenience
of the host graphics stack. The VHDL compositor operates on 9-bit RGB
(R3G3B3 plus a synthetic LSB). Any test that asserts behaviour by
manipulating the alpha channel of a source buffer is asserting
emulator-internal convention, **not** VHDL semantics, and must be
replaced.

## Pipeline Architecture

### Layer sources entering Stage 2

| Source | Pixel signal | Palette RAM | Has RGB-compare transp? | Has index-compare transp? |
|--------|--------------|-------------|-------------------------|---------------------------|
| ULA    | `ula_pixel_1`    | ULA/TM shared | Yes (vs NR 0x14) | No |
| Tilemap| `tm_pixel_1`     | ULA/TM shared | Yes in text mode (vs NR 0x14) | Yes (vs NR 0x4C, in tilemap.vhd) |
| Layer 2| `layer2_pixel_1` | L2/Sprite shared | Yes (vs NR 0x14) | Yes (pixel_en from layer2.vhd) |
| Sprite | `sprite_pixel_1` | L2/Sprite shared | No | Yes (vs NR 0x4B, in sprites.vhd) |
| LoRes  | (replaces ULA)   | ULA/TM shared | Via ULA path | No |

### ULA/Tilemap merge — `ula_final_rgb`

Two NR 0x68 configuration bits control the ULA↔Tilemap merge before
the result enters the SLU selector:

- `ula_stencil_mode_2` (NR 0x68 bit 0)
- `ula_blend_mode_2` (NR 0x68 bits 6:5)

From `zxnext.vhd:7125–7137` the stencil path is:

```
if stencil_mode AND ula_en AND tm_en:
    ula_final_rgb  = stencil_rgb          -- (ula_rgb AND tm_rgb)
    ula_final_transparent = stencil_transparent
else:
    ula_final_rgb  = ulatm_rgb
    ula_final_transparent = ulatm_transparent
```

where (line 7116):

```
ulatm_rgb = tm_rgb   when (NOT tm_transparent) AND (NOT tm_pixel_below_2 OR ula_transparent)
          = ula_rgb  otherwise
ulatm_transparent = ula_transparent AND tm_transparent
```

### `mix_rgb` / `mix_top` / `mix_bot` selection

From `zxnext.vhd:7139–7178`, `ula_blend_mode_2` picks what feeds the
SLU mixer's U position and what floats as `mix_top`/`mix_bot`:

| blend | `mix_rgb`      | `mix_top` (above S) | `mix_bot` (below S) |
|-------|----------------|---------------------|---------------------|
| 00    | `ula_mix_rgb`  | `tm_rgb` gated by `tm_pixel_below_2=0` | `tm_rgb` gated by `tm_pixel_below_2=1` |
| 01    | `"0" & transp` | `ula_rgb` if `tm_pixel_below_2=0`; `tm_rgb` otherwise | `tm_rgb` if `tm_pixel_below_2=0`; `ula_rgb` otherwise |
| 10    | `ula_final_rgb`| always transparent  | always transparent |
| 11    | `tm_rgb`       | `ula_rgb` gated by `tm_pixel_below_2=1` | `ula_rgb` gated by `tm_pixel_below_2=0` |

Note: modes 01/11 are the *inversions* — TM replaces ULA in the U
slot, and ULA appears as a top/bot insertion — the prior plan got
these the wrong way around in table UTB-05/UTB-06.

### Border exception (modes 011 / 100 / 101)

Shared clause at lines 7256, 7266, 7278:

```
ula_final_transparent = '0' AND NOT (ula_border_2 = '1' AND
                                     tm_transparent = '1' AND
                                     sprite_transparent = '0')
```

That is: ULA is treated as non-transparent **unless** the current
pixel is in the border region, the tilemap contributes no pixel, and
an opaque sprite is present. In that one case ULA is suppressed so the
sprite may show through. This prevents the border colour from hiding
sprites in priority orderings where ULA sits above S.

### Layer 2 priority promotion bit

From `zxnext.vhd:7123`:

```
layer2_priority = layer2_priority_2   when layer2_transparent = '0'
                = '0'                 otherwise
```

and the case blocks at 7220, 7242, 7264, 7276, 7300, 7342. The L2
priority bit is sampled from palette entry bit 15 (see Layer 2 test
plan). It promotes L2 to the topmost position in modes **000, 010,
100, 101, 110, 111**. It has no effect in 001 or 011 (where L2 is
already at the top of the stack). In blend modes 110/111 it
**overrides the blend** and shows the blend-combined RGB as the top
pixel (lines 7300–7301, 7342–7343).

### Fallback colour

From `zxnext.vhd:7214` the 9-bit fallback RGB is built as:

```
rgb_out_2 <= fallback_rgb_2 & (fallback_rgb_2(1) or fallback_rgb_2(0))
```

i.e. the NR 0x4A 8-bit value is concatenated with a synthetic LSB
formed from `OR` of the two low bits of blue. Default `0xE3`
(`zxnext.vhd:` reset clause for `nr_4a_fallback_rgb`). The default is
only visible when **all** layer branches in the selected priority case
failed to assign `rgb_out_2`. Note: the fallback is also used inside
ULA background substitution (`zxnext.vhd:6987–6991`, different
mechanism).

## Retractions from Prior Plan

Rows removed and reasons. These must be replaced by the corresponding
new rows in the Test Case Catalog below.

| Old row | Problem | Replacement |
|---------|---------|-------------|
| TR-01..TR-14 (alpha-driven) | Used ARGB alpha as transparency discriminator; does not exercise RGB-vs-NR 0x14 comparison (line 7100/7109/7121). | New TR-1x and TRI-2x series splitting the two transparency mechanisms. |
| FB-03 (`0x4A & bit0=1 -> 0x095`) | Arithmetic wrong: `fallback(1) OR fallback(0)` of `0x4A = 0100 1010` yields bit 1=1, so LSB=1; correct 9-bit is `0x4A<<1 \| 1 = 0x095`. The number is right but derivation was opaque. | FB-3x row showing the bit-level derivation. |
| PRI-xx (alpha opaque everywhere) | Couldn't distinguish "layer pixel matching NR 0x14" from "layer pixel missing". | PRI-1x rewritten with explicit NR 0x14 values. |
| L2P-04 (L2 priority wins vs ULA in mode 100 USL) | Correct direction, but prior test asserted it with ULA alpha=0xFF and L2 alpha=0xFF; same in mode 100 ULA already wins over L2 without the priority bit, so the test did not actually exercise promotion. | L2P-2x rewritten with a sprite opaque above ULA so the promotion path is actually taken (L2 bit makes L2 beat S). |
| BL-09 ("ULA transparent → no subtraction") | Tautological — mode 111 subtraction is gated on `mix_rgb_transparent`, so if ULA (or the chosen blend source depending on `ula_blend_mode`) is transparent the subtraction path is skipped **but L2 still contributes unchanged only if another branch chose L2**. The old row said "no subtraction applied" which is ambiguous. | BL-4x rewritten against line 7314. |
| UTB-05 / UTB-06 (mode 01 with `below`) | Had `mix_top`/`mix_bot` assignments reversed — mode 01 mix_rgb is forced transparent (line 7152 in original, actually 7149–7155), and `ula_rgb`/`tm_rgb` flip on `tm_pixel_below_2`. Old table had below=0 and below=1 rows swapped. | UTB-3x and UTB-4x corrected against lines 7149–7178 (plus the `others =>` branch at 7163–7177). |
| "LINE-01..04" (per-line capture) | Tautological: the old rows said "change mid-line → old value until next line" without ever defining what happens if the change falls **exactly** at the horizontal blanking boundary. | LINE-1x with explicit pixel-column preconditions. |
| SOB-01/02 (sprite-over-border) | Tests were asserting at the **compositor** level what is actually a **sprite engine** decision (`sprites.vhd:1067`, which ANDs `over_border_s` into `pixel_en_o`). This plan removes them and delegates the semantics to the Sprites test plan, keeping only the compositor-side integration row SOB-1x. | See SOB-1x. |
| Any "return value matches emulator output for reset state" row | Tautological comparison against C++. | Removed. Reset defaults are cited against VHDL reset clauses and verified positively via the behavioural tests, not by reading the state back. |

## Test Case Catalog

Legend for every row: ID — Title — Preconditions — Stimulus — Expected
— VHDL citation. Expected values are derived from VHDL line(s); where
derivation is non-obvious, the arithmetic is shown inline.

### Group TR — RGB-based transparency comparison (compositor layer)

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| TR-10 | ULA pixel with palette output ≠ NR 0x14 is opaque | NR 0x14=0xE3, NR 0x15[4:2]=000, L2/S/TM transparent | ULA pixel → palette entry RGB[8:1]=0x10 | `rgb_out_2 = ula_rgb_2` | 7100, 7104, 7226 |
| TR-11 | ULA pixel with palette output = NR 0x14 is transparent; fallback wins | NR 0x14=0xE3, all layers transparent otherwise | ULA pixel → palette entry RGB[8:1]=0xE3 | `rgb_out_2 = fallback_rgb_2 & extLSB` | 7100, 7214 |
| TR-12 | ULA palette entry whose LSB differs from NR 0x14 LSB is still transparent | NR 0x14=0xE3 | ULA palette entry 9-bit value `"11100011" & "0"` and `"11100011" & "1"` | both transparent | 7100 (only 8:1 compared) |
| TR-13 | `ula_clipped_2=1` forces ULA transparent regardless of RGB | NR 0x14=0x00, ULA pixel RGB=0xFF | set `ula_clipped_2=1` | ULA transparent | 7100 |
| TR-14 | `ula_en_2=0` forces ULA transparent even if mix_transparent=0 | — | Disable ULA (NR 0x68 bit 7=1) | `ula_transparent=1`, ULA does not win | 7103 |
| TR-15 | Compositor is resolution-agnostic at the ULA input boundary (hi-res / hi-colour / ULANext) | mode 000, L2/S/TM transparent, NR 0x14=0xE3 | Drive `ula_rgb_2`=0x1AA from the ULA path regardless of whether the ULA is in standard, hi-res, hi-colour or ULANext mode | `rgb_out_2 = 0x1AA` — stage 2 only consumes the post-palette 9-bit word; any hi-res/hi-colour semantics are resolved upstream of `ula_rgb_2` | 7100, 7104, 7226 |
| TR-16 | NR 0x14 = 0x00 with ULA palette output `RGB[8:1]=0x00` → ULA transparent (match succeeds) | NR 0x14=0x00, mode 000, L2/S/TM transparent, NR 0x4A=0x10 | ULA palette entry 9-bit = `"00000000" & "0"` | ULA transparent → fallback wins; `rgb_out_2 = 0x10<<1 \| 0 = 0x020` (bit1=0, bit0=0 → synthetic LSB=0) | 7100, 7214 |
| TR-17 | `ula_border_2` is ignored by stage-2 mix in modes 000/001/010 (only ULA's `ula_final_transparent` feeds the SLU selector) | mode 000, U opaque, `ula_border_2=1`, `ula_border_2=0` | Toggle `ula_border_2` with ULA identically opaque in both cases; L2/S/TM transparent | `rgb_out_2` identical for both — stage 2 branches at 7218–7248 reference `ula_final_transparent` only; the border exception clause is attached to `ula_final_transparent` exclusively at lines 7256/7266/7278 (modes 011/100/101), not at 7220/7232/7244 | 7218–7248, 7256, 7266, 7278 |
| TR-42 | NR 0x15[0] `nr_15_sprite_en = 0` forces every sprite-origin pixel transparent at the compositor output | mode 000, S palette RGB=0x1CC (opaque), L2/U transparent, NR 0x4A=0xE3 | Write NR 0x15 bit 0 = 0 while keeping a sprite that would otherwise produce `sprite_pixel_en=1` | `sprite_pixel_en_1 = sprite_pixel_en_1a AND sprite_en_1 = 0` → `sprite_pixel_en_2=0` → `sprite_transparent=1` at compositor → fallback wins (`rgb_out_2 = 0x1C7`), regardless of the sprite engine's internal pixel_en | 6934 (AND with `sprite_en_1`), 6819 (latch from NR 0x15[0]), 7118, 7214 |
| TR-20 | Tilemap text-mode RGB compare | NR 0x14=0xE3, tm_pixel_en=1, tm_textmode=1 | TM palette entry RGB[8:1]=0xE3 | tm_transparent=1 | 7109 |
| TR-21 | Tilemap non-text (attribute) ignores RGB compare | NR 0x14=0xE3, tm_pixel_en=1, tm_textmode=0 | TM palette entry RGB[8:1]=0xE3 | tm_transparent=0 (opaque) | 7109 (middle clause gated on textmode) |
| TR-22 | Tilemap `pixel_en=0` transparent regardless of mode | — | tm_pixel_en=0, textmode=0, RGB=0x10 | tm_transparent=1 | 7109 |
| TR-23 | `tm_en_2=0` forces TM transparent | — | Disable TM (NR 0x6B bit 7=0) | tm_transparent=1 | 7109 |
| TR-30 | Layer 2 RGB compare | NR 0x14=0xE3, l2_pixel_en=1 | L2 palette entry RGB[8:1]=0xE3 | layer2_transparent=1 | 7121 |
| TR-31 | Layer 2 `pixel_en=0` transparent | — | l2_pixel_en=0 | layer2_transparent=1 | 7121 |
| TR-32 | Layer 2 opaque pixel with non-zero `layer2_priority_2` propagates | l2 not transparent | palette bit 15 set | `layer2_priority=1` | 7123 |
| TR-33 | Layer 2 priority forced to 0 when layer is transparent | palette bit 15 set, l2_pixel_en=0 | — | `layer2_priority=0` (even though bit 15=1) | 7123 |
| TR-40 | Sprite `pixel_en=0` transparent | — | sprite_pixel_en_2=0 | sprite_transparent=1 | 7118 |
| TR-41 | Sprite `pixel_en=1` opaque regardless of NR 0x14 | NR 0x14=any | sprite_pixel_en_2=1, sprite_rgb_2=0x1C6 | sprite_transparent=0 | 7118 (no RGB compare) |
| TR-50 | **Deferral** (Task 45): a mid-frame NR 0x14 write does not retroactively affect a row whose per-line snapshot already ran | Row 13's `transparent_rgb_per_line_` snapshot captured with NR 0x14=0xE3 (reset default); mode 000, opaque ULA pixel RGB=0xAA, all other layers transparent | Write NR 0x14 ← 0xAA (now matches the ULA pixel's RGB) but do **not** re-snapshot row 13; composite row 13 directly via `composite_scanline` (bypasses `composite_one()`'s auto-sync — see the deferral-methodology note in Group PSCAN) | Row still shows the OLD snapshot (0xE3 ≠ 0xAA) — ULA pixel stays opaque and wins | 1137, 5226, 6822, 6912–6913, 7078, 7100 |
| TR-51 | **Deferral** (Task 45): the SAME row takes the new NR 0x14 value only once its snapshot is refreshed | Continuation of TR-50 (same `Renderer`, same row) | Re-snapshot row 13 (mirrors the next `on_scanline()` capture), composite the SAME row again | ULA RGB (0xAA) now matches the refreshed NR 0x14 snapshot → ULA transparent → the NR 0x4A fallback colour wins | 1137, 5226, 6822, 6912–6913, 7078, 7100 |
| TR-52 | **Skip-write recovery** (Task 46): `Layer2::render_scanline` used a SEPARATE live member (`PaletteManager::global_transparency()`) instead of the per-line snapshot `composite_scanline` uses, so a mid-frame NR 0x14 write could make it wrongly skip-write (destroy) an opaque pixel one row too early — unlike TR-50/51 this drives the REAL `Layer2::render_scanline` via `Renderer::render_row` (not a hand-set `layer2_line_`), because the bug is one pipeline stage before `composite_scanline` | Row 32's `transparent_rgb_per_line_` snapshot captured with NR 0x14=0xE3 (reset default); a real `Layer2` (bank 8, palette index 0x01 = RGB 0xAA) painted via `render_row`; `set_layer_mask(LAYER_LAYER2)` isolates L2 (host-side test knob, not machine state) | `write_nr14(0xAA)` (mirrors the emulator.cpp handler: sets BOTH `PaletteManager::global_transparency()` and `Renderer::transparent_rgb_` — now matches the pixel's RGB) but do **not** re-snapshot row 32; call `render_row` for row 32 | Pixel SURVIVES using the pre-write deferred value (0xE3 ≠ 0xAA → opaque) — before Task 46, the stale live 0xAA would make Layer2 skip-write it, and `composite_scanline`'s own NR 0x14 check cannot recover a pixel that was never written (`is_transparent(l2_px)` is already true) | 7121, 1137, 5226, 6822, 6912–6913, 7078 |
| TR-53 | **Deferral round-trip** (Task 46): the SAME row's Layer 2 output takes the new NR 0x14 value only once its snapshot is refreshed | Continuation of TR-52 (same `Renderer`, same `Layer2`, same row) | Re-snapshot row 32 (mirrors the next `on_scanline()` capture), call `render_row` for row 32 again | Layer 2 RGB (0xAA) now matches the refreshed NR 0x14 snapshot → Layer2 correctly skip-writes the now-transparent pixel → the NR 0x4A fallback colour wins | 7121, 1137, 5226, 6822, 6912–6913, 7078 |

### Group L2EQ — enforced dead-clause equivalence proof (Task 46)

Not a VHDL-derived pixel-math row like the rest of this plan: an exhaustive
property check over the two RGB-expansion functions
(`PaletteManager::layer2_rgb8`/`layer2_colour`, `Renderer::rrrgggbb_to_argb`)
that makes `composite_scanline`'s redundant `l2_transp` RGB-match clause
(`renderer.cpp`, `(l2_px & 0xFFFFFF) == nr14_rgb`) provably unreachable once
Layer2 and the compositor read the same `transparent_rgb` snapshot (proved
by TR-52/53 above). Committed as an enforced regression guard, not left as
a one-off review calculation, per independent-review requirement — see the
2026-07-13 changelog entry above for the full story, including a bug this
row's own first draft caught in itself (an over-claimed biconditional; the
converse direction is genuinely false at `r3=g3=0, b3=1, X=0x00` because
Layer2's own gate treats that as transparent regardless, so the compositor
never gets a chance to check it — see the row's own Expected column).

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| L2EQ-01 | Whenever Layer2's own transparency gate judges a pixel OPAQUE (writes it), the compositor's redundant RGB-match clause can never independently re-flag it transparent | For all 512 RGB333 palette entries (r3,g3,b3), written via two real `PaletteManager::write_9bit` calls | For all 256 NR 0x14 register values X, compare `layer2_rgb8(entry) != X` (Layer2's own gate says opaque) against `layer2_colour(entry) != rrrgggbb_to_argb(X)` (compositor's redundant full-precision check) | The implication holds in all 131072 cases (0 violations) — R/G use the identical injective expansion on both sides; B's two candidate 8-bit expansion sets `{0,36,73,109,146,182,219,255}` (3-bit) and `{0,85,170,255}` (2-bit) intersect only at the endpoints, which are exactly the b3 values already covered by the RGB8 comparison | 7121 |

### Group TRI — Index-based transparency (integration rows only)

These rows verify that the compositor uses the layer-engine-produced
`pixel_en` signal correctly. They do not test the index comparators
themselves (those live in the Sprites / Tilemap / Layer 2 plans).

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| TRI-10 | Sprite index matching NR 0x4B produces pixel_en=0 → compositor ignores | NR 0x4B=0xE3 | sprite index=0xE3, non-zero palette RGB | sprite_transparent=1 | `sprites.vhd:1067`; `zxnext.vhd:7118` |
| TRI-11 | Sprite index ≠ NR 0x4B and inside active area → pixel_en=1 | NR 0x4B=0xE3 | sprite index=0x42, inside display area | sprite_transparent=0 | `sprites.vhd:1067`; 7118 |
| TRI-20 | Tilemap nibble matching NR 0x4C → pixel_en=0 | NR 0x4C=0xF | TM index nibble=0xF | tm_transparent=1 | `zxnext.vhd:4395`; 7109 |

### Group FB — Fallback colour (NR 0x4A)

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| FB-10 | Fallback appears when all layers transparent (mode 000) | layer_priorities_2=000, S/L/U all transparent | NR 0x4A=0xE3 | `rgb_out_2 = "1110_0011" & "1"` = `0x1C7` | 7214 (fallback bit0 = `0x1`\|`0x1`=1) |
| FB-11 | Fallback=0x00 | all transparent | NR 0x4A=0x00 | `rgb_out_2 = 0x000` (bit0 = 0\|0 = 0) | 7214 |
| FB-12 | Fallback=0x4A = `0100_1010` | all transparent | — | `rgb_out_2 = 0x4A<<1 \| (1 \| 0) = 0x095` | 7214 |
| FB-13 | Fallback=0x01 = `0000_0001` | all transparent | — | `rgb_out_2 = 0x002 \| 1 = 0x003` | 7214 |
| FB-14 | Fallback=0x02 = `0000_0010` | all transparent | — | `rgb_out_2 = 0x004 \| 1 = 0x005` | 7214 (bit1 set) |
| FB-15 | Fallback not used when any layer opaque | S opaque, L/U transparent, mode 000 | NR 0x4A=0xE3 | `rgb_out_2 = sprite_rgb` not fallback | 7222 |
| FB-16 | Reset default is 0xE3 | power-on | read NR 0x4A | `0xE3` | `nr_4a_fallback_rgb` reset clause |
| FB-17 | All 8 priority modes converge on fallback when every layer transparent | for each of 000..111 | NR 0x4A=0x42 = `0100_0010` | `rgb_out_2 = 0x084 \| 1 = 0x085` for every mode | 7214 (default assignment before case) |

### Group PRI — Layer priority modes 000..101

All rows use distinct, non-NR 0x14 RGB values for each layer so winners
are identifiable by colour: `ula_rgb=0x1AA`, `layer2_rgb=0x1BB`,
`sprite_rgb=0x1CC`, NR 0x14=0xE3, fallback 0xE3. `tm_pixel_below_2=0`
unless stated, TM transparent unless stated, `ula_blend_mode_2=00`,
stencil off.

| ID | Title | Layer opacity | Mode | Expected winner | VHDL line |
|----|-------|---------------|------|-----------------|-----------|
| PRI-010-SLU-3 | Mode 000, all three opaque | S=✓ L=✓ U=✓ | 000 | Sprite | 7222 |
| PRI-010-SLU-LU | Mode 000, only L+U | S=✗ L=✓ U=✓ | 000 | Layer 2 | 7224 |
| PRI-010-SLU-U | Mode 000, only U | S=✗ L=✗ U=✓ | 000 | ULA | 7226 |
| PRI-010-SLU-0 | Mode 000, none | — | 000 | fallback | 7214 |
| PRI-011-LSU-3 | Mode 001, all three | S=✓ L=✓ U=✓ | 001 | Layer 2 | 7232 |
| PRI-011-LSU-SU | Mode 001, S+U only | S=✓ L=✗ U=✓ | 001 | Sprite | 7234 |
| PRI-011-LSU-U | Mode 001, U only | S=✗ L=✗ U=✓ | 001 | ULA | 7236 |
| PRI-010-SUL-3 | Mode 010, all three | S=✓ L=✓ U=✓ | 010 | Sprite | 7244 |
| PRI-010-SUL-UL | Mode 010, U+L | S=✗ L=✓ U=✓ | 010 | ULA | 7246 |
| PRI-010-SUL-L | Mode 010, L only | S=✗ L=✓ U=✗ | 010 | Layer 2 | 7248 |
| PRI-011-LUS-3 | Mode 011, all three | S=✓ L=✓ U=✓ | 011 | Layer 2 | 7254 |
| PRI-011-LUS-US | Mode 011, U(non-border)+S | S=✓ L=✗ U=✓, `ula_border_2=0` | 011 | ULA | 7256 |
| PRI-011-LUS-S | Mode 011, S only | S=✓ L=✗ U=✗ | 011 | Sprite | 7258 |
| PRI-011-LUS-border | Mode 011, U(border) + S + TM transp | `ula_border_2=1`, `tm_transparent=1`, S=✓, U=✓, L=✗ | 011 | Sprite (border exception) | 7256 |
| PRI-100-USL-3 | Mode 100, all three | S=✓ L=✓ U=✓ | 100 | ULA | 7266 |
| PRI-100-USL-border | Mode 100, U(border) + S, TM transp, L=✗ | 100 conditions | 100 | Sprite | 7266 |
| PRI-100-USL-L | Mode 100, L only | S=✗ L=✓ U=✗ | 100 | Layer 2 | 7270 |
| PRI-101-ULS-3 | Mode 101, all three | S=✓ L=✓ U=✓ | 101 | ULA | 7278 |
| PRI-101-ULS-border | Mode 101, U(border)+L+S, TM transp | conditions | 101 | Layer 2 | 7278, 7280 |
| PRI-101-ULS-S | Mode 101, S only | S=✓ L=✗ U=✗ | 101 | Sprite | 7282 |

### Group PRI-BOUND — Boundary and default branch

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| PRI-B-0 | In every mode 000..101 with all three transparent, fallback wins | 8 sub-rows | `rgb_out_2 = fallback_rgb_2 & extLSB` | 7214 default assignment before case branches |
| PRI-B-1 | Mode 000 with NR 0x14 = sprite_rgb[8:1] must not transparent-trigger sprite (sprite has no RGB compare) | Set sprite_rgb RGB[8:1]=NR 0x14 | sprite wins (opaque) | 7118 |
| PRI-B-2 | Mode 001: even if sprite opaque, L2 opaque beats it | S=✓ L=✓ U=✗ | Layer 2 wins | 7232 (L2 branch first) |

### Group L2P — Layer 2 priority bit promotion

All rows use `layer2_priority_2=1` sampled from palette bit 15 of a
non-transparent L2 pixel. `ula_blend_mode_2=00` unless stated.

| ID | Title | Mode | Opaque layers | Expected | VHDL |
|----|-------|------|---------------|----------|------|
| L2P-10 | Promotion in mode 000 over sprite | 000 | S=✓ L=✓ U=✗ | Layer 2 | 7220 |
| L2P-11 | Promotion in mode 010 over sprite | 010 | S=✓ L=✓ U=✗ | Layer 2 | 7242 |
| L2P-12 | Promotion in mode 100 over sprite (L2 above U) | 100 | S=✓ L=✓ U=✓ | Layer 2 | 7264 |
| L2P-13 | Promotion in mode 101 over sprite (L2 above U) | 101 | S=✓ L=✓ U=✓ | Layer 2 | 7276 |
| L2P-14 | No-op in mode 001 (L2 already top) | 001 | L=✓ | Layer 2 (L2 wins anyway) | 7232 |
| L2P-15 | No-op in mode 011 (L2 already top) | 011 | L=✓ | Layer 2 | 7254 |
| L2P-16 | `layer2_transparent=1` suppresses promotion | 000, L=✗, S=✓ | Sprite (not L2) | Sprite | 7123, 7222 |
| L2P-17 | Promotion in mode 110 (blend): L2 promoted shows blend output | 110, L=✓ S=✓ U=✓ | `layer2_priority=1` | blend RGB (clamped) | 7300 |
| L2P-18 | Promotion in mode 111 (blend): L2 promoted shows blend output | 111, L=✓ S=✓ U=✓ | `layer2_priority=1` | blend−5 RGB (clamped) | 7342 |
| L2P-19 | Native 640 mode + L2 priority bit set: both even and odd pixel columns observe priority promotion | 000 / native 640 | S=✓ L=✓ U=✗ | `layer2_priority_[x]` true for both even and odd `x`; compositor promotes L2 over sprite at every column (skip — see G93) | 7039-7050 |

### Group PFF — port_ff_reg NR-side fan-out (NR 0x69 / NR 0x22 / NR 0xC4)

Re-homed 2026-04-27 from the gap audit (G108). VHDL `zxnext.vhd:3617-3622`
fans three NextREG writes into the `port_ff_reg` storage that drives
Timex screen-mode + ULA-int gating. jnext `src/core/emulator.cpp:888-890`
forwards only NR 0x69 bit 7; bits 5:0 are dropped, NR 0x22 b2 has no
side-effect, NR 0xC4 b0 handler at `:983` carries an incorrect comment.
VHDL priority: a port-FF write wins over the NR-side mirror.

| ID            | Title                                                                            | Stimulus                                                                                | Expected                                                                                  | VHDL                  |
|---------------|----------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------|-----------------------|
| PFF-G108-01   | NR 0x69 bits 5:0 fan into `port_ff_reg(5:0)`                                     | reset; write NR 0x69 ← 0x3F; sample `port_ff_reg` low six bits                          | low six bits = 0x3F (skip — see G108)                                                     | zxnext.vhd:3617-3618  |
| PFF-G108-02   | NR 0x22 b2 fans into `port_ff_reg(6)`                                            | reset; write NR 0x22 ← 0x04 (bit 2 = 1); sample `port_ff_reg(6)`                        | bit 6 = 1 (skip — see G108)                                                               | zxnext.vhd:3619-3620  |
| PFF-G108-03   | NR 0xC4 b0 fans into `port_ff_reg(6)` as `not nr_wr_dat(0)` (inverted polarity)  | reset; write NR 0xC4 ← 0x00 (bit 0 = 0); sample `port_ff_reg(6)`                        | bit 6 = 1 (inversion); subsequent NR 0xC4 ← 0x01 sets bit 6 = 0 (skip — see G108)         | zxnext.vhd:3621-3622  |

### Group BL — Blend modes 110 and 111

Blend formulas (per channel, 3 bits at a time, `rgb(8:6)=R`,
`rgb(5:3)=G`, `rgb(2:0)=B`). Mode 110 computes `L2+U` clamped at 7
(`zxnext.vhd:7288–7298`). Mode 111 computes `L2+U−5` with the
asymmetric clamping from `zxnext.vhd:7316–7338`: if the 4-bit sum
`≤4` the result is 0; if the sum's top two bits equal `"11"` (≥12)
the result is 7; otherwise the result is `sum+0xB` mod 16 (i.e. −5).
The subtraction branch is only entered if `mix_rgb_transparent = '0'`
(line 7314).

| ID | Title | Mode | L2 channel | U (`mix_rgb`) channel | Expected | VHDL |
|----|-------|------|------------|-----------------------|----------|------|
| BL-10 | Add no clamp | 110 | R=3 G=2 B=1 | R=3 G=2 B=1 | R=6 G=4 B=2 | 7201–7210, 7286 |
| BL-11 | Add clamp hi | 110 | R=5 G=6 B=7 | R=5 G=6 B=7 | R=7 G=7 B=7 | 7288–7298 |
| BL-12 | Add 0+0 | 110 | 0 | 0 | 0 | 7201 |
| BL-13 | Add, `mix_top` opaque beats blend | 110 | L=✓ | U=✓ | mix_top_rgb (not the blend) | 7302 |
| BL-14 | Add, sprite between mix_top and mix_bot | 110 | L=✓ U=✗, S=✓ | mix_top transp | sprite | 7304 |
| BL-15 | Add, mix_bot wins after sprite transp | 110 | L=✓ | U=✓ as mix_bot | mix_bot_rgb | 7306 |
| BL-16 | Add, final fallback to blend | 110 | L=✓ S=✗ U=✗ (all mix_top/bot transp, L2 only opaque) | — | blend RGB | 7308 |
| BL-20 | Sub, ≤4 clamps to 0 | 111 | R=2 G=2 B=2 | R=2 G=2 B=2 | 0/0/0 (sum 4, ≤4) | 7316 |
| BL-21 | Sub, ≥12 clamps to 7 | 111 | R=7 G=7 B=7 | R=7 G=7 B=7 | 7/7/7 (sum 14) | 7318 |
| BL-22 | Sub, middle value | 111 | R=3 G=4 B=5 | R=3 G=4 B=5 | R=1 G=3 B=5 (6−5, 8−5, 10−5) | 7321 |
| BL-23 | Sub gated by `mix_rgb_transparent` | 111 | mix_rgb transparent | — | Pass-through via sprite / mix_top / mix_bot / L2 alone (no subtraction) | 7314 |
| BL-24 | Sub, mix_top opaque wins over blend | 111 | L=✓ U=✓ mix_top=✓ | | mix_top_rgb | 7344 |
| BL-25 | Sub, sprite between | 111 | | | sprite | 7346 |
| BL-26 | Sub, mix_bot fallback | 111 | | | mix_bot_rgb | 7348 |
| BL-27 | Sub, final L2-only fallback shows blended L2 | 111 | L=✓, all others transp | blend branch | subtracted RGB of `L2+0`, i.e. each channel=`L2-5` clamped | 7350 |
| BL-28 | L2 priority bit overrides blend (mode 110) | 110 | `layer2_priority=1` | | additive RGB always wins even with opaque mix_top | 7300 |
| BL-29 | L2 priority bit overrides blend (mode 111) | 111 | `layer2_priority=1` | | subtracted RGB always wins | 7342 |

### Group UTB — ULA/Tilemap blend mode (NR 0x68 bits 6:5)

| ID | Title | `ula_blend_mode_2` | `tm_pixel_below_2` | mix_rgb | mix_top | mix_bot | VHDL |
|----|-------|--------------------|--------------------|---------|---------|---------|------|
| UTB-10 | Mode 00, TM above | 00 | 0 | `ula_mix_rgb` | `tm_rgb` (opaque when TM opaque) | transparent | 7142–7148 |
| UTB-11 | Mode 00, TM below | 00 | 1 | `ula_mix_rgb` | transparent | `tm_rgb` | 7142–7148 |
| UTB-20 | Mode 10, stencil-off combined | 10 | either | `ula_final_rgb` (= `ulatm_rgb` when not stencil) | forced transparent | forced transparent | 7149–7155 |
| UTB-30 | Mode 11, TM as U, ULA floats below | 11 | 1 | `tm_rgb` | `ula_rgb` (below=1 → bot position; `mix_top_transparent = ula_transparent OR NOT tm_pixel_below_2` = `OR 0` = `ula_transparent`) — ULA goes to **top** | `ula_rgb` to bot gated `tm_pixel_below_2=0` → transparent | 7156–7162 |
| UTB-31 | Mode 11, TM as U, ULA floats above | 11 | 0 | `tm_rgb` | transparent (gate `NOT 0 = 1`) | `ula_rgb` (gate `tm_pixel_below_2 = 0`) → ULA goes to **bot** | 7156–7162 |
| UTB-40 | Mode 01 (`others`), below=0 | 01 | 0 | transparent | `tm_rgb` | `ula_rgb` | 7163–7176 (else branch) |
| UTB-41 | Mode 01, below=1 | 01 | 1 | transparent | `ula_rgb` | `tm_rgb` | 7163–7176 (if branch) |
| UB-G26-01 | UTB-40/41 oracle inversion check: confirm `ula_blend_mode_2 = 01` `mix_top`/`mix_bot` swap on `tm_pixel_below_2` matches FPGA-team intent | mode 01, set ULA+TM with TM pixel below = 1; sample `mix_rgb` source | matches FPGA-team-confirmed expected; if confirmed inverted, UTB-40/41 must be regenerated and this row re-rules | zxnext.vhd:7163-7177 |
| UB-G26-02 | L2 priority bit in modes 110/111 *over* opaque `mix_top` matches FPGA-team intent | mode 110, L=✓ S=✓ U=✓ mix_top=opaque, L2 priority bit set | additive RGB wins over `mix_top_rgb` (per VHDL 7300/7342 first-`if`); FPGA-team confirms not a copy-paste artefact | zxnext.vhd:7300, 7342 |

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| UTB-50 | **Deferral** (Task 43): a mid-frame NR 0x68 bits 6:5 write does not retroactively affect a row whose per-line snapshot already ran | Row 11's `blend_mode_per_line_` snapshot captured with blend_mode="00"; layer_priority=6 (mode 110, blend path), ULA+TM both opaque, `tm_pixel_below_2=0`, no L2/sprite | Write NR 0x68 bits 6:5 ← "10" (mode 10) but do **not** re-snapshot row 11; composite row 11 directly via `composite_scanline` (bypasses `composite_one()`'s auto-sync — see the deferral-methodology note in Group PSCAN) | Row still uses mode "00" (VHDL 7142–7148): TM opaque wins the `mix_top` cascade slot | 5446, 6811, 6900–6901, 7065 |
| UTB-51 | **Deferral** (Task 43): the SAME row takes the new blend mode only once its snapshot is refreshed | Continuation of UTB-50 (same `Renderer`, same row) | Re-snapshot row 11 (mirrors the next `on_scanline()` capture), composite the SAME row again | Row now uses mode "10" (VHDL 7149–7155): `mix_top`/`mix_bot` forced transparent; with no L2 pixel the additive cascade (VHDL 7300–7310) never fires → the NR 0x4A fallback colour wins | 7149–7155, 7300–7310 |

Note on UTB-30/UTB-31: the VHDL gating in mode 11 reads as
`mix_top_transparent <= ula_transparent or not tm_pixel_below_2`, i.e.
the ULA floats to the *top* when `tm_pixel_below_2=1` (because
`not 1 = 0` leaves only `ula_transparent` as the gate). This is
contrary to naive reading; the test title is chosen to reflect the
actual VHDL.

### Group STEN — Stencil mode (NR 0x68 bit 0)

Stencil is only active when both ULA and TM are enabled
(`ula_stencil_mode_2=1 AND ula_en_2=1 AND tm_en_2=1`, line 7130).
`stencil_rgb = ula_rgb AND tm_rgb`;
`stencil_transparent = ula_transparent OR tm_transparent`
(lines 7112–7113, 7131–7132). The result replaces `ula_final_rgb`.

| ID | Title | ULA rgb | TM rgb | Expected stencil rgb | VHDL |
|----|-------|---------|--------|----------------------|------|
| STEN-10 | Bitwise AND | 9'b111_111_111 | 9'b111_000_000 | 9'b111_000_000 | 7113 |
| STEN-11 | AND with zero | 9'b111_111_111 | 9'b000_000_000 | 9'b000_000_000 (not transparent — both opaque) | 7113 |
| STEN-12 | ULA transparent → stencil transparent | transparent | any opaque | `stencil_transparent=1` | 7112 |
| STEN-13 | TM transparent → stencil transparent | any opaque | transparent | `stencil_transparent=1` | 7112 |
| STEN-14 | Both transparent → transparent | transparent | transparent | `stencil_transparent=1` | 7112 |
| STEN-15 | Stencil inactive if `tm_en=0` (even with bit set) | ULA opaque, TM disabled | — | `ula_final_rgb = ulatm_rgb` (non-stencil path) | 7130, 7133 |
| STEN-16 | Stencil inactive if `ula_en=0` | — | — | non-stencil path | 7130 |
| STEN-17 | Stencil off (bit=0), both enabled | — | — | non-stencil path | 7130 |

| ID | Title | Preconditions | Stimulus | Expected | VHDL |
|----|-------|---------------|----------|----------|------|
| STEN-20 | **Deferral** (Task 43): a mid-frame NR 0x68 bit 0 write does not retroactively affect a row whose per-line snapshot already ran | Row 7's `stencil_mode_per_line_` snapshot captured with stencil_mode=off; mode 000 (SLU), `tm_enabled_=1`, `ula_enabled_per_line_[7]=1`, ULA=0xE0 and TM=0x1F (opaque, distinct, non-AND-equal RGBs) | Write NR 0x68 bit 0 ← 1 (stencil on) but do **not** re-snapshot row 7; composite row 7 directly via `composite_scanline` (bypasses `composite_one()`'s auto-sync — see the deferral-methodology note in Group PSCAN) | Row still uses the pre-write non-stencil `ulatm` merge — TM (opaque, `tm_pixel_below=0`) wins | 5445, 6810, 6897–6898, 7064 |
| STEN-21 | **Deferral** (Task 43): the SAME row takes stencil mode only once its snapshot is refreshed | Continuation of STEN-20 (same `Renderer`, same row) | Re-snapshot row 7 (mirrors the next `on_scanline()` capture), composite the SAME row again | Row now takes the stencil AND-branch: `ula_rgb AND tm_rgb` (0xE0 AND 0x1F = 0x00) | 7112–7113, 7130 |

### Group UDIS — NR 0x68 bit 7 ULA disable + end-to-end blend assertion

Re-homed 2026-04-23 from `test/ula/ula_test.cpp` §12 (S12.02/03/04) per
`doc/design/TASK-COMPOSITOR-NR68-BLEND-PLAN.md`. Groups UTB and STEN above
exercise NR 0x68 bits 6:5 and bit 0 at the pipeline-stage level (mix_top /
mix_bot / stencil routing). The three rows below cover the gaps those groups
cannot reach without a full render fixture:

1. **bit 7 ULA-enable wiring**: NR 0x68 bit 7 (`nr_68_ula_en`) must reach
   the render pipeline's ULA gate. STEN-16 already assumes this gate exists
   but the wiring itself has not been observed end-to-end.
2. **Frame-buffer visibility** of the bit-7 toggle.
3. **Blend-mode (bits 6:5) visibility** in the final frame buffer, not just
   at the stage routing level (UTB-* cover stage routing only).

Rows start as `skip()` with reason `F-UDIS-RENDER`; they un-skip when a
frame-buffer comparison harness is available (likely the same full-Emulator
fixture `ula_integration_test.cpp` uses).

| ID      | Title                                                 | Stimulus                                                                                        | Expected                                                                                                   | VHDL                         |
|---------|-------------------------------------------------------|-------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------|------------------------------|
| UDIS-01 | NR 0x68 bit 7 wired into render pipeline              | Full-Emulator, render one frame with ULA content; write NR 0x68 ← 0x00 (bit 7 = 0); render again | Frame buffer ULA contribution must differ between the two renders (bit 7 = 1 shows ULA; 0 disables it)     | `zxnext.vhd:5445`            |
| UDIS-02 | NR 0x68 bit 7 toggle visible at frame-buffer level    | Same as UDIS-01 but cycle bit 7 mid-scanline via a Copper MOVE                                  | Scanlines above the MOVE retain old ULA state; scanlines below respect the new bit-7 value                 | `zxnext.vhd:5445`, 6809      |
| UDIS-03 | NR 0x68 blend mode (bits 6:5) visible end-to-end      | Full-Emulator, render one frame with ULA+TM content in mode 00; switch to mode 10; render again | Rendered output must differ per UTB-20 semantics — `ula_final_rgb` routing change is visible frame-to-frame | `zxnext.vhd:5445`, 7142–7176 |

### Group SOB — Sprite over border (compositor integration)

Only one row here — the actual gating lives in `sprites.vhd`.

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| SOB-10 | Sprite rgb arrives at compositor with `sprite_pixel_en_2=1` in border when `nr_15_sprite_over_border_en=1`; opaque sprite should beat border-ULA in mode 000 | set NR 0x15[1]=1, border pixel, mode 000, only S opaque | Sprite | 7118, 7222 |

### Group LINE — Per-scanline parameter capture

`layer_priorities_0` and friends are latched at stage 0
(`zxnext.vhd:6799`) from `nr_15_layer_priority`. Writes to the
NextREG take effect on the **next** scanline's stage 0. Writes that
happen during hsync (before stage 0 of the next line) are captured;
writes after stage 0 are not.

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| LINE-10 | Write NR 0x15[4:2] mid-line | Write at column 100, current line running mode 000 | Current line remains mode 000; next line is new mode | 6799 |
| LINE-11 | Write NR 0x14 mid-line | same | Current line keeps old NR 0x14; next line uses new | 6822 |
| LINE-12 | Write NR 0x4A mid-line | same | Current line keeps old fallback | 6730–6832 block |
| LINE-13 | Copper write to NR 0x15 at end of line | Copper writes at hblank | Next line uses new mode | 6799 |
| LINE-14 | Two writes in one line: only the last is visible next line | Write A, then write B | Line +1 uses B | 6799 |

### Group PSCAN — Per-scanline transparency-key replay (NR 0x14 / 0x4B / 0x4C, G04) and NR 0x68 bits (G11)

Three transparent-key NextREGs are read once per frame today; mid-
frame Copper writes (sky-vs-foreground swap) collapse to last-write.
This sub-group adds the log-pattern clone for each of the three keys.
Three more cover NR 0x68 bit 0 (stencil), bits 6:5 (blend mode), and
bit 3 (ULA+ enable) per-scanline replay.

| ID            | Title                                                                          | Stimulus                                                                                          | Expected                                                                                          | VHDL                       |
|---------------|--------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------|----------------------------|
| PSCAN-G04-01  | NR 0x14 write logged with current scanline (RGB332 global transparent)         | start_frame; line=50 NR 0x14 ← 0xC3; line=100 NR 0x14 ← 0xE3                                      | nr_14 change-log holds (50, 0xC3) and (100, 0xE3); apply_changes_for_line replays them            | zxnext.vhd:1137,5226       |
| PSCAN-G04-02  | NR 0x4B (sprite transparent index) write logged + replayed per scanline        | RE-HOMED 2026-04-28 to `SPRITES-TEST-PLAN-DESIGN.md` Group 16 — NR 0x4B is owned by `Sprites` per VHDL `zxnext.vhd:5016, 1190` and `sprites.vhd` pixel engine; the compositor only consumes the already-keyed output. Cross-bucket G04 ownership. | — | zxnext.vhd:5016,1190       |
| PSCAN-G04-03  | NR 0x4C (TM transparent nibble) write logged + replayed per scanline           | RE-HOMED 2026-04-28 to `TILEMAP-TEST-PLAN-DESIGN.md` Group 1 (per-scanline subsection) — NR 0x4C is owned by `Tilemap` per VHDL `zxnext.vhd:5018, 4395` and `tilemap.vhd:425-429`; the compositor only consumes the already-keyed output. Cross-bucket G04 ownership.    | — | zxnext.vhd:5018,4395       |
| PSCAN-G11-01  | NR 0x68 bit 0 (stencil) per-scanline replay                                    | start_frame; line=50 NR 0x68 ← 0x01 (stencil_mode=1)                                              | rows 0..49 see stencil_mode=0; rows 50..end see stencil_mode=1                                    | zxnext.vhd:5445, 7142-7176 |
| PSCAN-G11-02  | NR 0x68 bits 6:5 (blend mode) per-scanline replay                              | start_frame; line=100 NR 0x68 ← 0x40 (blend mode 10 / mix_rgb=ula_final)                          | rows 0..99 use blend mode 00; rows 100..end use 10 (mix_rgb routing changes)                      | zxnext.vhd:5445, 7142-7176 |
| PSCAN-G11-03  | NR 0x68 bit 3 (ULA+ gate) per-scanline replay                                  | start_frame; line=80 NR 0x68 ← 0x08 (ulap_en=1)                                                   | rows 0..79 use ULA path; rows 80..end use ULA+ palette path                                       | zxnext.vhd:5445, ulap_en   |

**PSCAN-G04-01 / G11-01 / G11-02 test the array's bookkeeping in
isolation, not `composite_scanline`'s consumption of it.** They drive
`snapshot_*_for_line()`/`init_*_per_line()`/`*_for_line()` directly and
assert the array holds the right value per row — they never call
`composite_scanline` at all, so a bug where the compositor reads the
*live* member instead of the per-line snapshot (the exact defect fixed
in Task 43 for stencil/blend and Task 45 for NR 0x14) would not be
caught by these rows. Do not conflate the two: the bookkeeping is
proven here; the *consumption* is proven by the deferral pairs below,
which call `composite_scanline` directly and assert an observable pixel
difference before vs. after the snapshot is refreshed for the SAME row:

- **TR-50 / TR-51** (Group TR below) — NR 0x14, `composite_scanline`'s own transparency check.
- **STEN-20 / STEN-21** (Group STEN below) — NR 0x68 bit 0 (stencil).
- **UTB-50 / UTB-51** (Group UTB below) — NR 0x68 bits 6:5 (blend mode).
- **TR-52 / TR-53** (Group TR below) — NR 0x14, `Layer2::render_scanline`'s
  gate one stage EARLIER than `composite_scanline` (Task 46). This pair is
  the odd one out structurally: it calls `Renderer::render_row` (which
  itself calls the real `Layer2::render_scanline`), not
  `composite_scanline` directly, because the Task 46 defect was Layer2
  reading a separate live member, not `composite_scanline` reading the
  wrong array. `composite_one()`'s auto-sync would be equally vacuous
  here for the same reason as the other three pairs.

All three `composite_scanline`-level pairs share one methodological
point worth stating once, here, rather than only in a code comment:
`test/compositor/compositor_test.cpp`'s `composite_one()` helper
auto-syncs every per-line snapshot (`snapshot_transparent_rgb_for_line`,
`snapshot_stencil_mode_for_line`, `snapshot_blend_mode_for_line`) for
its target row immediately before compositing, so every OTHER row in
the suite (which sets the live member and composites through
`composite_one()` in the same statement) tests pixel math given an
*already-effective* value, not deferral. A deferral row that used
`composite_one()` would have its withheld snapshot silently filled in
by the helper and could never fail — a vacuous test. TR-50/51,
STEN-20/21 and UTB-50/51 therefore call `r.composite_scanline()`
directly, bypassing `composite_one()`, so the snapshot they
deliberately withhold for the "before" assertion stays withheld.
TR-52/53 sidesteps `composite_one()` for the same reason by calling
`r.render_row()` directly instead.

#### PSCAN sub-group G02 — NR 0x15 change-log replay CONSUMPTION (GH #73, wired 2026-07-23)

Unlike the G04/G11 bookkeeping rows above, these drive the real
`Renderer::render_frame` end to end (the PSCAN-05 recipe: Ram/Rom/Mmu +
ULA ink screen + Layer 2 fill), so an unwired apply/flush, a dead
baseline snapshot, or a frame-end live read at the sprite render gate
is an observable pixel difference. VHDL: NR 0x15 write capture at
`zxnext.vhd:5229-5234`; b4:2 and b0 are latched into the video pipeline
every pixel clock (`6799 layer_priorities_0`, `6819 sprite_en_0` →
`6906-6907` → consumed at `7216 case layer_priorities_2` and `6934
sprite_pixel_en_1a and sprite_en_1` → `7118`), so a mid-frame write
takes effect at raster granularity — per-line under jnext's model.
Bit ownership decision (documented at the emulator's NR 0x15 dispatch):
the log applies ONLY b4:2 + b0; b7 stays with the LoRes per-line
snapshot (GH #63 — exactly one mechanism drives b7 per line); b6/b5/b1
stay SpriteEngine live state at frame granularity.

| ID           | Title                                                        | Stimulus                                                                                                     | Expected                                                                                                        | VHDL                        |
|--------------|--------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------|-----------------------------|
| PSCAN-G02-01 | Mid-frame priority change through render_frame               | Baseline USL (0x10); logged write SLU (0x00) tagged line 100; render_frame with opaque ULA + L2               | Rows <100 composite USL (ULA wins); rows >=100 composite SLU (L2 wins)                                          | 6799, 7216-7290             |
| PSCAN-G02-02 | Cross-frame persistence, no rewrite                          | Frame F: baseline SLU, write USL at line 100, render; frame F+1: start_frame_nr15 only, render                | F: rows <100 L2, >=100 ULA; F+1: ALL rows ULA (persisted nonzero value — a reset-to-default baseline fails)     | 5229-5234 (register holds)  |
| PSCAN-G02-03 | Vblank write survives via flush (audit finding: no flush)    | Baseline SLU; write 0x11 (USL+b0) tagged line 300 (>= FB_HEIGHT); render; start_frame_nr15; render again      | F visible rows all SLU; live state after F = 0x11 (prio 4, sprite_en); F+1 rows composite USL                   | 5229-5234; sibling flush    |
| PSCAN-G02-04 | Sprite enable b0 per-line at render gate + compositor        | 16px sprite spanning line 100; b0=1 at frame start, b0=0 written at line 100; engine live flag ends FALSE     | Sprite rows 90..99 show sprite pixels, rows 100..105 show ULA; a frame-end live read at either stage blanks all | 6819, 6906-6907, 6934, 7118 |
| PSCAN-G02-05 | Change-log cap + warn latch + live tracking past cap         | MAX_NR15_CHANGES_PER_FRAME+1 writes; one more write past cap; start_frame_nr15                                | Log caps at MAX; warn latch fires once, persists, re-arms at start_frame; live state tracks writes past the cap | (jnext log infrastructure)  |

Mutation evidence (2026-07-23, each restored to green): unwiring
`apply_changes_for_line_nr15` from `render_frame` → G02-01 red;
zeroing the `start_frame_nr15` baseline snapshot → G02-02 red;
removing the `flush_remaining_changes_nr15` call → exactly G02-03 red;
reverting the `render_row` gate to the engine's live
`sprites_visible()` → exactly G02-04 red; gating the live update
behind the cap check → exactly G02-05 red.

### Group UCLIP — NR 0x1A ULA clip window per-line deferral

The last instance of the Task 43/45/46 per-line-deferral bug class.
`Renderer::apply_ula_clip` (the compositor-stage consumer of the ULA
clip — VHDL `zxnext.vhd:7104`, `ula_clipped` feeds `ula_transparent`)
read the LIVE `Ula::clip_x1()/clip_x2()/clip_y1()/clip_y2()` getters at
render time, so a mid-frame NR 0x1A write (Copper MOVE / CPU racing the
beam) retroactively re-masked every row rendered after the write. Per
VHDL the clip comparators consume the live registers per pixel
(`zxnext.vhd:988-991` stage-0 register; NR 0x1A rotating 4-write cycle
plus the y2 >= 0xC0 clamp at `zxnext.vhd:6779-6783`), so a mid-frame
change takes effect exactly at the raster position where it lands —
never earlier. jnext now snapshots the four EFFECTIVE clip values per
scanline (`Renderer::snapshot_ula_clip_for_line` /
`init_ula_clip_per_line` / `ula_clip_for_line`, captured by
`Emulator::on_scanline`; the rotating write-cycle index is NOT part of
the snapshot), and `apply_ula_clip(row)` consumes the snapshot. The
rows drive `apply_ula_clip()` directly — no `composite_one()`-style
auto-sync exists for this array, so the deferral assertions cannot be
made vacuous by a helper. Window A (x1=128, x2=255) is deliberately
NOT the reset default (0/255/0/191) so a dead capture (array left at
the reset default) is distinguishable from a stale-but-captured
snapshot via the left-border clip (`left_clipped = cx1 > 0` in
`renderer.cpp`).

| ID       | Title                                                                                           | Stimulus                                                                                                                                       | Expected                                                                                                 | VHDL               |
|----------|-------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------|--------------------|
| UCLIP-01 | **Deferral**: mid-frame NR 0x1A write does not retroactively re-mask an already-snapshotted row | Row 52's clip snapshot captured with window A (128,255,0,191); write window B (0,127,0,191) live; `apply_ula_clip(row 52)` on a full-ink line | Display col 200 (fb cell 464) survives — stale window A masks the row; live window B would clip it        | 988-991, 6779-6783 |
| UCLIP-02 | Snapshot is genuinely CAPTURED, not the reset default                                           | Same buffer as UCLIP-01                                                                                                                        | Left border cell clipped (snapshotted A has x1=128>0; the reset-default window would keep it)             | 6779-6783          |
| UCLIP-03 | The SAME row takes window B only once its snapshot is refreshed                                 | Re-snapshot row 52 (mirrors the next `on_scanline()` capture), re-apply on a fresh full-ink line                                               | Col 200 clipped, left border kept (x1=0), right border clipped (x2=127<255)                               | 988-991, 6779-6783 |
| UCLIP-04 | Split frame: rows < S masked with window A, rows >= S with window B                             | Snapshot rows 32..51 under A, write B, snapshot rows 52..71; apply per row on fresh buffers                                                    | Every row < 52 shows A masking (col kept, border clipped); every row >= 52 shows B masking (inverse)      | 988-991            |

Mutation coverage (both halves of the dead-snapshot trap, verified at
authoring time): reverting `apply_ula_clip` to the live getters fails
UCLIP-01/02/04; stubbing the capture (snapshot never writes) fails
UCLIP-02/03/04.

### Group BLANK — Output blanking

From `zxnext.vhd:7395–7412`:

```
if rgb_blank_n_6 = '1' then rgb_out_o <= rgb_out_6;
else                         rgb_out_o <= (others => '0');
```

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| BLANK-10 | Active area passes through | `rgb_blank_n_6=1` | `rgb_out_6` | 7395–7412 |
| BLANK-11 | Horizontal blanking forces 0 | `rgb_blank_n_6=0` | 0 | 7395–7412 |
| BLANK-12 | Vertical blanking forces 0 | same | 0 | 7395–7412 |
| BLANK-13 | Fallback colour is NOT shown during blank | `rgb_blank_n_6=0`, NR 0x4A=0xFF | 0 | 7395–7412 |
| BLANK-G27-01 | `rgb_blank_n_6` lockstep with `rgb_out_6`: a 1-pixel desync at the active-area edge would surface as a single-pixel artefact at hblank entry/exit | drive a known opaque pixel exactly at the active-to-blank transition; sample `rgb_out_o` at the boundary column | edge sample matches `rgb_out_6` (not 0); no one-pixel desync visible | zxnext.vhd:7395-7412 |

### Group PAL — Palette lookup integration with compositor

These integration rows verify the compositor consumes the correct 9-bit
palette output. Comprehensive palette-select/offset tests live in the
Layer 2 / Sprites / ULA plans.

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| PAL-10 | ULA pixel index → ULA/TM palette | set ULA/TM palette entry N to RGB X | ULA rgb = X (9-bit) | 6936–7005 |
| PAL-11 | ULA background substitution uses fallback | `ula_select_bgnd_1=1`, `lores_pixel_en_1=0` | `ula_rgb_1 = fallback_rgb_1 & extLSB` | 6987–6991 |
| PAL-12 | LoRes pixel overrides ULA background | `lores_pixel_en_1=1` | LoRes RGB | 6987–6991 else branch |
| PAL-13 | L2 palette select 0 vs 1 (NR 0x43[2]) | toggle bit | different RGB | palette RAM addressing |
| PAL-14 | L2 palette bit 15 surfaces as `layer2_priority_2` | pal entry bit 15=1 | `layer2_priority_2=1` | (propagated from palette RAM) |
| PAL-15 | Sprite palette is L2/Sprite RAM `sc(0)=1` | write sprite palette, read | sprite_rgb = written | 6936–7005 |

## Reset Defaults (compositor-relevant)

| Parameter | Default | VHDL reset clause |
|-----------|---------|-------------------|
| `nr_15_layer_priority` | `000` (SLU) | 4951 |
| `nr_15_sprite_en` | 0 | nr reset block |
| `nr_15_sprite_over_border_en` | 0 | nr reset block |
| `nr_14_global_transparent_rgb` | `0xE3` | 4946 |
| `nr_4a_fallback_rgb` | `0xE3` | nr reset block |
| `nr_4b_sprite_transparent_index` | `0xE3` | 5016 |
| `nr_4c_tm_transparent_index` | `0xF` | 5018 |
| `ula_en` | 1 (ULA on) | NR 0x68 bit 7 reset |
| `ula_blend_mode` | 00 | NR 0x68 bits 6:5 reset |
| `ula_stencil_mode` | 0 | NR 0x68 bit 0 reset |
| `tm_en` | 0 | NR 0x6B bit 7 reset |

### Group RST — Reset

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| RST-10 | After reset, all layers are transparent (TM disabled, S disabled, L2 pixel_en=0) → fallback 0xE3 shown (9-bit 0x1C7) | power-on | `rgb_out_2 = 0x1C7` | 7214, 4946 |
| RST-11 | After reset, mode is 000 (SLU) | power-on, opaque L2 pixel | L2 wins in S-absent scenario | 4951, 7222 |
| RST-12 | After reset, NR 0x4A = 0xE3 | read NR 0x4A | 0xE3 | reset clause |
| RST-13 | After reset, NR 0x14 = 0xE3 | read NR 0x14 | 0xE3 | 4946 |

### Group LMASK — Host-side layer mask (`--delayed-screenshot-layers`, Task 22b)

The only non-hardware input to the compositor. `Renderer::set_layer_mask`
selects which layers the delayed screenshot composes; the frontends arm it
for exactly the captured frame. Its whole contract is an equivalence: a
masked-out layer must behave **identically to that layer's hardware enable
bit being clear**, so the oracle for every row below is the corresponding
VHDL "layer disabled" clause and nothing new is invented. Consequences that
must hold and are therefore tested: the NR 0x15 priority order, the ULA/TM
merge, the L2 priority-bit promotion, the blend mixer, the border exception
and the NR 0x4A fallback all keep behaving per the VHDL; the border, which
the ULA emits, disappears with `ula` and falls through to NR 0x4A; and
stencil, gated on `tm_en`, goes down with `tiles`.

Parser rows (LMASK-P/E/S) pin the CLI contract: comma-separated, lowercase,
exactly `ula` / `layer2` / `sprites` / `tiles` / `all`; empty lists, empty
elements, unknown names, wrong case and double-selection (including
`all,ula`) are loud errors, never silently folded away.

| ID | Title | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| LMASK-P01..P08 | Parser accepts each name, both orders, and the full spell-out | `ula` / `layer2` / `sprites` / `tiles` / `all` / `ula,layer2` / `layer2,ula` / `sprites,tiles,ula,layer2` | mask bits set exactly; `error` cleared | — (CLI) |
| LMASK-E01..E14 | Parser rejects: empty list, unknown name, wrong case, whitespace, duplicates, `all`+name, leading/trailing/inner empty element | `""`, `bogus`, `ULA`, `ula ,tiles`, `ula,ula`, `all,ula`, `ula,`, `,ula`, `ula,,tiles`, `,` | returns false, `error` non-empty, `mask` untouched | — (CLI) |
| LMASK-S01 | Mask → canonical name list | ALL / `ula|tiles` / 0 | `all` / `ula,tiles` / `none` | — (CLI) |
| LMASK-C01 | Default mask is LAYER_ALL and composes every layer | reset(); all four layers opaque, mode 000 | sprite wins (SLU), mask == 0x0F | 7218 |
| LMASK-C02..C05 | Each layer captured alone reaches the output regardless of priority position | all four opaque, mask = one layer | that layer's pixel | 7103, 7106, 7109, 7118 |
| LMASK-C06 | A selected-but-transparent layer does not resurrect the masked ones | ULA/S/TM opaque, L2 transparent, mask = `layer2` | NR 0x4A fallback | 7214 |
| LMASK-C07 | Excluding `ula` removes the BORDER as well | border cell painted by ULA, mask = `layer2` | fallback colour (== hardware `ula_en=0`) | 7103 |
| LMASK-C08 | `ula_border_2` is raster geometry, not an enable: the mode-100 border exception still fires with the ULA masked | border + opaque sprite, mask = `ula,sprites` vs `ula` | sprite wins; sprite masked ⇒ ULA wins | 7266 |
| LMASK-C10 | Masking `layer2` cancels its priority-bit promotion over sprites | L2 opaque with palette b15, sprite opaque, mask = `sprites` | sprite wins | 7220 |
| LMASK-C11 | Blend mode 110: masking `ula` zeroes the `mix_rgb` contribution | ULA+L2 opaque, priority 110, blend 00, mask = `layer2` | mixer emits L2 unchanged | 7101, 7122, 7288-7298 |

#### LMASK-C09 — the stencil 2×2 matrix (mask-ula × mask-tiles)

Enumerated in full, deliberately. VHDL zxnext.vhd:7130 takes the stencil
AND-branch only when **both** enables are set:

```vhdl
if ula_stencil_mode_2 = '1' and ula_en_2 = '1' and tm_en_2 = '1' then
   ula_final_rgb <= stencil_rgb;      -- ula_rgb and tm_rgb   (7113)
else
   ula_final_rgb <= ulatm_rgb;        -- ordinary merge       (7135)
```

and `stencil_rgb` is transparent whenever **either** input is (7112). So
masking either layer away must drop out of the AND-branch and show the
survivor — never erase it. The first cut of this feature gated stencil on
`tm_en` only; the `ula_en` half was missing, and the miss was invisible
because only one off-diagonal cell had a test. All four cells are pinned now.
Fixture for every cell: stencil on, `tm_en` on, ULA and TM both opaque, mode 000.

| ID | mask | Meaning | Expected | VHDL |
|----|------|---------|----------|------|
| LMASK-C09-00 | `all` | neither masked | AND-branch live: ULA `and` TM, per channel | 7130, 7112-7113 |
| LMASK-C09-01 | `ula` | `tiles` masked (≡ tm_en=0) | AND-branch off → ulatm merge shows the **ULA** | 7130, 7134-7135 |
| LMASK-C09-10 | `tiles` | `ula` masked (≡ ula_en=0) | AND-branch off → ulatm merge shows the **TILE**, *not* the fallback | 7130, 7134-7135 |
| LMASK-C09-11 | `layer2` | both masked | ulatm merge transparent → NR 0x4A fallback | 7214 |
| LMASK-C09-SPR | `layer2,sprites,tiles` | the mask the CLI actually builds for "everything except the ULA" | tile survives (guards against fixing only the single-layer spelling) | 7130 |

`LMASK-C09-10` and `LMASK-C09-SPR` are the discriminative rows: they **fail**
against a compositor whose stencil gate omits `ula_en` (the tile is composited
as the fallback colour) and pass once it is present. Screenshot-level twin:
`stencil-layers-tiles` (see below).

## Test Count Summary

| Category | Tests |
|----------|------:|
| TR (RGB-compare transparency)      | 19 |
| TRI (index-compare integration)    |  3 |
| FB (fallback)                      |  8 |
| PRI (6 priority modes, rows)       | 20 |
| PRI-BOUND                          |  3 |
| L2P (L2 priority promotion)        |  9 |
| BL (blend modes 110/111)           | 16 |
| UTB (ULA/TM blend)                 |  7 |
| STEN (stencil)                     |  8 |
| SOB (sprite-over-border integ.)    |  1 |
| LINE (per-line latch)              |  5 |
| BLANK (output blanking)            |  4 |
| PAL (palette integration)          |  6 |
| RST (reset)                        |  4 |
| LMASK (host layer mask, Task 22b)  | 38 |
| UCLIP (NR 0x1A per-line deferral)  |  4 |
| **Total**                          |**155** |

## Open Questions (Honest)

1. **`ula_blend_mode_2 = 01` `mix_top_rgb`/`mix_bot_rgb` reading.** The
   VHDL has the mode-01 case as the `others =>` branch
   (lines 7163–7177) with `mix_rgb <= (others => '0')` and
   `mix_rgb_transparent <= '1'`. The `mix_top`/`mix_bot` selection
   swaps on `tm_pixel_below_2`, but the assignment semantics of
   "`ula_rgb` goes to top when below=1" look inverted to the name of
   the flag. The UTB-40/UTB-41 rows encode the VHDL as-is; if a future
   schematic review shows the VHDL itself has a typo, these rows must
   be regenerated.

2. **`layer2_priority` in mode 110/111 with `mix_top` opaque.** Lines
   7300 and 7342 place `layer2_priority=1` as the first `if` in the
   case, so L2-promotion wins over every other branch including
   `mix_top`. This is more aggressive than the non-blend modes where
   L2-promotion only wins over layers that would otherwise be under
   L2. Worth re-confirming with the FPGA team that this is the
   intended semantics rather than a copy-paste artefact.

3. **LoRes transparency.** Lines 7106–7107 in the VHDL show a
   commented-out `lores_transparent` calculation. In the current VHDL,
   LoRes is folded into the ULA pixel path before stage 2, so the
   compositor has no separate LoRes transparent check. Worth
   verifying in the emulator that there is no orphan LoRes test
   comparing a non-existent signal.

4. **Per-line capture at exact hblank column.** The stage 0 latch
   happens at a specific column boundary. A write that lands at that
   exact column is ambiguous — VHDL simulation shows it goes to the
   next line, but a cycle-exact test may be difficult to write without
   deeper timing infrastructure. LINE-10/14 rows treat this as "next
   line" but a tighter rule may be warranted.

5. **Stencil with `lores_en=1`.** The VHDL code path for stencil gating
   is `ula_en AND tm_en AND stencil_mode`. LoRes shares the ULA
   enable, so stencil-with-lores is not separately tested. If LoRes
   were to be revived as a separate branch, stencil rows would need
   new cases.

6. **`rgb_blank_n_6` vs `rgb_blank_n`.** The blanking logic at stage 3
   uses the `_6` (pipelined 6 stages) version. We do not test that the
   blank signal is correctly delayed in lockstep with `rgb_out_6`; a
   timing drift would only show as a one-pixel edge artefact. Noted
   as a known untested edge case.
