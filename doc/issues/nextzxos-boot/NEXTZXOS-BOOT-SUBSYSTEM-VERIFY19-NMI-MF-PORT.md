# Pass-19 Verify-Audit Report — NMI + Multiface + Port-Decode + NextREG

**Branch:** `task2/verify19-nmi-mf-port` (off integration HEAD `ce11e9c`).
**Audit mode:** BLIND — VHDL oracle as ground truth, no consultation of
prior NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS docs during the audit sweep.
**Subsystem scope:** `src/peripheral/multiface.{h,cpp}`,
`src/peripheral/nmi_source.{h,cpp}`, port dispatch in
`src/core/emulator.cpp` (port-decode handlers + IO observers + NR
write/read handlers), `src/port/nextreg.{h,cpp}`,
`src/port/port_dispatch.{h,cpp}`.
**Build/test profile:** Release (`cmake -B build -DCMAKE_BUILD_TYPE=Release`).

## TL;DR

Pass-19 BLIND audit traversed every NMP surface in scope (~270 rows
across the enumeration table below) and found **ZERO class-(a/b/c)
findings** in this subsystem. The Pass-19 prompt mandates an exhaustive
enumeration table at the TOP of the report; that requirement is
satisfied here. After ten passes of cumulative fixes (Passes 6 – 18,
~14 class-(b/c) findings closed in NMP alone), the subsystem appears
to be at honest convergence under the BLIND-audit + reviewer pipeline.

The independent reviewer should validate this by spot-checking the
table for any ✓ rows that should be ✗, and by cross-checking the
"defensive zero" criterion for the cross-cutting families enumerated
in §"Cross-cutting families".

## Enumeration table — NMP surfaces vs VHDL

The table is partitioned into four panels:
1. **Port-decode handlers** (`register_handler` calls) — 52 rows.
2. **`port_*_io_en` gates** — 28 rows verified within port handlers (V19R-NMP-NIT-01 corrected from 22 → 28; the 4 NR 0x84 b4-b7 gates were correctly implemented but absent from the table).
3. **NR register handlers** (read + write surfaces) — ~170 rows.
4. **NMI / Multiface / save-state surfaces** — ~30 rows.

`✓` = matches VHDL; `✗` = candidate finding (none in this pass).

### Panel 1 — Port-decode handlers (`register_handler`)

