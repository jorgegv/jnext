# NEXTZXOS Boot Subsystem — Verify-Pass 22 — NMI + Multiface + Port + NextREG

**Branch:** `task2/verify22-nmi-mf-port`
**Integration HEAD (pre-audit):** `4cffca6`
**Audit HEAD (post-fixes, pre-report):** `666a8ff` (V22-NMP-01 fix + tests — SUBSEQUENTLY REJECTED by reviewer)
**Fix-of-reviewer HEAD:** (this commit) — V22-NMP-01 reverted; tests flipped to VHDL-correct contract
**Date:** 2026-05-11
**Reviewer-blindness:** This pass was conducted without reading prior
`doc/issues/nextzxos-boot/` reports until after the table + findings
were complete (per Pass-22 prompt).

## Findings summary

**0 findings (final).** Pass-22 NMP is a **CONVERGENCE PASS** for the
NMI + Multiface + Port + NextREG subsystem.

| ID | Class | Surface | Status | Description |
|---|---|---|---|---|
| V22-NMP-01 | (c) | NR 0xC2 / NR 0xC3 RO-guard | **DISMISSED — false positive per reviewer** | The Pass-22 audit misread VHDL: the commented-out `nr_c2_we`/`nr_c3_we` at zxnext.vhd:5601-5605 are vestigial duplicates inside a SECOND (clocked) decoder process; the ACTIVE strobe assignments are in the FIRST (combinatorial) decoder process at :4894-4895 and are NOT commented out. The elsif arms at :2064-2067 ARE reachable from NextReg-port writes. Pre-fix jnext was VHDL-faithful; the proposed RO-guard would have introduced a NEW divergence. Reverted in fix-of-reviewer commit; 4 of 6 V22-NMP-01 tests flipped to assert the spec-correct contract (writes DO latch). |

**0 effective findings** — class-(c) "finding" was investigated and
withdrawn. No class-(a)/(b)/(c) bugs found this pass.

## Test results (Release build)

| Suite | Pre-fix | Post-fix (initial bad fix) | Post-revert (final) |
|---|---|---|---|
| ctest | 38/38 PASS | 38/38 PASS | 38/38 PASS |
| FUSE Z80 | 1356/1356 PASS | 1356/1356 PASS | 1356/1356 PASS |
| regression.sh | 33/0/0 | 33/0/0 | 33/0/0 |
| nextreg_integration | 272/272 PASS | 278/278 PASS (but 4 enshrined wrong contract) | 278/278 PASS (6 V22-NMP-01 tests assert spec-correct writable contract) |
| port_test | 102/0/1 PASS/Fail/Skip | 102/0/1 | 102/0/1 |
| multiface_test | 49/49 PASS | 49/49 PASS | 49/49 PASS |
| nmi_test | (clean) | (clean) | (clean) |

## Enumeration table

Granularity rules (per Pass-19+ mandate):
- one row per `register_handler` (port-decode bridge)
- one row per NR address (R + W separate)
- one row per `port_*_io_en` gate
- one row per NMI source / MF FSM transition / save-load field

Empty cells forbidden. ✗ = finding, ✓ requires SPECIFIC VHDL line.

### Section A — Port-decode handlers (52 rows from PortDispatch::register_handler)

