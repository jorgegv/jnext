#include "peripheral/dma.h"
#include "core/log.h"
#include "core/saveable.h"

// ─── DMA logger ──────────────────────────────────────────────────────

static std::shared_ptr<spdlog::logger>& dma_log() {
    static auto l = [] {
        auto existing = spdlog::get("dma");
        if (existing) return existing;
        auto logger = spdlog::stderr_color_mt("dma");
        logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return logger;
    }();
    return l;
}

// ─── Dma implementation ──────────────────────────────────────────────

Dma::Dma() {
    reset();
}

void Dma::reset() {
    state_ = State::IDLE;

    src_ = 0;
    dst_ = 0;
    counter_ = 0;

    dir_a_to_b_ = true;
    port_a_addr_ = 0;
    block_len_ = 0;

    port_a_is_io_ = false;
    port_a_addr_mode_ = 1;  // increment
    port_a_timing_ = 1;     // 3-cycle default

    port_b_is_io_ = false;
    port_b_addr_mode_ = 1;
    port_b_timing_ = 1;
    port_b_prescaler_ = 0;

    dma_en_ = false;
    mode_ = 1;  // continuous
    port_b_addr_ = 0;

    ce_wait_ = false;
    auto_restart_ = false;
    read_mask_ = 0x7F;

    status_at_least_one_ = false;
    status_end_of_block_ = false;

    burst_wait_ = 0;

    wr_seq_ = WrSeq::IDLE;
    rd_seq_ = RdSeq::STATUS;
    reg_temp_ = 0;
    z80_compat_ = false;

    dma_log()->info("DMA reset");
}

Dma::AddrMode Dma::src_addr_mode() const {
    // Source is port A when dir A->B, port B when dir B->A
    uint8_t mode = dir_a_to_b_ ? port_a_addr_mode_ : port_b_addr_mode_;
    if (mode >= 2) return AddrMode::FIXED;
    return static_cast<AddrMode>(mode);
}

Dma::AddrMode Dma::dst_addr_mode() const {
    uint8_t mode = dir_a_to_b_ ? port_b_addr_mode_ : port_a_addr_mode_;
    if (mode >= 2) return AddrMode::FIXED;
    return static_cast<AddrMode>(mode);
}

// ─── Register write protocol ─────────────────────────────────────────

