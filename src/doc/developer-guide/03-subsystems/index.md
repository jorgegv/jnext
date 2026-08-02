# 3. Subsystems

A ZX Spectrum Next is not one design but a couple of dozen cooperating ones,
synthesised side by side in a single FPGA: a Z80 core, a memory arbiter, a ULA
alongside three more video generators, three sound chips, a raster co-processor,
an SD-card interface and a handful of serial devices. JNEXT keeps those pieces
apart in the same places the hardware does. Each page in this chapter takes one
of them and answers the same three questions in the same order: what it is on
the real machine and what job it does there, how JNEXT models it, and what the
code assumes that a reader would not guess.

That the boundaries line up with the FPGA's is not a stylistic preference. The
VHDL is JNEXT's specification, and a specification is only usable if you can
find the part of it that answers your question. Because a C++ class covers
roughly the same ground as the VHDL entity it was derived from, "where does the
hardware decide this?" has a short answer — and almost every class names the
VHDL file and line numbers it came from in its header comment. When behaviour is
ambiguous the VHDL settles it, and the citation is how you find the passage
again.

## The map

The table below maps VHDL module to C++ home: what the hardware calls it, which
class and directory implement it, and what that class is responsible for. It was
built by reading `src/` and the citations the sources themselves carry, so it
describes where code *is*, not where a plan once said it would go.

Two things are worth knowing before you use it.

**A C++ class is rarely a 1:1 port of one VHDL entity.** A great deal of the
Next's behaviour lives in `zxnext.vhd` — the 287 KB top level that wires
everything together — rather than in the subsystem entities it instantiates.
That is why `zxnext.vhd` appears in most rows below, and why a subsystem's real
specification is usually "entity X, plus the parts of `zxnext.vhd` that drive
it".

**Which directory a class lands in follows the emulator, not the FPGA's file
tree.** The decomposition matches the hardware; the filing sometimes does not,
because JNEXT files a class next to whoever consumes it. The Copper is a video
co-processor but lives in `src/peripheral/`; the IM2 interrupt fabric is a
`device/` entity in VHDL but lives in `src/cpu/`, because the CPU is what
consumes its vectors.

| VHDL module(s) | C++ home | Responsibility |
|---|---|---|
| `cpu/t80n*.vhd` | `src/cpu` — `Z80Cpu` | Z80 + Z80N instruction execution, wrapping the vendored FUSE core |
| `device/im2_control.vhd`, `im2_device.vhd`, `im2_peripheral.vhd`, `peripherals.vhd` | `src/cpu` — `Im2Controller`, `Im2Client` | 14-slot IM2 daisy chain, pulse-mode /INT, RETI/RETN decode |
| `zxnext.vhd` (SRAM arbiter, MMU, paging ports) | `src/memory` — `Mmu` | 8×8 KB slot map, every memory overlay, ROM serving |
| `zxnext.vhd` + `video/zxula.vhd` (contention gate) | `src/memory` — `ContentionModel` | Per-bus-cycle CPU stretch/WAIT decision |
| `video/zxula.vhd`, `zxula_timing.vhd` | `src/memory` — `AttributeMux` | Mid-line attribute-write replay (Nirvana-class effects) |
| (backing stores) | `src/memory` — `Ram`, `Rom` | Flat 2 MB SRAM image; 4×16 KB ROM banks + NR 0x8C config |
| `video/zxula_timing.vhd` | `src/video` — `VideoTiming` | hc/vc raster counters, display window, per-machine geometry |
| `video/zxula.vhd` | `src/video` — `Ula` | ULA pixel/attribute fetch, Timex modes, border, floating bus |
| `video/lores.vhd` | `src/video` — `Lores` | LoRes 128×96 and Radastan; substitutes the ULA pixel slot |
| `video/layer2.vhd` | `src/video` — `Layer2` | Layer 2 bitmap, 256×192 / 320×256 / 640×256 |
| `video/tilemap.vhd` | `src/video` — `Tilemap` | 40×32 and 80×32 tile modes |
| `video/sprites.vhd` | `src/video` — `SpriteEngine` | 128 sprites, anchors, scaling, rotation, collision |
| `zxnext.vhd` (palette RAM) | `src/video` — `PaletteManager` | Four palette pairs, RGB333 → ARGB conversion |
| `zxnext.vhd` (layer mux) | `src/video` — `Renderer` | NR 0x15 priority, transparency, per-scanline replay, framebuffer |
| `device/copper.vhd` | `src/peripheral` — `Copper` | WAIT/MOVE program synchronised to the ULA raster counters |
| `audio/ym2149.vhd`, `turbosound.vhd` | `src/audio` — `AyChip`, `TurboSound` | Three PSGs with stereo/mono panning |
| `audio/soundrive.vhd` | `src/audio` — `Dac` | Soundrive / Specdrum / Covox 4-channel 8-bit DAC |
| `audio/audio_mixer.vhd` (EAR/MIC terms) | `src/audio` — `Beeper` | Port 0xFE bits 4/3 plus tape EAR input |
| `audio/audio_mixer.vhd` (I2S terms) | `src/audio` — `I2s` | Latched Pi sample pair + NR 0xA2 gate — a mixer input, not a protocol |
| `audio/audio_mixer.vhd` | `src/audio` — `Mixer` | 13-bit stereo sum, 44.1 kHz ring buffer |
| `zxnext.vhd` (port decoders) | `src/port` — `PortDispatch` | Mask/match 16-bit I/O routing |
| `zxnext.vhd` (NextREG file) | `src/port` — `NextReg` | Port 0x243B/0x253B register file and write dispatch |
| `device/ctc.vhd`, `ctc_chan.vhd` | `src/peripheral` — `Ctc`, `CtcChannel` | Four counter/timer channels with ZC/TO daisy chain |
| `device/dma.vhd` | `src/peripheral` — `Dma` | Z80-DMA compatible register protocol + ZXN burst mode |
| `device/divmmc.vhd` | `src/peripheral` — `DivMmc` | DivMMC ROM/RAM overlay and automap |
| `device/multiface.vhd` | `src/peripheral` — `Multiface` | MF ROM/RAM window and its NMI state machine |
| `zxnext.vhd:2089-2170` | `src/peripheral` — `NmiSource` | MF / DivMMC / expansion-bus NMI priority and FSM |
| `serial/spi_master.vhd`, `fifop.vhd` | `src/peripheral` — `SpiMaster`, `SpiDevice` | Byte-level SPI master and its device interface |
| (SD SPI-mode spec — no VHDL counterpart) | `src/peripheral` — `SdCardDevice` | SD command set over a raw `.img` file |
| `serial/uart.vhd`, `uart_rx.vhd`, `uart_tx.vhd`, `misc/debounce.vhd` | `src/peripheral` — `Uart`, `UartChannel` | Two UART channels, FIFOs, prescaler, RX noise rejection |
| (none — host-side peripheral) | `src/esp01`, `src/peripheral/esp_uart_adapter.*` | ESP-01 AT-command WiFi modem attached to UART 0 |
| `zxnext.vhd` (I2C bit-bang ports) | `src/peripheral` — `I2cController`, `I2cRtc` | SCL/SDA protocol decode and DS1307 RTC |
| `input/membrane/membrane.vhd` | `src/input` — `Keyboard` | 8×5 key matrix driven from host key events |
| `input/membrane/membrane_stick.vhd` | `src/input` — `MembraneStick` | Joystick folded into the key matrix |
| `zxnext.vhd` (joystick decode), `input/md6_joystick_connector_x2.vhd` | `src/input` — `Joystick`, `Md6ConnectorX2`, `JoystickDispatcher`, `IoMode` | Sinclair / Kempston / MD modes, NR 0x05 and NR 0x0B |
| `zxnext.vhd:3541-3562` | `src/input` — `KempstonMouse`, `MouseDispatcher` | Kempston mouse ports and NR 0x0A control bits |
| `input/membrane/emu_fnkeys.vhd` | `src/input` — `EmuFnKeys` | F-key hotkey state machine |
| `zxnext.vhd` (clock enables) | `src/core` — `Clock`, `Scheduler` | 28 MHz cycle counter and the event queue |
| `zxnext.vhd` (top level) | `src/core` — `Emulator` | Owns every subsystem and runs the frame |