| # | Mask/Val | Surface | Side | VHDL line | Status | Notes |
|---|---|---|---|---|---|---|
| A01 | 0xFFFF/0x123B | Layer2 ctrl port | RW | zxnext.vhd:2635 | ✓ | port_123b decode + NR 0x83 b7 gate |
| A02 | 0x8003/0x0001 | port 0x7FFD | W | zxnext.vhd:2593 | ✓ | A15=0, A14\|!p3, port_fd, NR 0x82 b1 gate (effective_internal_port_enable) |
| A03 | 0xF003/0x2001 | port 0x2FFD trap | R | zxnext.vhd:2601, 3835 | ✓ | NR 0xD8 b0 gate; nmi_accept_cause-gated cause/D9 capture (Pass-3 fix) |
| A04 | 0xF003/0x3001 | port 0x3FFD trap | RW | zxnext.vhd:2602, 3835 | ✓ | NR 0xD8 b0 gate; cause+D9 capture gated on nmi_accept_cause |
| A05 | 0xF003/0x1001 | port 0x1FFD | W | zxnext.vhd:2599, 2598 | ✓ | A15:12=0001, A1:0=01; NR 0x82 b3 gate |
| A06 | 0xFFFF/0x243B | NextREG select | W | zxnext.vhd:2625 | ✓ | full 16-bit |
| A07 | 0xFFFF/0x253B | NextREG data | RW | zxnext.vhd:2626 | ✓ | deferred-write window per G65 |
| A08 | 0x0001/0x0000 | port 0xFE / ULA | RW | zxnext.vhd:2582 | ✓ | LSB-only even-port match (V17 fix) |
| A09 | 0x00FF/0x00FF | Timex port 0xFF | W | zxnext.vhd:2583 | ✓ | LSB-only (V17-NMP-03); NR 0x82 b0 gate |
| A10 | 0xF003/0x0001 | port 0x0FFD +3 floating | R | zxnext.vhd:2589, 4517 | ✓ | +3-timing + NR 0x82 b4 gate; effective_paging_locked |
| A11 | 0xFFFF/0x303B | sprite slot/status | RW | zxnext.vhd:2681 | ✓ | port_303b; NR 0x83 b6 gate |
| A12 | 0x00FF/0x0057 | sprite attr | W | zxnext.vhd:2680 | ✓ | port_57; NR 0x83 b6 |
| A13 | 0x00FF/0x005B | sprite pattern | W | zxnext.vhd:2680 | ✓ | port_5b; NR 0x83 b6 |
| A14 | 0xF003/0xD001 | port 0xDFFD | W | zxnext.vhd:2596 | ✓ | A15:12=1101, A1:0=01; NR 0x82 b2 gate |
| A15 | 0xF0FF/0xE0F7 | port 0xEFF7 | W | zxnext.vhd:2604 | ✓ | A15:12=1110, port_f7_lsb; NR 0x85 b2 gate (G143) |
| A16 | 0xC007/0xC005 | port 0xFFFD AY select | RW | zxnext.vhd:2647 | ✓ | NR 0x84 b0 |
| A17 | 0xC007/0x8005 | port 0xBFFD AY data | RW | zxnext.vhd:2648, 2771 | ✓ | +3 alias R; NR 0x84 b0 |
| A18 | 0xC00F/0x8005 | port 0xBFF5 AY reg-query | RW | zxnext.vhd:2649 | ✓ | A3=0; AY reg_read(true) |
| A19 | 0x00FF/0x001F | port 0x1F DAC SD1 ch A | W | zxnext.vhd:2661 | ✓ | NR 0x84 b1 |
| A20 | 0x00FF/0x000F | port 0x0F DAC ch B | W | zxnext.vhd:2662 | ✓ | NR 0x84 b1\|b4 |
| A21 | 0x00FF/0x004F | port 0x4F DAC ch C | W | zxnext.vhd:2663 | ✓ | NR 0x84 b1\|b4 |
| A22 | 0x00FF/0x003F | port 0x3F DAC Profi ch A | W | zxnext.vhd:2661, V18 | ✓ | LSB-only (V18-NMP-02); NR 0x84 b3 |
| A23 | 0x00FF/0x005F | port 0x5F DAC | W | zxnext.vhd:2664 | ✓ | LSB-only; NR 0x84 b1\|b3 |
| A24 | 0x00FF/0x00F1 | port 0xF1 SD2 ch A | W | zxnext.vhd:2661 | ✓ | LSB-only (V18-NMP-03); NR 0x84 b2 |
| A25 | 0x00FF/0x00F3 | port 0xF3 SD2 ch B | W | zxnext.vhd:2662 | ✓ | LSB-only; NR 0x84 b2 |
| A26 | 0x00FF/0x00F9 | port 0xF9 SD2 ch C | W | zxnext.vhd:2663 | ✓ | LSB-only; NR 0x84 b2 |
| A27 | 0x00FF/0x00FB | port 0xFB SD2 ch D + mono | W | zxnext.vhd:2664, 2658 | ✓ | LSB-only; NR 0x84 b2 / b5 |
| A28 | 0x00FF/0x00B3 | port 0xB3 GS Covox | W | zxnext.vhd:2659 | ✓ | LSB-only (V18-NMP-04); NR 0x84 b6 |
| A29 | 0x00FF/0x00DF | port 0xDF SpecDrum / Kempston alias | RW | zxnext.vhd:2674, 2658 | ✓ | NR 0x84 b7 + NR 0x83 b5 + NR 0x82 b6 + hw_en |
| A30 | 0xFCFF/0x183B | port 0x183B..0x1B3B CTC | RW | zxnext.vhd:2690, ctc.vhd:128-137 | ✓ | A10=0 channels 0..3; NR 0x85 b3 gate (V21-NMP-02) |
| A31 | 0xFCFF/0x1C3B | CTC alias 0x1C3B..0x1F3B | R | zxnext.vhd:2690 | ✓ | A10=1 no-decode → 0x00 (V21R-NMP-NIT-02 fix) |
| A32 | 0x00FF/0x006B | port 0x6B DMA ZXN | RW | zxnext.vhd:2643 | ✓ | NR 0x82 b5 (port_dma_6b_io_en) |
| A33 | 0x00FF/0x000B | port 0x0B DMA z80 | RW | zxnext.vhd:2643 | ✓ | NR 0x85 b1 (port_dma_0b_io_en) |
| A34 | 0x00FF/0x00E7 | port 0xE7 SPI CS | W | zxnext.vhd:2620 | ✓ | NR 0x83 b3 (port_spi_io_en) |
| A35 | 0x00FF/0x00EB | port 0xEB SPI data | RW | zxnext.vhd:2621 | ✓ | NR 0x83 b3 |
| A36 | 0xFFFF/0x103B | I2C SCL | RW | zxnext.vhd:2630 | ✓ | NR 0x83 b2 |
| A37 | 0xFFFF/0x113B | I2C SDA | RW | zxnext.vhd:2631 | ✓ | NR 0x83 b2 |
| A38 | 0xFFFF/0x133B | UART Tx | RW | zxnext.vhd:2639 | ✓ | A15:11=00010, XOR(A10,A9∧A8)=1; NR 0x83 b4 |
| A39 | 0xFFFF/0x143B | UART Rx | RW | zxnext.vhd:2639 | ✓ | NR 0x83 b4 |
| A40 | 0xFFFF/0x153B | UART Sel | RW | zxnext.vhd:2639 | ✓ | NR 0x83 b4 |
| A41 | 0xFFFF/0x163B | UART Frame | RW | zxnext.vhd:2639 | ✓ | NR 0x83 b4 |
| A42 | 0x00FF/0x00E3 | DivMMC ctrl | RW | zxnext.vhd:2608 | ✓ | NR 0x83 b0 |
| A43 | 0x00FF/0x001F | Kempston1 | R | zxnext.vhd:2674 | ✓ | NR 0x82 b6 + port_1f_hw_en (G129) |
| A44 | 0x00FF/0x0037 | Kempston2 | R | zxnext.vhd:2675 | ✓ | NR 0x82 b7 + port_37_hw_en |
| A45 | 0x0FFF/0x0ADF | Mouse buttons | R | zxnext.vhd:2668 | ✓ | A11:8=A (V18-NMP-01); NR 0x83 b5 |
| A46 | 0x0FFF/0x0BDF | Mouse X | R | zxnext.vhd:2669 | ✓ | A11:8=B; NR 0x83 b5 |
| A47 | 0x0FFF/0x0FDF | Mouse Y | R | zxnext.vhd:2670 | ✓ | A11:8=F; NR 0x83 b5 |
| A48 | 0xFFFF/0xBF3B | ULA+ ctrl | RW | zxnext.vhd:2685 | ✓ | NR 0x85 b0 |
| A49 | 0xFFFF/0xFF3B | ULA+ data | RW | zxnext.vhd:2686 | ✓ | NR 0x85 b0 + ulap_mode "01" gate |
| A50 | 0xFFFF/`<magic>` | Magic-port debug | W | (jnext-only) | ✓ | host-side debug aid; no VHDL counterpart |
| A51 | 0x00FF/0x003F | MF+3 readback | R | zxnext.vhd:4310-4316, 2612 | ✓ | mode_p3 + invisible_eff=0 |
| A52 | 0x00FF/0x00BF | MF128 var-A readback | R | zxnext.vhd:4319, 2612 | ✓ | mf_type=01 + invisible_eff=0 |
| A53 | 0x00FF/0x009F | MF128 var-B readback | R | zxnext.vhd:4319, 2612 | ✓ | mf_type=10 + invisible_eff=0 |

### Section B — NextREG handler matrix (R and W, separately)

Format: NR addr | W | R | VHDL write line | VHDL read line | Status | Notes

Some addresses are read-only (RO); some are write-only (WO). Indicated below.