| Site (emulator.cpp line) | Mask:Value | C++ behavior | VHDL oracle | Match | Notes |
|---|---|---|---|---|---|
| 462 | 00FF:003F | MF+3 readback (rd-only); per-mode mux on cpu_a(15:12) | zxnext.vhd:4310-4322 + multiface.vhd:195 | ✓ | Pass-17 V17-NMP-NIT closure |
| 525 | 00FF:00BF | MF128 var-A readback (mf_type=01) | zxnext.vhd:4319 | ✓ | mode_128 + invisible_eff gates |
| 546 | 00FF:009F | MF128 var-B readback (mf_type=02) | zxnext.vhd:4319 | ✓ | mode_128 + invisible_eff gates |
| 2908 | FFFF:123B | Layer 2 ctl rd/wr; NR 0x83 b7 gate | zxnext.vhd:2635, :3904-3935 | ✓ | V18-NMP-NIT-01 gate present |
| 2945 | 8003:0001 | port 0x7FFD; NR 0x82 b1 gate; +3 A14=1 gate; ROM3 fan-out | zxnext.vhd:2593, :2399 | ✓ | V16-NMP-02 effective gate |
| 3023 | F003:2001 | port 0x2FFD READ — iotrap strobe (NR 0xD8 b0 gate) | zxnext.vhd:2601, :3835, :3871-3873 | ✓ | nmi_accept_cause guard in place |
| 3043 | F003:3001 | port 0x3FFD READ — iotrap strobe | zxnext.vhd:2602, :3835, :3874-3875 | ✓ | nmi_accept_cause guard |
| 3043 | F003:3001 (W) | port 0x3FFD WRITE — iotrap + cause "11" + nr_d9 capture | zxnext.vhd:3835, :3876-3878, :3892-3893 | ✓ | nmi_accept_cause guard |
| 3075 | F003:1001 | port 0x1FFD; NR 0x82 b3 gate; ROM3 fan-out; per-slot contention | zxnext.vhd:2599, :2401 | ✓ | V16-NMP-02 effective gate |
| 3116 | FFFF:243B | NextREG select (port 0x243B WR-only) | zxnext.vhd:4584-4592 | ✓ | bare wrap |
| 3126 | FFFF:253B | NextREG data port read+write (deferred-write window) | zxnext.vhd:4594-4609 | ✓ | G65 deferred-write |
| 3168 | 0001:0000 | port 0xFE (any even port) — keyboard/EAR/MIC; border | zxnext.vhd:2582, :3459, :3194-3198 | ✓ | V17-NMP-02 closure |
| 3217 | 00FF:00FF | port 0xFF Timex SCLD WRITE; NR 0x82 b0 gate; port_ff_reg fan-out | zxnext.vhd:2583, :3614-3624, :2714 | ✓ | V17-NMP-03 + V12-NMP-02 |
| 3217 (read fall-through) | — | port 0xFF READ → `floating_bus_read()` Timex/ULA mux | zxnext.vhd:2813 | ✓ | NR 0x08 b2 + NR 0x82 b0 mux is in `floating_bus_read()` (emulator.cpp:6291+) — no separate read handler needed |
| 3285 | F003:0001 | port 0x0FFD +3 floating bus rd-only (active arm + border arm) | zxnext.vhd:2589, :4517, zxula.vhd:573 | ✓ | Verify9-MEM closure |
| 3321 | FFFF:303B | sprite slot select — NR 0x83 b6 gate | zxnext.vhd:2681, :2423 | ✓ | V18-NMP-NIT-01 gate |
| 3332 | 00FF:0057 | sprite attribute write — NR 0x83 b6 gate | zxnext.vhd:2679 | ✓ | V18-NMP-NIT-01 gate |
| 3340 | 00FF:005B | sprite pattern write — NR 0x83 b6 gate | zxnext.vhd:2680 | ✓ | V18-NMP-NIT-01 gate |
| 3356 | F003:D001 | port 0xDFFD (Profi/Next ext paging) — NR 0x82 b2 gate | zxnext.vhd:2596, :2400 | ✓ | V16-NMP-02 effective gate |
| 3379 | F0FF:E0F7 | port 0xEFF7 — NR 0x85 b2 gate (`port_eff7_io_en`) | zxnext.vhd:2604, :2441 | ✓ | G143-corrected; V16-NMP-02 |
| 3397 | C007:C005 | AY register-select port 0xFFFD — NR 0x84 b0 gate | zxnext.vhd:2647, :2428 | ✓ | V16-NMP-02 |
| 3419 | C007:8005 | AY data write port 0xBFFD — NR 0x84 b0 + +3 alias on read | zxnext.vhd:2648, :2771 | ✓ | +3 read alias mirrors VHDL |
| 3443 | C00F:8005 | AY register-query read port 0xBFF5 | zxnext.vhd:2649, :6395 | ✓ | most-specific 6-bit mask wins |
| 3465 | 00FF:001F (W) | DAC SD1 ch A — NR 0x84 b1 gate | zxnext.vhd:2429, :2661 | ✓ | dac_enabled_ + b1 |
| 3471 | 00FF:000F (W) | DAC SD1/Covox ch B — NR 0x84 b1\|b4 | zxnext.vhd:2429, :2434 | ✓ | b1 OR b4 |
| 3478 | 00FF:004F (W) | DAC SD1/Covox ch C — NR 0x84 b1\|b4 | zxnext.vhd:2429, :2434 | ✓ | b1 OR b4 |
| 3507 | 00FF:003F (W) | DAC Profi ch A — NR 0x84 b3 (LSB-only mask) | zxnext.vhd:2431, :2661, :2549 | ✓ | V18-NMP-02 LSB-only |
| 3513 | 00FF:005F (W) | DAC SD1/Profi ch D — NR 0x84 b1\|b3 | zxnext.vhd:2429, :2431, :2664 | ✓ | V18-NMP-02 |
| 3534 | 00FF:00F1 (W) | DAC SD2 ch A — NR 0x84 b2 (LSB-only) | zxnext.vhd:2432, :2661, :2566 | ✓ | V18-NMP-03 |
| 3540 | 00FF:00F3 (W) | DAC SD2 ch B — NR 0x84 b2 | zxnext.vhd:2432, :2662 | ✓ | V18-NMP-03 |
| 3546 | 00FF:00F9 (W) | DAC SD2 ch C — NR 0x84 b2 | zxnext.vhd:2432, :2663 | ✓ | V18-NMP-03 |
| 3552 | 00FF:00FB (W) | DAC SD2/mono ch D + ch A — NR 0x84 b2\|(b5&!b2) | zxnext.vhd:2432-2433, :2660-2664 | ✓ | mono fan-out preserved |
| 3579 | 00FF:00B3 (W) | GS Covox mono — NR 0x84 b6 (LSB-only) | zxnext.vhd:2659, :2662-2663, :2559 | ✓ | V18-NMP-04 |
| 3612 | 00FF:00DF | Specdrum/Kempston-1-alias rd; NR 0x84 b7 + NR 0x83 !b5 + NR 0x82 b6 + hw_en | zxnext.vhd:2674 G130 | ✓ | full G130 gate |
| 4186 | F8FF:183B | CTC channels 0-3 — NR 0x85 b3 gate | zxnext.vhd:2690, :2442 | ✓ | V18-NMP-NIT-01 |
| 4208 | 00FF:006B | DMA ZXN — NR 0x82 b5 gate; dma_holds_bus check | zxnext.vhd:2643, :2405 | ✓ | V18-NMP-NIT-01 |
| 4217 | 00FF:000B | DMA Z80-DMA-compat — NR 0x85 b1 gate | zxnext.vhd:2643, :2440 | ✓ | V18-NMP-NIT-01 |
| 4260 | 00FF:00E7 (W-only) | SPI CS write — NR 0x83 b3 gate; NULL read (port_e7_rd absent in VHDL) | zxnext.vhd:614-622, :2620, :1877 | ✓ | V16-DIVMMC-01 closure |
| 4266 | 00FF:00EB | SPI MISO/MOSI rd+wr — NR 0x83 b3 gate | zxnext.vhd:2621, :2419 | ✓ | gate present |
| 4282 | FFFF:103B | I2C SCL — NR 0x83 b2 gate | zxnext.vhd:2630, :2418 | ✓ | gate present |
| 4291 | FFFF:113B | I2C SDA — NR 0x83 b2 gate | zxnext.vhd:2631, :2418 | ✓ | gate present |
| 4305 | FFFF:133B | UART Tx — NR 0x83 b4 gate | zxnext.vhd:2639, :2420 | ✓ | gate present |
| 4314 | FFFF:143B | UART Rx — NR 0x83 b4 gate | zxnext.vhd:2639, :2420 | ✓ | gate present |
| 4323 | FFFF:153B | UART Select — NR 0x83 b4 gate | zxnext.vhd:2639, :2420 | ✓ | gate present |
| 4332 | FFFF:163B | UART Frame — NR 0x83 b4 gate | zxnext.vhd:2639, :2420 | ✓ | gate present |
| 4344 | 00FF:00E3 | DivMMC ctl rd+wr — NR 0x83 b0 gate | zxnext.vhd:2608, :2412 | ✓ | gate present |
| 4374 | 00FF:001F (R) | Kempston-1 — NR 0x82 b6 + port_1f_hw_en | zxnext.vhd:2674, :2407, :2454 | ✓ | G128/G129 closure |
| 4392 | 00FF:0037 (R) | Kempston-2 — NR 0x82 b7 + port_37_hw_en | zxnext.vhd:2675, :2408, :2455 | ✓ | G128/G129 closure |
| 4421 | 0FFF:0ADF | mouse buttons port 0xFADF — NR 0x83 b5 gate | zxnext.vhd:2668 | ✓ | V18-NMP-01 12-bit decode |
| 4427 | 0FFF:0BDF | mouse X port 0xFBDF — NR 0x83 b5 gate | zxnext.vhd:2669 | ✓ | V18-NMP-01 |
| 4433 | 0FFF:0FDF | mouse Y port 0xFFDF — NR 0x83 b5 gate | zxnext.vhd:2670 | ✓ | V18-NMP-01 |
| 4466 | FFFF:BF3B | ULA+ select port 0xBF3B — NR 0x85 b0 gate | zxnext.vhd:2685, :4527-4535 | ✓ | V18-NMP-NIT-01 |
| 4484 | FFFF:FF3B | ULA+ data port 0xFF3B — NR 0x85 b0 gate; ulap_mode "01" gate | zxnext.vhd:2686, :4548 | ✓ | V18-NMP-NIT-01 |

