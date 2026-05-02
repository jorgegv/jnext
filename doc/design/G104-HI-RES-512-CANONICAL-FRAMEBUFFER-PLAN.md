# G104 — HI_RES 512px Refactor — Comprehensive Implementation Plan

**Status:** master plan, post-decisions-locked. Subsequent phase agents execute against this verbatim.
**Date:** 2026-05-02

---

## Section A — Decisions and assumptions (locked)

### A.1 Locked architectural decisions

1. **Single canonical framebuffer width = 640 px.**
   - Layout: `64 left border + 512 display + 64 right border = 640`.
   - Vertical dimension unchanged: 256 rows total = `32 top border + 192 display + 32 bottom border`.
   - Constants collapse: `Renderer::FB_WIDTH = 640`, `DISP_X = 64`, `DISP_W = 512`, `DISP_Y = 32`, `DISP_H = 192`, `FB_HEIGHT = 256`.
   - Removed: `Renderer::FB_WIDTH_HI`, `Renderer::composite_width_`, `Renderer::hi_res_active_`, `Emulator::FRAMEBUFFER_WIDTH_MAX`, `Emulator::last_frame_width_`.

2. **GUI vertical 2× scaling.**
   - Framebuffer in memory: `640 × 256` ARGB.
   - Qt widget renders at physical `(640 × scale_) × (512 × scale_)`. Each framebuffer row drawn twice during prescale.
   - `EmulatorWidget::NATIVE_W = 640`, `NATIVE_H = 256` (in-memory shape); the prescale step pixel-doubles vertically into `scaled_` (logical "native height" for window-sizing purposes is `256 × 2 = 512`).
   - Window sizing: `setFixedSize(qRound(640 × scale / dpr), qRound(512 × scale / dpr))`.
   - Letterbox math: `sx = pw / 640`, `sy = ph / 512`.

3. **PNG screenshot output = 640×512 (square pixels).**
   - The 640×256 in-memory framebuffer is vertically doubled at PNG-export time so users get correct CRT-faithful 4:3 geometry without post-processing.
   - Applies to: GUI screenshot path (`MainWindow`, `QtApp`), CLI `--delayed-screenshot` (both SDL and headless), and FFmpeg video recording.
   - Implementation: `save_screenshot_png` always doubles vertically (or accepts a doubling flag — see Section B for the cleaner option). Video recorder doubles at `capture_frame` time.

### A.2 Sprite-pixel-doubling rule (VHDL evidence)

**Sprites are clocked at 7 MHz unconditionally** (`sprites.vhd:1004,1017,1037` keying off `i_CLK_7`, instantiated at `zxnext.vhd:4332`). The compositor reads from sprite line buffers at the 14 MHz pixel clock. Therefore each sprite output pixel covers two consecutive 14 MHz pixels regardless of layer mode — sprites are **always pixel-doubled into the 640-wide framebuffer**.

Implementation rule: the sprite engine internally addresses a 320-position grid (matching its 9-bit X coordinate space for the Spectrum Next), and writes each pixel twice into the 640-wide line buffer at positions `2x` and `2x + 1`.

### A.3 Hi-res interleaving rule (VHDL evidence — NEW finding)

**Discovery during planning:** VHDL `zxula.vhd:131-138, 384-389` shows HI_RES `shift_reg_32` interleaves at **byte granularity**, not bit granularity:

```
shift_pbyte <= (pbyte00 & pbyte01)              -- 16 bits = 2 bytes from screen 0 (0x4000)
shift_abyte <= (abyte00 & abyte01)              -- 16 bits = 2 bytes from screen 1 (0x6000)
shift_reg_32 <= shift_pbyte(15:8) & shift_abyte(15:8) & shift_pbyte(7:0) & shift_abyte(7:0)
                when shift_screen_mode(2)='1'
shift_reg_ld <= shift_left(shift_reg_32, scroll)  -- emits MSB-first
```

So in a 16-cycle "column pair" window, the 16 emitted hi-res pixels are: **8 px from screen-0 column N (bits 7..0), then 8 px from screen-1 column N (bits 7..0)**. Then the next 16 px: 8 from s0 col N+1, 8 from s1 col N+1.

The existing comment in `src/video/ula.cpp:945-953` claims a bit-granularity interleave (`s1_b7, s0_b7, s1_b6, s0_b6, …`). **That comment contradicts VHDL.** The Phase 2 (ULA renderer) work must implement the byte-granularity rule, not the bit-granularity one. This is logged as a discovery, not a USER QUESTION — the VHDL is unambiguous and constitutes the spec.

### A.4 Stated assumptions

- **Line-cap arrays stay at 320.** The per-line state arrays in `Renderer` (e.g. `fallback_per_line_<320>`, `ula_enabled_per_line_<320>`, `transparent_rgb_per_line_<320>`, `stencil_mode_per_line_<320>`, `blend_mode_per_line_<320>`) and Ula (`ulap_en_per_line_<320>`) are indexed by **scanline number**, not by pixel column. The size 320 is a generous LINE cap (FB_HEIGHT=256 + headroom). These remain 320 — do not touch them.
- **Pentagon timing constant `(448, 320, …)` at `emulator_config.h:145` stays.** That `320` is `max_vc + 1` (lines per frame for Pentagon PAL), not framebuffer width.
- **Floating-bus check at `emulator.cpp:3870` (`line < 64 || line >= 256`) stays.** That `256` is `max_vc`-related vertical timing, not framebuffer width.
- **Sprite engine 9-bit X (`x & 0x1FF` for 0..511 wrap) stays.** Sprite VHDL uses 9-bit X; the wrap stays. Only the `if (screen_x >= DISPLAY_WIDTH) continue;` check changes (DISPLAY_WIDTH = 320 → 640).
- **`PaletteManager::FB_HEIGHT_HEADROOM`-style line caps (palette.cpp:434, palette.h:307) stay.** They are vertical, not horizontal.
- **VHDL is the spec.** When the existing C++ comment disagrees with VHDL (e.g. the hi-res bit-order comment), VHDL wins. This is mandated by `doc/testing/UNIT-TEST-PLAN-EXECUTION.md` §1.
- **Existing G102 (palette mirror collapse) and G105 (HI_RES 6-bit border encoding) stay as-is.** G104 builds on top of them.

### A.5 USER QUESTIONS surfaced during planning (none new beyond locked decisions)

None. The 3 locked decisions resolve the only architecturally ambiguous points. Section B below identifies a few minor implementation-style choices (e.g. "screenshot doubling: always vs flag") that are internal trade-offs decidable by the Phase 7 agent without user input — they are documented inline.

---

## Section B — File-by-file change inventory

For each file: changes (with line citations), impact, dependencies. Numbered `B.N` sections grouped by phase to make task assignment trivial. All paths are absolute under `/home/jorgegv/src/spectrum/jnext/`.

### B.1 — Constants and headers (Phase 1)

#### B.1.1 `src/video/renderer.h`

- **Lines 34-36** — collapse `FB_WIDTH=320 / FB_WIDTH_HI=640` into single `FB_WIDTH=640`. Delete `FB_WIDTH_HI` line.
- **Lines 39, 41** — `DISP_X = 32 → 64`, `DISP_W = 256 → 512`. (`DISP_Y=32`, `DISP_H=192` unchanged.)
- **Lines 270-283** — `render_frame` becomes `void render_frame(...)` (drops the `int` return that carried `composite_width_`). Update the doc-comment to remove the `@return` line; it's no longer dynamic.
- **Lines 354-362** — change `std::array<…, FB_WIDTH_HI>` → `std::array<…, FB_WIDTH>` (which is now 640) for `ula_line_`, `layer2_line_`, `sprite_line_`, `tilemap_line_`, `tm_pixel_below_`, `tm_pixel_textmode_`, `layer2_priority_`, `ula_border_`. (Visually no change in storage; the symbol is now the canonical 640.)
- **Lines 364-368** — delete `bool hi_res_active_` and `int composite_width_` member declarations.
- **Lines 370-373** — `composite_scanline` signature loses the `int width` parameter. Update the doc-comment.
- **Lines 182-183, 198-199, 218-219, 224-225, 235-236, 241-242, 247-248, 254-255** — these are LINE-cap checks (`if (line >= 0 && line < 320)`). The `320` here is `≥ FB_HEIGHT=256` headroom — line, not pixel. **Do not touch.** (Document this in commit message to forestall reviewer confusion.)
- **Lines 329, 337, 343, 346, 349** — same: line-cap arrays sized 320. **Do not touch.**

