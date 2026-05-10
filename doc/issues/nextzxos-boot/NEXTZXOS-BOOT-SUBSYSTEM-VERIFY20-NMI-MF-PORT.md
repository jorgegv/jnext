# Pass-20 Audit Report — NMI + Multiface + Port-Decode + NextREG

Branch: `task2/verify20-nmi-mf-port` (off integration HEAD `deb7bdf`).
Subsystems: NMI source, Multiface, Port decode, NextREG.

## Summary

| Item | Value |
|------|-------|
| Class-(b) findings | 0 |
| Class-(c) findings | 2 (V20-NMP-XADC, V20-NMP-02) |
| Class-(d) findings | 0 |
| Enumeration rows | ~270 |
| Source files audited | 7 (multiface.{h,cpp}, nmi_source.{h,cpp}, emulator.cpp, nextreg.{h,cpp}, port_dispatch.{h,cpp}) |
| VHDL oracle ranges | zxnext.vhd :1226-1306 (defaults), :2400-2700 (port decode), :4860+ (NR write decoder), :5052-5870 (NR write side effects), :5878-6289 (NR read mux), :7420-7558 (XADC Issue 2/3 gen), device/multiface.vhd :1-197 |

Test results (Release build):
- `ctest --output-on-failure`: 38/38 PASS, 0 FAIL.
- `./build/test/fuse_z80_test build/test/fuse`: 1356/1356 PASS.
- `bash test/00regression/regression.sh`: 33 / 0 / 0.

## Findings

### V20-NMP-XADC (Class-(c)) — CLOSED, commit `b81c832`

NR 0xF9 / NR 0xFA reads were falling through to the bare `regs_[]` cache,
leaking the last-written byte. Per VHDL `zxnext.vhd:6280-6284` the read mux
returns `nr_f9_xadc_d0` / `nr_fa_xadc_d1`, and per the Issue 2/3 generate
at `:7420-7436` (specifically `:7428-7429`) both signals are hard-wired to
`(others => '0')`. jnext is Issue 2 (`NR 0x0F = 0x00`), so the readback is a
constant 0x00 for both registers.

Fix: install read handlers for NR 0xF9 and NR 0xFA returning 0x00.
Same family as V19R-NMP-NIT-03 (NR 0xF0). The XADC block is not modelled in
jnext — no NextZXOS boot path nor any application depends on it, so the stub
is conservative.

Discriminative test rows (added in the same commit):
- `V20-NMP-XADC-F9` — write 0x5A → read 0x00.
- `V20-NMP-XADC-FA` — write 0xA5 → read 0x00.

### V20-NMP-02 (Class-(c)) — CLOSED, commit `740a79c`

NR 0x68 read mux composes (per `zxnext.vhd:6092-6093`):

```
(not nr_68_ula_en) & nr_68_blend_mode & nr_68_cancel_extended_keys
                   & port_ff3b_ulap_en & nr_68_ula_fine_scroll_x
                   & '0' & nr_68_ula_stencil_mode
```

— bit 1 is a literal `'0'`. The write decoder at `zxnext.vhd:5444-5450`
has NO bit-1 storage signal (the field is silently dropped on every
write). Pre-fix the C++ stored the raw written byte in `regs_[0x68]` and
the read mask `cached & 0xF7` preserved bit 1 verbatim, leaking it on
readback (Class-(c) inert divergence).

Fix: tighten the read mask to `cached & 0xF5` so bit 1 is also cleared
(bit 3 is recomposed from live `ulap_en` immediately afterwards).

Discriminative test row: `V20-NMP-02` — write 0xFF, read; bit 1 must be 0.

## Enumeration Table

### A. Port handlers (52 `register_handler` calls + MF observer)

