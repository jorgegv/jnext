# Beast.nex rendering investigation — ULA leak through tilemap

**Date**: 2026-04-24
**Branch**: `investigation-beast-nex` (worktree
`/home/jorgegv/src/spectrum/jnext/.claude/worktrees/agent-a07f5bc4`)
**Base**: `main` @ `ca88d01` (UDIS-03 Phase 1 landed — NR 0x68 bits 6:5 +
4-variant blend_mode)
**Status**: diagnostic only — branch not intended to be merged.

## 1. Summary

Running `/home/jorgegv/src/spectrum/CSpect3_1_0_0/Beast/beast.nex` under
jnext produces a frame where the Layer-2-style background art renders,
but scattered 8×8 coloured blocks (bright yellow, red, magenta, green,
white) leak through — particularly in the upper sky / cloud region and
among the foliage. CSpect renders the same NEX cleanly on the same
invocation.

jnext screenshot: `/tmp/beast-phase1-instrumented.png` (target name
`beast-6s.png` in the original task). Visual inspection confirms:

- Full-frame background is coherent (mountains, trees, character).
- 8×8 cells leak through in the sky and over the foliage; colours are
  classic ZX Spectrum bright-paper / bright-ink combinations
  (yellow-on-black, red-on-black, white-on-black, etc.).

The reference comparison asset (`beast-good.png`) was stated by the task
brief to live at `/home/jorgegv/src/spectrum/jnext/beast-good.png`; it
does not exist in the filesystem. This report relies on the textual
description of clean CSpect rendering.

The earlier assumption that this was a `ula_blend_mode` (NR 0x68 bits
6:5) issue is **definitively ruled out**: Phase 1 wired those bits, the
hash did not change, and the captured NR-write trace shows beast never
writes NR 0x68 — default `ula_blend_mode = 00` is in effect and the
priority mode in use does not consult it (see §4).

## 2. Setup — reproduction

Build with both GUI and debugger enabled (per
`feedback_build_gui_enabled.md`):

```bash
LANG=C cmake -S . -B build -DENABLE_QT_UI=ON -DENABLE_DEBUGGER=ON
LANG=C cmake --build build -j4 2>&1 | tail -5
```

Run (instrumentation active when `BEAST_NR_LOG` is exported):

```bash
BEAST_NR_LOG=/tmp/beast-nr-log.txt timeout 20 ./build/jnext \
    --headless --machine next \
    --load /home/jorgegv/src/spectrum/CSpect3_1_0_0/Beast/beast.nex \
    --delayed-screenshot /tmp/beast-phase1-instrumented.png \
    --delayed-screenshot-time 6 \
    --delayed-automatic-exit 8 \
    > /tmp/beast-stdout.txt 2>&1
```

Instrumentation sites (all scratch — not intended for merge):

- `src/port/nextreg.cpp` — `NextReg::write` appends every NR write to
  `$BEAST_NR_LOG`.
- `src/core/emulator.cpp` — ULA port 0xFE and L2 port 0x123B handlers
  append to the same file on writes that actually change state.
- `src/video/layer2.cpp` — first three `render_scanline` calls append
  Layer 2 state snapshot.

## 3. NR state observed at t=6 s

Filtered event sequence (palette/copper/MMU-reshuffle floods removed;
full log at `/tmp/beast-nr-log.txt`, 36 539 lines). Interleaves
NEX-loader writes (the `nexload.asm` emulation) with beast's in-game
writes.

Top-volume registers (most are palette/copper — benign):

```
  26058 NR 60   (copper program data — heavy scheduler)
   2120 NR 30   (tilemap scroll_x LSB)
   1818 NR 16   (Layer 2 scroll_x)
   1817 NR 2F   (tilemap scroll_x MSB)
   1516 NR 41   (palette data 8-bit)
    708 NR 56   (MMU slot 6 page-low)
    606 NR 62   (copper address low)
    606 NR 61   (copper address high)
```

Key one-shot writes (beast + nexload combined):

