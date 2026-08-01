# 3.2 Memory

`src/memory/` holds `Mmu`, the flat `Ram` and `Rom` backing stores, the
`ContentionModel`, and `AttributeMux`. Two more pieces of the memory story live
in `src/core/`: `sd_rom_extractor.h/.cpp`, which reads ROM files out of the SD
image at startup, and `embedded_nextboot_rom.h`, which is the FPGA boot ROM
compiled into the binary.

## The slot model

`Mmu` implements `MemoryInterface`, so it *is* what the CPU sees. The address
space is eight 8 KB slots. Two arrays of raw pointers — `read_ptr_[8]` and
`write_ptr_[8]` — are rebuilt by `rebuild_ptr(slot)` whenever a mapping
changes, so the common case ends in `ptr[addr & 0x1FFF]` with no branching on
page numbers. A read-only slot gets a null write pointer; an unmapped slot gets
null on both sides and reads back 0xFF, which is the floating bus.

That fast path is the *last* thing `read()` and `write()` do. Above it sits a
priority cascade transcribing the VHDL SRAM arbiter (`zxnext.vhd:3029-3132`),
and the order is load-bearing: boot-ROM overlay, Multiface window, DivMMC
window, Layer 2 read-over/write-over, alt-ROM override, config-mode routing
(NR 0x03 / NR 0x04), then the dispatch pointer.

Two page-number spaces coexist and are easy to confuse. `nr_mmu_[8]` is the
register-visible NR 0x50–0x57 value, including the 0xFF sentinel meaning "this
slot is under legacy ROM paging"; `slots_[8]` is the physical SRAM page actually
served. `to_sram_page()` applies the VHDL `mmu_A21_A13` shift (`+0x20`, Next
mode only). `get_page()` returns the register value, `get_effective_page()`
resolves through the sentinel.

## RAM, ROM, and where ROMs come from

`Ram` is a flat `std::vector<uint8_t>`, 2 MB by default, addressed in 8 KB
pages. `Rom` is 4×16 KB plus the NR 0x8C alt-ROM configuration byte — note that
`Rom` holds only the *configuration*; alt-ROM **content** lives in `Ram`, and
`Mmu::nr_8c_*` is the authoritative gate.

The FPGA boot ROM is baked into the executable: `src/core/embed_rom.cmake` turns
`roms/nextboot.rom` into a C byte array at build time — a portable replacement
for `objcopy`, which macOS does not ship in a usable form. It overlays
0x0000–0x3FFF while `bootrom_en` is set, indexed by `addr & 0x1FFF`, so the
upper 8 KB mirrors the lower half exactly as the VHDL wires `cpu_a(12:0)`.

Everything else is pulled out of the SD-card image at init by
`extract_sd_rom()`, a read-only MBR + FAT32 parser completely independent of the
runtime SPI path in `src/peripheral/sd_card.cpp` — one serves the host at
startup by name, the other the guest at runtime by block. The files are
`/MACHINES/NEXT/48.rom` (48K, and the Next fallback), `128.rom`, `plus3.rom`,
`enNxtmmc.rom` (DivMMC), `enNextMf.rom` (Multiface) and `enAltZX.rom`.

On the Next machine, `set_rom_in_sram(true)` makes ROM-mapped slots read `Ram`
pages 0..7 rather than the `Rom` buffer, after seeding those pages from `Rom`.
That is what lets `tbblue.fw`'s `load_roms()` write ROM content through the
config-mode window and have ordinary ROM reads see it afterwards — without it,
NextZXOS does not survive its own soft reset.

## Legacy paging

Ports 0x7FFD, 0x1FFD, 0xDFFD and 0xEFF7 do not maintain a separate page map.
Their write handlers update latch registers and then call
`apply_paging_update_()`, which recomposes the same eight slots:
`compose_bank_()` assembles the RAM bank number from the 7FFD low bits plus
either the DFFD extension bits or the 7FFD high bits (Pentagon-1024 mode);
`apply_legacy_ram_slots_()` writes slots 6/7; `apply_legacy_rom_slots_()` writes
slots 0/1 and normally leaves the 0xFF sentinel in `nr_mmu_`, so the resolved
ROM page comes from `sram_rom` rather than being stored. +3 special paging
(0x1FFD bit 0) has its own apply and revert functions.