Impact: any `.cpp` consumer of `FB_WIDTH_HI`, `composite_width_`, or `hi_res_active_` must change. The `render_frame` return is no longer assignable. Layer renderers' per-line-buffer-pointer parameters become parameter-free w.r.t. width.

Dependencies: B.1.2 (Ula constants), B.6.1 (emulator), B.6.2 (renderer.cpp).

#### B.1.2 `src/video/ula.h`

- **Lines 30-39** — update doc-comment ASCII art: `64px left + 512×192 display + 64px right = 640`. Top/bottom borders unchanged at 32 rows.
- **Line 46** — `FB_WIDTH = 320 → 640`.
- **Lines 50-52** — `DISP_X = 32 → 64`, `DISP_W = 256 → 512`. (`DISP_Y=32`, `DISP_H=192` unchanged.)
- **Lines 593, 597** — update render_frame / render_scanline doc-comment ("Pointer to FB_WIDTH × FB_HEIGHT" wording stays valid; just confirm reads correctly post-rename).
- **Lines 624, 701, 716, 730, 736** — comments referring to "320 pixels" / "32px borders" / "256 active display" need updating to "640 / 64 / 512". Pure comment churn but important for doc fidelity.
- **Lines 622** — `border_per_line_` is FB_HEIGHT-sized — NOT a width; stays.
- **Lines 638** — `ulap_en_per_line_<320>` is line-indexed (FB_HEIGHT-headroom cap). **Stays at 320.**
- **Lines 729-736** — the doc-comment for `render_display_line_hires` documents the "256-pixel approximation"; rewrite the comment to describe the new native 512-pixel rendering path (s0/s1 byte-interleaved per Section A.3).
- **Render-mode private declarations (lines 700-733)**: signatures unchanged — they all take `uint32_t* row` plus screen_row plus mmu, no width parameter. The implementations change but the API is stable.

Impact: ula.cpp implementation (Phase 2) needs to emit 640 across all 4 modes (`render_display_line` for STANDARD/STANDARD_1 and HI_COLOUR via `render_display_line_hicolour`, plus `render_display_line_hires` for the native-512 path, plus `render_border_line`).

Dependencies: B.2.1 (ula.cpp).

#### B.1.3 `src/core/emulator.h`

- **Line 175** — comment `up to 640×256` → just `640×256` (single canonical).
- **Line 180-181** — `int get_framebuffer_width()` either returns the constant directly (`return FRAMEBUFFER_WIDTH;`) or is removed entirely (callers use `Emulator::FRAMEBUFFER_WIDTH` constant). **Recommendation: keep the getter, return constant** to minimize churn at call sites. Remove `last_frame_width_` member.
- **Lines 488-491** — collapse to `static constexpr int FRAMEBUFFER_WIDTH = 640; static constexpr int FRAMEBUFFER_HEIGHT = 256; static constexpr int FRAMEBUFFER_PIXELS = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT;` Delete `FRAMEBUFFER_WIDTH_MAX` and `FRAMEBUFFER_PIXELS_MAX`.
- **Line 574-578** — comment update; delete `int last_frame_width_ = FRAMEBUFFER_WIDTH;` member.

Impact: `emulator.cpp:59-60` (init), `:3438-3445`, `:4365`, `:4466` (render_frame call sites) all reduce: no return value to capture, framebuffer always 640×256.

Dependencies: B.6.1 (emulator.cpp), B.7.1 (sdl_app.cpp) and B.7.2 (qt_app.cpp) and B.7.3 (headless_app.cpp), B.7.4 (main_window.cpp) all consume `last_frame_width_` indirectly via `get_framebuffer_width()`.

#### B.1.4 `src/gui/emulator_widget.h`

- **Lines 10-11** — comment update: `(320×256) → (640×256)` and explain the "1 in-memory row = 2 displayed rows" vertical doubling rule.
- **Line 23, 37** — comment text update.
- **Line 38** — `NATIVE_W = 320 → 640`.
- **Line 39** — `NATIVE_H = 256` unchanged.
- Add a new constant for the post-vertical-doubling logical height used in window sizing: e.g. `static constexpr int DISPLAY_H = NATIVE_H * 2;` (or inline the `* 2` factor in the call sites — Phase 7 chooses).

Impact: `emulator_widget.cpp` `set_scale` and `prescale` rewritten to vertically double; `qt_app.h` `NATIVE_W/NATIVE_H` (B.7.5), `main_window.cpp` window-sizing arithmetic.

Dependencies: B.7 (GUI work).

#### B.1.5 `src/gui/qt_app.h`

- **Lines 108-109** — `NATIVE_W = 320 → 640`. `NATIVE_H = 256` stays; if a "displayed height" is needed for window-sizing, add `static constexpr int DISPLAY_H = NATIVE_H * 2;` to mirror `EmulatorWidget`.

Dependencies: B.7.5 (qt_app.cpp).

### B.2 — ULA renderer (Phase 2)

#### B.2.1 `src/video/ula.cpp`

- **Lines 358-365** — `render_frame` doc-comment + per-row line-pointer arithmetic uses `FB_WIDTH`. Now 640. No code change, but the in-comment ASCII update from B.1.2 must be reflected.
- **Lines 367-368, 397-398** — `framebuffer + row * FB_WIDTH`. Code already uses `FB_WIDTH`; just verify after constant flip.
- **Lines 422-454** — `render_scanline` per-line border save/restore uses `FB_HEIGHT`. No change.
- **Lines 473-491** — `render_scanline_screen1` similarly. No change.
- **Lines 591-781** — `render_display_line` (STANDARD / STANDARD_1):
  - **Line 613-624** — Left-border fill: loop bound `0 < DISP_X` becomes `0 < 64`. Pixel-double: each border ARGB written into 64 cells (was 32). No structural change because loop uses constant.
  - **Lines 656-717** — Fast-path inner loop: 32 columns × 8 bits per byte = 256 source pixels. To emit 512 pixels per source byte (2 displayed pixels per source bit) within the 512-wide DISP_W, **double-write the same ARGB**: `*dst++ = ink/paper; *dst++ = ink/paper;` per source bit. Two output cells per bit. The destination index becomes `row + DISP_X + col * 16` (was `* 8`).
  - **Lines 723-774** — Scrolled path: `for (int disp_x = 0; disp_x < 256; ++disp_x)` becomes `for (int disp_x = 0; disp_x < 512; ++disp_x)`, and `src_x = fold_ula_x(disp_x / 2, scroll_x, fine);` (each pair of output pixels reads the same source bit). Verify this matches the VHDL `shift_left(shift_reg_32, …)` semantics: in std-ULA mode `shift_screen_mode(2)='0'` and the doubling is INTRINSIC TO `shift_reg_32` (lines 390-393 emit each `shift_pbyte(N)` twice). So the doubling is VHDL-faithful, not an emulator approximation.
  - **Lines 777-780** — Right-border fill: `FB_WIDTH - DISP_X - DISP_W = 640 - 64 - 512 = 64` cells (was 32). Loop bound updates automatically via constant.