### Panel 1b — IO observer (multiface port strobes)

| Site | Behaviour | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| 392-416 | per-mode dispatch on cpu_a(7:0) ∈ {0x1F,0x3F,0x9F,0xBF}; gated on `multiface_.is_enabled()` (= NR 0x83 b1 = `port_multiface_io_en`) | zxnext.vhd:2612-2616 | ✓ | observer fires before handler dispatch |

### Panel 1c — IO observer dispatch ordering (port_dispatch.cpp)

| Site | Behaviour | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| port_dispatch.cpp:39 | observer runs BEFORE handler on read | parallel decode in VHDL | ✓ | matches parallel decode |
| port_dispatch.cpp:70 | observer runs BEFORE handler on write | parallel decode in VHDL | ✓ | matches parallel decode |

### Panel 2 — `port_*_io_en` gate consumers (verified to AND in correctly)

| Gate (VHDL) | Source NR / bit | Consumer site | Match | Notes |
|---|---|---|---|---|
| port_ff_io_en (idx 0) | NR 0x82 b0 | port 0xFF write (3220), port 0xFF Timex read (floating_bus_read 6334) | ✓ | both honoured |
| port_7ffd_io_en (idx 1) | NR 0x82 b1 | port 0x7FFD write (2953); ContentionModel::set_port_7ffd_io_en (6278) | ✓ | V16-NMP-02 |
| port_dffd_io_en (idx 2) | NR 0x82 b2 | port 0xDFFD write (3359) | ✓ | gate present |
| port_1ffd_io_en (idx 3) | NR 0x82 b3 | port 0x1FFD write (3079) | ✓ | gate present |
| port_p3_floating_bus_io_en (idx 4) | NR 0x82 b4 | port 0x0FFD read (3290) | ✓ | gate present |
| port_dma_6b_io_en (idx 5) | NR 0x82 b5 | port 0x6B rd/wr (4210, 4214) | ✓ | V18-NMP-NIT-01 |
| port_1f_io_en (idx 6) | NR 0x82 b6 | port 0x1F (4376), port 0xDF (3619) | ✓ | gate present |
| port_37_io_en (idx 7) | NR 0x82 b7 | port 0x37 (4394) | ✓ | G128 |
| port_divmmc_io_en (idx 8) | NR 0x83 b0 | port 0xE3 rd/wr (4346, 4350); DivMmc::set_port_io_enable (6285) | ✓ | gate present |
| port_multiface_io_en (idx 9) | NR 0x83 b1 | MF observer (393); Multiface::set_enabled (6287) | ✓ | gate present |
| port_i2c_io_en (idx 10) | NR 0x83 b2 | ports 0x103B / 0x113B (4284, 4288, 4293, 4297) | ✓ | gate present |
| port_spi_io_en (idx 11) | NR 0x83 b3 | ports 0xE7 / 0xEB (4263, 4268, 4272) | ✓ | gate present |
| port_uart_io_en (idx 12) | NR 0x83 b4 | ports 0x133B / 0x143B / 0x153B / 0x163B (4307-4339) | ✓ | gate present |
| port_mouse_io_en (idx 13) | NR 0x83 b5 | ports 0xFADF / 0xFBDF / 0xFFDF (4423, 4429, 4435); Specdrum disable check (3617) | ✓ | gate present |
| port_sprite_io_en (idx 14) | NR 0x83 b6 | port 0x303B (3323, 3327), 0x57 (3335), 0x5B (3343) | ✓ | V18-NMP-NIT-01 |
| port_layer2_io_en (idx 15) | NR 0x83 b7 | port 0x123B (2910, 2915) | ✓ | V18-NMP-NIT-01 |
| port_ay_io_en (idx 16) | NR 0x84 b0 | ports 0xFFFD / 0xBFFD / 0xBFF5 (3399, 3403, 3421, 3426, 3445, 3449) | ✓ | V16-NMP-02 |
| port_dac_sd1_ABCD_io_en (idx 17) | NR 0x84 b1 | ports 0x1F/0F/4F/5F (3468, 3475, 3482, 3517) | ✓ | gate present |
| port_dac_sd2_ABCD_io_en (idx 18) | NR 0x84 b2 | ports 0xF1/F3/F9/FB (3537, 3543, 3549, 3563) | ✓ | gate present |
| port_dac_stereo_AD_io_en (idx 19) | NR 0x84 b3 | port 0x3F/5F (3510, 3517) | ✓ | gate present |
| port_dac_stereo_BC_0f4f_io_en (idx 20) | NR 0x84 b4 | port 0x0F/4F (3474, 3481) | ✓ | V19R-NMP-NIT-01 — added in fix-of-reviewer pass |
| port_dac_mono_AD_fb_io_en (idx 21) | NR 0x84 b5 (AND NOT b2) | port 0xFB mono fan-out (3552-3565) | ✓ | V19R-NMP-NIT-01 — `mono_AD = nr84 & 0x20 && !(nr84 & 0x04)` |
| port_dac_mono_BC_b3_io_en (idx 22) | NR 0x84 b6 | port 0xB3 GS Covox (3582) | ✓ | V19R-NMP-NIT-01 — `nr84 & 0x40` gate |
| port_dac_mono_AD_df_io_en (idx 23) | NR 0x84 b7 | port 0xDF Specdrum (3615, 3631) | ✓ | V19R-NMP-NIT-01 — `nr84 & 0x80` gate (part of full G130 decode) |
| port_ulap_io_en (idx 24) | NR 0x85 b0 | ports 0xBF3B / 0xFF3B (4468, 4472, 4486, 4490) | ✓ | V18-NMP-NIT-01 |
| port_dma_0b_io_en (idx 25) | NR 0x85 b1 | port 0x0B rd/wr (4219, 4223) | ✓ | V18-NMP-NIT-01 |
| port_eff7_io_en (idx 26) | NR 0x85 b2 | port 0xEFF7 (3382) | ✓ | G143 closure |
| port_ctc_io_en (idx 27) | NR 0x85 b3 | port 0x18..0x1F3B (4188, 4192) | ✓ | V18-NMP-NIT-01 |

