# DMA Subsystem Compliance Test Plan

VHDL-derived compliance test plan for the ZXN DMA controller implemented in
JNEXT. All behavioural requirements are extracted exclusively from
`dma.vhd` and the DMA-related wiring in `zxnext.vhd`.

## Purpose

The ZX Spectrum Next DMA controller is a Z80-DMA-inspired but non-identical
device. It supports two operating modes (ZXN DMA and Z80-DMA compatible),
three transfer modes, configurable address modes, prescaler-based burst
timing, auto-restart, and a programmable read-back sequence. This test plan
covers all VHDL-observable behaviour to verify the JNEXT emulation matches
hardware.

## VHDL Source Summary

### Entity: `z80dma` (dma.vhd)

**Ports:**

| Signal          | Dir | Width | Purpose                                         |
|-----------------|-----|-------|-------------------------------------------------|
| `turbo_i`       | in  | 2     | CPU speed: 00=3.5MHz, 01=7MHz, 10=14MHz, 11=28MHz |
| `dma_mode_i`    | in  | 1     | 0 = ZXN DMA, 1 = Z80-DMA compatible             |
| `dma_en_wr_s`   | in  | 1     | Write strobe (from port decode)                  |
| `dma_en_rd_s`   | in  | 1     | Read strobe (from port decode)                   |
| `wait_n_i`      | in  | 1     | Combined WAIT signal                             |
| `dma_delay_i`   | in  | 1     | IM2 DMA delay (interrupt arbitration)            |
| `bus_busreq_n_i`| in  | 1     | External bus request (daisy chain)               |
| `cpu_busreq_n_o`| out | 1     | DMA bus request to CPU                           |
| `cpu_bai_n`     | in  | 1     | Bus acknowledge in (daisy chain)                 |
| `cpu_bao_n`     | out | 1     | Bus acknowledge out (daisy chain)                |
| `dma_a_o`       | out | 16    | DMA address bus                                  |
| `dma_d_o`       | out | 8     | DMA data out                                     |
| `dma_d_i`       | in  | 8     | DMA data in                                      |
| `dma_rd_n_o`    | out | 1     | DMA read strobe                                  |
| `dma_wr_n_o`    | out | 1     | DMA write strobe                                 |
| `dma_mreq_n_o`  | out | 1     | DMA memory request                               |
| `dma_iorq_n_o`  | out | 1     | DMA I/O request                                  |
| `cpu_d_o`       | out | 8     | Read-back data to CPU                            |

### Key Internal Registers

| Register | Signals                           | Purpose                          |
|----------|-----------------------------------|----------------------------------|
| R0       | `R0_dir_AtoB_s`, `R0_start_addr_port_A_s`, `R0_block_len_s` | Direction, port A addr, block length |
| R1       | `R1_portAisIO_s`, `R1_portA_addrMode_s`, `R1_portA_timming_byte_s` | Port A type, addr mode, timing |
| R2       | `R2_portBisIO_s`, `R2_portB_addrMode_s`, `R2_portB_timming_byte_s`, `R2_portB_preescaler_s` | Port B type, addr mode, timing, prescaler |
| R3       | `R3_dma_en_s`                     | DMA enable (also triggers START_DMA) |
| R4       | `R4_mode_s`, `R4_start_addr_port_B_s` | Transfer mode, port B addr     |
| R5       | `R5_ce_wait_s`, `R5_auto_restart_s` | CE/WAIT mux, auto-restart      |
| R6       | `R6_read_mask_s`                  | Read sequence mask               |

### Transfer State Machine

```
IDLE -> START_DMA -> WAITING_ACK -> TRANSFERING_READ_1..4
  -> TRANSFERING_WRITE_1..4 -> WAITING_CYCLES (if prescaler)
  -> FINISH_DMA (if block done) or loop back to READ_1/START_DMA
```

### Port Decoding (zxnext.vhd)

- Port 0x6B: ZXN DMA mode (`port_0b_lsb = 0`)
- Port 0x0B: Z80-DMA compatible mode (`port_0b_lsb = 1`)
- Mode is latched on each read/write: `dma_mode <= port_0b_lsb`
- Both ports map to the same `z80dma` entity
- DMA cannot program itself: `port_dma_rd/wr` gated by `not dma_holds_bus`

### Bus Arbitration (zxnext.vhd)

- `dma_holds_bus = 1` when `z80_busak_n = 0`
- When DMA holds bus: address, data, control signals all come from DMA
- CPU M1, RFSH forced high; HALT passed through unchanged
- `dma_wait_n = z80_wait_n AND spi_wait_n`

