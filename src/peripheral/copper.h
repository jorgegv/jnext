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

    // ── Read-back for NextREG read path ─────────────────────────────

    /// NextREG 0x61 read — returns write address low byte.
    uint8_t read_reg_0x61() const;

    /// NextREG 0x62 read — returns mode (bits 7:6) and addr high (bits 2:0).
    uint8_t read_reg_0x62() const;

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
};