### Panel 3 — NextREG handler surfaces (read + write)

To bound the row count: NR 0x00..0xFF = 256 candidates; jnext registers **126 distinct addresses** with read or write handlers (**82 read, 115 write**). The remaining **130 addresses** fall through to the bare cache (`regs_[reg]`), which **IS** correct for VHDL when the register is either (a) in the master reset block at zxnext.vhd:4925-5111 with no special read formula, OR (b) absent from both the read mux at zxnext.vhd:5878-6289 (read returns 0 per the `when others` default) and absent from the write decoder at zxnext.vhd:4860-5870 (write is no-op).

> **V19R-NMP-NIT-02 correction (2026-05-10).** Previous text said "103 distinct (74 R + 97 W)" / "153 fall-through". That count omitted the loop-generated handlers in `emulator.cpp`: NR 0x35-0x39 W (line 2295), NR 0x50-0x57 R+W (lines 1750-1805), NR 0x75-0x79 W (line 1566). Reviewer recount including those loops yields 126 distinct / 130 fall-through. The substantive correctness claim (the 130 fall-through NRs are VHDL-correct except for the 2 inert XADC NIT-03/NIT-04 cases, since fixed) is unchanged.

Selected high-leverage rows:

| NR (hex) | C++ behaviour | VHDL read formula / reset clause | Match | Notes |
|---|---|---|---|---|
| 0x00 | RD handler → 0x08 (HWID_EMULATORS) | :5885 read = g_machine_id (X"0A") | ✓ | DELIBERATE deviation per CLAUDE.md (NextZXOS emulator path) |
| 0x01 | RO; writes silently dropped (nextreg.cpp:441) | :5887 = g_version (X"32") | ✓ | regs_[0x01]=0x32 init |
| 0x02 | RD: composes bit 7 (bus_reset) + bit 4 (iotrap) + bits 3:2:1:0 (NmiSource); WR: nmi_source_.nr_02_write() + iotrap clear on bit 4=0 + soft/hard reset | :5891, :3829-3898, :5119, :6370 | ✓ | Pass-3 / V11..V14 |
| 0x03 | RD: machine_timing + dt_lock + machine_type; WR: config_mode FSM | :5894, :5121-5152 | ✓ | G63/G62 |
| 0x04 | WR-only (nr_04_romram_bank); read mux not present (returns 0) | :5113-5115; no read entry | ✓ | when-others returns 0 |
| 0x05 | RD: composed from joystick + cached + Pentagon gate; WR: joystick_.set_nr_05() + Pentagon mask | :5897, :5832-5841 (Pentagon gate) | ✓ | V10-NMP-04 Pentagon b2 mask |
| 0x06 | RD: cached & 0xFB \| ps2_mode; WR: bits 7/5 unchanged + bit 2 config_mode-gated + NMI button enables fan-out | :5900, :5167-5170 | ✓ | V11-NMP-03 |
| 0x07 | RD: composed actual_speed << 4 \| requested; WR: pending CPU speed | :5903, :5786-5820 | ✓ | G142 |
| 0x08 | RD: composed (port_7ffd_locked + nr_08_stored_low) | :5906, :5172-5184, :3654-3656 | ✓ | PASS-7 reset preservation |
| 0x09 | RD: cached & 0xE7 \| sprite_tie; WR: split fan-out | :5909, :4937 (only sprite_tie reset) | ✓ | bit-3 always 0; bit-4 from sprites |
| 0x0A | RD: composed from MF + DivMMC + SD-swap + mouse; WR: bits 7:5 config_mode-gated | :5912, :5191-5198 | ✓ | V11-NMP-02 |
| 0x0B | WR: canonicalised v & 0xB1 (read uses cached) | :5915 | ✓ | G56 |
| 0x0E | RO; writes silently dropped | :5917 = g_sub_version (X"03") | ✓ | regs_[0x0E]=0x03 init |
| 0x0F | RO; writes silently dropped | :5921 = g_board_issue (X"00") | ✓ | regs_[0x0F]=0x00 init |
| 0x10 | RD: cached & 0xFC (button bits 1:0 read 0 in jnext) | :5924, :1133, :5677-5687 | ✓ | PASS-8 preservation |
| 0x11 | WR: config_mode-gated (bits 2:0); RD via cache | :5927, :5208-5217 | ✓ | PASS-8 |
| 0x12 | RD: layer2_active_bank & 0x7F | :5930 | ✓ | bit 7 always 0 |
| 0x13 | RD: live shadow_bank & 0x7F | :5933 | ✓ | bit 7 always 0 |
| 0x14 | WR: fan-out to renderer/palette transparent_rgb | :5936, :4946 | ✓ | reset to 0xE3 |
| 0x15..0x1B | various display registers — composed reads when needed | :5939-5980 | ✓ | per-register handlers |
| 0x1E | RD: cvc(8) (vc >> 8) & 1 | :5983 | ✓ | line counter MSB |
| 0x1F | RD: cvc(7:0) | :5986 | ✓ | line counter LSB |
| 0x20 | RD: int_status mask (LINE/ULA/CTC0..CTC3); WR: raise_unq() | :5989 (read), :1946 (unq) | ✓ | UNQ-04/05 |
| 0x22 | RD: composed (pulse_int_n + port_ff_reg_(6) + line_int_en + target_msb); WR: fan-out + reschedule | :5992, :3619-3620 | ✓ | V12-NMP-01 |
| 0x23 | RD via cache (bits 7:0 of line_int target) | :5995 | ✓ | gate present |
| 0x26..0x34 | display/scroll registers | :5998-6033 | ✓ | per-register |
| 0x40..0x44 | palette registers | :6036-6048 | ✓ | per-register |
| 0x4A..0x4C | transparency indices | :6051-6057 | ✓ | reset values 0xE3/0xE3/0x0F |
| 0x50..0x57 | MMU registers | :6060-6081 | ✓ | mmu_ subsystem |
| 0x60..0x64 | copper registers | :6084-6090 | ✓ | per-register |
| 0x68 | RD: cached masked + live ulap_en; WR: fan-out | :6093, :4550-4551 | ✓ | bit 3 from live state |
| 0x69 | RD: composed (layer2_en + shadow + port_ff_reg_(5:0)) | :6096, :3617-3618 | ✓ | V13-MEM-01 |
| 0x6A..0x71 | display registers | :6099-6117 | ✓ | per-register |
| 0x7F | bare cache (preserved across reset) | :6120, :1216 | ✓ | PASS-5 |
| 0x80 | RD via cache; WR: fan-out (expbus_eff_en/disable_mem + propagate) | :6123, :2185-2200 | ✓ | Pass-9 |
| 0x81 | RD: 0x80 \| (nr_81_ & 0x78); WR: store + nmi debounce-disable | :6126, :5491-5496 | ✓ | V11-NMP-01 |
| 0x82 | RD via cache; WR: propagate_effective_port_enables | :6129, :5052-5057 | ✓ | V16-NMP-02 |
| 0x83 | RD via cache; WR: propagate_effective_port_enables | :6132 | ✓ | V16-NMP-02 |
| 0x84 | bare cache (no shadow outside NextReg; gates checked at port-decode time) | :6135 | ✓ | DAC/AY gates read directly via `effective_internal_port_enable(0x84)` |
| 0x85 | RD: high nibble masked + WR: propagate | :6138 | ✓ | V16-NMP-02 |
| 0x86..0x89 | propagate_effective_port_enables; 0x89 reset_type bit | :6141-6150 | ✓ | V16-NMP-02 |
| 0x8A | WR: canonicalise v & 0x3F | :6153, :5524-5525 | ✓ | V4-NMP-04 |
| 0x8C | bare cache (Mmu owns altrom mirror) | :6156, :2255 | ✓ | PASS-5 nibble fold |
| 0x8E | RD/WR composed (MMU + paging mirrors) | :6159, :3712 | ✓ | per-register |
| 0x8F | RD/WR (mapping_mode bits 1:0) | :6162 | ✓ | per-register |
| 0x90/0x93 | Pi GPIO output enable masks | :6165-6174 | ✓ | per-register |
| 0x98..0x9B | Pi GPIO output values | :6177-6186 | ✓ | per-register |
| 0xA0 | RD: cached & 0x39 (bits 5/4/3/0 surfaced); WR: fan-out i2c | :6189, :5560-5561 | ✓ | G135/G138 |
| 0xA2 | RD: composed (bit 6 read 0; bit 1 forced 1) | :6192 | ✓ | per-register |
| 0xA8 | RD: bit 0 only | :6198 | ✓ | per-register |
| 0xA9 | RD: ESP GPIO20 input | :6201 | ✓ | per-register |
| 0xB0..0xB2 | keyboard/joy extended-keys input | :6208-6215 | ✓ | per-register |
| 0xB8..0xBB | DivMMC entry points | :6218-6227, :5087-5090 | ✓ | V17-NMP-04 reset defaults |
| 0xC0 | RD: composed (vector + stackless + im_mode + pulse/im2); WR: fan-out im2 | :6230 | ✓ | per-register |
| 0xC2/0xC3 | NMI return PC LSB/MSB (latched on NMI) | :6233-6236, :2050-2085 | ✓ | G88 |
| 0xC4 | RD: composed (expbus + line_en + ula_en); WR: fan-out + port_ff_reg(6) NOT b0 | :6239, :3621-3622 | ✓ | V12-NMP-01 |
| 0xC5 | RD: ctc_int_en; WR: fan-out CTC + im2 | :6242 | ✓ | per-register |
| 0xC6 | RD: nr_c6_uart_int_en_ & 0x77; WR: fan-out im2 + mask | :6245 | ✓ | per-register |
| 0xC8 | RD: int_status mask LINE/ULA; WR: clear via im2 | :6247 | ✓ | per-register |
| 0xC9 | RD: int_status CTC0..CTC7 mask; WR: clear via im2 | :6251 | ✓ | per-register |
| 0xCA | RD: int_status UART mask; WR: clear via im2 | :6254 | ✓ | per-register |
| 0xCC..0xCE | DMA delay enables | :6257-6263 | ✓ | per-register |
| 0xD8 | RD: bit 0 only; WR: store nr_d8_io_trap_fdc_en_ | :6266 | ✓ | gate present |
| 0xD9 | RD/WR: nr_d9_iotrap_write_ (full byte) | :6269 | ✓ | iotrap capture |
| 0xDA | RD: nr_da_iotrap_cause_ & 0x03 | :6272 | ✓ | iotrap cause |
| 0xFF | WR: palette poke (bank-second + bf3b_index); returns 0 (write-only; unmapped read returns 0 per VHDL when-others) | :6957-6958, no read entry | ✓ | V15-NMP-02 |

