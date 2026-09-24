#include "peripheral/dma.h"
#include "core/log.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

namespace {

// Enum name tables (design §6.2, §9.4). The binary encoding stays the u8
// ordinal the stream has always carried; the NAME is what a `.jns` writes, so
// renumbering any of these four FSMs becomes a visible schema diff rather
// than a silent re-interpretation of old files.
const char* const kStateNameArr[] = {
    "idle",          // Dma::State::IDLE (dma.h:56)
    "transferring",  // Dma::State::TRANSFERRING
};
const jnext::save::EnumNames kStateNames{
    kStateNameArr, sizeof(kStateNameArr) / sizeof(kStateNameArr[0])};

// Dma::WrSeq (dma.h:206) — VHDL reg_wr_seq_t, device/dma.vhd.
const char* const kWrSeqNameArr[] = {
    "idle",
    "r0_byte_0", "r0_byte_1", "r0_byte_2", "r0_byte_3",
    "r1_byte_0", "r1_byte_1",
    "r2_byte_0", "r2_byte_1",
    "r3_byte_0", "r3_byte_1",
    "r4_byte_0", "r4_byte_1",
    "r6_byte_0",
};
const jnext::save::EnumNames kWrSeqNames{
    kWrSeqNameArr, sizeof(kWrSeqNameArr) / sizeof(kWrSeqNameArr[0])};

// Dma::RdSeq (dma.h:217) — VHDL reg_rd_seq_t.
const char* const kRdSeqNameArr[] = {
    "status",
    "counter_lo", "counter_hi",
    "port_a_lo", "port_a_hi",
    "port_b_lo", "port_b_hi",
};
const jnext::save::EnumNames kRdSeqNames{
    kRdSeqNameArr, sizeof(kRdSeqNameArr) / sizeof(kRdSeqNameArr[0])};

// Dma::Phase (dma.h:244) — the bus-arbitration FSM of device/dma.vhd.
const char* const kPhaseNameArr[] = {
    "idle",            // Phase::IDLE
    "start_dma",       // Phase::START_DMA
    "waiting_ack",     // Phase::WAITING_ACK
    "transfer",        // Phase::TRANSFER
    "waiting_cycles",  // Phase::WAITING_CYCLES
};
const jnext::save::EnumNames kPhaseNames{
    kPhaseNameArr, sizeof(kPhaseNameArr) / sizeof(kPhaseNameArr[0])};

}  // namespace

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

    // VHDL dma.vhd:231 — DMA_timer_s cleared on reset.
    // turbo_ is NOT reset: it's a hardware input (VHDL :211-246 reset block
    // does not assign turbo_i).
    dma_timer_s_ = 0;
    in_waiting_cycles_ = false;

    wr_seq_ = WrSeq::IDLE;
    rd_seq_ = RdSeq::STATUS;
    reg_temp_ = 0;
    z80_compat_ = false;

    // Bus arbitration: VHDL dma.vhd:211-246 reset defaults.
    //   cpu_busreq_n_s <= '1'  (deasserted)
    //   cpu_bao_n <= cpu_bai_n (pass-through)
    phase_         = Phase::IDLE;
    cpu_busreq_n_  = true;
    cpu_bai_n_     = false;      // assume bus acknowledged (no external peer)
    cpu_bao_n_     = cpu_bai_n_; // pass-through
    bus_busreq_n_  = true;
    dma_delay_     = false;
    daisy_busy_    = false;

    dma_log()->debug("DMA reset");
}

// ─── Bus arbitration ─────────────────────────────────────────────────
//
// Mirrors the dma_seq_t FSM in VHDL dma.vhd:260-305 but uses a
// two-input simplification: cpu_bai_n_ gates WAITING_ACK (its VHDL-accurate
// role), while daisy_busy_ + bus_busreq_n_ + dma_delay_ gate START_DMA.
// In VHDL the START_DMA gate also reads cpu_bai_n = '0' to detect a daisy
// upstream peer already holding the bus; we split that concern off so the
// C++ default (cpu_bai_n_ = false = "CPU has granted the bus") does not
// spuriously close the START_DMA gate.

bool Dma::dma_holds_bus() const {
    // zxnext.vhd: `dma_holds_bus <= not cpu_busak_n`, which is equivalent to
    // "DMA has asserted busreq AND the CPU has acknowledged (bai_n=0)".
    // In WAITING_CYCLES (burst release) busreq is deasserted, so the DMA
    // no longer holds the bus (tests 12.4/12.5 observe this).
    return cpu_busreq_n_ == false && cpu_bai_n_ == false;
}

