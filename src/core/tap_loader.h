#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

class Emulator;

/// A single block from a TAP file.
struct TapBlock {
    uint8_t flag;                    // 0x00 = header, 0xFF = data
    std::vector<uint8_t> data;       // payload (excluding flag and checksum)
    uint8_t checksum;                // XOR checksum from file

    /// Verify the XOR checksum (flag ^ data[0] ^ data[1] ^ ... == checksum).
    /// Inline (not in tap_loader.cpp) so suites that cannot link
    /// jnext_core — mmu_test, which round-trips TapSaver output through
    /// this loader (BOOT-TAPESAVE-03) — can call it. Same precedent as
    /// z80_loader.h.
    bool verify_checksum() const {
        uint8_t xor_val = flag;
        for (uint8_t b : data) xor_val ^= b;
        xor_val ^= checksum;
        return xor_val == 0;
    }
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

    /// Parse raw TAP file bytes into blocks — the exact loop load() runs
    /// on the file contents. Defined inline here (not in tap_loader.cpp)
    /// so suites that cannot link jnext_core (mmu_test — BOOT-TAPESAVE-03
    /// round-trips TapSaver output through the real loader parse path)
    /// can call it. Same precedent as z80_loader.h. Anomalies (zero-length
    /// block, truncated block, checksum mismatch) are reported through the
    /// optional `warn` callback — load() routes them to Log::emulator()
    /// (this header cannot include core/log.h: spdlog's `namespace fmt`
    /// collides with test-local fmt() helpers in TUs including emulator.h).
    static void parse_blocks(const std::vector<uint8_t>& file_data,
                             std::vector<TapBlock>& blocks,
                             const std::function<void(const std::string&)>& warn = {}) {
        char msg[128];
        // Parse TAP blocks: each block is [2-byte LE length][length bytes of data]
        // The data bytes are: [flag][payload...][checksum]
        size_t pos = 0;
        blocks.clear();

        while (pos + 2 <= file_data.size()) {
            uint16_t block_len = static_cast<uint16_t>(
                file_data[pos] | (static_cast<uint16_t>(file_data[pos + 1]) << 8));
            pos += 2;

            if (block_len == 0) {
                if (warn) {
                    snprintf(msg, sizeof(msg), "TAP: zero-length block at offset %zu", pos - 2);
                    warn(msg);
                }
                continue;
            }

            if (pos + block_len > file_data.size()) {
                if (warn) {
                    snprintf(msg, sizeof(msg),
                             "TAP: truncated block at offset %zu (need %u bytes, have %zu)",
                             pos, block_len, file_data.size() - pos);
                    warn(msg);
                }
                break;
            }

            TapBlock block;
            block.flag = file_data[pos];

            // Data is everything between flag and checksum
            if (block_len >= 2) {
                block.data.assign(&file_data[pos + 1], &file_data[pos + block_len - 1]);
                block.checksum = file_data[pos + block_len - 1];
            } else {
                // Block with only flag byte (unusual but valid — checksum is flag itself)
                block.checksum = block.flag;
            }

            if (!block.verify_checksum() && warn) {
                snprintf(msg, sizeof(msg), "TAP: checksum mismatch in block %zu (flag=0x%02x)",
                         blocks.size(), block.flag);
                warn(msg);
            }

            blocks.push_back(std::move(block));
            pos += block_len;
        }
    }

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

    /// Eject the tape (clear all blocks).
    void eject();

    /// Fast-load mode: intercept ROM LD-BYTES and feed data directly.
    /// When false, real-time loading is used (EAR bit simulation).
    bool fast_load() const { return fast_load_; }
    void set_fast_load(bool enabled) { fast_load_ = enabled; }

    /// ROM trap addresses (48K/128K ROM).
    static constexpr uint16_t LD_BYTES_ADDR = 0x0556;
    /// Handle a ROM trap at LD-BYTES.
    /// Reads CPU registers (A=flag, IX=dest, DE=length, carry=LOAD/VERIFY),
    /// feeds the next TAP block, and sets up the return state.
    /// Returns true if the trap was handled (block available), false if tape ended.
    bool handle_ld_bytes_trap(Emulator& emu);

    // -----------------------------------------------------------------------
    // Real-time tape playback (EAR bit simulation)
    // -----------------------------------------------------------------------

    /// Start real-time playback of the current block.
    void start_realtime_playback();

    /// Advance the real-time playback state machine by one T-state.
    /// Returns the current EAR bit value (0 or 1).
    uint8_t tick_realtime(uint64_t tstates);

    /// True if real-time playback is in progress.
    bool is_playing() const { return playing_; }

    /// Get the tape filename (for UI display).
    const std::string& filename() const { return filename_; }

private:
    std::vector<TapBlock> blocks_;
    size_t current_block_ = 0;
    bool loaded_ = false;
    bool fast_load_ = true;
    std::string filename_;

    // Real-time playback state
    bool     playing_ = false;
    uint64_t play_tstates_ = 0;       // T-states elapsed since playback started
    size_t   play_byte_idx_ = 0;      // current byte index in block data
    int      play_bit_idx_ = 0;       // current bit index (7..0) in current byte
    int      play_pulse_count_ = 0;   // pulse counter within current phase
    uint8_t  ear_bit_ = 0;            // current EAR output (0 or 1)

    // Playback phases
    enum class PlayPhase {
        LEADER,       // pilot tone (leader pulses)
        SYNC1,        // first sync pulse
        SYNC2,        // second sync pulse
        DATA,         // data bits
        PAUSE,        // inter-block pause
        DONE          // block complete
    };
    PlayPhase play_phase_ = PlayPhase::DONE;
    uint64_t  phase_tstates_ = 0;     // T-states remaining in current pulse
    int       leader_pulses_ = 0;     // leader pulses remaining

    // Spectrum tape timing constants (T-states at 3.5 MHz)
    static constexpr int PILOT_PULSE   = 2168;  // pilot tone half-period
    static constexpr int SYNC1_PULSE   = 667;   // first sync pulse
    static constexpr int SYNC2_PULSE   = 735;   // second sync pulse
    static constexpr int ZERO_PULSE    = 855;   // zero bit half-period
    static constexpr int ONE_PULSE     = 1710;  // one bit half-period
    static constexpr int HEADER_LEADER = 8063;  // pilot pulses for header block
    static constexpr int DATA_LEADER   = 3223;  // pilot pulses for data block
    static constexpr int PAUSE_TSTATES = 3500000; // 1 second pause between blocks
};
