#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

/// Parsed NEX file header (V1.0 / V1.1 / V1.2).
struct NexHeader {
    char     magic[4];           // "Next"
    char     version[4];         // "V1.0", "V1.1", or "V1.2"
    uint8_t  ram_required;       // 0 = 768KB, 1 = 1792KB
    uint8_t  num_banks;          // number of 16KB banks in file
    uint8_t  screen_flags;       // bit flags for screen data
    uint8_t  border_colour;      // 0-7
    uint16_t sp;                 // stack pointer
    uint16_t pc;                 // program counter (0 = don't execute)
    uint16_t extra_files;        // unused
    uint8_t  banks[112];         // banks[i]==1 means bank i is present
    uint8_t  loading_bar;        // 0=OFF, 1=ON
    uint8_t  loading_bar_colour;
    uint8_t  loading_delay;
    uint8_t  start_delay;
    uint8_t  preserve_regs;      // 0=reset, 1=preserve
    uint8_t  core_version[3];    // major, minor, subminor
    uint8_t  hires_colour;       // HiRes colour or L2 palette offset
    uint8_t  entry_bank;         // 16K bank mapped to 0xC000 at entry
    uint16_t file_handle;        // 0=close, 1=keep open

    // Screen flag bit masks
    static constexpr uint8_t SCREEN_LAYER2    = 0x01;
    static constexpr uint8_t SCREEN_ULA       = 0x02;
    static constexpr uint8_t SCREEN_LORES     = 0x04;
    static constexpr uint8_t SCREEN_HIRES     = 0x08;
    static constexpr uint8_t SCREEN_HICOLOUR  = 0x10;
    static constexpr uint8_t SCREEN_NO_PAL    = 0x80;
};

/// NEX file loader for the ZX Spectrum Next emulator.
///
/// Usage:
///   NexLoader loader;
///   if (loader.load("game.nex")) {
///       loader.apply(emulator);
///   }
class NexLoader {
public:
    /// Parse and validate a NEX file, reading all data into memory.
    /// Returns true on success.
    bool load(const std::string& path);

    /// Access the parsed header (valid after successful load()).
    const NexHeader& header() const { return header_; }

    /// Apply the loaded NEX data to the emulator: load banks into RAM,
    /// set up screen data, configure CPU registers, border, and MMU.
    bool apply(Emulator& emu) const;

private:
    NexHeader header_{};
    std::vector<uint8_t> file_data_;  // all data after the 512-byte header
    bool loaded_ = false;

    // Bank loading order as specified by the NEX format
    static constexpr int kBankOrder[] = {
        5, 2, 0, 1, 3, 4, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23,
        24, 25, 26, 27, 28, 29, 30, 31,
        32, 33, 34, 35, 36, 37, 38, 39,
        40, 41, 42, 43, 44, 45, 46, 47,
        48, 49, 50, 51, 52, 53, 54, 55,
        56, 57, 58, 59, 60, 61, 62, 63,
        64, 65, 66, 67, 68, 69, 70, 71,
        72, 73, 74, 75, 76, 77, 78, 79,
        80, 81, 82, 83, 84, 85, 86, 87,
        88, 89, 90, 91, 92, 93, 94, 95,
        96, 97, 98, 99, 100, 101, 102, 103,
        104, 105, 106, 107, 108, 109, 110, 111
    };
};
