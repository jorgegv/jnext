#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

/// Parsed SNA file header (27 bytes).
struct SnaHeader {
    uint8_t  I;
    uint16_t HL2, DE2, BC2, AF2;   // alternate register set
    uint16_t HL, DE, BC;
    uint16_t IY, IX;
    uint8_t  IFF2;                  // bit 2: 0=DI, 1=EI
    uint8_t  R;
    uint16_t AF;
    uint16_t SP;
    uint8_t  IM;                    // 0, 1, or 2
    uint8_t  border;                // 0-7
};

/// Extended header for 128K SNA files (4 bytes after the 48K RAM dump).
struct SnaExtHeader {
    uint16_t PC;
    uint8_t  port_7ffd;             // 128K memory paging register
    uint8_t  trdos;                 // TR-DOS paged flag
};

/// SNA snapshot file loader for the ZX Spectrum emulator.
///
/// Supports both 48K (49179 bytes) and 128K (131103+ bytes) SNA files.
///
/// Usage:
///   SnaLoader loader;
///   if (loader.load("game.sna")) {
///       loader.apply(emulator);
///   }
class SnaLoader {
public:
    /// Parse and validate an SNA file, reading all data into memory.
    /// Returns true on success.
    bool load(const std::string& path);

    /// Access the parsed header (valid after successful load()).
    const SnaHeader& header() const { return header_; }

    /// Apply the loaded SNA data to the emulator: load RAM, set CPU registers,
    /// border colour, and memory paging.
    bool apply(Emulator& emu) const;

private:
    SnaHeader header_{};
    SnaExtHeader ext_header_{};
    std::vector<uint8_t> ram48_;          // 49152 bytes of main RAM
    std::vector<uint8_t> extra_banks_;    // remaining 128K banks (if present)
    bool loaded_ = false;
    bool is_128k_ = false;

    static constexpr size_t SNA_48K_SIZE  = 49179;   // 27 + 49152
    static constexpr size_t SNA_HEADER_SIZE = 27;
    static constexpr size_t SNA_RAM48_SIZE  = 49152;
    static constexpr size_t SNA_EXT_HEADER_SIZE = 4;
};