| # | NR | W | R | W line | R line | Status | Notes |
|---|---|---|---|---|---|---|---|
| B001 | 0x00 | (stored) | RO=0x08 | none | :5885 | ✓ | jnext deviates: 0x08 (HWID_EMULATORS) vs VHDL 0x0A (machine_id) — design choice |
| B002 | 0x01 | RO-guard | cached=0x32 | :5887 | :5887 | ✓ | RO-guard at NextReg::write (Pass-8) |
| B003 | 0x02 | handler | handler | :5117-5119, :3830-3872 | :5891 | ✓ | sw NMI strobes + bus_reset b7 + iotrap b4 + reset_type FSM |
| B004 | 0x03 | handler | handler | :5121-5151 | :5894 | ✓ | V21-NMP-01 closed — palette_sub_idx wired |
| B005 | 0x04 | handler | =0 | :5677-5704 | (none — G149 0) | ✓ | romram_bank; bit 7 dropped Issue 2/3; readback 0 |
| B006 | 0x05 | handler | handler | :5157-5158, :5832-5854 | :5897 | ✓ | joy0/joy1 swap; Pentagon-gated b2 (Pass-10/13); PASS-7/8 preserve |
| B007 | 0x06 | handler | handler | :5162-5170 | :5900 | ✓ | NMI button gates; config_mode-gated b2 (ps2_mode); PASS-6 preserve |
| B008 | 0x07 | handler | handler | :5789-5791 | :5902 | ✓ | V21-NMP-03 closed — act gated on expbus_eff_en |
| B009 | 0x08 | handler | handler | :5176-5182, :3654 | :5906 | ✓ | b7=unlock_paging strobe; PASS-7 preserve |
| B010 | 0x09 | handler | handler | :5186-5188 | :5909 | ✓ | b3 literal 0; sprite_tie reset; PASS-7 preserve |
| B011 | 0x0A | handler | handler | :5191-5198 | :5912 | ✓ | b2 literal 0; mf_type / sd_swap config_mode-gated; PASS-8 preserve |
| B012 | 0x0B | handler | (cached & 0xB1) | :5201-5203 | :5915 | ✓ | iomode_en + iomode + iomode_0; b6 / b3:1 literal 0 |
| B013 | 0x0C | (none) | cached=0 | none | none (others=>'0') | ✓ | unmapped; writes go to cache, reads 0 via cache zero |
| B014 | 0x0D | (none) | cached=0 | none | none | ✓ | unmapped |
| B015 | 0x0E | RO-guard | =0x03 | none | :5918 | ✓ | g_sub_version RO (PASS-8 guard) |
| B016 | 0x0F | RO-guard | =0x00 | none | :5921 | ✓ | g_board_issue RO Issue 2 |
| B017 | 0x10 | handler | handler | :5677-5704 | :5924 | ✓ | b7=flashboot, b4:0=coreid config-gated; b1:0=SPKEY (V16-NMP-01); PASS-8 preserve |
| B018 | 0x11 | handler | (cached & 0x07) | :5208-5217 | :5927 | ✓ | b7:3 literal 0; config-mode + g_video_inc gate; PASS-8 preserve |
| B019 | 0x12 | handler | (& 0x7F) | :5219-5220 | :5930 | ✓ | b7 literal 0; layer2_active_bank |
| B020 | 0x13 | handler | (& 0x7F) | :5222-5223 | :5933 | ✓ | b7 literal 0; layer2_shadow_bank; MMU mirror |
| B021 | 0x14 | handler | raw 8b | :5225-5226 | :5936 | ✓ | global transparency; reset 0xE3 (PASS-8) |
| B022 | 0x15 | handler | handler | :5229-5234 | :5939 | ✓ | sprite/layer priorities; full 8b |
| B023 | 0x16 | handler | raw 8b | :5236-5237 | :5942 | ✓ | layer2 scroll x lsb |
| B024 | 0x17 | handler | raw 8b | :5239-5240 | :5945 | ✓ | layer2 scroll y |
| B025 | 0x18 | handler+idx | handler+idx | :5242-5249 | :5947-5953 | ✓ | clip idx 4-way mux; idx auto-increment on write only |
| B026 | 0x19 | handler+idx | handler+idx | :5251-5258 | :5955-5961 | ✓ | sprite clip |
| B027 | 0x1A | handler+idx | handler+idx | :5260-5267 | :5963-5969 | ✓ | ULA clip — y2 raw (clamp at consumer per VHDL :6779) |
| B028 | 0x1B | handler+idx | handler+idx | :5269-5276 | :5971-5977 | ✓ | tilemap clip |
| B029 | 0x1C | handler | handler | :5278-5290 | :5980 | ✓ | clip idx reset bits 3:0 |
| B030 | 0x1E | (none) | composed | none | :5983 | ✓ | vc(8); bits 7:1 literal 0 |
| B031 | 0x1F | (none) | composed | none | :5986 | ✓ | vc(7:0) |
| B032 | 0x20 | unq strobe | composed | :1946 | :5989 | ✓ | unq INT strobes; readback im2_int_status; bits 5:4 literal 0 |
| B033 | 0x22 | handler | composed | :5295-5298 | :5992 | ✓ | line/ULA INT enable; b7=NOT pulse_int_n; bits 6:3 literal "0000" |
| B034 | 0x23 | handler | =cached | :5300-5301 | :5995 | ✓ | line INT target LSB (G56-C) |
| B035 | 0x26 | handler | raw | :5303-5304 | :5998 | ✓ | ULA scroll x |
| B036 | 0x27 | handler | raw | :5306-5307 | :6001 | ✓ | ULA scroll y |
| B037 | 0x28 | handler | =palette.nine_bit_first_byte | :6301-6303 | :6004 | ✓ | V14-NMP-02 — nr_stored_palette_value not the cached byte |
| B038 | 0x29 | handler | =0 | :6304-6305 | none (others=>'0') | ✓ | keymap addr LSB; WO (G149) |
| B039 | 0x2A | handler | =0 | :6312 commented | none (others=>'0') | ✓ | dead reg, V14-NMP-04 canonicalisation |
| B040 | 0x2B | handler | =0 | :6306-6307 | none (others=>'0') | ✓ | keymap data; WO (V14-NMP-03) |
| B041 | 0x2C | (none) | composed | :6007 | :6007 | ✓ | pi_audio_L; side-effect mutates nr_2d shadow |
| B042 | 0x2D | (none) | composed | :6011 | :6011 | ✓ | nr_2d_i2s_sample & "000000"; bits 5:0 literal 0 |
| B043 | 0x2E | (none) | composed | :6014 | :6014 | ✓ | pi_audio_R |
| B044 | 0x2F | handler | (& 0x03) | :5330-5331 | :6018 | ✓ | tm scroll x MSB bits 1:0; bits 7:2 literal "000000" |
| B045 | 0x30 | handler | raw | :5333-5334 | :6021 | ✓ | tm scroll x LSB |
| B046 | 0x31 | handler | raw | :5336-5337 | :6024 | ✓ | tm scroll y |
| B047 | 0x34 | (none) | composed | :4856 (mirror_we) | :6033 | ✓ | sprite_mirror_id; b7 literal 0 |
| B048a | 0x35 | handler (=0) | =0 | :4857-4875 | none (others=>'0') | ✓ | sprite attr no-inc; WO (G149) |
| B048b | 0x36 | handler (=0) | =0 | :4857-4875 | none | ✓ | sprite attr no-inc |
| B048c | 0x37 | handler (=0) | =0 | :4857-4875 | none | ✓ | sprite attr no-inc |
| B048d | 0x38 | handler (=0) | =0 | :4857-4875 | none | ✓ | sprite attr no-inc |
| B048e | 0x39 | handler (=0) | =0 | :4857-4875 | none | ✓ | sprite attr no-inc |
| B049 | 0x40 | handler | =palette_idx | :5374-5376 | :6036 | ✓ | palette idx; resets sub_idx |
| B050 | 0x41 | handler | =palette.read_8bit | :5378-5382 | :6039 | ✓ | palette dat(8:1); auto-inc idx |
| B051 | 0x42 | handler | raw | :5385-5386 | :6042 | ✓ | ulanext_format; reset 0x07 |
| B052 | 0x43 | handler | composed | :5388-5395 | :6045 | ✓ | palette ctl; resets sub_idx |
| B053 | 0x44 | handler | composed | :5397-5403 | :6048 | ✓ | 9-bit palette write; nr_palette_dat(10:9) & "00000" & dat(0); bits 5:1 literal 0 |
| B054 | 0x4A | handler | raw | :5406-5407 | :6051 | ✓ | fallback RGB |
| B055 | 0x4B | handler | raw | :5409-5410 | :6054 | ✓ | sprite transparent index |
| B056 | 0x4C | handler (& 0x0F) | (cached) | :5412-5413 | :6057 | ✓ | tm transparent index; bits 7:4 literal "0000" |
| B057a | 0x50 | handler | handler | :4880 | :6060 | ✓ | MMU0 |
| B057b | 0x51 | handler | handler | :4880 | :6063 | ✓ | MMU1 |
| B057c | 0x52 | handler | handler | :4880 | :6066 | ✓ | MMU2 |
| B057d | 0x53 | handler | handler | :4880 | :6069 | ✓ | MMU3 |
| B057e | 0x54 | handler | handler | :4880 | :6072 | ✓ | MMU4 |
| B057f | 0x55 | handler | handler | :4880 | :6075 | ✓ | MMU5 |
| B057g | 0x56 | handler | handler | :4880 | :6078 | ✓ | MMU6 |
| B057h | 0x57 | handler | handler | :4880 | :6081 | ✓ | MMU7 |
| B058 | 0x60 | handler (=0) | =0 | :4883-4885 | none | ✓ | copper data + addr++; WO (G149) |
| B059 | 0x61 | handler | =copper.read_61 | :5426-5427 | :6084 | ✓ | copper addr LSB |
| B060 | 0x62 | handler | =copper.read_62 | :5429-5431 | :6087 | ✓ | copper mode + addr hi; bits 5:3 literal "000" |
| B061 | 0x63 | handler (=0) | =0 | :5433-5437 | none | ✓ | copper data + addr++ alt; WO (V15-NMP-01) |
| B062 | 0x64 | handler | =copper.read_64 | :5441-5442 | :6090 | ✓ | copper offset |
| B063 | 0x68 | handler | masked | :5444-5450 | :6093 | ✓ | bit 1 literal 0 (V20-NMP-02 closed) |
| B064 | 0x69 | handler | composed | :5452 (we) + :3658,:3924 | :6096 | ✓ | NR 0x69 bit 7 → port_123b_layer2_en (V13-MEM-01) |
| B065 | 0x6A | handler (& 0x3F) | (cached) | :5455-5458 | :6099 | ✓ | bits 7:6 literal "00" |
| B066 | 0x6B | handler | =tilemap.get_control | :5460-5462 | :6102 | ✓ | full 8b |
| B067 | 0x6C | handler | raw | :5464-5465 | :6105 | ✓ | tm default attr |
| B068 | 0x6E | handler | =tilemap.get_map_base_read | :5467-5469 | :6108 | ✓ | tm base; bit 6 literal 0 |
| B069 | 0x6F | handler | =tilemap.get_def_base_read | :5471-5473 | :6111 | ✓ | tm tiles; bit 6 literal 0 |
| B070 | 0x70 | handler (& 0x3F) | (cached) | :5475-5477 | :6114 | ✓ | layer2 resolution + offset; bits 7:6 literal "00" |
| B071 | 0x71 | handler (& 0x01) | (cached) | :5479-5480 | :6117 | ✓ | scroll x MSB; bits 7:1 literal "0000000" |
| B072a | 0x75 | handler (=0) | =0 | :4861-4875 + :4916 | none | ✓ | sprite attr w/inc; WO |
| B072b | 0x76 | handler (=0) | =0 | :4861-4875 + :4916 | none | ✓ | sprite attr w/inc |
| B072c | 0x77 | handler (=0) | =0 | :4861-4875 + :4916 | none | ✓ | sprite attr w/inc |
| B072d | 0x78 | handler (=0) | =0 | :4861-4875 + :4916 | none | ✓ | sprite attr w/inc |
| B072e | 0x79 | handler (=0) | =0 | :4861-4875 + :4916 | none | ✓ | sprite attr w/inc |
| B073 | 0x7F | handler | raw | :5485-5486 | :6120 | ✓ | user reg 0; reset preserved (PASS-5) |
| B074 | 0x80 | handler | raw | :5488 (we) | :6123 | ✓ | expbus byte; reset lo→hi fold; commits expbus_eff_en (V21-NMP-03 consumes) |
| B075 | 0x81 | handler | (0x80 \| & 0x78) | :5491-5496 | :6126 | ✓ | bit 7 = i_BUS_ROMCS_n idle 1; bit 2 + speed bits literal 0 (V11-NMP-01) |
| B076 | 0x82 | handler | (cached) | :5498-5499 | :6129 | ✓ | internal_port_enable LSB; propagate_effective_port_enables |
| B077 | 0x83 | handler | (cached) | :5501-5502 | :6132 | ✓ | NR 0x83 enable; propagate |
| B078 | 0x84 | handler | (cached) | :5504-5505 | :6135 | ✓ | NR 0x84 enable |
| B079 | 0x85 | handler | (& 0x8F) | :5507-5509 | :6138 | ✓ | reset_type b7 + enable 3:0; bits 6:4 literal "000" |
| B080 | 0x86 | handler | (cached) | :5511-5512 | :6141 | ✓ | bus_port_enable; V16-NMP-02 AND-mask propagate |
| B081 | 0x87 | handler | (cached) | :5514-5515 | :6144 | ✓ | bus_port_enable |
| B082 | 0x88 | handler | (cached) | :5517-5518 | :6147 | ✓ | bus_port_enable |
| B083 | 0x89 | handler | (& 0x8F) | :5520-5522 | :6150 | ✓ | reset_type + enable 3:0; bits 6:4 literal "000" |
| B084 | 0x8A | handler (& 0x3F) | (cached) | :5524-5525 | :6153 | ✓ | bus_port_propagate; bits 7:6 literal "00" |
| B085 | 0x8C | handler | =mmu.get_nr_8c | :5527-5528 (we strobe) + :2255 | :6156 | ✓ | altrom; lo→hi nibble fold on reset |
| B086 | 0x8E | handler | =mmu.read_nr_8e | :4892 (we) + :3662-3734 | :6159 | ✓ | unified paging; bit 3 literal '1' |
| B087 | 0x8F | handler | (& 0x03) | :4893 (we) + :3787 | :6162 | ✓ | mapping mode; bits 7:2 literal "000000" |
| B088 | 0x90 | handler (& 0xFC) | (cached) | :5536-5537 | :6165 | ✓ | Pi GPIO o_en lo; bits 1:0 forced 0 |
| B089 | 0x91 | handler | raw | :5539-5540 | :6168 | ✓ | Pi GPIO o_en |
| B090 | 0x92 | handler | raw | :5542-5543 | :6171 | ✓ | Pi GPIO o_en |
| B091 | 0x93 | handler (& 0x0F) | (cached) | :5545-5546 | :6174 | ✓ | Pi GPIO o_en; bits 7:4 literal "0000" |
| B092 | 0x98 | (cache) | =0 | :5548-5549 | :6177 | ✓ | Pi GPIO input; jnext stubs to 0 (no Pi) |
| B093 | 0x99 | (cache) | =0 | :5551-5552 | :6180 | ✓ | Pi GPIO input |
| B094 | 0x9A | (cache) | =0 | :5554-5555 | :6183 | ✓ | Pi GPIO input |
| B095 | 0x9B | (cache) | =0 | :5557-5558 | :6186 | ✓ | Pi GPIO input; bits 7:4 literal "0000" |
| B096 | 0xA0 | handler | (& 0x39) | :5560-5561 | :6189 | ✓ | Pi peripheral en; bits 7,6,2,1 literal 0 |
| B097 | 0xA2 | handler | composed | :5563-5564 | :6192 | ✓ | I2S ctl; bit 5 literal 0, bit 1 literal 1 |
| B098 | 0xA8 | handler (& 0x01) | (cached) | :5569-5570 | :6198 | ✓ | ESP GPIO0 en; bits 7:1 literal 0 |
| B099 | 0xA9 | handler | =0 | :5572-5573 | :6201 | ✓ | ESP GPIO0 input; jnext stubs to 0 |
| B100 | 0xB0 | (none) | =keyboard.nr_b0_byte | none | :6208 | ✓ | extended keys |
| B101 | 0xB1 | (none) | =keyboard.nr_b1_byte | none | :6212 | ✓ | extended keys |
| B102 | 0xB2 | (none) | =md6.nr_b2_byte | none | :6215 | ✓ | MD6 buttons |
| B103 | 0xB8 | handler | =divmmc.entry_points_0 | :5584-5585 | :6218 | ✓ | reset default 0x83 (V17-NMP-01) |
| B104 | 0xB9 | handler | =divmmc.entry_valid_0 | :5587-5588 | :6221 | ✓ | reset default 0x01 |
| B105 | 0xBA | handler | =divmmc.entry_timing_0 | :5590-5591 | :6224 | ✓ | reset default 0x00 |
| B106 | 0xBB | handler | =divmmc.entry_points_1 | :5593-5594 | :6227 | ✓ | reset default 0xCD |
| B107 | 0xC0 | handler | composed | :5596-5599 | :6230 | ✓ | bit 4 literal 0; im_mode RO |
| B108 | 0xC2 | latch (RW) | (cached) | :4894 (we asserted) + :2054-2070 + :1080 | :6232-6233 | ✓ | Writable via NextReg port AND via NMIACK_LSB pathway; both pathways latch into nr_c2_retn_address_lsb (G88 set_nmi_return_address bypasses NextReg::write for the NMIACK path) — V22-NMP-01 dismissed per reviewer |
| B109 | 0xC3 | latch (RW) | (cached) | :4895 (we asserted) + :2054-2070 + :1081 | :6235-6236 | ✓ | Writable via NextReg port AND via NMIACK_MSB pathway; both pathways latch into nr_c3_retn_address_msb — V22-NMP-01 dismissed per reviewer |
| B110 | 0xC4 | handler | composed | :5607-5610 + :3621 | :6239 | ✓ | NR 0xC4 b0 NOT → port_ff(6); V12-NMP-01 + V19-IM2-02; bits 6:2 literal "00000" |
| B111 | 0xC5 | handler | =ctc.get_int_enable | :4897 (we) + :4078 | :6242 | ✓ | CTC int_en |
| B112 | 0xC6 | handler (& 0x77) | (cached) | :5615-5617 | :6245 | ✓ | UART int_en 0_654_0_210; bits 7 + 3 literal 0 |
| B113 | 0xC8 | handler | composed | :4898 + :1952-1955 | :6248 | ✓ | LINE/ULA status; bits 7:2 literal 0 |
| B114 | 0xC9 | handler | composed | :4899 + :1953 | :6251 | ✓ | CTC status |
| B115 | 0xCA | handler | composed | :4900 + :1952-1954 | :6254 | ✓ | UART status; bits 7+3 literal 0; RX duplicated |
| B116 | 0xCC | handler | composed | :5629-5630 | :6257 | ✓ | DMA delay; bits 6:2 literal "00000" |
| B117 | 0xCD | handler | raw | :5632-5633 | :6260 | ✓ | DMA delay CTC |
| B118 | 0xCE | handler | composed | :5635-5637 | :6263 | ✓ | DMA delay UART; bits 7 + 3 literal 0 |
| B119 | 0xD8 | handler | (=0/1) | :5639-5640 | :6266 | ✓ | iotrap FDC en; bits 7:1 literal 0 |
| B120 | 0xD9 | handler | =shadow | :5642 commented + :3892 | :6269 | ✓ | iotrap captured byte; full 8b |
| B121 | 0xDA | handler | =shadow & 0x03 | :5645 commented | :6272 | ✓ | iotrap cause; bits 7:2 literal "000000"; cleared by NR 0x02 b4=0 |
| B122 | 0xF0 | (cached) | =0 | :4902 (we) + :5648 commented | :6275 | ✓ | XADC cmd; Issue-2 hardwired 0 (V19R-NMP-NIT-03) |
| B123 | 0xF8 | (cached) | (& 0x7F) | :4903 (we) + :5651 commented | :6278 | ✓ | XADC daddr; bit 7 literal 0 (V19R-NMP-NIT-04) |
| B124 | 0xF9 | (cached) | =0 | :4904 (we) | :6281 | ✓ | XADC d0; Issue-2 unmodelled |
| B125 | 0xFA | (cached) | =0 | :4905 (we) | :6284 | ✓ | XADC d1 |
| B126 | 0xFF | handler (=0) | =0 | :4906 (we) + :4919, :6957 | none | ✓ | ULA+ palette poke; WO (V15-NMP-02) |