## Architecture

### Test Approach

Tests are structured as unit tests that directly exercise the DMA subsystem
through port I/O sequences, verifying memory/IO transfers, address
progression, status readback, and mode-specific behaviour. Each test:

1. Programs DMA registers via writes to port 0x6B (ZXN) or 0x0B (Z80)
2. Issues Load (0xCF) + Enable (0x87) commands
3. Runs the emulator for enough cycles to complete the transfer
4. Verifies destination memory/IO contents, address counters, status bits

### Test Data Format

Each test case specifies:
- Test name
- DMA port (0x6B or 0x0B) determining the mode
- Sequence of bytes written to the DMA port (register programming)
- Source memory/IO contents before transfer
- Expected destination memory/IO contents after transfer
- Expected status register and counter values on readback

## Test Case Catalog

### 1. Port Decoding and Mode Selection (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 1.1 | Write to port 0x6B sets ZXN mode | `dma_mode <= port_0b_lsb` (port_0b_lsb=0 for 0x6B) | After write to 0x6B, dma_mode_i = 0 |
| 1.2 | Write to port 0x0B sets Z80-DMA mode | `dma_mode <= port_0b_lsb` (port_0b_lsb=1 for 0x0B) | After write to 0x0B, dma_mode_i = 1 |
| 1.3 | Read from port 0x6B sets ZXN mode | Same latch on `port_dma_rd` | Read from 0x6B, mode = 0 |
| 1.4 | Read from port 0x0B sets Z80 mode | Same latch on `port_dma_rd` | Read from 0x0B, mode = 1 |
| 1.5 | Mode defaults to ZXN (0) on reset | `dma_mode <= '0'` on reset | Verify initial mode after reset |
| 1.6 | Mode switches on each access | Alternate 0x6B/0x0B writes | Mode tracks last accessed port |

### 2. Register Programming — R0 (Direction, Port A Address, Block Length) (~8 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 2.1 | R0 direction A->B | `R0_dir_AtoB_s <= cpu_d_i(2)` (bit 2 = 1) | Verify src=portA, dest=portB after Load |
| 2.2 | R0 direction B->A | Bit 2 = 0 | Verify src=portB, dest=portA after Load |
| 2.3 | R0 port A start address low byte | `R0_start_addr_port_A_s(7:0)` via R0_BYTE_0 | Readback port A address |
| 2.4 | R0 port A start address high byte | `R0_start_addr_port_A_s(15:8)` via R0_BYTE_1 | Readback port A address |
| 2.5 | R0 port A full 16-bit address | Bits 3+4 set -> both bytes follow | Transfer starts at correct address |
| 2.6 | R0 block length low byte | `R0_block_len_s(7:0)` via R0_BYTE_2 | Transfer correct number of bytes |
| 2.7 | R0 block length high byte | `R0_block_len_s(15:8)` via R0_BYTE_3 | Transfer correct number of bytes |
| 2.8 | R0 selective byte programming | Only set bit 3 (addr LO only, skip HI/len) | Only addr LO programmed |

VHDL byte sequence logic: The base R0 byte has bit 7=0, bits 6..3 select
which follow-up bytes are present (bit 3=addr LO, bit 4=addr HI, bit 5=len
LO, bit 6=len HI). Only bytes whose corresponding bit is set are expected
in the sequence.

### 3. Register Programming — R1 (Port A Configuration) (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 3.1 | Port A is memory (default) | `R1_portAisIO_s <= cpu_d_i(3)` (bit 3=0) | Transfer uses MREQ for port A side |
| 3.2 | Port A is I/O | Bit 3=1 | Transfer uses IORQ for port A side |
| 3.3 | Port A address increment | `R1_portA_addrMode_s <= cpu_d_i(5:4)` = "01" | Src addr increments each byte |
| 3.4 | Port A address decrement | addr mode = "00" | Src addr decrements each byte |
| 3.5 | Port A address fixed | addr mode = "10" or "11" | Src addr unchanged each byte |
| 3.6 | Port A timing byte | `R1_portA_timming_byte_s <= cpu_d_i(1:0)` | Affects read/write cycle count |

R1 identification: bits 7=0, bits 2:0 = "100". Bit 6 controls whether a
timing follow-up byte is present.