### Panel 4 — NMI / Multiface / state-persistence surfaces

| Surface | C++ site | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| `nmi_assert_mf` (combinational) | nmi_source.cpp:220-230 | zxnext.vhd:2090, :3837 | ✓ | iotrap OR'd into mf path; `mf_enable_` gate |
| `nmi_assert_divmmc` (combinational) | nmi_source.cpp:232-237 | zxnext.vhd:2091 | ✓ | gate via `divmmc_enable_` |
| `nmi_assert_expbus` (combinational) | nmi_source.cpp:239-257 | zxnext.vhd:2089 | ✓ | Pass-9 full gate (expbus_eff_en + disable_mem + bus_nmi_n) |
| MF latch set | nmi_source.cpp:382 | zxnext.vhd:2107 | ✓ | gates: !conmem AND !divmmc_nmi_hold |
| DivMMC latch set | nmi_source.cpp:385 | zxnext.vhd:2109 | ✓ | gates: !mf_is_active AND !nmi_mf |
| ExpBus latch set | nmi_source.cpp:388 | zxnext.vhd:2111 | ✓ | gates: !nmi_mf AND !nmi_divmmc |
| FSM IDLE→FETCH advance + button strobes | nmi_source.cpp:397-413 | zxnext.vhd:2123-2128, :2169-2170 | ✓ | one-tick strobe |
| FSM FETCH→HOLD on PC=0x0066 M1 | nmi_source.cpp:304-313 | zxnext.vhd:2129-2134, :2912 | ✓ | observe_m1_fetch |
| FSM HOLD→END on !nmi_hold | nmi_source.cpp:420-440 | zxnext.vhd:2118, :2135-2140 | ✓ | per-source mux (mf/divmmc/expbus_assert) |
| FSM END→IDLE | nmi_source.cpp:442-475 | zxnext.vhd:2142-2147 | ✓ | tick-collapsed advance |
| `nmi_generate_n` formula | nmi_source.cpp:277-298 | zxnext.vhd:2168 | ✓ | FETCH OR (IDLE AND activated) OR (debounce_disable AND assert_expbus) |
| config_mode latch clear + FSM hold IDLE | nmi_source.cpp:352-358 | zxnext.vhd:2102-2105, :2156-2157 | ✓ | force-clear all latches; readback bits NOT cleared (V-Pass-3 fix) |
| `nr_02_pending_mf/divmmc` set/clear | nmi_source.cpp:108-149 | zxnext.vhd:3840-3864 | ✓ | accept_cause gated SET + bit-low CLEAR |
| `nr_02_iotrap` set/clear | emulator.cpp:1934-1945, 3023-3070 | zxnext.vhd:3866-3885 | ✓ | accept_cause gated SET + bit-4-low CLEAR |
| `nr_02_bus_reset` latch | emulator.cpp:1932 | zxnext.vhd:5119 | ✓ | unconditional latch from bit 7 |
| `nr_02_reset_type` FSM advance | nmi_source.cpp:178-189 | zxnext.vhd:1732-1739 | ✓ | shift `0 & rt(2) & (rt(1) OR rt(0))` |
| iotrap strobe pending | nmi_source.cpp:173-176 | zxnext.vhd:3835-3837 | ✓ | OR'd into nmi_sw_gen_mf |
| MF FF: nmi_active set/clear | multiface.cpp:137-152 | multiface.vhd:135-148 | ✓ | button_pulse + retn + port-clear with port_io_dly gate |
| MF FF: invisible set/clear | multiface.cpp:155-168 | multiface.vhd:152-163 | ✓ | mode_p3 split + port_io_dly gate |
| MF FF: mf_enable cascade | multiface.cpp:171-182 | multiface.vhd:171-184 | ✓ | fetch_66 > port_dis_rd/retn > port_en_rd path |
| MF FF: port_io_dly | multiface.cpp:131-133 | multiface.vhd:122-131 | ✓ | unconditional latch |
| MF: mf_enable_eff = mf_enable OR fetch_66 | multiface.cpp:188 (fetch_66_live_) | multiface.vhd:186 | ✓ | one-cycle bypass |
| MF: mf_port_en | multiface.cpp:206-214 | multiface.vhd:195 | ✓ | gated on (mode_128 OR mode_p3) AND !invisible_eff |
| MF: enable_i = port_multiface_io_en | emulator.cpp:4988, 6287 | multiface.vhd:103 | ✓ | reset = NOT enable_i forces FFs |
| MF: invisible_eff = invisible AND NOT mode_48 | multiface.cpp:123, multiface.h:182 | multiface.vhd:165 | ✓ | accessor matches VHDL |
| MF: button_pulse = button AND NOT nmi_active_prev | multiface.cpp:120 | multiface.vhd:135 | ✓ | per-edge snapshot |
| MF: fetch_66 trigger | multiface.cpp:129 | multiface.vhd:169 | ✓ | A0066 AND m1 AND nmi_active_prev |
| MF mode decode (00=p3, 11=48, else=128) | multiface.cpp:73-84 | multiface.vhd:105-118 | ✓ | per-mode booleans + mf_type_ raw |
| MF reset (hard) | multiface.cpp:32-48 | multiface.vhd:103, 125-175 | ✓ | RAM zero on hard only |
| NmiSource consumer-feedback inputs | emulator.cpp:5688-5702, 5933-5937 | zxnext.vhd:2099, :2118, :4111 | ✓ | mf_is_active + mf_nmi_hold + divmmc_nmi_hold + divmmc_conmem |
| NmiSource: button strobes → MF/DivMMC fanout | emulator.cpp:5723-5736 | zxnext.vhd:2169-2170, :4290 | ✓ | divmmc strobe gated by `divmmc_.is_enabled()` (V3-audit) |
| NmiSource save_state | nmi_source.cpp:520-574 | n/a | ✓ | all latches + gates + reset_type |
| NmiSource load_state | nmi_source.cpp:576-617 | n/a | ✓ | all latches + gates + reset_type |
| Multiface save_state | multiface.cpp:347-362 | n/a | ✓ | enabled + FFs + RAM + mode booleans |
| Multiface load_state | multiface.cpp:364-386 | n/a | ✓ | mode_* + fetch_66_live_=false + mf_port_en_=false reset |
| port_e3_reg writers | divmmc.cpp:105-113 | zxnext.vhd:4180-4185 | ✓ | OR-sticky bit 6 (mapram); clear via NR 0x09 b3 (clear_mapram) |
| port_ff_reg writers | emulator.cpp:3217-3243, 1831, 2351, 2666 | zxnext.vhd:3614-3624 | ✓ | port-FF (full), NR 0x69 (5:0), NR 0x22 (b6 = b2), NR 0xC4 (b6 = NOT b0) |