### Section C — Port-decode IO-enable gates (per `internal_port_enable(0..27)`)

| # | Gate | VHDL signal | NR/bit | VHDL line | Status |
|---|---|---|---|---|---|
| C01 | port_ff_io_en | internal_port_enable(0) | NR 0x82 b0 | :2397 | ✓ |
| C02 | port_7ffd_io_en | (1) | NR 0x82 b1 | :2399 | ✓ |
| C03 | port_dffd_io_en | (2) | NR 0x82 b2 | :2400 | ✓ |
| C04 | port_1ffd_io_en | (3) | NR 0x82 b3 | :2401 | ✓ |
| C05 | port_p3_floating_bus_io_en | (4) | NR 0x82 b4 | :2403 | ✓ |
| C06 | port_dma_6b_io_en | (5) | NR 0x82 b5 | :2405 | ✓ |
| C07 | port_1f_io_en | (6) | NR 0x82 b6 | :2407 | ✓ |
| C08 | port_37_io_en | (7) | NR 0x82 b7 | :2408 | ✓ |
| C09 | port_divmmc_io_en | (8) | NR 0x83 b0 | :2412 | ✓ |
| C10 | port_multiface_io_en | (9) | NR 0x83 b1 | :2415 | ✓ (Multiface dispatch observer gates on this via is_enabled) |
| C11 | port_i2c_io_en | (10) | NR 0x83 b2 | :2418 | ✓ |
| C12 | port_spi_io_en | (11) | NR 0x83 b3 | :2419 | ✓ |
| C13 | port_uart_io_en | (12) | NR 0x83 b4 | :2420 | ✓ |
| C14 | port_mouse_io_en | (13) | NR 0x83 b5 | :2422 | ✓ |
| C15 | port_sprite_io_en | (14) | NR 0x83 b6 | :2423 | ✓ |
| C16 | port_layer2_io_en | (15) | NR 0x83 b7 | :2424 | ✓ |
| C17 | port_ay_io_en | (16) | NR 0x84 b0 | :2428 | ✓ |
| C18 | port_dac_sd1_ABCD_1f0f4f5f_io_en | (17) | NR 0x84 b1 | :2429 | ✓ |
| C19 | port_dac_sd2_ABCD_f1f3f9fb_io_en | (18) | NR 0x84 b2 | :2430 | ✓ |
| C20 | port_dac_stereo_AD_3f5f_io_en | (19) | NR 0x84 b3 | :2431 | ✓ |
| C21 | port_dac_stereo_BC_0f4f_io_en | (20) | NR 0x84 b4 | :2432 | ✓ |
| C22 | port_dac_mono_AD_fb_io_en | (21) | NR 0x84 b5 (AND NOT b2) | :2433 | ✓ |
| C23 | port_dac_mono_BC_b3_io_en | (22) | NR 0x84 b6 | :2434 | ✓ |
| C24 | port_dac_mono_AD_df_io_en | (23) | NR 0x84 b7 | :2435 | ✓ |
| C25 | port_ulap_io_en | (24) | NR 0x85 b0 | :2439 | ✓ |
| C26 | port_dma_0b_io_en | (25) | NR 0x85 b1 | :2440 | ✓ |
| C27 | port_eff7_io_en | (26) | NR 0x85 b2 | :2441 | ✓ (G143 fix corrected NR 0x84 → NR 0x85) |
| C28 | port_ctc_io_en | (27) | NR 0x85 b3 | :2442 | ✓ |

