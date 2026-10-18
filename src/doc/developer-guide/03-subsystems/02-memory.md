# 3.2 Memory

A Z80 can address 64 KB. A ZX Spectrum Next has up to 2 MB of RAM, several
ROMs, a DivMMC overlay, a Multiface window and a Layer 2 bitmap that can be
paged in over the top of any of it. The memory subsystem is what reconciles
those two facts, and it is the busiest code in the emulator: every instruction
fetch, every operand read and every write passes through it.

The Next's own answer to paging is the MMU. The 64 KB address space is divided
into eight 8 KB slots, and each slot has a register — NR 0x50 through NR 0x57 —
naming the physical 8 KB page it currently serves. That is four times finer than
the classic 128K scheme, in which a single 16 KB bank is swapped in at 0xC000
through port 0x7FFD. Both schemes are live at the same time, because Next
programs and 128K programs run on the same machine, and the way JNEXT keeps them
consistent is that the legacy ports maintain no map of their own: they recompose
the same eight slots.

On top of that sit the *overlays* — the boot ROM, the DivMMC and Multiface
windows, the Layer 2 CPU window, the alt ROM, the config-mode routing — each of
which can claim an address range out from under the slot map. Their relative
priority is fixed in hardware, and getting that order wrong produces failures
that look like anything but a memory bug.

In the emulator, `Mmu` implements `MemoryInterface`, so it *is* what the CPU
sees. The video layers deliberately do not go through it: they read physical RAM
banks directly, as [3.3 Video](03-video.md) describes.

`src/memory/` holds `Mmu`, the flat `Ram` and `Rom` backing stores, the
`ContentionModel` and `AttributeMux`. Two further pieces of the memory story
live in `src/core/`: `sd_rom_extractor.h/.cpp`, which reads ROM files out of the
SD image at startup, and `embedded_nextboot_rom.h`, the FPGA boot ROM compiled
into the binary.

## The slot model

Two arrays of raw pointers — `read_ptr_[8]` and `write_ptr_[8]` — are rebuilt by
`rebuild_ptr(slot)` whenever a mapping changes, so the common case ends in
`ptr[addr & 0x1FFF]` with no branching on page numbers at all. A read-only slot
gets a null write pointer; an unmapped slot gets null on both sides and reads
back 0xFF, which is the floating bus.

That fast path is the *last* arm of one arbiter, `Mmu::route(addr)`: the single
function that decides what supplies a CPU read, transcribing the VHDL decode
order (`zxnext.vhd:2937-2945`, the SRAM arbiter `:3029-3132` and the `cpu_di` mux
`:1856-1870`). Its order is load-bearing: boot-ROM overlay (above the whole SRAM
arbiter), Multiface window, DivMMC ROM and RAM, Layer 2 read-over, alt-ROM
override, config-mode routing (NR 0x03 / NR 0x04), and only then the slot's own
pointer. It returns a `Route`: the `MemSpace` that supplied the byte, the offset
within that store, and the byte's address (null when nothing answers, which reads
back 0xFF). The spaces are `Page{n}` (an MMU RAM page, or on the Next the bank-5 /
bank-7 BRAM pages), `Rom{i}`, `BootRom`, `MultifaceRom` / `MultifaceRam`,
`DivMmcRom` / `DivMmcRam{bank}`, `AltRom{i}`, `Layer2{page}`, `Config{bank}` and
`Floating`. Equality is by decode path, not by physical store: `Layer2{p}` and
`Page{p}` share external SRAM for most `p` and stay different spaces.

`route_with(addr, divmmc_active, mf_active)` is the same body asked about an M1
that has not happened yet, with the two overlays that M1 itself can switch
(DivMMC's automap, the Multiface's 0x0066 fetch) given instead of read: it is how
a debugger predicts the source of the next opcode fetch
(`Emulator::predict_fetch()`), and it cannot disagree with `read()` because it is
`read()`'s function. The write path keeps its own cascade, because write
arbitration has rules the read side does not (read-only halves, the Layer 2 write
enable, alt-ROM write-over).

`read()` and `read_m1()` are `route()` followed by one side-effect tail for every
source: the byte (0xFF for a null pointer), the +3 floating-bus latch and the
READ watchpoint. `read_m1()` is the opcode fetch of `Z80Cpu::execute()`; it also
records what supplied the first opcode byte, readable as `last_m1()`. That record
is `MemSpace::cpu()`, "none recorded", after `reset()` and `load_state()` until
the next fetch, and an accepted INT or NMI, which fetch nothing
(`Z80Cpu::fetched_opcode_last_execute()` is false), leave it as it was.

