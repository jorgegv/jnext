# 3.6 Peripherals

Everything in `src/peripheral/` is a device that hangs off the machine rather
than being part of it — a co-processor, a storage interface, a timer, a serial
port. Some are Next-era additions, some are 1980s add-on hardware the Next
reimplements in its FPGA so that old software still finds them where it expects.

They share a shape. Each is a plain C++ class with no SDL, no Qt and no
knowledge of `Emulator`; the wiring lives in `Emulator::init()`, which registers
their ports, and in `tick_devices_after_instruction()`, which advances the ones
that need to move on their own. Their interrupt outputs are collected by the
IM2 controller in `src/cpu/im2.*`, and the three that can raise a
non-maskable interrupt go through the arbiter described at the end of this page.
All of them implement `save_state` and `load_state`, because rewind snapshots
the whole machine (see
[Save state, rewind and determinism](../02-architecture/05-save-state-and-rewind.md)).

## Copper

The Copper is a tiny co-processor that runs in step with the video raster. It
holds a short program of its own and executes it as the beam sweeps the screen,
so a Next can change hardware registers at an exact point on an exact scanline
without the CPU having to sit in a timing loop waiting for it. That is what
makes gradient skies, mid-screen palette swaps and split-scroll playfields cheap
on this machine. Without it those effects either cost the CPU its whole frame or
tear.

`copper.{h,cpp}` models `device/copper.vhd`: a 1K × 16-bit instruction RAM
running just two instructions, `WAIT hpos,vpos` and `MOVE nextreg,value`,
against the raster counters. Its NextREG writes bypass the CPU path entirely.
Programming it is via NextREG `0x60`-`0x64`; there is no Copper port.