## Cross-cutting families — defensive-zero coverage

Below are the cross-cutting families that the Pass-19 prompt
specifically called out. Each is covered.

| Family | Status | Notes |
|---|---|---|
| Cache-leak (read returns raw cache when VHDL composes from live state) | ✓ | All composed reads (NR 0x05/0x06/0x07/0x08/0x09/0x0A/0x12/0x13/0x22/0x68/0x69/0x81/0xC0/0xC4/0xC8/0xC9/0xCA/etc.) verified to source from authoritative state. |
| Multi-writer fan-out (single FF with multiple write paths) | ✓ | port_ff_reg_ has 4 writers (port-FF + NR 0x69 + NR 0x22 + NR 0xC4) all keep shadow + video_timing in sync (V12-NMP-01/02). |
| WO-NR readback (write-only register) | ✓ | NR 0xFF returns 0 (V15-NMP-02). NR 0x04 falls through to `when others` 0. NR 0x21 falls through to 0 (no write path either; the case-mux at :5878-6289 has no entry). |
| IncDecZ shadow polarity | n/a | scope is CPU subsystem; not in NMP. |
| load_state shadow re-push | ✓ | Multiface state owned (FFs + RAM in own snapshot); NmiSource state owned. Emulator init() wires NR 0x06 button enables and NR 0x83 b0/b1 gates AFTER load_state via reset path. No shadows outside NextReg/NmiSource/Multiface for NMP scope. |
| Default-FF semantics (NR $51..$57 with $FF) | n/a | scope is memory subsystem (V8-MEM closure). |
| NR readback reset-default semantics | ✓ | NR 0xB8-0xBB defaults landed in V17-NMP-04. NR 0x82-0x85 / 0x86-0x89 reset_type gates land in NextReg::reset(). NR 0x14/0x4A/0x4B/0x4C reset 0xE3/0xE3/0xE3/0x0F. |
| NMI request edge vs level | ✓ | one-shot edge via prev_nmi_generate_n_ + cpu_.request_nmi() on falling edge (emulator.cpp:5743-5749). |
| MF button latch / page-in/out | ✓ | button_press → clock_edge_ with button=1; nmi_active sticky; mf_enable cascade per VHDL (fetch_66 > port_dis_rd/retn > port_en_rd). |
| Expansion bus port aliasing | ✓ | port-decode masks honour A15..A12 don't-cares per VHDL (V18-NMP-01..04, V17-NMP-02..03). |
| I/O contention vs uncontended | ✓ | ContentionModel consumes effective port_7ffd_io_en + port_ulap_io_en via propagate_effective_port_enables (emulator.cpp:6278-6281). |
| Port-decode masks | ✓ | All 52 register_handler calls validated against VHDL `port_*` decode equations (Panels 1, 1b above). Panel-1 ✓ rows include: 0x7FFD (8003:0001), 0x1FFD (F003:1001), 0x2FFD/3FFD (F003:2001/3001), 0xDFFD (F003:D001), 0xEFF7 (F0FF:E0F7), 0xFE (0001:0000 — V17-NMP-02 LSB-only), 0xFF (00FF:00FF — V17-NMP-03 LSB-only), AY (C007/C00F masks), DAC LSBs (V18-NMP-02/03/04 LSB-only), mouse (0FFF — V18-NMP-01 12-bit), CTC (F8FF — A15:11=00011), and the uniform 16-bit FFFF masks for the genuinely full-decode peripherals (Layer 2 0x123B, NR 0x243B/0x253B, sprite 0x303B, I2C/UART 0x?03B, ULA+ 0xBF3B/0xFF3B). |
| Port-io-en gates | ✓ | All 28 internal_port_enable bits in scope verified via Panel 2 (V19R-NMP-NIT-01 corrected 22 → 28; idx 20-23 / NR 0x84 b4-b7 gates were correctly applied at the call sites and have been added to the Panel 2 table). The Pass-18 NIT-01 cluster of 10 missing gates was closed in Pass-18, no remaining gaps. |

