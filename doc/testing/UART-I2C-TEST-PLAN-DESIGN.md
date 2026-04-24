# UART + I2C/RTC Compliance Test Plan

VHDL-derived compliance test plan for the UART (dual-channel) and I2C
(bit-banged) subsystems of the JNEXT ZX Spectrum Next emulator. All
specifications are extracted directly from the FPGA VHDL source code.

## Purpose

The ZX Spectrum Next provides two UARTs (UART 0 for ESP WiFi, UART 1 for
Raspberry Pi) and a bit-banged I2C master used to communicate with the
on-board DS1307 RTC. These subsystems are exercised by NextZXOS during boot
and by user programs. This test plan ensures the emulator faithfully
reproduces the VHDL behaviour for port I/O, FIFO management, baud rate
configuration, status register semantics, and I2C bit-bang protocol.

## Current status

Live as of 2026-04-24 (Phase 3 close, post skip-reduction plan):

- **`uart_test`: 92 total / 92 pass / 0 fail / 0 skip** — ZERO skips.
- **`uart_integration_test` (new): 12 total / 12 pass / 0 fail / 0 skip.**
- Aggregate: 104 rows, 104 pass, dashboard-confirmed at `test/SUBSYSTEM-TESTS-STATUS.md`.

The skip-reduction plan at
[`doc/design/TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md`](../design/TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md)
closed end-to-end (Phases 0→4) over 2026-04-24. Summary of what landed:

- **Phase 0** re-homed 12 cross-subsystem rows (INT-01..06, GATE-01..03,
  DUAL-05/06, I2C-10) to `// RE-HOME:` comments owned by the new
  `uart_integration_test.cpp`, and empirically re-audited RTC-06..10 + I2C-P06
  as passing (the 0x73 BCD symptom documented in prior revisions was already
  resolved by commit `174fa56` — see "Historical C-class bugs" below).
- **Phase 1** added bit-level `UartTxEngine` + `UartRxEngine` (7+8 VHDL
  states per `uart_tx.vhd` / `uart_rx.vhd`) + RX FIFO widened to `uint16_t`
  + `I2cRtc` expansion (regs_ 8→64 bytes, `osc_halt_`, `mode_12h_`,
  `use_real_time_`, `poke_register`, `peek_register`). Bit-level mode is
  opt-in via `set_bitlevel_mode(bool)`; the default is legacy-compatible.
  Two scaffold bugs surfaced in retrospective critic and fixed post-merge:
  (a) TX parity XOR must read pre-shift LSB (`uart_tx.vhd:156`); (b) RX
  sticky `err_framing_` / `err_parity_` must OR-in on every path that
  transitions to `S_ERROR` (`uart.vhd:541`). Fix commit `487928c`.
- **Phase 2** ran 5 parallel waves covering TX bit-level (A), RX bit-level
  (B), integration suite (C), dual routing (D), and I2C/RTC expansion (E).
  Per-wave test-flip counts were lower than planned because scaffold bugs
  exposed mid-phase forced deferred flipping: TX-09/10/13/14 and RX-08/09
  were flipped in `b86de83` after the `487928c` bug fix. 36 check() rows
  landed in Phase 2 (vs 35 planned). The retrospective critic (post-wave,
  cross-wave) APPROVED with one recommendation to document the deferred-
  flipping pattern — captured here.
- **Phase 3** deleted 7 stale duplicate `skip("RTC-11..17", ...)` calls in
  `uart_test.cpp` left behind by the Wave E merge (the `check()` rows for
  those same IDs were already passing). BAUD-02 and BAUD-03 were converted
  from `skip()` to a `D-UNOBSERVABLE` block comment per
  `feedback_unobservable_audit_rule` — `uart.vhd:323-326` half-selective
  writes to the prescaler low 14 bits are not exposed to any VHDL read
  path, indirectly covered by BAUD-07's post-prescaler TX-empty latency
  assertion.

### Historical C-class bugs (fixed, retained for reference)

Two bugs documented in earlier revisions of this plan previously caused 12
FAILs in this suite; both were fixed on main long before the skip-reduction
plan executed:

- **`src/peripheral/i2c.cpp:101` false-STOP** — FIXED in commit `174fa56`
  (`fix(i2c): remove false STOP detection from write_scl`). `detect_start_stop()`
  was called from `write_scl` against a stale `prev_sda_`, so every
  `i2c_send_byte` tripped a spurious STOP on the first bit. Removing the
  `detect_start_stop()` call from `write_scl` unblocked 9 rows: I2C-P03,
  I2C-P05a, I2C-P05b, RTC-01, RTC-02, RTC-04, RTC-05, plus flow-through for
  RTC-06/07.
- **`src/peripheral/uart.cpp:299` select-register bit** — FIXED in commit
  `47ee7e2` (`fix(uart): select-register read returns bit 3 (0x08) not bit
  6 (0x40)`). VHDL `uart.vhd:371` emits `"01000" & msb` when UART 1 is
  selected — the emulator now matches. Unblocked 3 rows: SEL-02, SEL-05,
  DUAL-02.

## VHDL Source Files

| File | Role |
|------|------|
| `serial/uart.vhd` | Top-level dual-UART with register interface, FIFOs, status |
| `serial/uart_rx.vhd` | UART receiver state machine with noise rejection |
| `serial/uart_tx.vhd` | UART transmitter state machine with flow control |
| `serial/fifop.vhd` | FIFO pointer manager with fullness flags |
| `zxnext.vhd` lines 3227-3268 | I2C master (bit-banged via ports 0x103B/0x113B) |
| `zxnext.vhd` lines 2628-2639 | Port address decoding for I2C and UART |
| `zxnext.vhd` lines 3335-3421 | UART instantiation and multiplexing |

## Architecture

### Port Address Map (from VHDL decoding)

The UART port decode in `zxnext.vhd` line 2639:

```
port_uart <= '1' when cpu_a(15 downto 11) = "00010"
             and (cpu_a(10) xor (cpu_a(9) and cpu_a(8))) = '1'
             and port_3b_lsb = '1'
             and port_uart_io_en = '1'
```

The register select is `cpu_a(9 downto 8)`:

| Port | A[9:8] | uart_reg | Read | Write |
|------|--------|----------|------|-------|
| 0x143B | 00 | RX | RX data byte (or 0x00 if empty) | Prescaler LSB |
| 0x153B | 01 | Select | Prescaler MSB (bits 2:0) | UART select + prescaler MSB |
| 0x163B | 10 | Frame | Frame register | Frame register |
| 0x133B | 11 | TX/Status | Status register | TX data byte |

I2C ports (active when `internal_port_enable(10)` = 1):

| Port | Read | Write |
|------|------|-------|
| 0x103B | bit 0 = SCL pin state (ANDed with Pi I2C1 SCL) | bit 0 -> SCL output |
| 0x113B | bit 0 = SDA pin state (ANDed with Pi I2C1 SDA) | bit 0 -> SDA output |

### Dual UART Architecture (from uart.vhd)

Both UARTs share the four register ports. The active UART is selected by
writing bit 6 of the select register (port 0x153B). UART 0 = ESP (bit 6 = 0),
UART 1 = Pi (bit 6 = 1).

Each UART has:
- **RX FIFO**: 512 bytes deep (DEPTH_BITS = 9), backed by shared dual-port BRAM
- **TX FIFO**: 64 bytes deep (DEPTH_BITS = 6), backed by simple dual-port RAM
- **Prescaler**: 17-bit (3 MSB + 14 LSB), default = 0x000F3 (243 decimal = 115200 baud @ 28 MHz)
- **Frame register**: 8-bit, default = 0x18 (8N1)

### FIFO Flags (from fifop.vhd)

The `fifop` entity tracks `stored` (DEPTH_BITS+1 bits wide) and derives:

| Flag | Condition | DEPTH_BITS=9 (RX) | DEPTH_BITS=6 (TX) |
|------|-----------|--------------------|--------------------|
| empty | stored == 0 | 0 bytes | 0 bytes |
| full_near | stored[N] or (stored[N-1] and stored[N-2]) | >= 384 (3/4) | >= 48 |
| full_almost | stored[N] or stored[N-1:1] all ones | >= 510 (full-2) | >= 62 |
| full | stored[N] | >= 512 | >= 64 |