- **Lines 806-886** — `render_display_line_hicolour` (HI_COLOUR mode) — same shape change as STANDARD: 256 source pixels → 512 emitted with each source bit doubled. Border fills at 64.
- **Lines 908-970** — `render_display_line_hires` (HI_RES mode) — **THE G104 CORE CHANGE**:
  - Replace the existing 256-pixel approximation with native 512-pixel rendering per Section A.3 VHDL byte-interleaving rule:
    ```
    For each col in 0..31:
        b0 = vram_read(screen0_base + col, mmu)   // pbyte0N, N=col%2
        b1 = vram_read(screen1_base + col, mmu)   // abyte0N
        // VHDL zxula.vhd:389: shift_reg_32 holds [pbyte_hi][abyte_hi][pbyte_lo][abyte_lo]
        // and shifts MSB-first. So for column index col, the 16 emitted hi-res pixels are:
        //   [b0 bits 7..0]  then  [b1 bits 7..0]
        // i.e. 8 px from screen 0 followed by 8 px from screen 1.
        uint32_t* dst = row + DISP_X + col * 16;
        for (int bit = 7; bit >= 0; --bit) *dst++ = (b0 >> bit) & 1 ? ink_argb : paper_argb;
        for (int bit = 7; bit >= 0; --bit) *dst++ = (b1 >> bit) & 1 ? ink_argb : paper_argb;
    ```
  - Border ink/paper still derive from `screen_mode_reg_` bits 2:0 / 5:3.
  - Border fills 64 cells either side. Existing `render_border_line` in ULA path is still used — see below.
  - Update the function-header doc-comment to describe native 512 rendering and remove the "256-pixel approximation" language.
- **Lines 976-1042** — `render_border_line`:
  - All `for (int x = 0; x < FB_WIDTH; ++x)` loops now fill 640 cells (constant flip handles it).
  - HI_RES TMX border path (lines 1006-1041) unchanged structurally (just emits the same `border_argb` 640 times instead of 320).
- **Lines 343, 358-365** — `render_frame` ASCII doc updates: 320×256 → 640×256, 32px borders → 64px borders, 256×192 active → 512×192 active.

**VHDL-faithful HI_RES bit-order verification:** at line 957 of the current code, the comment says s1 contributes the LEFT pixel of each pair. Per VHDL (Section A.3), this is wrong — screen-0 emits FIRST. The new implementation must emit s0 byte first (8 px), then s1 byte (8 px) for each column, matching VHDL byte-level interleaving.

Impact: tests in `test/ula/ula_test.cpp` and `test/ula/ula_integration_test.cpp` that use `std::array<uint32_t, 320>` need to widen to 640. The S5.10 SKIP at `test/ula/ula_test.cpp:621` becomes a real `check()` — see Section B.8.1.

Dependencies: Phase 1 must land first (constants flipped); Phase 6 (compositor) consumes the 640-emit ULA layer.

### B.3 — Layer 2 renderer (Phase 3)

#### B.3.1 `src/video/layer2.h`

- **Lines 116-132** — `render_scanline` interface: drop `int render_width = 320` parameter. Update doc-comment to say "always renders 640 pixels".
- **Lines 134-139** — same for `render_scanline_debug`.

#### B.3.2 `src/video/layer2.cpp`

- **Lines 349-360** — `render_scanline_debug` body: drop `render_width` from signature, drop from forwarding call.
- **Lines 362-365** — `render_scanline` signature: drop `render_width`.
- **Lines 380-382** — change `static constexpr int DISP_Y = 32; DISP_X = 32;` to `DISP_Y = 32; DISP_X = 64;` (only DISP_X widens).
- **Lines 376-422 (resolution_==0, 256×192 8bpp)** — to fit the 640 buffer, pixel-double each emitted ARGB: emit at `dst[DISP_X + 2*x]` AND `dst[DISP_X + 2*x + 1]`. The for-loop `for (int x = 0; x < 256; ++x)` stays at 256 source columns, but each source pixel writes 2 destination cells.
- **Lines 424-466 (resolution_==1, 320×256 8bpp)** — pixel-double the 320-wide native into 640-wide framebuffer. Each source pixel `dst[2x]` AND `dst[2x+1]`. Loop count stays 320; clip values may need re-check (but `clip_x1_eff = clip_x1_ << 1` is already in 0..639 range — verify).
- **Lines 467-519 (resolution ≥ 2, 640×256 4bpp native)** — already emits 640 pixels per row. Drop the `if (render_width == 640) … else …` branch; keep only the 640 native-emit code (the `else` branch was the 320-mode downsampling fallback, now obsolete).

Impact: tests in `test/layer2/layer2_test.cpp` lose the `render_width` parameter; assertions about 640-wide output stay; assertions about 320-wide output need to be re-baselined (the function emits 640 always now).

Dependencies: Phase 1 lands first; Phase 6 (compositor) consumes the 640-emit Layer2 output.

### B.4 — Tilemap renderer (Phase 4)

#### B.4.1 `src/video/tilemap.h`

- **Lines 159-179, 181-186** — drop `int render_width = 320` from both `render_scanline` and `render_scanline_debug`. Update doc-comment.

#### B.4.2 `src/video/tilemap.cpp`

- **Lines 264-281** — `render_scanline_debug` body: drop `render_width`.
- **Lines 283-287** — `render_scanline` signature.
- **Line 299** — `clip_out_width = mode_80col_ ? render_width : 320` becomes `clip_out_width = 640` (mode-independent — both 40-col and 80-col emit 640).
- **Line 315** — `clip_x_shift` logic was: `(mode_80col_ && render_width==640) ? 1 : 0`. New rule: `mode_80col_ ? 1 : 0` (80-col native maps each 640-grid emit pixel to a tilemap pixel; 40-col downsamples 640 → 320 by halving via `clip_x_shift = 0` plus the doubled-emit pattern). Re-verify per VHDL `tilemap.vhd:416-417`.
- **Line 368** — `out_width = mode_80col_ ? render_width : 320` becomes `out_width = 640`.
- **Lines 370-385** — main per-pixel loop: rebase to `for (int screen_x = 0; screen_x < 640; ++screen_x)`. In **80-col mode**, emit native (1:1, tilemap_x = screen_x). In **40-col mode**, emit double-write: each pair of `screen_x = 2k, 2k+1` reads the same tilemap pixel (`tilemap_x = screen_x / 2`). Confirm this matches the VHDL `pixel_en_s` 1:1 mapping at 14 MHz when 80-col, and 1-px-per-2-px replication when 40-col.

Impact: tests in `test/tilemap/tilemap_test.cpp` lose `render_width` parameter; the 320-output assertion path goes away.

Dependencies: Phase 1 first; Phase 6 (compositor) consumes 640-emit.

### B.5 — Sprite engine (Phase 5)

#### B.5.1 `src/video/sprites.h`

- **Line 40** — `DISPLAY_WIDTH = 320 → 640`.
- **Lines 179-190** — update `render_scanline` doc-comment: "640 pixels wide (64 left border + 512 display + 64 right border)".

#### B.5.2 `src/video/sprites.cpp`

- **Line 660 (drain comment)** — comment update only (320 → 640).
- **Line 735** — `bool line_occupied[DISPLAY_WIDTH]` automatically widens to 640 via constant.
- **Lines 813-844** — clip-window arithmetic in absolute framebuffer space:
  - `clip_xs/clip_xe` for `over_border && !border_clip_en`: `clip_xe = 319 → 639`.
  - `clip_xs = clip_x1_ * 2; clip_xe = clip_x2_ * 2 + 1;` — the *2 implies the clip register is in 320-grid units. **Per VHDL `sprites.vhd:1043-1059`, sprite clip is in spr_cur_x grid (9-bit). The doubling here is currently to map 9-bit clip-x1 (0..255 → 0..511) into the 0..319 buffer via `<<1 + …`. Re-verify VHDL semantics under 640-buffer:** the VHDL `x_s = clip_x1 << 1` produces a 9-bit value 0..511 covering the 9-bit framebuffer. In the 640-buffer model with 64-px left border and 512-px display, the clip-coordinate-to-buffer mapping changes. The simplest VHDL-faithful approach: **internally the sprite engine still computes everything in the 320-grid (matching the 9-bit hardware coordinate space), then doubles each emitted pixel into the 640 buffer at the very end.**
