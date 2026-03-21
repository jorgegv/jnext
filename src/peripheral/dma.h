#pragma once
#include <cstdint>
#include <functional>

/// DMA controller — Z80-DMA compatible + ZXN short mode.
///
/// The ZX Next DMA supports two programming interfaces, both writing
/// to the same underlying transfer engine:
///   Port 0x6B: ZXN DMA mode (dma_mode=0) — same register protocol,
///              counter initializes to 0
///   Port 0x0B: Z80-DMA compatible mode (dma_mode=1) — same register
///              protocol, counter initializes to 0xFFFF (-1)
///
/// The register write protocol is sequential: bytes written to the DMA
/// port are parsed by a state machine that tracks which register group
/// and sub-byte is being written.
///
/// VHDL reference: device/dma.vhd + zxnext.vhd (integration, port decode)
///
/// Transfer modes (R4 bits 6:5):
///   00 = byte-at-a-time (one byte per enable)
///   01 = continuous (transfer entire block)
///   10 = burst (transfer with prescaler pauses)
///
/// Address modes per port (2 bits):
///   00 = decrement
///   01 = increment
///   10 or 11 = fixed
///
/// Register groups (base byte identification):
///   WR0: bit7=0, (bit1=1 or bit0=1)  — direction, port A addr, block length
///   WR1: bit7=0, bits[2:0]=100       — port A config (IO/mem, addr mode, timing)
///   WR2: bit7=0, bits[2:0]=000       — port B config (IO/mem, addr mode, timing)
///   WR3: bit7=1, bits[1:0]=00        — DMA enable, match/mask
///   WR4: bit7=1, bits[1:0]=01        — mode, port B addr, interrupts
///   WR5: bits[7:6]=10, bits[2:0]=010 — ready/wait, auto-restart
///   WR6: bit7=1, bits[1:0]=11        — commands (RESET, ENABLE, etc.)

class Dma {
public:
    /// Address adjustment mode for source/destination.
    enum class AddrMode : uint8_t {
        DECREMENT = 0,
        INCREMENT = 1,
        FIXED     = 2
    };

    /// Transfer mode.
    enum class TransferMode : uint8_t {
        BYTE       = 0,   // one byte per enable
        CONTINUOUS = 1,   // entire block
        BURST      = 2    // with prescaler pauses
    };

    /// DMA transfer state.
    enum class State : uint8_t {
        IDLE,
        TRANSFERRING
    };

    Dma();

    void reset();

    /// Write a byte to the DMA port.
    /// @param val   data byte
    /// @param z80_compat  true if port 0x0B (Z80-DMA mode), false if port 0x6B (ZXN mode)
    void write(uint8_t val, bool z80_compat);

    /// Read from the DMA port — returns data according to the read sequence.
    uint8_t read();

    /// Execute a burst of transfers (called while DMA is active).
    /// @param max_bytes  maximum bytes to transfer this call
    /// @return actual number of bytes transferred
    int execute_burst(int max_bytes);

    /// Returns true when DMA is actively transferring (CPU should stall).
    bool is_active() const { return state_ == State::TRANSFERRING; }

    // ── Callbacks ─────────────────────────────────────────────────────

    /// Memory read callback: addr -> byte
    std::function<uint8_t(uint16_t addr)> read_memory;

    /// Memory write callback: (addr, val)
    std::function<void(uint16_t addr, uint8_t val)> write_memory;

    /// I/O read callback: port -> byte
    std::function<uint8_t(uint16_t port)> read_io;

    /// I/O write callback: (port, val)
    std::function<void(uint16_t port, uint8_t val)> write_io;

    /// Fires when a transfer block completes (for IM2 DMA interrupt level).
    std::function<void()> on_interrupt;

    // ── Accessors for debug / testing ─────────────────────────────────

    State       state() const { return state_; }
    uint16_t    src_addr() const { return src_; }
    uint16_t    dst_addr() const { return dst_; }
    uint16_t    counter() const { return counter_; }
    uint16_t    block_length() const { return block_len_; }
    bool        dir_a_to_b() const { return dir_a_to_b_; }
    AddrMode    src_addr_mode() const;
    AddrMode    dst_addr_mode() const;
    TransferMode transfer_mode() const { return static_cast<TransferMode>(mode_); }

private:
    // ── Register write state machine ──────────────────────────────────