The FIFO advances read/write pointers on the **falling edge** of the
rd/wr signal (edge-triggered: `rd_advance = rd_dly and not i_rd and not empty`).

### Status Register (port 0x133B read)

From `uart.vhd` lines 358-361 (UART 0) / 375-377 (UART 1):

| Bit | Name | Source |
|-----|------|--------|
| 7 | rx_err_break | Break condition detected (held while condition exists) |
| 6 | rx_err_framing | Framing or parity error (sticky, cleared by reading TX/status port or FIFO reset) |
| 5 | rx_err_9bit | Bit 8 of RX FIFO entry AND rx_avail (9th bit stored with each byte) |
| 4 | tx_empty | TX FIFO empty AND TX not busy |
| 3 | rx_near_full | RX FIFO >= 3/4 full |
| 2 | rx_err_overflow | Overflow (sticky, cleared same as bit 6) |
| 1 | tx_full | TX FIFO full |
| 0 | rx_avail | RX FIFO not empty |

**Sticky error clearing**: Reading the TX/status register (port 0x133B) generates
a falling edge on `uartN_tx_rd` which clears both `rx_err_overflow` and
`rx_err_framing` flags. FIFO reset (via frame register bit 7) also clears them.

### Frame Register (port 0x163B)

| Bit | TX meaning | RX meaning |
|-----|------------|------------|
| 7 | Reset TX to idle (abort in-flight byte) | Reset RX to idle |
| 6 | Hold break (TX line low, cannot send) | Pause (wait in idle) |
| 5 | Enable HW flow control (CTS/RTR) | N/A (forced to 0 for RX) |
| 4:3 | Data bits: 11=8, 10=7, 01=6, 00=5 | Same |
| 2 | Parity enable | Same |
| 1 | 1=odd parity, 0=even parity | Same |
| 0 | 0=one stop bit, 1=two stop bits | Same |

Default: 0x18 = 8 data bits, no parity, 1 stop bit, no flow control.

### Select Register (port 0x153B)

Write format:

| Bit | Meaning |
|-----|---------|
| 6 | UART select (0 = UART 0/ESP, 1 = UART 1/Pi) |
| 4 | Prescaler MSB write enable |
| 2:0 | Prescaler MSB value (written to selected UART when bit 4 = 1) |

Read format:

| Bit | UART 0 | UART 1 |
|-----|--------|--------|
| 6 | 0 | 1 |
| 4 | 0 | 0 |
| 2:0 | prescaler MSB | prescaler MSB |

### Prescaler Configuration

The 17-bit prescaler = {3-bit MSB, 14-bit LSB}.

Writing port 0x143B (RX register port):
- If written byte bit 7 = 0: sets prescaler LSB bits 6:0
- If written byte bit 7 = 1: sets prescaler LSB bits 13:7

Writing port 0x153B with bit 4 = 1: sets prescaler MSB bits 2:0.

Default prescaler: MSB = 0b000, LSB = 0b00000011110011 (0x00F3).
Full 17-bit value = 0x000F3 = 243 decimal. Baud = 28000000 / 243 = ~115226 baud.

### I2C Bit-Bang (from zxnext.vhd)

The I2C master is purely bit-banged by the CPU:
- **Write port 0x103B**: bit 0 sets SCL output (1=tristate/high, 0=assert low)
- **Write port 0x113B**: bit 0 sets SDA output (1=tristate/high, 0=assert low)
- **Read port 0x103B**: returns 0xFE | (i_I2C_SCL_n AND pi_i2c1_scl) in bit 0
- **Read port 0x113B**: returns 0xFE | (i_I2C_SDA_n AND pi_i2c1_sda) in bit 0

Reset state: both SCL and SDA outputs = 1 (released/high).

Read values are latched on the **falling edge** of the CPU clock.
Write values are latched on the **rising edge** of i_CLK_28.

## Test Case Catalog

### Group 1: UART Select Register (port 0x153B)

