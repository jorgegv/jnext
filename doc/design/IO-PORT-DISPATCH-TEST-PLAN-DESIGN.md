# I/O Port Dispatch Compliance Test Plan

VHDL-derived compliance test plan for the I/O port address decoding and
dispatch subsystem of the JNEXT emulator, covering port address matching,
device enable gating, read/write routing, and the internal response signal.

## Purpose

Validate that the emulator's I/O port dispatch matches the VHDL behaviour
defined in `zxnext.vhd` (lines ~2384-2840), specifically the two-stage
address decode (MSB then LSB), peripheral enable gating, read/write signal
generation, and the wired-OR data bus output.

## Authoritative VHDL Source

`zxnext.vhd`:
- Lines 2384-2464: Port enable registers and hardware enable latching
- Lines 2466-2576: Early address decode (MSB and LSB case statements)
- Lines 2578-2699: Full port address matching (combinatorial)
- Lines 2700-2797: Read/write signal generation (iord/iowr qualification)
- Lines 2800-2840: Wired-OR read data bus assembly

## Architecture

### Two-Stage Address Decode

The VHDL uses a two-phase decode to generate port match signals:

**Phase 1 --- MSB decode** (lines 2470-2506): Decodes `cpu_a(15:8)` into
one-hot signals (`port_00xx_msb`, `port_24xx_msb`, `port_ffxx_msb`, etc.).

**Phase 2 --- LSB decode** (lines 2508-2576): Decodes `cpu_a(7:0)` into
one-hot signals (`port_1f_lsb`, `port_3b_lsb`, `port_ff_lsb`, etc.).

**Final match**: Each port combines MSB, LSB, and enable signals.

### Port Enable Gating

Each peripheral has an enable bit from the NextREG port enable registers
(NR 0x82-0x89). A port only decodes if its enable bit is set.

Additionally, some ports have hardware enable conditions:
- Port 0x1F/0x37 require `port_1f_hw_en`/`port_37_hw_en` (joystick connected)
- DAC ports require `dac_hw_en` (NR 0x08 bit 3)
- +3 ports require `p3_timing_hw_en`
- 128K ports require `s128_timing_hw_en`

### Wired-OR Data Bus

Read data uses wired-OR: each port contributes its data when active,
or 0x00 when inactive. All are ORed together (lines 2837-2840).

## Test Case Catalog

### 1. ULA Port 0xFE

From `zxnext.vhd` line 2582: `port_fe <= '1' when cpu_a(0) = '0'`

The ULA port matches on bit 0 = 0 only, regardless of other address bits.

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| FE-01 | 0x00FE | Read | port_fe_rd active |
| FE-02 | 0xFFFE | Read | port_fe_rd active (bit 0 = 0) |
| FE-03 | 0x00FE | Write | port_fe_wr active |
| FE-04 | 0x00FF | Read | port_fe NOT active (bit 0 = 1) |
| FE-05 | 0x01FE | Read | port_fe active (any even address) |
| FE-06 | 0xFEFE | Read | port_fe active |

### 2. Timex SCLD Port 0xFF

From line 2583: `port_ff <= '1' when port_ff_lsb = '1'`

Port 0xFF matches on exact LSB 0xFF. Write requires `port_ff_io_en`.

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| FF-01 | 0x00FF | Write, io_en=1 | port_ff_wr active |
| FF-02 | 0x00FF | Write, io_en=0 | port_ff_wr NOT active |
| FF-03 | 0x00FF | Read, ff_rd_en=1 | TMX data returned |
| FF-04 | 0x00FF | Read, ff_rd_en=0 | ULA floating bus data |

### 3. NextREG Ports 0x243B / 0x253B

From lines 2625-2626:
```
port_243b <= '1' when port_24xx_msb = '1' and port_3b_lsb = '1'
port_253b <= '1' when port_25xx_msb = '1' and port_3b_lsb = '1'
```