### 4. Register Programming — R2 (Port B Configuration) (~8 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 4.1 | Port B is memory (default) | `R2_portBisIO_s <= cpu_d_i(3)` (bit 3=0) | Transfer uses MREQ for port B side |
| 4.2 | Port B is I/O | Bit 3=1 | Transfer uses IORQ for port B side |
| 4.3 | Port B address increment | `R2_portB_addrMode_s` = "01" | Dest addr increments |
| 4.4 | Port B address decrement | addr mode = "00" | Dest addr decrements |
| 4.5 | Port B address fixed | addr mode = "10" or "11" | Dest addr unchanged |
| 4.6 | Port B timing byte | `R2_portB_timming_byte_s <= cpu_d_i(1:0)` | Affects write cycle count |
| 4.7 | Port B prescaler byte | `R2_portB_preescaler_s <= cpu_d_i` (R2_BYTE_1) | Controls inter-byte delay |
| 4.8 | Port B prescaler = 0 (no delay) | Prescaler defaults to 0x00 | No WAITING_CYCLES state |

R2 identification: bits 7=0, bits 2:0 = "000". Bit 6 controls timing byte;
within timing byte, bit 5 controls prescaler byte.

### 5. Register Programming — R3 (DMA Enable) (~4 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 5.1 | R3 with bit 6=1 triggers START_DMA | `if cpu_d_i(6) = '1' then dma_seq_s <= START_DMA` | DMA begins transfer |
| 5.2 | R3 with bit 6=0 does not start | Bit 6=0 | DMA remains idle |
| 5.3 | R3 mask byte (bit 3) | Follow-up byte consumed but no functional effect (commented out) | Byte consumed, no crash |
| 5.4 | R3 match byte (bit 4) | Follow-up byte consumed | Byte consumed, no crash |

R3 identification: bit 7=1, bits 1:0 = "00".

### 6. Register Programming — R4 (Transfer Mode, Port B Address) (~8 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 6.1 | Byte mode (R4_mode = "00") | `R4_mode_s <= cpu_d_i(6:5)` = "00" | Single byte then stop |
| 6.2 | Continuous mode (R4_mode = "01") | Mode = "01" | Transfer runs without bus release |
| 6.3 | Burst mode (R4_mode = "10") | Mode = "10" | Bus released during prescaler wait |
| 6.4 | Default mode is continuous ("01") | Reset: `R4_mode_s <= "01"` | Verify default after reset |
| 6.5 | Port B start address low | `R4_start_addr_port_B_s(7:0)` via R4_BYTE_0 | Readback port B addr |
| 6.6 | Port B start address high | `R4_start_addr_port_B_s(15:8)` via R4_BYTE_1 | Readback port B addr |
| 6.7 | Port B full 16-bit address | Bits 2+3 -> two follow-up bytes | Transfer to correct dest |
| 6.8 | Mode "11" treated as "00" (byte) | No special case in VHDL | Verify byte-mode behaviour |

R4 identification: bit 7=1, bits 1:0 = "01". Bits 6:5 = mode. Bit 2 =
port B addr LO follows, bit 3 = port B addr HI follows.

### 7. Register Programming — R5 (Auto-restart, CE/WAIT) (~4 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 7.1 | Auto-restart enabled | `R5_auto_restart_s <= cpu_d_i(5)` = 1 | After block done, transfer restarts |
| 7.2 | Auto-restart disabled (default) | Reset: `R5_auto_restart_s <= '0'` | Transfer stops after block done |
| 7.3 | CE/WAIT mux bit | `R5_ce_wait_s <= cpu_d_i(4)` | Configuration accepted |
| 7.4 | R5 defaults on reset | Both bits = 0 | Verify defaults |

R5 identification: bits 7:6 = "10", bits 2:0 = "010".