The non-obvious part is **which counters** it compares against.
`Copper::execute(hc, vc, …)` takes the VHDL `hc_ula` / `cvc` pair, where `hc` is
the 7 MHz ULA pixel counter, zeroed 11 pixels before the active display — it is
**not** the 28 MHz master-cycle offset into the line, and **not** anchored at the
raw line start. jnext passed the raw 28 MHz offset, which put
every `WAIT` four times too early and one row off (GH #181). `WAIT` also
compares `hc >= (hpos<<3)+12` in wrapping 9-bit arithmetic, so `hpos=63` yields
4, not 516.

## DMA

A DMA controller copies blocks of bytes — memory to memory, or memory to and
from an I/O port — without the CPU touching each one. On a Next this is how
software moves a Layer 2 screen, decompresses into video RAM or streams sampled
audio out to a DAC at a fixed rate; the CPU is stopped for the duration and the
controller drives the bus itself.

`dma.{h,cpp}` models `device/dma.vhd`: one transfer engine behind two
programming ports, `0x6B` (ZXN) and `0x0B` (Z80-DMA compatible). The difference
between them is the counter's initial value, not the register protocol.

Memory and I/O access go through four callbacks bound to `Mmu` and
`PortDispatch`, so a DMA-driven `OUT` re-enters the normal dispatcher — which is
why `PortDispatch::write()` has to tolerate a nested write. `execute_burst(16)`
runs from the instruction loop, and `dma_holds_bus()` both stalls the CPU and
silences the DMA ports. In burst mode the CPU is released during the prescaler
wait; in continuous and byte mode it is not.

## DivMMC

DivMMC is the Next's SD-card interface, inherited from a family of third-party
Spectrum add-ons. It is more than a card reader: it carries its own 8 KB ROM and
128 KB of RAM which page in over the bottom of the address space, so that
firmware such as esxdos can take over ROM entry points and offer a filing system
to software that predates the existence of storage on this machine.

The mechanism that makes that possible is **automap**. Rather than requiring the
guest to page the DivMMC in explicitly, the hardware watches instruction fetches
and maps itself in when the CPU fetches from one of a set of trigger addresses —
the RST vectors and the ROM's tape routines — then unmaps again when execution
leaves through a designated window. Software that calls `RST 8` finds the DivMMC
firmware answering, and never knew it was there.

`divmmc.{h,cpp}` models `device/divmmc.vhd`: an 8 KB ROM plus 16 × 8 KB RAM
pages overlaid on slots 0 and 1, controlled both by port `0xE3` (`conmem` bit 7,
`mapram` bit 6, bank in bits 3:0) and by the automap path.

**It has two independent gates, and conflating them breaks boot.**
`port_io_enable_` is NR `0x83` bit 0 and defaults to *set*; `nr_0a_4_enable_` is
NR `0x0A` bit 4 and defaults to *clear*. The VHDL output gate is
`(conmem OR automap) AND port_io_en`, so `conmem` works with NR `0x0A` bit 4
still clear — which is precisely what esxdos's IM1 handler relies on during
early boot, long before firmware sets that bit. Only the **automap** path needs
both (`zxnext.vhd:4112`), so `is_active()` checks `port_io_enable_` alone.

The trigger addresses are **not hard-coded**: NR `0xB8`-`0xBB` select which RST
vectors and tape traps map, whether each is instant or delayed, and whether the
`0x3D00` wildcard and the `0x1FF8`-`0x1FFF` unmap window are live.

## SPI and the SD card

SPI is the bus the SD card sits on, and `spi.{h,cpp}` (`serial/spi_master.vhd`)
is the master: port `0xE7` selects a device, port `0xEB` exchanges a byte. It is
**zero-latency** — a write completes the whole byte exchange synchronously — so
`spi_wait_n()` is constantly asserted. That is faithful at byte granularity but
not at cycle granularity, which matters only to a DMA-over-SPI consumer.

`sd_card.{h,cpp}` implements `SpiDevice` over a raw `.img` file, with enough of
the SPI-mode command set for NextZXOS, esxdos and the firmware boot — including
CMD18 multi-block streaming, which is how tbblue.fw reads `/TBBLUE.FW`. **The
image is opened read-write and guest writes persist.** `--sdcard-readonly` opens
it read-only, and the card then answers CMD24 with the write-error token exactly
as a write-protected card would. This runtime path is entirely separate from the
host-side FAT32 reader in `src/core/sd_rom_extractor.{h,cpp}`, which pulls the
ROM images out of the same file at startup.

## CTC

The CTC is the Zilog counter/timer companion to the Z80, and the Next carries
one: four independent channels that divide down a clock or count external
events, and raise an interrupt when they expire. Software uses it for anything
that needs a periodic tick faster or more precise than the 50 Hz frame
interrupt — sample playback rates, music drivers, timed I/O.

`ctc.{h,cpp}` models `device/ctc.vhd` and `ctc_chan.vhd`. The four channels sit
at ports `0x183B`-`0x1B3B`, each a transcription of the VHDL five-state machine,
and channel *N*'s ZC/TO output feeds channel *N*+1 so they can be cascaded.

The aliased range `0x1C3B`-`0x1F3B` (A10 = 1) gets its **own** handler, which
reads `0x00` and drops writes. That is not defensive padding: with the CTC I/O
enable set, the VHDL asserts a read response and OR-folds zeros, so real
hardware drives `0x00` there rather than letting the bus float.

## The UARTs

`uart.{h,cpp}` models `serial/uart.vhd`. Two channels share ports `0x133B`
(Tx/status), `0x143B` (Rx/prescaler LSB), `0x153B` (select) and `0x163B`
(framing), each with a 512-byte RX FIFO and a 64-byte TX FIFO. RX entries are
9 bits wide because the VHDL carries a per-byte `overflow OR framing` flag.

**UART 0 is the ESP, UART 1 the Raspberry Pi** (`zxnext.vhd:1611`) — the obvious
guess is the wrong way round. A channel with nothing attached loops its TX back
into its own RX, which matters more than it sounds; see the replay gate below.

## The ESP-01

A real ZX Spectrum Next has an ESP-01 WiFi module soldered to it, talking to the
machine over UART 0 with the AT command set that module's firmware speaks:
`AT+CIPSTART` to open a connection, `AT+CIPSEND` to write to it, `+IPD` frames
coming back the other way. It is how a Next gets on the network, and NextZXOS
ships drivers and dot commands built on it, as do third-party programs such as
the NXtel BBS client and nextsync.

jnext emulates it as a **real host-side network bridge, not a canned
responder**. `AT+CIPSTART` opens an actual TCP or UDP socket from the host, so
guest software reaches the real internet — which is the only way to be sure the
emulation is right, since the software on the other end is not ours. It is off
by default and enabled with `--esp` (the GUI can persist that choice, which is
why `--no-esp` also exists).

The command set is deliberately narrow and every command in it is evidenced in
software that actually runs on a Next. There is no `AT+CIPMODE` passthrough and
no SSL, because nothing uses either.

**Server mode and multiplexing exist** (`AT+CIPMUX=1`, `AT+CIPSERVER=1,<port>`,
`+IPD,<id>,<len>:`, `AT+CIPCLOSE=<id>`), added once a consumer appeared — a debug
stub that has to listen, because the debugger on the PC only ever dials out. The
argument form of `AT+CIPCLOSE` frees the named connection's slot; the
no-argument form still means "close the outbound connection", in every mode,
because nextsync loops that exact spelling. The constraint that
shaped it is worth knowing before touching that code: **the power-on default
stays `AT+CIPMUX=0`**, because nextsync never sends the command and its `+IPD`
reader does not reject the multiplexed form — it silently mis-parses it. So the
multiplexed framing reaches only a connection whose own session asked for it,
and the listener binds loopback unless the user widens it with
`--esp-listen-address`.

**`AT+CIPSTO` closes an idle inbound connection** (0-7200 s, default 180), which
is the one behaviour here derived from a *measurement* rather than from a
document or a source: a real Ai-Thinker ESP-01 answers `+CIPSTO:180` and drops a
silent server-accepted client at ~182 s. It is also why `AtEngine` has an
injectable clock — a three-minute default is not provable by waiting, so
`set_clock()` exists and both of the engine's wall-clock deadlines read through
it. Only bytes arriving **from the peer** restart the window; whether
server-initiated traffic does is what the matching firmware document does not
say, and is deliberately not modelled.

### Why it is a separate component, and what that costs

The ESP-01 emulation lives in `src/esp01/`, **outside** `src/peripheral/`, and
that is not filing. It is a deliberate isolation contract, stated in that
directory's `CMakeLists.txt` and enforced by the build.

The reasoning is simple: an AT-command engine over sockets is not
Spectrum-specific. Any other emulator, or any tool that needs to speak to
software expecting an ESP-01, wants the same code — and the only thing that
makes such a component reusable rather than theoretically-extractable is that
nothing in it ever reached for its host. Isolation here is a property that was
designed in and is checked, not one that happens to hold today.

Four consequences, all deliberate:

- **The `esp01` target links nothing from jnext.** Not `jnext_peripheral`, not
  `jnext_core`, not even the project's spdlog wrapper. Its interface
  dependencies are the C++17 standard library and the OS components that
  library needs — the system threading library on POSIX, and Winsock on
  Windows for the sockets themselves. Both are OS components rather than
  third-party packages, so the project's no-new-dependency rule is untouched.
- **`include/` is the single public include root.** Every include of a module
  header reads `#include "esp01/<name>.h"` — inside the module, in its own
  tests, and in jnext alike. A consumer adds one directory and nothing else.
- **The tests ship with the code.** `src/esp01/test/` holds two suites: one
  drives the AT engine against an in-memory fake transport with no socket, no
  DNS and no listener, and one covers the address policy and the transport
  against an in-process loopback listener. A consumer gets the proof alongside
  the implementation instead of taking it on trust, and can run the two
  binaries directly without adopting jnext's test manifest.
- **The thread is optional.** `esp01/esp_threaded.h` wraps the passive core so
  socket work runs off the guest's thread, but the core is equally drivable
  inline by a consumer with its own scheduler. It is always built, so no one
  has to reconfigure to get it, and both modes are exercised by the module's
  suites — an unexercised alternative mode rots.

Everything that couples the two sides lives on **jnext's** side of the line:

- `esp_uart_adapter.{h,cpp}` implements jnext's `UartDevice` in terms of the
  module's `esp::EspDevice`, installs a sink that pushes bytes into the guest's
  RX FIFO, mirrors the core's tick gate, and binds the module's logging seam to
  jnext's `esp01` logger so `--log-level esp01=debug` works. It is five
  forwarding methods plus one flag; if it ever grows a state machine, something
  has been put on the wrong side of the line.
- `esp_host_policy.{h,cpp}` holds what a reusable module cannot own, because it
  is about *this program's* relationship with *its* user: the optional
  `--esp-allow` hostname allowlist, and a bounded thread-safe connection log
  the GUI can display, so every attempt, success and refusal is visible. It
  reaches the engine as a decorator over the module's transport interface,
  which is what lets jnext add an allowlist without editing a line of
  `src/esp01/`.
- jnext's own coupling suites live in `test/esp/` and `test/gui/`, separate
  from the module's.

The address-based half of the security posture, by contrast, is *inside* the
module, as `esp::AddressPolicy`: it denies loopback, link-local,
cloud-metadata, unspecified and multicast addresses while deliberately allowing
RFC1918, so the guest can reach the user's own LAN. That belongs with the
transport because it is a property of the component in any host, not of jnext.

The adapter also carries a **replay gate**. During rewind fast-forward and RZX
playback the adapter goes inert rather than detaching, because detaching would
re-enable the UART channel's loopback and inject bytes the original run never
saw. Going inert is what "the ESP is silent" actually has to mean, and the gate
is needed because a live network is not re-executable — replaying an
`AT+CIPSTART` would open a second real connection.

## I2C and the RTC

`i2c.{h,cpp}` is a bit-banged bus decoder on ports `0x103B` (SCL) and `0x113B`
(SDA), with reads ANDing the internal line against the Raspberry Pi bridge
inputs. The only device attached to it is the Next's real-time clock, `I2cRtc`
at address `0x68` — a DS1307 modelled with the full register map, BCD encoding,
NVRAM, the CH oscillator-halt bit and 12-hour mode. It reads the host clock by
default; `--rtc` pins it to a fixed instant, which is what makes boot
screenshots reproducible.

## Multiface

The Multiface was a Romantic Robot cartridge with a physical button on it. Press
the button mid-program and it raises an NMI, pages its own ROM in over the
bottom of memory and puts up a menu, from which the running program could be
saved to tape or patched — the standard way to snapshot or poke a game in the
1980s. The Next reimplements all three historical variants in its FPGA.

`multiface.{h,cpp}` models `device/multiface.vhd` as four flip-flops
(`nmi_active`, `invisible`, `mf_enable`, `port_io_dly`) plus a mode-dependent
port decode selected by NR `0x0A` bits 7:6 — MF+3, MF128 in two variants, MF1.
The enable and disable strobes land on different low bytes in each mode, which
is why dispatch reaches it through an I/O observer rather than a handler.

The memory overlay itself is in the MMU, not here: `Mmu` holds a non-owning
`Multiface*` and checks `is_mem_active()` at priority 1, below the boot ROM and
above DivMMC, mapping the ROM at `0x0000` and the RAM at `0x2000`. On a Next the
two halves are **backed by external SRAM pages `0x0A` and `0x0B`**, because
tbblue.fw loads `enNextMf.rom` into page `0x0A` during boot; standalone machines
use the private buffers instead.

## NMI arbitration

Three devices can pull the Z80's non-maskable interrupt line — the Multiface,
DivMMC and the expansion bus — and they can want it at the same time, so the
hardware arbitrates. `nmi_source.{h,cpp}` is not a device but that arbiter, from
`zxnext.vhd:2089-2170`: the three producers with their NR `0x06` and NR `0x81`
enable gates, a priority chain of MF > DivMMC > ExpBus, and a four-state FSM
that advances on the `0x0066` M1 fetch — the address the Z80 jumps to when it
takes an NMI, which is how the hardware knows the request was accepted.

It is fully wired; its own header still calls itself a "Phase-1 scaffold", which
is stale. Stackless NMI (NR `0xC0` bit 3) is out of scope.
