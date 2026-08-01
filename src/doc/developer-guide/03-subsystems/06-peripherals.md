# 3.6 Peripherals

Everything in `src/peripheral/` is a device that hangs off the machine rather
than being part of it. Each is a plain C++ class with no SDL, no Qt and no
knowledge of `Emulator`; the wiring lives in `Emulator::init()` and
`tick_devices_after_instruction()`. All implement `save_state` / `load_state`,
because rewind snapshots the whole machine (see
[Save state, rewind and determinism](../02-architecture/05-save-state-and-rewind.md)).

## Copper

`copper.{h,cpp}`, from `device/copper.vhd`. A 1K × 16-bit instruction RAM running
`WAIT hpos,vpos` and `MOVE nextreg,value` against the raster counters, writing
NextREG directly and bypassing the CPU. Programming is via NextREG `0x60`-`0x64`
only; there is no Copper port.

The non-obvious part is **which counters**. `Copper::execute(hc, vc, …)` takes
the VHDL `hc_ula` / `cvc` pair: `hc` is the 7 MHz ULA pixel counter, zeroed 11
pixels before the active display — **not** the 28 MHz master-cycle offset into
the line, and **not** anchored at the raw line start. jnext passed the raw 28 MHz
offset for a long time, putting every WAIT four times too early and one row off
(GH #181). `WAIT` also compares `hc >= (hpos<<3)+12` in wrapping 9-bit
arithmetic, so `hpos=63` yields 4, not 516.

## DMA

`dma.{h,cpp}`, from `device/dma.vhd`. One transfer engine behind two programming
ports, `0x6B` (ZXN) and `0x0B` (Z80-DMA compatible); the difference is the
counter's initial value, not the register protocol.

Memory and I/O access go through four callbacks bound to `Mmu` and
`PortDispatch`, so a DMA-driven `OUT` re-enters the normal dispatcher — which is
why `PortDispatch::write()` tolerates a nested write. `execute_burst(16)` runs
from the instruction loop, and `dma_holds_bus()` both stalls the CPU and silences
the DMA ports. In burst mode the CPU is released during the prescaler wait; in
continuous and byte mode it is not.

## DivMMC

`divmmc.{h,cpp}`, from `device/divmmc.vhd`. An 8 KB ROM plus 16 × 8 KB RAM pages
overlaid on slots 0 and 1, controlled by port `0xE3` (`conmem` bit 7, `mapram`
bit 6, bank in bits 3:0) and by automatic mapping on instruction fetch.

**It has two independent gates, and conflating them breaks boot.**
`port_io_enable_` is NR `0x83` bit 0 and defaults to *set*; `nr_0a_4_enable_` is
NR `0x0A` bit 4 and defaults to *clear*. The VHDL output gate is
`(conmem OR automap) AND port_io_en`, so `conmem` works with NR `0x0A` bit 4
still clear — which is what esxdos's IM1 handler relies on during early boot,
long before firmware sets that bit. Only the **automap** path needs both
(`zxnext.vhd:4112`), so `is_active()` checks `port_io_enable_` alone.

The trigger addresses are **not hard-coded**: NR `0xB8`-`0xBB` select which RST
vectors and tape traps map, whether each is instant or delayed, and whether the
`0x3D00` wildcard and the `0x1FF8`-`0x1FFF` unmap window are live.

## SPI and the SD card

`spi.{h,cpp}` (`serial/spi_master.vhd`): port `0xE7` selects, port `0xEB`
exchanges. It is **zero-latency** — a write completes the whole byte exchange
synchronously — so `spi_wait_n()` is constantly asserted. Faithful at byte
granularity, not at cycle granularity, which matters only to a DMA-over-SPI
consumer.

`sd_card.{h,cpp}` implements `SpiDevice` over a raw `.img` file, with enough of
the SPI-mode command set for NextZXOS, esxdos and the firmware boot — including
CMD18 multi-block streaming, which is how tbblue.fw reads `/TBBLUE.FW`. **The
image is opened read-write and guest writes persist**; `--sdcard-readonly` opens
it read-only and the card then answers CMD24 with the write-error token, as a
write-protected card would. This runtime path is entirely separate from the
host-side FAT32 reader in `src/core/sd_rom_extractor.{h,cpp}` that pulls the ROM
images out of the same file at startup.

## CTC

`ctc.{h,cpp}`, from `device/ctc.vhd` and `ctc_chan.vhd`. Four channels at ports
`0x183B`-`0x1B3B`, each a transcription of the VHDL five-state machine; channel
*N*'s ZC/TO feeds channel *N*+1.

The aliased range `0x1C3B`-`0x1F3B` (A10 = 1) gets its **own** handler reading
`0x00` and dropping writes — not defensive padding: with the CTC I/O enable set
the VHDL asserts a read response and OR-folds zeros, so hardware drives `0x00`
there rather than letting the bus float.

## UARTs and the ESP-01

`uart.{h,cpp}`, from `serial/uart.vhd`. Two channels sharing ports `0x133B`
(Tx/status), `0x143B` (Rx/prescaler LSB), `0x153B` (select) and `0x163B`
(framing), with a 512-byte RX FIFO and 64-byte TX FIFO each. RX entries are
9 bits wide because the VHDL carries a per-byte `overflow OR framing` flag.
**UART 0 is the ESP, UART 1 the Raspberry Pi** (`zxnext.vhd:1611`) — the obvious
guess is wrong. An unattached channel loops TX back into its own RX.

The ESP-01 is a real host-side network bridge, not a canned responder.
`src/esp01/` is a self-contained module — an AT-command engine over a socket
transport, with no jnext types in it — and `esp_uart_adapter.{h,cpp}` is the
entire coupling: five forwarding methods implementing `UartDevice`. It opens
**actual TCP and UDP sockets to the real internet**, and is off unless `--esp` is
given. `esp_host_policy.{h,cpp}` adds the host-side controls: an address policy
refusing loopback, link-local, cloud-metadata, unspecified and multicast while
allowing RFC1918 so the guest can reach the user's own LAN, plus an optional
`--esp-allow` hostname allowlist. Every attempt, success and refusal lands in a
bounded thread-safe log the GUI can show.

No server mode, no `AT+CIPMODE` passthrough, no SSL, no multiplexed connections —
each omission because no Next software uses it. The adapter also has a **replay
gate**: during rewind fast-forward and RZX playback it goes inert rather than
detaching, because detaching would re-enable the channel's loopback and inject
bytes the original run never saw.

## I2C and the RTC

`i2c.{h,cpp}`. A bit-banged bus decoder on ports `0x103B` (SCL) and `0x113B`
(SDA), with reads ANDing the internal line against the Raspberry Pi bridge
inputs. The only attached device is `I2cRtc` at address `0x68` — a DS1307 with
the full register map, BCD encoding, NVRAM, the CH oscillator-halt bit and
12-hour mode. It reads the host clock by default; `--rtc` pins it to a fixed
instant, which is what makes boot screenshots reproducible.

## Multiface

`multiface.{h,cpp}`, from `device/multiface.vhd`. Four flip-flops (`nmi_active`,
`invisible`, `mf_enable`, `port_io_dly`) plus a mode-dependent port decode
selected by NR `0x0A` bits 7:6 — MF+3, MF128 in two variants, MF1. The
enable/disable strobes land on different low bytes per mode, which is why
dispatch goes through an I/O observer rather than a handler.

The memory overlay is in the MMU, not here: `Mmu` holds a non-owning
`Multiface*` and checks `is_mem_active()` at priority 1, below the boot ROM and
above DivMMC. On a Next the ROM and RAM halves are **backed by external SRAM
pages `0x0A` and `0x0B`**, because tbblue.fw loads `enNextMf.rom` into page
`0x0A` during boot; standalone machines use the private buffers.

## NMI arbitration

`nmi_source.{h,cpp}` is not a device but the central arbiter from
`zxnext.vhd:2089-2170`: three producers (Multiface, DivMMC, expansion bus) with
their NR `0x06` / NR `0x81` gates, a priority chain MF > DivMMC > ExpBus, and a
four-state FSM advancing on the `0x0066` M1 fetch. It is fully wired — its own
header still calls itself a "Phase-1 scaffold", which is stale. Stackless NMI
(NR `0xC0` bit 3) is out of scope.