```
NR 12 = 08, 08, 09         Layer 2 bank (final 9)
NR 13 = 0C                 Layer 2 shadow bank
NR 14 = E3, E3, E3, E7     global transparent RGB (final 0xE7)
NR 15 = 01                 priority = SLU (sprites on top, L2 mid, ULA bot), sprites visible
NR 17 = 00                 L2 scroll_y = 0
NR 18..1B (×4 each)        clip windows set to full-screen for all layers
NR 1C = 0F                 reset all clip indices
NR 4A = E3, 00             fallback colour (final 0x00 = black)
NR 4B = E3                 sprite transparent index
NR 4C = 0F, 0F, 00         tilemap transparent index (final 0x00)
NR 5x                      MMU slot mapping (game paging)
NR 64 = 15                 video line offset
NR 6B = 83                 tilemap control — bit 7=en, bit 1=512-tile, bit 0=tm-on-top
NR 6E = 76                 tilemap map base (bank 5, offset 0x3600 → 0x17600)
NR 6F = 40                 tile-definitions base (bank 5, offset 0 → 0x14000 — note bit 6
                           is "unused" per VHDL; decode discards it)
NR 70                      (never written — stays 00: 256×192 @ 8bpp L2)
NR 68 / 69                 (never written — defaults: ULA enabled, L2 disabled)
PORT 123B                  (never written — L2 port-enable stays 0)
PORT FE                    (never written with non-zero border)
```

Copper heavy activity (26 k writes to NR 0x60 with NR 0x61/0x62 address
writes) indicates beast is driving a per-scanline Copper program — most
likely for tilemap horizontal scroll / palette rotation.

## 4. Hypotheses — test and verdict

### H1: `ula_blend_mode` (NR 0x68 bits 6:5) / priority modes 6/7 — **RULED OUT**

Beast does not write NR 0x68 (trace confirms: 0 writes). Default is
`00` (`ula_mix_rgb = ULA`). Beast writes NR 0x15 = 0x01 — bits 4:2 =
`000` → priority mode **0 (SLU)**, which in the compositor does not
enter the blend-mode branch at all (modes 6/7 only). Phase 1's rewire
is irrelevant on this path.

### H2: NR 0x15 bit 0 `layer2_priority` misdecode — **RULED OUT**

Beast's NR 0x15 = `0x01`. Bit 0 is `sprites_visible`, not
`layer2_priority`. Layer 2 priority bit (VHDL `nr_15_layer2_priority`)
lives at bit 5 of NR 0x15, not bit 0 — and it is 0 in beast.

### H3: clip-window (NR 0x18/19/1A/1B/1C) misdecode — **RULED OUT**

All four clip windows set to full-screen (0,FF,0,FF / 0,FF,0,BF / 0,9F,
0,FF). No layer is being clipped to a partial region. Verified the
indices get reset correctly via NR 0x1C = 0x0F before the 4-byte
rotating writes.

### H4: palette / stale ULA memory — **PROMOTED TO LEADING HYPOTHESIS**

See §5 for the full chain. Summary: ULA is enabled, but reads from
physical page 10 (bank 5) — into which the NEX loader has already
written beast's game data (`kBankOrder` loads bank 5 first, before all
other banks). The ULA therefore reinterprets beast's game data as
bitmap+attributes and produces visible garbage wherever the tilemap
is transparent (pixel value = NR 0x4C = 0x00).

### H5: Layer 2 configuration — **RULED OUT — Layer 2 is not rendering**

The most striking data-point from this investigation: the
instrumentation at the top of `Layer2::render_scanline` (logging on
every call regardless of `enabled_`) **never fires**. The renderer
short-circuits at `src/video/renderer.cpp:54` (`if
(layer2.enabled())`) because `enabled_` stays at its default `false`:
beast never writes NR 0x69 (which is the only handler that calls
`Layer2::set_enabled(true)` besides `port 0x123B`, which beast also
never writes).

So the Layer-2-looking art in the screenshot is **not from Layer 2 —
it is entirely rendered by the tilemap**. Beast is using tilemap with
NR 0x6B = `0x83` (enabled, 40×32 mode, 512-tile mode, tm-on-top) to
draw the whole scene. Tile definitions at bank 5 offset 0, tilemap map
at bank 5 offset 0x3600.

## 5. Root cause (best theory)

1. Beast renders via **tilemap**, not Layer 2. 40×32 cells, each 8×8
   pixels.
