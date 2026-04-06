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

    /// Apply the loaded snapshot to the emulator: set registers, paging,
    /// border, and load RAM pages.
    bool apply(Emulator& emu) const;

private:
    // SZX file header
    uint8_t  major_version_ = 0;
    uint8_t  minor_version_ = 0;
    uint8_t  machine_id_    = 0;
    uint8_t  flags_         = 0;

    // Z80R — CPU registers
    struct {
        uint16_t AF, BC, DE, HL;
        uint16_t AF2, BC2, DE2, HL2;
        uint16_t IX, IY, SP, PC;
        uint8_t  I, R;
        uint8_t  IFF1, IFF2, IM;
        uint32_t tstates;
        bool     halted;
    } regs_{};
    bool have_z80r_ = false;

    // SPCR — Spectrum configuration
    struct {
        uint8_t border;
        uint8_t port_7ffd;
        uint8_t port_1ffd;
        uint8_t port_fe;
    } spcr_{};
    bool have_spcr_ = false;

    // RAMP — RAM pages (page number → 16384 bytes)
    std::map<uint8_t, std::vector<uint8_t>> ram_pages_;

    bool loaded_ = false;

    // Chunk handlers
    bool parse_z80r(const uint8_t* data, uint32_t size);
    bool parse_spcr(const uint8_t* data, uint32_t size);
    bool parse_ramp(const uint8_t* data, uint32_t size);
};
