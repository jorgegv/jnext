# LAYER COMPOSITION RESOLUTION

On the ZX Spectrum Next, the **entire display does not switch** to 640 pixels wide in a way that forces all layers to change their internal resolution. Instead, the Next uses a sophisticated **layered compositing system** where each layer can operate at its own "native" horizontal resolution independently.

### 1. Per-Layer Resolution
When you enable the **80x32 Tilemap mode**, only the Tilemap layer (often called Layer 3) is rendered at 640 pixels wide. The other layers remain at their configured resolutions.

* **Tilemap Layer:** In 80x32 mode, it renders **640 pixels** across the horizontal scanline.
* **Layer 2:** Can be set to 256, 320, or **640 pixels** (4bpp mode) independently of the Tilemap.
* **ULA Layer:** Remains at its standard resolution (effectively **320 pixels** wide when including the "over-border" area, or 256 for the main screen).
* **Sprites:** Work on a coordinate system based on the **320-pixel** width. A single sprite pixel horizontally covers the same space as **two** 640-mode tilemap pixels.

---

### 2. How Compositing Works
The hardware composites these layers "on the fly" as the video signal is generated. Because the VGA/HDMI timing (the "master clock") is constant, the FPGA simply fetches data from the 640-res layers twice as fast as it does from the 320-res layers.

| Layer       | Max Horizontal Res | Effect in 80x32 Tilemap Mode                                |
|:------------|:-------------------|:------------------------------------------------------------|
| **Tilemap** | 640                | High-detail text or backgrounds.                            |
| **Layer 2** | 640                | Can match the Tilemap or stay at 320 (chunky).              |
| **Sprites** | 320                | Sprite pixels appear "double-wide" relative to 80-col text. |
| **ULA**     | 256/320            | Standard Spectrum graphics under/over the high-res map.     |

---

### 3. Coordinate Systems & Clipping
One important distinction is how the Next handles coordinates for these different resolutions:
* **Tilemap Clip Window:** When in 80x32 mode, the X-coordinates for the Tilemap clipping window (NextReg `$1B`) are **internally doubled** to accommodate the 640-pixel range.
* **Sprite Coordinates:** Sprites still use a 9-bit X coordinate (`0-511`) that corresponds to the 320-wide grid. If you place a sprite at a specific X-coordinate, it will maintain its position relative to the 320-pixel "logical" screen, even if the background tilemap is showing 640 tiny pixels.

### 4. Visual Mixing
If you mix 80-column text with standard sprites, you will see a clear difference in "grain":
* **Text/Tiles:** Will look sharp and thin (640-res).
* **Sprites/ULA:** Will look "standard" resolution, meaning their pixels are exactly twice as wide as the 80-column characters.



For a deep dive into how these registers interact, [this technical overview of the Tilemap registers](https://www.youtube.com/watch?v=YjswEJkR9HA) explains how Layer 2 and other graphics modes are paged and rendered independently on the hardware.

This video is relevant because it demonstrates the memory mapping and resolution handling of the Next's higher-resolution layers, helping to visualize how the hardware handles different pixel widths simultaneously.