- **Lines 855-892** — main per-sprite render loop:
  - The natural rewrite: keep all internal coordinates in the 320-grid (sprite X in 0..319 / 9-bit space; clip in same grid). For each emitted pixel at `screen_x` in 0..319, write to `dst[2*screen_x]` AND `dst[2*screen_x + 1]`. Update collision-line tracker to widen but retain its 320-grid logical addressing (`line_occupied[640]`, write at `2*screen_x` only — collision detection stays at 320-grid resolution since sprites are 320-grid VHDL-natively).
  - Alternative: widen everything to 640. **REJECTED** — sprite VHDL is at 9-bit/14 MHz with each pixel covering 2× the 14 MHz clock, so the 320-grid is the truthful internal coordinate. Doubling at emit-time is the VHDL-faithful model.
- **Line 860** — `if (screen_x >= DISPLAY_WIDTH)` — but `DISPLAY_WIDTH` is now 640. The internal screen_x is still in 0..511 (9-bit wrap). This check needs to either:
  - (a) Change to `if (screen_x >= 320) continue;` and double-write at emit, OR
  - (b) Widen the internal arithmetic and check against 640.
  - **Recommended (a):** keeps internal 320-grid (VHDL-faithful 9-bit coordinate space), doubles at emit. The 9-bit sprite X (`spr_x`) directly indexes the 320-grid; doubling is a presentation step.
  - Concretely: after the `screen_x &= 0x1FF; if (screen_x >= 320) continue;` check, the emit becomes `dst[2*screen_x] = colour; dst[2*screen_x + 1] = colour; line_occupied[2*screen_x] = true; line_occupied[2*screen_x + 1] = true;`.
- **Sprite border-clip range (lines 829)** — `clip_xs = 0; clip_xe = 319;` stays at 319 (320-grid). The 640-grid emit doubles.

Impact: tests in `test/sprites/sprite_test.cpp` and `test/sprites/sprite_integration_test.cpp` that buffer 320-wide outputs need to widen to 640; assertions about pixel positions may need to multiply by 2.

Dependencies: Phase 1 first; Phase 6 consumes.

### B.6 — Renderer compositor (Phase 6)

#### B.6.1 `src/core/emulator.cpp`

- **Lines 59-60** — `framebuffer_.assign(FRAMEBUFFER_PIXELS, 0xFF000000u);` (was `FRAMEBUFFER_PIXELS_MAX`). Delete `last_frame_width_ = FRAMEBUFFER_WIDTH;`.
- **Lines 3438-3440** — `renderer_.render_frame(framebuffer_.data(), …)` — drop the `last_frame_width_ = …` capture (return is void).
- **Lines 3443-3445** — `video_recorder_.capture_frame(framebuffer_.data(), FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT)` — replace `last_frame_width_` with the constant.
- **Lines 4365, 4466** — same: drop the assignment, just call `render_frame(...)`.
- **Line 3870** — `if (line < 64 || line >= 256 || …)` — vertical timing for floating bus, unrelated to FB width. **Stays.**
- **Lines 4041, 4118** — `Renderer::FB_HEIGHT` references (FB_HEIGHT=256 unchanged) — stay.
- All references to `FRAMEBUFFER_WIDTH` and `FRAMEBUFFER_HEIGHT` in this file remain valid via the new (collapsed) constants.

Impact: `last_frame_width_` member no longer exists; any reader was via `get_framebuffer_width()` which now returns the constant.

Dependencies: Phase 1 lands first.

#### B.6.2 `src/video/renderer.cpp`

- **Lines 48-52** — `render_frame` becomes `void`; drop `int composite_width_ = …` return logic at line 319.
- **Lines 71-74** — delete the `hi_res_active_ = …` and `composite_width_ = hi_res_active_ ? FB_WIDTH_HI : FB_WIDTH;` lines. Compositor always operates at 640.
- **Line 134** — `uint32_t* out = framebuffer + row * composite_width_` becomes `framebuffer + row * FB_WIDTH`.
- **Lines 137-144** — `std::fill_n(*, composite_width_, …)` becomes `std::fill_n(*, FB_WIDTH, …)` for all 7 layer/flag buffers.
- **Lines 147-169** — `layer2.render_scanline(layer2_line_.data(), row, ram, palette, composite_width_, mmu.rom_in_sram())` becomes `layer2.render_scanline(layer2_line_.data(), row, ram, palette, mmu.rom_in_sram())` (drop the width arg). Same for `tilemap->render_scanline(...)`.
- **Lines 187, 204-235** — ULA clip-window code uses `DISP_X`, `FB_WIDTH`, `DISP_W` — constants flip handles it. Verify the math at the new dimensions: `for (int x = DISP_X; x < DISP_X + DISP_W; ++x)` is now `64..575` (576 = 64+512). Outer-border fill `for (int x = DISP_X + DISP_W; x < FB_WIDTH; ++x)` is now `576..639`. Correct.
- **Lines 238-280** — **DELETE THE ENTIRE PIXEL-DOUBLING BLOCK.** Comment block + `if (hi_res_active_)` body all go. Layers now emit 640 directly.
- **Lines 282-283** — `composite_scanline(out, fb_argb, composite_width_)` becomes `composite_scanline(out, fb_argb)` — drop the width parameter.
- **Lines 364-365** — `composite_scanline` signature drops `int width`. The `for (int x = 0; x < width; ++x)` becomes `for (int x = 0; x < FB_WIDTH; ++x)`.
- **Lines 586-602** — compositor trace CSV: column index `x` now 0..639 (640 rows per scanline). Header row is unchanged (column names same). The dump volume per traced frame doubles; document in commit message.

Impact: any test using `renderer_.render_frame(...)` capturing the return value (none expected — we have not surfaced any such test) breaks. Compositor unit tests in `test/compositor/compositor_test.cpp` and `test/compositor/compositor_integration_test.cpp` need re-baselining (line buffers widen).

Dependencies: Phases 2-5 must all have landed (so all layer renderers emit 640). Phase 1 (constants) must have landed.

### B.7 — GUI prescale + screenshot vertical doubling (Phase 7)

#### B.7.1 `src/platform/sdl_app.cpp`

- **Lines 180-181** — `const int fb_w = emulator_.get_framebuffer_width();` becomes `const int fb_w = Emulator::FRAMEBUFFER_WIDTH;` (or just inline 640). Width fixed.
- **Line 188** — `save_screenshot_png(screenshot_file_, fb, fb_w, fb_h)` — width is now 640, height is 256, but **PNG should be 640×512**. Either: (a) call new `save_screenshot_png_doubled()` helper, or (b) `save_screenshot_png` always doubles vertically internally. **Recommendation (b):** simpler API, can't be called wrong. See B.7.6.

#### B.7.2 `src/platform/sdl_app.h`

- **Line 82** — `NATIVE_W = 320 → 640` (if used; verify call sites — likely just for window-creation arithmetic). Add `NATIVE_H = 256` and a `DISPLAY_H = NATIVE_H * 2 = 512` for window sizing if SDL needs vertical doubling at the platform layer (likely yes — SDL window should be `640 × scale` × `512 × scale` for square pixels).

#### B.7.3 `src/platform/headless_app.cpp`

