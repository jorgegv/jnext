#pragma once
#include <cstdint>
#include <array>

class NextReg;

/// Copper co-processor — display-synchronized instruction engine.
///
/// Executes WAIT and MOVE instructions from a 1K x 16-bit instruction RAM,
/// driven by the raster counters (hc, vc).  Writes to NextREG registers
/// bypass the CPU path, enabling palette changes and other effects at
/// precise raster positions.
///
/// VHDL reference: device/copper.vhd + zxnext.vhd (integration, write logic)
///
/// Instruction encoding (16 bits):
///   bit 15 = 1  =>  WAIT:  [15]=1, [14:9]=hpos(6 bits), [8:0]=vpos(9 bits)
///                   HALT is encoded as WAIT with vpos=511 (never matches)
///   bit 15 = 0  =>  MOVE:  [15]=0, [14:8]=nextreg(7 bits), [7:0]=value
///                   NOP is encoded as MOVE with nextreg=0 (no write pulse)
///
/// WAIT comparison (from VHDL):
///   triggered when  vc == vpos  AND  hc >= (hpos << 3) + 12
///
/// Modes (NextREG 0x62 bits 7:6):
///   00 = stop (copper disabled)
///   01 = start, reset PC to 0
///   10 = start from current PC (no reset)
///   11 = start, reset PC to 0 at each frame (vc=0, hc=0)
class Copper {
public:
    Copper();

    void reset();

    /// Execute copper instructions for the given raster position.
    /// Called once per 28 MHz tick (or batched per scanline).
    /// In the emulator's line-accurate model this is called once per
    /// scanline with the line's vc and a sweep of hc values.
    ///
    /// @param hc  horizontal counter (9-bit, 0-based, in 28 MHz domain)
    /// @param vc  vertical counter (9-bit, 0-based)
    /// @param nextreg  reference to NextReg for MOVE writes
    void execute(int hc, int vc, NextReg& nextreg);

    /// Called at frame start (vc=0, hc=0).
    /// In mode 11, resets PC to 0.
    void on_vsync();

    // ── NextREG write handlers ──────────────────────────────────────

    /// NextREG 0x60 — copper data (8-bit write, auto-increment addr).
    /// On even addr: stores MSB.  On odd addr: commits 16-bit word.
    void write_reg_0x60(uint8_t val);

    /// NextREG 0x61 — copper write address low 8 bits.
    void write_reg_0x61(uint8_t val);

    /// NextREG 0x62 — copper control: bits 7:6 = mode, bits 2:0 = addr[10:8].
    void write_reg_0x62(uint8_t val);

    /// NextREG 0x63 — copper data (8-bit write, auto-increment addr).
    /// Same as 0x60 but nr_copper_write_8 = 0 (LSB-first pairing).
    void write_reg_0x63(uint8_t val);

    /// NextREG 0x64 — Copper vertical line offset (8 bits).
    /// VHDL: zxnext.vhd:1197 (signal), :5024 (reset to 0), :5442 (write),
    ///       :6723 (wired as i_cu_offset into zxula_timing.vhd).
    /// On the first active display line (ula_min_vactive) cvc is reloaded
    /// to unsigned('0' & i_cu_offset) — see zxula_timing.vhd:455-468.
    void write_reg_0x64(uint8_t val) { offset_ = val; }

    // ── Read-back for NextREG read path ─────────────────────────────

    /// NextREG 0x61 read — returns write address low byte.
    uint8_t read_reg_0x61() const;

    /// NextREG 0x62 read — returns mode (bits 7:6) and addr high (bits 2:0).
    uint8_t read_reg_0x62() const;

    /// NextREG 0x64 read — returns the cached vertical offset byte.
    /// VHDL: zxnext.vhd:6090 — port_253b_dat <= nr_64_copper_offset.
    uint8_t read_reg_0x64() const { return offset_; }

    /// Public accessor for the vertical offset (used by emulator wiring
    /// and tests). Returns the last value written via NR 0x64.
    uint8_t offset() const { return offset_; }

    /// Configure the Copper-vertical-counter wrap value (c_max_vc).
    /// Per VHDL zxula_timing.vhd the value depends on the active timing
    /// mode: 263 (VGA 60 Hz), 310 (VGA 50 Hz), 311 (other 50 Hz slots),
    /// 319 (HDMI). The Emulator should call this once at init-time with
    /// (timing_.lines_per_frame - 1). Default on construction is 311
    /// (VGA-0 50 Hz) so stand-alone unit tests have a sane wrap without
    /// having to wire the full timing model.
    void set_c_max_vc(int v) { c_max_vc_ = v; }

    // ── Accessors for debug / testing ───────────────────────────────

    uint16_t pc() const { return pc_; }
    uint8_t  mode() const { return mode_; }
    bool     is_running() const { return mode_ != 0; }
    uint16_t instruction(uint16_t addr) const { return instructions_[addr & 0x3FF]; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    std::array<uint16_t, 1024> instructions_{};  // 1K instruction RAM
    uint16_t pc_ = 0;          // program counter (10-bit, 0-1023)
    uint8_t  mode_ = 0;        // 2-bit mode from NextREG 0x62
    uint8_t  last_mode_ = 0;   // previous mode (for edge detection)
    bool     move_pending_ = false;  // MOVE output needs one cycle to clear

    // Write address state machine
    uint16_t write_addr_ = 0;         // 11-bit byte address into instruction RAM
    uint8_t  write_data_stored_ = 0;  // stored MSB byte for 16-bit writes

    // ── Vertical timing (NR 0x64 + c_max_vc wrap) ───────────────────
    //
    // VHDL model (zxula_timing.vhd):
    //   cvc reloads to ('0' & i_cu_offset) on the first active display
    //   line; increments per line; wraps to 0 when cvc == c_max_vc.
    //   c_max_vc is 9-bit, timing-mode dependent:
    //     HDMI     = 319  (zxula_timing.vhd:168)
    //     VGA 50Hz = 310  (zxula_timing.vhd:204)
    //     VGA 60Hz = 263  (zxula_timing.vhd:238)
    //     (other 50 Hz slot values up to 311 — see the remaining case
    //      branches in zxula_timing.vhd).
    //
    // The emulator today passes a rebased vc already equal to 0 at the
    // first active display line, so we only need to model the offset
    // reload and the wrap. Copper::execute() computes
    //   cvc_effective = (vc + offset_) % (c_max_vc_ + 1)
    // and uses that for WAIT vpos compares.
    //
    // Defaults:
    //   offset_   = 0    — matches VHDL reset (zxnext.vhd:5024).
    //   c_max_vc_ = 311  — sane stand-alone default (VGA-0 50 Hz).
    //                      The Emulator overrides this at init-time.
    //   c_max_vc_ is configuration (timing mode), NOT hardware state,
    //   so reset() leaves it untouched.
    uint8_t offset_ = 0;
    int     c_max_vc_ = 311;
};
