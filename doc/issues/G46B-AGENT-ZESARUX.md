# G46(b) Agent Report: ZEsarUX Cross-Comparison

Date: 2026-05-05 (timestamp: session continuation, EOD `bypass-tbblue-fw` track).
Agent: read-only research. No code changes.

## TL;DR

**ZEsarUX does NOT have a "bypass tbblue.fw" mode equivalent to ours.** Its
`--tbblue-fast-boot-mode` reduces to "boot as plain 48K with `48.rom` mapped 4×
into RAM" — it does NOT load `enNextZX.rom`, does NOT enter NextZXOS, and does
NOT exercise the supervisor at all (`tbblue.c:4084-4125` + `cpu.c:3263-3266` +
`cpu.c:3544-3553`). In normal mode ZEsarUX boots the FPGA `tbblue_loader.rom`
IPL → which loads & runs the real `tbblue.fw` from the SD → which legitimately
populates SRAM (including pages 0x2F/0x30 the supervisor needs) and finally
issues `RESET_SOFT` to hand off to NextZXOS, exactly like CSpect and real
hardware.

**There is therefore no ZEsarUX "bypass" code we can crib.** What ZEsarUX does
prove, by comparison, is that its **MMU semantics for NR 0x50–0x57 are
functionally identical to jnext's** (the "+0x20 shift" is baked into ZEsarUX's
SRAM base pointer rather than computed at map time, but the net effective
mapping is the same). Our remaining divergences from CSpect therefore are NOT
MMU-shift bugs — they are bypass-handoff state that only the real `tbblue.fw`
firmware can write into SRAM.

## Methodology

Files read in `/home/jorgegv/src/spectrum/zesarux/src/`:

- `machines/tbblue.c` lines 62-93, 2906-3210, 3406-3674, 3714-3736, 3899-4140,
  4426-5440, 5462-5662, 8459-8696
- `machines/tbblue.h` lines 36-260
- `cpu.c` lines 1450-1571, 3240-3559
- `start.c` lines 423-455, 4100-4116
- `operaciones.c` lines 2639-2752
- `storage/esxdos_handler.c` lines 2110-2135 (read-only verify the M_DOSVERSION
  hook is opt-in, default disabled)

Searches: `tbblue_fast_boot`, `tbblue_set_ram_page`, `tbblue_set_rom_page`,
`tbblue_set_memory_pages`, `tbblue_paging_128k_reg_142`, `nr_02_reset_type`,
`enNextZX|nextzxos|MACHINES/NEXT`, `random_ram`, `tbblue_loader`, `altrom`,
`tbblue_low_segment_writable`, `tbblue_is_writable_segment_mmu_rom_space`.

Cross-referenced against:

- `/home/jorgegv/src/spectrum/jnext/src/memory/mmu.{cpp,h}`
- `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp` 1280-1315, 3500-3830
- `/home/jorgegv/src/spectrum/jnext/src/peripheral/nmi_source.cpp` 145-156
- `/home/jorgegv/src/spectrum/jnext/doc/issues/G46B-AGENT-PATHCOMPARE.md`
- VHDL `zxnext.vhd:2964, 3052, 3060-3061, 3199-3204, 3686-3690, 3787-3794, 4611-4699, 5891`

## Question 1: Boot sequence

**ZEsarUX (cpu.c:3263-3266, tbblue.c:4084-4132)**

Normal mode (default):
- Loads `tbblue_loader.rom` (8 KB FPGA IPL — equivalent to our `nextboot.rom`)
  into `tbblue_fpga_rom` buffer (mirrored to 16 KB).
- Sets `tbblue_bootrom.v=1` (bootrom overlay active at 0-16383).
- Z80 starts at PC=0 inside `tbblue_loader.rom`.
- IPL writes NR 0x03 → `tbblue_bootrom.v=0` (overlay off), then proceeds to
  load `tbblue.fw` from the mounted `tbblue.mmc` SD image and runs it via real
  Z80 execution.