### Section D — Expansion-bus AND-mask (NR 0x86..0x89)

| # | Gate | VHDL | Status |
|---|---|---|---|
| D01 | NR 0x82 b1 AND NR 0x86 b1 when expbus_eff_en | :2392-2393 | ✓ V16-NMP-02 |
| D02 | NR 0x82 b3 AND NR 0x86 b3 (port_1ffd) | :2392-2393 | ✓ |
| D03 | NR 0x83 b0 AND NR 0x87 b0 (port_divmmc) | :2392-2393 | ✓ |
| D04 | NR 0x83 b1 AND NR 0x87 b1 (port_multiface) | :2392-2393 | ✓ |
| D05 | NR 0x83 b4 AND NR 0x87 b4 (port_uart) | :2392-2393 | ✓ |
| D06 | NR 0x84 b0 AND NR 0x88 b0 (port_ay) | :2392-2393 | ✓ |
| D07 | NR 0x85 b0 AND NR 0x89 b0 (port_ulap) | :2392-2393 | ✓ |
| D08 | NR 0x85 b2 AND NR 0x89 b2 (port_eff7) | :2392-2393 | ✓ |
| D09 | NR 0x85 b3 AND NR 0x89 b3 (port_ctc) | :2392-2393 | ✓ |
| D10 | NR 0x86 b1..NR 0x89 b3 reset polarity = bit 7 | :5052-5067 | ✓ Pass-5 reset preserve |