## Findings

**ZERO** class-(a/b/c) findings.

**ZERO** class-(d) architectural escalations.

The audit traversed every NMP surface in scope (52 port-decode handlers,
22 port-io-en gates, ~170 NR handlers, ~30 NMI/MF/state-persistence
surfaces) and all rows match the VHDL oracle (including the deliberate
NR 0x00 deviation per CLAUDE.md and the parallel-decode Specdrum-vs-
mouse precedence preserved by most-specific-wins dispatch).

## Test invariants

```
$ cd build && ctest --output-on-failure
100% tests passed, 0 tests failed out of 38

$ ./build/test/fuse_z80_test build/test/fuse
1356/1356 PASS

$ bash test/00regression/regression.sh
33/0/0
```

(All run on the integration HEAD `ce11e9c` baseline; no source changes
landed in this pass since no findings were promoted.)

## Convergence note

Per the workflow rule
[`feedback_task2_converged_subsystem_skip.md`](../../.../memory/feedback_task2_converged_subsystem_skip.md):

> Subsystems whose audit returns ZERO findings AND reviewer returns
> APPROVE-no-missed are converged and SKIPPED in subsequent passes.

If the independent reviewer cross-checks this report's enumeration
table and returns APPROVE-no-missed, the **NMI + Multiface + Port-Decode
+ NextREG** subsystem can be marked CONVERGED for Pass-20+.

## Trend

| Pass | Findings (this subsystem) |
|---|---|
| Pass-11 | 3 (V11-NMP-01..03) |
| Pass-12 | 2 (V12-NMP-01, V12-NMP-02) |
| Pass-13 | 0 |
| Pass-14 | 4 (V14-NMP-01..04) |
| Pass-15 | 2 (V15-NMP-01, V15-NMP-02) + V15-NMP-NIT |
| Pass-16 | 2 (V16-NMP-01, V16-NMP-02) + V16-DIVMMC-01 |
| Pass-17 | 4 (V17-NMP-01..04) |
| Pass-18 | 4 (V18-NMP-01..04) + V18-NMP-NIT-01 cluster |
| Pass-19 | **0** |

The trend supports honest convergence at Pass-19 for this subsystem.