A DivMMC entry point makes the fetch and the bus cycles after it see different
memory (`divmmc.vhd:128-148`): `automap_held` takes `automap_hold` on the first
MREQ-high after the opcode byte, so a delayed-on entry's own operands already
come from the DivMMC, and those of an instruction in the 0x1FF8-0x1FFF unmap
range already come from RAM. `read_m1()` and `read_fetch()` (a prefix's inner
byte; no record) route with the fetch view, `DivMmc::is_active()`; `read()` and
the write path use `DivMmc::is_active_after_fetch()`. Every byte of a prefix chain
is an M1 of its own (it loads the hold again), so `Z80Cpu::fetch_prefix_chain_()`
runs the `on_m1_prefetch` decode at each inner address and reads that byte once
into `s_fetch_buf`; the FUSE core's re-reads of the M1 bytes are served from it.
The `d` and `op` of `DD CB d op` are no M1 (`t80n.vhd:414-417, 1755-1761`): plain
data reads. The debugger's reads of the instruction about to run follow the same
split (`Emulator::predict_instruction_()`: each M1 byte under the overlay its own
M1 selects, the DivMMC hold chained from one M1 to the next with
`DivMmc::active_on_m1()` / `hold_after_m1()`, the rest under
`DivMmc::active_with_hold()`), pure, through `route_with()`.

The identity of a slot's own source is cached beside the pointer: `slot_space_[8]`
is written wherever `read_ptr_[slot]` is (`rebuild_ptr_body_()` and
`map_rom_physical()`), rebuilt by `load_state()`, never serialised. Deriving it
per fetch would cost more than the cache, which is one load.

Two page-number spaces coexist here and are easy to confuse. `nr_mmu_[8]` holds
the register-visible NR 0x50–0x57 value, including the 0xFF sentinel that means
"this slot is under legacy ROM paging"; `slots_[8]` holds the physical SRAM page
actually served. `to_sram_page()` applies the VHDL `mmu_A21_A13` shift (`+0x20`,
Next mode only). `get_page()` returns the register value, and
`get_effective_page()` resolves through the sentinel.

## RAM, ROM, and where ROMs come from

`Ram` is a flat `std::vector<uint8_t>`, 2 MB by default, addressed in 8 KB
pages. `Rom` is 4×16 KB plus the NR 0x8C alt-ROM configuration byte — and note
that `Rom` holds only the *configuration*. Alt-ROM **content** lives in `Ram`,
and `Mmu::nr_8c_*` is the authoritative gate.

The FPGA boot ROM is baked into the executable: `src/core/embed_rom.cmake` turns
`roms/nextboot.rom` into a C byte array at build time, a portable replacement
for `objcopy`, which macOS does not ship in a usable form. It overlays
0x0000–0x3FFF while `bootrom_en` is set and is indexed by `addr & 0x1FFF`, so
the upper 8 KB mirrors the lower half exactly as the VHDL wires `cpu_a(12:0)`.

Everything else is pulled out of the SD-card image at init by
`extract_sd_rom()`, a read-only MBR + FAT32 parser that is completely
independent of the runtime SPI path in `src/peripheral/sd_card.cpp`. The two
never meet: one serves the host at startup, by name; the other serves the guest
at runtime, by block. The files are `/MACHINES/NEXT/48.rom` (48K, and the Next
fallback), `128.rom`, `plus3.rom`, `enNxtmmc.rom` (DivMMC), `enNextMf.rom`
(Multiface) and `enAltZX.rom`.

On the Next machine, `set_rom_in_sram(true)` makes ROM-mapped slots read `Ram`
pages 0..7 rather than the `Rom` buffer, after seeding those pages from `Rom`.
That is what lets `tbblue.fw`'s `load_roms()` write ROM content through the
config-mode window and have ordinary ROM reads see it afterwards. Without it,
NextZXOS does not survive its own soft reset.

## Legacy paging

Ports 0x7FFD, 0x1FFD, 0xDFFD and 0xEFF7 do not maintain a separate page map.
Their write handlers update latch registers and then call
`apply_paging_update_()`, which recomposes the same eight slots the MMU
registers drive. `compose_bank_()` assembles the RAM bank number from the 7FFD
low bits plus either the DFFD extension bits or the 7FFD high bits
(Pentagon-1024 mode); `apply_legacy_ram_slots_()` writes slots 6/7; and
`apply_legacy_rom_slots_()` writes slots 0/1, normally leaving the 0xFF sentinel
in `nr_mmu_` so that the resolved ROM page comes from `sram_rom` rather than
being stored. +3 special paging (0x1FFD bit 0) has its own apply and revert
functions.

## Overlays

**Layer 2** read-over and write-over are both modelled, driven by NR 0x12/0x13
and port 0x123B. The enable gate differs per address half exactly as the VHDL
does, and the `layer2_A21_A13(8)` corner — where the composed bank lands in the
inactive range — returns 0xFF on reads and drops writes, rather than aliasing
into ROM space.