These are always decoded (no enable gate --- they are the mechanism to control enables).

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| NR-01 | 0x243B | Write | port_243b_wr active |
| NR-02 | 0x243B | Read | Returns nr_register |
| NR-03 | 0x253B | Write | port_253b_wr active |
| NR-04 | 0x253B | Read | Returns register data |
| NR-05 | 0x243C | Any | NOT decoded (LSB wrong) |
| NR-06 | 0x253B | Read-then-read | Two consecutive reads work |

### 4. 128K Memory Ports (0x7FFD, 0xDFFD, 0x1FFD)

From lines 2593-2604:

```
port_7ffd <= '1' when cpu_a(15)='0' and (cpu_a(14)='1' or not p3) and port_fd='1' 
             and port_1ffd='0' and port_7ffd_io_en='1'
port_dffd <= '1' when cpu_a(15:12)="1101" and port_fd='1' and port_dffd_io_en='1'
port_1ffd <= '1' when cpu_a(13:12)="01" and port_xffd='1' and port_1ffd_io_en='1'
```

Where `port_fd = cpu_a(1:0) == "01"` and `port_xffd = cpu_a(15:14) == "00" and port_fd`.

| Test | Address | Timing | Expected |
|------|---------|--------|----------|
| MEM-01 | 0x7FFD | 128K timing | port_7ffd_active |
| MEM-02 | 0x7FFD | 48K timing | port_7ffd but NOT active |
| MEM-03 | 0xDFFD | Any | port_dffd when io_en |
| MEM-04 | 0x1FFD | +3 timing | port_1ffd when io_en |
| MEM-05 | 0x7FFD | io_en=0 | NOT decoded |
| MEM-06 | 0xFFFD | Any | NOT port_7ffd (bit 15=1) |
| MEM-07 | 0x3FFD | +3 timing | port_3ffd (FDC data) |
| MEM-08 | 0x2FFD | +3 timing | port_2ffd (FDC status) |

### 5. Kempston Joystick Ports (0x1F, 0x37)

From lines 2674-2675:

```
port_1f <= '1' when (port_1f_lsb='1' or (port_df_lsb='1' and dac_mono_AD_df and not mouse))
           and port_1f_io_en='1' and port_1f_hw_en='1'
port_37 <= '1' when port_37_lsb='1' and port_37_io_en='1' and port_37_hw_en='1'
```

Note: port 0x1F also matches when LSB is 0xDF (for Specdrum DAC compatibility
with Kempston) --- but only if DAC DF mode is enabled and mouse is disabled.

| Test | Address | Conditions | Expected |
|------|---------|------------|----------|
| JOY-01 | 0x001F | io_en=1, hw_en=1 | port_1f_rd active |
| JOY-02 | 0x001F | io_en=1, hw_en=0 | NOT decoded |
| JOY-03 | 0x001F | io_en=0 | NOT decoded |
| JOY-04 | 0x0037 | io_en=1, hw_en=1 | port_37_rd active |
| JOY-05 | 0x00DF | dac_df_en=1, mouse=0, 1f_en=1 | port_1f active |
| JOY-06 | 0x00DF | mouse=1 | NOT port_1f (mouse takes priority) |
| JOY-07 | 0xFF1F | Any | port_1f (MSB ignored for LSB-only decode) |

### 6. AY Sound Ports (0xFFFD, 0xBFFD)

From lines 2647-2649:

```
port_fffd <= '1' when cpu_a(15:14)="11" and cpu_a(2)='1' and port_fd='1' and port_ay_io_en='1'
port_bffd <= '1' when cpu_a(15:14)="10" and cpu_a(2)='1' and port_fd='1' and port_ay_io_en='1'
```