- **Line 187-189** — `save_screenshot_png(screenshot_file_, emulator_.get_framebuffer(), emulator_.get_framebuffer_width(), emulator_.get_framebuffer_height())` — width/height become 640/256, but the PNG output is 640×512 (B.7.6).

#### B.7.4 `src/gui/main_window.cpp`

- **Lines 348-351** — `save_screenshot_png(path.toStdString(), emulator_->get_framebuffer(), emulator_->get_framebuffer_width(), emulator_->get_framebuffer_height())` — same as above.

#### B.7.5 `src/gui/qt_app.cpp`

- **Lines 210-218** — same `save_screenshot_png` call site, same treatment.
- The `EmulatorWidget::update_frame(emulator_.get_framebuffer(), 640, 256)` call passes the in-memory dimensions; the widget handles vertical doubling at prescale time.

#### B.7.6 `src/platform/screenshot.h` and `src/platform/screenshot.cpp`

**Recommendation: always double vertically inside `save_screenshot_png`.** Simpler, consistent for all callers. Output PNG is always `width × (height * 2)`.

Implementation sketch for `screenshot.cpp`:
```cpp
png_set_IHDR(png, info, width, height * 2, 8, PNG_COLOR_TYPE_RGB, …);
png_write_info(png, info);

std::vector<uint8_t> row(width * 3);
for (int y = 0; y < height; ++y) {
    const uint32_t* src = framebuffer + y * width;
    for (int x = 0; x < width; ++x) {
        uint32_t pixel = src[x];
        row[x*3+0] = (pixel >> 16) & 0xFF;
        row[x*3+1] = (pixel >>  8) & 0xFF;
        row[x*3+2] = (pixel >>  0) & 0xFF;
    }
    // Write the same row twice to vertically double.
    png_write_row(png, row.data());
    png_write_row(png, row.data());
}
```

The `width × height` parameters keep representing the in-memory framebuffer dims. The PNG file is always `width × (height * 2)`. This makes call sites idempotent — no API churn at consumers.

#### B.7.7 `src/core/video_recorder.cpp`

- **Line 135-156** — `capture_frame(framebuffer, width, height)`: same vertical-doubling logic. The first frame's `frame_height_ = height * 2`; the per-frame conversion writes each ARGB row twice to the temp file.
- **Line 143** — `rgb_buffer_.resize(width * height * 3)` becomes `rgb_buffer_.resize(width * (height * 2) * 3)` so the doubled rows fit.
- The FFmpeg invocation in `start()` uses `frame_width_ × frame_height_` to set `-s` and `-video_size`; with `frame_height_ = 512` already accounted for, FFmpeg gets the correct geometry.

#### B.7.8 `src/gui/emulator_widget.cpp`

- **Lines 44-61** (`set_scale`):
  - `lw = qRound(NATIVE_W * factor / dpr) = qRound(640 * factor / dpr)`.
  - `lh = qRound(NATIVE_H * 2 * factor / dpr) = qRound(512 * factor / dpr)` (vertical-doubling factor).
- **Lines 75-115** (`prescale`):
  - `target_h` math: `target_h = NATIVE_H * 2 * fs_scale` in fullscreen.
  - Inner scaling loop: `target_h` rows are sampled from `nh = NATIVE_H` source rows. The current `scanLine(dy * nh / target_h)` already handles arbitrary scaling — works correctly for the doubled height.
  - Fullscreen letterbox: `sx = pw / NATIVE_W = pw / 640`, `sy = ph / (NATIVE_H * 2) = ph / 512`. Set `target_w = NATIVE_W * fs_scale`, `target_h = NATIVE_H * 2 * fs_scale`.
- **Line 84-88** (fullscreen scale calc): replace `NATIVE_H` with `NATIVE_H * 2` (or introduce `DISPLAY_H = 512` constant).

#### B.7.9 `src/gui/main_window.h`

- **Line 26** — comment "exact integer multiple of 320×256" → "exact integer multiple of 640×512 (in-memory 640×256 with vertical 2× doubling)".
- **Line 142** — comment "Default 2x scale (640x512 viewport)" — already correct in spirit, but now the math is `(640*2) × (256*2*2) = 1280 × 1024` for scale=2. Update comment.

Impact: window dimensions change for users — at scale=2 the previous `640×512` becomes `1280×1024`. **This is a user-visible change that must be highlighted in handover memory and (if user agrees) in `FEATURES.md` / ChangeLog.**

Dependencies: B.1.4 / B.1.5 (constants); compatible with any phase order of 2-6.

### B.8 — Tests (Phase 8)

#### B.8.1 `test/ula/ula_test.cpp`

- **Lines 334-380** (and any other 320-buffer test): widen `std::array<uint32_t, 320>` to `std::array<uint32_t, 640>`.
- **Lines 616-622** — un-skip S5.10. Replace the `skip("S5.10", …)` with a real `check()` block that:
  - Loads HI_RES screen-mode (`port_ff_reg_ = 0x06 | (paper << 3) | ink`).
  - Pokes 4 distinct bytes into screen-0 (col 0..1) and 2 into screen-1 (col 0..1) at known display row.
  - Calls `bed.ula.render_scanline(line.data(), DISP_Y, bed.mmu)`.
  - Asserts that `line[DISP_X+0..DISP_X+7]` equal `screen-0 col 0` bits 7..0 mapped to ink/paper, AND `line[DISP_X+8..DISP_X+15]` equal `screen-1 col 0` bits 7..0 mapped to ink/paper. Cite VHDL `zxula.vhd:389-393`.
  - Verify a second pair (col 1) at `line[DISP_X+16..DISP_X+31]` to confirm the byte-interleave continues correctly across columns.
- Add new tests for: (a) HI_RES border at 64-px width left+right; (b) HI_RES s0+s1 independent visibility (the scientific test the SKIP gestured at).

#### B.8.2 `test/ula/ula_integration_test.cpp`

- **Lines 151, 224, 277, 327, 599, 610, 642, 649, 855, 872, 878, 887, 1191, 1284** — every `std::array<uint32_t, 320>` widens to 640. Verify each `line[index]` index references update accordingly (display-area indexes shift from `32..287` → `64..575`).

#### B.8.3 `test/compositor/compositor_test.cpp` and `test/compositor/compositor_integration_test.cpp`

- Any width-related test: widen line buffers to 640. Update DISP_X/DISP_W expectations.

#### B.8.4 `test/layer2/layer2_test.cpp`

- Drop `render_width` argument from all `render_scanline` calls.
- Re-baseline assertions: `dst[…]` indexes for 256×192 and 320×256 modes are now in 640-grid (each source pixel at 2x position).

#### B.8.5 `test/tilemap/tilemap_test.cpp` (if it exists; verify with `find`)

- Drop `render_width`. Re-baseline 80-col (now 640 native) and 40-col (640 with 2x doubling) assertions.

#### B.8.6 `test/sprites/sprite_test.cpp` and `test/sprites/sprite_integration_test.cpp`

- Widen line buffers from 320 to 640 (sprite lines).
- Update sprite-position assertions: each sprite pixel writes at `dst[2*x]` AND `dst[2*x+1]`.
- Verify collision-detection tests still hold (collision is 320-grid logically; doubled at emit).

#### B.8.7 Test-wide `\b320\b` audit

- Run `grep -nrE '\b320\b' test/` and classify each match:
  - **(a) FB-width references** → update.
  - **(b) Non-width 320 references** (machine size, t-states, opcode count, line counts, palette sizes) → leave.
  - **(c) Test fixture data** (e.g. `static const uint32_t expected[320] = {…}`) → regenerate or rewrite.
- Document the classification in the Phase 8 commit message.

#### B.8.8 `test/00regression/regression_tests.conf`

- **Lines 23-24** — `layer2-320x256` and `layer2-640x256` are TEST NAMES (not framebuffer width assertions). Stay as-is.

#### B.8.9 Subsystem test plans (`doc/testing/*-TEST-PLAN-DESIGN.md`)