void Dma::write(uint8_t val, bool z80_compat) {
    z80_compat_ = z80_compat;

    // If we are in a sub-byte sequence, process the continuation byte
    if (wr_seq_ != WrSeq::IDLE) {
        switch (wr_seq_) {

        // ── R0 sub-bytes: port A address + block length ──────────
        case WrSeq::R0_BYTE_0:
            port_a_addr_ = (port_a_addr_ & 0xFF00) | val;
            if (reg_temp_ & 0x10)
                wr_seq_ = WrSeq::R0_BYTE_1;
            else if (reg_temp_ & 0x20)
                wr_seq_ = WrSeq::R0_BYTE_2;
            else if (reg_temp_ & 0x40)
                wr_seq_ = WrSeq::R0_BYTE_3;
            else
                wr_seq_ = WrSeq::IDLE;
            break;

        case WrSeq::R0_BYTE_1:
            port_a_addr_ = (port_a_addr_ & 0x00FF) | (static_cast<uint16_t>(val) << 8);
            if (reg_temp_ & 0x20)
                wr_seq_ = WrSeq::R0_BYTE_2;
            else if (reg_temp_ & 0x40)
                wr_seq_ = WrSeq::R0_BYTE_3;
            else
                wr_seq_ = WrSeq::IDLE;
            break;

        case WrSeq::R0_BYTE_2:
            block_len_ = (block_len_ & 0xFF00) | val;
            if (reg_temp_ & 0x40)
                wr_seq_ = WrSeq::R0_BYTE_3;
            else
                wr_seq_ = WrSeq::IDLE;
            break;

        case WrSeq::R0_BYTE_3:
            block_len_ = (block_len_ & 0x00FF) | (static_cast<uint16_t>(val) << 8);
            wr_seq_ = WrSeq::IDLE;
            break;

        // ── R1 sub-bytes: port A timing + prescaler ──────────────
        case WrSeq::R1_BYTE_0:
            port_a_timing_ = val & 0x03;
            if (val & 0x20)
                wr_seq_ = WrSeq::R1_BYTE_1;
            else
                wr_seq_ = WrSeq::IDLE;
            break;

        case WrSeq::R1_BYTE_1:
            // Port A prescaler — not used in VHDL, ignored
            wr_seq_ = WrSeq::IDLE;
            break;

        // ── R2 sub-bytes: port B timing + prescaler ──────────────
        case WrSeq::R2_BYTE_0:
            port_b_timing_ = val & 0x03;
            if (val & 0x20)
                wr_seq_ = WrSeq::R2_BYTE_1;
            else
                wr_seq_ = WrSeq::IDLE;
            break;

        case WrSeq::R2_BYTE_1:
            port_b_prescaler_ = val;
            wr_seq_ = WrSeq::IDLE;
            break;

        // ── R3 sub-bytes: mask + match (unused in VHDL) ──────────
        case WrSeq::R3_BYTE_0:
            // R3 mask byte — not used
            if (reg_temp_ & 0x10)
                wr_seq_ = WrSeq::R3_BYTE_1;
            else
                wr_seq_ = WrSeq::IDLE;
            break;

        case WrSeq::R3_BYTE_1:
            // R3 match byte — not used
            wr_seq_ = WrSeq::IDLE;
            break;

        // ── R4 sub-bytes: port B address ─────────────────────────
        case WrSeq::R4_BYTE_0:
            port_b_addr_ = (port_b_addr_ & 0xFF00) | val;
            if (reg_temp_ & 0x08)
                wr_seq_ = WrSeq::R4_BYTE_1;
            else
                wr_seq_ = WrSeq::IDLE;
            break;

        case WrSeq::R4_BYTE_1:
            port_b_addr_ = (port_b_addr_ & 0x00FF) | (static_cast<uint16_t>(val) << 8);
            wr_seq_ = WrSeq::IDLE;
            break;

        // ── R6 sub-byte: read mask ───────────────────────────────
        case WrSeq::R6_BYTE_0:
            read_mask_ = val;
            // Also initialize read sequence based on mask
            advance_read_seq(-1);
            wr_seq_ = WrSeq::IDLE;
            break;

        default:
            wr_seq_ = WrSeq::IDLE;
            break;
        }

        dma_log()->trace("DMA wr_seq sub-byte: val={:#04x} next_seq={}", val, static_cast<int>(wr_seq_));
        return;
    }

    // ── Base register identification (from VHDL IDLE state) ──────────
    //
    // Multiple register patterns can match the same byte.  The VHDL
    // uses parallel if-statements (not if-elsif), so later matches
    // can override earlier ones.  The priority order in VHDL is:
    //   R0, R1, R2, R3, R4, R5, R6
    // with R6 winning if multiple match (since it is last).
    //
    // We implement the same priority: check from R6 down to R0,
    // taking the first (= highest priority) match.

    // Register 6: bit7=1, bits[1:0]=11
    if ((val & 0x83) == 0x83) {
        process_r6_command(val);
        return;
    }

    // Register 5: bits[7:6]=10, bits[2:0]=010
    if ((val & 0xC7) == 0x82) {
        reg_temp_ = val;
        ce_wait_ = (val >> 4) & 1;
        auto_restart_ = (val >> 5) & 1;
        wr_seq_ = WrSeq::IDLE;
        dma_log()->debug("R5: ce_wait={} auto_restart={}", ce_wait_, auto_restart_);
        return;
    }

    // Register 4: bit7=1, bits[1:0]=01
    if ((val & 0x83) == 0x81) {
        reg_temp_ = val;
        mode_ = (val >> 5) & 0x03;
        dma_log()->debug("R4: mode={} ({})", mode_,
                         mode_ == 0 ? "byte" : mode_ == 1 ? "continuous" : "burst");
        if (val & 0x04)
            wr_seq_ = WrSeq::R4_BYTE_0;
        else if (val & 0x08)
            wr_seq_ = WrSeq::R4_BYTE_1;
        else
            wr_seq_ = WrSeq::IDLE;
        return;
    }

    // Register 3: bit7=1, bits[1:0]=00
    if ((val & 0x83) == 0x80) {
        reg_temp_ = val;
        dma_en_ = (val >> 6) & 1;
        dma_log()->debug("R3: dma_en={}", dma_en_);

        if (dma_en_) {
            state_ = State::TRANSFERRING;
            status_at_least_one_ = false;
            dma_log()->info("DMA enabled via R3 -> TRANSFERRING");
        }

        if (val & 0x08)
            wr_seq_ = WrSeq::R3_BYTE_0;
        else if (val & 0x10)
            wr_seq_ = WrSeq::R3_BYTE_1;
        else
            wr_seq_ = WrSeq::IDLE;
        return;
    }

    // Register 2: bit7=0, bits[2:0]=000
    if ((val & 0x87) == 0x00) {
        reg_temp_ = val;
        port_b_is_io_ = (val >> 3) & 1;
        port_b_addr_mode_ = (val >> 4) & 0x03;
        dma_log()->debug("R2: portB is_io={} addr_mode={}", port_b_is_io_, port_b_addr_mode_);

        if (val & 0x40)
            wr_seq_ = WrSeq::R2_BYTE_0;
        else
            wr_seq_ = WrSeq::IDLE;
        return;
    }

    // Register 1: bit7=0, bits[2:0]=100
    if ((val & 0x87) == 0x04) {
        reg_temp_ = val;
        port_a_is_io_ = (val >> 3) & 1;
        port_a_addr_mode_ = (val >> 4) & 0x03;
        dma_log()->debug("R1: portA is_io={} addr_mode={}", port_a_is_io_, port_a_addr_mode_);

        if (val & 0x40)
            wr_seq_ = WrSeq::R1_BYTE_0;
        else
            wr_seq_ = WrSeq::IDLE;
        return;
    }

    // Register 0: bit7=0, (bit1=1 or bit0=1)
    if ((val & 0x80) == 0 && (val & 0x03) != 0) {
        reg_temp_ = val;
        dir_a_to_b_ = (val >> 2) & 1;
        dma_log()->debug("R0: dir={}", dir_a_to_b_ ? "A->B" : "B->A");

        if (val & 0x08)
            wr_seq_ = WrSeq::R0_BYTE_0;
        else if (val & 0x10)
            wr_seq_ = WrSeq::R0_BYTE_1;
        else if (val & 0x20)
            wr_seq_ = WrSeq::R0_BYTE_2;
        else if (val & 0x40)
            wr_seq_ = WrSeq::R0_BYTE_3;
        else
            wr_seq_ = WrSeq::IDLE;
        return;
    }

    dma_log()->trace("DMA write: unrecognized base byte {:#04x}", val);
}

