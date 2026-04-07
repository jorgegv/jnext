#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

/// Save the current emulator state as an SNA snapshot byte vector.
/// This is the reverse of SnaLoader::apply() — reads registers and RAM
/// from the emulator and produces the SNA file format.
///
/// Only 48K SNA format is supported (simpler, and sufficient for RZX embedding).
class SnaSaver {
public:
    /// Save the current emulator state as a 48K SNA byte vector.
    /// Returns an empty vector on failure.
    static std::vector<uint8_t> save(Emulator& emu);
};