### 8. Register 6 Commands (~16 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 8.1 | 0xC3 — Reset | Resets to IDLE, clears status, resets timings, prescaler, auto-restart | All registers at defaults |
| 8.2 | 0xC7 — Reset port A timing | `R1_portA_timming_byte_s <= "01"` | Port A timing = "01" |
| 8.3 | 0xCB — Reset port B timing | `R2_portB_timming_byte_s <= "01"` | Port B timing = "01" |
| 8.4 | 0xCF — Load | Loads src/dest from start addresses, resets counter | src/dest match programmed addresses |
| 8.5 | 0xCF — Load A->B direction | `dma_src_s <= R0_start_addr_port_A_s; dma_dest_s <= R4_start_addr_port_B_s` | Correct src/dest |
| 8.6 | 0xCF — Load B->A direction | Reversed assignment | Correct src/dest |
| 8.7 | 0xCF — Load counter ZXN mode | `dma_counter_s <= 0` when dma_mode=0 | Counter starts at 0 |
| 8.8 | 0xCF — Load counter Z80 mode | `dma_counter_s <= 0xFFFF` when dma_mode=1 | Counter starts at -1 |
| 8.9 | 0xD3 — Continue | Resets counter, keeps current src/dest addresses | Counter reset, addresses unchanged |
| 8.10 | 0xD3 — Continue ZXN mode | Counter = 0 | Verify counter |
| 8.11 | 0xD3 — Continue Z80 mode | Counter = 0xFFFF | Verify counter |
| 8.12 | 0x87 — Enable DMA | `dma_seq_s <= START_DMA` | Transfer begins |
| 8.13 | 0x83 — Disable DMA | `dma_seq_s <= IDLE` | Transfer stops |
| 8.14 | 0x8B — Reinitialize status | `status_endofblock_n <= '1'; status_atleastone <= '0'` | Status bits cleared |
| 8.15 | 0xBB — Read mask follows | Next byte sets `R6_read_mask_s` | Read sequence reprogrammed |
| 8.16 | 0xBF — Read status byte | `reg_rd_seq_s := RD_STATUS` | Next read returns status |

Interrupt commands (0xAF, 0xAB, 0xA3, 0xB7) are present in VHDL but have
no functional implementation (empty branches). Tests should verify they are
accepted without error.

### 9. Memory-to-Memory Transfer (~8 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 9.1 | Simple A->B, increment both | R1: mem/inc, R2: mem/inc | N bytes copied, addrs incremented |
| 9.2 | Simple B->A, increment both | Direction reversed | N bytes copied in reverse direction |
| 9.3 | A->B, decrement source | R1: mem/dec, R2: mem/inc | Source walks backwards |
| 9.4 | A->B, fixed source (fill) | R1: mem/fixed, R2: mem/inc | Same byte written N times |
| 9.5 | A->B, fixed dest (probe) | R1: mem/inc, R2: mem/fixed | Each byte written to same address |
| 9.6 | Block length = 1 | `R0_block_len_s = 1` | Exactly 1 byte transferred |
| 9.7 | Block length = 256 | 256 bytes | Verify full block transfer |
| 9.8 | Block length = 0 (edge case) | Counter starts at 0, `dma_counter_s < R0_block_len_s` is false | No bytes transferred (immediate FINISH) |

VHDL note on block length: The comparison is `dma_counter_s < R0_block_len_s`.
In ZXN mode the counter starts at 0 and increments per byte, so block_len=N
transfers N bytes (counter reaches N, which is not < N). In Z80 mode the
counter starts at 0xFFFF and wraps, so block_len=N transfers N+1 bytes.

### 10. Memory-to-IO Transfer (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 10.1 | Mem(A) -> IO(B), A inc, B fixed | R1: mem/inc, R2: IO/fixed | Bytes written to I/O port |
| 10.2 | Mem(A) -> IO(B), A inc, B inc | R2: IO/inc | Port address increments |
| 10.3 | Verify MREQ on read, IORQ on write | `dma_mreq_n_o` / `dma_iorq_n_o` logic | Correct bus signals per phase |
| 10.4 | IO(A) -> Mem(B) | R1: IO, R2: mem | Bytes read from I/O, written to memory |
| 10.5 | IO(A) -> IO(B) | Both ports are I/O | IORQ on both read and write phases |
| 10.6 | Port B address as I/O port | 16-bit address driven on bus | Full 16-bit address on bus for I/O |

VHDL bus signal logic:
- `dma_mreq_n_o = (NOT dma_mreq_cycle) OR (dma_rd_n AND dma_wr_n)`
- `dma_iorq_n_o = dma_mreq_cycle OR (dma_rd_n AND dma_wr_n)`
- Read phase: `dma_mreq_cycle = 1` when source is memory, `0` when source is I/O
- Write phase: `dma_mreq_cycle = 1` when dest is memory, `0` when dest is I/O

### 11. Address Mode Combinations (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 11.1 | Both increment (A->B) | addrMode "01" for both, dir A->B | src++, dest++ per byte |
| 11.2 | Both decrement (A->B) | addrMode "00" for both | src--, dest-- per byte |
| 11.3 | Source inc, dest dec | Mixed modes | Verify independent addressing |
| 11.4 | Source dec, dest fixed | dec + fixed | Only src changes |
| 11.5 | Both fixed (port-to-port) | Both "10" or "11" | Neither address changes |
| 11.6 | Address wrap at 0xFFFF | Start near 0xFFFF, increment | Wraps to 0x0000 (16-bit) |