void Dma::tick_arbitration() {
    // Iterate twice so a single call can progress START_DMA -> WAITING_ACK
    // -> TRANSFER when all gate conditions are already satisfied.
    for (int i = 0; i < 2; ++i) {
        switch (phase_) {
        case Phase::IDLE:
            cpu_busreq_n_ = true;
            cpu_bao_n_    = cpu_bai_n_;   // pass-through (dma.vhd:226,263)
            return;

        case Phase::START_DMA:
            // VHDL dma.vhd:269 defer condition, adapted:
            //   bus_busreq_n_i = '0'  -> downstream wants bus
            //   cpu_bai_n     = '0'  -> daisy upstream already has bus
            //   dma_delay_i   = '1'  -> IM2 DMA delaying
            if (bus_busreq_n_ == false || daisy_busy_ || dma_delay_) {
                cpu_busreq_n_ = true;            // wait for other dma to finish
                cpu_bao_n_    = cpu_bai_n_;      // pass-through
                return;
            }
            // Gate open: request the bus.
            cpu_busreq_n_ = false;
            cpu_bao_n_    = true;
            phase_        = Phase::WAITING_ACK;
            break;   // fall through to WAITING_ACK on next iteration

        case Phase::WAITING_ACK:
            // VHDL dma.vhd:296: wait for CPU to drop cpu_bai_n low (= grant).
            if (cpu_bai_n_ == false) {
                phase_ = Phase::TRANSFER;
            }
            // Bus-request signal stays asserted while waiting.
            cpu_bao_n_ = true;
            return;

        case Phase::TRANSFER:
            cpu_busreq_n_ = false;
            cpu_bao_n_    = true;
            return;

        case Phase::WAITING_CYCLES:
            // Burst-mode bus release (dma.vhd:439-464): busreq deasserted,
            // bao pass-through; state is updated elsewhere when the wait ends.
            cpu_busreq_n_ = true;
            cpu_bao_n_    = cpu_bai_n_;
            return;
        }
    }
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

// ── Timing → cycle-count mapping (VHDL dma.vhd:313-316, :365-368) ────
//
// The VHDL READ and WRITE state machines decode the 2-bit timing byte
// into the number of cycles spent on each transfer phase:
//    "00" -> READ_2 / WRITE_2  (4 cycles)
//    "01" -> READ_3 / WRITE_3  (3 cycles)
//    "10" -> READ_4 / WRITE_4  (2 cycles)
//    "11" -> when others -> same as "00" (4 cycles)
//
// These helpers return the configured cycle counts for the side
// (source = read, destination = write) matching the current
// transfer direction.
static uint8_t timing_to_cycles(uint8_t t) {
    switch (t & 0x03) {
    case 0x00: return 4;
    case 0x01: return 3;
    case 0x02: return 2;
    case 0x03: return 4;
    }
    return 4;  // unreachable
}

uint8_t Dma::read_cycles() const {
    // Source side: A->B reads from port A (R1), B->A reads from port B (R2).
    uint8_t t = dir_a_to_b_ ? port_a_timing_ : port_b_timing_;
    return timing_to_cycles(t);
}

uint8_t Dma::write_cycles() const {
    // Dest side: A->B writes to port B (R2), B->A writes to port A (R1).
    uint8_t t = dir_a_to_b_ ? port_b_timing_ : port_a_timing_;
    return timing_to_cycles(t);
}

// ─── Register write protocol ─────────────────────────────────────────

void Dma::write(uint8_t val, bool z80_compat) {
    // zxnext.vhd gates port_dma_rd/wr on `not dma_holds_bus`: while the DMA
    // owns the bus the CPU cannot observe its own port writes.  That gate
    // is enforced externally by the caller (port dispatch); tests can verify
    // the behaviour via dma_holds_bus() directly.  See test 15.8.

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

        // Guarded — as every per-port-access trace in this file — for the
        // should_log() reason given in PortDispatch::read
        // (src/port/port_dispatch.cpp): unguarded, a trace() with arguments
        // is an out-of-line call per DMA port byte with tracing off (GH #244).
        if (dma_log()->should_log(spdlog::level::trace))
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
            phase_ = Phase::START_DMA;
            status_at_least_one_ = false;
            in_waiting_cycles_ = false;
            dma_log()->debug("DMA enabled via R3 -> TRANSFERRING");
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

    if (dma_log()->should_log(spdlog::level::trace))
        dma_log()->trace("DMA write: unrecognized base byte {:#04x}", val);
}

// ─── R6 command processing ───────────────────────────────────────────

void Dma::process_r6_command(uint8_t val) {
    wr_seq_ = WrSeq::IDLE;

    switch (val) {
    case 0xC3:  // RESET
        dma_log()->debug("R6: RESET");
        state_ = State::IDLE;
        phase_ = Phase::IDLE;
        cpu_busreq_n_ = true;
        cpu_bao_n_    = cpu_bai_n_;
        status_end_of_block_ = false;
        status_at_least_one_ = false;
        port_a_timing_ = 1;
        port_b_timing_ = 1;
        port_b_prescaler_ = 0;
        ce_wait_ = false;
        auto_restart_ = false;
        in_waiting_cycles_ = false;
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
        dma_log()->debug("R6: ENABLE DMA -> TRANSFERRING");
        state_ = State::TRANSFERRING;
        phase_ = Phase::START_DMA;
        status_at_least_one_ = false;
        in_waiting_cycles_ = false;
        break;

    case 0x83:  // Disable DMA
        dma_log()->debug("R6: DISABLE DMA -> IDLE");
        state_ = State::IDLE;
        phase_ = Phase::IDLE;
        cpu_busreq_n_ = true;
        cpu_bao_n_    = cpu_bai_n_;
        in_waiting_cycles_ = false;
        break;

    case 0xBB:  // Read mask follows
        dma_log()->debug("R6: read mask follows");
        wr_seq_ = WrSeq::R6_BYTE_0;
        break;

    default:
        if (dma_log()->should_log(spdlog::level::trace))
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

    if (dma_log()->should_log(spdlog::level::trace))
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
    // GH #106 — fresh accumulator per call so a caller reading
    // last_burst_read_wait_tstates() after a zero-transfer call sees 0.
    last_burst_read_wait_tstates_ = 0;

    if (state_ != State::TRANSFERRING) return 0;

    // VHDL dma.vhd:439-464 — while the state machine is in WAITING_CYCLES
    // it loops on itself until the prescaler comparison opens.  Model:
    // if we previously latched "waiting", keep returning 0 until a tick
    // has advanced DMA_timer_s enough for the gate to open.  The flag
    // (rather than prescaler_wait_active() alone) is what keeps the
    // VERY FIRST byte from being skipped: on enable, timer=0 but the
    // VHDL machine is at IDLE/START_DMA, not WAITING_CYCLES.
    if (in_waiting_cycles_) {
        if (prescaler_wait_active()) return 0;
        in_waiting_cycles_ = false;
    }

    // Progress the arbitration FSM (START_DMA -> WAITING_ACK -> TRANSFER).
    // Defaults on the external inputs let this complete in one call; tests
    // exercising deferral (15.3-15.6, 20.1) drive the inputs first.
    tick_arbitration();

    // If the bus has not been granted yet, no transfer this tick.
    if (phase_ != Phase::TRANSFER) return 0;

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

    while (transferred < max_bytes && state_ == State::TRANSFERRING
           && phase_ == Phase::TRANSFER) {
        // VHDL dma.vhd:309 — entering TRANSFERING_READ_1 clears DMA_timer_s.
        // Reset the timer at the START of each source-byte read so the
        // prescaler comparison measures the post-read elapsed time.
        dma_timer_s_ = 0;

        // Read byte from source
        uint8_t data;
        if (src_is_io) {
            if (read_io) data = read_io(src_);
            else data = 0xFF;
        } else {
            if (read_memory) data = read_memory(src_);
            else data = 0xFF;
            // GH #106 — 28 MHz SRAM read wait on DMA-driven read cycles
            // (zxnext.vhd:3171-3181). During dma_holds_bus the DMA drives
            // cpu_mreq_n/cpu_rd_n/cpu_a (zxnext.vhd:1828-1835), so this
            // source memory read is a real MREQ+RD machine cycle and
            // stretches like a CPU read; the wait reaches the DMA via
            // dma_wait_n (zxnext.vhd:1839,1844). Memory source only:
            // I/O reads are IORQ cycles (sram_memcycle needs
            // cpu_mreq_n='0', zxnext.vhd:3144); the destination write
            // below never waits (cpu_rd_n='0' required, zxnext.vhd:3175).
            if (read_mem_wait_tstates)
                last_burst_read_wait_tstates_ += read_mem_wait_tstates(src_);
        }

        // Write byte to destination
        if (dst_is_io) {
            // GH #230: copy before invoking. This is the outermost frame of
            // the one route by which a guest can destroy a live callable
            // through NR 0x02 bit 0 — an I/O destination of port 0x253B
            // reaches PortDispatch::write and then the NR 0x02 write handler,
            // which calls Emulator::soft_reset() -> Emulator::init(), and
            // init() reassigns dma_.write_io while this call is still on the
            // stack. Same fix and same reasoning as PortDispatch::write and
            // NextReg::write; the copy is allocation-free for the `[this]`
            // lambda Emulator::init() installs.
            if (write_io) {
                const auto io_write = write_io;
                io_write(dst_, data);
            }
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
                phase_ = Phase::START_DMA;
                dma_log()->debug("DMA auto-restart");
            } else {
                state_ = State::IDLE;
                phase_ = Phase::IDLE;
                cpu_busreq_n_ = true;
                cpu_bao_n_    = cpu_bai_n_;
            }
            in_waiting_cycles_ = false;
            break;
        }

        // VHDL dma.vhd:420-432: mid-transfer the FSM re-evaluates dma_delay_i
        // and drops back to START_DMA when it is asserted, releasing the bus.
        // Observable via cpu_busreq_n() returning true momentarily.
        if (dma_delay_) {
            phase_ = Phase::START_DMA;
            cpu_busreq_n_ = true;
            cpu_bao_n_    = cpu_bai_n_;
            break;
        }

        // VHDL dma.vhd:424 enters WAITING_CYCLES whenever the prescaler
        // comparison is active, regardless of transfer mode.  With the
        // per-byte timer reset above, DMA_timer_s(13:5) is still 0 here,
        // so any nonzero prescaler immediately trips the wait gate.
        // In burst mode the CPU bus is released during the wait
        // (:441-449) and phase_ moves to WAITING_CYCLES so cpu_busreq_n()
        // reflects it; in other modes the bus stays held.  is_active()
        // handles the burst vs non-burst CPU-stall distinction.
        if (prescaler_wait_active()) {
            in_waiting_cycles_ = true;
            if (mode_ == 2) {
                // Burst: release the bus.
                phase_        = Phase::WAITING_CYCLES;
                cpu_busreq_n_ = true;
                cpu_bao_n_    = cpu_bai_n_;
            }
            break;
        }

        // Burst mode: one byte per enable even without prescaler.
        // VHDL loops back through START_DMA each byte, but with no prescaler
        // the bus is never released, so we keep phase_ = TRANSFER.
        if (mode_ == 2) {
            break;
        }
    }

    return transferred;
}

void Dma::tick_burst_wait(uint64_t master_cycles) {
    // VHDL dma.vhd:249-255 — DMA_timer_s increments per clock by a value
    // that depends on turbo_i (higher turbo = smaller increment, so
    // prescaler waits take more real clocks):
    //   "00" 3.5MHz → +8, "01" 7MHz → +4, "10" 14MHz → +2, "11" 28MHz → +1
    uint16_t inc_per_clock;
    switch (turbo_ & 0x03) {
        case 0x00: inc_per_clock = 8; break;
        case 0x01: inc_per_clock = 4; break;
        case 0x02: inc_per_clock = 2; break;
        default:   inc_per_clock = 1; break;
    }
    // Accumulate in 32-bit, then mask to 14 bits to match the VHDL register
    // width (dma.vhd:129).  The maximum in-wait growth is prescaler*32 =
    // 255*32 = 8160 < 16384, so no in-wait wrap can confuse the gate.
    uint32_t next = static_cast<uint32_t>(dma_timer_s_) +
                    static_cast<uint32_t>(inc_per_clock) *
                    static_cast<uint32_t>(master_cycles);
    dma_timer_s_ = static_cast<uint16_t>(next & 0x3FFF);

    // When the burst-mode prescaler wait expires, the DMA must re-arbitrate
    // (VHDL dma.vhd:451-460 returns through START_DMA).  Test 12.5 observes
    // cpu_busreq_n() going back to false after the wait, so drive the
    // arbitration FSM with the current inputs as soon as the gate opens.
    if (phase_ == Phase::WAITING_CYCLES && !prescaler_wait_active()) {
        phase_ = Phase::START_DMA;
        tick_arbitration();
    }
}

// NOTE: snapshot format changed with the cycle-accurate prescaler timer.
// The old `burst_wait_` u16 field has been removed; `turbo_` (u8),
// `dma_timer_s_` (u16), and `in_waiting_cycles_` (bool) are now appended
// at the end.  Old snapshots are not forward-compatible — this is an
// unreleased dev build and there is no schema version in StateReader/
// Writer, so the break is clean.

// GH #27 S5 — the ONE field list (design §9.2). Declaration order IS the
// binary stream order, so it must not be disturbed: the byte-identity gate
// (§17.1) pins these 43 bytes as block 13 of the 2 292 965-byte stream.
//
// The four enums are marshalled through a local `uint8_t`. These four ARE
// `: uint8_t`-backed, so a `reinterpret_cast<uint8_t&>` would work — it is
// not used, because the same idiom then reads identically where an enum has
// no fixed underlying type and is `int`-wide, where it would NOT work
// (`CtcChannel::State`). One idiom that is always right beats two that differ
// by a property of the enum a reader has to go and check.
void Dma::describe_state(jnext::save::StateDesc& d)
{
    d.boolean("dir_a_to_b", dir_a_to_b_);
    d.u16("port_a_addr", port_a_addr_);
    d.u16("block_len", block_len_);
    d.boolean("port_a_is_io", port_a_is_io_);
    d.u8("port_a_addr_mode", port_a_addr_mode_);
    d.u8("port_a_timing", port_a_timing_);
    d.boolean("port_b_is_io", port_b_is_io_);
    d.u8("port_b_addr_mode", port_b_addr_mode_);
    d.u8("port_b_timing", port_b_timing_);
    d.u8("port_b_prescaler", port_b_prescaler_);
    d.boolean("dma_en", dma_en_);
    d.u8("mode", mode_);
    d.u16("port_b_addr", port_b_addr_);
    d.boolean("ce_wait", ce_wait_);
    d.boolean("auto_restart", auto_restart_);
    d.u8("read_mask", read_mask_);
    {
        uint8_t state = static_cast<uint8_t>(state_);
        d.enum8("state", state, kStateNames);
        state_ = static_cast<State>(state);
    }
    d.u16("src", src_);
    d.u16("dst", dst_);
    d.u16("counter", counter_);
    d.boolean("status_at_least_one", status_at_least_one_);
    d.boolean("status_end_of_block", status_end_of_block_);
    {
        uint8_t wr = static_cast<uint8_t>(wr_seq_);
        d.enum8("wr_seq", wr, kWrSeqNames);
        wr_seq_ = static_cast<WrSeq>(wr);
    }
    {
        uint8_t rd = static_cast<uint8_t>(rd_seq_);
        d.enum8("rd_seq", rd, kRdSeqNames);
        rd_seq_ = static_cast<RdSeq>(rd);
    }
    d.u8("reg_temp", reg_temp_);
    d.boolean("z80_compat", z80_compat_);
    d.u8("turbo", turbo_);
    d.u16("dma_timer_s", dma_timer_s_);
    d.boolean("in_waiting_cycles", in_waiting_cycles_);

    // Bus arbitration (appended at the end).
    {
        uint8_t phase = static_cast<uint8_t>(phase_);
        d.enum8("phase", phase, kPhaseNames);
        phase_ = static_cast<Phase>(phase);
    }
    d.boolean("cpu_busreq_n", cpu_busreq_n_);
    d.boolean("cpu_bao_n", cpu_bao_n_);
    d.boolean("cpu_bai_n", cpu_bai_n_);
    d.boolean("bus_busreq_n", bus_busreq_n_);
    d.boolean("dma_delay", dma_delay_);
    d.boolean("daisy_busy", daisy_busy_);
}

void Dma::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Dma::load_state(StateReader& r)
{
    jnext::save::BinReadDesc d(r);
    describe_state(d);
    if (d.failed()) {
        // The only way this fires is an `enum8` ordinal the declaration does
        // not name — a stream and a build that disagree about one of the four
        // FSMs. The field keeps its pre-load value rather than taking a wrong
        // FSM state (§16.1: "a wrong FSM state is not a safe default"), the
        // stream stays in sync (the byte was consumed either way), and the
        // fault is named.
        dma_log()->error("Dma::load_state: the stream does not match this "
                         "build's declaration at '{}'",
                         d.failure() ? d.failure() : "?");
    }
    // The two RESTORE MASKS. They were part of the read expressions
    // (`turbo_ = r.read_u8() & 0x03`, `dma_timer_s_ = r.read_u16() & 0x3FFF`)
    // and a declaration has no room for them, so they move here — applied
    // after the walk, to the same two fields, with the same widths. `turbo_`
    // is NR 0x06 bits 1:0 (zxnext.vhd:4966) and `dma_timer_s_` is the 14-bit
    // burst-mode prescaler timer of device/dma.vhd, so a wider value in the
    // stream is not a state the hardware can be in.
    turbo_       = static_cast<uint8_t>(turbo_ & 0x03);
    dma_timer_s_ = static_cast<uint16_t>(dma_timer_s_ & 0x3FFF);
}