Read logic (line 2771): `port_fffd_rd = iord and (port_fffd or (port_bffd and machine_timing_p3) or port_bff5)`

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| AY-01 | 0xFFFD | Write | port_fffd_wr (register select) |
| AY-02 | 0xBFFD | Write | port_bffd_wr (data write) |
| AY-03 | 0xFFFD | Read | port_fffd_rd (register read) |
| AY-04 | 0xBFFD | Read, +3 timing | port_fffd_rd (readable on +3) |
| AY-05 | 0xBFFD | Read, 128K timing | NOT readable |
| AY-06 | 0xFFFD | ay_io_en=0 | NOT decoded |

### 7. SPI Ports (0xE7, 0xEB)

From lines 2620-2621:

```
port_e7 <= '1' when port_e7_lsb='1' and port_spi_io_en='1'
port_eb <= '1' when port_eb_lsb='1' and port_spi_io_en='1'
```

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| SPI-01 | 0x00E7 | Write | port_e7_wr (SPI CS) |
| SPI-02 | 0x00EB | Read | port_eb_rd (SPI data) |
| SPI-03 | 0x00EB | Write | port_eb_wr (SPI data) |
| SPI-04 | 0x00E7 | spi_io_en=0 | NOT decoded |

### 8. DivMMC Port (0xE3)

From line 2608: `port_e3 <= '1' when port_e3_lsb='1' and port_divmmc_io_en='1'`

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| DIV-01 | 0x00E3 | Read | port_e3_rd |
| DIV-02 | 0x00E3 | Write | port_e3_wr |
| DIV-03 | 0x00E3 | divmmc_io_en=0 | NOT decoded |

### 9. Sprite Ports (0x57, 0x5B, 0x303B)

From lines 2679-2681:

```
port_57 <= '1' when port_57_lsb='1' and port_sprite_io_en='1'
port_5b <= '1' when port_5b_lsb='1' and port_sprite_io_en='1'
port_303b <= '1' when port_30xx_msb='1' and port_3b_lsb='1' and port_sprite_io_en='1'
```

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| SPR-01 | 0x0057 | Write | port_57_wr (sprite attr) |
| SPR-02 | 0x005B | Write | port_5b_wr (sprite pattern) |
| SPR-03 | 0x303B | Read | port_303b_rd (sprite status) |
| SPR-04 | 0x303B | Write | port_303b_wr (sprite index) |
| SPR-05 | 0x0057 | sprite_io_en=0 | NOT decoded |

### 10. Layer 2 Port (0x123B)

From line 2635: `port_123b <= '1' when port_12xx_msb='1' and port_3b_lsb='1' and port_layer2_io_en='1'`

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| L2-01 | 0x123B | Read | port_123b_rd |
| L2-02 | 0x123B | Write | port_123b_wr |
| L2-03 | 0x123B | layer2_io_en=0 | NOT decoded |

### 11. I2C Ports (0x103B, 0x113B)

From lines 2630-2631:

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| I2C-01 | 0x103B | Read/Write | Active when i2c_io_en |
| I2C-02 | 0x113B | Read/Write | Active when i2c_io_en |
| I2C-03 | 0x103B | i2c_io_en=0 | NOT decoded |

### 12. UART Ports

From line 2639: Complex decode using bits 15:8, with uart_io_en gate.

```
port_uart <= '1' when cpu_a(15:11)="00010" and (cpu_a(10) xor (cpu_a(9) and cpu_a(8)))='1'
             and port_3b_lsb='1' and port_uart_io_en='1'
```

Valid UART addresses: 0x143B, 0x153B, 0x163B, 0x173B (TX/RX for two UARTs).

| Test | Address | Expected |
|------|---------|----------|
| UART-01 | 0x143B | port_uart active |
| UART-02 | 0x153B | port_uart active |
| UART-03 | 0x133B | NOT decoded (fails bit test) |
| UART-04 | 0x143B, uart_io_en=0 | NOT decoded |

### 13. ULA+ Ports (0xBF3B, 0xFF3B)

From lines 2685-2686:

| Test | Address | Direction | Expected |
|------|---------|-----------|----------|
| ULAP-01 | 0xBF3B | Write | port_bf3b_wr |
| ULAP-02 | 0xFF3B | Read | port_ff3b_rd |
| ULAP-03 | 0xFF3B | Write | port_ff3b_wr |
| ULAP-04 | 0xBF3B | ulap_io_en=0 | NOT decoded |

### 14. Kempston Mouse Ports (0xFADF, 0xFBDF, 0xFFDF)

From lines 2668-2670: Decode uses `cpu_a(11:8)` for the specific sub-port.

| Test | Address | Expected |
|------|---------|----------|
| MOUSE-01 | 0xFADF | port_fadf_rd (buttons) |
| MOUSE-02 | 0xFBDF | port_fbdf_rd (X coord) |
| MOUSE-03 | 0xFFDF | port_ffdf_rd (Y coord) |
| MOUSE-04 | 0xFADF, mouse_io_en=0 | NOT decoded |
| MOUSE-05 | 0xF0DF | NOT decoded (wrong nibble) |

### 15. DMA Port (0x6B / 0x0B)

From line 2643: `port_dma <= (port_6b_lsb and port_dma_6b_io_en) or (port_0b_lsb and port_dma_0b_io_en)`

| Test | Address | Expected |
|------|---------|----------|
| DMA-01 | 0x006B | port_dma when dma_6b_en |
| DMA-02 | 0x000B | port_dma when dma_0b_en |
| DMA-03 | Both disabled | NOT decoded |

### 16. CTC Port

From line 2690: `port_ctc <= cpu_a(15:11)="00011" and port_3b_lsb='1' and port_ctc_io_en='1'`

| Test | Address | Expected |
|------|---------|----------|
| CTC-01 | 0x183B | port_ctc active |
| CTC-02 | 0x1F3B | port_ctc active |
| CTC-03 | 0x203B | NOT decoded (wrong high bits) |

### 17. DAC Ports

From lines 2651-2664: Complex routing with multiple DAC modes.

| Test | Address | Mode | Expected |
|------|---------|------|----------|
| DAC-01 | 0x00FB | mono_AD_fb_en | port_dac_A and port_dac_D |
| DAC-02 | 0x00B3 | mono_BC_b3_en | port_dac_B and port_dac_C |
| DAC-03 | 0x001F | sd1_en, dac_hw_en | port_dac_A |
| DAC-04 | 0x00FB | sd2_en | SD2 overrides mono_AD |
| DAC-05 | 0x001F | dac_hw_en=0 | NOT written (DAC disabled) |

### 18. Internal Response Signal

From line 2696-2699: `port_internal_response` is the OR of all decoded port
signals. This determines whether the expansion bus sees the access or if it's
handled internally.

| Test | Scenario | Expected |
|------|----------|----------|
| INT-01 | Port 0xFE access | internal_response = 1 |
| INT-02 | Port 0x243B access | internal_response = 1 |
| INT-03 | Unknown port (e.g. 0x0042) | internal_response = 0 |
| INT-04 | Port 0x1F with hw_en=0 | internal_response = 0 |

### 19. Read Data Bus Assembly

From lines 2812-2840: Wired-OR of all port read data.

| Test | Scenario | Expected |
|------|----------|----------|
| BUS-01 | Read 0x243B | Only port_243b data on bus |
| BUS-02 | Read 0xFE | Only port_fe data on bus |
| BUS-03 | No port active | Data = 0x00 |
| BUS-04 | Two ports conflict | OR of both (should not happen in practice) |

### 20. IORQ/M1 Qualification

From lines 2705-2706:
```
iord <= '1' when cpu_iorq_n='0' and cpu_m1_n='1' and cpu_rd_n='0'
iowr <= '1' when cpu_iorq_n='0' and cpu_m1_n='1' and cpu_wr_n='0'
```