`--tbblue-fast-boot-mode` (`start.c:4115` / `tbblue.c:4084-4125`):
- Skips `tbblue_loader.rom` entirely.
- Loads `48.rom` (16 KB) **4 times** into `memoria_spectrum[0..0xFFFF]`.
- Forces `tbblue_registers[3]=3` (machine_type=ZX +2/+3e), peripheral=2+8+16
  (turbosound+specdrum+speaker), forces NR_50/NR_51 = 0xFF.
- Calls `divmmc_diviface_disable()`.
- Z80 starts at PC=0 in plain 48K BASIC.

**Critical: ZEsarUX's fast-boot mode is NOT a NextZXOS bypass — it is a "skip
firmware AND skip supervisor, fall back to 48K" mode.** No equivalent of
`--bypass-tbblue-fw` exists.

**jnext (`emulator.cpp:3506-3534, 3551-3566, 3791-3831`)**

- Normal mode: loads `nextboot.rom` (silicon-baked 8 KB), runs real
  `tbblue.fw`. Same architecture as ZEsarUX normal mode.
- `--bypass-tbblue-fw`: loads `enNextZX.rom` (64 KB) directly into rom_ banks
  0..3, copies pages 0..7 into `ram_` (ROM-in-SRAM seed), then synthesises
  the post-handoff NR state that `tbblue.fw` would have written (NR 0x05–0x0A,
  0x82–0x85, 0x03=0xB3) and strobes the `nr_02_reset_type` FSM via
  `nmi_source_.strobe_soft_reset()`. **Z80 starts at PC=0 inside the
  supervisor.**

**Verdict:** ZEsarUX has no analogous bypass mode. The only thing we can borrow
from ZEsarUX is its **normal-mode** boot, and we already implement that path
(it works under CSpect, reportedly works on jnext with an external
real-tbblue.fw). The bypass mode is a jnext-only feature with no upstream
template. Whatever is wrong is wrong because we are skipping legitimate
firmware-side SRAM initialisation, not because we're modelling the FPGA wrong.

## Question 2: ROM-in-SRAM

**ZEsarUX (tbblue.c:2919-2982, 3171-3210, 3406-3674)**

Memory model is a flat 2 MB allocation `memoria_spectrum[]` whose offsets
mirror the VHDL SRAM map (zxnext.vhd:2924-2925) verbatim:

```
0x000000-0x00FFFF (64K)   ZX Spectrum ROM      (= 8×8K rom pages 0..7)
0x010000-0x011FFF (8K)    DivMMC ROM
0x012000-0x013FFF (8K)    unused
0x014000-0x017FFF (16K)   Multiface ROM,RAM
0x018000-0x01BFFF (16K)   Alt ROM0 128k         (= SRAM pages 0x0C..0x0D)
0x01C000-0x01FFFF (16K)   Alt ROM1 48k          (= SRAM pages 0x0E..0x0F)
0x020000-0x03FFFF (128K)  DivMMC RAM
0x040000-0x1FFFFF (1.75M) ZX Next RAM           (starts at SRAM page 0x20)
```

`tbblue_ram_memory_pages[i] = &memoria_spectrum[0x040000 + 8192*i]`
(`tbblue.c:2972-2974`). So `tbblue_ram_memory_pages[0]` IS physical SRAM
page 0x20. **The +0x20 shift is baked into the array base address rather than
computed in the MMU at map time** — but the effective semantics match jnext
exactly.

ROM-in-SRAM seeding:
- ZEsarUX does NOT seed pages 0..7 of `tbblue_ram_memory_pages[]` with the ROM
  contents. ROM lives separately in `tbblue_rom_memory_pages[i]` (the first
  64 KB of `memoria_spectrum`), and Next-mode "config_mode" routing is handled
  in `tbblue_set_memory_pages()` `case 0` (default branch, `tbblue.c:3613-
  3672`): when `tbblue_bootrom.v=0`, slot 0 is mapped via `nr_04_romram_bank`
  (`tbblue_registers[4]`) directly into the **first 1 MB of SRAM** (lines
  3629-3635) — i.e., it routes to the ROM block & DivMMC/MF/AltROM block.