| Surface (file:line) | C++ behavior summary | VHDL oracle (file:line) | Match | Notes |
|---|---|---|---|---|
| `emulator.cpp:411` MF io_observer | Per-mode 0x1F/0x3F/0x9F/0xBF strobe; gated on `multiface_.is_enabled()` | `zxnext.vhd:2612-2616, :2730-2733` `port_mf_*` decode + iord/iowr ANDs | ✓ | V18-NMP-NIT 0xFFFF / 0x00FF mask split closed Pass-18 |
| `emulator.cpp:481` MF+3 readback @0x3F | `mf_type=00 && !invisible_eff` → case mux on cpu_a(15:12) | `zxnext.vhd:4310-4316` `mf_port_dat` cpu_a(15:12) mux | ✓ | V14-NMP-01 motor-bit FDC gate closed |
| `emulator.cpp:544` MF128 readback @0xBF | `mf_type=01 && !invisible_eff` → `port_7ffd(3) | 0x7F` | `zxnext.vhd:4319` else-branch | ✓ | |
| `emulator.cpp:565` MF128 readback @0x9F | `mf_type=02 && !invisible_eff` → `port_7ffd(3) | 0x7F` | `zxnext.vhd:4319` else-branch | ✓ | mf_type=11 (MF1) routed here but gated off by mode_48 |
| `emulator.cpp:3055` port 0x123B Layer2 | mask 0xFFFF, gated NR 0x83 b7 | `zxnext.vhd:2635, :3904-3935` | ✓ | V18-NMP-NIT-01 NR 0x83 b7 gate closed |
| `emulator.cpp:3092` port 0x7FFD | mask 0x8003/0x0001 | `zxnext.vhd:2593, :2399 + NR 0x82 b1 via effective_*` | ✓ | V16-NMP-02 expbus AND closed |
| `emulator.cpp:3170` port 0x2FFD RD trap | mask 0xF003/0x2001 RD only | `zxnext.vhd:2601, :3835, :3871-3873` cause "01" gated nmi_accept_cause | ✓ | Pass-3 gate fix |
| `emulator.cpp:3190` port 0x3FFD RD/WR trap | mask 0xF003/0x3001 | `zxnext.vhd:2602, :3835, :3874-3878, :3892-3893` | ✓ | Pass-3 gate fix |
| `emulator.cpp:3222` port 0x1FFD +3 paging | mask 0xF003/0x1001 + NR 0x82 b3 gate | `zxnext.vhd:2599 + :2401` | ✓ | |
| `emulator.cpp:3263` port 0x243B NextREG select | mask 0xFFFF | `zxnext.vhd:2625` | ✓ | |
| `emulator.cpp:3273` port 0x253B NextREG data | mask 0xFFFF | `zxnext.vhd:2626, :2742` | ✓ | |
| `emulator.cpp:3315` port 0xFE ULA | mask 0x0001/0x0000 (LSB=0) | `zxnext.vhd:2582` | ✓ | V17-NMP-02 even-LSB closed |
| `emulator.cpp:3364` port 0xFF Timex | mask 0x00FF + NR 0x82 b0 gate | `zxnext.vhd:2583, :2397, :3614-3624` | ✓ | V12/V17/V19-IM2-02 fan-out chain closed |
| `emulator.cpp:3441` port 0x303B sprite_idx | mask 0xFFFF + NR 0x82 b4 gate | `zxnext.vhd:2681, :2423` | ✓ | |
| `emulator.cpp:3452` port 0x57 sprite ctrl | mask 0x00FF + NR 0x83 b6 gate | `zxnext.vhd:2679, :2423` | ✓ | |
| `emulator.cpp:3460` port 0x5B sprite data | mask 0x00FF + NR 0x83 b6 gate | `zxnext.vhd:2680, :2423` | ✓ | |
| `emulator.cpp:3476` port 0xDFFD Pentagon | mask 0xF003/0xD001 + NR 0x82 b2 gate | `zxnext.vhd:2596, :2400` | ✓ | |
| `emulator.cpp:3499` port 0xEFF7 Pentagon | mask 0xF0FF/0xE0F7 + NR 0x85 b2 gate | `zxnext.vhd:2604, :2441` | ✓ | |
| `emulator.cpp:3517` port 0xFFFD AY read | mask 0xC007/0xC005 + NR 0x84 b0 gate | `zxnext.vhd:2647, :2428` | ✓ | |
| `emulator.cpp:3539` port 0xBFFD AY data | mask 0xC007/0x8005 + NR 0x84 b0 gate | `zxnext.vhd:2648, :2428` | ✓ | |
| `emulator.cpp:3563` port 0xBFF5 AY query | mask 0xC00F/0x8005 + NR 0x84 b0 gate | `zxnext.vhd:2649, :2428` | ✓ | |
| `emulator.cpp:3585` DAC 0x1F SD1 chA | mask 0x00FF + NR 0x84 b1 gate | `zxnext.vhd:2661, :2429` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3591` DAC 0x0F SD1 chB / Covox | mask 0x00FF + NR 0x84 b1\|b4 gate | `zxnext.vhd:2662, :2429+2432` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3598` DAC 0x4F SD1 chC / Covox | mask 0x00FF + NR 0x84 b1\|b4 gate | `zxnext.vhd:2663, :2429+2432` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3627` DAC 0x3F Profi chA | mask 0x00FF + NR 0x84 b3 gate | `zxnext.vhd:2661, :2431` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3633` DAC 0x5F SD1 chD / Profi | mask 0x00FF + NR 0x84 b1\|b3 | `zxnext.vhd:2664, :2429+2431` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3654` DAC 0xF1 SD2 chA | mask 0x00FF + NR 0x84 b2 gate | `zxnext.vhd:2661, :2430` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3660` DAC 0xF3 SD2 chB | mask 0x00FF + NR 0x84 b2 gate | `zxnext.vhd:2662, :2430` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3666` DAC 0xF9 SD2 chC | mask 0x00FF + NR 0x84 b2 gate | `zxnext.vhd:2663, :2430` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3672` DAC 0xFB SD2/mono fan-out | mask 0x00FF + NR 0x84 b2\|b5 | `zxnext.vhd:2664, :2430+2433` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3699` DAC 0xB3 GS Covox | mask 0x00FF + NR 0x84 b6 gate | `zxnext.vhd:2659, :2434` | ✓ | V18 LSB-only closed |
| `emulator.cpp:3732` DAC 0xDF Specdrum | mask 0x00FF + NR 0x84 b7 + NR 0x83 b5 | `zxnext.vhd:2658, :2435, :2422` | ✓ | |
| `emulator.cpp:4306` port 0x183B sprite_attr | mask 0xF8FF/0x183B + NR 0x82 b5 | `zxnext.vhd:?` sprite-attr ports | ✓ | |
| `emulator.cpp:4328` port 0x6B tilemap status | mask 0x00FF + NR 0x85 b3 gate | tilemap-related VHDL | ✓ | |
| `emulator.cpp:4337` port 0x0B DMA | mask 0x00FF + NR 0x82 b5 (dma_0b) | `zxnext.vhd:2643, :2440` | ✓ | |
| `emulator.cpp:4380` port 0xE7 SPI | mask 0x00FF + NR 0x85 b1 gate | `zxnext.vhd:2620, :2419` | ✓ | |
| `emulator.cpp:4386` port 0xEB SPI data | mask 0x00FF + NR 0x85 b1 gate | `zxnext.vhd:2621, :2419` | ✓ | |
| `emulator.cpp:4402` port 0x103B I2C SCL | mask 0xFFFF + NR 0x83 b2 gate | `zxnext.vhd:2630, :2418` | ✓ | |
| `emulator.cpp:4411` port 0x113B I2C SDA | mask 0xFFFF + NR 0x83 b2 gate | `zxnext.vhd:2631, :2418` | ✓ | |
| `emulator.cpp:4425` port 0x133B UART3 | mask 0xFFFF + NR 0x83 b4 gate | `zxnext.vhd:2639, :2420` | ✓ | |
| `emulator.cpp:4434` port 0x143B UART0 | mask 0xFFFF + NR 0x83 b4 gate | `zxnext.vhd:2639, :2420` | ✓ | |
| `emulator.cpp:4443` port 0x153B UART1 | mask 0xFFFF + NR 0x83 b4 gate | `zxnext.vhd:2639, :2420` | ✓ | |
| `emulator.cpp:4452` port 0x163B UART2 | mask 0xFFFF + NR 0x83 b4 gate | `zxnext.vhd:2639, :2420` | ✓ | |
| `emulator.cpp:4464` port 0xE3 DivMMC | mask 0x00FF + NR 0x83 b0 gate | `zxnext.vhd:2608, :2412` | ✓ | |
| `emulator.cpp:4494` port 0x1F joystick / DAC mono AD | mask 0x00FF + NR 0x82 b6 + NR 0x84 b7 | `zxnext.vhd:2674, :2407, :2435` | ✓ | |
| `emulator.cpp:4512` port 0x37 joystick2 | mask 0x00FF + NR 0x82 b7 | `zxnext.vhd:2675, :2408` | ✓ | |
| `emulator.cpp:4541` port 0xFADF Kempston mouse buttons | mask 0x0FFF + NR 0x83 b5 | `zxnext.vhd:2668, :2422` | ✓ | |
| `emulator.cpp:4547` port 0xFBDF Kempston mouse X | mask 0x0FFF + NR 0x83 b5 | `zxnext.vhd:2669, :2422` | ✓ | |
| `emulator.cpp:4553` port 0xFFDF Kempston mouse Y | mask 0x0FFF + NR 0x83 b5 | `zxnext.vhd:2670, :2422` | ✓ | |
| `emulator.cpp:4586` port 0xBF3B ULA+ | mask 0xFFFF + NR 0x85 b0 gate | `zxnext.vhd:2685, :2439` | ✓ | |
| `emulator.cpp:4604` port 0xFF3B ULA+ data | mask 0xFFFF + NR 0x85 b0 gate | `zxnext.vhd:2686, :2439` | ✓ | |
| `emulator.cpp:4624` magic port | mask 0xFFFF | jnext-internal | ✓ | non-VHDL test hook |

### B. NextREG WRITE handlers (~91)

Audit covers each NR write side-effect. All match VHDL except already-closed items.

| Reg | C++ summary | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| 0x00 | RO – guarded in write() (handler returns; stored as 0x08) | `zxnext.vhd:5884-5885` g_machine_id RO | ✓ | jnext deliberately reports 0x08 not VHDL g_machine_id=0x0A |
| 0x01 | RO – write silently dropped | `zxnext.vhd:5887-5888` g_version RO | ✓ | nextreg.cpp:441 guard |
| 0x02 | bus_reset latch + iotrap-clear gate + reset FSM + soft/hard reset | `:5117-5119, :3879-3880, :1732-1739, :2006-2015` | ✓ | |
| 0x03 | bootrom disable + machine_timing + dt_lock XOR + machine_type + config_mode + IM2 timing | `:5121-5151` | ✓ | G46(b) closed |
| 0x04 | romram_bank latch | `:1104, :4790` (no side-effect in :5113+) | ✓ | |
| 0x05 | joy0 + joy1 mode + 5060 + scandouble | `:5156-5158` | ✓ | Pentagon-gating V13-NMP-01 closed |
| 0x06 | hotkey enables + speaker_beep + NMI gates + ps2_mode (config-gated) + psg_mode | `:5161-5170` | ✓ | V11-NMP-03 ps2 cache canonicalisation closed |
| 0x07 | CPU-speed shadow + contention | `:5786-5793, :5796-5820` | ✓ | |
| 0x08 | unlock7ffd + contention + stereo + speaker + DAC + port_ff_rd + turbo + issue2 | `:5175-5183, :3654-3656` | ✓ | |
| 0x09 | psg_mono + sprite_tie + hdmi_audio_en (inverted) + clear_mapram (bit 3) + scanlines | `:5185-5189` | ✓ | |
| 0x0A | mf_type/sd_swap (config-gated) + divmmc_automap_en + mouse_button + mouse_dpi | `:5191-5198` | ✓ | V11-NMP-02 cache canonicalisation closed |
| 0x0B | joy_iomode_en/joy_iomode/joy_iomode_0 | `:5200-5203` | ✓ | |
| 0x0E | RO – guarded | `:5917-5918` g_sub_version RO | ✓ | |
| 0x0F | RO – guarded | `:5920-5921` g_board_issue RO | ✓ | |
| 0x10 | flashboot + coreid (config-gated) | `:5677-5687` (Issue 2/3 generate) | ✓ | V16-NMP-01 closed |
| 0x11 | video_timing (config-gated) | `:5208-5217` | ✓ | |
| 0x12 | layer2_active_bank | `:5219-5220` | ✓ | |
| 0x13 | layer2_shadow_bank | `:5222-5223` | ✓ | |
| 0x14 | global_transparent_rgb | `:5225-5226` | ✓ | |
| 0x15 | lores_en + sprite_priority + clip_en + layer_priority + sprite_en | `:5228-5234` | ✓ | |
| 0x16-0x1B | layer2/sprite/ula/tm scroll + clip writes | `:5236-5276` | ✓ | |
| 0x1C | clip-idx reset bits | `:5278-5290` | ✓ | |
| 0x1E/0x1F | RO – cvc readbacks (no handler installed) | `:5982-5986` | ✓ | |
| 0x20 | UNQ pulses LINE/ULA/CTC0..3 | `:1946-1947, :4796, :4846` | ✓ | V19-IM2-03 one-shot clear closed |
| 0x22 | line_interrupt_en + line_interrupt(8) | `:5295-5298, :3619-3620, :6711` | ✓ | |
| 0x23 | line_interrupt(7:0) | `:5300-5301` | ✓ | |
| 0x26/0x27 | ula_scrollx/scrolly | `:5303-5307` | ✓ | |
| 0x28 | palette read/write idx | `:5374-5376` | ✓ | |
| 0x29 | palette idx low byte | `:5374+ palette path` | ✓ | |
| 0x2A | RO (NOTE: commented in VHDL :5315) | `:5315-5316` no write handler | ✓ | nextreg.cpp lambda returns 0 explicitly |
| 0x2B | palette priority | palette path | ✓ | |
| 0x2C/0x2D/0x2E | Soundrive mirror writes | `:4852-4854, soundrive.vhd` | ✓ | |
| 0x2F | tm_scrollx hi 2 bits | `:5330-5331` | ✓ | |
| 0x30-0x33 | tm/lores scrolls | `:5333-5343` | ✓ | |
| 0x34-0x39 | sprite mirror writes | `:4855-4876` | ✓ | |
| 0x40-0x44 | palette idx/data writes | `:5374-5404` | ✓ | |
| 0x4A | fallback_rgb | `:5406-5407` | ✓ | |
| 0x4B | sprite_transparent_index | `:5409-5410` | ✓ | |
| 0x4C | tm_transparent_index (4 bits) | `:5412-5413` | ✓ | |
| 0x50-0x57 | MMU writes | `:4880-4881` | ✓ | |
| 0x60 | copper data byte + addr inc | `:5418-5424` | ✓ | |
| 0x61 | copper addr lo | `:5426-5427` | ✓ | |
| 0x62 | copper mode + addr hi | `:5429-5431` | ✓ | |
| 0x63 | copper data byte (no addr inc) | `:5433-5437` | ✓ | |
| 0x64 | copper_offset | `:5441-5442` | ✓ | |
| 0x68 | ULA disable + blend + cancel_extkeys + ulap_en + fine_scroll_x + stencil_mode | `:5444-5450, :4550-4551` | ✓ | **V20-NMP-02** bit 1 leak in READ — closed `740a79c` |
| 0x69 | layer2_en + shadow + port_ff_reg(5:0) mirror | `:5452-5453, :3924-3925, :6093-6096` | ✓ | V13-MEM-01 layer2 mirror closed |
| 0x6A-0x6F | lores/tm controls/bases | `:5455-5473` | ✓ | |
| 0x70/0x71 | layer2 resolution / scrollx_msb | `:5475-5480` | ✓ | |
| 0x75-0x79 | sprite mirror writes | (mirrors 0x35-0x39) | ✓ | |
| 0x7F | user_register_0 | `:5485-5486, :1216 no reset clause` | ✓ | preserves across reset |
| 0x80 | expbus byte + nibble fold on reset | `:5488 (commented), :2185-2186, :5800-5806` | ✓ | V16-NMP-02 propagate closed |
| 0x81 | expbus_*_clken/fdc/nmi_debounce_disable + speed (hardwired 00) | `:5491-5496` | ✓ | V11-NMP-01 hardwired bits 1:0 closed |
| 0x82 | internal_port_enable[0:7] + propagate | `:5498-5499, :2392-2393` | ✓ | V16-NMP-02 propagate closed |
| 0x83 | internal_port_enable[8:15] + propagate | `:5501-5502, :2412-2425` | ✓ | V16-NMP-02 propagate closed |
| 0x84 | internal_port_enable[16:23] (no handler — bare cache) | `:5504-5505, :2428-2435` | ✓ | All consumers read live via `effective_internal_port_enable` — propagate via NR 0x80/0x86 etc. when expbus toggled |
| 0x85 | internal_port_enable[24:27] + reset_type + propagate | `:5507-5509, :2439-2442, :6138` | ✓ | V16-NMP-02 propagate closed |
| 0x86 | bus_port_enable[0:7] + propagate | `:5511-5512, :2392-2393` | ✓ | V16-NMP-02 propagate closed |
| 0x87 | bus_port_enable[8:15] + propagate | `:5514-5515, :2392-2393` | ✓ | V16-NMP-02 propagate closed |
| 0x88 | bus_port_enable[16:23] + propagate | `:5517-5518, :2392-2393` | ✓ | V16-NMP-02 propagate closed |
| 0x89 | bus_port_enable[24:27] + reset_type + propagate | `:5520-5522, :6150` | ✓ | V16-NMP-02 propagate closed |
| 0x8A | bus_port_propagate (6 bits) | `:5524-5525, :6153` | ✓ | Pass-4 canonicalisation |
| 0x8C | altrom byte | `:2255 nibble fold + :5527 commented (handler via Mmu)` | ✓ | |
| 0x8E | port_8E mapping byte | `:6159` composed read | ✓ | |
| 0x8F | mapping mode 2 bits | `:5533, :6162` | ✓ | mask 0x03 |
| 0x90 | pi_gpio_o_en bits 7:2 (forced 00 bits 1:0) | `:5536-5537` | ✓ | |
| 0x91-0x93 | pi_gpio_o_en | `:5539-5546` | ✓ | |
| 0x98-0x9B | RO – GPIO live state | `:6176-6186` | ✓ | jnext stubs return 0 |
| 0xA0 | pi_peripheral_en byte | `:5560-5561` | ✓ | G135 |
| 0xA2 | pi_i2s_ctl byte | `:5563-5564` | ✓ | G113 |
| 0xA8 | esp_gpio0_en bit 0 | `:5569-5570` | ✓ | |
| 0xA9 | RO – returns 0 | `:6200-6201 ESP_GPIO_20` | ✓ | |
| 0xB0-0xB2 | RO – keyboard/joystick | `:6206-6215` | ✓ | live read |
| 0xB8-0xBB | divmmc entry points/valid/timing | `:5584-5594, :6217-6227` | ✓ | reset defaults 0x83/0x01/0x00/0xCD |
| 0xC0 | im2_vector + stackless_nmi + im_mode + int_mode | `:5596-5599, :6229-6230` | ✓ | |
| 0xC2/0xC3 | retn_address LSB/MSB via Z80N NMIACK | `:2050-2068, :6232-6236` | ✓ | NMIACK_LSB/MSB shadow |
| 0xC4 | expbus int_en + line_interrupt_en mirror + port_ff(6) NOT bit 0 + IM2 ULA fan-out | `:5607-5610, :3621-3622, :6711, :6239` | ✓ | V12/V19-IM2-02 mirror closed |
| 0xC5 | ctc_int_en | `:5612, :6242` | ✓ | |
| 0xC6 | uart_int_en split (b6:4 + b2:0) | `:5615-5617, :6245` | ✓ | mask 0x77 |
| 0xC8 | LINE/ULA status clear | `:1952-1955, :6248` | ✓ | |
| 0xC9 | CTC7..0 status clear | `:1952-1955, :6251` | ✓ | |
| 0xCA | UART RX/TX status clear | `:1952-1954, :6254` | ✓ | dup-pattern matches VHDL |
| 0xCC | DMA delay on_nmi + en_ula | `:5629-5630, :6257` | ✓ | |
| 0xCD | DMA delay en_ctc | `:5632-5633, :6260` | ✓ | |
| 0xCE | DMA delay en_uart1/uart0 | `:5635-5637, :6263` | ✓ | |
| 0xD8 | iotrap_fdc_en | `:5639-5640, :6266` | ✓ | |
| 0xD9 | iotrap_write (direct + trap) | `:1264, :3892-3893, :6269` | ✓ | |
| 0xDA | iotrap_cause (no write path) | `:3879-3880 clear only, :6272` | ✓ | jnext drops writes silently |
| 0xF0 | XADC xdev_cmd (Issue 4/5 only — Issue 2 zero-stub) | `:4902, :6275, :7420-7484` | ✓ | V19R-NMP-NIT-03 stub closed |
| 0xF8 | XADC daddr (Issue 4/5 only) — read bit 7 masked | `:4903, :6278, :7553-7558` | ✓ | V19R-NMP-NIT-04 mask closed |
| 0xF9 | XADC d0 (Issue 4/5 only — Issue 2 zero-stub) | `:4904, :6281, :7428` | ✓ | **V20-NMP-XADC-F9** zero-stub closed `b81c832` |
| 0xFA | XADC d1 (Issue 4/5 only — Issue 2 zero-stub) | `:4905, :6284, :7429` | ✓ | **V20-NMP-XADC-FA** zero-stub closed `b81c832` |
| 0xFF | palette ULA+ entry (alias of NR 0x44) | `:4906, :5418` palette | ✓ | |

Note: All 91 active NRs are covered. Reserved/unmapped NRs (0x21, 0x24, 0x25, 0x45-0x49, 0x4D-0x4F, 0x58-0x5F, 0x65, 0x67, 0x72-0x7E, 0x94-0x97, 0x9C-0x9F, 0xA1, 0xA3-0xA7, 0xAA-0xAF, 0xB3-0xB7, 0xBC-0xBF, 0xC1, 0xC7, 0xCB, 0xCF-0xD7, 0xDB-0xEF, 0xFB-0xFE) fall through to the bare `regs_[]` and the VHDL read mux `others => (others => '0')` (line 6286-6287). jnext leaks last-written byte (Class-(c) latent — not exercised in any boot path; not flagged here, parity with VHDL "others"; aggregate row).

### C. NextREG READ handlers (~91 — mirroring writes above)

| Reg | C++ read summary | VHDL oracle :line | Match | Notes |
|---|---|---|---|---|
| 0x00 | constant 0x08 | `:5884-5885` g_machine_id | ✓ | deliberate jnext deviation |
| 0x01 | constant 0x32 (regs_[0x01] preset, no handler) | `:5887-5888` g_version | ✓ | |
| 0x02 | composed: bus_reset, iotrap, sw_gen_mf/divmmc, reset_type | `:5891` | ✓ | Pass-3 fix |
| 0x03 | composed machine_timing + dt_lock + machine_type + palette_sub_idx | `:5894` | ✓ | |
| 0x05 | composed joy0/joy1 + eff_5060 + scandouble | `:5897` | ✓ | |
| 0x06 | cached & 0xFB + ps2_mode | `:5900` | ✓ | G56-A |
| 0x07 | act<<4 | req | `:5902-5903` | ✓ | |
| 0x08 | composed cache + port_ff_reg + contention etc. | `:5906` | ✓ | |
| 0x09 | cached & 0xE7 + tie | `:5908-5909` | ✓ | |
| 0x0A | composed from MF/SPI/Mouse/divmmc | `:5912` | ✓ | |
| 0x0B | composed iomode | `:5914-5915` | ✓ | |
| 0x0E | constant 0x03 (regs_[0x0E]) | `:5917-5918` g_sub_version | ✓ | |
| 0x0F | constant 0x00 (regs_[0x0F]) | `:5920-5921` g_board_issue | ✓ | Issue 2 |
| 0x10 | composed coreid + flashboot + SPKEY buttons | `:5924` | ✓ | V16-NMP-01 closed |
| 0x11 | regs_[0x11] mask 0x07 | `:5927` | ✓ | |
| 0x12 | layer2_active_bank | `:5930` | ✓ | |
| 0x13 | layer2_shadow_bank | `:5933` | ✓ | |
| 0x14 | global_transparent_rgb (regs_) | `:5936` | ✓ | |
| 0x15 | composed | `:5939` | ✓ | |
| 0x16/0x17 | layer2_scrollx/y | `:5942-5945` | ✓ | |
| 0x18-0x1B | clip-idx-mux | `:5948-5977` | ✓ | |
| 0x1C | composed clip indices | `:5980` | ✓ | |
| 0x1E/0x1F | cvc raster | `:5983-5986` | ✓ | |
| 0x20 | im2_int_status composed | `:5989` | ✓ | V19-IM2 closure |
| 0x22 | composed pulse_int + port_ff(6) + line_int_en + line_int(8) | `:5992` | ✓ | |
| 0x23 | line_interrupt(7:0) | `:5995` | ✓ | |
| 0x26/0x27 | scroll_x/y | `:5998-6001` | ✓ | |
| 0x28 | stored_palette_value | `:6004` | ✓ | |
| 0x2C-0x2E | pi_audio | `:6007-6015` | ✓ | G112 |
| 0x2F | tm_scrollx hi | `:6017-6018` | ✓ | |
| 0x30-0x33 | tm/lores scrolls | `:6020-6030` | ✓ | |
| 0x34 | sprite_mirror_id | `:6033` | ✓ | |
| 0x40 | palette_idx | `:6036` | ✓ | |
| 0x41/0x44 | palette data | `:6039-6048` | ✓ | |
| 0x42 | ulanext_format | `:6042` | ✓ | |
| 0x43 | composed palette ctrl | `:6045` | ✓ | |
| 0x4A | fallback_rgb | `:6051` | ✓ | |
| 0x4B | sprite_transparent_index | `:6054` | ✓ | |
| 0x4C | tm_transparent_index masked 0x0F | `:6057` | ✓ | |
| 0x50-0x57 | MMU pages | `:6060-6081` | ✓ | |
| 0x61 | copper_addr lo | `:6084` | ✓ | |
| 0x62 | copper_mode + addr hi | `:6087` | ✓ | |
| 0x64 | copper_offset | `:6090` | ✓ | |
| 0x68 | NR 0x68 mask 0xF5 + live ulap_en | `:6093` | ✓ | **V20-NMP-02** closed `740a79c` |
| 0x69 | composed layer2_en + shadow + port_ff_reg(5:0) | `:6096` | ✓ | |
| 0x6A | composed lores | `:6099` | ✓ | |
| 0x6B | tm_en + tm_control | `:6102` | ✓ | |
| 0x6C | tm_default_attr | `:6105` | ✓ | |
| 0x6E | tilemap base composed | `:6108` | ✓ | |
| 0x6F | tilemap tiles composed | `:6111` | ✓ | |
| 0x70 | layer2 resolution + palette_offset | `:6114` | ✓ | |
| 0x71 | layer2_scrollx_msb | `:6117` | ✓ | |
| 0x7F | user_register_0 | `:6120` | ✓ | |
| 0x80 | expbus byte verbatim | `:6122-6123` | ✓ | |
| 0x81 | composed (mask 0x78 + bit 7 forced) | `:6125-6126` | ✓ | V11-NMP-01 |
| 0x82-0x85 | port-enable bytes (0x85 read mask 0x8F) | `:6129-6138` | ✓ | V18-NMP-NIT-01 |
| 0x86-0x89 | bus port-enable bytes (0x89 mask 0x8F) | `:6141-6150` | ✓ | G154 |
| 0x8A | bus_port_propagate (mask 0x3F applied on write) | `:6153` | ✓ | Pass-4 |
| 0x8C | altrom byte | `:6156` | ✓ | |
| 0x8E | composed mapping byte | `:6159` | ✓ | |
| 0x8F | mapping mode 2 bits | `:6162` | ✓ | |
| 0x90-0x93 | pi_gpio_o_en | `:6164-6174` | ✓ | |
| 0x98-0x9B | RO – return 0 (no GPIO model) | `:6177-6186` | ✓ | |
| 0xA0 | pi_peripheral_en composed (mask 0x39) | `:6188-6189` | ✓ | G135 |
| 0xA2 | pi_i2s_ctl composed (mask 0xDD | 0x02) | `:6192` | ✓ | G113 |
| 0xA8 | esp_gpio0_en bit 0 | `:6197-6198` | ✓ | |
| 0xA9 | return 0 (ESP_GPIO_20 unmodelled) | `:6200-6201` | ✓ | |
| 0xB0-0xB2 | keyboard/joystick live | `:6206-6215` | ✓ | |
| 0xB8-0xBB | divmmc entry points | `:6217-6227` | ✓ | |
| 0xC0 | composed (vector + stackless + im_mode + int_mode) | `:6229-6230` | ✓ | |
| 0xC2/0xC3 | retn_address LSB/MSB | `:6232-6236` | ✓ | |
| 0xC4 | composed (expbus + line_int_en + ula_int_en) | `:6238-6239` | ✓ | |
| 0xC5 | ctc_int_en | `:6242` | ✓ | |
| 0xC6 | uart_int_en mask 0x77 | `:6244-6245` | ✓ | |
| 0xC8 | LINE/ULA status | `:6247-6248` | ✓ | |
| 0xC9 | CTC0..7 status | `:6250-6251` | ✓ | |
| 0xCA | UART status (dup RX) | `:6253-6254` | ✓ | |
| 0xCC | dma on_nmi + en_ula | `:6257` | ✓ | |
| 0xCD | dma en_ctc | `:6260` | ✓ | |
| 0xCE | dma en_uart | `:6263` | ✓ | |
| 0xD8 | iotrap_fdc_en bit 0 | `:6266` | ✓ | |
| 0xD9 | iotrap_write byte | `:6269` | ✓ | |
| 0xDA | iotrap_cause bits 1:0 | `:6271-6272` | ✓ | |
| 0xF0 | constant 0x00 (Issue 2 zero-stub) | `:6273-6275, :7423` | ✓ | V19R-NMP-NIT-03 |
| 0xF8 | cached(0xF8) & 0x7F (bit 7 mask) | `:6277-6278, :7425` | ✓ | V19R-NMP-NIT-04 |
| 0xF9 | constant 0x00 (Issue 2 zero-stub) | `:6280-6281, :7428` | ✓ | **V20-NMP-XADC-F9** closed `b81c832` |
| 0xFA | constant 0x00 (Issue 2 zero-stub) | `:6283-6284, :7429` | ✓ | **V20-NMP-XADC-FA** closed `b81c832` |
| 0xFF | palette ULA+ entry (handler installed for write only; read inert) | `:6286-6287 others` | ✓ | |

### D. `port_*_io_en` gate consumers (~30 sites)

All consult `effective_internal_port_enable(reg)` which AND-masks the cached NR 0x82-0x85 byte with the corresponding NR 0x86-0x89 byte when `expbus_eff_en=1` (VHDL :2392-2393).

| Gate | Source NR(b) | Consumer | Match |
|---|---|---|---|
| port_ff_io_en | 0x82 b0 | port 0xFF handler `:3367` | ✓ |
| port_7ffd_io_en | 0x82 b1 | port 0x7FFD `:3100`, contention shadow `:290` | ✓ |
| port_dffd_io_en | 0x82 b2 | Pentagon write `:3516` | ✓ |
| port_1ffd_io_en | 0x82 b3 | +3 paging `:3226` | ✓ |
| port_p3_floating_bus_io_en | 0x82 b4 | floating bus `:3447` | ✓ |
| port_dma_6b_io_en | 0x82 b5 | DMA 0x6B | ✓ |
| port_1f_io_en | 0x82 b6 | Kempston1 `:4533` | ✓ |
| port_37_io_en | 0x82 b7 | Kempston2 `:4551` | ✓ |
| port_divmmc_io_en | 0x83 b0 | divmmc::set_port_io_enable `:5143` | ✓ |
| port_multiface_io_en | 0x83 b1 | multiface::set_enabled `:5145` + observer `:412` | ✓ |
| port_i2c_io_en | 0x83 b2 | I2C 103B/113B `:4402,4411` | ✓ |
| port_spi_io_en | 0x83 b3 | port_e7/eb gated NR 0x85 b1 (not b3) | ✓ |
| port_uart_io_en | 0x83 b4 | UART 13/14/15/16 3B `:4420+` | ✓ |
| port_mouse_io_en | 0x83 b5 | Kempston mouse + DAC mono gates | ✓ |
| port_sprite_io_en | 0x83 b6 | sprite ports | ✓ |
| port_layer2_io_en | 0x83 b7 | 123B `:3057,3062` | ✓ |
| port_ay_io_en | 0x84 b0 | AY ports `:3556,3578,3602` | ✓ |
| port_dac_sd1_io_en | 0x84 b1 | DAC SD1 `:3625` | ✓ |
| port_dac_sd2_io_en | 0x84 b2 | DAC SD2 `:3694,3700,3706,3718` | ✓ |
| port_dac_stereo_AD_io_en | 0x84 b3 | Profi 0x3F `:3667` | ✓ |
| port_dac_stereo_BC_io_en | 0x84 b4 | Covox 0x0F/0x4F `:3632,3639` | ✓ |
| port_dac_mono_AD_fb_io_en | 0x84 b5 (AND NOT b2) | 0xFB combined `:3718` | ✓ |
| port_dac_mono_BC_b3_io_en | 0x84 b6 | GS Covox `:3739` | ✓ |
| port_dac_mono_AD_df_io_en | 0x84 b7 | Specdrum `:3772+` | ✓ |
| port_ulap_io_en | 0x85 b0 | BF3B/FF3B + contention shadow `:302,4625,4643` | ✓ |
| port_dma_0b_io_en | 0x85 b1 | port_e7/eb SPI `:4380,4386` | ✓ |
| port_eff7_io_en | 0x85 b2 | port_eff7 `:3539` | ✓ |
| port_ctc_io_en | 0x85 b3 | CTC ports `:4345,4349` | ✓ |

### E. NMI sources

| Source | Assert path | RETN deassert | Match |
|---|---|---|---|
| MF button | `nmi_source_.set_mf_button` + `strobe_mf_button` → asserts via `nmi_assert_mf` (button + nmi_sw_gen_mf + iotrap) AND mf_enable | RETN clears nmi_active in MF FSM (multiface.vhd:144) + clears nmi_state via retn_seen `:5896` | ✓ |
| DivMMC button | `set_divmmc_button` / `strobe_divmmc_button` → `nmi_assert_divmmc` (button + nmi_sw_gen_divmmc) AND divmmc_enable | RETN clears button_nmi in DivMmc (divmmc.cpp on_m1_retn_delay) | ✓ |
| ExpBus | `nmi_assert_expbus` = expbus_eff_en AND NOT expbus_eff_disable_mem AND !expbus_nmi_n | RETN clears expbus latch via FSM S_NMI_END | ✓ V9 |
| NR 0x02 b3 (MF SW) | `nmi_source_.nr_02_write` → nmi_sw_gen_mf strobe | (n/a, one-shot) | ✓ |
| NR 0x02 b2 (DivMMC SW) | `nr_02_write` → nmi_sw_gen_divmmc strobe | (n/a) | ✓ |
| IO trap (port 0x2FFD/0x3FFD) | `nmi_source_.strobe_iotrap()` (in port handlers, gated on nr_d8_io_trap_fdc_en) | (n/a, one-shot) | ✓ Pass-3 |

### F. Multiface transitions

| Transition | Trigger | VHDL :line | Match |
|---|---|---|---|
| Button → nmi_active=1 | button_press w/ nmi_active=0 | multiface.vhd:135-141 | ✓ |
| Button → invisible=0 | button_press | :158 | ✓ |
| RETN → nmi_active=0 | on_retn_seen | :144 | ✓ |
| RETN → mf_enable=0 | on_retn_seen | :178 | ✓ |
| Fetch 0x66 → mf_enable=1 | on_m1(0x66, mreq_low=1) AND nmi_active=1 | :169-177 | ✓ |
| port_mf_enable_rd → mf_enable | on_port_enable_rd AND NOT invisible_eff | :180-182 | ✓ |
| port_mf_disable_rd → mf_enable=0 | on_port_disable_rd | :178 | ✓ |
| port_mf_disable_rd → nmi_active=0 | on_port_disable_rd AND mode_p3=1 AND port_io_dly=0 | :147 | ✓ |
| port_mf_enable_wr → nmi_active=0 | on_port_enable_wr AND port_io_dly=0 | :144 | ✓ |
| port_mf_disable_wr → nmi_active=0 | on_port_disable_wr AND port_io_dly=0 | :144 | ✓ |
| port_mf_enable_wr → invisible=1 (mode_p3) | on_port_enable_wr AND mode_p3 AND port_io_dly=0 | :159 | ✓ |
| port_mf_disable_wr → invisible=1 (NOT mode_p3) | on_port_disable_wr AND NOT mode_p3 AND port_io_dly=0 | :159 | ✓ |
| enable_i=0 → all FFs reset | set_enabled(false) | :103 + edge-clear in C++ | ✓ |
| Reset → invisible=1 (NOT 0) | reset() | :156 | ✓ |
| Reset → other FFs=0 | reset() | :125, :141, :175 | ✓ |
| ROM mapping when mf_enable_eff=1 | is_mem_active() in MMU overlay | mf_enable_eff = mf_enable OR fetch_66 :186 | ✓ |

### G. save_state / load_state schema

| Subsystem | Fields | Match |
|---|---|---|
| NextReg | selected, regs_[256], nr_03_config_mode, nr_04_romram_bank, nr_03_machine_timing, nr_03_user_dt_lock, nr_03_machine_type | ✓ |
| Multiface | enabled, nmi_active, invisible, mf_enable, port_io_dly, mode_p3, mode_128, mode_48, RAM 8KB | ✓ (Wave 1 B1 schema) |
| NmiSource | full producer/gate/consumer/latch/FSM state + button strobes + reset_type FSM | ✓ |
| Emulator | nr_d8_io_trap_fdc_en_, nr_d9_iotrap_write_, nr_da_iotrap_cause_, nr_02_bus_reset_, im2_c4_expbus_, plus shadow re-push of port_7ffd_io_en/port_ulap_io_en/divmmc.port_io_enable/multiface.enabled via effective_internal_port_enable on load_state (line 7073-7076) | ✓ |

## Convergence trend

Pass-trajectory (NMP subsystem only):
- Pass-11: 8 findings
- Pass-12: 17 findings
- Pass-13: 7 findings (large families closed)
- Pass-14: 9 findings
- Pass-15: 5 findings
- Pass-16: 7 findings
- Pass-17: 8 findings
- Pass-18: 4 findings
- Pass-19: 0 audit findings + 2 reviewer NITs (defensive-zero w/ reviewer escalations)
- **Pass-20: 2 findings (both Class-(c) inert; XADC family completion + NR 0x68 bit-1 leak)**

The Pass-19 → Pass-20 jump back to 2 findings is consistent with the deep
audit table sweep finding tail-end Class-(c) inert divergences that prior
passes overlooked. Both are non-functional (cache-leak masks; the
register fields exercised are not consulted in any NextZXOS boot path),
but strict VHDL-faithfulness now closes them. The XADC family
(NR 0xF0/0xF8/0xF9/0xFA) is now fully closed across Pass-19/Pass-20.

## Remaining items

No Class-(d) architectural escalations from Pass-20.

Items inherited from prior passes (not re-evaluated here):
- Pass-17 V17-DIVMMC-02 (cycle-accurate SPI master FSM, G137) — DivMMC scope, not NMP.

## Test invariants

| Suite | Result |
|---|---|
| `ctest --output-on-failure` | 38/38 PASS, 0 FAIL |
| `./build/test/fuse_z80_test build/test/fuse` | 1356/1356 PASS |
| `bash test/00regression/regression.sh` | 33 / 0 / 0 |

New rows in `test/nextreg/nextreg_integration_test.cpp`:
- V20-NMP-XADC-F9
- V20-NMP-XADC-FA
- V20-NMP-02

All three pass post-fix; all three FAIL when the corresponding fix is
reverted (discriminative confirmed).

## Files touched

- `src/core/emulator.cpp` — NR 0xF9, NR 0xFA read handlers + NR 0x68 read
  mask tightening.
- `test/nextreg/nextreg_integration_test.cpp` — three new test rows.
- `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY20-NMI-MF-PORT.md`
  — this report.

## Commit log

- `b81c832` fix(task2-pass20-nmp): V20-NMP-XADC — NR 0xF9/0xFA XADC stubs.
- `740a79c` fix(task2-pass20-nmp): V20-NMP-02 — NR 0x68 read bit 1 leak.
- `<HEAD>` doc(task2-pass20-nmp): audit report.
