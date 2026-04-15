# I/O Port Dispatch Compliance Test Plan

> **Plan Rebuilt 2026-04-14 — prior 78/78 PASS result RETRACTED.**
>
> The previous revision of this document claimed 78/78 passing tests against
> `PortDispatch`. A Task 4 critical review found that result to be coverage
> theatre and the plan is hereby rebuilt from the VHDL and from the libz80
> port API contract. Retracted because:
>
> 1. **Container-only testing.** Every prior test called
>    `make_clean_dispatch()` and then registered private stub handlers with
>    hand-picked `(mask, value)` pairs. It verified that
>    `std::vector<PortHandler>` linear search works — not that the real
>    peripherals (`Ay`, `NextReg`, `Mmu`, `Ula`, `DivMMC`, `Dma`, `Sprite`,
>    `Layer2`, `Spi`, `I2c`, `Uart`, `Ctc`, `Mouse`, …) register the right
>    masks, in the right order, with the right enable gating. A regression
>    that mis-registers `0xBFFD` as `0xBFFF` in the real `Ay` wiring would
>    not flip a single prior test red.
> 2. **No libz80 regression oracle.** A historical bug in this codebase
>    (memory: *libz80 passes only C (8-bit) to I/O macros*) wired ports
>    through with a truncated 8-bit address, collapsing `0x7FFD` and
>    `0xBFFD` into the same handler. The FUSE Z80 core now passes full
>    16-bit `BC`/`nn|A<<8` addresses to `readport`/`writeport` (see
>    `third_party/fuse-z80/opcodes_base.c:890,943` and `z80_ed.c:31,264,
>    317` — `writeport(BC, …)`, `readport(BC)`). Nothing in the old plan
>    would fail if the shim were silently reverted to 8-bit.
> 3. **NR 0x82–0x89 port-enable bits: zero coverage.** These 32 bits of
>    internal-port-enable state gate almost every Next-only and legacy
>    port. The prior plan mentioned `port_*_io_en` signals in prose but
>    did not test a single bit-to-port mapping, did not test the 0x86–0x89
>    expansion-bus AND masking, did not test reset defaults, and did not
>    test the NR 0x85 bit 7 `reset_type` that controls whether a soft
>    reset reloads the enable bits.
> 4. **Peripheral collision / precedence untested.** Two handlers with
>    overlapping `(mask,value)` ranges were never examined; the iteration
>    order in `PortDispatch::read` (first match wins) is a silent
>    correctness surface.
> 5. **Floating bus on unmapped read untested.** The VHDL returns the
>    +3 floating-bus byte (or 0xFF on 48K) for unmatched reads when
>    `port_p3_floating_bus_io_en` is set; the prior plan accepted
>    `0xFF` from the default arm with no VHDL citation.
>
> Until this plan is re-implemented, the I/O port dispatch subsystem has
> **no trustworthy test coverage**. The subsystem must not be marked green
> on Task 5 based on the old 78/78 number.

---

## Purpose

Validate that `src/port/port_dispatch.{h,cpp}` and every peripheral that
registers handlers against it behave identically to the VHDL top-level
port decode in `zxnext.vhd` **and** correctly interpret the full 16-bit
port address delivered by the FUSE Z80 core.

The two surfaces under test are:

1. **The dispatcher itself** (`PortDispatch::read`/`write`, mask/value
   matching, default-read fallback, precedence, clear/re-register on
   reset).
2. **The as-wired set of real peripherals** in a constructed `Emulator`
   object. Tests call the CPU-facing `IoInterface::in`/`out` through the
   real registered handler list, not a synthetic clean container.

Every expected value is derived from VHDL (`zxnext.vhd`) or from the
libz80 port contract (`third_party/fuse-z80/*.c`) — never from the C++
under test.

## Authoritative Sources

### VHDL (hardware oracle)