| ID | Test | Expected |
|----|------|----------|
| SEL-01 | Reset state: read select register | Returns 0x00 (UART 0 selected, prescaler MSB = 0) |
| SEL-02 | Write 0x40 to select, read back | Returns 0x08 + prescaler MSB (bit 6 reflects UART 1 selected; read shows bit 3 set for UART 1) |
| SEL-03 | Write 0x00 to select, read back | Returns 0x00 (UART 0 re-selected) |
| SEL-04 | Write 0x15 (bit4=1, bits2:0=101), read back with UART 0 | Returns 0x05 (prescaler MSB = 5) |
| SEL-05 | Write 0x55 (bit6=1, bit4=1, bits2:0=101), read back with UART 1 | Returns 0x0D (bit 3 for UART1, prescaler MSB = 5) |
| SEL-06 | Hard reset clears prescaler MSB to 0 | After hard reset, read returns 0x00 |
| SEL-07 | Soft reset clears uart_select_r to 0 but preserves prescaler MSB | Prescaler MSB retained, select = 0 |

### Group 2: Frame Register (port 0x163B)

| ID | Test | Expected |
|----|------|----------|
| FRM-01 | Hard reset state: read frame | Returns 0x18 (8N1) |
| FRM-02 | Write 0x1B (8 bits, parity odd, 2 stop), read back | Returns 0x1B |
| FRM-03 | Frame applies to selected UART only | Write 0xFF to UART 0 frame, switch to UART 1, read frame returns 0x18 |
| FRM-04 | Bit 7 write resets FIFO | Write 0x98, verify TX/RX FIFOs are empty |
| FRM-05 | Bit 6 sets break on TX | Write 0x58, TX line held low, o_busy = 1 |
| FRM-06 | Frame bits 4:0 sampled at transmission start | Change frame mid-byte, verify original frame used |

### Group 3: Prescaler / Baud Rate (ports 0x143B write, 0x153B)

| ID | Test | Expected |
|----|------|----------|
| BAUD-01 | Default prescaler = 243 (115200 @ 28MHz) | Full 17-bit value = 0x000F3 |
| BAUD-02 | Write 0x33 to port 0x143B (bit7=0): sets LSB bits 6:0 = 0x33 | Prescaler LSB[6:0] = 0x33 |
| BAUD-03 | Write 0x85 to port 0x143B (bit7=1): sets LSB bits 13:7 = 0x05 | Prescaler LSB[13:7] = 0x05 |
| BAUD-04 | Write prescaler MSB via select register | Full prescaler = {MSB, LSB} concatenated |
| BAUD-05 | Prescaler applies to selected UART independently | UART 0 and UART 1 can have different baud rates |
| BAUD-06 | Hard reset restores default prescaler for both UARTs | Both = 0x000F3 |
| BAUD-07 | Prescaler sampled at start of TX/RX (not mid-byte) | Mid-byte change does not affect current transfer |

### Group 4: TX FIFO and Transmission

| ID | Test | Expected |
|----|------|----------|
| TX-01 | Write byte to port 0x133B when TX FIFO empty | Byte enters TX FIFO, TX begins |
| TX-02 | Write 64 bytes: FIFO full | Status bit 1 (tx_full) = 1 |
| TX-03 | Write 65th byte when full | Byte silently dropped (FIFO write gated by not full) |
| TX-04 | TX empty flag: requires FIFO empty AND transmitter not busy | Status bit 4 = 0 while last byte still shifting out |
| TX-05 | TX FIFO write is edge-triggered | Holding write signal high writes only one byte |
| TX-06 | Frame bit 7 resets TX FIFO and transmitter | Mid-transmission abort, TX returns to idle |
| TX-07 | Frame bit 6 (break): TX line held low, busy = 1, cannot send | Write to TX while break active is ignored |
| TX-08 | 8N1 frame: start(0) + 8 data bits (LSB first) + stop(1) | Correct bit pattern on TX output |
| TX-09 | 7E2 frame: start + 7 bits + even parity + 2 stops | Correct bit pattern |
| TX-10 | 5O1 frame: start + 5 bits + odd parity + 1 stop | Correct bit pattern |
| TX-11 | Flow control: bit 5 enabled, CTS_n=1 blocks TX start | TX waits in S_RTR state until CTS_n=0 |
| TX-12 | Flow control disabled: CTS_n ignored | TX proceeds regardless of CTS_n |
| TX-13 | Parity calculation: even parity (frame bit 1 = 0) | Parity bit = XOR of all data bits |
| TX-14 | Parity calculation: odd parity (frame bit 1 = 1) | Parity bit = NOT(XOR of all data bits) |