VHDL address update (in TRANSFERING_WRITE_1):
- Source increments: `dma_src_s <= dma_src_s + 1` when (A->B and portA_addrMode="01") or (B->A and portB_addrMode="01")
- Source decrements: `dma_src_s <= dma_src_s - 1` when mode="00"
- Fixed: no update (modes "10", "11")
- Same logic for dest with reversed port mapping

### 12. Transfer Modes (~8 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 12.1 | Continuous mode — full block | `R4_mode_s = "01"` (default) | Bus held for entire block |
| 12.2 | Burst mode — no prescaler | `R4_mode_s = "10"`, prescaler=0 | Transfers without bus release |
| 12.3 | Burst mode — with prescaler | Prescaler > 0 | Bus released during WAITING_CYCLES |
| 12.4 | Burst mode — bus release timing | `cpu_busreq_n_s <= '1'` in WAITING_CYCLES | BUSREQ deasserted during wait |
| 12.5 | Burst mode — bus re-request | After prescaler expires, START_DMA if bus was released | DMA re-requests bus |
| 12.6 | Byte mode — single byte | `R4_mode_s = "00"` | One byte per enable command |
| 12.7 | Continuous mode — no prescaler delay | Even with prescaler set, continuous uses it | Prescaler still applies between bytes |
| 12.8 | Burst mode — prescaler vs timer | `R2_portB_preescaler_s > DMA_timer_s(13:5)` | Timer scaled by CPU speed |

### 13. Prescaler and Timing (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 13.1 | Prescaler = 0 (no wait) | Default 0x00 | No WAITING_CYCLES state entered |
| 13.2 | Prescaler > 0 at 3.5MHz | Timer increments by 8 per clock | Wait cycles proportional |
| 13.3 | Prescaler > 0 at 7MHz | Timer increments by 4 per clock | Double the clocks vs 3.5MHz |
| 13.4 | Prescaler > 0 at 14MHz | Timer increments by 2 per clock | 4x clocks vs 3.5MHz |
| 13.5 | Prescaler > 0 at 28MHz | Timer increments by 1 per clock | 8x clocks vs 3.5MHz |
| 13.6 | Prescaler comparison | `('0' & R2_portB_preescaler_s) > DMA_timer_s(13:5)` | Timer top 9 bits compared to 9-bit prescaler |

VHDL timer logic:
```
DMA_timer_s is 14 bits. Each clock it increments by:
  3.5MHz: +8  (bits 13:3 advance by 1 per clock)
  7MHz:   +4  (bits 13:2 advance by 1 per clock)
  14MHz:  +2  (bits 13:1 advance by 1 per clock)
  28MHz:  +1  (bits 13:0 advance by 1 per clock)
Timer is reset to 0 at TRANSFERING_READ_1.
Comparison uses DMA_timer_s(13:5) vs prescaler (9 bits).
```

This means the prescaler value represents a delay in units of 32 base clocks
at 3.5MHz (since 8 increments per clock * 4 clocks to shift into bits 13:5
= 32 base clocks per prescaler unit). At higher speeds, more real clocks
elapse for the same prescaler value, keeping the delay constant in real time.

### 14. Counter Behaviour — ZXN vs Z80 Mode (~8 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 14.1 | ZXN mode: counter starts at 0 | `dma_counter_s <= (others=>'0')` when dma_mode=0 | Counter = 0 after Load |
| 14.2 | Z80 mode: counter starts at 0xFFFF | `dma_counter_s <= (others=>'1')` when dma_mode=1 | Counter = 0xFFFF after Load |
| 14.3 | Counter increments per byte | `dma_counter_s <= dma_counter_s + 1` in WRITE_1 | Counter = N after N bytes |
| 14.4 | ZXN: block_len=5 transfers 5 bytes | `counter < block_len` -> 0,1,2,3,4 < 5 | 5 bytes transferred |
| 14.5 | Z80: block_len=5 transfers 6 bytes | Counter wraps: FFFF,0,1,2,3,4,5 — wait, 0xFFFF+1=0, etc. | 6 bytes (block_len+1) |
| 14.6 | ZXN: block_len=0 transfers 0 bytes | 0 < 0 is false immediately | No transfer |
| 14.7 | Z80: block_len=0 transfers 1 byte | 0xFFFF < 0 is false... wait, unsigned: 0xFFFF < 0 is false | Need to verify edge case |
| 14.8 | Counter readback accuracy | Read counter via read sequence | Matches internal counter |

