# JNEXT — Feature List

## Machine emulation
- ZX Spectrum 48K, 128K, +2A/+3, Pentagon, and ZX Spectrum Next machine types
- FUSE-based Z80 core with all standard opcodes (100% pass rate on FUSE test suite)
- All 26 Z80N extended opcodes (NEXTREG, MUL, LDIRX, barrel shifts, etc.)
- Accurate memory contention for 48K, 128K, +3, and Pentagon timing models
- IM1/IM2 interrupt controller with all 14 Next interrupt levels
- Z80 CTC, UART, DMA, SPI, I2C/RTC peripherals (VHDL-verified)
- DivMMC with 8KB SRAM, automap, and SD card image mounting
- Floating bus emulation (48K/128K modes)
- Pentagon timing mode (448 cycles/line, zero contention)

## Video
- ULA: standard 48K pixel+attribute, Timex hi-colour (8×1 attributes), Timex hi-res (512×192)
- Layer 2: 256×192, 320×256, and 640×256 resolutions at 8-bit colour
- Hardware sprites: 128 sprites, 16×16 pixels, 8-bit/4-bit colour, scaling ×1/×2/×4/×8
- Composite sprites (anchor + relative for larger objects)
- Tilemap: 40×32 and 80×32 modes, 4bpp/1bpp patterns, hardware scroll
- Copper co-processor: WAIT/MOVE instructions, per-scanline register writes
- 6-mode layer compositor (SLU/LSU/SUL/LUS/USL/ULS priority)
- Per-scanline border colour updates (authentic tape loading stripes)

## Palettes
- 8 palettes (ULA/Layer2/Sprite/Tilemap × first/second), 256 entries each
- 9-bit RGB colour (512 possible colours), full NextREG palette control

## Audio
- AY-3-8910 / YM2149 × 3 (TurboSound), with tone, noise, envelope, and stereo panning
- 4-channel 8-bit DAC (Soundrive/Specdrum/Covox)
- Beeper (EAR/MIC)
- SDL audio output at 44100 Hz stereo

## File format support
- NEX (v1.0/1.1/1.2): direct page loading, Layer 2 screen/palette from header
- SNA: 48K and 128K snapshots with full register and paging restore
- SZX: chunked format with zlib-compressed RAM pages
- TAP: fast-load via ROM trap + real-time EAR bit simulation
- TZX: full block support via ZOT library, fast-load + real-time playback
- WAV: RIFF/PCM EAR bit playback (8-bit/16-bit, mono/stereo)
- RZX: playback and recording (IN replay, embedded SNA snapshot, zlib compressed)

## GUI (Qt 6)
- Native Qt 6 main window with menu bar, toolbar, and status bar
- Hi-DPI pixel-perfect rendering at integer scale (2×/3×/4×)
- True fullscreen with aspect-ratio letterbox
- CRT scanline filter overlay
- FPS counter, CPU speed, and machine mode in status bar
- Emulator speed control (0.5×/1×/2×/4×/custom %, or `--speed`)
- PNG screenshot (Ctrl+S, toolbar, `--delayed-screenshot`)
- Video recording to MP4 via FFmpeg pipe (`--record`)

## Debugger (Qt 6)
- Separate debugger window with full panel layout
- Panels: CPU registers, disassembly, memory hex editor, MMU, stack, call stack
- Panels: video layers (ULA/Layer2/Sprites/Tilemap per-scanline view), sprites, copper, NextREG, audio, watches, breakpoints
- PC/data/read/write breakpoints with watchpoints
- Symbol table from Z88DK MAP files; inline symbol names in disassembly
- Trace log (circular buffer, export to file)
- Stepping: Step Into (F6), Step Over (F7), Step Out (F8), Run to EOF, Run to EOSL
- Backwards execution (rewind): frame snapshots ring buffer, Step Back (Shift+F7), Frame Back (Shift+F6), rewind slider
- Magic breakpoint: `ED FF` (ZEsarUX) / `DD 01` (CSpect) triggers debugger pause
- Magic debug port: configurable 16-bit port logs writes as hex/dec/ascii

## CLI
- `--machine`, `--roms-directory`, `--load`, `--headless`
- `--inject` raw binary with `--inject-org`, `--inject-pc`, `--inject-delay`
- `--boot-rom`, `--divmmc-rom`, `--sd-card`
- `--rewind-buffer-size`, `--speed`, `--record`, `--rzx-play`, `--rzx-record`
- `--magic-breakpoint`, `--magic-port`, `--magic-port-mode`
- `--log-level` per subsystem (cpu, video, audio, etc.)
- `--version`