**DivMMC** and **Multiface** are non-owning pointers installed by `Emulator`,
with the MF window sitting above DivMMC. The automap decision is not made here:
`Emulator`'s `on_m1_prefetch` closure computes the VHDL `sram_pre_override`
priority bits (`Mmu::sram_pre_override_divmmc_eligible()` and
`sram_pre_override_romcs_priority()`), through `Emulator::m1_automap_gates_()`,
and hands them to `DivMmc`. `predict_fetch()` calls the same function, so the
prediction and the hook cannot use different inputs. The DivMMC window's own
decode (ROM at page 0 unless MAPRAM, bank 3 at page 0 under MAPRAM, the selected
bank at page 1) exists once, in `DivMmc::overlay_at()`; `DivMmc::read()` and the
arbiter both use it. `DivMmc::active_on_m1()` and `Multiface::would_latch_on_m1()`
answer, without changing anything, what each overlay will be for an M1 at a given
pc.

## Bank 5 and bank 7

`bank5_vram_` (16 KB) and `bank7_bram_` (8 KB) are plain `std::array` members of
`Mmu`, standing in for the VHDL `dpram2` instances `bank5_ram` and `bank7_ram`
— **dedicated dual-port BRAMs, not slices of external SRAM**. `rebuild_ptr()`
routes MMU pages 0x0A/0x0B and 0x0E to them, but only when `rom_in_sram_` is
set, which is to say on the Next machine; the standalone 48K/128K/+3
personalities keep those banks in flat RAM, and an ungated redirect splits a
128K bank 7 in half.

That distinction was the root cause of a NextZXOS boot failure. With the pages
aliased onto external SRAM, `tbblue.fw` loading `enNextMf.rom` into bank 5
through the config-mode window painted the visible screen. Other paths — the
config-mode NR 0x04 window, the CPU Layer 2 window, the Layer 2 pixel fetch —
deliberately keep addressing `ram_`, because on real hardware the arbiter forces
the BRAM gates low for them.

## Contention

On a 48K or 128K Spectrum the CPU and the ULA share the same RAM, and the ULA
has priority. While it is fetching display bytes it holds the CPU off the bus,
so an instruction touching contended memory takes longer than its documented
time — by an amount that depends on where the raster beam is when the access
happens. Programs measure this: contention is how a good deal of Spectrum
software knows where the beam is, and effects locked to it fall apart if the
stretch is even slightly wrong. On the Next, contention is a compatibility
feature: present under 48K, 128K and +3 timing, absent under Pentagon timing,
switchable off outright through NR 0x08 bit 6, and simply outrun at any CPU
speed above 3.5 MHz.

`ContentionModel` is what decides the wait states, and it is worth being precise
about which part of it actually runs. `build()` fills a `lut_[320][456]` table
and `delay(hc, vc)` reads it — but nothing on the runtime path consults either.
The only callers of `delay()` are in `test/contention/contention_test.cpp`.

The live path is `contention_tick()`, called per bus cycle from
`src/cpu/z80_cpu.cpp`. It transcribes the VHDL directly: the enable gate
(NR 0x08 bit 6, NR 0x07 speed, Pentagon timing), the window gate on `hc_adj`,
the `mem_contend` page decode selected by timing mode, the `port_contend`
decode, and finally the stretch magnitude from `kPat48[hc & 0xF]` or
`kPatP3[hc & 0xF]`. An I/O cycle goes through `io_clock_tick()`, which combines
the two decodes per clock of the cycle: the port still sits on the address bus,
so a port in a contended *page* is contended even when the port itself is not
(`zxula.vhd:587-595`).

Those patterns have **sixteen** entries because they are indexed by a 7 MHz
pixel tick, not by a T-state. Indexing FUSE's eight-entry per-T-state pattern
with a pixel counter under-charged five of the six contended phases and
displaced contention-locked engines such as BIFROST by about 22 T-states per
line — which is what the distinction costs when you get it wrong.

Two axes stay separate throughout, following the VHDL. `MachineType`
(`ZXN_ISSUE2`, `ZX48K`, `ZX128K`, `ZX_PLUS3`, `PENTAGON`) is the `typ_sel` axis and comes
from `--machine`. `MachineTimingMode` (`Timing48`, `Timing128`, `TimingPlus3`,
`TimingPentagon`) is the `tim_sel` axis, switchable at runtime through NR 0x03.
`PENTAGON` is both: `typ_sel` 100 is the 128K family (`Mmu` has
the same arm for it as for `ZX128K`), and its default timing is
`TimingPentagon`. Guest software can still reach Pentagon *timing* on any
machine by writing NR 0x03.