Note: In Z80 mode, counter starts at 0xFFFF. After first byte: counter =
0x0000. Comparison `0x0000 < block_len` continues if block_len > 0.
For block_len = 0: 0xFFFF < 0 is false (unsigned), so 0 bytes transfer in
Z80 mode too. For block_len = 1: after first byte counter = 0x0000 < 1,
so a second byte transfers (counter becomes 0x0001, 1 < 1 is false, stop).
This means Z80 mode transfers block_len + 1 bytes for block_len >= 1.

### 15. Bus Arbitration (~8 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 15.1 | DMA requests bus before transfer | `cpu_busreq_n_s <= '0'` in START_DMA | BUSREQ asserted |
| 15.2 | DMA waits for bus acknowledge | WAITING_ACK checks `cpu_bai_n = '0'` | No transfer until ACK |
| 15.3 | DMA releases bus when idle | `cpu_busreq_n_s <= '1'` in IDLE | BUSREQ deasserted |
| 15.4 | DMA defers to external BUSREQ | START_DMA: `if bus_busreq_n_i = '0' ... wait` | DMA waits for other bus master |
| 15.5 | DMA defers to daisy chain | START_DMA: `if cpu_bai_n = '0' ... wait` | DMA waits if downstream busy |
| 15.6 | DMA defers to IM2 delay | START_DMA: `if dma_delay_i = '1' ... wait` | DMA waits for interrupt |
| 15.7 | Bus mux when DMA holds bus | `cpu_a <= dma_a when dma_holds_bus` | Address/data from DMA on bus |
| 15.8 | DMA cannot self-program | `port_dma_rd/wr` gated by `not dma_holds_bus` | Port writes ignored during transfer |

### 16. Auto-Restart and Continue (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 16.1 | Auto-restart reloads addresses | FINISH_DMA: reloads src/dest from start regs | Addresses reset to start |
| 16.2 | Auto-restart reloads counter | Counter reset (0 or 0xFFFF by mode) | Counter at initial value |
| 16.3 | Auto-restart direction A->B | `dma_src_s <= R0_start_addr_port_A_s` | Correct reload |
| 16.4 | Auto-restart direction B->A | `dma_src_s <= R4_start_addr_port_B_s` | Reversed reload |
| 16.5 | Continue preserves addresses | 0xD3 resets counter but not src/dest | Transfer resumes from current position |
| 16.6 | Continue vs Load | Load resets both addresses and counter; Continue only counter | Different behaviour verified |

### 17. Status Register and Read Sequence (~10 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 17.1 | Status byte format | `"00" & status_endofblock_n & "1101" & status_atleastone` | Bits 5:2 = "1101" fixed |
| 17.2 | End-of-block flag clear initially | `status_endofblock_n = '1'` (bit 5 = 1) | Bit 5 = 1 means not ended |
| 17.3 | End-of-block set after transfer | `status_endofblock_n <= '0'` in FINISH_DMA | Bit 5 = 0 after block done |
| 17.4 | At-least-one flag | `status_atleastone <= '1'` in WRITE_4 | Bit 0 = 1 after first byte |
| 17.5 | Status cleared by 0x8B | Both flags reset | Status = 0x2E (00_1_01101_0) |
| 17.6 | Status cleared by 0xC3 (reset) | Both flags reset | Status = 0x2E |
| 17.7 | Default read mask | `R6_read_mask_s <= "01111111"` on reset | All 7 fields enabled |
| 17.8 | Read sequence cycles through mask | Each read advances to next enabled field | 7 reads return all fields |
| 17.9 | Custom read mask (status+counter only) | Mask = 0x07 (bits 0,1,2) | Only 3 fields in sequence |
| 17.10 | Read sequence wraps around | After last enabled field, wraps to first | Cyclic readback |

Status byte layout: `[7:6]=00, [5]=endofblock_n, [4:1]=1101, [0]=atleastone`
- Initial/reset: `0b00_1_1101_0 = 0x3A` (end-of-block not reached, no bytes)
- After partial: `0b00_1_1101_1 = 0x3B` (not done, at least one byte)
- After complete: `0b00_0_1101_1 = 0x1B` (end of block, at least one byte)

