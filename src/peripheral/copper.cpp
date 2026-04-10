#include "peripheral/copper.h"
#include "port/nextreg.h"
#include "core/log.h"
#include "core/saveable.h"

#include <cstring>

// ─── Instruction decoding helpers ──────────────────────────────────

static inline bool is_wait(uint16_t instr) {
    return (instr & 0x8000) != 0;
}

static inline bool is_move(uint16_t instr) {
    return (instr & 0x8000) == 0;
}

/// WAIT instruction: hpos is bits [14:9] (6 bits).
/// The VHDL compares hcount >= (hpos << 3) + 12.
static inline int wait_hpos_threshold(uint16_t instr) {
    int hpos_6bit = (instr >> 9) & 0x3F;
    return (hpos_6bit << 3) + 12;
}

/// WAIT instruction: vpos is bits [8:0] (9 bits).
static inline int wait_vpos(uint16_t instr) {
    return instr & 0x01FF;
}

/// MOVE instruction: nextreg is bits [14:8] (7 bits).
static inline uint8_t move_nextreg(uint16_t instr) {
    return static_cast<uint8_t>((instr >> 8) & 0x7F);
}

/// MOVE instruction: value is bits [7:0].
static inline uint8_t move_value(uint16_t instr) {
    return static_cast<uint8_t>(instr & 0xFF);
}

/// HALT is a WAIT with vpos = 511 (all 9 bits set) — effectively never matches.
static inline bool is_halt(uint16_t instr) {
    return is_wait(instr) && wait_vpos(instr) == 511;
}

// ─── Copper implementation ─────────────────────────────────────────

Copper::Copper() {
    reset();
}

void Copper::reset() {
    instructions_.fill(0);
    pc_ = 0;
    mode_ = 0;
    last_mode_ = 0;
    move_pending_ = false;
    write_addr_ = 0;
    write_data_stored_ = 0;
}

void Copper::on_vsync() {
    // Mode 11: reset PC at frame start (vc=0, hc=0)
    if (mode_ == 3) {
        pc_ = 0;
        move_pending_ = false;
        Log::copper()->trace("on_vsync: mode=11, PC reset to 0");
    }
}

void Copper::execute(int hc, int vc, NextReg& nextreg) {
    // Check for mode change (edge detection, matching VHDL last_state_s)
    if (last_mode_ != mode_) {
        last_mode_ = mode_;

        // Entering mode 01 or 11: reset PC to 0
        if (mode_ == 1 || mode_ == 3) {
            pc_ = 0;
            Log::copper()->debug("mode change to {:02b}, PC reset to 0", mode_);
        }

        move_pending_ = false;
        return;  // VHDL: no execution on the cycle where mode changes
    }

    // Mode 11: reset PC at frame start
    if (mode_ == 3 && vc == 0 && hc == 0) {
        pc_ = 0;
        move_pending_ = false;
        return;
    }

    // Mode 00: copper stopped
    if (mode_ == 0) {
        move_pending_ = false;
        return;
    }

    // ── Execute one copper cycle ────────────────────────────────

    if (move_pending_) {
        // After a MOVE, the VHDL clears copper_dout_s on the next cycle.
        // No new instruction is fetched this cycle.
        move_pending_ = false;
        return;
    }

    uint16_t instr = instructions_[pc_ & 0x3FF];

    if (is_wait(instr)) {
        // WAIT: compare vc == vpos AND hc >= (hpos << 3) + 12
        int vpos = wait_vpos(instr);
        int hthresh = wait_hpos_threshold(instr);

        if (vc == vpos && hc >= hthresh) {
            // Condition met — advance past this WAIT
            pc_ = (pc_ + 1) & 0x3FF;
            Log::copper()->trace("WAIT satisfied at vc={} hc={}, PC now {}", vc, hc, pc_);
        }
        // Otherwise stall (do nothing, stay at this instruction)

    } else {
        // MOVE: write nextreg value
        uint8_t reg = move_nextreg(instr);
        uint8_t val = move_value(instr);

        if (reg != 0) {
            // NOP check: reg==0 means no write pulse (VHDL: copper_list_data_i(14 downto 8) /= "0000000")
            nextreg.write(reg, val);
            move_pending_ = true;
            Log::copper()->trace("MOVE nextreg[{:#04x}] = {:#04x}, PC={}", reg, val, pc_);
        }

        pc_ = (pc_ + 1) & 0x3FF;
    }
}