2. Tilemap transparent index = NR 0x4C = **0x00**. Any tile pixel whose
   4-bit palette index is `0` is marked transparent
   (`pixel_en_standard_s = 0` in VHDL tilemap.vhd:427; same in jnext
   `tilemap.cpp:379-380`).
3. For each transparent tilemap pixel, the compositor in priority
   mode 0 (SLU) falls back to the ULA/TM merge; at
   `src/video/renderer.cpp:278-281`:
   ```cpp
   ulatm_transp = ula_transp && tm_transp;
   if (!tm_transp && (!ula_over_flags_[x] || ula_transp)) {
       u_px = tm_px;
   } else {
       u_px = ula_transp ? TRANSPARENT : ula_px;   //  ← this branch
   }
   ```
   Because `tm_transp = 1` there, `u_px = ula_px` whenever the ULA
   pixel is itself not transparent.
4. **ULA reads physical page 10 (bank 5)** via `Ula::vram_read`
   (`src/video/ula.cpp:35-51`, VHDL-faithful). The NEX loader, via
   `kBankOrder` (`src/core/nex_loader.h:66-74`), loads bank 5 **first**
   — writing 16 KB of beast's game data into exactly the region the
   ULA scans. The ULA dutifully interprets that data as 6 144 bytes of
   bitmap + 768 bytes of attributes and produces per-cell colour.
5. Wherever the ULA-produced pixel happens to be opaque (it is unless
   its palette-resolved RGB equals the NR 0x14 transparent colour —
   0xE7 in beast after launch), it shows through the transparent
   tilemap cell as an 8×8 attribute-coloured block.

### Why does CSpect render cleanly?

Not yet observed directly (no CSpect screenshot on disk). Plausible
explanations, ordered by likelihood:

1. CSpect's tilemap transparency handling at priority mode 0 routes
   transparent tilemap pixels to the **fallback colour** (NR 0x4A,
   which beast sets to 0x00 = black) rather than to ULA. i.e. the
   `ulatm_transp` / `u_px = ula_px` branch in jnext diverges from
   CSpect — possibly because CSpect honours a subtle VHDL-compositor
   gate we don't.
2. CSpect's Copper program (26 058 NR 0x60 writes in jnext — we run
   the code but may not be interpreting all mnemonics) reconfigures
   ULA disable / clip mid-frame to cover the transparent cells;
   jnext's Copper runs but may skip a MOVE or WAIT that sets NR 0x68
   bit 7 (ULA disable) at a specific scanline.
3. CSpect is tolerant of the ULA reading uninitialised bank 5 because
   its bank-5 memory contents happen to be zeroed (all-black bitmap +
   black attributes → invisible against fallback black), whereas
   jnext's NEX loader loaded beast's bank 5 there.

### Data point disambiguating (1) vs (2)

The leaks appear only inside the display region (rows 32-223), and
follow 8×8 alignment — that is consistent with either (1) or (2). Key
distinction:

- If (1): disabling ULA globally (NR 0x68 bit 7 = 1) at the top of the
  beast run in jnext would make the leaks disappear and the tilemap
  cells would show fallback black instead.
- If (2): merely disabling ULA would also fix the leaks but would
  additionally break wherever Copper WAS trying to toggle ULA on for a
  specific band.

A proposed disambiguating experiment (next step, §7) is to force NR
0x68 = 0x80 (ULA disabled) from the NEX loader and re-snap beast — if
the resulting image matches CSpect exactly, the root cause is (1) and
the fix is a compositor divergence; if it changes-but-still-different,
Copper participation is material.

## 6. Proposed fix scope (kernel)

The likely work:

1. **Re-audit the VHDL ULA/TM merge at zxnext.vhd:7115-7116 vs.
   jnext `renderer.cpp:258-283`.** The formula is:
   ```
   ulatm_rgb <= tm_rgb when (tm_transparent='0')
                          and (tm_pixel_below_2='0'
                               or ula_transparent='1')
                      else ula_rgb;
   ```
   That appears to be what jnext computes. But the COMPOSITOR
   priority-mode 0 (VHDL line 7218) then does:
   ```
   if layer2_prio='1' then l2
   elsif sprite_transparent='0' then sprite
   elsif layer2_transparent='0' then l2
   elsif ulatm_transparent='0' then ulatm_rgb
   else fallback
   ```
   — ulatm is only used when `ulatm_transparent = 0`, and
   ulatm_transparent = ula_transparent AND tm_transparent. In the
   leaky-cell case, tm_transparent = 1 and ula_transparent = 0 →
   ulatm_transparent = 0 → ulatm_rgb selected → and ulatm_rgb = ula_rgb
   in that branch. **So VHDL and jnext should behave identically.**
   This argues AGAINST pure-compositor (1) and toward (3) —
   uninitialised-memory divergence (see below).