- `ULA-VIDEO-TEST-PLAN-DESIGN.md`: section S5 ("Timex screen modes"): rewrite the S5.10 row to describe the native 512-pixel rendering. Update any "256-pixel approximation" notes.
- `LAYER2-TEST-PLAN-DESIGN.md`: 256×192 / 320×256 / 640×256 mode descriptions stay accurate, but the framebuffer-mapping section needs a note that all three modes emit at 640 (with 2× / 1.5× / 1× pixel-doubling internally as appropriate).
- `TILEMAP-TEST-PLAN-DESIGN.md`: 40-col / 80-col mode descriptions update similarly.
- `SPRITES-TEST-PLAN-DESIGN.md`: section on 320 DISPLAY_WIDTH → mention 640 buffer with internal 320-grid doubling.
- `COMPOSITOR-TEST-PLAN-DESIGN.md`: rework the per-scanline geometry section, drop references to dynamic 320/640 selection.
- `TRACEABILITY-MATRIX.md`: refresh after `make unit-test` post-Phase-2-and-8 to reflect the closed S5.10 row.
- `SUBSYSTEM-TESTS-STATUS.md` (if present at `doc/testing/`): refresh.

Dependencies: Phase 8 audit can start in parallel with Phases 2-6; concrete un-skip + test edits depend on the corresponding Phase 2 (S5.10), Phase 3, 4, 5, 6 work landing.

### B.9 — Reference regeneration (Phase 9)

#### B.9.1 27 PNG references at `test/00regression/img/`

All 27 reference PNGs change dimensions: were 320×256 (or 640×256 for the rare hi-res-natively-already-using-640 case — TBD per test), now all become 640×512. Diff-based regression will FAIL on every test until references regenerate.

Per `feedback_regression_refs.md`: **never bulk-regenerate**. Regeneration requires per-test user authorization after eyeballing the GUI output.

Phase 9 procedure (Section C):
1. Clean build + gui-release.
2. Run regression once with old refs to confirm all 27 fail and capture the diff list to `/tmp/regression-g104.log`.
3. **Stop. Show user the diff list.**
4. For each test in priority order (beast.nex first, parallax-demo second, then alphabetical), eyeball in the GUI, get user OK, then `bash test/00regression/generate-references.sh <test_name>` (per-test, NOT bulk).
5. After all 27 are regenerated, re-run regression and confirm 33/0/0.

### B.10 — Documentation (Phase 10)

#### B.10.1 `EMULATOR-DESIGN-PLAN.md` (root)

- Refresh framebuffer geometry section: cite the new `640 × 256` in-memory + `640 × 512` displayed convention, the `64 + 512 + 64` horizontal layout, and the sprite-pixel-doubling VHDL-faithfulness.

#### B.10.2 `FPGA-REPO-ANALYSIS.md` (root)

- Search for `320` and `640` references; refresh to match new canonical width.

#### B.10.3 `FEATURES.md` and `TODO.md` (root)

- Per CLAUDE.md: ask user whether G104 closure merits a FEATURES.md entry. **Don't add unilaterally.**

#### B.10.4 `doc/testing/UNIT-TEST-PLAN-EXECUTION.md`

- Search for `320`; refresh if any pixel-width approximations are documented. (Likely no matches — this is a process doc, not a geometry doc.)

#### B.10.5 `ChangeLog`

- Per CLAUDE.md §50-67: do NOT update unless user requests. **Don't.**

### B.11 — Memory + handover (Phase 11)

- Drop **project memory**: `project_g104_640_canonical.md` documenting:
  - 640-canonical decision (in-memory 640×256, displayed 640×512).
  - VHDL evidence chain: sprites at 7 MHz (`sprites.vhd:1004,1017,1037`), ULA shift register pre-doubling at 14 MHz lo-res (`zxula.vhd:389-393`), HI_RES native byte-interleaved (`zxula.vhd:389`).
  - GUI vertical 2× rule (each in-memory row drawn twice).
  - PNG / video output 640×512.
  - Sweep summary (files touched).
  - User-visible window-size change (scale=2 viewport `640×512` → `1280×1024`).
- Drop **feedback memory** (only if a durable rule emerges). Candidate: `feedback_framebuffer_dim_is_format.md` — "framebuffer dimension is part of the format, not a runtime out-of-band signal." Capture this if the work surfaces it as a re-usable rule (likely yes given how much we excise `last_frame_width_` plumbing).

---

## Section C — Phase plan (executable)

Each phase = one branch off LOCAL `main` (per `feedback_local_main_not_origin.md`). Independent reviewer per phase per CLAUDE.md and `feedback_never_self_review.md`. No `git push` (per `feedback_no_unauthorized_push.md`). All commits with terse insightful messages, no Co-Authored-By trailers.

### Phase 1 — Constants and types (BLOCKING PREREQUISITE)

**Branch:** `g104-phase1-constants`.
**Files:** B.1.1 (`renderer.h`), B.1.2 (`ula.h`), B.1.3 (`emulator.h`), B.1.4 (`emulator_widget.h`), B.1.5 (`qt_app.h`), and minor compile fixups in `renderer.cpp`, `ula.cpp`, `emulator.cpp`, `gui/qt_app.cpp`, `gui/main_window.cpp`, `platform/sdl_app.cpp`, `platform/sdl_app.h`, `platform/headless_app.cpp` to adapt to changed signatures + dropped `last_frame_width_`.

To get the build green at the end of Phase 1 without doing the layer-renderer work yet, **introduce a TEMPORARY pixel-doubling pass at the compositor stage** that doubles 320-emit layer outputs into the 640 buffer. (This is essentially the existing `renderer.cpp:240-280` block, but unconditional.) Phase 6 deletes it. Document in commit message that this is a temporary scaffold.

**Deliverables:**
- Build green: `LANG=C make clean && LANG=C make gui-release` succeeds.
- All unit tests pass (some may fail at width assertions; widen them as needed in this same branch since the layer renderers are still emitting 320 internally — fix any test breakage caused only by constant flips, leave layer-specific tests for later phases).
- Screenshot regression: needs new refs (Phase 9). For Phase 1, allow regression to FAIL; reviewer notes which tests fail and confirms the failures are only due to dimension changes, not behavior changes.

**Reviewer:** independent agent. Verify: constants flip is consistent; no `FB_WIDTH_HI` / `composite_width_` / `hi_res_active_` / `last_frame_width_` / `FRAMEBUFFER_WIDTH_MAX` remnants left. Reviewer does NOT need to run regression at this phase (only width-flip — no behavior change yet) — but must verify build green + unit-test green.

**Effort:** M (4-6 hours).
**Risk:** Med — many small touches; one missed reference breaks the build. Rollback: branch deletion.

### Phase 2 — ULA renderer 640-native (parallel with 3, 4, 5)

**Branch:** `g104-phase2-ula`.
**Files:** B.2.1 (`ula.cpp`), B.8.1 (`test/ula/ula_test.cpp`, including S5.10 un-skip), B.8.2 (`test/ula/ula_integration_test.cpp`).

**1:1:1 deliverable:**
- All 4 ULA modes (STANDARD, STANDARD_1, HI_COLOUR, HI_RES) emit native 640 (with VHDL-faithful internal doubling for the 256×192 + HI_COLOUR cases; HI_RES is true 512 across the display area).
- `render_border_line` emits 640.
- S5.10 SKIP retired and replaced with real check verifying s0/s1 byte-interleaved hi-res.
- New tests for HI_RES native 512: assert s0_col0 → display[64..71], s1_col0 → display[72..79], s0_col1 → display[80..87], s1_col1 → display[88..95] (per VHDL `zxula.vhd:389-393`).