### 18. Read Sequence Fields (~8 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 18.1 | Read status byte | `cpu_d_o <= status bits` | Correct status format |
| 18.2 | Read counter LO | `cpu_d_o <= dma_counter_s(7:0)` | Low byte of counter |
| 18.3 | Read counter HI | `cpu_d_o <= dma_counter_s(15:8)` | High byte of counter |
| 18.4 | Read port A addr LO (A->B) | `cpu_d_o <= dma_src_s(7:0)` | Current src address |
| 18.5 | Read port A addr HI (A->B) | `cpu_d_o <= dma_src_s(15:8)` | Current src address |
| 18.6 | Read port B addr LO (A->B) | `cpu_d_o <= dma_dest_s(7:0)` | Current dest address |
| 18.7 | Read port B addr HI (A->B) | `cpu_d_o <= dma_dest_s(15:8)` | Current dest address |
| 18.8 | Read port A/B in B->A mode | Port A = dest, port B = src | Direction-aware readback |

VHDL read logic for port addresses: When `R0_dir_AtoB_s = 1`, port A =
`dma_src_s` and port B = `dma_dest_s`. When `R0_dir_AtoB_s = 0`, port A =
`dma_dest_s` and port B = `dma_src_s`.

### 19. Reset Behaviour (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 19.1 | Hardware reset defaults | All signals in reset block | Verify all defaults |
| 19.2 | R6 0xC3 soft reset | Subset of signals reset | Verify partial reset |
| 19.3 | 0xC3 does not reset R0/R4 addresses | Not mentioned in 0xC3 handler | Addresses preserved |
| 19.4 | 0xC3 resets timing to "01" | Both port timings reset | 3-cycle timing default |
| 19.5 | 0xC3 resets prescaler to 0x00 | `R2_portB_preescaler_s <= x"00"` | No prescaler delay |
| 19.6 | 0xC3 resets auto-restart to 0 | `R5_auto_restart_s <= '0'` | Auto-restart off |

Hardware reset additionally resets: `dma_seq_s <= IDLE`, `dma_a_s <= 0`,
`dma_counter_s <= 0`, `cpu_busreq_n_s <= '1'`, `R4_mode_s <= "01"`,
`R6_read_mask_s <= "01111111"`, `read_count_s <= "000"`,
`reg_rd_seq_s := RD_STATUS`.

### 20. DMA Delay and Interrupt Integration (~4 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 20.1 | DMA delay blocks START_DMA | `if dma_delay_i = '1' then ... wait` | Transfer deferred |
| 20.2 | DMA delay mid-transfer | After WRITE_4: `if dma_delay_i = '1' then dma_seq_s <= START_DMA` | Transfer interrupted, re-requests bus |
| 20.3 | IM2 DMA interrupt enable regs | NextREGs 0xCC, 0xCD, 0xCE | Enable bits correctly wired |
| 20.4 | DMA delay signal composition | `im2_dma_delay = im2_dma_int OR (nmi AND nr_cc_7) OR (delay AND dma_delay)` | Correct delay logic |

### 21. Timing Byte Effects (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 21.1 | Timing "00" = 4-cycle read/write | READ_1 -> READ_2 (4 states) | Slowest timing |
| 21.2 | Timing "01" = 3-cycle (default) | READ_1 -> READ_3 (3 states) | Default timing |
| 21.3 | Timing "10" = 2-cycle | READ_1 -> READ_4 (2 states) | Fastest timing |
| 21.4 | Timing "11" = 4-cycle | Same as "00" (others branch) | Falls through to 4-cycle |
| 21.5 | Read timing from source port | A->B: uses R1 timing; B->A: uses R2 timing | Source determines read timing |
| 21.6 | Write timing from dest port | A->B: uses R2 timing; B->A: uses R1 timing | Dest determines write timing |

### 22. Edge Cases and Corner Cases (~6 tests)

| # | Test | VHDL Reference | Verification |
|---|------|----------------|--------------|
| 22.1 | Disable during active transfer | Write 0x83 mid-transfer | DMA goes to IDLE immediately |
| 22.2 | Enable without Load | 0x87 without prior 0xCF | Uses whatever addresses are in registers |
| 22.3 | Multiple Loads before Enable | Program, Load, reprogram, Load, Enable | Last Load values used |
| 22.4 | Continue after auto-restart | 0xD3 during auto-restart loop | Counter reset, addresses preserved |
| 22.5 | R0 register decoding ambiguity | Byte with bit7=0, bit2:0 match both R0 and R1 or R2 | VHDL processes all matching if-blocks |
| 22.6 | Simultaneous R0/R2 decode | Byte 0x00 matches R2 (bits 2:0="000") AND R0 if bits 0 or 1 set | R0 requires bit 0 or 1 set; 0x00 only matches R2 |

