# LoRes VHDL-Derived Compliance Test Plan

> **Plan written BEFORE the implementation exists (2026-07-22, GH [#63](https://github.com/jorgegv/jnext/issues/63)).**
>
> jnext does not implement LoRes at all today. That ordering is deliberate and is
> the whole point of this document: every row below is derived from the FPGA VHDL,
> so none of them can be a description of whatever jnext happens to do once the
> layer is built. The project has been bitten by the opposite ordering — six test
> rows written against jnext's behaviour rather than the spec were found in the
> week this plan was written, and they had encoded a bug.
>
> **No row in this plan may be adjusted to match the implementation.** If an
> implementation disagrees with a row, either the row's VHDL citation is wrong
> (fix the row, citing the VHDL) or the implementation is wrong (fix the code).
> See [UNIT-TEST-PLAN-EXECUTION.md](UNIT-TEST-PLAN-EXECUTION.md).
>
> **That path was taken once, on 2026-07-22** (amendment below): LR-127 and
> LR-128 disagreed with the implementation, the VHDL was re-read, the *rows* were
> found wrong, and they were retired in place with the citation that refutes them
> — not edited into agreement, not deleted. The implementer skipped them and
> reported rather than reinterpreting. That is the sanctioned sequence, and the
> record of it is the point.

## Implementation status (2026-07-22, v0.98.91+ — amended; skips resolved 2026-07-24)

**Implemented against this plan; all 91 catalogued rows are live `check()`
rows** (LR-140 became live on 2026-07-24 — see below).
`src/video/lores.{h,cpp}` (the pixel generator, a 1:1 model of
`lores.vhd`) plus `Renderer::apply_lores` (the ULA-slot substitution). The
per-suite breakdown is the "Row-to-suite reconciliation" table under "Test Case
Summary".

**The catalogue was amended on 2026-07-22** as a direct consequence of building
it. Two rows were **retired** and two **adopted**; the total is unchanged at 91.

- **LR-127 and LR-128 are RETIRED — the rows were wrong**, and the
  implementation is what proved it. Both asserted that outside the shared NR
  `$1A` window the ULA pixel shows through and that nothing is blanked purely by
  the clip. The VHDL says the opposite: `lores.vhd:115` and `zxula.vhd:562`
  compare the same counters against the **same** registered clip signals
  (zxnext.vhd:4258-4261 vs 4437-4471), so outside the window
  `ula_clipped_2 = '1'` (zxnext.vhd:7063) and the whole ULA/LoRes slot goes
  transparent (zxnext.vhd:7100-7104) — falling to the NR `$4A` fallback. The
  full retirement record, including what each row claimed, is in group 7. The
  implementer **skipped** both rather than implementing to a wrong spec or
  quietly editing this catalogue; the `skip()` rows stayed in
  `compositor_test.cpp` as visible tombstones until 2026-07-24, when they were
  removed under the owner-authorized zero-skip mandate — this document is now
  the sole retirement record (see group 7).
- **LR-127a is ADOPTED** (group 7, tier C) — the corrected observable, with a
  whole-frame sweep proving no ULA colour reaches the display area anywhere.
- **LR-PSCAN is ADOPTED** (group 8, tier C) — it pins the per-scanline replay of
  NR `$15` b7 / `$32` / `$33` / `$6A` that "Documented limitations" *mandates in
  prose* but the original catalogue carried no row for.

**LR-140 — resolved 2026-07-24 (was skipped 2026-07-22 → 2026-07-24):**

- The 2026-07-22 skip reported, correctly, that jnext's live ULA render path
  never produced an `ula_select_bgnd` assertion: `Ula::compute_ulanext_pixel`
  computed the flag but every render-path call site in `src/video/ula.cpp`
  discarded it, so no stimulus could reach zxnext.vhd:6987. The 2026-07-24
  re-audit ruled that state a **jnext emulator gap, not a hardware
  unreachability**: on the FPGA the stimulus is trivially guest-reachable
  (NR `$43` bit 0 ULAnext enable, zxnext.vhd:5394/6813, plus any NR `$42`
  format outside the `{01,03,07,0F,1F,3F,7F}` table — zxula.vhd:516-527
  `when others` asserts `ula_select_bgnd` for every paper pixel, and
  zxula.vhd:498-502 asserts it for the border when the format is `$FF`), and
  zxnext.vhd:6986-6991 then substitutes the NR `$4A` fallback (expanded per
  :6990) for the palette lookup unless a LoRes pixel is present. Per the
  1:1:1 rule the missing consumer was implemented (`ula_select_bgnd` →
  per-line NR `$4A` fallback in the ULAnext render branches; the LoRes
  bypass is the unconditional `Renderer::apply_lores` overwrite that models
  the `lores_pixel_en_1 = '1'` disjunct of :6987) and the row became a live
  `check()` in the same change. The row also carries a stimulus-validity
  guard leg — the same ULA state with LoRes disabled must show the NR `$4A`
  fallback — so the row cannot pass vacuously if the consumer regresses.

## Original status (pre-implementation)

**Not implemented; zero rows exist.** As of v0.98.85:

- NR `$15` bit 7 (`nr_15_lores_en`) is stored but never read — the read-back
  handler sources it from the cached last-written byte and says so
  ([src/core/emulator.cpp:1838](../../src/core/emulator.cpp#L1838)).
- NR `$32` / `$33` (LoRes scroll X/Y) have no wiring at all.
- NR `$6A` is cached only ([src/core/emulator.cpp:2056-2061](../../src/core/emulator.cpp#L2056)).
- There is no LoRes renderer and no `lores_test` suite.

Consequence: any program that enables LoRes shows the classic ULA screen
instead — usually attribute garbage, because the program is using bank 5 for
LoRes data. Known affected: TX-1696 ([#53](https://github.com/jorgegv/jnext/issues/53))
and ScrollNutter ([#50](https://github.com/jorgegv/jnext/issues/50)).

## Purpose

LoRes is a 128×96 chunky display mode with two colour depths (8-bit "LoRes"
and 4-bit "Radastan"), independent X/Y hardware scrolling, and a 4-bit palette
offset. It is **not a separate compositor layer**: it substitutes its pixel for
the ULA's pixel inside the ULA layer slot, so everything downstream — the ULA
palette, the ULA palette-select, global transparency, the ULA/tilemap stencil
and blend modes, and the NR `$15` layer priority — treats the LoRes colour as
if the ULA had produced it. The VHDL says this in as many words
(zxnext.vhd:7097-7098: *"stop pretending lores and ula are separate — only one
of these can get a colour, the ula contains the lores colour when lores is
active"*).

This plan covers register decode and reset state, the enable gate, both depth
modes, address generation for both, scroll in both axes including wrap and its
hardware quirk, palette offset in all three of its forms, the ULA-slot
substitution, the shared clip window, and the compositor-stage interactions.

## VHDL Oracle

### Source files

- `cores/zxnext/src/video/lores.vhd` (117 lines) — the entire LoRes pixel generator
- `cores/zxnext/src/zxnext.vhd` — NextREG decode, instantiation and wiring,
  bank-5 VRAM arbitration, ULA-slot mux, palette lookup, compositor
- `cores/zxnext/src/video/zxula.vhd` — `border_active`, `o_ula_clipped`,
  `ula_pixel` (for the substitution's counterparty)
- `cores/zxnext/src/video/zxula_timing.vhd` — the `phc` counter LoRes indexes with
- `cores/zxnext/nextreg.txt` — register documentation (secondary; the VHDL wins)

Paths are relative to the FPGA worktree
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.

### NextREG map

| NextREG | VHDL signal | Function | Citation |
|---------|-------------|----------|----------|
| `$15`[7] | `nr_15_lores_en` | LoRes enable | zxnext.vhd:1138, 5229, 5939 |
| `$1A` | `nr_1a_ula_clip_x1/x2/y1/y2` | **Shared** ULA/LoRes clip window | zxnext.vhd:4258-4261, 6775-6783 |
| `$1C`[2] | `nr_1a_ula_clip_idx` reset | Resets the shared clip write index | zxnext.vhd:5285-5287 |
| `$32` | `nr_32_lores_scrollx` | LoRes X scroll (8 bit) | zxnext.vhd:1179, 5340, 6027 |
| `$33` | `nr_33_lores_scrolly` | LoRes Y scroll (8 bit) | zxnext.vhd:1180, 5343, 6030 |
| `$43`[1] | `nr_43_active_ula_palette` | Palette bank LoRes indexes | zxnext.vhd:6825, 6981 |
| `$6A`[5] | `nr_6a_lores_radastan` | 0 = 8-bit LoRes, 1 = Radastan 4-bit | zxnext.vhd:1203, 5456, 6099 |
| `$6A`[4] | `nr_6a_lores_radastan_xor` | XOR applied to the Timex display-file select | zxnext.vhd:1204, 5457, 6796 |
| `$6A`[3:0] | `nr_6a_lores_palette_offset` | Palette offset | zxnext.vhd:1205, 5458, 6797 |
| `$68`[7] | `nr_68_ula_en` | ULA output disable — **also blanks LoRes** | zxnext.vhd:6809, 7103 |
| `$14` | `nr_14_global_transparent_rgb` | Global transparency, applied to the LoRes colour | zxnext.vhd:7100 |
| `$4A` | `nr_4a_fallback_rgb` | Fallback colour — **bypassed** for LoRes pixels | zxnext.vhd:6987-6990 |

There is **no** LoRes-specific clip register. `nr_1d_*` signals exist only as
commented-out declarations (zxnext.vhd:1167-1171, 6785-6793) and NR `$1D` is not
decoded in the NextREG write case statement — it falls through to
`when others => null`. `nextreg.txt` says so too, in the `$1A` entry: *"LoRes
may get a separate clip window in the future"*.

### Register reset values

The NextREG state block resets on `reset <= i_RESET` (zxnext.vhd:1730, 4930).

| Field | Value | Citation |
|-------|-------|----------|
| `nr_15_lores_en` | `'0'` | zxnext.vhd:4948 |
| `nr_32_lores_scrollx` | `0x00` | zxnext.vhd:4995 |
| `nr_33_lores_scrolly` | `0x00` | zxnext.vhd:4997 |
| `nr_6a_lores_radastan` | `'0'` | zxnext.vhd:5032 |
| `nr_6a_lores_palette_offset` | `0x0` | zxnext.vhd:5033 |
| `nr_6a_lores_radastan_xor` | `'0'` | zxnext.vhd:5034 |
| `nr_1a_ula_clip_x1` / `x2` / `y1` / `y2` | `0x00` / `0xFF` / `0x00` / `0xBF` | zxnext.vhd:4971-4974 |
| `nr_1a_ula_clip_idx` | `"00"` | zxnext.vhd:4975 |

So after reset LoRes is off, unscrolled, in 8-bit mode, offset 0, and the shared
clip covers the whole 256×192 display.

### Key VHDL identities

Reproduced verbatim in meaning from `lores.vhd`; rows below reference them by name.

- **Input counters** (zxnext.vhd:4250-4251): `hc_i` is **`phc`**, not `hc` —
  the "practical counter for 256×192" which is 0..255 across the active display
  and ≥256 elsewhere (zxula_timing.vhd:66, 509-519). `vc_i` is `vc`.
- **X** (lores.vhd:82): `x = phc(7:0) + scroll_x`, 8-bit, wraps mod 256.
- **Y pre-wrap** (lores.vhd:84): `y_pre = vc + ('0' & scroll_y)`, 9-bit.
- **Y wrap** (lores.vhd:86-87): `y(7:6) = y_pre(7:6) + 1` when `y_pre >= 192`
  (9-bit compare), else `y_pre(7:6)`; `y(5:0) = y_pre(5:0)`; `y_pre(8)` is
  discarded. For `y_pre < 384` this is exactly `y = (vc + scroll_y) mod 192`.
  For `y_pre >= 384` it is **not** — see LR-109.
- **8-bit address** (lores.vhd:91, 93-94):
  `addr_pre = y(7:1) & x(7:1)` (14 bits — 128 bytes per row, 2 display pixels
  per LoRes pixel in each axis);
  `addr(13:11) = addr_pre(13:11) + 1` when `y >= 96` (8-bit compare), else
  unchanged; `addr(10:0) = addr_pre(10:0)`.
  Net effect: rows 0..47 land at bank-5 offsets `0x0000-0x17FF` and rows 48..95
  at `0x2000-0x37FF` — i.e. `$4000-$57FF` and `$6000-$77FF` in the classic map,
  with the `0x1800-0x1FFF` attribute area skipped (lores.vhd:22-25).
- **Radastan address** (lores.vhd:96):
  `addr_rad = dfile & y(7:1) & x(7:2)` (14 bits — 64 bytes per row). No `+1`
  correction: 96 rows × 64 bytes = `0x1800`, contiguous from the half's base.
  `dfile = 0` → `0x0000-0x17FF`; `dfile = 1` → `0x2000-0x37FF` (lores.vhd:27-30).
- **Mode mux** (lores.vhd:98): `mode_i = 0` selects `addr`, `1` selects `addr_rad`.
- **dfile source** (zxnext.vhd:6796):
  `lores_dfile_0 = port_ff_screen_mode(0) XOR nr_6a_lores_radastan_xor`, where
  `port_ff_screen_mode` is `port_ff_dat_tmx(5:0)` (zxnext.vhd:3634). Used **only**
  in Radastan mode.
- **8-bit pixel** (lores.vhd:102, 111):
  `pixel = (data(7:4) + palette_offset) & data(3:0)`. The 4-bit add wraps; the
  low nibble passes through untouched.
- **Radastan pixel** (lores.vhd:106-107, 111):
  low nibble = `data(7:4)` when `x(1) = '0'`, else `data(3:0)`;
  high nibble = `palette_offset` when `ulap_en_i = '0'`, else
  `"11" & palette_offset(1:0)`.
  `ulap_en_i = ulap_en_0 AND NOT ulanext_en_0` (zxnext.vhd:4246), i.e. ULA+
  enabled (port `$BF3B`/`$FF3B`, latched at 6815) and ULANext disabled.
- **Pixel enable / clip** (lores.vhd:115):
  `lores_pixel_en = '1'` iff `phc ∈ [clip_x1, clip_x2]` **and**
  `vc ∈ [clip_y1, clip_y2]`, all inclusive, comparing the 9-bit counters against
  zero-extended 8-bit clip values. The clip inputs are the **ULA** clip
  registers (zxnext.vhd:4258-4261), with `clip_y2` clamped to `0xBF` whenever
  `nr_1a_ula_clip_y2(7:6) = "11"` (zxnext.vhd:6779-6783). There is no separate
  display-area test: it falls out of the comparison, because `phc(8)` is set
  outside the 256-pixel window and `clip_y2 <= 0xBF < 192`.
- **Enable gate** (zxnext.vhd:6817, 6903-6904, 6933):
  `lores_pixel_en_1 = lores_pixel_en_1a AND lores_en_1`, where `lores_en_*` is
  the pipelined `nr_15_lores_en`. The LoRes module itself is never gated — it
  fetches from bank 5 every pixel regardless.
- **ULA-slot substitution** (zxnext.vhd:6980-6981):
  `ulalores_pixel_1 = lores_pixel_1 when lores_pixel_en_1 = '1' else ula_pixel_1`;
  `ulatm_pixel_1 = ('0' & ula_palette_select_1 & ulalores_pixel_1)` on the ULA
  half-cycle. So the LoRes byte is an **8-bit index into the 256-entry ULA
  palette**, in the bank chosen by NR `$43`[1].
- **Fallback bypass** (zxnext.vhd:6986-6991):
  the palette colour is taken when `lores_pixel_en_1 = '1' OR ula_select_bgnd_1 = '0'`;
  the NR `$4A` fallback is substituted otherwise. A LoRes pixel therefore never
  shows the fallback colour, whatever the ULA's `select_bgnd` says.
- **Clip cancel** (zxnext.vhd:7063):
  `ula_clipped_2 = ula_clipped_1 AND NOT lores_pixel_en_1`. Given that
  `o_ula_clipped` (zxula.vhd:562) and `lores_pixel_en` (lores.vhd:115) compare
  the **same** counters against the **same** registered clip values
  (zxnext.vhd:4258-4261 vs 4437-4471), the two can never both be asserted, so
  this term is a **no-op** in the shipped core. See "Corrections to prior claims".
- **Memory** (zxnext.vhd:6558-6578, 6603-6631):
  LoRes reads the dedicated bank-5 dual-port BRAM through port A, sharing it
  with the CPU (`vram_bank5_a0 <= ... when cpu_bank5_sched = '1' else lores_addr`,
  6631). It is a raw 14-bit bank-5 offset (lores.vhd:56) — **not** an MMU
  translation, and **never** bank 7 (the shadow-screen mux at zxnext.vhd:6651-6655
  is on the ULA's port B only).

## Test Architecture

### Tiers

Rows are tagged with the tier that owns them:

- **U** — unit: a `lores_test` suite driving the LoRes pixel generator directly.
  The VHDL entity's own interface (lores.vhd:37-61) is the natural shape:
  inputs `(mode, dfile, ulap_en, palette_offset, phc, vc, clip, scroll, byte)`,
  outputs `(pixel, pixel_en)`. A generator with that boundary makes every
  addressing, palette and clip row a pure function test with no frame rendering.
- **C** — compositor: rows about substitution into the ULA slot, transparency,
  fallback, stencil/blend and priority. These belong in `compositor_test`
  (which already drives the real `Renderer::render_row`), not in `lores_test`.
  *As shipped:* 27 of the 29 C rows are in `compositor_test`; LR-163 and LR-164
  (contention and floating bus) landed in `test/lores/lores_integration_test.cpp`
  instead, because they need a whole `Emulator`, not a `Renderer`.
- **N** — NextREG: register decode / read-back / reset rows. *As shipped:* they
  are in `test/nextreg/nextreg_integration_test.cpp` (group `LoRes-NR`), not the
  bare `nextreg_test` this plan named — LoRes register effects are only visible
  through a full emulator.
- **S** — screenshot: whole-frame rows in `test/00regression`, for the geometry
  that only a rendered frame proves.

**No row in this plan is tier S, and that is deliberate.** A screenshot row's
expected value *is* a reference PNG, and there is no way to author one before a
LoRes renderer exists to produce it — the image would have to be generated from
the implementation, which is precisely the ordering this plan refuses (see the
banner). S rows become appropriate once groups 1-4 are green and a LoRes frame
can be rendered from known bank-5 content; at that point a reference image is a
*check* on an already-proven pixel generator rather than a *definition* of it.
The natural first S row is a LoRes NEX loading screen — read the "Adjacent
finding" section below before writing it.

### Column conventions

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |

- **Stimulus** lists every non-default register; anything unlisted is at its
  reset default from the table above.
- **Expected** is mechanical — a byte offset, a pixel index, an enable bit, or a
  read-back value. No row says "renders correctly".
- **VHDL cite** is the line the expected value is computed from. A row with no
  citation is not written; see "Deliberately not specified".

### Reference values used by several rows

For the 8-bit mode with all scrolls at 0 and `phc = 2·cx`, `vc = 2·cy`
(`cx ∈ [0,127]`, `cy ∈ [0,95]`), the byte offset is

```
off(cx, cy) = cy < 48 ?  cy*128 + cx
                      :  0x2000 + (cy-48)*128 + cx
```

and in Radastan mode

```
off_rad(cx, cy) = dfile*0x2000 + cy*64 + (cx >> 1),  nibble = high if cx even
```

Both follow directly from lores.vhd:91/93-94 and :96/106.

## Test Case Catalog

### Group 1 — Register decode, read-back and reset (LR-01 … LR-12)

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-01 | N | NR `$15` bit 7 stores the LoRes enable | write `$15` = `0x80` | read `$15` bit 7 = 1, bits 6:0 unchanged | zxnext.vhd:5229, 5939 |
| LR-02 | N | NR `$15` bit 7 reset value | reset | read `$15` bit 7 = 0 | zxnext.vhd:4948 |
| LR-03 | N | NR `$15` bit 7 is independent of bits 6:0 | write `$15` = `0xFF` then `0x7F` | bit 7 clears, bits 6:0 keep `0x7F` | zxnext.vhd:5229 |
| LR-04 | N | NR `$32` stores 8 bits of X scroll | write `$32` = `0xA5` | read `$32` = `0xA5` | zxnext.vhd:5340, 6027 |
| LR-05 | N | NR `$32` reset value | reset | read `$32` = `0x00` | zxnext.vhd:4995 |
| LR-06 | N | NR `$33` stores 8 bits of Y scroll | write `$33` = `0xC3` | read `$33` = `0xC3` | zxnext.vhd:5343, 6030 |
| LR-07 | N | NR `$33` reset value | reset | read `$33` = `0x00` | zxnext.vhd:4997 |
| LR-08 | N | NR `$6A` bit 5 = Radastan select | write `$6A` = `0x20` | read `$6A` = `0x20` | zxnext.vhd:5456, 6099 |
| LR-09 | N | NR `$6A` bit 4 = dfile XOR | write `$6A` = `0x10` | read `$6A` = `0x10` | zxnext.vhd:5457, 6099 |
| LR-10 | N | NR `$6A` bits 3:0 = palette offset | write `$6A` = `0x0F` | read `$6A` = `0x0F` | zxnext.vhd:5458, 6099 |
| LR-11 | N | NR `$6A` bits 7:6 are not stored and read back 0 | write `$6A` = `0xFF` | read `$6A` = `0x3F` | zxnext.vhd:5456-5458, 6099 |
| LR-12 | N | NR `$6A` reset value | reset | read `$6A` = `0x00` | zxnext.vhd:5032-5034 |

### Group 2 — Enable gate and ULA-slot substitution (LR-20 … LR-31)

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-20 | C | With NR `$15` bit 7 = 0 the ULA pixel is untouched everywhere | LoRes data written to bank 5; `$15`[7]=0 | every display pixel equals the ULA-only reference frame, bit for bit | zxnext.vhd:6933, 6980 |
| LR-21 | C | With NR `$15` bit 7 = 1 every display pixel comes from LoRes | `$15`[7]=1; distinct LoRes and ULA content in bank 5 | all 256×192 display pixels take LoRes values; none take ULA values | zxnext.vhd:6980 |
| LR-22 | C | LoRes never paints the border | `$15`[7]=1, border colour ≠ any LoRes colour | all border cells keep the port `$FE` border colour | lores.vhd:115; zxula.vhd:414-415; zxula_timing.vhd:513-517 |
| LR-23 | U | `pixel_en` is 0 for `phc >= 256` | `phc` = 256, 300, 464 | `pixel_en = 0` for each | lores.vhd:115 |
| LR-24 | U | `pixel_en` is 0 for `vc >= 192` at default clip | `vc` = 192, 255, 300 | `pixel_en = 0` for each (default `clip_y2 = 0xBF`) | lores.vhd:115; zxnext.vhd:4974 |
| LR-25 | U | `pixel_en` is 1 at the display corners | `(phc,vc)` = (0,0), (255,0), (0,191), (255,191) | `pixel_en = 1` for each | lores.vhd:115 |
| LR-26 | C | LoRes occupies the ULA slot in NR `$15` priority, it is not a new layer | `$15`[7]=1 with priority modes 000..101 in turn | LoRes hides/is hidden exactly where the ULA would be in each mode | zxnext.vhd:6980-6981 |
| LR-27 | C | NR `$68` bit 7 (ULA disable) blanks LoRes too | `$15`[7]=1, `$68`[7]=1 | display falls through to the layer below / NR `$4A`; no LoRes pixel survives | zxnext.vhd:7103-7104 |
| LR-28 | C | LoRes replaces the ULA pixel in Timex hi-res mode, both 512-mode half-pixels taking the same LoRes colour | port `$FF` = hi-res mode, `$15`[7]=1 | each pair of adjacent 512-grid columns carries one identical LoRes colour; no hi-res detail survives | zxnext.vhd:6843 vs 6858, 6980, 6986 |
| LR-29 | C | ULA attribute FLASH does not modulate a LoRes pixel | attributes with bit 7 set, `$15`[7]=1, two frames 16 frames apart | both frames identical in the display area | zxula.vhd:470; zxnext.vhd:6980 |
| LR-30 | C | The LoRes byte indexes the **ULA** palette, not the Layer 2 / sprite / tilemap palette | write distinguishable values to ULA palette entry `k` and L2 palette entry `k` | the LoRes pixel takes the ULA palette colour | zxnext.vhd:6960-6978, 6981 |
| LR-31 | C | NR `$43` bit 1 selects which of the two ULA palette banks LoRes indexes | same LoRes index, the two ULA banks holding different colours, toggle `$43`[1] | the emitted colour follows the selected bank | zxnext.vhd:6825, 6981 |

### Group 3 — 8-bit mode addressing (LR-40 … LR-51)

All rows: `mode = 0` (NR `$6A` bit 5 = 0), scrolls 0 unless stated.

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-40 | U | Top-left LoRes pixel reads bank-5 offset 0 | `phc=0`, `vc=0` | `addr = 0x0000` | lores.vhd:91 |
| LR-41 | U | Row stride is 128 bytes | `phc=0`, `vc=2` | `addr = 0x0080` | lores.vhd:91 |
| LR-42 | U | Column stride is 1 byte per 2 display pixels | `phc=2`, `vc=0` | `addr = 0x0001` | lores.vhd:91 |
| LR-43 | U | A LoRes pixel is a 2×2 block of display pixels | `(phc,vc)` ∈ {(4,6),(5,6),(4,7),(5,7)} | all four give `addr = 0x0182` | lores.vhd:91 |
| LR-44 | U | Last byte of the top half | `phc=254`, `vc=94` | `addr = 0x17FF` | lores.vhd:91, 93 |
| LR-45 | U | First byte of the bottom half skips the attribute area | `phc=0`, `vc=96` | `addr = 0x2000`, **not** `0x1800` | lores.vhd:93-94 |
| LR-46 | U | Last byte of the bottom half | `phc=254`, `vc=190` | `addr = 0x37FF` | lores.vhd:91, 93 |
| LR-47 | U | No address in `0x1800-0x1FFF` is ever generated in 8-bit mode | sweep all `phc ∈ [0,255]`, `vc ∈ [0,191]`, scrolls 0 | no generated address falls in `[0x1800, 0x1FFF]` | lores.vhd:93-94 |
| LR-48 | U | The half-select uses the **scrolled** y, not `vc` | `vc=0`, `scroll_y=96` | `addr = 0x2000` (y = 96 ⇒ `+1` on bits 13:11) | lores.vhd:86-87, 93 |
| LR-49 | C | LoRes reads physical bank 5, independent of the MMU slot mapping | map bank 5 out of the CPU address space entirely, then render | output unchanged | zxnext.vhd:6631, 6558-6578; lores.vhd:56 |
| LR-50 | C | LoRes is unaffected by the port `$7FFD` bit 3 shadow-screen select | write different content to banks 5 and 7, toggle `$7FFD`[3] | LoRes always shows bank-5 content | zxnext.vhd:6631 vs 6651-6655 |
| LR-51 | U | In 8-bit mode the Timex display-file bit and NR `$6A` bit 4 have no effect | `mode=0`, all four combinations of port `$FF`[0] and `$6A`[4] | identical addresses in all four | lores.vhd:96, 98 |

### Group 4 — Radastan 4-bit mode (LR-60 … LR-69)

All rows: `mode = 1` (NR `$6A` bit 5 = 1), scrolls 0 unless stated.

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-60 | U | Radastan row stride is 64 bytes | `phc=0`, `vc=2`, `dfile=0` | `addr = 0x0040` | lores.vhd:96 |
| LR-61 | U | Two LoRes pixels per byte | `phc ∈ {0,1,2,3}`, `vc=0` | all four give `addr = 0x0000` | lores.vhd:96 |
| LR-62 | U | `x(1) = 0` selects the HIGH nibble (left pixel of the pair) | byte `0xAB` at the fetched address; `phc=0` then `phc=2` | pixel low nibble `0xA` then `0xB` | lores.vhd:106 |
| LR-63 | U | `dfile = 0` bases the image at offset 0 | `dfile=0`, `phc=0`, `vc=190` | `addr = 0x17C0` | lores.vhd:96 |
| LR-64 | U | `dfile = 1` bases the image at offset `0x2000` | `dfile=1`, `phc=0`, `vc=0` | `addr = 0x2000` | lores.vhd:96 |
| LR-65 | U | Radastan applies **no** `+0x800` correction at row 48 | `phc=0`, `vc=96`, `dfile=0` | `addr = 0x0C00` (not `0x1400`) | lores.vhd:96 vs 93-94 |
| LR-66 | C | `dfile` = port `$FF` bit 0 XOR NR `$6A` bit 4 | all four combinations of `$FF`[0] and `$6A`[4], distinct content at `0x0000` and `0x2000` | `(0,0)`→half 0, `(1,0)`→half 1, `(0,1)`→half 1, `(1,1)`→half 0 | zxnext.vhd:6796 |
| LR-67 | U | The Radastan image is 6144 bytes, contiguous within its half | sweep the display with `dfile=0` | every generated address ∈ `[0x0000, 0x17FF]`; all 6144 are reachable | lores.vhd:96 |
| LR-68 | U | Switching NR `$6A` bit 5 switches the address generator, nothing else latched | same `phc`/`vc`, toggle `mode` | address switches between the 8-bit and Radastan formulas with no residual state | lores.vhd:98 |
| LR-69 | C | Radastan and 8-bit mode reach different bytes for the same screen position | `phc=8`, `vc=4`: 8-bit → `0x0104`, Radastan `dfile=0` → `0x0082` | both observed on the same bank-5 content | lores.vhd:91, 96 |

### Group 5 — Palette offset and pixel composition (LR-80 … LR-88)

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-80 | U | 8-bit: offset adds to the HIGH nibble only | byte `0x35`, offset `0x2` | pixel `0x55` | lores.vhd:102, 111 |
| LR-81 | U | 8-bit: the high-nibble add wraps at 4 bits with no carry out | byte `0xF7`, offset `0x3` | pixel `0x27` | lores.vhd:102 |
| LR-82 | U | 8-bit: the low nibble is passed through untouched by any offset | byte `0x0F`, offset `0xF` | pixel `0xFF` | lores.vhd:111 |
| LR-83 | U | 8-bit: offset 0 is the identity | byte `0x9C`, offset `0x0` | pixel `0x9C` | lores.vhd:102 |
| LR-84 | U | Radastan: high nibble **is** the offset (not an add) | byte `0xAB`, offset `0x5`, `x(1)=0`, `ulap_en=0` | pixel `0x5A` | lores.vhd:107, 111 |
| LR-85 | U | Radastan: with ULA+ enabled the high nibble becomes `"11" & offset(1:0)` | byte `0xAB`, offset `0x1`, `x(1)=0`, `ulap_en=1` | pixel `0xDA` (`0b1101_1010`) | lores.vhd:107 |
| LR-86 | U | Radastan + ULA+: offset bits 3:2 are ignored | offsets `0x1` and `0xD`, `ulap_en=1` | identical pixel (`0xDA` in both) | lores.vhd:107 |
| LR-87 | C | ULANext cancels the ULA+ translation of the Radastan high nibble | `ulap_en` source set, NR `$43` ULANext enable set, offset `0x1` | pixel `0x1A` — high nibble `0x1`, not `0xD` | zxnext.vhd:4246 |
| LR-88 | U | The offset never affects `pixel_en` | offsets `0x0` and `0xF`, position fixed | `pixel_en` identical | lores.vhd:111, 115 |

### Group 6 — Scrolling (LR-100 … LR-111)

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-100 | U | X scroll advances the source column | `scroll_x=4`, `phc=0`, `vc=0` | `addr = 0x0002` (image moves left by 2 LoRes pixels) | lores.vhd:82, 91 |
| LR-101 | U | The X-scroll LSB is discarded in 8-bit mode | `scroll_x=0` vs `1`; then `2` vs `3` | pairs give identical addresses; the two pairs differ | lores.vhd:82, 91 |
| LR-102 | U | X scroll wraps mod 256 (= mod 128 LoRes pixels) | `scroll_x=250`, `phc=10`, `vc=0` | `addr = 0x0002` (x = 4) | lores.vhd:82 |
| LR-103 | U | X scroll wraps within the row, never into the next row | sweep `phc` at `scroll_x=200`, `vc=0` | every address ∈ `[0x0000, 0x007F]` | lores.vhd:82, 91 |
| LR-104 | U | Radastan X scroll granularity: the low two bits select nibble then byte | `scroll_x` = 0,1,2,3 at `phc=0` | 0,1 → high nibble of byte 0; 2,3 → low nibble of byte 0 | lores.vhd:82, 96, 106 |
| LR-105 | U | Y scroll advances the source row | `scroll_y=4`, `phc=0`, `vc=0` | `addr = 0x0100` | lores.vhd:84-87, 91 |
| LR-106 | U | The Y-scroll LSB is discarded away from the wrap boundary | `scroll_y=10` vs `11` at `vc=0` | identical addresses | lores.vhd:86-87, 91 |
| LR-107 | U | Y scroll wraps mod 192 | `scroll_y=100`, `vc=100` ⇒ `y_pre=200` | `y = 8`, `addr = 0x0200` | lores.vhd:84-87 |
| LR-108 | U | Y wrap is exact across the whole legal range | for all `scroll_y ∈ [0,192]`, all `vc ∈ [0,191]` | `y == (vc + scroll_y) mod 192` | lores.vhd:84-87 |
| LR-109 | U | **Hardware quirk**: for `vc + scroll_y >= 384` the wrap is wrong | `scroll_y=193`, `vc=191` ⇒ `y_pre=384` | `y = 192` (**not** 0); the row reads outside the 96-row image | lores.vhd:84-87 |
| LR-110 | U | The quirk's address consequence: the 3-bit `+1` field wraps to 0 | `y = 224` (`scroll_y=255`, `vc=161` ⇒ `y_pre=416`), `phc=0` | `addr_pre = 0x3800`, `addr = 0x0000` (bits 13:11 = `7 + 1 → 0`) | lores.vhd:93-94 |
| LR-111 | U | Scroll registers are independent | `scroll_x=6`, `scroll_y=8` | `addr = 0x0203` at `phc=0, vc=0` | lores.vhd:82, 84, 91 |

### Group 7 — Clip window (LR-120 … LR-126, LR-127a)

The clip window is the **shared ULA** window, NR `$1A`. LoRes has no window of
its own.

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-120 | U | LoRes IS clipped by NR `$1A` — it is not exempt | clip = (64,191,32,159), `phc=32`, `vc=100` | `pixel_en = 0` | lores.vhd:115; zxnext.vhd:4258-4261 |
| LR-121 | U | Clip X bounds are inclusive at both ends | clip x = (64,191); `phc` = 63, 64, 191, 192 | `pixel_en` = 0, 1, 1, 0 | lores.vhd:115 |
| LR-122 | U | Clip Y bounds are inclusive at both ends | clip y = (32,159); `vc` = 31, 32, 159, 160 | `pixel_en` = 0, 1, 1, 0 | lores.vhd:115 |
| LR-123 | U | Clip X is in 256-pixel display units (it indexes `phc`), so a clip edge can fall mid-LoRes-pixel | clip x2 = 127, then 128 | last drawn LoRes column is 63, then 64 (half of it) | lores.vhd:115; zxnext.vhd:4250 |
| LR-124 | N/C | `clip_y2` values with bits 7:6 = `"11"` clamp to `0xBF` | write NR `$1A` y2 = `0xFF`, then `0xC0` | both behave as `0xBF` | zxnext.vhd:6779-6783 |
| LR-125 | U | An inverted X window (`x1 > x2`) draws nothing | clip x = (200,100) | `pixel_en = 0` for all `phc` | lores.vhd:115 |
| LR-126 | U | An inverted Y window (`y1 > y2`) draws nothing | clip y = (150,50) | `pixel_en = 0` for all `vc` | lores.vhd:115 |
| ~~LR-127~~ | — | **RETIRED 2026-07-22 — the row was wrong.** See below. | — | — | — |
| ~~LR-128~~ | — | **RETIRED 2026-07-22 — the row was wrong.** See below. | — | — | — |
| LR-127a | C | LoRes and the ULA share **one** clip window and are suppressed **together**: inside NR `$1A` the LoRes pixel draws; outside it the pixel falls to the NR `$4A` fallback, and no ULA colour reaches the display area anywhere | `$15`[7]=1, clip = (64,191,32,159), distinct ULA content in bank 5, NR `$4A` set to a colour in neither content set | `phc=100,vc=100` → the LoRes colour; `phc=32,vc=100` and `phc=100,vc=10` → the NR `$4A` fallback; no display pixel in the whole frame carries a ULA colour | lores.vhd:115; zxula.vhd:562; zxnext.vhd:4258-4261 vs 4437-4471, 7100-7104 |

#### LR-127 and LR-128 — retired, with the reason

They are recorded here rather than deleted. This plan's discipline is that a row
is never quietly adjusted to the implementation; the mirror of that rule is that
a row is never quietly removed either — otherwise the same wrong observable gets
rediscovered from the VHDL half-read and re-added.

**What they claimed.**

- **LR-127** (tier C, cited zxnext.vhd:6980): *"Outside the clip window the ULA
  pixel shows through"* — stimulus `$15`[7]=1 with clip (64,191,32,159) and
  distinct ULA content; expected *"inside the window LoRes, outside it the ULA
  screen"*.
- **LR-128** (tier C, cited zxnext.vhd:7063 + lores.vhd:115 + zxula.vhd:562):
  *"A clipped-away LoRes region is not blanked but ULA-substituted"* — expected
  *"no pixel anywhere is blank/fallback purely because of the clip"*.

**Why they are wrong.** Both assume the ULA is *not* clipped where LoRes is. It
is — by the very same window:

- `lores.vhd:115` compares `hc_i`/`vc_i` against `clip_x1_i…clip_y2_i`, and
  `zxula.vhd:562` computes `o_ula_clipped` from `i_phc`/`i_vc` against
  `i_ula_clip_x1…i_ula_clip_y2`. The two port maps
  (zxnext.vhd:4258-4261 for LoRes, zxnext.vhd:4437-4471 for the ULA) wire **both**
  to the same registered signals `ula_clip_x1_0` / `x2_0` / `y1_0` / `y2_0`.
  Inside the display area the two comparators are therefore exact complements.
- zxnext.vhd:7063 — `ula_clipped_2 <= ula_clipped_1 and not lores_pixel_en_1`.
  Outside the window `lores_pixel_en_1 = '0'` and `ula_clipped_1 = '1'`, so
  `ula_clipped_2 = '1'`; the cancel term does not fire.
- That drives `ula_mix_transparent` → `ula_transparent` → `ula_rgb <= "000000000"`
  (zxnext.vhd:7100-7104). The **entire ULA/LoRes slot goes transparent** outside
  the window, so the raw ULA pixel cannot "show through" (refuting LR-127), and a
  region *is* blanked purely by the clip — falling to the NR `$4A` fallback if no
  other layer covers it (refuting LR-128).

**What survives.** The rows' *underlying* claim — that the zxnext.vhd:7063 cancel
term is a no-op in the shipped core — is correct and is unchanged; see
"Documented limitations" below. Only their stated observables were wrong.

**Replacement.** **LR-127a**, catalogued above, asserts the VHDL-derived outcome,
including the direct refutation of LR-127 (a whole-frame sweep proving no ULA
colour reaches the display area). The shared-clip behaviour is therefore not left
untested.

**Row IDs are not recycled.** LR-127 and LR-128 stay burned. From 2026-07-22 to
2026-07-24 the suite (`test/compositor/compositor_test.cpp`) additionally emitted
a `skip()` for each, so the retirement was visible in the test output while this
amendment settled. On **2026-07-24** those two tombstone `skip()` calls were
**removed** under the owner-authorized zero-skip mandate (a `skip` row advertises
outstanding work, and a retirement is not outstanding work — the corrected
observable is live as LR-127a). This document is the retirement record; the IDs
remain burned and must not be reused.

### Group 8 — Compositor-stage interaction (LR-140 … LR-146, LR-PSCAN)

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-140 | C | A LoRes pixel never shows the NR `$4A` fallback colour | ULA condition that would assert `select_bgnd` (ULANext format `0x00`), `$15`[7]=1 | the LoRes palette colour is emitted, not `$4A` | zxnext.vhd:6986-6991 |
| LR-141 | C | The LoRes colour is subject to NR `$14` global transparency | ULA palette entry for the LoRes index set to the NR `$14` RGB | the pixel is transparent; the layer below shows | zxnext.vhd:7100-7101 |
| LR-142 | C | Transparency compares the palette **RGB[8:1]**, not the palette index | NR `$14` = `0xE3`; LoRes index `0xE3` mapped to a non-`0xE3` RGB, and a different index mapped to RGB `0xE3` | only the second is transparent | zxnext.vhd:7100 |
| LR-143 | C | LoRes participates in ULA/tilemap stencil mode as the ULA colour | `$68`[0]=1, tilemap enabled, `$15`[7]=1 | the stencil AND uses the LoRes colour | zxnext.vhd:7112-7113, 7130-7132 |
| LR-144 | C | LoRes participates in NR `$15` blend modes 6/7 as the ULA colour | priority `110`, `$68`[6:5] = `00`, `$15`[7]=1 | the blend's ULA operand is the LoRes colour | zxnext.vhd:7100-7101, 7139-7148 |
| LR-145 | C | The tilemap "below ULA" ordering applies unchanged to LoRes | `$6B`[0] toggled, `$15`[7]=1 | tilemap under / over the LoRes colour exactly as it would be under / over the ULA | zxnext.vhd:7116 |
| LR-146 | C | Sprite and Layer 2 priority relative to the ULA slot is unchanged by LoRes | each priority mode with `$15`[7] = 0 then 1 | the stacking order is identical; only the ULA-slot colour changes | zxnext.vhd:6980, 7139+ |
| LR-PSCAN | C | NR `$15`[7], `$32`, `$33` and `$6A` are replayed **per scanline** — a mid-frame write affects only the rows from the write onward, never rows the beam already passed | frame baseline `$15`[7]=0; mid-frame at display row 40 write `$15`[7]=1 and `$33`=4, snapshotting each subsequent line | row 39 is bit-for-bit the pure-ULA frame; row 40 shows LoRes with `scroll_y = 4` applied | zxnext.vhd:6768-6802 (LoRes params re-latched every `CLK_7`), 6817 (`lores_en` on `CLK_14`) |

**LR-140 — live `check()` since 2026-07-24 (skipped 2026-07-22 → 2026-07-24).**
The skip's factual claim was accurate — jnext's live ULA render path discarded
`ula_select_bgnd` at every `Ula::compute_ulanext_pixel` call site, so no stimulus
could reach zxnext.vhd:6987 — but the 2026-07-24 re-audit classified that as a
**jnext emulator gap (REACHABLE on hardware), not an unreachability of the row**:
NR `$43` bit 0 + a NR `$42` format outside `{01,03,07,0F,1F,3F,7F}` makes every
ULAnext paper pixel assert `ula_select_bgnd` (zxula.vhd:516-527 `when others`;
border with format `$FF` per :498-502), and zxnext.vhd:6986-6991 substitutes the
NR `$4A` fallback unless a LoRes pixel is present. The missing consumer was
implemented (1:1:1 — same change as the un-skip) and the row asserts both halves
of the :6987 disjunct: with `$15`[7]=1 the LoRes palette colour is emitted, and —
the stimulus-validity guard — the identical ULA state with `$15`[7]=0 shows the
NR `$4A` fallback. The row was not reworded; its catalogued stimulus and expected
value are asserted exactly as specified.

**LR-PSCAN keeps its non-numeric ID.** It is the ID the suite emits, and doc/suite
IDs must agree for the traceability extractor; renaming it to `LR-147` would need a
test-source edit this amendment is not permitted to make. It is a full member of
the catalogue, tier C, group 8. It exists because the requirement it pins is stated
in prose in "Documented limitations" below — *"the LoRes implementer must add NR
`$15` bit 7, NR `$32`, NR `$33` and NR `$6A` to the per-line snapshot set, or the
layer will be wrong on exactly the demo that motivated implementing it"* — and the
original catalogue carried no row for it. A mandated behaviour with no row is a
coverage hole with a paragraph in front of it.

### Group 9 — Negative / non-behaviour rows (LR-160 … LR-167)

These exist because each is a plausible wrong implementation.

| ID | Tier | Asserts | Stimulus | Expected | VHDL cite |
|----|------|---------|----------|----------|-----------|
| LR-160 | U/C | NR `$26` / `$27` (ULA scroll) do not move the LoRes image | `$26=64`, `$27=64`, `$15`[7]=1 | LoRes output unchanged | lores.vhd:37-61 (no such port); zxnext.vhd:4241-4271 |
| LR-161 | C | NR `$68` bit 2 (ULA half-pixel scroll) does not move the LoRes image | `$68`[2]=1, `$15`[7]=1 | LoRes output unchanged | zxnext.vhd:4241-4271 |
| LR-162 | N | NR `$1D` is not a LoRes clip register | write `$1D` = arbitrary values, `$15`[7]=1 | no effect on the LoRes image | zxnext.vhd:1167-1171, 5278 (undecoded), 6785-6793 |
| LR-163 | C | Enabling LoRes does not change ULA memory contention | contention-sensitive timing test with `$15`[7] = 0 then 1 | identical T-state counts | zxula.vhd:583; zxnext.vhd:6603-6631 (separate BRAM port) |
| LR-164 | C | Enabling LoRes does not change the floating-bus value | floating-bus read with `$15`[7] = 0 then 1 | identical | zxula.vhd:573 (ULA port B only) |
| LR-165 | C | LoRes does not disturb the ULA's own VRAM fetch | `$15`[7]=1 then 0 within one frame | the ULA screen is intact when LoRes is switched off | zxnext.vhd:6631, 6660 |
| LR-166 | C | NR `$19` (sprite clip) does not clip LoRes | `$15`[7]=1, `$1A` left at its reset default, `$19` clip = (64,191,32,159) | the full 256×192 LoRes image draws — not one pixel is clipped | zxnext.vhd:4258-4261 (LoRes takes `ula_clip_*`), 4366-4369 (`nr_19_*` reaches the sprite module and nothing else) |
| LR-167 | C | NR `$1B` (tilemap clip) does not clip LoRes | `$15`[7]=1, `$1A` left at its reset default, `$1B` clip = (64,191,32,159) | the full 256×192 LoRes image draws — not one pixel is clipped | zxnext.vhd:4258-4261, 4424-4427 (`nr_1b_*` reaches the tilemap module and nothing else) |

LR-166 and LR-167 use the **same numeric window as LR-120**, which asserts the
opposite outcome for NR `$1A`. The three rows together pin down *which* clip
register reaches LoRes, rather than merely that some clipping happens: today only
the correct register is positively tested, so an implementation that grabbed
`$19` or `$1B` would pass every other row in this plan. That slip is plausible
because the four clip registers share one write-index mechanism and one reset
index (zxnext.vhd:5252-5289) and differ only in which module port map consumes
them. The consumers are exhaustive — `nr_19_*` appears at zxnext.vhd:1151-1155
(declaration), 4366-4369 (sprite port map), 4965-4969 (reset), 5252-5258 /
5283 (write decode) and 5956-5960 / 5980 (read-back), and nowhere else;
`nr_1b_*` likewise at 1161-1165, 4424-4427, 4977-4981, 5270-5276 / 5289 and
5972-5976 / 5980.

## Test Case Summary

| Group | Rows | Tier mix | U | C | N |
|-------|------|----------|---|---|---|
| 1 — Register decode / reset | 12 | N | 0 | 0 | 12 |
| 2 — Enable gate / ULA-slot substitution | 12 | U + C | 3 | 9 | 0 |
| 3 — 8-bit addressing | 12 | U + C | 10 | 2 | 0 |
| 4 — Radastan addressing | 10 | U + C | 8 | 2 | 0 |
| 5 — Palette offset / pixel composition | 9 | U + C | 8 | 1 | 0 |
| 6 — Scrolling | 12 | U | 12 | 0 | 0 |
| 7 — Clip window | 8 | U + C/N | 6 | 1 | 1 |
| 8 — Compositor interaction | 8 | C | 0 | 8 | 0 |
| 9 — Negative / non-behaviour | 8 | U/C/N | 1 | 6 | 1 |
| **Total** | **91** | | **48** | **29** | **14** |

(Rows tagged `U/C` or `N/C` are counted once, in the first-named tier.
48 + 29 + 14 = 91.)

**Amendment arithmetic (2026-07-22).** Group 7 lost the two retired rows LR-127
and LR-128 (both tier C) and gained LR-127a (tier C): 9 − 2 + 1 = **8**, C going
2 − 2 + 1 = **1**. Group 8 gained LR-PSCAN (tier C): 7 + 1 = **8**, all C. Group
totals: 12+12+12+10+9+12+8+8+8 = **91**, unchanged, because two rows left and two
joined. Tier totals: U untouched at **48**; C = 29 − 2 + 1 + 1 = **29**; N
untouched at **14**. Retired rows are excluded from every count — they are
history, not coverage.

### Row-to-suite reconciliation

| Tier | Suite | Catalogued rows | live `check()` | `skip()` |
|------|-------|-----------------|----------------|----------|
| U | `test/lores/lores_test.cpp` | 48 | 48 | 0 |
| C | `test/compositor/compositor_test.cpp` (group `LR`) | 27 | 27 | 0 |
| C | `test/lores/lores_integration_test.cpp` | 2 | 2 | 0 |
| N | `test/nextreg/nextreg_integration_test.cpp` (group `LoRes-NR`) | 14 | 14 | 0 |
| | **Total** | **91** | **91** | **0** |

C rows split 27 + 2 = 29. From 2026-07-22 to 2026-07-24 `compositor_test`
additionally emitted two `skip()` rows for the **retired** LR-127 / LR-128
tombstones (29 printed `LR` rows); both tombstones and the LR-140 skip were
resolved on 2026-07-24 (see group 7 and group 8), so the suite now prints exactly
the 27 catalogued C rows, all live.

Suggested implementation order — each block is independently mergeable and each
one leaves the suite green:

1. Groups 1 + 2 (registers and the enable gate) — makes `$15`[7] real and lets
   the ULA-substitution path be proved with a trivial pixel generator.
2. Group 3 + 6 (8-bit addressing and scroll) — the bulk of the pixel maths.
3. Group 5 (palette) and group 4 (Radastan).
4. Groups 7, 8, 9 (clip, compositor, negatives).

## Documented limitations under jnext's per-scanline accuracy model

These are hardware behaviours the plan deliberately does **not** turn into rows,
because jnext's line-accurate model cannot observe them. Each is a modelling
limitation, recorded here so a future reader does not mistake the absence of a
row for an oversight.

- **Per-pixel latching of the LoRes parameters.** `lores_scroll_x_0`,
  `lores_scroll_y_0`, `lores_mode_0`, `lores_dfile_0` and
  `lores_palette_offset_0` are re-latched on every rising `CLK_7`, i.e. once per
  256-mode display pixel (zxnext.vhd:6768-6802); `lores_en_0` is latched on
  `CLK_14` with `sc(0)='1'` (6817). A Copper `MOVE` therefore takes effect
  mid-scanline on hardware. jnext snapshots display state once per scanline
  (`Renderer::snapshot_*`), so a mid-line change is applied from the next line.
  This is the same limitation already accepted for every other layer — see
  [PER-SCANLINE-DISPLAY-STATE-AUDIT.md](../design/PER-SCANLINE-DISPLAY-STATE-AUDIT.md).
  **NR `$15` is a live case, not a hypothetical**: beast.nex toggles `$15`
  between `0x80` and `0x01` mid-frame, and NR `$15` is already listed in that
  audit as needing per-line replay. The LoRes implementer must add NR `$15`
  bit 7, NR `$32`, NR `$33` and NR `$6A` to the per-line snapshot set, or the
  layer will be wrong on exactly the demo that motivated implementing it.
- **Pipeline latency.** `lores_pixel_1` / `lores_pixel_en_1a` are registered one
  `CLK_7` after the module output (zxnext.vhd:6843-6844), and `lores_en` passes
  through two further `CLK_14` stages (6903-6904). The ULA's own pixel is
  registered on the same schedule (6858), so the relative alignment is zero and
  the absolute delay is a sub-pixel pipeline artefact jnext does not model.
- **Bank-5 port-A arbitration.** The CPU wins the shared bank-5 port; the LoRes
  fetch is deferred to the next 28 MHz slot when they collide (zxnext.vhd:6603-6631).
  The fetch still completes inside the pixel period, so there is no visible
  effect — and no added CPU contention, which is why LR-163 asserts the
  *absence* of an effect rather than a timing value.
- **The `ula_clipped_2` cancel term.** zxnext.vhd:7063 ANDs `NOT lores_pixel_en_1`
  into the ULA clipped flag. Because both flags are computed from the same
  counters and the same registered clip values, they are mutually exclusive and
  the term can never change the result in the shipped core — it is a genuine
  no-op. That makes the term itself **unobservable**, and it is why the retired
  LR-128 could not be salvaged by rewording: there is no stimulus under which the
  cancel term changes an output. **LR-127a** asserts the observable that the
  shared window *does* produce (LoRes inside, fallback outside, no ULA anywhere),
  which is the strongest statement available.

## Deliberately not specified

Behaviours found in the VHDL that this plan does **not** turn into rows, with
the reason:

- **`lores_addr_o` is driven even when LoRes is disabled** (the module has no
  enable input; the gate is at zxnext.vhd:6933). Bank 5 is read every pixel
  regardless. This is unobservable — a read has no side effects on a dual-port
  BRAM — so there is nothing to assert.
- **The commented-out separate LoRes clip window** (`nr_1d_*`, zxnext.vhd:1167-1171,
  6785-6793, lores.vhd:48-51 wired to the ULA window at 4258-4261). LR-162
  asserts that NR `$1D` does nothing; the plan does not speculate about the
  future register `nextreg.txt` hints at.
- **The commented-out separate-layer LoRes compositing** (zxnext.vhd:1477, 1491,
  1514-1515, 7006, 7067, 7106-7107, 7127-7129). An earlier core revision treated
  LoRes as its own layer with its own transparency; the shipped core does not.
  Rows are written against the shipped code only.
- **Anything about a LoRes-specific behaviour in 48K / 128K / +3 machine
  timing.** LoRes is unconditional in the FPGA and has no machine-timing input
  (lores.vhd:37-61); a row would be asserting the absence of a dependency the
  VHDL never had.

No row in this plan is uncited. If a future revision cannot cite the VHDL for a
row, that row must not be written — an uncited row is a guess with a test
number attached.

## Corrections to prior claims

Two statements circulating in the issue tracker and the roadmap are wrong; they
are corrected here because the implementer would otherwise build to them.

1. **"LoRes is exempt from the ULA clip window (zxnext.vhd:7063)."** — [#63](https://github.com/jorgegv/jnext/issues/63).
   It is not. `lores.vhd:115` clips the LoRes pixel itself, and the clip inputs
   are the **ULA** clip registers (zxnext.vhd:4258-4261), `clip_y2` clamp
   included. `nextreg.txt` names NR `$1A` "Clip Window ULA/LoRes" and notes
   *"LoRes may get a separate clip window in the future"*. Line 7063 cancels the
   ULA's *clipped* flag where a LoRes pixel exists, which — given identical
   inputs to both comparators — can never fire. Building LoRes as clip-exempt
   would be a visible bug (LR-120, LR-125, LR-126, LR-127a all catch it).

   The first revision of this plan then over-corrected in the opposite direction:
   having established that LoRes *is* clipped, LR-127 and LR-128 assumed the ULA
   *is not*, and asserted the ULA showing through outside the window. It does not
   — the same window suppresses both. Those two rows are retired; see group 7 for
   the full record. The lesson is worth keeping: NR `$1A` is one window feeding
   two comparators, and any claim that names only one of them is suspect.
2. **`doc/design/EMULATOR-DESIGN-PLAN.md` §3.1: LoRes "distributed
   (Compositor+NextREG+ULA)".** Nothing is distributed anywhere; the enable bit
   is not read. Corrected in the same change that adds this plan.

## Adjacent finding — NOT a row in this plan, but read it before any tier-S row

**Confirmed defect, filed as GH [#68](https://github.com/jorgegv/jnext/issues/68)
(2026-07-22). Fix it before writing the first LoRes screenshot row.**

`src/core/nex_loader.cpp:274-308` loads a NEX `SCREEN_LORES` block — and
identically the `SCREEN_HIRES` and `SCREEN_HICOLOUR` blocks — as **12288
contiguous bytes at bank-5 offset 0**, so the second 6144-byte half lands at
`0x1800-0x2FFF`.

The official loader does not. `tbblue/src/asm/nexload/nexload.asm:472-474`
(LoRes; `:488-490` HiRes, `:505-507` HiColour) issues **two** 6144-byte `fread`s,
to `$4000` and then to `$6000` — not `$5800` — leaving `$5800-$5FFF` (bank-5
offset `0x1800-0x1FFF`) untouched. That is exactly the attribute-area gap
`lores.vhd:93-94` skips and LR-47 asserts is never read. So the bottom half
belongs at bank-5 offset `0x2000-0x37FF`, and jnext places it `0x800` too early.

This is no longer the open question an earlier revision of this section recorded:
it was verified against the official loader source, and the hardware layout the
VHDL implies and the layout the loader writes agree with each other and disagree
with jnext.

It matters *here* because the first LoRes screenshot row will very likely be a
NEX file, and **until #68 is fixed a misplaced image half looks exactly like a
LoRes addressing bug** — the bottom 48 rows wrong, the top 48 right, which is
also the signature of a missing `+1` on `lores_addr(13:11)` (lores.vhd:93).
An implementer debugging that symptom against an unfixed loader will chase the
wrong subsystem. See also the tier-S note in "Test Architecture" above.

## Open questions (the VHDL did not fully resolve)

- **Does `scroll_y > 192` occur in real software?** The wrap quirk (LR-109,
  LR-110) is a genuine hardware behaviour, but `nextreg.txt` documents NR `$33`
  as "0-191". If a future oracle comparison (CSpect / real hardware) disagrees
  with LR-109, the disagreement is worth resolving before relaxing the row —
  the VHDL is unambiguous about what the gates do.
- **Radastan + ULA+ palette region.** `"11" & offset(1:0)` (lores.vhd:107) places
  the pixel in the `0xC0-0xFF` region of the ULA palette, which is where the
  ULA+ CLUT is written (zxnext.vhd:6958). LR-85/LR-86 assert the index maths,
  which is all the VHDL states; whether every ULA+-mode program expects that
  region to be populated is a software question, not a hardware one.

## Coverage notes (moved from the traceability matrix, GH #196)

The matrix is a generated artifact now and carries no prose of its own; it
links here instead. These notes were written alongside the rows they explain.

The LoRes layer (`src/video/lores.{h,cpp}`), 128x96 8-bit and Radastan 4-bit.
Plan: [LORES-TEST-PLAN-DESIGN.md](LORES-TEST-PLAN-DESIGN.md), whose row titles
are the Description column below. LoRes is **not** a layer of its own — it
substitutes the ULA-slot pixel (`zxnext.vhd:6980`), which is why its rows are
about address generation and the NR `$15` b7 / `$32` / `$33` / `$6A` inputs
rather than about compositing.
The plan has 91 rows; this suite implements the 48 that are reachable from the
`LoRes` class alone. Two more need a running CPU and the real memory-timing
model and live in the companion suite below; the remainder are compositor- or
demo-level and are recorded in the sections that own them.
The two LoRes plan rows that assert the ABSENCE of an effect on the CPU-visible
side of the machine, and therefore need a full 128K emulator with contention
and a floating bus rather than the bare `LoRes` class. Both follow from one
hardware fact: the LoRes fetch uses bank 5's dual-port BRAM **port A**
(`zxnext.vhd:6603-6631`) while ULA contention and the floating bus are
functions of the ULA's own **port B** fetch.