### Group 5: RX FIFO and Reception

| ID | Test | Expected |
|----|------|----------|
| RX-01 | Inject byte into RX: read port 0x143B | Returns received byte |
| RX-02 | Read empty RX FIFO | Returns 0x00 |
| RX-03 | Fill RX FIFO with 512 bytes | Status bit 0 (rx_avail) = 1, full flag set |
| RX-04 | RX FIFO overflow: 513th byte | Overflow sticky flag set (status bit 2) |
| RX-05 | Read advances RX FIFO pointer (edge-triggered) | Sequential reads return FIFO bytes in order |
| RX-06 | RX near-full flag at 3/4 capacity (384 bytes) | Status bit 3 = 1 |
| RX-07 | Frame bit 7 resets RX FIFO | After reset, rx_avail = 0, FIFO empty |
| RX-08 | Framing error: missing stop bit | Status bit 6 (sticky) set, byte discarded |
| RX-09 | Parity error | Status bit 6 (sticky) set, byte discarded |
| RX-10 | Break condition: all-zero shift register in error state | Status bit 7 = 1, held while condition exists |
| RX-11 | Error byte stored with 9th bit in FIFO | Status bit 5 = rx_err AND rx_avail |
| RX-12 | Noise rejection: pulse < 2^NOISE_REJECTION_BITS / CLK is filtered | Short glitch on RX line does not trigger start |
| RX-13 | RX state machine: pause mode (frame bit 6) | RX waits until pause cleared AND line high |
| RX-14 | RX variables sampled at start bit detection | Mid-byte config change does not affect current reception |
| RX-15 | Hardware flow control: RTR_n asserted when FIFO almost full | o_Rx_rtr_n = flow_ctrl_en AND fifo_full_almost |

### Group 6: Status Register Clearing

| ID | Test | Expected |
|----|------|----------|
| STAT-01 | Sticky errors (overflow, framing) persist across reads of RX | Errors remain set until cleared |
| STAT-02 | Reading TX/status port (0x133B read) clears sticky errors | Both overflow and framing flags cleared on falling edge of read |
| STAT-03 | FIFO reset (frame bit 7) clears sticky errors | Both overflow and framing flags cleared |
| STAT-04 | Status bits reflect correct UART (per select register) | Switch select, verify independent status |
| STAT-05 | tx_empty = tx_fifo_empty AND NOT tx_busy | While transmitting last byte, tx_empty = 0 |
| STAT-06 | rx_avail = NOT rx_fifo_empty | Reflects FIFO occupancy, not receiver state |

### Group 7: Dual UART Independence

| ID | Test | Expected |
|----|------|----------|
| DUAL-01 | UART 0 and UART 1 have independent FIFOs | Data written to UART 0 not visible in UART 1 |
| DUAL-02 | Independent prescalers | Different baud rates on each UART |
| DUAL-03 | Independent frame registers | Different framing on each UART |
| DUAL-04 | Independent status registers | Errors on UART 0 do not affect UART 1 status |
| DUAL-05 | UART 0 = ESP, UART 1 = Pi channel assignment | Verify RX/TX signal routing per zxnext.vhd |
| DUAL-06 | Joystick UART mode multiplexing | When joy_iomode_uart_en=1, UART RX/TX/CTS routed through joystick port |

### Group 8: I2C Bit-Bang (ports 0x103B, 0x113B)