### Section E — Multiface state machine (per multiface.vhd)

| # | Signal / FF | VHDL line | Status |
|---|---|---|---|
| E01 | mode_p3 (mf_type "00") | :112-113 | ✓ |
| E02 | mode_48 (mf_type "11") | :114 | ✓ |
| E03 | mode_128 (mf_type "01"/"10") | :115 | ✓ |
| E04 | port_io_dly FF | :122-131 | ✓ rising-edge OR of 4 port lines |
| E05 | button_pulse comb | :135 | ✓ button AND NOT nmi_active |
| E06 | nmi_active FF set on button_pulse | :142-143 | ✓ |
| E07 | nmi_active FF clear on cpu_retn_seen | :144 | ✓ |
| E08 | nmi_active clear on port_mf_enable_wr (port_io_dly=0) | :144 | ✓ |
| E09 | nmi_active clear on port_mf_disable_wr (port_io_dly=0) | :144 | ✓ |
| E10 | nmi_active clear on port_mf_disable_rd + mode_p3 + port_io_dly=0 | :144 | ✓ |
| E11 | invisible_eff = invisible AND NOT mode_48 | :165 | ✓ |
| E12 | invisible set on button_pulse → '0' | :158 | ✓ |
| E13 | invisible set on port_mf_disable_wr + NOT mode_p3 (gated) | :159 | ✓ |
| E14 | invisible set on port_mf_enable_wr + mode_p3 (gated) | :159 | ✓ |
| E15 | fetch_66 comb = a_0066 AND m1_n='0' AND nmi_active | :169 | ✓ |
| E16 | mf_enable set on fetch_66 + mreq_n='0' | :176-177 | ✓ |
| E17 | mf_enable clear on port_mf_disable_rd OR retn | :178 | ✓ |
| E18 | mf_enable assign on port_mf_enable_rd = NOT invisible_eff | :180-181 | ✓ |
| E19 | mf_enable_eff = mf_enable OR fetch_66 (one-cycle bypass) | :186 | ✓ via fetch_66_live_ |
| E20 | mf_port_en comb = port_en_rd AND NOT invisible_eff AND (mode_128 OR mode_p3) | :195 | ✓ |
| E21 | enable_i held-reset (reset = reset_i OR NOT enable_i) | :103 | ✓ |
| E22 | Per-mode port LSB decode (enable_io / disable_io) | :2612-2613 | ✓ V21 dispatch observer matches all four mode rows |
| E23 | port_mf_enable_rd / wr strobe (= iord/iowr AND port_mf_enable) | :2730-2731 | ✓ |
| E24 | port_mf_disable_rd / wr strobe | :2732-2733 | ✓ |

### Section F — NMI Source pipeline (per zxnext.vhd:2089-2170 + 3830-3872)