// ─── NextREG write handlers ────────────────────────────────────────
//
// The instruction RAM is addressed by write_addr_ (11-bit byte address).
// write_addr_[0] selects MSB (even) or LSB (odd) of the 16-bit instruction
// at word address write_addr_[10:1].
//
// Register 0x60 writes with nr_copper_write_8 = 1:
//   - On even addr (bit 0 = 0): store byte as MSB, auto-increment
//   - On odd addr (bit 0 = 1): write stored MSB + this byte as LSB
//     Actually: MSB write triggers when write_8=1 and addr[0]=0,
//               LSB write triggers when addr[0]=1
//
// Register 0x63 writes with nr_copper_write_8 = 0:
//   - On even addr: store byte, auto-increment
//   - On odd addr: write stored byte + this byte
//     MSB triggers when write_8=0 and addr[0]=1, LSB when addr[0]=1
//
// From VHDL (zxnext.vhd lines 3977-3999):
//   copper_msb_we = nr_copper_we AND ((write_8=0 AND addr[0]=1) OR (write_8=1 AND addr[0]=0))
//   copper_msb_dat = nr_wr_dat when write_8=1 else nr_copper_data_stored
//   copper_lsb_we = nr_copper_we AND addr[0]=1
//   copper_lsb_dat = nr_wr_dat

void Copper::write_reg_0x60(uint8_t val) {
    // nr_copper_write_8 = 1
    uint16_t word_addr = (write_addr_ >> 1) & 0x3FF;
    bool addr_bit0 = (write_addr_ & 1) != 0;

    if (!addr_bit0) {
        // Even byte address: store as MSB data (written to MSB RAM)
        // VHDL: write_8=1 AND addr[0]=0 => copper_msb_we=1, copper_msb_dat=nr_wr_dat
        // Also: store the data (for 0x63 path)
        write_data_stored_ = val;
        // Write MSB immediately
        instructions_[word_addr] = (instructions_[word_addr] & 0x00FF) | (static_cast<uint16_t>(val) << 8);
        Log::copper()->trace("reg 0x60: write MSB [{:#05x}] = {:#04x}", word_addr, val);
    } else {
        // Odd byte address: write LSB
        // VHDL: addr[0]=1 => copper_lsb_we=1, copper_lsb_dat=nr_wr_dat
        instructions_[word_addr] = (instructions_[word_addr] & 0xFF00) | val;
        Log::copper()->trace("reg 0x60: write LSB [{:#05x}] = {:#04x}", word_addr, val);
    }

    write_addr_ = (write_addr_ + 1) & 0x7FF;
}

void Copper::write_reg_0x61(uint8_t val) {
    // Set write address low 8 bits
    write_addr_ = (write_addr_ & 0x700) | val;
    Log::copper()->trace("reg 0x61: write_addr low = {:#04x}, addr now {:#05x}", val, write_addr_);
}

void Copper::write_reg_0x62(uint8_t val) {
    // bits 7:6 = mode, bits 2:0 = addr[10:8]
    mode_ = (val >> 6) & 0x03;
    write_addr_ = (write_addr_ & 0x0FF) | (static_cast<uint16_t>(val & 0x07) << 8);
    Log::copper()->debug("reg 0x62: mode={:02b}, write_addr high={}, addr now {:#05x}",
                         mode_, val & 0x07, write_addr_);
}

void Copper::write_reg_0x63(uint8_t val) {
    // nr_copper_write_8 = 0
    uint16_t word_addr = (write_addr_ >> 1) & 0x3FF;
    bool addr_bit0 = (write_addr_ & 1) != 0;

    if (!addr_bit0) {
        // Even byte address: store data for later MSB write
        write_data_stored_ = val;
        Log::copper()->trace("reg 0x63: stored data = {:#04x}", val);
    } else {
        // Odd byte address: write stored data as MSB + this byte as LSB
        // VHDL: write_8=0 AND addr[0]=1 => copper_msb_we=1, copper_msb_dat=nr_copper_data_stored
        // Also: copper_lsb_we=1, copper_lsb_dat=nr_wr_dat
        instructions_[word_addr] = (static_cast<uint16_t>(write_data_stored_) << 8) | val;
        Log::copper()->trace("reg 0x63: write word [{:#05x}] = {:#06x}",
                             word_addr, instructions_[word_addr]);
    }

    write_addr_ = (write_addr_ + 1) & 0x7FF;
}

uint8_t Copper::read_reg_0x61() const {
    return static_cast<uint8_t>(write_addr_ & 0xFF);
}

uint8_t Copper::read_reg_0x62() const {
    return static_cast<uint8_t>((mode_ << 6) | ((write_addr_ >> 8) & 0x07));
}

void Copper::save_state(StateWriter& w) const
{
    // 1024 × uint16_t instruction RAM (2048 bytes)
    for (const auto& ins : instructions_) w.write_u16(ins);
    w.write_u16(pc_);
    w.write_u8(mode_);
    w.write_u8(last_mode_);
    w.write_bool(move_pending_);
    w.write_u16(write_addr_);
    w.write_u8(write_data_stored_);
}

void Copper::load_state(StateReader& r)
{
    for (auto& ins : instructions_) ins = r.read_u16();
    pc_                = r.read_u16();
    mode_              = r.read_u8();
    last_mode_         = r.read_u8();
    move_pending_      = r.read_bool();
    write_addr_        = r.read_u16();
    write_data_stored_ = r.read_u8();
}