| ID | Test | Expected |
|----|------|----------|
| I2C-01 | Reset state: SCL = 1, SDA = 1 (both released) | Read ports return bit 0 = 1 |
| I2C-02 | Write 0x00 to port 0x103B | SCL output = 0 (asserted low) |
| I2C-03 | Write 0x01 to port 0x103B | SCL output = 1 (released) |
| I2C-04 | Write 0x00 to port 0x113B | SDA output = 0 (asserted low) |
| I2C-05 | Write 0x01 to port 0x113B | SDA output = 1 (released) |
| I2C-06 | Read port 0x103B | Returns 0xFE | (SCL_pin AND pi_i2c1_scl) in bit 0 |
| I2C-07 | Read port 0x113B | Returns 0xFE | (SDA_pin AND pi_i2c1_sda) in bit 0 |
| I2C-08 | Only bit 0 is significant for write | Writing 0xFE to SCL port sets SCL = 0 (bit 0 = 0) |
| I2C-09 | Bits 7:1 always read as 1 | Read always returns 0xFE or 0xFF |
| I2C-10 | I2C port enable gated by internal_port_enable(10) | When disabled, I2C ports do not respond |
| I2C-11 | Pi I2C1 AND-gating: if pi_i2c1_scl = 0, SCL reads 0 | External Pi I2C can pull SCL low |
| I2C-12 | Reset releases both lines | After reset, both outputs = 1 |

### Group 9: I2C Protocol Sequences (software-level)

These tests verify the emulator correctly supports the bit-bang sequences
that NextZXOS uses to communicate with the DS1307 RTC over I2C.

| ID | Test | Expected |
|----|------|----------|
| I2C-P01 | START condition: SDA high->low while SCL high | SDA transitions while SCL held high |
| I2C-P02 | STOP condition: SDA low->high while SCL high | SDA transitions while SCL held high |
| I2C-P03 | Send byte (8 clocks): MSB first, clock each bit | 8 SDA transitions with SCL toggling |
| I2C-P04 | Read ACK: release SDA, clock SCL, read SDA bit 0 | Returns 0 for ACK, 1 for NACK |
| I2C-P05 | Read byte (8 clocks): release SDA, read 8 bits | Each SCL high phase reads one SDA bit |
| I2C-P06 | Send ACK/NACK after read | Master drives SDA 0 (ACK) or 1 (NACK) |

### Group 10: DS1307 RTC Register Map

The DS1307 is an external I2C device at address 0x68 (0xD0 write, 0xD1 read).
These tests verify the emulator's RTC emulation responds correctly to I2C
transactions targeting the DS1307.

| ID | Test | Expected |
|----|------|----------|
| RTC-01 | Address 0xD0 write: device ACKs | SDA = 0 during ACK clock |
| RTC-02 | Address 0xD1 read: device ACKs | SDA = 0 during ACK clock |
| RTC-03 | Wrong address: device NACKs | SDA = 1 during ACK clock |
| RTC-04 | Write register pointer (0x00), read seconds | Returns BCD seconds (bits 6:0), CH bit 7 |
| RTC-05 | Read minutes (register 0x01) | Returns BCD minutes (bits 6:0) |
| RTC-06 | Read hours (register 0x02) | 24h mode: BCD hours (bits 5:0), bit 6 = 0 |
| RTC-07 | Read day-of-week (register 0x03) | Returns 1-7 |
| RTC-08 | Read date (register 0x04) | Returns BCD date (bits 5:0) |
| RTC-09 | Read month (register 0x05) | Returns BCD month (bits 4:0) |
| RTC-10 | Read year (register 0x06) | Returns BCD year (00-99) |
| RTC-11 | Read control register (0x07) | Returns control byte (OUT, SQWE, RS1, RS0) |
| RTC-12 | Write seconds register | Written value read back correctly |
| RTC-13 | Write hours in 12h mode (bit 6 = 1) | 12h format with AM/PM bit 5 |
| RTC-14 | Sequential read: auto-increment register pointer | Reading multiple bytes advances through registers 0-7 then wraps |
| RTC-15 | Sequential write: auto-increment register pointer | Writing multiple bytes advances register pointer |
| RTC-16 | Clock halt bit (seconds register bit 7) | CH=1 stops oscillator; CH=0 resumes |
| RTC-17 | NVRAM registers 0x08-0x3F (56 bytes) | Read/write general-purpose SRAM |

### Group 11: UART IM2 Interrupt Integration