**Reviewer:** independent agent. **Per `feedback_review_run_screenshot_regression.md`: REVIEWER MUST RUN headless regression on the representative demos and md5-compare to references.** Specifically:
- beast.nex (sentinel for std-ULA + palette)
- copper-demo (rasterbar / per-line ULA work)
- layer2-320x256 + layer2-640x256 (regression sanity)
- contention-test (ULA timing fidelity)
- floating-bus

If any demo's MD5 mismatches the reference, the reviewer must STOP and surface the diff. Do NOT regenerate references in the review pass.

**Effort:** L (8-12 hours, including HI_RES native + tests).
**Risk:** High — HI_RES bit-order semantics easy to get wrong. The existing comment is wrong; new comment must cite the VHDL block byte-interleave. A wrong implementation will silently produce a half-rendered hi-res image that "looks plausible" but is geometrically incorrect. Mitigation: explicit unit test verifying first 16-px pair structure.

**Rollback:** `git branch -D g104-phase2-ula`; redo on a fresh branch.

### Phase 3 — Layer 2 renderer 640-only (parallel with 2, 4, 5)

**Branch:** `g104-phase3-layer2`.
**Files:** B.3.1 (`layer2.h`), B.3.2 (`layer2.cpp`), B.8.4 (`test/layer2/`).

**Deliverable:**
- `render_scanline` and `render_scanline_debug` lose the `render_width` parameter and emit 640 always.
- 256×192 mode emits 256-wide-source as 512-wide-pixel-doubled at display offset 64.
- 320×256 mode emits 320-wide-source as 640-wide-pixel-doubled (the 320 wide mode physically extends across the full 640).
- 640×256 mode emits native 640 (no doubling).
- All `test/layer2/` tests updated.

**Reviewer:** independent agent + headless regression sweep on `layer2-320x256-reference.png`, `layer2-640x256-reference.png`, `parallax-demo-reference.png` (parallax uses L2 heavily), `dapr-layer2-reference.png`. MD5-compare.

**Effort:** M (4-6 hours).
**Risk:** Med — the `clip_x1_eff` math under 640 framebuffer needs careful re-verification.

### Phase 4 — Tilemap renderer 640-only (parallel with 2, 3, 5)

**Branch:** `g104-phase4-tilemap`.
**Files:** B.4.1 (`tilemap.h`), B.4.2 (`tilemap.cpp`), B.8.5 (`test/tilemap/`).

**Deliverable:**
- 40-col mode: native 320 → 640 with each source pixel doubled.
- 80-col mode: native 640 (no doubling).
- All `test/tilemap/` tests updated.

**Reviewer:** independent + regression on `tilemap-demo-reference.png`, `dapr-tilemap_00-reference.png` / `dapr-tilemap_01` / `dapr-tilemap_02` / `dapr-tilemapper_00` / `dapr-tilemapper_01`. MD5.

**Effort:** M (4-6 hours).
**Risk:** Med — `clip_x_shift` logic re-baselining is subtle.

### Phase 5 — Sprite engine 640-emit (parallel with 2, 3, 4)

**Branch:** `g104-phase5-sprites`.
**Files:** B.5.1 (`sprites.h`), B.5.2 (`sprites.cpp`), B.8.6 (`test/sprites/`).

**Deliverable:**
- `DISPLAY_WIDTH = 640`; internal 320-grid retained; emit doubles each 320-grid pixel into 640 buffer.
- Sprite border-clip range, border-only mode, full-area mode all emit at 640.
- `line_occupied[640]`; collision detection still 320-grid logically (write at `[2x]` only).

**Reviewer:** independent + regression on `sprite-scaling-reference.png`, `sprite-anchor-reference.png`, `parallax-demo-reference.png`, `dapr-sprite-reference.png`. MD5.

**Effort:** M (4-6 hours).
**Risk:** High — sprite VHDL semantics are dense (anchoring, scaling, mirroring, rotation) and the 320-grid-vs-640-grid coordinate translation is easy to confuse. Mitigation: keep ALL internal arithmetic at 320-grid; double ONLY at the final emit.

### Phase 6 — Compositor unconditional 640 (DEPENDS ON 1-5)

**Branch:** `g104-phase6-compositor`.
**Files:** B.6.1 (`emulator.cpp`), B.6.2 (`renderer.cpp`), B.8.3 (`test/compositor/`).

