# G46(b) — RESET_SOFT vs cold-boot init parity audit (read-only)

**Date:** 2026-05-05  
**Scope:** read-only VHDL-vs-jnext audit on the difference between (a) the
state the NextZXOS supervisor sees in normal boot (after tbblue.fw writes
NR 0x02 = 0x01 → FPGA RESET_SOFT) and (b) the state the supervisor sees
in `--bypass-tbblue-fw` mode (cold init only — no soft reset).

## Methodology

1. Trace the FPGA reset tree:
   * top-level FSM in `zxnext_top_issue4.vhd:268-858` (states
     `S_RESET_HARD_*` and `S_RESET_SOFT_*`).
   * the `reset` rail inside `zxnext.vhd:1730` is `i_RESET = reset_hard
     OR reset_soft`. The core sees ONE merged reset; soft and hard look
     identical to every `if reset = '1'` clause.
   * the only signals that distinguish hard from soft are those with NO
     `if reset = '1'` clause — they default at FPGA configuration time
     (signal initializer `:= ...`) and are mutated only by writes,
     surviving both flavours of reset.
2. Enumerate every `if reset = '1' then` block in `zxnext.vhd`
   (44 occurrences, lines listed below) and the per-peripheral
   `i_reset`/`reset_i` ports (im2_control, divmmc, copper, sprites,
   tilemap, layer2, lores, ULA, CTC, DMA, multiface, audio mixer, AY,
   DAC, I2S — all in the same reset domain).
3. Cross-check `Emulator::init(cfg, preserve_memory)` and
   `Emulator::soft_reset` in `src/core/emulator.cpp:60-218 / 4802-4855`.
   Verify each VHDL "reset on `if reset = '1'`" item is matched in
   `init()` (which is called from cold boot AND from soft_reset with
   `preserve_memory=true`).
4. Diff the post-handoff state expected by the supervisor between (a)
   "after tbblue.fw + RESET_SOFT" and (b) "after `--bypass-tbblue-fw`
   block at `emulator.cpp:3768-3795` only" — same `init()` ran in both
   cases, the difference is the SECOND init triggered by NR 0x02 = 0x01
   that erases tbblue.fw transients.

## Inventory: signals reset by `if reset = '1'` in `zxnext.vhd`

(`reset = reset_hard OR reset_soft`, so this list applies to BOTH
RESET_HARD and RESET_SOFT identically.)

