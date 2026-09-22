# 3.1 CPU

The Next runs a Z80, and every program it will ever run is a stream of Z80
instructions. That makes the CPU the emulator's metronome as much as its
processor: `Emulator` executes one instruction, then hands every other subsystem
the T-states that instruction consumed, and everything else — video, audio,
timers, DMA — advances by that amount. Nothing in JNEXT moves without the CPU
moving first.

It is not quite an ordinary Z80, though. The FPGA implements a soft core called
T80N: a Z80 with an extra instruction set of its own (Z80N), a clock that
software can switch between 3.5, 7, 14 and 28 MHz through NextREG 0x07, and an
interrupt fabric with fourteen prioritised sources instead of the single /INT
line a 48K Spectrum has. JNEXT reaches memory through `Mmu` and I/O through
`PortDispatch`, both behind abstract interfaces, and calls back out at
instruction-fetch time so that DivMMC paging, the NMI state machine and the
Multiface window can settle before the byte they affect is read.

`src/cpu/` holds the `Z80Cpu` wrapper (`z80_cpu.h/.cpp`), the Z80N extension
opcodes (`z80n_ext.h/.cpp`), the IM2 interrupt fabric (`im2.h/.cpp`) and the
small `Im2Client` facade peripherals use to reach it. The CTC is *not* here
despite driving interrupts — it lives in `src/peripheral/ctc.h/.cpp`.

## The wrapper and the backend

The rest of the emulator only ever sees `Z80Cpu`. No FUSE type appears in
`z80_cpu.h`, and that is the point: swapping the backend would mean rewriting
`z80_cpu.cpp` and nothing else. `Z80Cpu` reaches the machine through two
abstract interfaces declared in that header — `MemoryInterface` and
`IoInterface` — implemented by `Mmu` and `PortDispatch`.