`AttributeMux` replays, at beam time, every byte the display fetches from VRAM:
all of bank 5 (pages 0x0A and 0x0B) and of the bank-7 BRAM (page 0x0E). Three
layers fetch it: the ULA (pixel and attribute planes, the Timex second plane and
the shadow screen), the tilemap (map entry and tile definition, anywhere in
either bank) and LoRes (bank 5). The ULA fetches a pixel byte and an
attribute byte per column on every scanline (`zxula.vhd:270-303`), so a write
is seen by the first fetch after it, whichever plane it hit; the tilemap
fetches each 8-pixel group one character ahead (`tilemap.vhd:226-231`) and
LoRes a byte per 7 MHz pixel (`zxnext.vhd:6603-6631`). `beam_counters.h` holds
the `whc` / `phc` raw-`hc` derivations (`zxula_timing.vhd:476-522`) the
tilemap, LoRes and Layer 2 instants hang off. Each write is
tagged with the raw frame `(hc, vc)` at the instant the byte lands on the bus —
a position on the master clock, so it is the same at 3.5, 7, 14 and 28 MHz
(`derive_hc_vc` scales the CPU T-state counter by the divisor
`rebase_fuse_tstates_()` publishes). A CPU write states that position for its
one write: `fuse_z80_writebyte` calls `Mmu::attr_mux_set_write_pos()` before the
write and `attr_mux_end_write()` after it. Every other writer — the DMA, the
tape traps, the debugger's `poke` — is tagged with the line
`Emulator::on_scanline()` last set, at column 0. The log is growable (no write is
dropped) and per-frame work is proportional to the number of writes. The fetch
instants are tabulated in `attribute_mux.h`: pixel and attribute of a display
slot differ by two ticks, the slot's parity moves both, and the bytes a fine
X scroll shifts in come from a separate, later "secondary" fetch. Until GH #278
the CPU's position outlived its write, and the next non-CPU attribute write
took it over (`mmu_integration_test` G12-TAG). A write before framebuffer row 0
is tagged (row 0, hc 0): it precedes every row-0 fetch, and the tilemap and the
wide Layer 2 modes do fetch on row 0. Every read consumes such a write, so the
mux folds an offset's leading run of (0,0) writes into its baseline instead of
chaining them: the baseline is the frame-start bytes plus those writes. An
offset is *hot* when it holds a write the mux has not folded, so that a read of
it depends on the instant and walks the offset's chain of writes; an offset
written only that way (the frame interrupt's sysvar stores) is not hot, so no
read of it walks a chain and the tilemap does not re-fetch its block per group.
A (0,0) write that follows a later-tagged one (a coarse DMA write after a CPU
write on row 0) is chained as before (`mmu_test` G12-MUX-15..17).

`SramBeamLog` is the same idea for Layer 2, which fetches the SRAM bitmap one
pixel ahead of the beam (`layer2.vhd`, `hc_eff <= hc + 1`). The SRAM is 2 MB,
so it is sparse: an append-only per-frame log of `(key, address, value)` in
which each address remembers the byte its first write replaced, so an address
that was written reads back its frame-start value, and one that never was reads
live from `Ram` (also its frame-start value). It also keeps a bit per 16 KB SRAM
bank that holds a written address, and Layer 2 asks it once per span
(`banks_clean()`): a 256x192 row fetches one bank, a wide row up to eight from
its first, and a span none of whose banks was written is read straight from
`Ram` — the byte the replay would return. It is disarmed at every frame
start and armed when Layer 2 is on (at frame start, or at the moment it is
turned on); once armed it records every SRAM store, enabled or not, until the
frame ends. The stores are `Mmu::write()`'s four (Layer 2 write-over, alt-ROM,
config-mode, normal slot) and the debugger's page pokes
(`Mmu::log_page_store()`), which bypass `write()`; the list and what is
deliberately not logged are in `sram_beam_log.h`. All of them go through
`Mmu::sram_log_write_()`. It tests recording inline, so a store the log does not
record costs that one test, and leaves the rest to one out-of-line function,
`Mmu::sram_log_record_()`: the SRAM bound check, the tag, and the append with
`SramBeamLog::record_unchecked_()`, which makes no check of its own: both it
relies on (recording, and the address inside the SRAM) are the caller's. The
entry and slot storage keeps its size from frame to frame and grows, doubling,
only in a cold helper, so the append is straight-line code.
`Mmu` separately keeps the
+3 floating-bus latch `p3_floating_bus_dat_`, updated on every read and write
that `mem_contend_for_(addr)` says is contended — a per-page decode, not the
older per-16 KB mirror — whatever supplied the byte: an overlay read, a Layer 2
write-over and a write the arbiter drops all move it, because in the VHDL the
latch sits on the MREQ cycle and not on the SRAM arbiter
(`zxnext.vhd:4498-4509`).

Finally, `Mmu::reset(bool hard)` distinguishes the two VHDL reset domains: a
soft reset preserves the alt-ROM bits, the contention-disable bit and the 7FFD
paging lock, all of which the VHDL clears only on the hard `reset` signal.
