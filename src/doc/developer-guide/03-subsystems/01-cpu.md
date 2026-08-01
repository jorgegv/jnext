# 3.1 CPU

`src/cpu/` holds the `Z80Cpu` wrapper (`z80_cpu.h/.cpp`), the Z80N extension
opcodes (`z80n_ext.h/.cpp`), the IM2 interrupt fabric (`im2.h/.cpp`) and the
small `Im2Client` facade peripherals use to reach it. The CTC is *not* here
despite driving interrupts — it lives in `src/peripheral/ctc.h/.cpp`.

## The wrapper and the backend

The rest of the emulator only ever sees `Z80Cpu`. No FUSE type appears in
`z80_cpu.h`, which is the point: swapping the backend means rewriting
`z80_cpu.cpp` and nothing else. `Z80Cpu` reaches the machine through two
abstract interfaces declared in that header — `MemoryInterface` and
`IoInterface` — implemented by `Mmu` and `PortDispatch`.

The backend is the FUSE Z80 core, vendored under `third_party/fuse-z80/` from
FUSE 1.6.0 (GPLv2-or-later, compatible with JNEXT's GPLv3). `fuse_z80_core.c`
is one translation unit that `#include`s the opcode files; `fuse_z80_shim.h`
stubs out every FUSE emulator dependency they would otherwise pull in. The core
is inherently single-instance — a global `z80` struct and a global `tstates`
counter — which matches JNEXT's one-CPU design. `Z80Cpu` re-points the
file-static `s_mem` / `s_io` pointers on every `reset()` and `execute()`, and
copies the register file in and out around each instruction.

`Z80Registers` carries the architectural registers plus MEMPTR, FUSE's `Q`
latch, and `IncDecZ` — a one-bit shadow that exists only in the VHDL
(`t80n.vhd:1358-1367`) and becomes observable through the Z80N `LDWS`
instruction. It cannot be read out of FUSE, so `execute()` reconstructs it after
the fact for DJNZ, `INC BC` / `DEC BC` and the ED block transfers, walking any
DD/FD prefix chain first because those prefixes do not change which mcode path
runs.

## Z80N interception

`execute()` reads the byte at PC itself. On `0xED` it reads the extension byte
and consults `kZ80NOpcodeTable`, a 256-entry table built at static-init from the
`Z80NOpcode` enum. A hit dispatches to `execute_z80n()` and never reaches FUSE,
which would decode it as a NOP; a miss falls through. `ED FF` and `DD 01` are
checked first, as the two magic-breakpoint encodings.

Z80N instructions account for their own time: the wrapper charges the two M1
fetches via `contend_read()`, `execute_z80n()` charges every operand access
through the same FUSE bus callbacks the standard opcodes use, and the wrapper
returns the elapsed `tstates` delta rather than a table constant. `NEXTREG nn,n`
and `NEXTREG nn,A` route through `IoInterface::nextreg_opcode_write()` rather
than writing ports 0x243B/0x253B, because the opcode must not disturb the
port-0x243B select latch — a following `IN A,(0x253B)` still reads whatever the
port last selected.

## Bus callbacks and contention

`fuse_z80_readbyte`, `fuse_z80_writebyte`, `fuse_z80_readport` and
`fuse_z80_writeport` are the `extern "C"` functions the FUSE macros expand to;
they dispatch to `s_mem` / `s_io` and add the base FUSE timing.

Contention is the subtle part, and is not applied where you might expect.
`Z80Cpu::on_contention` exists but `Emulator::init()` sets it to `nullptr`, and
no pre-computed table is consulted. Every bus cycle instead calls
`ContentionModel::contention_tick()`, a per-cycle transcription of the VHDL
gate. There are seven such sites: the four callbacks above, plus the three FUSE
*in-opcode* macros `contend_read`, `contend_read_no_mreq` and
`contend_write_no_mreq`. Those three become extern calls because
`src/cpu/CMakeLists.txt` compiles the FUSE translation unit — and only that one
— with `-DCORETEST`, flipping the macros in `z80_macros.h` from table lookups to
overridable functions.

Each site first tests `contention_possible()` (NR 0x08 bit 6, NR 0x07 CPU speed,
Pentagon timing) so the raster arithmetic is skipped when contention cannot
fire. When it can, `derive_hc_vc()` turns the FUSE `tstates` counter into a raw
frame position and `to_ula_counters()` rebases it onto the ULA's own
display-relative counters — the VHDL gate is written against `i_hc`/`i_vc`,
which reset at the start of the active display rather than of the frame, so raw
coordinates would contend the top border while missing the bottom 64 display
lines. The runtime is installed by `z80_set_contention_runtime()`; with it null,
all seven sites are inert.

A separate stretch is the 28 MHz SRAM read wait (`zxnext.vhd:3171-3181`): at CPU
speed 3 every memory *read* cycle reaching external SRAM costs one extra
T-state, qualified per target by `Mmu::sram_read_wait28()`.

## M1 hooks and interrupts

Two callbacks bracket the opcode fetch. `on_m1_prefetch` fires *before* the
read, because DivMMC automap, the NMI FSM and the Multiface window must be
settled before the byte is fetched through them. `on_m1_cycle` fires *after*,
once per fetched byte of a prefix chain — including the inner byte of
`DD ED xx` — because the IM2 opcode decoder models one event per M1.

`request_interrupt()` records the T-state at which /INT was asserted, and
`execute()` drops a pending request once the hardware pulse window has elapsed
(32 CPU cycles on 48K/+3, 36 otherwise, per `zxnext.vhd:2033`) — unconditionally,
not gated on IFF1, because the hardware line goes high regardless. It also
replicates FUSE's EI-grace rejection *before* calling `on_int_ack`, so the daisy
chain is not advanced by an acknowledge cycle that never happens. The vector
comes from `on_int_ack()` when installed, otherwise from `int_vector_`. NMI has
its own path, including the Next stackless mode (NR 0xC0 bit 3), which
suppresses the stack writes and substitutes the live NR 0xC3:0xC2 pair on the
matching RETN/RETI.

## The IM2 fabric

`Im2Controller` owns 14 device slots in VHDL priority order (`DevIdx`). Each
carries the `im2_peripheral.vhd` wrapper state — request edge detect, enable
bit, unqualified one-shot, status latch — and the `im2_device.vhd` four-state
machine `S_0 → S_REQ → S_ACK → S_ISR` with its IEI/IEO daisy chain. The
controller also holds the `im2_control.vhd` decoder that recognises RETI/RETN
and IM-mode changes from the M1 byte stream, the legacy pulse-mode /INT
generator, and the NR 0xCC/CD/CE DMA-delay latch. It is ticked once per
instruction with the T-states that instruction consumed. Peripherals do not
call it directly in new code: they hold an `Im2Client`, a two-field facade
binding one `DevIdx`.

## What the FUSE suite proves

`test/fuse/fuse_z80_test.cpp` runs FUSE's own `tests.in` / `tests.expected`
corpus — 1356 cases — comparing the entire register file, MEMPTR, IFF1/IFF2,
IM, the halt flag, every touched memory byte, and the **total** T-state count.
`test/z80n/z80n_test.cpp` is the equivalent harness for the Z80N set (85 cases).

What it does not prove matters as much. It does not check the per-bus-cycle
event trace FUSE's coretest emits, only the total. And it runs `Z80Cpu` against
a flat 64 KB `TestMemory` and a `TestIO` that echoes the port high byte: no MMU,
no contention runtime, no IM2 acknowledge callback, no M1 hooks. A green
1356/1356 is a statement about instruction semantics and per-instruction totals
in isolation; everything the wrapper adds on top — contention stretch, IM2
arbitration, automap timing, the pulse window — is proved by other suites and by
the screenshot regression.