| # | Signal | VHDL line | Status |
|---|---|---|---|
| F01 | nmi_assert_expbus = expbus_eff_en AND NOT disable_mem AND NOT i_BUS_NMI_n | :2089 | ✓ Pass-9 |
| F02 | nmi_assert_mf = (hotkey_m1 OR nmi_sw_gen_mf) AND nr_06_b3 | :2090 | ✓ |
| F03 | nmi_assert_divmmc = (hotkey_drive OR nmi_sw_gen_divmmc) AND nr_06_b4 | :2091 | ✓ |
| F04 | nmi_activated = OR of 3 latches | :2093 | ✓ |
| F05 | nmi_mf latch set: assert_mf AND NOT e3_b7 AND NOT divmmc_nmi_hold | :2107 | ✓ |
| F06 | nmi_divmmc latch set: assert_divmmc AND NOT mf_is_active AND NOT nmi_mf | :2109-2110 | ✓ |
| F07 | nmi_expbus latch set: assert_expbus AND NOT (mf OR divmmc) | :2111-2112 | ✓ |
| F08 | Latches clear: reset OR nr_03_config_mode OR S_NMI_END | :2095-2105 | ✓ |
| F09 | nmi_hold = mf_nmi_hold / divmmc_nmi_hold / assert_expbus | :2118 | ✓ |
| F10 | S_NMI_IDLE → S_NMI_FETCH on nmi_activated=1 | :2123-2128 | ✓ |
| F11 | S_NMI_FETCH → S_NMI_HOLD on mf_a_0066 + m1_n=0 + mreq_n=0 | :2130-2134 | ✓ |
| F12 | S_NMI_HOLD → S_NMI_END on nmi_hold=0 | :2135-2139 | ✓ |
| F13 | S_NMI_END → S_NMI_IDLE on cpu_wr_n=1 | :2142-2146 | ✓ per-instruction tick = bus-idle |
| F14 | nmi_accept_cause = (IDLE OR FETCH) | :2164 | ✓ |
| F15 | nmi_generate_n = 0 when IDLE+activated OR FETCH OR debounce_disable+assert_expbus | :2168 | ✓ |
| F16 | nmi_mf_button arbiter strobe (IDLE + nmi_mf) | :2169 | ✓ feeds Multiface::button_press |
| F17 | nmi_divmmc_button arbiter strobe (IDLE + nmi_divmmc) | :2170 | ✓ feeds DivMmc::set_button_nmi |
| F18 | nmi_sw_gen_mf strobe = nmi_cpu_02_we AND wr_dat(3) OR nmi_gen_iotrap | :3837 | ✓ |
| F19 | nmi_sw_gen_divmmc strobe | :3833-3835 | ✓ |
| F20 | nmi_gen_iotrap = port_2ffd_rd OR port_3ffd_rd OR port_3ffd_wr (gated by NR 0xD8 b0) | :3835 | ✓ |
| F21 | nr_02_generate_mf_nmi readback latch SET when bit 3 AND nmi_accept_cause | :3840-3847 | ✓ Pass fix |
| F22 | nr_02_generate_divmmc_nmi readback latch | :3858-3861 | ✓ |
| F23 | nr_02_generate_*_nmi clear: NR 0x02 write with bit explicitly 0 | :3847-3848, :3860-3861 | ✓ Pass-rev fix (do NOT auto-clear at END) |
| F24 | nr_02_reset_type FSM shift = '0' & rt(2) & (rt(1) OR rt(0)) | :1732-1739 | ✓ "100" → "010" → "001" saturate; survives reset |
| F25 | nr_02_iotrap (read bit 4) = nr_da_iotrap_cause(1) OR (0) | :3885 | ✓ |
| F26 | NR 0xDA cause set on nmi_accept_cause = '1' only | :3871-3878 | ✓ Pass-3 fix |
| F27 | NR 0xD9 captured byte set on nmi_accept_cause = '1' only | :3892-3893 | ✓ Pass-3 fix |
| F28 | hotkey_drive port_divmmc_io_en gate | :6349 | ✓ honoured in on_hotkey_f10_divmmc_nmi |
| F29 | hotkey_soft_reset config_mode gate | :6370 | ✓ honoured in on_hotkey_f4_soft_reset |
| F30 | hotkey_hard_reset (no config_mode gate) | :6371 | ✓ |

### Section G — NMI / MF save-load fields

| # | Field | Owner | Status |
|---|---|---|---|
| G01 | NmiSource.mf_button_ | save_state | ✓ |
| G02 | NmiSource.divmmc_button_ | save_state | ✓ |
| G03 | NmiSource.expbus_nmi_n_ | save_state | ✓ |
| G04 | NmiSource.strobe_mf_button_pending_ | save_state | ✓ |
| G05 | NmiSource.strobe_divmmc_button_pending_ | save_state | ✓ |
| G06 | NmiSource.nmi_sw_gen_mf_ | save_state | ✓ |
| G07 | NmiSource.nmi_sw_gen_divmmc_ | save_state | ✓ |
| G08 | NmiSource.iotrap_strobe_pending_ | save_state | ✓ |
| G09 | NmiSource.mf_enable_ | save_state | ✓ |
| G10 | NmiSource.divmmc_enable_ | save_state | ✓ |
| G11 | NmiSource.expbus_debounce_disable_ | save_state | ✓ |
| G12 | NmiSource.expbus_eff_en_ | save_state (Pass-9 append) | ✓ |
| G13 | NmiSource.expbus_eff_disable_mem_ | save_state (Pass-9 append) | ✓ |
| G14 | NmiSource.config_mode_ | save_state | ✓ |
| G15 | NmiSource.mf_nmi_hold_ / mf_is_active_ | save_state | ✓ |
| G16 | NmiSource.divmmc_nmi_hold_ / divmmc_conmem_ | save_state | ✓ |
| G17 | NmiSource.nmi_mf_ / nmi_divmmc_ / nmi_expbus_ | save_state | ✓ |
| G18 | NmiSource.state_ FSM | save_state | ✓ |
| G19 | NmiSource.nr_02_pending_mf_ / divmmc_ | save_state | ✓ |
| G20 | NmiSource.prev_wr_n_ | save_state | ✓ |
| G21 | NmiSource.mf_button_strobe_ / divmmc_button_strobe_ | save_state | ✓ |
| G22 | NmiSource.reset_type_ (FSM "100" → "010" → "001") | save_state (Pass-3 append) | ✓ |
| G23 | Multiface.enabled_ | save_state | ✓ |
| G24 | Multiface.nmi_active_ / invisible_ / mf_enable_ / port_io_dly_ | save_state | ✓ |
| G25 | Multiface.mode_p3_ / mode_128_ / mode_48_ | save_state | ✓ |
| G26 | Multiface.mf_type_ (reconstructed from booleans; lossy 01 vs 10) | load_state | ✓ Wave 1 B2 documented limitation |
| G27 | Multiface.ram_ (8 KB) | save_state | ✓ |
| G28 | NextReg.regs_ (256 bytes incl. NR 0xC2/0xC3 RW latches) | save_state | ✓ V22-NMP-01 dismissed — both NextReg-port and NMIACK pathways latch into same regs_[] slot |
| G29 | NextReg.selected_ | save_state | ✓ |
| G30 | NextReg.nr_03_config_mode_ / machine_timing_ / user_dt_lock_ / machine_type_ | save_state | ✓ |
| G31 | NextReg.nr_04_romram_bank_ | save_state | ✓ |

## Cross-cutting families re-verified

The following Pass-XX-NMP-related families were re-spot-checked against the
current code; each was found CLEAN and is listed for traceability:

- **Cache-leak** (= write-side stores raw byte while VHDL stores a narrower
  field): NR 0x05 / 0x06 / 0x09 / 0x0A / 0x10 / 0x11 / 0x4C / 0x6A / 0x70 /
  0x71 / 0x8A / 0x90 / 0x93 / 0xA8 / 0xC6 / 0xD8 / 0xF8 — all canonicalised
  via write_handler return value (G56 strategy) or composed read handler.