The backend itself is the FUSE Z80 core, vendored under `third_party/fuse-z80/`
from FUSE 1.6.0 (GPLv2-or-later, compatible with JNEXT's GPLv3).
`fuse_z80_core.c` is a single translation unit that `#include`s the opcode
files, while `fuse_z80_shim.h` stubs out every FUSE emulator dependency those
files would otherwise pull in. The core is inherently single-instance — it keeps
one global `z80` struct and one global `tstates` counter — which happens to suit
JNEXT's one-CPU design. `Z80Cpu` re-points the file-static `s_mem` / `s_io`
pointers on every `reset()` and `execute()`, and copies the register file in and
out around each instruction.

`Z80Registers` carries the architectural registers plus MEMPTR, FUSE's `Q` latch
and `IncDecZ`. That last one is a one-bit shadow that exists only in the VHDL
(`t80n.vhd:1358-1367`), where it becomes observable through the Z80N `LDWS`
instruction. It cannot be read out of FUSE at all, so `execute()` reconstructs
it after the fact for DJNZ, `INC BC` / `DEC BC` and the ED block transfers,
walking any DD/FD prefix chain first because those prefixes do not change which
mcode path runs.

## The Z80N instruction set

Z80N is the Next's own extension to the Z80. Every one of its instructions is
encoded behind the `ED` prefix, in slots a real Z80 leaves undefined and quietly
executes as a two-byte NOP, so Z80N code is harmless on a plain Spectrum and
does real work on a Next. The additions exist to make cheap the things a Z80 is
bad at: `MUL D,E` is an 8×8→16-bit multiply, the `BSLA` / `BSRA` / `BSRL` /
`BSRF` / `BRLC` family barrel-shifts `DE` by `B`, `PIXELAD` converts a (Y, X)
coordinate pair in `DE` into the corresponding ULA screen address in `HL` while
`PIXELDN` steps that address down one pixel row, `NEXTREG nn,n` writes a NextREG
directly from an opcode, and the `LDIX` family copies blocks while treating one
byte value as transparent. This is not an optional corner of the machine —
NextZXOS itself uses `NEXTREG`, `MUL` and the `ADD rr,A` forms constantly.

### Interception

`execute()` reads the byte at PC itself. On `0xED` it reads the extension byte
and consults `kZ80NOpcodeTable`, a 256-entry table built at static-init from the
`Z80NOpcode` enum. A hit dispatches to `execute_z80n()` and never reaches FUSE,
which would otherwise decode it as a NOP; a miss falls through untouched.

Two encodings are checked before that table: `ED FF` and `DD 01`, the two magic
breakpoints. A magic breakpoint is an instruction a program plants in its own
code to stop the emulator at that exact spot and hand control to the debugger —
the two encodings are the ones ZEsarUX and CSpect established, and JNEXT honours
both. They only bite when `--magic-breakpoint` is given; otherwise they execute
as on real silicon: `ED FF` as a two-byte NOP, `DD 01 nn nn` as `LD BC,nn`
(the `DD` prefix ignored).

Z80N instructions account for their own time rather than quoting a table
constant. The wrapper charges the two M1 fetches via `contend_read()`,
`execute_z80n()` charges every operand access through the same FUSE bus
callbacks the standard opcodes use, and the wrapper returns the elapsed
`tstates` delta. `NEXTREG nn,n` and `NEXTREG nn,A` route through
`IoInterface::nextreg_opcode_write()` instead of writing ports 0x243B/0x253B,
because the opcode must not disturb the port-0x243B select latch: a following
`IN A,(0x253B)` still has to read whatever that port last selected.

## Bus callbacks and contention

On a 48K or 128K Spectrum the CPU and the ULA share the same RAM, and the ULA
wins. While it is fetching display bytes it holds the CPU off the bus, so an
instruction that touches contended memory takes longer than the data book says —
and by an amount that depends on where the raster beam happens to be. Software
can and does measure this, which is why contention has to be modelled rather
than approximated. On the Next it is a compatibility feature: present under 48K,
128K and +3 timing, absent under Pentagon timing, switchable off outright
(NR 0x08 bit 6), and simply outrun at any CPU speed above 3.5 MHz. The gate
itself lives in `ContentionModel` — see [3.2 Memory](02-memory.md) — and this
section is about how the CPU calls into it.

`fuse_z80_readbyte`, `fuse_z80_writebyte`, `fuse_z80_readport` and
`fuse_z80_writeport` are the `extern "C"` functions the FUSE macros expand to;
they dispatch to `s_mem` / `s_io` and add the base FUSE timing.

Contention is the subtle part, and it is not applied where you might expect.
There is no per-instruction contention hook on `Z80Cpu` — there used to be an
`on_contention` callback, but `Emulator::init()` only ever assigned it
`nullptr` and it has been removed — and no pre-computed table is ever
consulted either. Every bus cycle instead calls
`ContentionModel::contention_tick()`, a per-cycle transcription of the VHDL
gate. There are seven such call sites: the four callbacks above, plus the three
FUSE *in-opcode* macros `contend_read`, `contend_read_no_mreq` and
`contend_write_no_mreq`. Those three become extern calls only because
`src/cpu/CMakeLists.txt` compiles the FUSE translation unit — and only that one
— with `-DCORETEST`, which flips the macros in `z80_macros.h` from table lookups
into overridable functions.

Each site first tests `contention_possible()` (NR 0x08 bit 6, NR 0x07 CPU speed,
Pentagon timing), so the raster arithmetic is skipped entirely when contention
cannot fire. When it can, `derive_hc_vc()` turns the FUSE `tstates` counter into
a raw frame position and `to_ula_counters()` rebases that onto the ULA's own
display-relative counters. The rebase is not a nicety: the VHDL gate is written
against `i_hc`/`i_vc`, which reset at the start of the active display rather
than at the start of the frame, so feeding it raw coordinates would contend the
top border while missing the bottom 64 display lines. The runtime is installed
by `z80_set_contention_runtime()`; with it null, all seven sites are inert.

The two port callbacks charge the I/O cycle clock by clock — T1, the automatic
wait, T2, T3 — each clock's stretch from `ContentionModel::io_clock_tick()`
(`zxula.vhd:587-595`): the port's *page* can stretch any clock, the *port*
decode only the second, and a port-contended cycle stops stretching after it.
`fuse_z80_readport` calls the port handler in the last clock, after every
stretch, because the T80 latches the data bus only on the falling edge of T3;
`fuse_z80_writeport` calls it at the start of the second clock, because a write
strobe acts as soon as IORQ and WR go low. The order matters to any read whose
value depends on time — the floating bus, NR 0x1E/0x1F, the tape EAR bit — and
`Z80Cpu::io_clock_into_instruction()` tells those reads where each clock of the
cycle began, stretches included (GH #265).

`derive_hc_vc()` only gives the right answer if the FUSE counter and the master
clock agree: counter × CPU divisor = clock − frame start, at every instruction
boundary. `Emulator::rebase_fuse_tstates_()` re-establishes that where the
counter's origin or unit changes — the frame start, which seeds it with the
last instruction's overshoot past the frame end, and an effective CPU-speed
change — and `advance_fuse_tstates_()` moves it with the clock when the clock
advances with no CPU instruction (DMA holding the bus, a parked CPU, a tape
trap's synthetic cycles).

A separate stretch is the 28 MHz SRAM read wait (`zxnext.vhd:3171-3181`): at CPU
speed 3, every memory *read* cycle that reaches external SRAM costs one extra
T-state, qualified per target by `Mmu::sram_read_wait28()`.

## M1 hooks and interrupts

Two callbacks bracket the opcode fetch, and the order matters. `on_m1_prefetch`
fires *before* the read, because DivMMC automap, the NMI FSM and the Multiface
window all decide what memory the CPU is about to see, and must be settled
before the byte is fetched through them. `on_m1_cycle` fires *after*, once per
fetched byte of a prefix chain — including the inner byte of `DD ED xx` —
because the IM2 opcode decoder models one event per M1.

`request_interrupt(vector, first, last)` gives the CPU the window of
instruction boundaries, on the FUSE T-state counter, at which /INT is seen low.
The T80 samples INT_n into INT_s on every CPU rising edge (`t80n.vhd:1664`) and
decides at the end of an instruction with the INT_s taken at the *start* of its
last T-state (`:1742-1772`), so a pulse first sampled on CPU edge E_1 and last on
E_N is taken at a boundary in [E_1 + 1 T, E_N + 1 T] — 32 CPU cycles on 48K/+3,
36 otherwise (`zxnext.vhd:2033`). `execute()` takes it at the first boundary
inside the window, keeps it pending at one before (a request resolved at the end
of an instruction can be too late for that instruction's boundary), and drops it
once the window has passed — unconditionally rather than gating on IFF1, because
the hardware line goes high again regardless of whether anyone was listening.
The one-argument `request_interrupt(vector)` is the window "from now, for the
pulse width", for callers without a timeline. `rebase_fuse_tstates_()` moves
the window, and FUSE's EI stamp, with the counter whenever it re-bases it — at
each frame, and at a CPU-speed change, where the window keeps its distance in
CPU T-states because the pulse counts CPU clock edges (the IM2 fabric re-places
its own pulse edges, `Im2Controller::set_cpu_divisor()`) (GH #265). A snapshot
stores the window relative to the counter, which a load re-seeds, and the EI
stamp as whether its grace is pending at the saved boundary. `execute()` also replicates FUSE's EI-grace rejection *before*
calling `on_int_ack`, so the daisy chain is not advanced by an acknowledge cycle
that never happens. The vector comes from
`on_int_ack()` when one is installed, and from `int_vector_` otherwise. NMI has
a path of its own, including the Next's stackless mode (NR 0xC0 bit 3), which
suppresses the stack writes and substitutes the live NR 0xC3:0xC2 pair on the
matching RETN/RETI.

## The IM2 fabric

`Im2Controller` owns 14 device slots in VHDL priority order (`DevIdx`). Each
slot carries the `im2_peripheral.vhd` wrapper state — request edge detect,
enable bit, unqualified one-shot, status latch — and the `im2_device.vhd`
four-state machine `S_0 → S_REQ → S_ACK → S_ISR` with its IEI/IEO daisy chain.
The controller additionally holds the `im2_control.vhd` decoder that recognises
RETI/RETN and IM-mode changes out of the M1 byte stream, the legacy pulse-mode
/INT generator, and the NR 0xCC/CD/CE DMA-delay latch.

It is ticked once per instruction, at the *end* of the instruction's slot
(`Emulator::finish_slot_interrupts()`), after the devices and the interrupt
events have raised that instruction's requests, and it runs on the CLK_28
timeline (GH #265): each request carries the edge it was raised on
(`raise_req(dev, edge)`), and the tick resolves them in edge order — the status
and `im2_int_req` latches set on the edge after the request
(`im2_peripheral.vhd:154-178`), the pulse falling on the CLK_28 falling edge
after it and lasting 32/36 CPU edges (`zxnext.vhd:2017-2044`), a device
entering S_REQ on the first CPU edge after its latch that has M1_n high
(`im2_device.vhd:106`; M1_n is low for T1-T2 of every opcode fetch, which is why
the tick is told how many fetches the instruction opened with).
`latch_edges_until()` does the same part-way through an instruction, for an
interrupt-status read or a write that must be ordered against the requests.
A request raised while no instruction runs — a DMA burst, a NEX boot hold —
keeps its edge for the status latch, but its pulse starts with the next
instruction: jnext's DMA holds the bus for whole bursts, and letting a pulse
expire unseen behind one would be a bus-arbitration model jnext does not have,
not the VHDL.

Peripherals do not call the controller directly in new code. They hold an
`Im2Client` instead — a two-field facade binding one `DevIdx` — so a device
needs to know its own priority slot and nothing else about the fabric.

## What the FUSE suite proves

`test/fuse/fuse_z80_test.cpp` runs FUSE's own `tests.in` / `tests.expected`
corpus of 1356 cases, comparing the entire register file, MEMPTR, IFF1/IFF2, IM,
the halt flag, every touched memory byte, and the **total** T-state count.
`test/z80n/z80n_test.cpp` is the equivalent harness for the Z80N set, with 85
cases.

What those suites do *not* prove matters just as much. They do not check the
per-bus-cycle event trace FUSE's coretest emits, only the total. And they run
`Z80Cpu` against a flat 64 KB `TestMemory` and a `TestIO` that echoes the port
high byte: no MMU, no contention runtime, no IM2 acknowledge callback, no M1
hooks. A green 1356/1356 is a statement about instruction semantics and
per-instruction totals in isolation. Everything the wrapper layers on top —
contention stretch, IM2 arbitration, automap timing, the interrupt pulse window
— is proved by other suites and by the screenshot regression.
