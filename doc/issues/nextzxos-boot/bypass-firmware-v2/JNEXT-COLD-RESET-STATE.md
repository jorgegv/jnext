# jnext cold-reset baseline (Task 18)

This document captures the state of the jnext emulator **immediately
after `Emulator::init()` returns**, before any firmware runs (no
`nextboot.rom` instructions executed, no `tbblue.fw`, no NextZXOS).
It is the reference state Task 18 (firmware-bypass investigation)
should compare against when synthesising a faked-firmware boot.

**Method.** A scratch probe (`test/task18_baseline/cold_reset_probe.cpp`;
build target `task18_cold_reset_probe`) constructs a single `Emulator`
with the canonical Next config (`MachineType::ZXN_ISSUE2`,
`sd_card_image = roms/nextzxos-1gb-fat32fix.img`, no `--load`), calls
`init()` exactly once, then dumps every NR register (both the raw
`cached()` byte and the live `read()` value through the registered
read handlers), the MMU slot map, Z80 register defaults, MMU control
flags, and the contents of every populated SRAM page. The probe is
SCRATCH — not registered with CTest, not part of the regression
suite — it exists only to materialise this document.

**VHDL oracle.** All "VHDL" column entries below cite
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
unless noted.

---

## 1. NR registers (task-18 scope)

Two columns: `cached` is the raw byte sitting in `NextReg::regs_[reg]`
after init; `read()` is what a Z80 `IN A,(0x253B)` after
`OUT (0x243B),reg` would actually observe (i.e. the live read mux,
through registered read handlers).