- **RO-from-NextReg-port** registers (no `nr_*_we` strobe in VHDL): NR 0x01
  / 0x0E / 0x0F (PASS-8 guard). NR 0xC2 / NR 0xC3 are **NOT** in this
  family — they have ACTIVE `nr_c2_we`/`nr_c3_we` strobes at zxnext.vhd
  :4894-4895 (process A combinatorial decoder) and ARE writable via the
  NextReg port (V22-NMP-01 dismissed per reviewer).
- **Multi-writer fan-out** to port_ff_reg(6) (port-FF, NR 0x22 b2, NR 0xC4
  b0 NOT) — V12-NMP-01 / V12-NMP-02 closures, re-verified.
- **WO-NR readback** (= unmapped read returns "(others=>'0')" per VHDL
  :6286) — NR 0x04 / 0x29 / 0x2A / 0x2B / 0x35-39 / 0x60 / 0x63 / 0x75-79 /
  0xFF — handlers canonicalise cache to 0 (V14-NMP-03/04, V15-NMP-01/02,
  G149).
- **IncDecZ polarity** (NR 0x18-0x1B clip idx auto-advances on write only,
  NOT on read): rows B025-B028 — re-verified.
- **load_state shadow re-push** for NR 0x05, 0x06, 0x09, 0x0A, 0x08 →
  joystick / NMI gates / sprites / multiface / turbosound — re-verified.
- **Default-FF / reset preservation**: NR 0x05 / 0x06 / 0x09 / 0x0A / 0x10
  / 0x11 / 0x80 / 0x8A / 0x8C — all preserve via NextReg::reset() and
  per-handler fan-out (PASS-5..8 closures).
- **NR readback reset defaults**: B8/B9/BA/BB DivMMC (V17-NMP-01) — re-
  verified.
- **NMI request edge vs level**: edge-driven via prev_nmi_generate_n_
  watch in tick_peripheral_subsystems() — re-verified.
- **MF latches**: nmi_active / invisible / mf_enable / port_io_dly — all
  four match VHDL clock-edge process semantics; one clock_edge_() call per
  input pulse — re-verified.
- **Port-decode masks** (Pass-17/18 fix family): port_FE (LSB-only even),
  port_FF (LSB-only), port_1F/3F/4F/5F/9F/BF/B3/F1/F3/F9/FB (LSB-only),
  mouse 0xADF/0xBDF/0xFDF (A11:8 + LSB), MF strobes via observer — re-
  verified.
- **port_*_io_en gates** (Pass-18/19/20 closure): all 28 gates table
  section C — re-verified, no missed gates.
- **NR mux bit-position composition** ("literal '0' / '1'" lines that
  need read masking): all literal-zero positions across the 126 NR mux
  entries inspected; previously-closed cases (NR 0x68 b1 / NR 0x03 b7 /
  NR 0x07 act gate) confirmed; remaining bits all correctly masked or
  composed. No new literal-zero leak this pass.
- **NMIACK latch path**: VHDL :2050-2070 latches NR 0xC2/0xC3 from the
  Z80 NMI-service stack-push on NMIACK_LSB/NMIACK_MSB Z80N commands
  (priority elsif arms at :2060-2063), AND from the NextReg-port write
  path (lower-priority elsif arms at :2064-2067, fired by `nr_c2_we`/
  `nr_c3_we` asserted at :4894-4895). jnext exposes the NMIACK path via
  `NextReg::set_nmi_return_address(pc)` called from
  `cpu_.on_nmi_servicing` (emulator.cpp:708-710), which bypasses
  `NextReg::write` and writes `regs_[0xC2/0xC3]` directly. The NextReg-
  port write path goes through `NextReg::write` and falls through to the
  bare `regs_[reg]=val` assignment (no special handler needed — pre-fix
  jnext was already VHDL-faithful here).

## Convergence trajectory

| Pass | Findings (NMI/MF/Port) | Class breakdown |
|---|---|---|
| 11 | 8 | mixed |
| 12 | 17 | (a)+(b)+(c) |
| 13 | 7 | mostly (c) |
| 14 | 9 | (a)+(c) |
| 15 | 5 | (b)+(c) |
| 16 | 7 | (b)+(c) |
| 17 | 8 | (b)+(c) |
| 18 | 4 | (c) |
| 19 | 4 | (c)+NIT |
| 20 | 1 (V20-NMP-02) | (c) |
| 21 | 3 (V21-NMP-01/02/03) | all (c) |
| **22** | **0 (V22-NMP-01 dismissed)** | **n/a** |

The NMI/MF/Port subsystem has **CONVERGED** at Pass-22: the single
class-(c) candidate (V22-NMP-01 — NR 0xC2/0xC3 RO-guard) was
investigated and dismissed by the independent reviewer as a false
positive (the audit misread vestigial commented-out duplicates in
process B at :5601-5605 as the only strobe location, missing the ACTIVE
strobe assignments in process A at :4894-4895). Pre-audit jnext was
VHDL-faithful for NR 0xC2/0xC3; the proposed fix would have INTRODUCED
a divergence. Pass-22 therefore yields **0 effective findings** and
qualifies for convergence-skip in subsequent passes per
`feedback_task2_converged_subsystem_skip.md` (subject to fix-reviewer
APPROVE of this remediation).

## Test enumeration

- nextreg_integration_test: V22-NMP-01-A/B (NR 0xC2/0xC3 NextReg-port
  writes DO latch — VHDL :4894-4895 + :2064-2067 spec-correct contract,
  flipped from initial wrong direction per reviewer), V22-NMP-01-C-LSB/
  C-MSB (NMIACK pathway also latches), V22-NMP-01-D-LSB/D-MSB (post-
  NMIACK NextReg-port writes OVERWRITE the latch — latest writer wins)
  = **6 new rows** (group renamed `V22-NMP-01-NRC2-C3-Writable`).

Total new test rows: **6** (all nextreg_integration_test).

## VHDL-line citations index

All VHDL line numbers in this report reference
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
unless prefixed with `device/multiface.vhd` or `device/ctc.vhd`.

## Class-(d) escalations (architectural; pending user authorization)

None new this pass. Pre-existing class-(d) items tracked in the
aggregate report remain pending.

## Row count summary

- Section A (port-decode handlers): **53 rows** (P21 had 52; +1 for the
  V21R-NMP-NIT-02 CTC alias handler 0x1C3B..0x1F3B split out as A31)
- Section B (NextREG R/W matrix): **141 sub-rows** (B001..B126 logical
  with sub-row expansions for MMU slot rows B057a..h, sprite-attr no-inc
  B048a..e, sprite-attr w/inc B072a..e)
- Section C (port_*_io_en gates): **28 rows**
- Section D (expbus AND-mask): **10 rows**
- Section E (Multiface state machine): **24 rows**
- Section F (NMI Source pipeline): **30 rows**
- Section G (NMI/MF save-load fields): **31 rows**
- **Total enumeration rows: 53 + 141 + 28 + 10 + 24 + 30 + 31 = 317 rows**

Match-or-exceed P21 (301 rows) ✓ — 317 rows this pass, +16 from new
sub-row expansions in Section A (CTC alias) and Section B (MMU slot
rows B057a..h, sprite-attr rows B048a..e and B072a..e).
