#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

/// A single block from a TAP file.
struct TapBlock {
    uint8_t flag;                    // 0x00 = header, 0xFF = data
    std::vector<uint8_t> data;       // payload (excluding flag and checksum)
    uint8_t checksum;                // XOR checksum from file

    /// Verify the XOR checksum (flag ^ data[0] ^ data[1] ^ ... == checksum).
    bool verify_checksum() const;
};

/// TAP file loader for the ZX Spectrum emulator.
///
/// Parses a standard .TAP file into a sequence of blocks and provides
/// fast-load support via ROM trap interception at LD-BYTES (0x0556).
///
/// Usage:
///   TapLoader loader;
///   if (loader.load("game.tap")) {
///       // Attach to emulator — it will intercept ROM load calls
///       emulator.attach_tape(std::move(loader));
///   }
class TapLoader {
public:
    /// Parse a TAP file into blocks.  Returns true on success.
    bool load(const std::string& path);

    /// Number of blocks in the tape.
    size_t block_count() const { return blocks_.size(); }

    /// Current block index (0-based).
    size_t current_block() const { return current_block_; }

    /// True if all blocks have been consumed.
    bool at_end() const { return current_block_ >= blocks_.size(); }

    /// Get the next block and advance the position.
    /// Returns nullptr if at end of tape.
    const TapBlock* next_block();

    /// Peek at the current block without advancing.
    const TapBlock* peek_block() const;

    /// Rewind to the beginning of the tape.
    void rewind() { current_block_ = 0; }

    /// Check if a tape is loaded.
    bool is_loaded() const { return loaded_; }

    /// ROM trap address for LD-BYTES routine (48K/128K ROM).
    static constexpr uint16_t LD_BYTES_ADDR = 0x0556;

    /// Handle a ROM trap at LD-BYTES.
    /// Reads CPU registers (A=flag, IX=dest, DE=length, carry=LOAD/VERIFY),
    /// feeds the next TAP block, and sets up the return state.
    /// Returns true if the trap was handled (block available), false if tape ended.
    bool handle_ld_bytes_trap(Emulator& emu);

private:
    std::vector<TapBlock> blocks_;
    size_t current_block_ = 0;
    bool loaded_ = false;
};