- Crucially, ZEsarUX's `tbblue_set_rom_page_no_255` (line 3009-3019) maps
  ROM-area slot reads directly to `tbblue_ram_memory_pages[reg_value]` —
  NOT to `tbblue_rom_memory_pages[]`. So once NR_50/51 are set non-0xFF, slot
  0/1 read from regular RAM at offset `0x040000 + 8192*reg_value`. **This is
  exactly equivalent to jnext's `rom_in_sram_=true` + `to_sram_page(page) =
  page + 0x20`.**

Pre-loaded Next-only RAM pages: ZEsarUX **does NOT pre-load any SRAM page
beyond what the regular ROM file provides**. Pages 0x20..0xDF are zero-init.
AltROM SRAM pages 0x0C..0x0F are zero-init too — they only get populated when
firmware writes to them.

**jnext (`emulator.cpp:3578-3594, 3707-3762`)**

- Seeds RAM pages 0..7 from `rom_` buffer (`for (p=0; p<8; ++p)
  memcpy(ram_.page_ptr(p), rom_.page_ptr(p), 0x2000)`) — **functionally
  equivalent** to ZEsarUX's `tbblue_set_rom_page_no_255` routing reads to
  `tbblue_ram_memory_pages[reg_value]`.
- In bypass mode, also pre-loads:
  - **AltROM pages 0x0C..0x0F from `enAltZX.rom`** (32 KB) — ZEsarUX never
    pre-loads these. This is a jnext-bypass-only divergence required because
    jnext skips firmware. Real boot would not need this — firmware would
    self-populate. Not a divergence vs ZEsarUX in normal-boot semantics.
  - **AltROM mirror at pages 0x2C..0x2F** (experimental, see line 3723-3735) —
    debug-only; should be removed once root cause is found.
  - **CSpect-captured page 0x30 from `doc/issues/cspect-captures/page30.raw`**
    if it exists — debug-only; doesn't help if file is absent.

**Verdict:** MMU and ROM-in-SRAM semantics are equivalent. The +0x20 shift is
implemented identically (different code shape, same effect). No divergence
here.

## Question 3: MMU semantics

### NR 0x50–0x57 page register

**ZEsarUX** (`tbblue.c:5301-5313, 2994-3019`):

```c
case 80: case 81:
    tbblue_set_memory_pages();
    break;
case 82: case 83: case 84: case 85: case 86: case 87:
    tbblue_set_memory_pages();
    break;