2. **Audit Copper program interpretation for ULA-disable WAIT/MOVE
   instructions.** If beast's Copper sets NR 0x68 bit 7 at every
   scanline (to hide ULA while tilemap renders), and jnext drops those
   MOVEs, the leaks would persist. The Copper dashboard already has
   NMI-blocked skips (memory: `project_nmi_fragmented_status.md`) and
   the Compositor/Copper plan owns rows re-homed from the ULA closure
   (memory: `project_ula_plan_closed_20260423.md` / the 5 new plan
   docs referenced there).

3. **Consider whether the NEX loader should zero physical pages 10/11
   (bank 5) before loading the NEX banks.** Real hardware boot leaves
   bank 5 in an indeterminate state (typically zeros from power-on);
   an emulator that loads beast's bank-5 payload into pages 10/11
   de-facto corrupts the ULA view even if the game never reads the
   ULA. This is a jnext divergence from "power-on ULA memory state
   that the game expects" — but it is also a game-side bug (beast
   relies on ULA being hidden, not on ULA memory being zeroed). The
   fix belongs in the renderer, not the loader.

4. **Investigate whether NR 0x6B bit 0 (`tm_on_top`) should suppress
   ULA entirely at the compositor — not just reorder within ulatm.**
   When `tm_on_top = 1`, `pixel_below = 0` (per VHDL tilemap.vhd:308),
   which feeds into `ulatm_rgb <= tm_rgb when tm_transparent='0'
   and tm_pixel_below='0'` — exactly line 7116 above. But this still
   only suppresses ULA where TM is opaque. The semantics "tilemap
   always on top of ULA" in VHDL does **not** mean "suppress ULA where
   TM is transparent" — so this is NOT where the divergence lives.

On that evidence, explanations (2) Copper-MOVE interpretation and (3)
uninitialised-memory handling are the most likely roots.

## 7. Next steps — follow-up plan kernel

1. **Generate a CSpect screenshot** and byte-compare regions in the
   leak area vs. jnext's screenshot. If CSpect renders fallback black
   there, look for a compositor divergence or Copper-MOVE
   interpretation gap. If CSpect shows a *different* ULA pattern there
   (not fallback), the root cause is uninitialised-memory divergence.
2. **Instrument Copper MOVEs targeting NR 0x68.** Add a one-shot log
   in the Copper executor's NR-write path to see whether beast's
   Copper ever writes NR 0x68 (ULA-disable bit) and when.
3. **Disambiguation experiment**: inside the scratch branch, force
   `nextreg_.write(0x68, 0x80)` at NEX-loader end. Re-snap beast. If
   the render now matches CSpect 1:1, the fix is "beast expects ULA
   off; jnext correctly honours NR 0x68 default ULA-on; the real
   divergence is upstream — beast's real-hardware boot path disables
   ULA before this NEX is launched." That would make this a *boot
   path* issue, not a renderer issue.
4. **Feed into a future plan doc** — likely a *Compositor ULA-leak*
   audit — rather than folding into UDIS-03. UDIS-03 Phase 1 is
   closed; this is a distinct bug.

## Instrumentation list (to revert before any merge)

- `src/port/nextreg.cpp:1-35, 112-128` — BEAST-INVESTIGATION block
  (getenv-gated file logger).
- `src/core/emulator.cpp:7-11` — `<cstdio>` / `<cstdlib>` includes.
- `src/core/emulator.cpp:1130-1149` — port 0x123B logger.
- `src/core/emulator.cpp:1280-1294` — port 0xFE logger.
- `src/video/layer2.cpp:1-7` — `<cstdio>` / `<cstdlib>` includes.
- `src/video/layer2.cpp:98-115` — three-call state snapshot.

All keyed off `BEAST_NR_LOG` env var; silent when unset.