## What is deliberately not emulated

Some VHDL has no software counterpart at all, either because it describes the
FPGA itself or because it drives a physical wire that SDL and Qt replace. Each
omission below is a decision with a reason, not a gap waiting to be filled:

| VHDL | Why not |
|---|---|
| `ram/dpram.vhd`, `dpram2.vhd`, `sdpram.vhd`, `tdpram.vhd` | Block-RAM primitives. Where a specific instance is behaviourally load-bearing — `bank5_ram` and `bank7_ram` — `Mmu` models it as a dedicated buffer; the primitive itself is just an array. |
| `pll/*`, `misc/flashboot*.vhd` | Clock synthesis and FPGA configuration. There is no reconfigurable fabric to configure. |
| `video/hdmi/*`, `video/vga/scan_convert.vhd`, `audio/i2s/*`, `audio/pwm.vhd` | Output physical layers. JNEXT hands a framebuffer to Qt and samples to SDL instead. |
| `input/keyboard/ps2_*.vhd`, `input/ps2_mouse.v` | PS/2 wire protocol. Host key and mouse events arrive already decoded. |
| `misc/debounce.vhd`, `relaxation.vhd`, `synchronize.vhd` | Metastability and mechanical debounce. The one exception is the UART RX noise rejector, which changes what bytes arrive and is therefore modelled inside `UartChannel`. |
| `serial/uart_old.vhd` | Superseded in the core itself. |
| `zxnext_top_issue{2,4,5}.vhd` | Board pinouts and housekeeping around the same `zxnext.vhd` machine. |
| `rom/bootrom*.vhd` | ROM content, not logic — see [3.2 Memory](02-memory.md). |

## In this chapter

- [3.1 CPU](01-cpu.md)
- [3.2 Memory](02-memory.md)
- [3.3 Video](03-video.md)
- [3.4 Audio](04-audio.md)
- [3.5 Ports and NextREG](05-ports-and-nextreg.md)
- [3.6 Peripherals](06-peripherals.md)
- [3.7 Input](07-input.md)
- [3.8 Media and loaders](08-media-and-loaders.md)
- [3.9 Debug and the debugger](09-debug-and-the-debugger.md)