| Signal / register                  | VHDL line | Reset value     | Mirrored in jnext cold init? |
|------------------------------------|-----------|-----------------|------------------------------|
| `nr_02_reset_type` history (FSM)   | 1735-1736 | `100`→`010`→`001` per soft-reset count | jnext: `nmi_source_.strobe_soft_reset()` advances FSM (peripheral/nmi_source.cpp:145). On bypass cold-boot we DON'T strobe — the FSM stays at `100` (HARD). **Diverges from soft-reset path which leaves FSM at `010`.** |
| `port_e7_reg`                      | 3308-3309 | `0xFF`           | `Emulator::init` → `spi_.reset()` (file emulator.cpp:111) ✓ |
| `nmi_mf` / `nmi_divmmc` / `nmi_expbus` | 2098-2105 | `'0'` | `nmi_source_.reset()` ✓ |
| `nmi_state` FSM                    | 2154-2157 | `S_NMI_IDLE`     | `nmi_source_.reset()` ✓ |
| `nr_02_generate_mf_nmi`            | 3843-3844 | `'0'`            | `nmi_source_.reset()` ✓ |
| `nr_02_generate_divmmc_nmi`        | 3856-3857 | `'0'`            | `nmi_source_.reset()` ✓ |
| `nr_da_iotrap_cause`               | 3869-3870 | `"00"`           | `nr_da_iotrap_cause_ = 0` (emulator.cpp:122) ✓ |
| `nr_d9_iotrap_write`               | 3890-3891 | `0x00`           | `nr_d9_iotrap_write_ = 0` (emulator.cpp:121) ✓ |
| `port_7ffd_reg`                    | 3646-3648 | `0x00`           | jnext: gated on **hard** only (mmu.cpp:58-60). **Soft-reset preserves ; cold-boot also clears via the `hard` branch.** Matches VHDL since cold = hard. |
| `port_dffd_reg`, `_6`              | 3686-3689 | `0`, `0`         | Same hard-only gate (mmu.cpp:73-78). On cold boot already 0. |
| `port_1ffd_reg`                    | 3713-3715 | `"000"`          | Reset by mmu_.reset (handled in apply_legacy_paging_). ✓ |
| `port_1ffd_mtr_n`                  | 3747-3749 | `'1'`            | mmu_ owns it. ✓ |
| `MMU0..MMU7`                       | 4610-4618 | `FF FF 0A 0B 04 05 00 01` | mmu_.reset RESET_PAGES (mmu.cpp:121-130) ✓ |
| `nr_register` (NR-port-of-NR)      | 4595-4596 | `0x24`           | `nextreg_.reset()` (nextreg.cpp:82) ✓ |
| `port_bf3b_ulap_mode/index`        | 4528-4530 | `00`             | layer2/palette path ✓ |
| `port_ff3b_ulap_en`                | 4546-4547 | `'0'`            | ✓ |
| `nr_07_cpu_speed` AND live `cpu_speed` | 5786-5801 | `"00"` (3.5MHz) | `nextreg_.write(0x07,…)` reset path; cold init writes `regs_[0x07]=0x00` (nextreg.cpp:44). ✓ |
| `eff_nr_08_contention_disable`     | 5802     | `'0'`            | nr_08_stored_low_ = 0x10 (emulator.cpp:140); contention.reset wires this. ✓ |
| `nr_8c_altrom(7:4) ← (3:0)`        | 2253-2256 | nibble copy      | mmu_.reset hard-only (mmu.cpp:92-95) ✓ |
| Big NR state block (NR 06,08,09,0B,12,13,14,15-1B,22,23,26,27,30-33, palette idx, 42,43,4A,4B,4C,62,64,68,6A,6B,6C,6E,6F,70,71,98-9B,90-93,A0,A2,A8,A9,B8-BB,C0,C4,C6,CC,CD,CE,D8) | 4930-5108 | various per VHDL | `nextreg_.reset()` clears `regs_.fill(0)` then re-applies a curated set (nextreg.cpp:7-117). Most match. **NR 0xA0 explicitly handled at emulator.cpp:181.** |
| `nr_82-85` (gated on NR 0x85 bit 7) | 5052-5057 | conditional 0xFF | nextreg.cpp:11-64 implements identical conditional. ✓ |
| `nr_86-89` (gated on NR 0x89 bit 7) | 5061-5067 | conditional 0xFF | nextreg.cpp:72-80 forces 0xFF unconditionally (approximation). minor drift, unlikely to influence boot. |
| `nr_d8_io_trap_fdc_en`             | 5107     | `'0'`            | `nr_d8_io_trap_fdc_en_ = false` (emulator.cpp:118) ✓ |
| `bootrom_en` (gated on `nr_03_config_mode='1'`) | 5109-5111 | `'1'` if cfg_mode=1 | mmu_.reset re-enables if `boot_rom_` ptr present (mmu.cpp:120). bypass mode does NOT load boot ROM, so `boot_rom_` is nullptr → `boot_rom_en_` stays at member-init value (false). ✓ |
| im2_control internal state         | im2_control.vhd:99,114,150,221 | various FSMs | `im2_.reset()` (emulator.cpp:93) ✓ |
| Sprites engine state (very large)  | sprites.vhd:523-1020 | various          | `sprites_.reset()` ✓ |
| Copper FSM, address                | copper.vhd:60-69 | `0`, idle   | `copper_.reset()` ✓ |
| DivMMC automap held / button NMI   | divmmc.vhd:108,126,139 | `'0'`     | `divmmc_.reset()` (peripheral/divmmc.cpp:29) ✓ |
| Multiface FSM                      | multiface.vhd            | various          | `multiface_.reset(true)` (emulator.cpp:115) ✓ |
| CTC, DMA, AY, DAC, I2S, mixer, ULA, tilemap, lores, layer2 | per-file `reset_i` paths | various | each peripheral has its own `.reset()` invoked from emulator.cpp:88-114. ✓ |
| NR write `case` defaults that initialise fields not in big block (port_eff7_reg_2/3, port_dffd_reg_6, etc.) | various | various | mmu.cpp owns the hard-gated subset; nextreg.cpp owns the rest. ✓ |
| `z80_retn_seen_28_d`               | 1918-1919 | `'0'`            | cpu_.reset() ✓ |

### Signals NOT in the `if reset = '1'` family (survive both flavours of reset)

