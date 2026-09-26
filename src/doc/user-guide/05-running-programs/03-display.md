# 5.3 Display

**Scale.** **View > Scale 1x/2x/3x**, or press **F2** to cycle. Scaling is
always a whole number of pixels, so the picture stays sharp.

**Fullscreen.** **F11**, or **View > Fullscreen**. The image is centred with
black bars, keeping the correct shape. F11 again returns. (Esc will not: it is
the Break key.)

**CRT filter.** **View > CRT Filter** overlays soft scanlines, for a more
period-accurate look.

**Layers.** The Next composites several video layers — the classic ULA screen,
Layer 2, the tilemap and up to 128 sprites — and the running program decides
their stacking order and transparency. There is nothing to configure; it is
simply what you see.

![All layers](../img/layers-all.png)

You can, however, pull them apart, which is useful when something looks wrong
and you want to know which layer is responsible:

```
jnext --headless demo.nex --delayed-screenshot l2.png \
    --delayed-screenshot-layers layer2 \
    --delayed-screenshot-frames 200 --delayed-automatic-exit-frames 250
```

![Layer 2 only](../img/layers-layer2.png) ![Sprites only](../img/layers-sprites.png)

*The same frame with only Layer 2, and with only the sprites.* Excluded layers
are treated as switched off, so what remains still composites normally.
Leaving out `ula` also removes the border, since that is the ULA's job.

**Screenshots.** **File > Save Screenshot…** (Alt+S) or the toolbar
**Screenshot** button asks for a name and writes the file. JNEXT remembers the directory. For
scripted, repeatable captures, see
[chapter 7](../07-automation-and-ci/index.md).

**Quick screenshots.** When you want a *sequence* of captures, the dialog is
the slow part. **File > Quick Screenshot** (**Alt+K**) writes immediately, with
no dialog at all: the file is named after the date and time
(`jnext-20260718-103412.png`) and lands in the quick-screenshot directory,
which the status bar names each time so a capture is never lost. Two captures
in the same second get `-02`, `-03` and so on rather than overwriting each
other. The directory (by default `~/.jnext/screenshots`, created on first use)
and the format are set under **Settings > Preferences > Paths**.

**Two formats, chosen by the extension.** There is no format option anywhere;
the name you give decides.

- **`.png`** is the picture — every layer, the border, the palette, exactly as
  the window shows it. A 640x256 frame is written as a 640x512 file so the
  pixels are square.
- **`.scr`** is the classic ZX screen dump that Spectrum graphics tools read
  and write: a copy of the ULA screen memory as it stands, taken from whichever
  screen bank is on display. Nothing is converted, so the bytes are exactly
  what the program put there.

`.scr` has a real limit, and it is worth knowing before you rely on it: **the
format can only describe the classic ULA screen.** Layer 2, the tilemap, the
sprites, the LoRes modes and the Next palettes have no place in it and are
simply not in the file — capture a Layer 2 game as `.scr` and you get whatever
happens to be left in the ULA screen memory, which is often nothing. Use `.png`
for those.

In the Timex hi-colour and hi-res modes the file is 12288 bytes instead of the
usual 6912: those modes use two screen planes and both are written, which is
what Timex-aware tools expect. The file does not record *which* of the two
modes produced it, because the format has nowhere to say so.

The same choice applies on the command line:

```
jnext --headless demo.nex --delayed-screenshot shot.scr \
    --delayed-screenshot-frames 200 --delayed-automatic-exit-frames 250
```

`--delayed-screenshot-layers` is refused with a `.scr`, since a screen dump has
no layers to choose between.

---