// ─── R6 command processing ───────────────────────────────────────────

void Dma::process_r6_command(uint8_t val) {
    wr_seq_ = WrSeq::IDLE;

    switch (val) {
    case 0xC3:  // RESET
        dma_log()->debug("R6: RESET");
        state_ = State::IDLE;
        status_end_of_block_ = false;
        status_at_least_one_ = false;
        port_a_timing_ = 1;
        port_b_timing_ = 1;
        port_b_prescaler_ = 0;
        ce_wait_ = false;
        auto_restart_ = false;
        break;

    case 0xC7:  // Reset port A timing
        dma_log()->debug("R6: reset port A timing");
        port_a_timing_ = 1;
        break;

    case 0xCB:  // Reset port B timing
        dma_log()->debug("R6: reset port B timing");
        port_b_timing_ = 1;
        break;

    case 0xCF:  // LOAD
        dma_log()->debug("R6: LOAD");
        cmd_load();
        break;

    case 0xD3:  // CONTINUE (reload counter only, keep current addresses)
        dma_log()->debug("R6: CONTINUE");
        status_end_of_block_ = false;
        if (!z80_compat_)
            counter_ = 0;
        else
            counter_ = 0xFFFF;
        break;

    case 0xAF:  // Disable interrupts
        dma_log()->debug("R6: disable interrupts");
        break;

    case 0xAB:  // Enable interrupts
        dma_log()->debug("R6: enable interrupts");
        break;

    case 0xA3:  // Reset and disable interrupts
        dma_log()->debug("R6: reset+disable interrupts");
        break;

    case 0xB7:  // Enable after RETI
        dma_log()->debug("R6: enable after RETI");
        break;

    case 0xBF:  // Read status byte
        dma_log()->debug("R6: read status byte");
        rd_seq_ = RdSeq::STATUS;
        break;

    case 0x8B:  // Reinitialize status byte
        dma_log()->debug("R6: reinit status byte");
        status_end_of_block_ = false;
        status_at_least_one_ = false;
        break;

    case 0xA7:  // Initialize read sequence
        dma_log()->debug("R6: init read sequence");
        advance_read_seq(-1);
        break;

    case 0xB3:  // Force ready
        dma_log()->debug("R6: force ready");
        break;

    case 0x87:  // Enable DMA
        dma_log()->info("R6: ENABLE DMA -> TRANSFERRING");
        state_ = State::TRANSFERRING;
        status_at_least_one_ = false;
        break;

    case 0x83:  // Disable DMA
        dma_log()->info("R6: DISABLE DMA -> IDLE");
        state_ = State::IDLE;
        break;

    case 0xBB:  // Read mask follows
        dma_log()->debug("R6: read mask follows");
        wr_seq_ = WrSeq::R6_BYTE_0;
        break;

    default:
        dma_log()->trace("R6: unhandled command {:#04x}", val);
        break;
    }
}