Per `zxnext.vhd:1930-1944, 1949-1950, 5615-5617, 6245` the IM2 vector map is
four vectors — UART 0 RX (vector 1), UART 1 RX (vector 2), UART 0 TX
(vector 12), UART 1 TX (vector 13). NR 0xC6 bits 2:0 gate UART 0
(rx_avail / rx_near_full / tx_empty); bits 6:4 gate UART 1. Bits 3 and 7
are reserved (read back as 0 per `zxnext.vhd:6245`). The rx_near_full
path is **not** a separate vector — it is OR-ed into vectors 1 and 2 so
near-full asserts even when the `rx_avail` enable bit (bits 1 and 5) is
clear, i.e. host software cannot opt out of near-full while keeping the
channel enabled. Note: `im2_int_unq` (NR 0x20 manual-assert,
`zxnext.vhd:1946-1947`) does NOT inject into UART vectors 1/2/12/13.

| ID | Test | Expected |
|----|------|----------|
| INT-01 | Vector 1 on UART 0 `rx_avail` (NR 0xC6 bit 0 set, bit 1 clear) | IM2 vector 1 fires on `uart0_rx_avail AND NOT nr_c6_210(1)` |
| INT-02 | Vector 1 on UART 0 `rx_near_full` (NR 0xC6 bit 1 set; fires even if bit 0 clear — the "override") | IM2 vector 1 fires via `uart0_rx_near_full OR ...` |
| INT-03 | Vector 2 on UART 1 `rx_avail` (NR 0xC6 bit 4 set, bit 5 clear) | IM2 vector 2 fires on `uart1_rx_avail AND NOT nr_c6_654(1))` |
| INT-04 | Vector 2 on UART 1 `rx_near_full` (NR 0xC6 bit 5 set) | IM2 vector 2 fires via near-full OR |
| INT-05 | Vector 12 on UART 0 `tx_empty` (NR 0xC6 bit 2 set) | IM2 vector 12 fires |
| INT-06 | Vector 13 on UART 1 `tx_empty` (NR 0xC6 bit 6 set) | IM2 vector 13 fires |

### Group 12: Port Enable Gating

| ID | Test | Expected |
|----|------|----------|
| GATE-01 | UART port enable (internal_port_enable bit 12) | When disabled, UART ports do not decode |
| GATE-02 | I2C port enable (internal_port_enable bit 10) | When disabled, I2C ports do not decode |
| GATE-03 | Enable controlled by NextREG 0x82-0x85 | Writing 0 to appropriate bit disables port |

## Special Handling

### FIFO Edge-Triggered Semantics

The `fifop` entity uses edge detection for read and write advances:

```
rd_advance = rd_dly AND (NOT i_rd) AND (NOT empty)
wr_advance = wr_dly AND (NOT i_wr) AND (NOT full)
```

This means the pointer advances on the **falling edge** of the rd/wr signal
(signal goes from 1 to 0), not on the rising edge. The test harness must
correctly model this: a port read/write pulse must go high then low, and the
FIFO advances on the low transition.

### RX FIFO Write Priority

From `uart.vhd` line 798:

```
uart0_rx_fifo_we <= uart0_rx_avail_d AND (NOT uart0_rx_fifo_full)
                    AND (NOT uart0_rx_rd) AND (NOT uart0_tx_rd);
```

CPU reads (rx_rd, tx_rd) block FIFO writes for one cycle. This ensures the
BRAM read port is available for the CPU with priority over the UART receiver.

### Overflow Detection

Overflow is detected when a new byte arrives (`rx_avail`) while the previous
byte has not yet been written to the FIFO (`rx_avail_d` still high):

```
uart0_status_rx_err_overflow <= uart0_status_rx_err_overflow
    OR (uart0_rx_avail AND uart0_rx_avail_d);
```

### Error Byte Storage

Each RX FIFO entry is 9 bits wide. Bit 8 = `status_rx_err` (overflow OR
framing error) at the time the byte was received. Status register bit 5
reflects bit 8 of the current FIFO head entry AND rx_avail.

### TX FIFO Draining

The TX module automatically drains the TX FIFO:

```
uart0_tx_en <= '1' when uart0_tx_busy = '0' AND uart0_tx_fifo_empty = '0'
```

