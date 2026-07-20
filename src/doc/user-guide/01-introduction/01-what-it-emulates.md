# What it emulates

| Area | What you get |
|------|--------------|
| Machines | ZX Spectrum 48K, 128K, +2A/+3, and ZX Spectrum Next (Issue 2) |
| CPU | Z80N — the Next's Z80 with its extra instructions |
| Video | ULA (including the Timex modes), Layer 2, 128 hardware sprites, tilemap, Copper, and the layer compositor |
| Audio | Three AY-3-8910 chips (TurboSound), the DAC, and the beeper |
| Storage and peripherals | DivMMC and SD card, Multiface, DMA, UART, CTC, SPI / I²C / real-time clock |
| Input | Keyboard, USB gamepads, Kempston mouse |
| File formats | NEX, SNA, SZX, Z80, TAP, TZX, WAV, RZX |

The Next boots the real NextZXOS operating system, through the real boot chain,
from a real SD-card image — exactly as the hardware does. Every ROM the machine
needs comes off that SD card rather than out of the emulator. [Chapter
3](../03-first-run/index.md) covers how you get one.