These persist from FPGA configuration time onwards; they only mutate
through explicit writes. tbblue.fw sets them BEFORE its trailing
RESET_SOFT, so the supervisor sees firmware-set values. In bypass mode
we must replay the firmware's writes; the bypass block at
`emulator.cpp:3768-3795` does exactly this for the documented set.

| Signal                       | VHDL line | Power-on default | Mutator (write only)                  |
|------------------------------|-----------|------------------|---------------------------------------|
| `nr_03_config_mode`          | 1102      | `'1'`            | NR 0x03 bits[2:0] (5147-5151)         |
| `nr_03_machine_type`         | 1103      | `"011"` (+3)     | NR 0x03 bits[2:0] (5137-5145), gated on config_mode=1 |
| `nr_03_machine_timing`       | 1099      | `"011"` (+3)     | NR 0x03 bit7+bits[6:4] (5126-5132)     |
| `nr_03_user_dt_lock`         | (scalar)  | `'0'`            | NR 0x03 bit3 toggle (5135)             |
| `nr_05_joy0/joy1/5060/scandouble` | 1105-1303 | `001/000/0/1` | NR 0x05 (5157-5158)                    |
| `nr_06_*` *(except hotkey gates which ARE in reset block)* | 1107-1113 | various | NR 0x06 (5162-5170)                    |
| `nr_85_internal_port_reset_type` | 1230  | `'1'`            | NR 0x85 bit7                            |
| `nr_89_bus_port_reset_type`  | 1235      | `'1'`            | NR 0x89 bit7                            |
| `nr_8c_altrom` lower nibble  | 387       | `0x00`           | NR 0x8C (only upper nibble copied on reset) |
| `nr_8a_bus_port_propagate`   | 1236      | `0x00`           | NR 0x8A                                 |
| `nr_8f_mapping_mode`         | 888       | `0x00`           | NR 0x8F                                 |
| `nr_10_coreid` / `nr_10_flashboot` | 1133  | `00001` / `0`    | NR 0x10 writes (5680-5705) gated on cfg_mode |
| `nr_04_romram_bank`          | 1104      | `0x00`           | NR 0x04 (5717)                          |
| `nr_7f_user_register_0`      | 1216      | `0xFF`           | NR 0x7F                                 |
| `nr_a9_esp_gpio0`            | 1245      | `'1'`            | NR 0xA9                                 |
| `nr_f0_select`               | 1266      | `'1'`            | NR 0xF0                                 |

## Top 3 candidate "missing initializations" most likely to influence
supervisor's early branching behaviour

### 1. `nr_02_reset_type` history FSM is left at HARD on bypass

VHDL:`nr_02_reset_type` declared `:= "100"` (HARD); each soft reset
shifts: `"100" → "010" → "001"`. Read at NR 0x02 bits[1:0]
(zxnext.vhd:5891). NextZXOS likely uses these bits to distinguish
"first cold boot" from "subsequent soft resets" and pick a different
boot path accordingly. **In normal boot**, after tbblue.fw triggers
NR 0x02 = 0x01, the FSM has already been to soft once → bits 1:0 = `10`.
**In bypass cold-boot**, the FSM is still at HARD → bits 1:0 = `00`.
**This is the single most plausible explanation for the supervisor
taking the "dispatcher" path (cold boot) vs the "wrapper" path
(post-soft-reset).**

(Compare to G46(b) symptom: supervisor reaches the same PCs but with
SP=$FF7F (user stack) vs CSpect's SP=$5BF9 (supervisor stack). NextZXOS
clearly has a "first-time setup" branch that runs once on hard reset
and a "resume" branch that runs after soft reset.)

`Emulator::soft_reset` calls `nmi_source_.strobe_soft_reset()` to
advance the FSM in normal boot (peripheral/nmi_source.cpp:145, called
from the NR 0x02 write_handler). The bypass path NEVER triggers
soft_reset and NEVER strobes that FSM, so the supervisor reads HARD.

### 2. `bootrom_en` shadow + `nr_03_config_mode` interplay

VHDL:5109-5111 — on `reset='1'`, if `nr_03_config_mode='1'` then
`bootrom_en <= '1'`. In normal boot the supervisor sees `bootrom_en=0`
(tbblue.fw wrote NR 0x03 = 0xB3 → cfg_mode=0 BEFORE soft reset; the
soft-reset block at 5109 sees cfg_mode=0 and leaves bootrom_en at its
last-written value 0). In bypass mode, `mmu_.reset()` re-enables boot
ROM if `boot_rom_` is loaded (mmu.cpp:120) — bypass intentionally does
NOT load boot ROM, so this is fine. **No divergence here for bypass.**