Path: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`

| Lines        | Content                                                      |
|--------------|--------------------------------------------------------------|
| 460–489      | `port_*_io_en` signal declarations                           |
| 1226–1235    | NR 0x82–0x89 state registers and power-on defaults           |
| 2180         | `hotkey_expbus_freeze` interplay (DivMMC/MF enable diff)     |
| 2392–2393    | `internal_port_enable` assembly (with/without expbus AND)    |
| 2397–2442    | Bit-to-`port_*_io_en` dispatch (the NR bit map)              |
| 2470–2576    | MSB / LSB early decode (`port_24xx_msb`, `port_3b_lsb`, …)   |
| 2578–2699    | Full per-port match equations                                |
| 2700–2797    | `iord`/`iowr` qualification + read-side enables              |
| 2800–2840    | Wired-OR read data bus                                       |
| 5052–5057    | Soft-reset reload of NR 0x82–0x85 when `reset_type = 1`      |
| 5499–5509    | NR 0x82–0x85 write paths (including `reset_type` in bit 7)   |
| 6129–6138    | Read-back of NR 0x82–0x85 through `port_253b`                |

### libz80 port contract (software oracle)

Path: `third_party/fuse-z80/`

| File / symbol                                    | Contract                                         |
|--------------------------------------------------|--------------------------------------------------|
| `opcodes_base.c:890` `OUT (nn),A`                | `writeport(nn | (A<<8), A)` — upper 8 bits = A   |
| `opcodes_base.c:943` `IN A,(nn)`                 | `readport(nn | (A<<8))` — upper 8 bits = A       |
| `z80_ed.c:31,87,113,…` `OUT (C),r`               | `writeport(BC, r)` — full 16-bit BC              |
| `z80_ed.c:27,83,109,…` `IN r,(C)`                | `readport(BC)` — full 16-bit BC                  |
| `z80_ed.c:317,338,386,…` INI/OUTI/INIR/OTIR/…    | `readport(BC)` / `writeport(BC, …)`              |
| `src/cpu/z80_cpu.cpp:88,111`                     | `s_io->in(port)` / `s_io->out(port, b)` — 16-bit |

**Regression vector.** A prior bug truncated the port parameter to
`C` only. Any test that can only distinguish ports whose LSBs differ
will silently pass on a regressed build. The plan therefore requires
at least one oracle row per (MSB-distinguished) pair where **LSB is
identical** and the correct decode depends on bits 15:8.

### C++ implementation under test

- `src/port/port_dispatch.{h,cpp}` — `PortDispatch` (mask/value linear match).
- `src/core/emulator.cpp` — where each peripheral calls
  `port_dispatch.register_handler(...)`. Tests must construct a real
  `Emulator` (headless) and probe **through** `IoInterface::in`/`out`,
  not through a private clean container.

## Architecture Summary (as derived from VHDL)

### Two-stage decode
`zxnext.vhd:2470–2576` splits `cpu_a(15:0)` into MSB (`cpu_a(15:8)`)
and LSB (`cpu_a(7:0)`) one-hot strobes, then recombines them plus an
`io_en` bit per port. Our dispatcher does the same match in one step
via `(port & mask) == value`; the VHDL decomposition is still visible
in the `(mask, value)` pairs each peripheral registers.

### Internal port enable vector
`zxnext.vhd:2392–2393`:

```
internal_port_enable <=
    (nr_85 & nr_84 & nr_83 & nr_82)                                    when expbus_eff_en = '0' else
    ((nr_89 and nr_85) & (nr_88 and nr_84) & (nr_87 and nr_83) & (nr_86 and nr_82));
