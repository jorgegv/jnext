#include "peripheral/copper.h"
#include "port/nextreg.h"
#include "core/log.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

#include <cstring>

namespace {

// GH #27 S4 — the enum name table for NR 0x62 bits 7:6, the Copper control
// mode. Names from `cores/zxnext/nextreg.txt:634-638`:
//
//   00 = Copper fully stopped
//   01 = Copper start, execute the list from index 0, and loop to the start
//   10 = Copper start, execute the list from last point, and loop to the start
//   11 = Copper start, execute the list from index 0, and restart the list
//        (at each frame)
//
// The field is two bits and `write_reg_0x62` masks every write to them
// (`copper.cpp` — `mode_ = (val >> 6) & 0x03`), and `last_mode_` is only ever
// assigned from `mode_`, so all four ordinals are reachable and there is no
// hole. Design §6.2's worked `state/copper.json` example encodes `mode` and
// `last_mode` as strings for exactly this reason: renumbering the control
// field becomes a visible name change rather than a silent re-interpretation.
const char* const kCopperModeNameArr[] = {
    "stopped",               // 00
    "run_loop_from_0",       // 01
    "run_loop_from_last",    // 10
    "run_restart_per_frame", // 11
};
const jnext::save::EnumNames kCopperModeNames{
    kCopperModeNameArr, sizeof(kCopperModeNameArr) / sizeof(kCopperModeNameArr[0])};

}  // namespace

// ─── Instruction decoding helpers ──────────────────────────────────

static inline bool is_wait(uint16_t instr) {
    return (instr & 0x8000) != 0;
}

static inline bool is_move(uint16_t instr) {
    return (instr & 0x8000) == 0;
}

/// Width of the VHDL `hcount_i` port, and therefore of the WAIT threshold
/// arithmetic below (copper.vhd:35 — `hcount_i : in unsigned(8 downto 0)`).
static constexpr int kHcountBits = 9;