**Deliverable:**
- Drop the 240-280 pixel-doubling block (the temporary scaffold from Phase 1, plus the original conditional doubling code if it's still there).
- Compositor always operates on 640 line buffers.
- `composite_scanline` width parameter dropped.
- `render_frame` becomes `void`.
- Compositor trace CSV at 640 columns per scanline.
- All `test/compositor/` tests updated.

**Reviewer:** independent + FULL regression sweep (all 27 demos). Without re-baseline references, all should fail with dimension diff. Reviewer's job: confirm that every failure is purely dimension-related (640×512 PNG vs the old 320×256-or-640×256), not pixel-content-related. If any failure is content-level (e.g. some pixels look corrupted within the expected 640×512 area), STOP and rework Phase 6 (it would mean a layer is emitting incorrect content).

**Effort:** S-M (3-5 hours).
**Risk:** Med — collation across 4 layer-emit phases. Mitigation: serialize this phase after 1-5 land.

### Phase 7 — GUI prescale + screenshot vertical doubling

**Phase 7 can run in PARALLEL with Phases 2-6 because it touches only `src/gui/`, `src/platform/screenshot.{h,cpp}`, `src/platform/sdl_app.{h,cpp}`, `src/platform/headless_app.{cpp}`, `src/core/video_recorder.cpp`.** No layer code or compositor code involved.

**Branch:** `g104-phase7-gui-vert2x`.
**Files:** B.7.1-B.7.9.

**Deliverable:**
- `EmulatorWidget::set_scale` and `prescale` emit physical 640×scale × 512×scale.
- Fullscreen letterbox math correct.
- `save_screenshot_png` always emits PNG with 2× vertical (so output PNG is `width × (height * 2)` when called with the in-memory 640×256 framebuffer).
- `VideoRecorder::capture_frame` emits doubled frames; `start()` reports correct dims to FFmpeg.
- Window sizes at scale=2/3/4 produce 1280×1024 / 1920×1536 / 2560×2048 (square pixel-perfect 4:3 targets).

**Reviewer:** independent + GUI smoke test (open the emulator, take screenshot, confirm PNG is 640×512). Reviewer must visually inspect.

**Effort:** M (4-6 hours).
**Risk:** High — DPR math + Qt fullscreen letterbox + window-resize chain is fragile. Mitigation: explicit hand-tested checkpoints; existing GUI helpers re-applied.

### Phase 8 — Test sweep + traceability matrix + S5.10 closure

**Branch:** `g104-phase8-tests`.
**Phase 8 may start in parallel with the audit pass during Phases 2-6, but the CONCRETE test edits depend on those phases having landed.**

**Files:** B.8.7-B.8.9 (audit, traceability matrix, subsystem plans).

**Deliverable:**
- All `test/*` files audited for `\b320\b` and `\b256\b` and reclassified.
- S5.10 closed in `test/ula/ula_test.cpp` (handed off to Phase 2 reviewer to confirm).
- Traceability matrix refreshed via `perl test/refresh-traceability-matrix.pl` after `make unit-test` (per `doc/testing/UNIT-TEST-PLAN-EXECUTION.md` §6a).
- `SUBSYSTEM-TESTS-STATUS.md` and 4 subsystem plans (ULA, LAYER2, TILEMAP, SPRITES, COMPOSITOR) updated.

**Reviewer:** independent. Per `feedback_reviewer_for_test_removal.md`, **even retiring the S5.10 SKIP requires an independent reviewer** because that's removing a SKIP row.

**Effort:** M (4-8 hours, mostly mechanical).
**Risk:** Low.

### Phase 9 — Reference regeneration (DEPENDS ON 1-8)

**Branch:** `g104-phase9-refs`.
**Per `feedback_clean_gui_release_for_regression.md` + `feedback_jnext_test_jobs.md` + `feedback_regression_log_to_file.md`:**

Procedure:
1. `LANG=C make clean && LANG=C make gui-release && make unit-test`
2. `JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh 2>&1 | tee /tmp/regression-g104-pre.log`
3. **Expected: all 27 PNG-comparison tests FAIL with dimension diff.**
4. Show user the FAIL list. **STOP.**
5. **Per-test, in priority order:**
   - Test 1: `beast-demo` (sentinel for palette mirror + per-scanline ULA from 2026-05-01).
   - Test 2: `parallax-demo` (sentinel for multi-week parallax work + sprite multiplexing).
   - Test 3-N: alphabetical sweep of remaining 25.
   - For each: launch in GUI (`./build/gui-release/jnext --machine next --load <nex>`), eyeball post-`<delay>` frames, screenshot, compare to user's expectation.
   - Get user OK.
   - `bash test/00regression/generate-references.sh <test_name>` (per-test).
6. Re-run regression with new refs:
   `JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh 2>&1 | tee /tmp/regression-g104-post.log`
7. Confirm 33/0/0.

**Reviewer:** N/A — reference regeneration is a user-authorized operational step, not a code change.

**Effort:** L (4-8 hours of eyeball burden, depending on user availability).
**Risk:** Very High — this is THE point at which a hidden behavioral regression manifests. Each test must be visually inspected. Skipping eyeballing here = regression risk.

### Phase 10 — Documentation

**Branch:** `g104-phase10-docs`.
**Files:** B.10.1-B.10.4. `B.10.5 (ChangeLog)` skipped per CLAUDE.md.

**Reviewer:** light independent review for accuracy.
**Effort:** S (1-2 hours).
**Risk:** Low.

### Phase 11 — Memory + handover

**No branch — main session.**
**Files:** project memory + (optionally) feedback memory.

**Effort:** S (30 min).
**Risk:** Low.

---

## Section D — Cross-phase parallelization map (DAG)

```
                  Phase 1 (constants)  [SERIALIZE — blocks all]
                      |       |       |       |       |       |
              +-------+-------+-------+-------+-------+
              |       |       |       |       |       |
           Phase 2  Phase 3  Phase 4  Phase 5  Phase 7  Phase 8 (audit)
            (ULA)    (L2)    (TM)    (SPR)    (GUI)
              \       \       /       /
               +-------+-------+-------+
                       |
                  Phase 6 (compositor)  [SERIALIZE — needs 2,3,4,5]
                       |
                  Phase 8 (concrete edits + matrix)
                       |
                  Phase 9 (regression refs — user-gated per-test)
                       |
                  Phase 10 (docs)
                       |
                  Phase 11 (memory)
```

**Wave 1:** Phase 1 alone.
**Wave 2:** Phases 2, 3, 4, 5, 7 in parallel (5 agents — matches the project budget per `feedback_parallel_agent_budget_20260421.md`). Phase 8 audit may also run here (audit-only, no code changes yet).
**Wave 3:** Phase 6 alone (after 2-5 land + merge to main).
**Wave 4:** Phase 8 concrete edits (after 6 lands).
**Wave 5:** Phase 9 (user-gated; not parallel-agent-managed — manual operation).
**Wave 6:** Phase 10 + Phase 11 (parallel with each other).

**Estimated total wall-clock:** 1 working day for code changes + 1 day for reference regeneration eyeballing = **~2 working days end-to-end**.

---

## Section E — Per-phase effort + risk

| Phase | Effort | Risk | Concrete failure mode |
|---|---|---|---|
| 1 | M (4-6h) | Med | A `FB_WIDTH_HI` reference left somewhere → link error or runtime size mismatch |
| 2 | L (8-12h) | High | HI_RES bit-order wrong → silently broken Timex hi-res rendering |
| 3 | M (4-6h) | Med | `clip_x1_eff` 640-buffer math wrong → Layer 2 clip window off by 2× |
| 4 | M (4-6h) | Med | `clip_x_shift` rebaseline wrong → tilemap clip wrong |
| 5 | M (4-6h) | High | Sprite 320-grid-vs-640-grid confusion → sprite positions all 2× wrong |
| 6 | S-M (3-5h) | Med | Compositor still references `composite_width_` somewhere → undefined member |
| 7 | M (4-6h) | High | Qt DPR + fullscreen math wrong → window 50% wrong size |
| 8 | M (4-8h) | Low | Mechanical |
| 9 | L (4-8h) | V High | Hidden behavior regression in eyeballed reference → permanent silent bug |
| 10 | S (1-2h) | Low | Doc drift |
| 11 | S (30min) | Low | None significant |

**Rollback for any phase:** branch deletion (`git branch -D <branch>`), return to LOCAL `main`. Per `feedback_local_main_not_origin.md`, no origin contamination because nothing was pushed.

---

## Section F — Process gates (NON-NEGOTIABLE)

After every `src/`-touching phase:

1. **Build:**
   ```
   LANG=C make clean && LANG=C make gui-release
   ```
   Per `feedback_lang_c_builds.md`, `feedback_clean_gui_release_for_regression.md`, `feedback_build_gui_enabled.md`.

2. **Unit tests:**
   ```
   make unit-test
   ```
   Must show 0 failures. Skips OK.

3. **Independent code review** (different agent from author, per `feedback_never_self_review.md`).

4. **Headless regression** (rendering-adjacent phases — Phase 2 onwards, per `feedback_review_run_screenshot_regression.md`):
   ```
   JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh 2>&1 | tee /tmp/regression-g104-phase<N>.log
   ```
   Reviewer cites both computed MD5 and reference MD5 verbatim per the rule. **Until Phase 9 lands new refs, all tests will FAIL on dimension; reviewer's job is to confirm failures are dimension-only, not content-level.**

5. **Branch hygiene:**
   - Base on LOCAL `main` (per `feedback_local_main_not_origin.md`).
   - One feature per branch.
   - **No `git push` without explicit user authorization** (per `feedback_no_unauthorized_push.md`).
   - **No `--no-verify` on commits** (per CLAUDE.md).
   - Each fix = its own commit (per `feedback_individual_commits.md`); no amending.
   - Terse insightful commit messages, no Co-Authored-By trailers (per CLAUDE.md).

6. **Per-test reference regeneration** (Phase 9 only):
   - **Never bulk** (per `feedback_regression_refs.md`).
   - Per-test user OK before each `generate-references.sh <test>`.

---

## Section G — Open USER QUESTIONS (only NEW)

**None.** The 3 locked decisions resolve the architectural ambiguity. The hi-res bit-order question (Section A.3) was a planning-time discovery that VHDL resolves directly — no user input needed. The screenshot doubling style ("flag vs always") is an internal style choice the Phase 7 agent decides without user input.

If during Phase 2 the agent finds that the `shift_pbyte` byte-vs-bit interleave question has additional VHDL evidence (e.g. another path in `zxula.vhd` that re-interleaves bits), they should pause and surface it before committing. Otherwise, proceed with the byte-interleave model per Section A.3.

---

## Critical Files for Implementation

The 5 most critical files for implementing this plan:

- /home/jorgegv/src/spectrum/jnext/src/video/renderer.h
- /home/jorgegv/src/spectrum/jnext/src/video/renderer.cpp
- /home/jorgegv/src/spectrum/jnext/src/video/ula.cpp
- /home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp
- /home/jorgegv/src/spectrum/jnext/src/gui/emulator_widget.cpp

(Layer2, tilemap, sprites, screenshot, video_recorder are also load-bearing but mechanical compared to the core 5. The 5 above are where the architectural collapse from `dynamic-width(320|640)` → `canonical-640` actually manifests.)