I/O operations are only valid when IORQ is active AND M1 is inactive (M1+IORQ
is interrupt acknowledge, not I/O).

| Test | Scenario | Expected |
|------|----------|----------|
| IORQ-01 | IORQ=0, M1=1, RD=0 | iord active |
| IORQ-02 | IORQ=0, M1=0, RD=0 | iord NOT active (int ack) |
| IORQ-03 | IORQ=1, M1=1, RD=0 | iord NOT active |

## Port Address Summary Table

| Port | Address | Decode | Enable |
|------|---------|--------|--------|
| ULA | xxFE (bit 0=0) | Any even | Always |
| SCLD | xxFF | LSB=0xFF | port_ff_io_en |
| NextREG sel | 243B | MSB=24, LSB=3B | Always |
| NextREG dat | 253B | MSB=25, LSB=3B | Always |
| 128K page | 7FFD | A15=0, A14=1, FD | port_7ffd_io_en |
| +3 extended | 1FFD | A15:14=00, A13:12=01, FD | port_1ffd_io_en |
| Pentagon ext | DFFD | A15:12=1101, FD | port_dffd_io_en |
| DivMMC | 00E3 | LSB=E3 | port_divmmc_io_en |
| SPI CS | 00E7 | LSB=E7 | port_spi_io_en |
| SPI data | 00EB | LSB=EB | port_spi_io_en |
| AY select | FFFD | A15:14=11, A2=1, FD | port_ay_io_en |
| AY data | BFFD | A15:14=10, A2=1, FD | port_ay_io_en |
| Kempston 1 | 001F | LSB=1F | port_1f_io_en + hw_en |
| Kempston 2 | 0037 | LSB=37 | port_37_io_en + hw_en |
| Sprite attr | 0057 | LSB=57 | port_sprite_io_en |
| Sprite pat | 005B | LSB=5B | port_sprite_io_en |
| Sprite ctl | 303B | MSB=30, LSB=3B | port_sprite_io_en |
| Layer 2 | 123B | MSB=12, LSB=3B | port_layer2_io_en |
| I2C (SCL) | 103B | MSB=10, LSB=3B | port_i2c_io_en |
| I2C (SDA) | 113B | MSB=11, LSB=3B | port_i2c_io_en |
| UART | 1x3B | MSB=1[4-7], LSB=3B | port_uart_io_en |
| Mouse btn | FADF | A11:8=A, LSB=DF | port_mouse_io_en |
| Mouse X | FBDF | A11:8=B, LSB=DF | port_mouse_io_en |
| Mouse Y | FFDF | A11:8=F, LSB=DF | port_mouse_io_en |
| ULA+ reg | BF3B | MSB=BF, LSB=3B | port_ulap_io_en |
| ULA+ data | FF3B | MSB=FF, LSB=3B | port_ulap_io_en |
| DMA | 006B / 000B | LSB match | Respective dma_io_en |
| CTC | 1x3B | A15:11=00011, LSB=3B | port_ctc_io_en |
| EFF7 | EFF7 | A15:12=E, LSB=F7 | port_eff7_io_en |

## Test Count Summary

| Category | Tests |
|----------|-------|
| ULA port 0xFE | ~6 |
| Timex SCLD 0xFF | ~4 |
| NextREG 0x243B/0x253B | ~6 |
| 128K memory ports | ~8 |
| Kempston joystick | ~7 |
| AY sound | ~6 |
| SPI | ~4 |
| DivMMC | ~3 |
| Sprite | ~5 |
| Layer 2 | ~3 |
| I2C | ~3 |
| UART | ~4 |
| ULA+ | ~4 |
| Mouse | ~5 |
| DMA | ~3 |
| CTC | ~3 |
| DAC | ~5 |
| Internal response | ~4 |
| Read data bus | ~4 |
| IORQ/M1 qualification | ~3 |
| **Total** | **~90** |