When the transmitter finishes one byte and the FIFO is not empty, the next
byte is immediately loaded. The TX FIFO write is also edge-triggered:

```
uart0_tx_fifo_we <= uart0_tx_wr AND (NOT uart0_tx_wr_d) AND NOT uart0_tx_fifo_full
```

### Baud Rate Timer Resynchronization (RX)

The RX baud rate timer has a mid-bit resynchronization feature. When a signal
edge is detected mid-bit (Rx_e = 1) and the timer has not already been updated,
the timer is reset to half the prescaler value. This improves tolerance of
baud rate mismatch:

```
elsif state /= S_START and Rx_e = '1' and rx_timer_updated = '0' then
    rx_timer <= '0' & rx_prescaler(PRESCALER_BITS-1 downto 1);
    rx_timer_updated <= '1';
```

## Test Harness Design

### UART Test Harness

The UART test harness should:

1. **Provide a loopback or injection interface**: Allow test code to inject
   bytes into the RX path and capture bytes from the TX path without requiring
   actual serial hardware.

2. **Model the port I/O cycle**: Each port read/write must correctly model the
   edge-triggered semantics (signal goes high for one cycle, then low).

3. **Track FIFO state**: Maintain internal counters to verify FIFO fullness
   flags match expected values at each step.

4. **Verify status register bits independently**: Each status bit should be
   tested in isolation and in combination.

### I2C/RTC Test Harness

The I2C test harness should:

1. **Model the open-drain bus**: Both master and slave can pull SDA/SCL low.
   The read value is the AND of all drivers (wired-AND).

2. **Implement DS1307 slave emulation**: Respond to I2C transactions at
   address 0x68 with proper ACK/NACK, register pointer auto-increment,
   and BCD time register format.

3. **Verify bit-bang timing**: Ensure SCL/SDA transitions occur in the
   correct order for valid I2C protocol.

## File Layout

```
test/
  uart_i2c/
    uart_fifo_test.cpp        # FIFO pointer and flag tests
    uart_register_test.cpp    # Port read/write, select, frame, prescaler
    uart_txrx_test.cpp        # TX/RX data path, framing, parity
    uart_status_test.cpp      # Status register bits, sticky errors, clearing
    i2c_bitbang_test.cpp      # I2C port read/write, protocol sequences
    rtc_ds1307_test.cpp       # DS1307 register map, BCD time, NVRAM
  CMakeLists.txt              # Updated: new test executables + CTest
doc/design/
  UART-I2C-TEST-PLAN-DESIGN.md   # This document
```

## How to Run

```bash
# Build
cmake --build build -j$(nproc) 2>&1 | tail -5

# Run UART/I2C tests standalone
./build/test/uart_fifo_test
./build/test/uart_register_test
./build/test/uart_txrx_test
./build/test/uart_status_test
./build/test/i2c_bitbang_test
./build/test/rtc_ds1307_test

# Run full regression suite (includes all tests)
bash test/regression.sh
```

## Summary

| Group | Tests | Coverage |
|-------|------:|----------|
| UART Select Register | 7 | Channel selection, prescaler MSB, reset |
| Frame Register | 6 | Framing modes, reset, break |
| Prescaler / Baud Rate | 7 | 17-bit prescaler, split writes, defaults |
| TX FIFO and Transmission | 14 | FIFO capacity, edge-trigger, framing, flow control, parity |
| RX FIFO and Reception | 15 | FIFO capacity, errors, noise rejection, flow control |
| Status Register Clearing | 6 | Sticky flags, clear-on-read, independence |
| Dual UART Independence | 6 | Separate FIFOs, config, status, routing |
| I2C Bit-Bang | 12 | Port I/O, enable gating, reset, pin AND-logic |
| I2C Protocol Sequences | 6 | START/STOP, byte send/receive, ACK/NACK |
| DS1307 RTC | 17 | Address, registers, BCD time, NVRAM, auto-increment |
| IM2 Interrupt Integration | 6 | RX/TX interrupt sources, enable control |
| Port Enable Gating | 3 | NextREG 0x82-0x85 port disable |
| **Total** | **~105** | |