### 3. `nr_03_machine_timing` may be wrong on bypass

VHDL: `:= "011"` (+3 timing) — survives reset.
The bypass `nextreg_.write(0x03, 0xB3)` writes bits[6:4]=011, which
sets `nr_03_machine_timing := "011"` (line 5129 of zxnext.vhd). Same
as the power-on default. **No divergence here**, but it's worth noting
that the supervisor branches on `eff_nr_03_machine_timing` for video
timing — the path is correct as-is.

## Concrete recommendation

**Strobe the soft-reset history FSM in the `--bypass-tbblue-fw`
post-handoff block** so the supervisor sees `nr_02_reset_type` =
`"010"` (last reset was SOFT, count=1) instead of `"100"` (HARD), as
real hardware does after tbblue.fw triggers RESET_SOFT exactly once.

Concrete one-liner addition near `emulator.cpp:3793`:

```cpp
// Emulate the tbblue.fw RESET_SOFT side-effect on the
// nr_02_reset_type history FSM (VHDL zxnext.vhd:1735-1736).
// Without this, NextZXOS reads bits 1:0 = "00" (HARD) and takes the
// cold-boot/dispatcher path instead of the post-soft-reset/wrapper
// path. See peripheral/nmi_source.cpp:145 for the FSM mutator.
nmi_source_.strobe_soft_reset();
```

This change is purely internal NMI-source FSM bookkeeping — it does
NOT trigger an actual reset, so the bypass path's loaded-supervisor +
NR-state handoff is undisturbed. After this change, NR 0x02 read by
the supervisor will return bits[1:0]=`10` (one prior soft reset, as
real hardware would after tbblue.fw's `for(;;)`).

If the bypass mode wants to mirror "soft-reset just happened TWICE"
(once for the deferred config write, once for the actual handoff —
some firmware paths do this), call `strobe_soft_reset()` twice; the
FSM saturates at `"001"` after two strobes (VHDL:1736 OR-with-self
behaviour).

## Secondary recommendation (lower priority, lower confidence)

If the soft-reset-FSM tweak does not unblock G46(b), look next at
`nr_8a_bus_port_propagate` and `nr_8c_altrom`. These are NOT in the
bypass replay block, but tbblue.fw also doesn't write them in its
init_registers path — so they should be at power-on defaults in both
flows. Verify by `grep nr_8a tbblue/src/firmware/app/src/boot.c` if
that hypothesis surfaces.

## Files referenced

- VHDL top-level: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
- VHDL chip-level: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext_top_issue4.vhd:268-858`
- VHDL peripherals: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/{device/{im2_control,divmmc,copper,multiface}.vhd, video/sprites.vhd}`
- jnext core init: `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp:60-218` (cold init path) and `:4802-4855` (soft reset)
- jnext bypass post-handoff: `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp:3751-3795`
- jnext NextReg reset: `/home/jorgegv/src/spectrum/jnext/src/port/nextreg.cpp:7-118`
- jnext MMU reset: `/home/jorgegv/src/spectrum/jnext/src/memory/mmu.cpp:52-150`
- jnext NMI source FSM: `/home/jorgegv/src/spectrum/jnext/src/peripheral/nmi_source.cpp:145`

## Closing note (audit confidence)

- **High confidence** that `nr_02_reset_type` is the most likely
  divergence — it is THE one piece of FPGA state explicitly
  documented (zxnext.vhd:1735-1736) to track soft-reset history, it is
  surfaced verbatim through NR 0x02 read, and NextZXOS clearly has
  separate first-cold-boot vs post-soft-reset code paths (per CSpect
  reference traces showing supervisor reaching the SAME PC with very
  different SP/IX/MMU state).
- **Medium confidence** that the rest of the bypass post-handoff block
  is correct. The audit found no other "if reset='1'" item missed by
  jnext's cold init.
- **Low confidence** that the soft-reset strobe alone fixes G46(b),
  but it eliminates the most parsimonious remaining hypothesis. If the
  symptom persists after that change, the divergence is no longer a
  reset-tree issue and investigation should pivot to the NextZXOS
  supervisor's first-cold-boot setup code (which presumably runs and
  leaves persistent state our bypass mode currently misses).