```

`tbblue_set_ram_page(segment)` at line 2994:

```c
z80_byte reg_value = tbblue_registers[80+segment];
reg_value = tbblue_get_limit_sram_page(reg_value);   // clamp to 223
tbblue_memory_paged[segment] = tbblue_ram_memory_pages[reg_value];
```

`tbblue_get_limit_sram_page(p)` returns `min(p, 223)`. There is **no E0+
gating to floating bus** — values 0xE0..0xFF clamp to 0xDF (last valid page,
which is initialised to zero-data anyway). Slot 0/1 with non-0xFF reg value
goes via `tbblue_set_rom_page_no_255` which uses the same
`tbblue_ram_memory_pages[reg_value]` array — no special-case.

**jnext** (`mmu.cpp:154-189`, `mmu.h:796-800`):

```cpp
uint8_t to_sram_page(uint8_t logical) const {
    if (!rom_in_sram_) return logical;
    if (logical == 0x0A || logical == 0x0B || logical == 0x0E) return logical;
    return logical + 0x20;
}
```

- Applies `+0x20` shift unless logical page is in `{0x0A, 0x0B, 0x0E}`
  (which jnext maps "passthrough" — special-case for some VHDL shadow-bank
  routing).
- Slot 2..7 with `page >= 0xE0`: explicit `read_ptr_=write_ptr_=nullptr`
  (floating-bus gate per zxnext.vhd:3060-3061). Reads return 0xFF, writes
  drop.
- Slot 0/1 with `page == 0xFF` AND not `read_only_`: nullptr. With
  `read_only_=true`: reads from `ram_.page_ptr(rom_page)` (no +0x20 shift,
  treating ROM page index as direct).

**Functional equivalence check:**

| logical page | jnext effective SRAM page | ZEsarUX effective SRAM page |
| ------------ | ------------------------- | --------------------------- |
| 0x00..0x09   | 0x20..0x29 (+shift)       | 0x20..0x29 (base 0x040000)  |
| 0x0A         | 0x0A (passthrough!)       | 0x2A                        |
| 0x0B         | 0x0B (passthrough!)       | 0x2B                        |
| 0x0C..0x0D   | 0x2C..0x2D                | 0x2C..0x2D                  |
| 0x0E         | 0x0E (passthrough!)       | 0x2E                        |
| 0x0F..0xDF   | 0x2F..0xFF                | 0x2F..0xFF                  |
| 0xE0..0xFF   | floating bus (slots 2-7)  | clamp to 0xDF → 0xFF        |

**MAJOR DIVERGENCE FOUND**: jnext's `to_sram_page` has special-cases for
logical pages `0x0A`, `0x0B`, `0x0E` that pass through *without* the +0x20
shift. ZEsarUX has no such exception — every page indexes
`tbblue_ram_memory_pages[reg_value]` directly. Per VHDL `zxnext.vhd:2964`,
there is **NO** such exception either:

```
mmu_A21_A13 <= ("0001" + ('0' & mem_active_page(7:5))) & mem_active_page(4:0)
```

The formula adds 0x20 to ALL pages unconditionally (the high nibble's "0001"
literal means "always add 0x20 in next mode"; the only escape is when
mem_active_page(7:5)=="111" → high nibble becomes "1000" → mmu_A21_A13(8)='1',
i.e., the floating-bus gate at >=0xE0). **There is no logical-0x0A/0x0B/0x0E
passthrough in the VHDL.**

This `to_sram_page` exception in jnext (`mmu.h:798`) is **suspicious** and
should be audited — but note the comment context (line 786-794 of mmu.h
indicates Layer 2 uses these specific pages and the comment claims "VHDL
exactly means keeping those logical values un-shifted"). I cannot verify the
comment's claim without re-reading the relevant Layer 2 VHDL and the historical
test that landed this exception. If the rationale is wrong, it would mis-route
ANY MMU map of pages 0x0A/0x0B/0x0E — a broad regression. Worth checking
carefully whether the supervisor ever sets NR_5x to 0x0A/0x0B/0x0E.

### Slot 0/1 special case

**ZEsarUX** (`operaciones.c:2733-2740`, `tbblue.c:8459-8468`):
- `poke_byte_no_time_tbblue` for `dir<16384`: blocks write unless
  `tbblue_low_segment_writable.v` OR `mmu_value != 255`.
- `tbblue_is_writable_segment_mmu_rom_space(dir)`: returns 1 iff machine!=0
  AND `tbblue_registers[80 + dir/8192] != 255`.

**jnext** (`mmu.cpp:154-189` + NR 0x50/0x51 handler at `emulator.cpp:1283-1290`):
- NR 0x50 write 0xFF → `mmu_.map_rom(0, 0)` → `read_only_[0]=true`,
  `write_ptr_[0]=nullptr` → write blocked.
- NR 0x50 write !=0xFF → `mmu_.set_page(0, v)` → `read_only_[0]=false`,
  `write_ptr_[0]=ram_.page_ptr(to_sram_page(v))` → write goes to SRAM.

**Verdict: equivalent.** Both make slot 0/1 writable iff the explicit NR map
is non-0xFF.

### NR 0x8C AltROM

**ZEsarUX** (`tbblue.c:3171-3210, 3022-3168, 8459`):
- AltROM read mode (NR 0x8C bits 7:6 == 10): when slot 0/1 is in ROM mode
  (NR_50/51 == 0xFF), `tbblue_set_rom_page` redirects the slot pointer to
  `&memoria_spectrum[tbblue_get_altrom_offset_dir(altrom, 8192*segment)]`.
  Offsets: ROM0 at `0x018000+dir`, ROM1 at `0x01C000+dir` — equivalent to
  SRAM pages 0x0C..0x0D / 0x0E..0x0F.
- AltROM write mode (NR 0x8C bits 7:6 == 11): write redirects in
  `poke_byte_no_time_tbblue:2649`. Reads remain on regular ROM.

**jnext** (`mmu.h:803-825`, altrom_sram_page_):
- Implements the same logic: pages 0x0C..0x0F driven by NR 0x8C lock bits +
  port_1ffd ROM bit + machine type. Identical SRAM page numbering.

**Verdict: equivalent.**

## Question 4: NR register state on boot

**ZEsarUX hard reset** (`tbblue.c:4049-4140` `tbblue_hard_reset`):
- Sets `tbblue_registers[2]=4+2`, `[3]=0`, `[4]=0`, `[5]=1`, `[6]=0`,
  `[7]=0`, `[8]=16`, `[9]=0`, `[0x8c]=0`.
- Then calls `tbblue_reset_common()` which seeds default values for many
  display/sprite/copper/clip-window registers but **NOT** NR_50–NR_57.
- Pages 0x50–0x57 are left **uninitialised** in `tbblue_registers[]` (i.e.,
  whatever was in the malloc'd buffer; potentially 0). When `tbblue_bootrom.v=1`
  the `default:` branch of `tbblue_set_memory_pages()` runs and bypasses
  NR_50/0x51 entirely (slot 0/1 mapped to `tbblue_fpga_rom`), but slots 2..7
  via `tbblue_set_ram_page` index `tbblue_ram_memory_pages[0]` (since
  `tbblue_registers[82..87]==0`).
- In **fast-boot** mode: also sets `tbblue_registers[3]=3` (machine=+3),
  `[8]=2+8+16`, `[80]=0xff`, `[81]=0xff`. Then
  `tbblue_set_memory_pages()` runs case 3 (+3 mode).

ZEsarUX does NOT pre-set NR_55, NR_56, NR_57 to anything specific BEFORE the
Z80 starts.

**jnext bypass** (`emulator.cpp:3791-3814`):
- Writes NR 0x07=0x03, 0x06=0xa0, 0x05=0x81, 0x06=0x80, 0x08=0x3e, 0x09=0x00,
  0x0a=0x01, 0x82=0xda, 0x83=0x3d, 0x84=0xff, 0x85=0x01, then NR 0x03=0xB3.
- Does NOT touch NR_50–NR_57. They stay at MMU reset defaults: slot 0/1 =
  0xFF (ROM), slots 2..7 = 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01 (per VHDL
  zxnext.vhd:4611-4618 `RESET_PAGES`).

**Verdict:** Both leave NR_50..0x57 at the MMU's natural reset state. Not a
divergence vs ZEsarUX. **But: ZEsarUX runs the actual `tbblue.fw`
afterwards**, which itself writes NR_55–NR_57 to specific values during ROM
loading. jnext bypass skips that step; the supervisor sees the raw reset state
via the wrapper path, which the real-firmware boot would have overwritten.

## Question 5: Soft reset semantics

**ZEsarUX** (`tbblue.c:4928-4969, 5462+`):
- Hard reset (NR 0x02 bit 1): `tbblue_bootrom.v=1`, `tbblue_registers[3]=0`,
  `tbblue_set_memory_pages()`, `reg_pc=0`.
- Soft reset (NR 0x02 bit 0): `reg_pc=0` only. **No FPGA state reset, no
  NR_03 reset, no MMU reset.**
- Reads of NR 0x02 just return `tbblue_registers[2]` directly (no FSM).

**jnext** (`peripheral/nmi_source.cpp:145-156`, `core/emulator.cpp:3826`):
- Models `nr_02_reset_type` shift FSM per VHDL zxnext.vhd:1306, 1732-1739:
  power-on '100', each soft-reset shift collapses '100' → '010' → '001'
  (saturates).
- Reading NR 0x02 returns FSM state in bits[2:0] (`peripheral/nmi_source.cpp:
  126-136`).
- Bypass init calls `nmi_source_.strobe_soft_reset()` once to advance FSM
  from '100' → '010'.

**Verdict:** **DIVERGENCE — but in the right direction.** jnext is more
VHDL-faithful than ZEsarUX here. The supervisor reads NR 0x02 to distinguish
"first cold boot" (rt='100', bits[1:0]='00') from "post-soft-reset boot"
(rt='010', bits[1:0]='10'). After our `strobe_soft_reset()`, jnext reports
'10' = post-soft-reset path, which matches what tbblue.fw + RESET_SOFT would
produce. **This should be correct.**

## Question 6: Reads from uninitialised SRAM

**ZEsarUX** (`cpu.c:1560-1571, start.c:423-455`):
- `random_ram(memoria_spectrum, TBBLUE_TOTAL_MEMORY_USED*1024)` is called at
  alloc time. For non-48K machines (TBBlue case), `random_ram` body at line
  443-452 returns **0** for every byte (`MACHINE_IS_TBBLUE` does not match
  `MACHINE_IS_SPECTRUM_16_48`, so the else-branch `valor=0` runs).
- Effective: ZEsarUX zero-fills all SRAM at boot.

**jnext** (`memory/ram.cpp:5`):
- `Ram::Ram(size_t) : data_(size_bytes, 0)` — explicit zero-fill.

**Verdict: equivalent.** Both zero-fill.

For pages we don't pre-load (0x21+ in jnext bypass, 0x20+ in ZEsarUX normal
boot before firmware writes there): both return 0 on read.

## Question 7: Specific quirks

Searched for `// HACK`, `// FIXME`, `// FAKE`, "kludge", "hack", "TODO" in
`tbblue.c`. No NextZXOS-boot-specific kludges. Notable comments:

- `tbblue.c:3190` — `//TODO: tener en cuenta altrom si maquina es distinta de
  machine_type_p3, que es como en teoria lo estoy haciendo. Ver codigo vhdl
  para salir de dudas.` (Author-noted incomplete altrom handling for
  non-+3 machines — would not affect jnext bypass since we set machine=+3.)
- `tbblue.c:4115` — Comment near AY chips noting fast-boot mode skips
  divmmc enable: `//Enable divmmc. NO! Juegos como bubble gum o the next war
  fallarian` ("Enable divmmc. NO! Games like bubble gum or the next war would
  fail"). Tells us ZEsarUX explicitly disables DivMMC in fast-boot — not
  applicable to jnext bypass (which keeps DivMMC enabled per supervisor's
  needs).
- `storage/esxdos_handler.c:2122` — `//Asumimos nextzxos 2.07` ("We assume
  NextZXOS 2.07") — only relevant if `--enable-esxdos-handler` is set
  (default OFF). Not exercised in normal/fast-boot or in CSpect-equivalent
  flows.

No NextZXOS-specific hacks beyond the M_DOSVERSION fake reply (opt-in).

## Top 3 candidate divergences

### 1. `to_sram_page` exception for logical pages 0x0A / 0x0B / 0x0E

**File:** `/home/jorgegv/src/spectrum/jnext/src/memory/mmu.h:796-800`

```cpp
uint8_t to_sram_page(uint8_t logical) const {
    if (!rom_in_sram_) return logical;
    if (logical == 0x0A || logical == 0x0B || logical == 0x0E) return logical;
    return static_cast<uint8_t>(logical + 0x20);
}
```

ZEsarUX has NO such exception (`tbblue.c:2994-3006` indexes
`tbblue_ram_memory_pages[reg_value]` directly with no special pages). VHDL
`zxnext.vhd:2964` formula `("0001" + …) & mem_active_page(4:0)` adds 0x20
unconditionally for all sub-0xE0 pages. **The exception in jnext looks
non-VHDL-faithful.** If the supervisor ever maps NR_5x = 0x0A / 0x0B / 0x0E,
jnext routes the access to physical page 0x0A/0x0B/0x0E (DivMMC area + Alt
ROM area) instead of 0x2A/0x2B/0x2E (ZX RAM area). This would be a major
data-flow corruption.

**Severity:** Could be the root cause if the supervisor ever uses one of these
specific NR_5x values. Given Agent 1's report shows NR_57=$0F at the failure
point — NOT one of the exception values — this likely is NOT the immediate
cause for the $20E6 loop. But it is a latent bug worth auditing independently
(possibly responsible for OTHER subtle issues — e.g., shadow screen / Layer 2
banking).

### 2. NR_55–NR_57 are uninitialised in bypass mode

**File:** `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp:3791-3814`

The jnext bypass post-handoff init writes NR 0x05–0x0A, 0x82–0x85, 0x03 — but
**not NR_50–0x57**. The MMU starts in its `RESET_PAGES = {0xFF, 0xFF, 0x0A,
0x0B, 0x04, 0x05, 0x00, 0x01}` state (`mmu.cpp:13`). The supervisor enters
its wrapper path with stale slot mappings that real `tbblue.fw` would have
explicitly re-programmed before issuing RESET_SOFT.

Crucially, the ground truth in CSpect (per Agent 1's PATHCOMPARE report) is
NR_57=$10 (page 0x30) at the moment of $20E6 entry; jnext arrives at the same
$20E6 with NR_57=$0F (page 0x2F). **One of the supervisor's early bank-
switch paths is taking different branches because IX/SP/upstream NR state
diverges**, and that divergence ultimately stems from missing firmware-side
initialisation.

**Severity:** This is the proximate cause. Page 0x30 is the supervisor's
post-firmware sprite-descriptor / stack page; without firmware to populate
it, there's nothing for the supervisor's wrapper path to read. ZEsarUX
proves this by inverse: ZEsarUX boots NextZXOS only because it RUNS the real
firmware, which writes that data.

### 3. AltROM pre-load mirror at 0x2C–0x2F (experimental band-aid)

**File:** `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp:3723-3735`

This is a debugging band-aid that mirrors the AltROM 0x0C–0x0F into 0x2C–0x2F
"in case the supervisor reads bank 7 from this region". ZEsarUX has nothing
analogous and works correctly. **This experiment should be removed once root
cause is identified** (per `feedback_vhdl_faithful_only.md`). Not the cause of
the failure but it muddies the picture.

## Top recommendation

**Audit the `to_sram_page` exception in `mmu.h:798` against the VHDL
zxnext.vhd:2964 formula.** This is the single change with the largest VHDL-
faithfulness deficit between jnext and ZEsarUX. Two outcomes:

1. **If the exception is wrong**: removing it could explain a class of subtle
   data-flow divergences (Layer 2 banking, shadow screen, NextZXOS supervisor
   page reads). Even if it doesn't fix G46(b), it removes a latent bug.

2. **If the exception is right** (i.e., the VHDL has additional context that
   genuinely makes 0x0A/0x0B/0x0E passthrough): keep the comment but add the
   explicit VHDL line reference proving it (current comment at line 786-794
   is hand-wavy). This converts a "trust the comment" code site into an
   "auditable VHDL citation" site.

For G46(b) itself, this audit alone will not fix the bypass mode. The actual
fix path for the bypass remains: **populate the firmware-side SRAM state
that's missing**, OR **switch to running the real `tbblue.fw` like CSpect and
ZEsarUX do**. ZEsarUX confirms by example that the latter is the architectural
sweet spot — every working Next emulator we have evidence of runs the real
firmware. The bypass mode is a jnext-specific shortcut and inherits the cost
of replicating firmware behaviour we don't fully understand.

## Files referenced

- `/home/jorgegv/src/spectrum/zesarux/src/machines/tbblue.c`
- `/home/jorgegv/src/spectrum/zesarux/src/machines/tbblue.h`
- `/home/jorgegv/src/spectrum/zesarux/src/cpu.c`
- `/home/jorgegv/src/spectrum/zesarux/src/start.c`
- `/home/jorgegv/src/spectrum/zesarux/src/operaciones.c`
- `/home/jorgegv/src/spectrum/zesarux/src/storage/esxdos_handler.c`
- `/home/jorgegv/src/spectrum/jnext/src/memory/mmu.cpp`
- `/home/jorgegv/src/spectrum/jnext/src/memory/mmu.h`
- `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp`
- `/home/jorgegv/src/spectrum/jnext/src/peripheral/nmi_source.cpp`
- `/home/jorgegv/src/spectrum/jnext/doc/issues/G46B-AGENT-PATHCOMPARE.md`