// ─── Read protocol ───────────────────────────────────────────────────

uint8_t Dma::read() {
    uint8_t result = 0;

    switch (rd_seq_) {
    case RdSeq::STATUS:
        // Status byte from VHDL: "00" & status_endofblock_n & "1101" & status_atleastone
        // Bit layout: [7:6]=00, [5]=endofblock_n, [4:1]=1101, [0]=atleastone
        // Note: status_end_of_block_ stores the INVERTED sense (true = block ended),
        // but the VHDL outputs the _n signal directly (1 = NOT ended).
        result = static_cast<uint8_t>(
            (status_end_of_block_ ? 0x00 : 0x20) |  // bit 5: endofblock_n
            0x1A |                                    // bits [4:1] = 1101 = 0x1A
            (status_at_least_one_ ? 0x01 : 0x00)     // bit 0
        );
        break;

    case RdSeq::COUNTER_LO:
        result = counter_ & 0xFF;
        break;

    case RdSeq::COUNTER_HI:
        result = (counter_ >> 8) & 0xFF;
        break;

    case RdSeq::PORT_A_LO:
        // Port A is src when A->B, dst when B->A
        result = dir_a_to_b_ ? (src_ & 0xFF) : (dst_ & 0xFF);
        break;

    case RdSeq::PORT_A_HI:
        result = dir_a_to_b_ ? ((src_ >> 8) & 0xFF) : ((dst_ >> 8) & 0xFF);
        break;

    case RdSeq::PORT_B_LO:
        result = dir_a_to_b_ ? (dst_ & 0xFF) : (src_ & 0xFF);
        break;

    case RdSeq::PORT_B_HI:
        result = dir_a_to_b_ ? ((dst_ >> 8) & 0xFF) : ((src_ >> 8) & 0xFF);
        break;
    }

    // Advance to next read field based on read mask
    advance_read_seq(static_cast<int>(rd_seq_));

    dma_log()->trace("DMA read: {:#04x}", result);
    return result;
}

// ─── Read sequence advancement ───────────────────────────────────────

void Dma::advance_read_seq(int after_bit) {
    // Scan read_mask_ for the next set bit after 'after_bit'.
    // The bit positions map to:
    //   0 = STATUS, 1 = COUNTER_LO, 2 = COUNTER_HI,
    //   3 = PORT_A_LO, 4 = PORT_A_HI, 5 = PORT_B_LO, 6 = PORT_B_HI
    //
    // Wraps around; defaults to STATUS if nothing set.

    for (int i = 1; i <= 7; ++i) {
        int bit = (after_bit + i) % 7;
        if (read_mask_ & (1 << bit)) {
            rd_seq_ = static_cast<RdSeq>(bit);
            return;
        }
    }

    // Default fallback
    rd_seq_ = RdSeq::STATUS;
}

// ─── LOAD command ────────────────────────────────────────────────────

void Dma::cmd_load() {
    status_end_of_block_ = false;

    if (dir_a_to_b_) {
        src_ = port_a_addr_;
        dst_ = port_b_addr_;
    } else {
        src_ = port_b_addr_;
        dst_ = port_a_addr_;
    }

    if (!z80_compat_)
        counter_ = 0;
    else
        counter_ = 0xFFFF;  // Z80 DMA loads -1

    dma_log()->debug("LOAD: src={:#06x} dst={:#06x} len={:#06x} dir={}",
                     src_, dst_, block_len_, dir_a_to_b_ ? "A->B" : "B->A");
}

// ─── Transfer execution ─────────────────────────────────────────────