    /// States for the sequential register write protocol (from VHDL reg_wr_seq_t).
    enum class WrSeq : uint8_t {
        IDLE,
        R0_BYTE_0, R0_BYTE_1, R0_BYTE_2, R0_BYTE_3,
        R1_BYTE_0, R1_BYTE_1,
        R2_BYTE_0, R2_BYTE_1,
        R3_BYTE_0, R3_BYTE_1,
        R4_BYTE_0, R4_BYTE_1,
        R6_BYTE_0
    };

    /// States for the sequential register read protocol (from VHDL reg_rd_seq_t).
    enum class RdSeq : uint8_t {
        STATUS,
        COUNTER_LO, COUNTER_HI,
        PORT_A_LO, PORT_A_HI,
        PORT_B_LO, PORT_B_HI
    };

    /// Advance read sequence to the next field based on R6 read mask.
    /// @param after_bit  skip bits below this position when searching
    void advance_read_seq(int after_bit);

    /// Process R6 command byte (RESET, ENABLE, DISABLE, LOAD, etc.).
    void process_r6_command(uint8_t val);

    /// Perform the LOAD command: set src/dst from port A/B addresses per direction.
    void cmd_load();

    // ── DMA registers (matching VHDL signal names) ────────────────────

    // R0: direction + port A start address + block length
    bool     dir_a_to_b_ = true;          // R0_dir_AtoB_s: 1=A->B, 0=B->A
    uint16_t port_a_addr_ = 0;            // R0_start_addr_port_A_s
    uint16_t block_len_ = 0;              // R0_block_len_s

    // R1: port A configuration
    bool     port_a_is_io_ = false;       // R1_portAisIO_s
    uint8_t  port_a_addr_mode_ = 1;       // R1_portA_addrMode_s (00=dec, 01=inc, 10/11=fixed)
    uint8_t  port_a_timing_ = 1;          // R1_portA_timming_byte_s (cycle count)

    // R2: port B configuration
    bool     port_b_is_io_ = false;       // R2_portBisIO_s
    uint8_t  port_b_addr_mode_ = 1;       // R2_portB_addrMode_s
    uint8_t  port_b_timing_ = 1;          // R2_portB_timming_byte_s
    uint8_t  port_b_prescaler_ = 0;       // R2_portB_preescaler_s

    // R3: DMA enable
    bool     dma_en_ = false;             // R3_dma_en_s

    // R4: mode + port B start address
    uint8_t  mode_ = 1;                   // R4_mode_s (00=byte, 01=continuous, 10=burst)
    uint16_t port_b_addr_ = 0;            // R4_start_addr_port_B_s

    // R5: auto-restart, CE/WAIT
    bool     ce_wait_ = false;            // R5_ce_wait_s
    bool     auto_restart_ = false;       // R5_auto_restart_s

    // R6: read mask
    uint8_t  read_mask_ = 0x7F;           // R6_read_mask_s

    // ── Transfer state ────────────────────────────────────────────────

    State    state_ = State::IDLE;
    uint16_t src_ = 0;                    // current source address (dma_src_s)
    uint16_t dst_ = 0;                    // current destination address (dma_dest_s)
    uint16_t counter_ = 0;               // bytes transferred counter (dma_counter_s)

    // ── Status bits ───────────────────────────────────────────────────

    bool     status_at_least_one_ = false;   // status_atleastone
    bool     status_end_of_block_ = false;   // !status_endofblock_n (inverted for clarity)

    // ── Write/read state machine ──────────────────────────────────────

    WrSeq    wr_seq_ = WrSeq::IDLE;
    RdSeq    rd_seq_ = RdSeq::STATUS;
    uint8_t  reg_temp_ = 0;              // stores base register byte for sub-byte sequencing
    bool     z80_compat_ = false;        // current DMA mode (latched on each port access)
};
