#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Emulator;

/// Parsed SZX (zx-state) snapshot file.
///
/// Usage:
///   SzxLoader loader;
///   if (loader.load("game.szx")) {
///       loader.apply(emulator);
///   }
class SzxLoader {
public:
    /// Parse and validate an SZX file, reading all chunk data into memory.
    /// Returns true on success.
    bool load(const std::string& path);

    /// Parse and validate SZX bytes already in memory (an RZX file's embedded
    /// snapshot). `name` identifies the data in log messages only.
    /// Returns true on success.
    bool load_from_buffer(const std::vector<uint8_t>& buf, const std::string& name);

    /// Apply the loaded snapshot to the emulator: set registers, paging,
    /// border, and load RAM pages.
    bool apply(Emulator& emu) const;

    // Read-only accessors — let Emulator-free test tiers (e.g. mmu_test,
    // which cannot link jnext_core / construct an Emulator) verify a
    // round trip through the real load() parser without going through
    // apply(). Added alongside SzxSaver (Task 13b) for BOOT-SNAPSAVE-02.
    struct Z80RState {
        uint16_t AF, BC, DE, HL;
        uint16_t AF2, BC2, DE2, HL2;
        uint16_t IX, IY, SP, PC;
        uint8_t  I, R;
        uint8_t  IFF1, IFF2, IM;
        uint32_t tstates;
        bool     halted;
    };
    struct SpcrState {
        uint8_t border;
        uint8_t port_7ffd;
        uint8_t port_1ffd;
        uint8_t port_fe;
    };
    /// SCLD chunk (Timex port 0xFF "DEC"), GH #323.
    bool have_scld() const { return have_scld_; }
    uint8_t scld_dec() const { return scld_dec_; }
    /// AY chunk (selected register + the 16 registers), GH #323.
    bool have_ay() const { return have_ay_; }
    bool loaded() const { return loaded_; }
    bool have_z80r() const { return have_z80r_; }
    bool have_spcr() const { return have_spcr_; }
    uint8_t machine_id() const { return machine_id_; }
    const Z80RState& z80r() const { return regs_; }
    const SpcrState& spcr() const { return spcr_; }
    const std::map<uint8_t, std::vector<uint8_t>>& ram_pages() const { return ram_pages_; }

private:
    // SZX file header
    uint8_t  major_version_ = 0;
    uint8_t  minor_version_ = 0;
    uint8_t  machine_id_    = 0;
    uint8_t  flags_         = 0;

    // Z80R — CPU registers
    Z80RState regs_{};
    bool have_z80r_ = false;

    // SPCR — Spectrum configuration
    SpcrState spcr_{};
    bool have_spcr_ = false;

    // SCLD — Timex port 0xFF (chF4 = HSR is ignored: the Next has no horizontal MMU)
    bool    have_scld_ = false;
    uint8_t scld_dec_  = 0;

    // AY — sound chip: selected register and registers 0-15
    bool    have_ay_      = false;
    uint8_t ay_selected_  = 0;
    uint8_t ay_regs_[16]  = {};

    // RAMP — RAM pages (page number → 16384 bytes)
    std::map<uint8_t, std::vector<uint8_t>> ram_pages_;

    bool loaded_ = false;

    /// The 128K-class machine IDs (128K, +2, +2A, +3, +3e, Pentagon 128, 128Ke):
    /// the ones with port 0x7FFD paging and an AY. Not 0/1/15 (16K, 48K, NTSC 48K).
    bool has_128k_paging() const { return (machine_id_ >= 2 && machine_id_ <= 7) || machine_id_ == 16; }

    // Chunk handlers
    bool parse_z80r(const uint8_t* data, uint32_t size);
    bool parse_spcr(const uint8_t* data, uint32_t size);
    bool parse_ramp(const uint8_t* data, uint32_t size);
    bool parse_scld(const uint8_t* data, uint32_t size);
    bool parse_ay(const uint8_t* data, uint32_t size);
};