## Overlays

**Layer 2** read-over and write-over are both modelled, driven by NR 0x12/0x13
and port 0x123B. The enable gate differs per address half exactly as the VHDL
does, and the `layer2_A21_A13(8)` corner — where the composed bank lands in the
inactive range — returns 0xFF on reads and drops writes rather than aliasing
into ROM space.

**DivMMC** and **Multiface** are non-owning pointers installed by `Emulator`,
with the MF window above DivMMC. The automap decision is not made here:
`Emulator`'s `on_m1_prefetch` closure computes the VHDL `sram_pre_override`
priority bits (`Mmu::sram_pre_override_divmmc_eligible()` and
`sram_pre_override_romcs_priority()`) and hands them to `DivMmc`.

## Bank 5 and bank 7

`bank5_vram_` (16 KB) and `bank7_bram_` (8 KB) are plain `std::array` members of
`Mmu`, standing in for the VHDL `dpram2` instances `bank5_ram` and `bank7_ram`
— **dedicated dual-port BRAMs, not slices of external SRAM**. `rebuild_ptr()`
routes MMU pages 0x0A/0x0B and 0x0E to them, but only when `rom_in_sram_` is
set, i.e. on the Next machine; the standalone 48K/128K/+3 personalities keep
those banks in flat RAM, and an ungated redirect splits a 128K bank 7 in half.

That distinction was the root cause of a NextZXOS boot failure: with the pages
aliased onto external SRAM, `tbblue.fw` loading `enNextMf.rom` into bank 5
through the config-mode window painted the visible screen. Other paths — the
config-mode NR 0x04 window, the CPU Layer 2 window, the Layer 2 pixel fetch —
deliberately keep addressing `ram_`, because on real hardware the arbiter forces
the BRAM gates low for them.

## Contention

`ContentionModel` decides CPU wait states, and it is worth being precise about
what runs. `build()` fills a `lut_[320][456]` table and `delay(hc, vc)` reads it
— but nothing on the runtime path consults either; the only callers of `delay()`
are in `test/contention/contention_test.cpp`.

The live path is `contention_tick()`, called per bus cycle from
`src/cpu/z80_cpu.cpp`. It transcribes the VHDL directly: the enable gate
(NR 0x08 bit 6, NR 0x07 speed, Pentagon timing), the window gate on `hc_adj`,
the `mem_contend` page decode selected by timing mode, the `port_contend`
decode, then the stretch magnitude from `kPat48[hc & 0xF]` or
`kPatP3[hc & 0xF]`. Those patterns have **sixteen** entries because they are
indexed by a 7 MHz pixel tick, not by a T-state; indexing FUSE's eight-entry
per-T-state pattern with a pixel counter under-charged five of the six contended
phases and displaced contention-locked engines such as BIFROST by about 22
T-states per line.

Two axes stay separate throughout, following the VHDL. `MachineType`
(`ZXN_ISSUE2`, `ZX48K`, `ZX128K`, `ZX_PLUS3`) is the `typ_sel` axis and comes
from `--machine`. `MachineTimingMode` (`Timing48`, `Timing128`, `TimingPlus3`,
`TimingPentagon`) is the `tim_sel` axis, switchable at runtime through NR 0x03.
**Pentagon is a timing mode only** — there is no Pentagon `MachineType` and no
`--machine pentagon`; guest software reaches it by writing NR 0x03.

`AttributeMux` models the mid-line attribute-write multiplexing Nirvana-class
multicolour routines depend on, tagged with the raw frame `(hc, vc)` at the
instant the byte lands on the bus. `Mmu` keeps the +3 floating-bus latch
`p3_floating_bus_dat_`, updated on every read `mem_contend_for_(addr)` says is
contended — a per-page decode, not the older per-16 KB mirror.

Finally, `Mmu::reset(bool hard)` distinguishes the two VHDL reset domains: a
soft reset preserves the alt-ROM bits, the contention-disable bit and the 7FFD
paging lock, which the VHDL clears only on the hard `reset` signal.