| NR  | cached | read() | VHDL default | Notes |
|-----|--------|--------|--------------|-------|
| 00  | 0x08   | 0x08   | g_machine_id = 0x0A (issue-5) | **jnext deviation by design** — reports 0x08 (HWID_EMULATORS) so NextZXOS takes its emulator-aware boot paths. `nextreg.cpp:223-224`. |
| 01  | 0x32   | 0x32   | g_version = 0x32 | Core 3.02. RO. `nextreg.cpp:225`. |
| 02  | 0x00   | 0x00   | n/a (write-strobe) | Reset/NMI generator strobe; reads zero. |
| 03  | 0x00   | **0x34** | nr_03 read mux | Read composes `tim_sel<<4 \| user_dt_lock<<3 \| typ_sel`. tim_sel=011(+3), typ_sel=100(ZXN). Read mux at zxnext.vhd:5894. **cached != read** because NextReg owns separate fields (`nr_03_machine_timing_`, `nr_03_machine_type_`, `nr_03_user_dt_lock_`). |
| 04  | 0x00   | 0x00   | nr_04_romram_bank=0x00 (zxnext.vhd:1104) | |
| 05  | 0x41   | 0x41   | joy0[2:0]=001, joy1[2:0]=000, 5060=0, scandbl=1 | Read formula zxnext.vhd:5897 → 0x41. `nextreg.cpp:236`. |
| 06  | 0xA0   | 0xA0   | bits 7,5 forced '1' (zxnext.vhd:4932-4933) | hotkey_cpu_speed_en + hotkey_5060_en latched high at reset. |
| 07  | 0x00   | 0x00   | cpu_speed="00" (zxnext.vhd:1300) | 3.5 MHz. |
| 08  | 0x00   | **0x90** | composed read (zxnext.vhd:5906) | bit 4 = nr_08_internal_speaker_en=1 (init `nr_08_stored_low_ = 0x10`, emulator.cpp:189), bit 7 = paging_lock readback (port_7ffd(5) collapses to 1 at this composition). |
| 09  | 0x00   | 0x00   | all subfields 0 | `nextreg.cpp:248`. |
| 0A  | 0x01   | 0x01   | mouse_dpi="01" (zxnext.vhd:1128) | Other bits 0. |
| 0B  | 0x01   | 0x01   | iomode_0=1 at reset (zxnext.vhd:4939-4941) | |
| 0E  | 0x03   | 0x03   | g_sub_version = 0x03 | RO, board generic. |
| 0F  | 0x00   | 0x00   | g_board_issue | RO. |
| 10  | 0x04   | 0x04   | nr_10_coreid="00001" + buttons=00 → 0_00001_00 = 0x04 (zxnext.vhd:5924) | G56. |
| 11  | 0x03   | 0x03   | nr_11_video_timing = g_video_def | jnext seeds 0x03; Issue 5 top-level sets g_video_def="000" (`zxnext_top_issue5.vhd:36`). **Minor jnext vs Issue-5 deviation** — kept as pre-existing per `nextreg.cpp:39-40` "typically 0x03 for issue-2". Not in Task-18 fix scope. |
| 12  | 0x08   | 0x08   | nr_12_layer2_active_bank="0001000"=8 (zxnext.vhd:4943) | Authoritative `Layer2::active_bank_=8`. |
| 13  | 0x00   | **0x0B** | nr_13_layer2_shadow_bank="0001011"=11 (zxnext.vhd:4944) | cached=0 because the constructor does not seed it; read handler returns `Layer2::shadow_bank_=11`. VHDL-correct via the live path. |
| 14  | 0xE3   | 0xE3   | global_transparent_rgb=0xE3 (zxnext.vhd:4946) | `nextreg.cpp:285`. |
| 22  | 0x00   | 0x00   | line-int enable=0, ULA-int-disable=0 | `nextreg.cpp:fill(0)` + emulator.cpp:229-232. |
| 23  | 0x00   | 0x00   | line_int_target low byte = 0 | |
| 40  | 0x00   | 0x00   | palette_index=0 | |
| 41  | 0x00   | 0x00   | n/a (palette write port) | |
| 42  | 0x07   | 0x07   | nr_42_ulanext_format=0x07 (zxnext.vhd:5002) | |
| 43  | 0x00   | 0x00   | all subfields 0 | |
| 44  | 0x00   | 0x00   | n/a | |
| 4A  | 0xE3   | 0xE3   | fallback_rgb=0xE3 (zxnext.vhd:5014) | |
| 4B  | 0xE3   | 0xE3   | sprite_transparent_index=0xE3 (zxnext.vhd:5016) | |
| 4C  | 0x0F   | 0x0F   | tm_transparent_index=0xF (zxnext.vhd:5018) | |
| 50  | 0xFF   | 0xFF   | MMU0 sentinel for legacy ROM (zxnext.vhd:4611) | Slot 0 = ROM. |
| 51  | 0xFF   | 0xFF   | MMU1 sentinel for legacy ROM (zxnext.vhd:4612) | Slot 1 = ROM. |
| 52  | 0x0A   | 0x0A   | MMU2 = bank 5 lo = page 0x0A (zxnext.vhd:4613) | |
| 53  | 0x0B   | 0x0B   | MMU3 = bank 5 hi = page 0x0B | |
| 54  | 0x04   | 0x04   | MMU4 = bank 2 lo = page 0x04 | |
| 55  | 0x05   | 0x05   | MMU5 = bank 2 hi = page 0x05 | |
| 56  | 0x00   | 0x00   | MMU6 = bank 0 lo = page 0x00 (zxnext.vhd:4617) | |
| 57  | 0x01   | 0x01   | MMU7 = bank 0 hi = page 0x01 (zxnext.vhd:4618) | |
| 69  | 0x00   | 0x00   | port_ff + L2-enable mirror = 0 | |
| 6B  | 0x00   | 0x00   | tilemap control = 0 | |
| 7F  | 0xFF   | 0xFF   | nr_7f_user_register_0=0xFF (zxnext.vhd:1216) | |
| 80  | 0x00   | 0x00   | nr_80_expbus | Lo-nibble all-zero → hi-nibble copy = 0x00 (reset rule, zxnext.vhd:2186). |
| 81  | 0x00   | **0x80** | read mux zxnext.vhd:6126 | bit 7 = `i_BUS_ROMCS_n` (idle/pulled-up = 1); all other bits 0. |
| 82  | 0xFF   | 0xFF   | nr_82=0xFF (zxnext.vhd:1226 + reset_type=1 path) | |
| 83  | 0xFF   | 0xFF   | nr_83=0xFF | |
| 84  | 0xFF   | 0xFF   | nr_84=0xFF | |
| 85  | 0x8F   | 0x8F   | reset_type=1 \| enable[3:0]=1111 = 0x8F | |
| 86  | 0xFF   | 0xFF   | nr_86=0xFF | |
| 87  | 0xFF   | 0xFF   | nr_87=0xFF | |
| 88  | 0xFF   | 0xFF   | nr_88=0xFF | |
| 89  | 0x8F   | 0x8F   | bus_reset_type=1 \| enable[3:0]=1111 = 0x8F | |
| 8A  | 0x00   | 0x00   | nr_8a_bus_port_propagate=0 (zxnext.vhd:1236) | |
| 8C  | 0x00   | 0x00   | nr_8c_altrom — lo→hi copy of 0 = 0 | |
| 8E  | 0x00   | **0x08** | read mux fixes bit 3 = '1' (zxnext.vhd:6158) | |
| 8F  | 0x00   | 0x00   | nr_8f_mapping_mode="00" (zxnext.vhd:1102's signal default) | |
| A0  | 0x00   | 0x00   | nr_a0_pi_peripheral_en=0 (zxnext.vhd:5080) | |
| C0  | 0x00   | 0x00   | nr_c0_im2_vector=0, stackless_nmi=0, im2_mode=0 (zxnext.vhd:5092-5094) | |
| C4  | 0x00   | **0x81** | read mux zxnext.vhd:6239 | bit 7 = nr_c4_int_en_0_expbus=1 (zxnext.vhd:5096), bit 0 = ula_int_en(0)=NOT port_ff_reg(6)=1. |
| C5  | 0x00   | 0x00   | nr_c5_int_en_1 = 0 (CTC channels disabled) | |
| C6  | 0x00   | 0x00   | nr_c6_int_en_2 = 0 (UART INTs disabled) | |
| C8  | 0x00   | 0x00   | nr_c8_int_status_0 = 0 | |
| D8  | 0x00   | 0x00   | nr_d8_io_trap_fdc_en='0' (zxnext.vhd:5107) | |
| D9  | 0x00   | 0x00   | nr_d9_iotrap_write=0 (zxnext.vhd:3891) | |
| DA  | 0x00   | 0x00   | nr_da_iotrap_cause=0 (zxnext.vhd:3870) | |

Six cached/read divergences (NR 0x03, 0x08, 0x13, 0x81, 0x8E, 0xC4)
are all expected — they correspond to read handlers that compose
output from authoritative subsystem state rather than the cache. A
firmware-bypass build that wants to **observe** the post-reset state
the way the Z80 sees it must use the `read()` column, not `cached`.

## 2. NextReg pointer / config-mode state

| Field | Value | VHDL |
|-------|-------|------|
| `selected` (`nr_register`) | 0x24 | zxnext.vhd:4594-4596 |
| `nr_03_config_mode` | **1** | zxnext.vhd:1102 |
| `nr_03_machine_type` | 0x04 (ZXN_ISSUE2 → "100") | zxnext.vhd:1103 (default 011; jnext pushes 100 in init() per the CLI machine type, emulator.cpp:344-351) |
| `nr_03_machine_timing` | 0x03 (+3 timing) | zxnext.vhd:1099 |
| `nr_03_user_dt_lock` | 0 | zxnext.vhd:1100 |
| `nr_04_romram_bank` | 0x00 | zxnext.vhd:1104 |

## 3. MMU slot map

Each row shows: NR-visible page (`get_page(slot)` = `nr_mmu_[slot]`),
the effective backing page after the SRAM arbiter's 0xFF-sentinel
resolution, whether the slot is in ROM mode, and whether
`slot_in_rom_area()` reports `page >= 0xE0`.

| Slot | Range          | nr_mmu | effective | read_only | VHDL |
|------|----------------|--------|-----------|-----------|------|
| 0    | 0x0000-0x1FFF  | 0xFF   | 0x00      | 1 (ROM)   | sentinel → sram_rom*2+0; sram_rom=0 (boot/cfg) → page 0x00. zxnext.vhd:4611, 3052. |
| 1    | 0x2000-0x3FFF  | 0xFF   | 0x01      | 1 (ROM)   | sram_rom*2+1 = page 0x01. zxnext.vhd:4612, 3052. |
| 2    | 0x4000-0x5FFF  | 0x0A   | 0x0A      | 0 (RAM)   | bank 5 lo. zxnext.vhd:4613. |
| 3    | 0x6000-0x7FFF  | 0x0B   | 0x0B      | 0 (RAM)   | bank 5 hi. zxnext.vhd:4614. |
| 4    | 0x8000-0x9FFF  | 0x04   | 0x04      | 0 (RAM)   | bank 2 lo. zxnext.vhd:4615. |
| 5    | 0xA000-0xBFFF  | 0x05   | 0x05      | 0 (RAM)   | bank 2 hi. zxnext.vhd:4616. |
| 6    | 0xC000-0xDFFF  | 0x00   | 0x00      | 0 (RAM)   | bank 0 lo. zxnext.vhd:4617. |
| 7    | 0xE000-0xFFFF  | 0x01   | 0x01      | 0 (RAM)   | bank 0 hi. zxnext.vhd:4618. |

| MMU flag | Value | Note |
|----------|-------|------|
| `boot_rom_enabled` | **1** | nextboot.rom 8 KB overlay active at 0x0000-0x1FFF (priority 0 in zxnext.vhd:2937-2945). Pre-empts the slot-0 ROM mapping above. |
| `rom_in_sram` | **1** | Next mode — ROM slots read from SRAM pages 0..7 (zxnext.vhd:3052 `sram_rom & cpu_a(13)`). |
| `paging_locked` | 0 | port_7ffd(5)=0. |
| `port_7FFD` | 0x00 | zxnext.vhd:3646-3648. |
| `port_1FFD` | 0x00 | zxnext.vhd:3713-3715. |

## 4. Z80 register defaults

| Reg | Value | VHDL/FUSE source |
|-----|-------|------------------|
| PC | 0x0000 | `fuse_z80_reset()` hard branch |
| SP | 0xFFFF | same |
| AF, AF' | 0xFFFF, 0xFFFF | same |
| BC, DE, HL | 0x0000 each | hard-reset branch |
| BC', DE', HL' | 0x0000 each | hard-reset branch |
| IX, IY | 0x0000 each | hard-reset branch |
| MEMPTR (WZ) | 0x0000 | hard-reset branch |
| I, R | 0x00, 0x00 | always cleared |
| IFF1, IFF2 | 0, 0 | always cleared |
| IM | 0 | always cleared |
| halted | 0 | always cleared |

(Source: `third_party/fuse-z80/fuse_z80_core.c:95-114`.)

## 5. Memory layout at cold reset

`Ram::reset()` (`ram.cpp:28`) fills the whole SRAM with 0x00 before
`init()` populates select pages from the host-side SD-card reader.
After `init()` returns, the following SRAM pages hold non-zero
content:

| Page | Source | First byte | Notes |
|------|--------|------------|-------|
| 0x00 | `48.rom` bank 0 lo  | 0xF3 (DI) | Seeded via `rom_.load_bytes()` then copied to `ram_.page_ptr(p)` for p=0..7 (emulator.cpp:5209-5214). Real 48 BASIC entry: DI / XOR A / LD DE,$FFFF / JP $11CB. |
| 0x01 | `48.rom` bank 0 hi  | 0x0D | continuation of 48 BASIC. |
| 0x02-0x07 | rom_ banks 1..3 unloaded | 0xFF | The "Next 48K fallback" loader only loads `48.rom` (16 KB = 1 ROM bank = pages 0/1); pages 2..7 are the 0xFF-init `Rom` default which gets copied verbatim into SRAM. Real Next hardware would have 128K/+3/Pentagon ROMs here; in jnext at init the slots are still 0xFF placeholders until `tbblue.fw load_roms()` runs. |
| 0x0C-0x0F | `enAltZX.rom` (32 KB) | varies | AltROM, pre-loaded by host so the first NextZXOS AltROM trampoline at $007B can resolve (emulator.cpp:5350-5370). Per VHDL these pages back `nr_8c_altrom_en=1` reads (zxnext.vhd:2981-3001). |

Pages 0x10-0x1F are reserved for DivMMC SRAM (separate physical
region in VHDL; in jnext modelled by `DivMmc::ram_`, NOT by SRAM
pages). DivMMC overlay state at init:

- `DivMmc::is_enabled()` = **false** — port_io_enable_/nr_0a_4_enable_
  both default to 0 at construction; firmware turns DivMMC on via
  NR 0x83 bit 0 / NR 0x0A bit 4. (The "DivMMC enabled" log line
  printed earlier in init() is the ROM-load-success indicator; the
  `enabled_` flag is independent and starts low. See divmmc.cpp:31-51
  plus the `set_enabled(true)` call gated on the NR-write path.)
- `divmmc_.rom_data()[0]` = 0xF3 — `enNxtmmc.rom` (DivMMC firmware)
  loaded from SD by `extract_sd_rom()`.

Multiface ROM at `multiface_.rom_data()[0]` = 0xF5 — `enNextMf.rom`
loaded from SD. `multiface_.is_enabled()` = false at init (the
`enabled_` flag is driven by NR 0x83 bit 1, fanned out by the
NR 0x83 write handler; cold-reset NR 0x83 cache is 0xFF so b1=1,
but the constructor explicitly starts `enabled_=false` and the
firmware drives the first write through `propagate_effective_port_enables()`).

The boot ROM (`nextboot.rom`, 8 KB) is held in `Emulator::boot_rom_`
(separate vector, not in `ram_`); served via the `Mmu::boot_rom_`
pointer at priority 0 of the SRAM arbiter cascade while
`boot_rom_en_=1`.

## 6. What the Z80 sees at PC=0x0000

With `boot_rom_enabled=1`, the very first CPU fetch resolves to byte
0 of the embedded `nextboot.rom` blob (priority 0 in the SRAM
arbiter, masks the slot-0 SRAM page). Per the EOD-30i+14 finding
(see `MEMORY.md` index entry), jnext's embedded `nextboot.rom` is a
historically different binary from CSpect's boot ROM — first 4 bytes
`F3 ED 56 C3` (DI / IM 1 / JP $0080). That divergence is OUT OF
SCOPE for Task 18 (firmware bypass) but worth noting: a faked-firmware
build that skips `nextboot.rom` entirely sidesteps this divergence
by definition.

## 7. Single-line summary for cross-checking

- (a) **NR 0x03 cold-reset value**: cached=0x00, read=**0x34** (composed: tim=011, type=100, lock=0). `nr_03_config_mode` = **1** (config mode ON).
- (b) **NR 0x06 cold-reset value**: cached=0xA0, read=0xA0 (bits 7/5 forced high by VHDL reset; all others zero).
- (c) **MMU slot 0** maps to physical page **0x00** via the `0xFF` sentinel → sram_rom(=0) × 2 + 0 path; but the actual byte served is masked by the **boot-ROM overlay** (`boot_rom_enabled=1`), so the Z80 reads from `nextboot.rom`, not from SRAM page 0x00.
- (d) **`nr_03_config_mode` after init() = true** (preserved across reset per VHDL; latched at FPGA power-on per zxnext.vhd:1102).
- (e) **Most surprising finding**: six NRs (0x03, 0x08, 0x13, 0x81, 0x8E, 0xC4) have a `cached` byte of `0x00` but a `read()` value that is the **VHDL-correct non-zero composed byte**. A bypass-firmware build that pokes `cached()` to override these would still observe the live read mux output — the only way to "set" them VHDL-faithfully is via NR writes that fan out to the authoritative subsystem state.