/// WAIT instruction: hpos is bits [14:9] (6 bits).
///
/// VHDL copper.vhd:94:
///     hcount_i >= unsigned(copper_list_data_i(14 downto 9)&"000") + 12
///
/// The left operand of that add is `6 bits & "000"` = a **9-bit** UNSIGNED,
/// and `numeric_std`'s `"+"(UNSIGNED, NATURAL) return UNSIGNED` yields a
/// result of the LEFT operand's width — so the add is evaluated modulo 2^9,
/// in the same 9-bit domain as the `hcount_i` it is compared against.
///
/// That truncation is observable at the top of the hpos range: hpos=63 gives
/// 504 + 12 = 516, which WRAPS to 4, so the WAIT fires almost immediately
/// rather than never (GH #182).
static inline int wait_hpos_threshold(uint16_t instr) {
    int hpos_6bit = (instr >> 9) & 0x3F;
    // (hpos & "000") already occupies the full 9 bits; the +12 wraps in it.
    return ((hpos_6bit << 3) + 12) & ((1 << kHcountBits) - 1);
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
    // VHDL zxnext.vhd:3959-3996 — copper_inst_msb_ram / copper_inst_lsb_ram
    // are dpram2 entities with NO reset port. Their contents persist
    // across soft resets; only nr_copper_addr and nr_62_copper_mode are
    // cleared. Soft-reset menus that re-run a previously-uploaded Copper
    // program rely on this. (G118 closure.)
    //   instructions_.fill(0);   // <-- removed: dpram2 has no reset
    pc_ = 0;
    mode_ = 0;
    last_mode_ = 0;
    move_pending_ = false;
    write_addr_ = 0;
    write_data_stored_ = 0;
    // VHDL zxnext.vhd:5024 — nr_64_copper_offset resets to 0.
    offset_ = 0;
    // NOTE: c_max_vc_ is timing-mode configuration (not hardware state).
    // It is NOT cleared on reset — the Emulator sets it once at init-time
    // via set_c_max_vc() and it persists across soft resets.
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

    // Mode 11: reset PC when cvc==0 and hc==0.
    //
    // VHDL zxnext.vhd:3950 wires the Copper's vcount_i to cvc (the
    // offset-adjusted counter from zxula_timing.vhd:455-472), and
    // copper.vhd:80 matches vcount_i==0 & hcount_i==0 as the mode-11
    // restart condition. So with offset != 0 the restart point shifts
    // along with cvc — matching the VHDL behaviour, not raw vc.
    int cvc_restart = (vc + static_cast<int>(offset_)) % (c_max_vc_ + 1);
    if (mode_ == 3 && cvc_restart == 0 && hc == 0) {
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
        // WAIT: compare cvc_effective == vpos AND hc >= (hpos << 3) + 12.
        //
        // VHDL (zxula_timing.vhd:455-472): cvc reloads to the vertical
        // offset on the first active display line, then increments per
        // line and wraps to 0 when it reaches c_max_vc. The emulator
        // passes a vc that is already 0 at the first active line, so we
        // model the VHDL cvc as (vc + offset_) mod (c_max_vc_ + 1).
        int vpos = wait_vpos(instr);
        int hthresh = wait_hpos_threshold(instr);
        int cvc_effective = (vc + static_cast<int>(offset_)) % (c_max_vc_ + 1);

        if (cvc_effective == vpos && hc >= hthresh) {
            // Condition met — advance past this WAIT
            pc_ = (pc_ + 1) & 0x3FF;
            // Guarded, like every per-instruction and per-upload-word trace in
            // this file: see the should_log() rationale in PortDispatch::read
            // (src/port/port_dispatch.cpp) — unguarded, each is an
            // out-of-line call with tracing off (GH #244).
            if (Log::copper()->should_log(spdlog::level::trace))
                Log::copper()->trace("WAIT satisfied at cvc={} (vc={} off={}) hc={}, PC now {}",
                                     cvc_effective, vc, offset_, hc, pc_);
        }
        // Otherwise stall (do nothing, stay at this instruction)

    } else {
        // MOVE: write nextreg value
        uint8_t reg = move_nextreg(instr);
        uint8_t val = move_value(instr);

        if (reg != 0) {
            // NOP check: reg==0 means no write pulse (VHDL: copper_list_data_i(14 downto 8) /= "0000000")
            // GH #270 — publish the raster column this MOVE is issued at so
            // NextREG handlers with a per-scanline change log can record
            // WHERE along the line the write landed, not just which line.
            // See Copper::active_move_hc().
            move_hc_ = hc;
            nextreg.write(reg, val);
            move_hc_ = -1;
            move_pending_ = true;
            if (Log::copper()->should_log(spdlog::level::trace))
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
        if (Log::copper()->should_log(spdlog::level::trace))
            Log::copper()->trace("reg 0x60: write MSB [{:#05x}] = {:#04x}", word_addr, val);
    } else {
        // Odd byte address: write LSB
        // VHDL: addr[0]=1 => copper_lsb_we=1, copper_lsb_dat=nr_wr_dat
        instructions_[word_addr] = (instructions_[word_addr] & 0xFF00) | val;
        if (Log::copper()->should_log(spdlog::level::trace))
            Log::copper()->trace("reg 0x60: write LSB [{:#05x}] = {:#04x}", word_addr, val);
    }

    write_addr_ = (write_addr_ + 1) & 0x7FF;
}

void Copper::write_reg_0x61(uint8_t val) {
    // Set write address low 8 bits
    write_addr_ = (write_addr_ & 0x700) | val;
    if (Log::copper()->should_log(spdlog::level::trace))
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
        if (Log::copper()->should_log(spdlog::level::trace))
            Log::copper()->trace("reg 0x63: stored data = {:#04x}", val);
    } else {
        // Odd byte address: write stored data as MSB + this byte as LSB
        // VHDL: write_8=0 AND addr[0]=1 => copper_msb_we=1, copper_msb_dat=nr_copper_data_stored
        // Also: copper_lsb_we=1, copper_lsb_dat=nr_wr_dat
        instructions_[word_addr] = (static_cast<uint16_t>(write_data_stored_) << 8) | val;
        if (Log::copper()->should_log(spdlog::level::trace))
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

// GH #27 S4 — the ONE field list (design §9.2). Block 11 of the byte-identity
// stream (§17.1), 2 057 bytes. Declaration order IS the stream order.
//
// The 1 024-entry instruction RAM is §9.4's loop collapse: one `for` becomes
// one `d.bytes` of 2 048. §6.1 puts it in JSON explicitly — "Copper::
// instructions_ 2 048 B (NR 0x60-0x63) | 3, < 8 KB | JSON" — and §6.2's
// worked `state/copper.json` shows exactly this field as a 4 096-character
// hex string, which is what `bytes` produces.
//
// COLLAPSING A `uint16_t` ARRAY INTO `bytes` IS BYTE-IDENTICAL ON EVERY HOST.
// `StateWriter::write_u16` is `write_bytes(&v, 2)` — a memcpy of the host
// representation (`saveable.h:36-38`) — and `std::array<uint16_t, 1024>` is
// contiguous with no padding, so 1 024 sequential `write_u16` calls and one
// `write_bytes` of 2 048 copy the same bytes in the same order whatever the
// endianness. The `static_assert` is what keeps that true.
//
// NOT DECLARED: `c_max_vc_`, which is timing-mode CONFIGURATION rather than
// hardware state — the Emulator re-sets it at init time from the machine
// timing model, so it is §9.5(7)'s derived class and has never been in the
// stream.
//
// No field carries a DECLARED DEFAULT: §12.2's gate for them is S6's.
void Copper::describe_state(jnext::save::StateDesc& d)
{
    static_assert(sizeof(instructions_) == 1024 * sizeof(uint16_t), "");

    d.bytes("instructions", reinterpret_cast<uint8_t*>(instructions_.data()),
            sizeof(instructions_));
    d.u16("pc", pc_);
    // Marshalled through a local `uint8_t` although `mode_` already IS one:
    // the `enum8` idiom then reads identically at every call site, including
    // the ones where the field is an `enum class` that is `int`-wide.
    {
        uint8_t mode = mode_;
        d.enum8("mode", mode, kCopperModeNames);
        mode_ = mode;
    }
    {
        uint8_t last_mode = last_mode_;
        d.enum8("last_mode", last_mode, kCopperModeNames);
        last_mode_ = last_mode;
    }
    d.boolean("move_pending", move_pending_);
    d.u16("write_addr", write_addr_);
    d.u8("write_data_stored", write_data_stored_);
    // NR 0x64 vertical offset (appended after the existing sequence).
    d.u8("offset", offset_);
}

void Copper::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Copper::load_state(StateReader& r)
{
    jnext::save::BinReadDesc d(r);
    describe_state(d);
    if (d.failed()) {
        // The only way this fires is an `enum8` ordinal the declaration does
        // not name — impossible for a two-bit field this build wrote, so it
        // means a stream and a build that disagree. The field keeps its
        // pre-load value rather than taking a wrong mode (§16.1: "a wrong FSM
        // state is not a safe default"), the stream stays in sync (the byte
        // was consumed either way), and the fault is NAMED.
        Log::copper()->error("Copper::load_state: the stream does not match "
                             "this build\'s declaration at \'{}\'",
                             d.failure() ? d.failure() : "?");
    }
}