VHDL note on register decode: The register write decode in IDLE state uses
non-exclusive if statements. R0 matches when `bit7=0 AND (bit1=1 OR bit0=1)`.
R1 matches when `bit7=0 AND bits2:0="100"`. R2 matches when `bit7=0 AND
bits2:0="000"`. These are mutually exclusive since R0 requires bit0 or bit1
set, while R1 has bit2=1 and R2 has bits2:0=000.

## Test Count Summary

| Section | Tests |
|---------|------:|
| 1. Port decoding and mode | 6 |
| 2. R0 programming | 8 |
| 3. R1 programming | 6 |
| 4. R2 programming | 8 |
| 5. R3 programming | 4 |
| 6. R4 programming | 8 |
| 7. R5 programming | 4 |
| 8. R6 commands | 16 |
| 9. Memory-to-memory | 8 |
| 10. Memory-to-IO | 6 |
| 11. Address modes | 6 |
| 12. Transfer modes | 8 |
| 13. Prescaler and timing | 6 |
| 14. Counter behaviour | 8 |
| 15. Bus arbitration | 8 |
| 16. Auto-restart/continue | 6 |
| 17. Status and read sequence | 10 |
| 18. Read sequence fields | 8 |
| 19. Reset behaviour | 6 |
| 20. DMA delay/interrupt | 4 |
| 21. Timing bytes | 6 |
| 22. Edge cases | 6 |
| **Total** | **~142** |

## Implementation Notes

### Test Harness Requirements

The DMA test harness needs:

1. **Port I/O interception** -- Capture all DMA port writes (0x6B/0x0B) and
   provide configurable read responses. Also capture I/O from DMA-initiated
   transfers (when DMA drives IORQ).

2. **Memory inspection** -- Read/write arbitrary memory locations to set up
   source data and verify destination contents after transfer.

3. **Cycle counting** -- Count CPU cycles consumed by DMA transfers to verify
   timing behaviour (prescaler, timing bytes).

4. **Bus signal monitoring** -- Track BUSREQ/BUSAK, MREQ/IORQ assertions to
   verify correct bus arbitration and signal generation.

5. **Multi-step programming** -- Each test writes a sequence of bytes to the
   DMA port to program registers, then issues Load + Enable commands.

### Typical Test Sequence (Memory-to-Memory)

```
; Program DMA for 16-byte copy from 0x8000 to 0x9000, A->B, both increment
OUT (0x6B), 0x7D    ; R0: A->B, transfer, addr LO+HI+len LO+len HI follow
OUT (0x6B), 0x00    ; Port A addr LO = 0x00
OUT (0x6B), 0x80    ; Port A addr HI = 0x80
OUT (0x6B), 0x10    ; Block len LO = 16
OUT (0x6B), 0x00    ; Block len HI = 0
OUT (0x6B), 0x14    ; R1: port A = memory, increment
OUT (0x6B), 0x10    ; R2: port B = memory, increment
OUT (0x6B), 0xAD    ; R4: continuous mode, port B addr follows
OUT (0x6B), 0x00    ; Port B addr LO = 0x00
OUT (0x6B), 0x90    ; Port B addr HI = 0x90
OUT (0x6B), 0xCF    ; Load
OUT (0x6B), 0x87    ; Enable DMA
; Run emulator, verify 0x9000-0x900F == 0x8000-0x800F
```

### VHDL Deviations from Z80-DMA Spec

The VHDL comment at the top states: "Loosely based on Zilog Z80C10. There
are differences!" Key observed deviations:

1. **Search mode not implemented** -- R0 search bit and R3 mask/match
   registers are commented out
2. **Interrupt control not implemented** -- R4 interrupt bytes commented out;
   R6 interrupt commands (0xAF, 0xAB, 0xA3, 0xB7) are no-ops
3. **Port A prescaler not implemented** -- R1_BYTE_1 is consumed but ignored
4. **Counter direction** -- ZXN mode counts up from 0; Z80 mode counts up
   from 0xFFFF (wrapping), affecting block length semantics
5. **Force Ready (0xB3) is a no-op**
6. **Ready active level (R5 bit 3) is ignored**

## File Layout

```
test/
  dma/
    dma_test.cpp              # Test runner
    dma_test_cases.h          # Test case definitions
  CMakeLists.txt              # Updated with DMA test target
doc/design/
  DMA-TEST-PLAN-DESIGN.md    # This document
```

## How to Run

```bash
# Build
cmake --build build -j$(nproc) 2>&1 | tail -5

# Run DMA tests standalone
./build/test/dma_test

# Run full regression suite (includes DMA)
bash test/regression.sh
```