int Dma::execute_burst(int max_bytes) {
    if (state_ != State::TRANSFERRING) return 0;

    int transferred = 0;

    // Determine source and destination properties
    bool src_is_io, dst_is_io;
    uint8_t src_mode, dst_mode;

    if (dir_a_to_b_) {
        src_is_io = port_a_is_io_;
        dst_is_io = port_b_is_io_;
        src_mode = port_a_addr_mode_;
        dst_mode = port_b_addr_mode_;
    } else {
        src_is_io = port_b_is_io_;
        dst_is_io = port_a_is_io_;
        src_mode = port_b_addr_mode_;
        dst_mode = port_a_addr_mode_;
    }

    while (transferred < max_bytes && state_ == State::TRANSFERRING) {
        // Read byte from source
        uint8_t data;
        if (src_is_io) {
            if (read_io) data = read_io(src_);
            else data = 0xFF;
        } else {
            if (read_memory) data = read_memory(src_);
            else data = 0xFF;
        }

        // Write byte to destination
        if (dst_is_io) {
            if (write_io) write_io(dst_, data);
        } else {
            if (write_memory) write_memory(dst_, data);
        }

        // Increment counter (counts up, compared against block_len_)
        counter_++;
        transferred++;
        status_at_least_one_ = true;

        // Adjust source address
        if (src_mode == 0x01)       src_++;       // increment
        else if (src_mode == 0x00)  src_--;       // decrement
        // else: fixed (10 or 11)

        // Adjust destination address
        if (dst_mode == 0x01)       dst_++;
        else if (dst_mode == 0x00)  dst_--;

        // Check for end of block
        if (counter_ >= block_len_) {
            // Transfer complete
            status_end_of_block_ = true;
            dma_log()->debug("DMA transfer complete: {} bytes", transferred);

            if (on_interrupt) {
                on_interrupt();
            }

            if (auto_restart_) {
                // Reload addresses and counter for next pass
                cmd_load();
                dma_log()->debug("DMA auto-restart");
            } else {
                state_ = State::IDLE;
            }
            break;
        }

        // In burst mode, transfer one byte then wait for prescaler cycles.
        // VHDL: waits until DMA_timer_s(13:5) >= prescaler, i.e. prescaler*32 master cycles.
        if (mode_ == 2) {  // burst
            if (port_b_prescaler_ > 0) {
                burst_wait_ = static_cast<int64_t>(port_b_prescaler_) * 32;
            }
            break;
        }
    }

    return transferred;
}

void Dma::tick_burst_wait(uint64_t master_cycles) {
    if (burst_wait_ > 0) {
        burst_wait_ -= static_cast<int64_t>(master_cycles);
        if (burst_wait_ < 0) burst_wait_ = 0;
    }
}

void Dma::save_state(StateWriter& w) const
{
    w.write_bool(dir_a_to_b_);
    w.write_u16(port_a_addr_);
    w.write_u16(block_len_);
    w.write_bool(port_a_is_io_);
    w.write_u8(port_a_addr_mode_);
    w.write_u8(port_a_timing_);
    w.write_bool(port_b_is_io_);
    w.write_u8(port_b_addr_mode_);
    w.write_u8(port_b_timing_);
    w.write_u8(port_b_prescaler_);
    w.write_bool(dma_en_);
    w.write_u8(mode_);
    w.write_u16(port_b_addr_);
    w.write_bool(ce_wait_);
    w.write_bool(auto_restart_);
    w.write_u8(read_mask_);
    w.write_u8(static_cast<uint8_t>(state_));
    w.write_u16(src_);
    w.write_u16(dst_);
    w.write_u16(counter_);
    w.write_bool(status_at_least_one_);
    w.write_bool(status_end_of_block_);
    w.write_u16(static_cast<uint16_t>(burst_wait_ > 0 ? burst_wait_ : 0));
    w.write_u8(static_cast<uint8_t>(wr_seq_));
    w.write_u8(static_cast<uint8_t>(rd_seq_));
    w.write_u8(reg_temp_);
    w.write_bool(z80_compat_);
}

void Dma::load_state(StateReader& r)
{
    dir_a_to_b_          = r.read_bool();
    port_a_addr_         = r.read_u16();
    block_len_           = r.read_u16();
    port_a_is_io_        = r.read_bool();
    port_a_addr_mode_    = r.read_u8();
    port_a_timing_       = r.read_u8();
    port_b_is_io_        = r.read_bool();
    port_b_addr_mode_    = r.read_u8();
    port_b_timing_       = r.read_u8();
    port_b_prescaler_    = r.read_u8();
    dma_en_              = r.read_bool();
    mode_                = r.read_u8();
    port_b_addr_         = r.read_u16();
    ce_wait_             = r.read_bool();
    auto_restart_        = r.read_bool();
    read_mask_           = r.read_u8();
    state_               = static_cast<State>(r.read_u8());
    src_                 = r.read_u16();
    dst_                 = r.read_u16();
    counter_             = r.read_u16();
    status_at_least_one_ = r.read_bool();
    status_end_of_block_ = r.read_bool();
    burst_wait_          = r.read_u16();
    wr_seq_              = static_cast<WrSeq>(r.read_u8());
    rd_seq_              = static_cast<RdSeq>(r.read_u8());
    reg_temp_            = r.read_u8();
    z80_compat_          = r.read_bool();
}
