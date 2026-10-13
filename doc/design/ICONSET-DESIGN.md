# Toolbar Icon Set — Design

**Status:** icon set approved by the owner 2026-10-13; runtime rendering on branch `toolbar-mono-icons`.
**Last updated:** 2026-10-13.
**Audience:** anyone adding or changing an icon in the jnext GUI.

The jnext toolbars use one monochrome SVG icon set. This document records what the
set looks like, the constraints the owner set when it was drawn, and the rules a new
icon has to follow so the set stays consistent.

---

## 1. Owner constraints (2026-10-13)

These came from the owner when the set was commissioned. They are requirements, not
style preferences.

1. **Based on the icons they replace.** Each icon keeps the shape of the one it
   replaced (Fusion's standard icons, the camera theme icon, the hand-painted Debug
   chip), unless the owner chose a different metaphor (section 3).
2. **Monochrome.** One colour, no tints, no colour accents. Each icon must be easy to
   tell apart both in light mode (dark glyph on a white or grey background) and in
   dark mode (light grey or white glyph on a dark background).
3. **SVG files, rendered at runtime.** The SVG in the repository is the only source
   of each icon. No PNGs are committed and no icon is redrawn in QPainter code.
4. **License.** A reused icon must be public domain or free software. The current
   set reuses nothing: every icon was drawn for jnext and is dedicated to the public
   domain (CC0-1.0), stated in a comment inside each file.

## 2. Characteristics

| Property | Value |
|----------|-------|
| Grid | `viewBox="0 0 24 24"`, designed for a 24 px toolbar |
| Must read at | 16, 24, 32 and 48 px, in light and in dark mode |
| Colour | `currentColor` only: every `stroke` and `fill` uses it, and nothing else |
| Stroke | 2 units by default; 1.2–1.8 for fine detail (arrowheads, pins, letters) |
| Caps and joins | `stroke-linecap="round"`, `stroke-linejoin="round"` |
| Weight | outlines plus solid fills: a filled part gives the glyph mass at 16 px |
| Margin | keep to about 1–23; only a part that needs it (a magnifier handle) goes to the edge |

**Overlaps are gaps, not layers.** With one colour, a shape drawn over another merges
into a blob. Where two parts overlap, the part behind stops short and leaves a blank
border of about 1.5–2 units round the part in front: the Debug chip's outline breaks
round the magnifier lens, and the NMI ring breaks round the letters.

**Letters are drawn as strokes, not text.** `<text>` depends on the fonts installed on
the host. The NMI letters are polylines, so they render the same everywhere.

**Allowed SVG features:** `path`, `rect`, `circle`, `ellipse`, `line`, `polyline`,
`polygon`, the presentation attributes above, and comments. **Not allowed:** `<text>`,
`<use>`, `<defs>`, gradients, patterns, masks, `clip-path`, filters, `opacity` or
alpha, embedded images, CSS `<style>` blocks. Each of these either depends on parts of
SVG that Qt's renderer (SVG Tiny 1.2) handles incompletely, or breaks the monochrome
rule. A shape that seems to need one of them can be built from plain paths: compute
the arc end points instead of masking.

## 3. The set

Files live in `src/gui/icons/`.

| File | Action | Shape | Replaced |
|------|--------|-------|----------|
| `power.svg` | Power Reset | power symbol: a circle open at the top, with a vertical bar (owner's choice) | `SP_BrowserReload`, two circular arrows |
| `soft-reset.svg` | Soft Reset | three arrows chasing round a circle, a recycle symbol (owner chose this circular variant over the triangular one, which is muddier at 16 px) | `SP_DialogResetButton`, a paintbrush |
| `pause.svg` | Pause | two rounded bars | `SP_MediaPause` |
| `resume.svg` | Resume | rounded triangle | `SP_MediaPlay` |
| `load.svg` | Load | open folder: back outlined, front flap filled | `SP_DialogOpenButton` |
| `screenshot.svg` | Screenshot | camera: body, lens, viewfinder bump, flash dot | theme icon `camera-photo`, which Fusion does not have, so the button showed the word |
| `nmi.svg` | Multiface NMI | the letters "NMI" across a ring, with a blank border round the text so the ring is visibly cut by it (owner's choice) | none: a text-only button |
| `debug.svg` | Toggle Debugger | chip with pins and a solid die, magnifier over its lower-right corner | QPainter drawing: a dark chip and an orange magnifier |

The arc and arrowhead coordinates in `power.svg`, `soft-reset.svg`, `nmi.svg` and
`debug.svg` were computed (end points on the circle, arrowheads tangent to the arc,
the gaps where a halo circle crosses the chip edges), not drawn by hand. Edit them the
same way: by working the geometry out, not by nudging points until it looks right.

## 4. Runtime rendering

Qt does not colour `currentColor` from the widget palette, so the SVG is coloured
before it is rendered:

- The GUI links **QtSvg** (the `Svg` component, for both the Qt 6 and the Qt 5 build)
  and renders each icon with `QSvgRenderer`. It does not use QIcon's SVG
  icon-engine plugin, so no Qt plugin has to be deployed for the icons to appear.
- The SVGs are compiled in as Qt resources.
- At render time `currentColor` is replaced with the palette's `WindowText` colour
  for the Normal state, and with the Disabled `WindowText` colour for the Disabled
  state. Qt's automatic greying is weak on a flat glyph, which is why the disabled
  state gets its own rendering.
- One QIcon holds a pixmap per size (16, 24, 32, 48) and per state, so a HiDPI
  toolbar picks a pixmap drawn for it rather than scaling a smaller one up.
- The icons are re-rendered when the palette changes (`QEvent::PaletteChange`), so a
  switch between light and dark follows at run time.

The branch that implements this keeps this section accurate.

## 5. Adding or changing an icon

1. Start from the icon being replaced or from the established convention for the
   action. Keep the shape, and drop the colour.
2. Draw it on the 24-unit grid with the rules in section 2. Put the CC0 comment (or
   the reused icon's licence) in the file.
3. Preview it before committing: at 16, 24, 32 and 48 px, on a light panel
   (`#efefef` background, `#1e1e1e` glyph) and a dark one (`#2a2a2e` background,
   `#e6e6e6` glyph), next to the rest of the set in a mock toolbar, including the
   hover and disabled states. Inline the SVGs in an HTML page and set the panel's CSS
   `color`: `currentColor` picks it up, which an `<img>` tag would not.
4. Get the owner's approval of the preview. The owner chose the power, soft-reset and
   NMI shapes on such a page.
5. Add the file to the resource list and to the icon tests.