```

So the 32-bit enable word is NR 0x82 in bits 7:0, NR 0x83 in 15:8,
NR 0x84 in 23:16, NR 0x85 in 27:24 (bits 28..31 are unused). When the
expansion-bus IO passthrough is enabled, each byte is ANDed with the
corresponding NR 0x86–0x89 mask.

### Exact bit-to-port map (VHDL `zxnext.vhd:2397–2442`)

This is the authoritative map the new plan must exercise one bit at a
time. Each emulator test toggles exactly one enable bit and verifies
that exactly the expected handler goes silent while all others remain
active.

| NR   | Bit | `internal_port_enable` index | VHDL signal                         | Emulator effect                            |
|------|-----|------------------------------|-------------------------------------|--------------------------------------------|
| 0x82 | 0   | 0                            | `port_ff_io_en`                     | Timex SCLD port 0xFF write                 |
| 0x82 | 1   | 1                            | `port_7ffd_io_en`                   | 128K bank port 0x7FFD                      |
| 0x82 | 2   | 2                            | `port_dffd_io_en`                   | Pentagon extended 0xDFFD                   |
| 0x82 | 3   | 3                            | `port_1ffd_io_en`                   | +3 extended memory 0x1FFD                  |
| 0x82 | 4   | 4                            | `port_p3_floating_bus_io_en`        | +3 floating bus (low 0x?FFD on p3)         |
| 0x82 | 5   | 5                            | `port_dma_6b_io_en`                 | DMA 0x6B (ZXN DMA)                         |
| 0x82 | 6   | 6                            | `port_1f_io_en`                     | Kempston 1 (0x1F + DAC-df alias gate)      |
| 0x82 | 7   | 7                            | `port_37_io_en`                     | Kempston 2 (0x37)                          |
| 0x83 | 0   | 8                            | `port_divmmc_io_en`                 | DivMMC 0xE3                                |
| 0x83 | 1   | 9                            | `port_multiface_io_en`              | Multiface enable/disable I/O               |
| 0x83 | 2   | 10                           | `port_i2c_io_en`                    | I²C 0x103B / 0x113B                        |
| 0x83 | 3   | 11                           | `port_spi_io_en`                    | SPI 0xE7 / 0xEB                            |
| 0x83 | 4   | 12                           | `port_uart_io_en`                   | UART 0x143B / 0x153B                       |
| 0x83 | 5   | 13                           | `port_mouse_io_en`                  | Kempston mouse 0xFADF / 0xFBDF / 0xFFDF    |
| 0x83 | 6   | 14                           | `port_sprite_io_en`                 | Sprite 0x0057 / 0x005B / 0x303B            |
| 0x83 | 7   | 15                           | `port_layer2_io_en`                 | Layer 2 0x123B                             |
| 0x84 | 0   | 16                           | `port_ay_io_en`                     | AY 0xFFFD / 0xBFFD                         |
| 0x84 | 1   | 17                           | `port_dac_sd1_ABCD_1f0f4f5f_io_en`  | Soundrive mode 1 DAC (0x1F/0x0F/0x4F/0x5F) |
| 0x84 | 2   | 18                           | `port_dac_sd2_ABCD_f1f3f9fb_io_en`  | Soundrive mode 2 DAC                       |
| 0x84 | 3   | 19                           | `port_dac_stereo_AD_3f5f_io_en`     | Profi Covox stereo AD (0x3F/0x5F)          |
| 0x84 | 4   | 20                           | `port_dac_stereo_BC_0f4f_io_en`     | Covox stereo BC (0x0F/0x4F)                |
| 0x84 | 5   | 21                           | `port_dac_mono_AD_fb_io_en`         | Pentagon/ATM mono AD 0xFB (masked by sd2)  |
| 0x84 | 6   | 22                           | `port_dac_mono_BC_b3_io_en`         | GS Covox mono BC 0xB3                      |
| 0x84 | 7   | 23                           | `port_dac_mono_AD_df_io_en`         | Specdrum 0xDF (also Kempston 0x1F alias)   |
| 0x85 | 0   | 24                           | `port_ulap_io_en`                   | ULA+ 0xBF3B / 0xFF3B                       |
| 0x85 | 1   | 25                           | `port_dma_0b_io_en`                 | DMA 0x0B (Z80-DMA compatible)              |
| 0x85 | 2   | 26                           | `port_eff7_io_en`                   | Port 0xEFF7 (Pentagon config)              |
| 0x85 | 3   | 27                           | `port_ctc_io_en`                    | Z80 CTC (0x183B..0x1F3B per VHDL 2690)     |

**Power-on default** (`zxnext.vhd:1226–1234`): all bits `'1'` — every
port is enabled out of reset. **Soft-reset reload**: only if NR 0x85
bit 7 `reset_type = '1'` (default `'1'`, `zxnext.vhd:5052–5057`); if
software clears that bit, NR 0x82–0x85 survive a soft reset. NR 0x86–
0x89 are the bus-side masks; they are only AND-ed when
`expbus_eff_en = '1'` (external expansion bus I/O passthrough enabled).

### NR 0x85 bit packing (`zxnext.vhd:5508–5509, 6138`)

Writing NR 0x85: `nr_85_internal_port_enable <= dat(3:0)`;
`nr_85_internal_port_reset_type <= dat(7)`. Reading NR 0x85 returns
`reset_type & "000" & enable(3:0)`. Bits 4..6 read back as 0.

### NR 0x86–0x89 expansion-bus ANDing

Tests must cover: with `expbus_eff_en = 0` (default), writing NR 0x86–
0x89 has **no** observable gating effect on any port. With
`expbus_eff_en = 1`, clearing a bit in NR 0x86 disables the
corresponding NR 0x82 port. The exception is the two `_diff` signals
at `zxnext.vhd:2413,2416` used to detect DivMMC/Multiface enable
transitions, which are *XORs* — worth a dedicated row.

## Test Approach

- Tests are written in `test/port/port_test.cpp` and must build a real
  `Emulator` (headless, `machine-type=next`) so that real peripherals
  register real handlers. The dispatcher-in-isolation container tests
  from the prior revision are deleted (not merely relabelled).
- `PortDispatch::read` and `write` are called through
  `IoInterface::in`/`out` to exercise the same path libz80 uses.
- For NR 0x82–0x89 tests, the test writes NR 0x82 value via the real
  NextReg pathway (port 0x253B after selecting 0x82 via 0x243B) — not
  by poking C++ fields directly — so that the NR → dispatcher wiring
  is under test end-to-end.
- Every row below is either a VHDL-grounded behavioural oracle or a
  libz80-contract regression oracle. Tautologies (e.g. "register then
  read the same mask and check it matches") are banned.

## Test Catalog

Each row: **ID · title · preconditions · stimulus · expected · oracle**.
"Oracle" is the VHDL line or libz80 file:line that dictates the expected
value. All port values are 16-bit.

### Group A. libz80 regression oracles (the historical bug)

The point of this group is that it would fail — in a way a code reader
can see in 5 seconds — if anything between `Z80Cpu` and
`PortDispatch::in`/`out` ever again truncates the port to 8 bits.

| ID        | Title                                   | Preconditions                                   | Stimulus                                                            | Expected                                                                                     | Oracle                                              |
|-----------|-----------------------------------------|-------------------------------------------------|---------------------------------------------------------------------|----------------------------------------------------------------------------------------------|-----------------------------------------------------|
| LIBZ80-01 | `OUT (C),r` to 0x7FFD vs 0xBFFD         | 128K timing; AY+MMU real handlers registered    | BC=0x7FFD, r=0x10, OUT (C),r; then BC=0xBFFD, r=0x3F, OUT (C),r     | MMU bank register sees 0x10; AY data register sees 0x3F. Swapping bus addresses is detected. | `zxnext.vhd:2593,2648`; `z80_ed.c:31` `writeport(BC,…)` |
| LIBZ80-02 | `IN A,(nn)` upper byte honoured         | A=0x24, nn=0x3B, NextReg selected reg = 0x01    | Execute `IN A,(0x3B)` with A=0x24 (forms port 0x243B)              | Returns NextReg read for register 0x01 at 0x243B, not a generic LSB-0x3B handler.            | `zxnext.vhd:2625`; `opcodes_base.c:943`             |
| LIBZ80-03 | `OUT (nn),A` upper byte honoured        | A=0x25, nn=0x3B, reg 0x07 pre-selected          | `OUT (0x3B),A` with A=0x25 (forms port 0x253B), data 0x02          | NextReg 0x07 receives 0x02; **not** an unrelated 0x003B handler.                             | `zxnext.vhd:2626`; `opcodes_base.c:890`             |
| LIBZ80-04 | INIR block transfer uses full BC        | BC=0x12_03B, HL=0x4000                          | Execute INI once                                                     | `readport(0x123B)` hits Layer 2 register; not 0x003B.                                        | `zxnext.vhd:2635`; `z80_ed.c:317`                   |
| LIBZ80-05 | MSB-only discrimination                 | NR 0x84 bit 0 cleared (AY disabled)             | Read 0xBFFD                                                         | Floating-bus (or default) value, **not** AY register. A C-only truncation maps this to 0xFD. | `zxnext.vhd:2648`; libz80 contract                  |

### Group B. Real-peripheral registration

These require the real `Emulator` object and walk every port that the
emulator actually wires to a subsystem. Each test both **hits** the
port (positive) and **verifies a near-miss does not match** (negative).

| ID       | Title                                | Preconditions                   | Stimulus                                              | Expected                                                     | Oracle                                   |
|----------|--------------------------------------|---------------------------------|-------------------------------------------------------|--------------------------------------------------------------|------------------------------------------|
| REG-01   | ULA 0xFE matches any even address    | All enables default             | OUT 0xFEFE / 0x01FE / 0x00FE                          | All three reach ULA border/beeper.                           | `zxnext.vhd:2582`                        |
| REG-02   | 0xFE does not match on odd address   | —                               | OUT 0x00FF (odd)                                      | Hits `port_ff` / SCLD path, not ULA.                         | `zxnext.vhd:2582–2583`                   |
| REG-03   | NextReg select 0x243B                | —                               | OUT 0x243B ← 0x07                                     | NextReg register index becomes 0x07.                         | `zxnext.vhd:2625`                        |
| REG-04   | NextReg data 0x253B                  | Reg 0x07 selected               | OUT 0x253B ← 0x03                                     | Underlying NextReg 0x07 state = 0x03 (check via read-back).  | `zxnext.vhd:2626`                        |
| REG-05   | 0x243C/0x253C not decoded            | —                               | OUT 0x243C                                            | Ignored (no handler); default-read on IN returns floating.   | `zxnext.vhd:2625`                        |
| REG-06   | AY select 0xFFFD real                | 128K or Next                    | OUT 0xFFFD ← 0x08                                     | Real AY register latch = 0x08.                               | `zxnext.vhd:2647`                        |
| REG-07   | AY data 0xBFFD real                  | AY register 0x08 latched        | OUT 0xBFFD ← 0x0F                                     | Real AY channel volume state = 0x0F.                         | `zxnext.vhd:2648`                        |
| REG-08   | 0x7FFD MMU bank select               | Not in 0x1FFD special mode      | OUT 0x7FFD ← 0x11                                     | MMU page 3 = bank 1, ROM select bit honoured.                | `zxnext.vhd:2593`                        |
| REG-09   | 0x1FFD +3 extended                   | `plus3` machine type            | OUT 0x1FFD ← 0x04                                     | +3 extended paging latched.                                  | `zxnext.vhd:2599`                        |
| REG-10   | 0xDFFD Pentagon ext                  | —                               | OUT 0xDFFD ← 0x07                                     | Pentagon extended bank state updated.                        | `zxnext.vhd:2596`                        |
| REG-11   | DivMMC 0xE3 real                     | DivMMC subsystem present        | OUT 0xE3 ← 0x40                                       | DivMMC automap latch reflects value.                         | `zxnext.vhd:2608`                        |
| REG-12   | SPI CS 0xE7, data 0xEB               | SPI subsystem present           | OUT 0xE7 ← 0xFE; OUT 0xEB ← 0x55                      | Real SPI CS register and data shift register updated.        | `zxnext.vhd:2620–2621`                   |
| REG-13   | Sprite 0x303B write-then-read        | Sprite subsystem                | OUT 0x303B ← 0x05; IN 0x303B                          | Sprite status reflects VHDL contract for that write.         | `zxnext.vhd:2681`                        |
| REG-14   | Layer 2 0x123B                       | Layer 2 subsystem               | OUT 0x123B ← 0x13                                     | Layer 2 NextReg control reflects write.                      | `zxnext.vhd:2635`                        |
| REG-15   | I²C 0x103B / 0x113B                  | I²C subsystem                   | OUT 0x103B ← 0; OUT 0x113B ← 0                        | SCL/SDA lines driven.                                        | `zxnext.vhd:2630–2631`                   |
| REG-16   | UART 0x143B / 0x153B                 | UART subsystem                  | OUT 0x143B ← 'A'                                      | UART TX holding register = 'A'.                              | `zxnext.vhd:2639`                        |
| REG-17   | UART 0x133B rejected                 | UART subsystem                  | OUT 0x133B ← 'Z'                                      | No UART state change (fails bit equation).                   | `zxnext.vhd:2639`                        |
| REG-18   | Kempston 1 0x001F                    | `port_1f_hw_en=1`               | IN 0x001F                                             | Joystick state returned.                                     | `zxnext.vhd:2674`                        |
| REG-19   | Kempston 2 0x0037                    | `port_37_hw_en=1`               | IN 0x0037                                             | Joystick 2 state returned.                                   | `zxnext.vhd:2675`                        |
| REG-20   | Mouse 0xFADF/0xFBDF/0xFFDF           | Mouse present                   | IN each                                               | Buttons / X / Y respectively.                                | `zxnext.vhd:2668–2670`                   |
| REG-21   | ULA+ 0xBF3B / 0xFF3B                 | ULA+ present                    | OUT 0xBF3B ← idx; OUT 0xFF3B ← col                    | Palette entry updated.                                       | `zxnext.vhd:2685–2686`                   |
| REG-22   | DMA 0x6B vs 0x0B                     | DMA present                     | OUT 0x6B; OUT 0x0B                                    | Both land on same DMA engine; mode latch differs.            | `zxnext.vhd:2643`                        |
| REG-23   | CTC 0x183B range                     | CTC enable default              | OUT 0x183B                                            | CTC channel 0 updated.                                       | `zxnext.vhd:2690`                        |
| REG-24   | Unmapped port read                   | —                               | IN 0x0042 (nothing registered)                        | Floating-bus byte per 48K/128K/+3 rules, **not** 0x00.       | `zxnext.vhd:2589`, `2800–2840`           |
| REG-25   | Unmapped port write                  | —                               | OUT 0x0042                                            | No side effect in any subsystem.                             | `zxnext.vhd:2697`                        |

### Group C. NR 0x82–0x89 bit-by-bit enable gating

One row per enable bit. Each test (a) confirms the port works with the
bit set (default), (b) writes NR 0x82..0x85 via 0x243B/0x253B to clear
**only that bit**, (c) confirms that exact port is now inert, (d)
confirms a *different* port (from a different NR) is still live.

| ID       | NR/Bit    | Port exercised          | Oracle (VHDL)              |
|----------|-----------|-------------------------|----------------------------|
| NR82-00  | 0x82 b0   | 0x00FF (SCLD write)     | `zxnext.vhd:2397`          |
| NR82-01  | 0x82 b1   | 0x7FFD (MMU)            | `zxnext.vhd:2399`          |
| NR82-02  | 0x82 b2   | 0xDFFD                  | `zxnext.vhd:2400`          |
| NR82-03  | 0x82 b3   | 0x1FFD                  | `zxnext.vhd:2401`          |
| NR82-04  | 0x82 b4   | +3 floating bus 0x0FFD  | `zxnext.vhd:2403, 2589`    |
| NR82-05  | 0x82 b5   | DMA 0x6B                | `zxnext.vhd:2405, 2643`    |
| NR82-06  | 0x82 b6   | Kempston 0x1F           | `zxnext.vhd:2407, 2674`    |
| NR82-07  | 0x82 b7   | Kempston 0x37           | `zxnext.vhd:2408, 2675`    |
| NR83-00  | 0x83 b0   | DivMMC 0xE3             | `zxnext.vhd:2412, 2608`    |
| NR83-01  | 0x83 b1   | Multiface I/O           | `zxnext.vhd:2415, 2615`    |
| NR83-02  | 0x83 b2   | I²C 0x103B              | `zxnext.vhd:2418, 2630`    |
| NR83-03  | 0x83 b3   | SPI 0xE7                | `zxnext.vhd:2419, 2620`    |
| NR83-04  | 0x83 b4   | UART 0x143B             | `zxnext.vhd:2420, 2639`    |
| NR83-05  | 0x83 b5   | Mouse 0xFADF            | `zxnext.vhd:2422, 2668`    |
| NR83-06  | 0x83 b6   | Sprite 0x303B           | `zxnext.vhd:2423, 2681`    |
| NR83-07  | 0x83 b7   | Layer 2 0x123B          | `zxnext.vhd:2424, 2635`    |
| NR84-00  | 0x84 b0   | AY 0xFFFD/0xBFFD        | `zxnext.vhd:2428, 2647–8`  |
| NR84-01  | 0x84 b1   | DAC SD1 0x1F/0x0F/0x4F/0x5F | `zxnext.vhd:2429, 2661–4` |
| NR84-02  | 0x84 b2   | DAC SD2 0xF1/0xF3/0xF9/0xFB | `zxnext.vhd:2430, 2661–4` |
| NR84-03  | 0x84 b3   | DAC stereo AD 0x3F/0x5F | `zxnext.vhd:2431, 2661, 2664` |
| NR84-04  | 0x84 b4   | DAC stereo BC 0x0F/0x4F | `zxnext.vhd:2432, 2662–3`  |
| NR84-05  | 0x84 b5   | DAC mono AD 0xFB (masked by sd2) | `zxnext.vhd:2433, 2658` |
| NR84-06  | 0x84 b6   | DAC mono BC 0xB3        | `zxnext.vhd:2434, 2659`    |
| NR84-07  | 0x84 b7   | Specdrum 0xDF + Kempston alias | `zxnext.vhd:2435, 2674` |
| NR85-00  | 0x85 b0   | ULA+ 0xBF3B             | `zxnext.vhd:2439, 2685`    |
| NR85-01  | 0x85 b1   | DMA 0x0B                | `zxnext.vhd:2440, 2643`    |
| NR85-02  | 0x85 b2   | 0xEFF7                  | `zxnext.vhd:2441, 2604`    |
| NR85-03  | 0x85 b3   | CTC 0x183B              | `zxnext.vhd:2442, 2690`    |

Plus:

| ID        | Title                                            | Stimulus                                                                | Expected                                                                | Oracle                              |
|-----------|--------------------------------------------------|-------------------------------------------------------------------------|-------------------------------------------------------------------------|-------------------------------------|
| NR-DEF-01 | Power-on defaults all-enabled                    | Fresh `Emulator` construct; read NR 0x82–0x85 via 0x243B/0x253B          | NR 0x82=0xFF, 0x83=0xFF, 0x84=0xFF, 0x85 low nibble=0x0F, bit 7=1       | `zxnext.vhd:1226–1230`              |
| NR-RST-01 | Soft reset reloads when reset_type=1             | Clear NR 0x82 bit 0; leave NR 0x85 bit 7 = 1; soft-reset                | NR 0x82 returns to 0xFF                                                 | `zxnext.vhd:5052–5057`              |
| NR-RST-02 | Soft reset does NOT reload when reset_type=0     | Clear NR 0x82 bit 0; clear NR 0x85 bit 7; soft-reset                    | NR 0x82 bit 0 remains 0                                                 | `zxnext.vhd:5052–5057`              |
| NR-85-PK  | NR 0x85 packing: bits 4–6 read back zero         | Write NR 0x85 ← 0xFF; read back                                         | Value 0x8F (bit 7 + low nibble; middle bits 0)                          | `zxnext.vhd:5508–5509, 6138`        |

### Group D. Expansion-bus masks NR 0x86–0x89

| ID        | Title                                         | Preconditions                     | Stimulus                                          | Expected                                                   | Oracle                 |
|-----------|-----------------------------------------------|-----------------------------------|---------------------------------------------------|------------------------------------------------------------|------------------------|
| BUS-86-01 | NR 0x86 inert when expbus_eff_en=0            | expbus_eff_en=0                   | NR 0x86 ← 0x00; OUT 0x00FF                        | SCLD write still reaches handler                           | `zxnext.vhd:2392`      |
| BUS-86-02 | NR 0x86 gates when expbus_eff_en=1            | expbus_eff_en=1, NR 0x82 bit 0=1  | NR 0x86 bit 0 ← 0; OUT 0x00FF                     | SCLD write blocked                                         | `zxnext.vhd:2393`      |
| BUS-86-03 | NR 0x86 AND with NR 0x82                      | expbus_eff_en=1                   | NR 0x82 bit 1=1, NR 0x86 bit 1=0                  | 0x7FFD blocked (AND of cleared bit)                        | `zxnext.vhd:2393, 2399`|
| BUS-87-D  | DivMMC enable-diff detection                  | expbus_eff_en=1                   | Toggle NR 0x87 bit 0 while NR 0x83 bit 0 fixed    | `port_divmmc_io_en_diff` rising edge observable            | `zxnext.vhd:2413, 2180`|
| BUS-88-00 | NR 0x88 AND with NR 0x84 (AY)                 | expbus_eff_en=1                   | NR 0x88 bit 0 ← 0                                 | 0xFFFD/0xBFFD blocked regardless of NR 0x84                | `zxnext.vhd:2393, 2428`|
| BUS-89-00 | NR 0x89 AND with NR 0x85 (ULA+)               | expbus_eff_en=1                   | NR 0x89 bit 0 ← 0                                 | 0xBF3B blocked                                             | `zxnext.vhd:2393, 2439`|

### Group E. Precedence, collision, clear/re-register

| ID    | Title                                                 | Stimulus                                                                                                                         | Expected                                                                                                                           | Oracle                           |
|-------|-------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------|----------------------------------|
| PR-01 | First-match-wins ordering                             | Register handler A with mask/value matching 0x0100–0x01FF, then handler B for 0x01FE                                             | Dispatcher returns handler A for 0x01FE (insertion order) — document this as the contract; a VHDL fanout has no order but the C++ does | `port_dispatch.cpp:36–43`        |
| PR-02 | Two real peripherals must not overlap                 | Walk `handlers_` after `Emulator::init`                                                                                          | For every pair (h1, h2), `((v1 ^ v2) & (m1 & m2)) != 0` OR the masks are disjoint — asserts no silent shadowing                    | VHDL per-port decode is one-hot  |
| PR-03 | `clear_handlers()` then re-register on reset          | Clear, register one handler, OUT                                                                                                 | Only new handler sees the write                                                                                                    | `port_dispatch.h:21`             |
| PR-04 | Default-read used when no handler matches             | No handler registered for 0x0042; install default_read returning floating-bus byte                                               | IN 0x0042 returns that byte, not 0xFF                                                                                              | `port_dispatch.cpp:43–47`        |
| PR-05 | Default-read NOT used when any handler matches (even with 0 return) | Handler for 0x00FE returning 0x00; default_read returns 0xAA                                                       | IN 0x00FE returns 0x00                                                                                                             | `port_dispatch.cpp:36–42`        |

### Group F. IORQ/M1 / RMW / contention-affected ports

| ID      | Title                                         | Stimulus                                                | Expected                                                            | Oracle                           |
|---------|-----------------------------------------------|---------------------------------------------------------|---------------------------------------------------------------------|----------------------------------|
| IORQ-01 | Interrupt ack not routed to `in`              | Raise IRQ line, let IM1 vector                           | `PortDispatch::in` is **not** called during M1+IORQ                 | `zxnext.vhd:2705`                |
| IORQ-02 | Normal IN is routed                           | Execute `IN A,(0xFE)` inside normal instruction          | `PortDispatch::in(0xFE|(A<<8))` called exactly once                 | `zxnext.vhd:2705`                |
| RMW-01  | 0xFE border + beeper latch                    | OUT 0xFE ← 0x07 (border); OUT 0xFE ← 0x10 (beeper bit)  | ULA border = 7 after first write, bit 4 latches speaker             | `zxnext.vhd:2582`                |
| CTN-01  | Contended-port timing on 0x4000-range port    | `IN A,(0x4000|n)`                                       | T-state count matches contended-port pattern from `readport`        | `z80_cpu.cpp:84–104`             |
| CTN-02  | Uncontended `IN A,(nn)` outside 0x4000 range  | `IN A,(0x00FE)` with A=0                                | Only the fixed +1/+3 T-states                                       | `z80_cpu.cpp:84–104`             |

### Group G. DivMMC automap interaction

| ID       | Title                                              | Preconditions                                             | Stimulus                            | Expected                                                                                                  | Oracle                                |
|----------|----------------------------------------------------|-----------------------------------------------------------|-------------------------------------|-----------------------------------------------------------------------------------------------------------|---------------------------------------|
| AMAP-01  | DivMMC enable diff freezes expansion bus           | NR 0x83 b0 = 1, NR 0x87 b0 = 0, expbus_eff_en = 1         | Write to a divmmc-trigger address   | `hotkey_expbus_freeze` asserts (observable via debug hook / log)                                          | `zxnext.vhd:2180, 2413`               |
| AMAP-02  | 0xE3 writes honoured even when automap held        | DivMMC automap held                                        | OUT 0xE3 ← 0x80                     | DivMMC register state updates (the port is not squelched by automap; only memory mapping changes)         | `zxnext.vhd:2608`                     |
| AMAP-03  | NR 0x83 b0 = 0 disables 0xE3 regardless of automap | NR 0x83 b0 = 0                                             | OUT 0xE3                            | No DivMMC state change; handler gated off                                                                 | `zxnext.vhd:2412, 2608`               |

### Group H. Read data bus wired-OR semantics

The C++ dispatcher returns the **first** matching handler's data. The
VHDL ORs all active lines. For this to be equivalent, at most one
handler can be active for any given read — tested by PR-02. Group H
also verifies the *read* disable gating where the VHDL blocks a handler
from contributing.

| ID     | Title                                           | Stimulus                                         | Expected                                                             | Oracle                     |
|--------|-------------------------------------------------|--------------------------------------------------|----------------------------------------------------------------------|----------------------------|
| BUS-01 | Single-owner invariant over all registered      | Loop all ports 0x0000..0xFFFF through `read`     | For every port, ≤1 handler matches (collect from `handlers_`)        | VHDL one-hot per port      |
| BUS-02 | Disabled port yields default-read byte          | NR 0x84 b0 = 0; IN 0xFFFD                        | Default-read byte (floating bus), not stale AY data                  | `zxnext.vhd:2428, 2771`    |
| BUS-03 | SCLD read gated by `ff_rd_en`, not just `_io_en`| `port_ff_io_en=1`, `ff_rd_en=0`; IN 0x00FF       | ULA floating bus                                                     | `zxnext.vhd:2583, 2789-ish`|

## Out-of-scope / explicitly not tested

- Internal signal cycle-accuracy of the VHDL two-stage decode. The C++
  collapses MSB/LSB into one step; we verify equivalence by port match,
  not by mirroring the intermediate signals.
- Per-peripheral behavioural correctness (e.g. "does the sprite index
  increment correctly" is Sprite's own test plan, not this one). This
  plan verifies only that writes/reads *reach* the right subsystem.
- Multiface paging interactions beyond the enable-diff XOR.

## Open questions (must be resolved before plan is considered final)

1. **Floating-bus default on unmapped reads.** The VHDL returns a
   timing-dependent ULA byte on 48K and the +3 floating-bus byte on +3.
   Our dispatcher's default-read is currently pluggable. What machine-
   mode-specific callback is installed, and does it match
   `zxnext.vhd:2589` for +3 and `zxnext.vhd:2800-2840` wired-OR zero-
   default for Next? Row REG-24 must be pinned down per machine type.
2. **NR 0x85 middle bits.** VHDL declares `nr_85_internal_port_enable`
   as 4 bits and `reset_type` as 1 bit but the register is 8 bits wide
   on the bus. Row NR-85-PK assumes bits 4..6 read as zero; confirm vs
   a recent VHDL revision (the cited lines are from the current clone
   but the signal declaration uses an aggregate).
3. **`port_ff_io_en` read path.** VHDL has a separate `ff_rd_en` that
   gates the *read* contribution of port 0xFF independently of the
   write enable; BUS-03 cites an "ish" line number. The exact gate
   name and line number must be pinned before implementation.
4. **Kempston 0x1F / Specdrum 0xDF overlap.** The `port_1f` equation
   (`zxnext.vhd:2674`) folds 0xDF into port_1f when the Specdrum DAC
   mono-AD-df mode is enabled *and* mouse is disabled. Tests NR82-06
   and NR84-07 partially cover this, but we need a combinatorial row
   for (`port_1f_io_en=0`, `port_dac_mono_AD_df_io_en=1`) to confirm
   the AND gating is correct (should still be blocked).
5. **Dispatcher first-match-wins.** PR-01 documents the current C++
   contract but the VHDL is order-independent. If PR-02 is enforced
   as an invariant, PR-01 becomes vacuous — but any future handler
   that deliberately uses a catch-all mask (e.g. a debug tracer)
   would depend on insertion order. Decide whether PR-01 is an
   asserted contract or a warning.
6. **Multiface enable diff (BUS-87-D).** The `_diff` XOR produces a
   transient; we need to decide whether the test observes it through a
   debug hook or by injecting a memory access that `hotkey_expbus_freeze`
   would gate. The latter crosses into MMU territory.
7. **CTC port range.** `zxnext.vhd:2690` says
   `cpu_a(15:11)="00011"` which covers 0x183B, 0x1B3B, 0x1D3B, 0x1F3B.
   NR85-03 only hits 0x183B — the test must also hit the top of the
   range and a near-miss (0x203B).

## Summary of retractions vs previous revision

| Prior row | Problem                                                                 | Replacement                                       |
|-----------|-------------------------------------------------------------------------|---------------------------------------------------|
| §1 FE-01..06   | Tested a hand-registered stub, not real ULA              | REG-01, REG-02, RMW-01                            |
| §2 FF-01..04   | No `ff_rd_en` distinction; no enable coverage            | NR82-00, BUS-03                                   |
| §3 NR-01..06   | No read-path NextReg state assertions                    | REG-03, REG-04, LIBZ80-02, LIBZ80-03              |
| §4 MEM-01..08  | No MMU state check; no libz80 regression oracle          | REG-08, REG-09, REG-10, LIBZ80-01                 |
| §5 JOY-01..07  | No real peripheral; no NR 0x82 bit coverage              | REG-18, REG-19, NR82-06, NR82-07                  |
| §6 AY-01..06   | No AY state check; no LSB-collision probe                | REG-06, REG-07, LIBZ80-01, NR84-00, BUS-02        |
| §7..§17        | All container-only; no enable-bit single-step            | Groups B + C (per-bit)                            |
| §18 INT-01..04 | "internal response" is a VHDL-internal signal; we test the observable `default_read_` fallback instead | PR-04, PR-05, REG-24 |
| §19 BUS-01..04 | Only trivial wired-OR claims; no one-hot invariant       | BUS-01, BUS-02, PR-02                             |
| §20 IORQ-01..03| Asserted iord/iowr equations, not CPU pathway            | IORQ-01, IORQ-02                                  |

All ~90 tautological rows are retracted. The new plan contains
approximately **5 libz80 oracles + 25 real-peripheral rows + 32 NR bit
rows + 4 NR defaults/reset + 6 expansion-bus rows + 5
precedence/collision rows + 5 IORQ/RMW/contention rows + 3 automap
rows + 3 wired-OR rows ≈ 88 rows**, every one grounded in a specific
VHDL line or libz80 symbol citation.
