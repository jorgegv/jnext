#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class Emulator;

/// TZX file loader for the ZX Spectrum emulator.
///
/// Wraps antirez's ZOT TZX player library (MIT license) and provides
/// both real-time EAR-bit playback and fast-load via ROM trap interception.
/// Also handles TAP files (auto-detected by ZOT).
///
/// The interface mirrors TapLoader so both can be used interchangeably
/// by the emulator.
class TzxLoader {
public:
    TzxLoader();
    ~TzxLoader();

    // Non-copyable (owns file data and player state).
    TzxLoader(const TzxLoader&) = delete;
    TzxLoader& operator=(const TzxLoader&) = delete;

    // Movable.
    TzxLoader(TzxLoader&& other) noexcept;
    TzxLoader& operator=(TzxLoader&& other) noexcept;

    /// Load a TZX file.  Returns true on success. A file that is not a
    /// well-formed TZX (see validate()) is refused with an error log line,
    /// and the loader is left exactly as it was.
    bool load(const std::string& path);

    /// True when `data` is a structurally sound TZX file with at least one
    /// block. Otherwise false, with the reason in `error`.
    ///
    /// Structure is checked as libspectrum 1.5.0 — the tape library FUSE
    /// loads tapes with — checks it (internal_tzx_read(), tzx_read.c): the
    /// 10-byte header and its "ZXTape!\x1A" signature, then every block
    /// walked with that library's length rules, so a block that runs past
    /// the end of the file, or a fragment too short for its own fields,
    /// refuses the whole tape. A header with no block after it is refused
    /// too (libspectrum reads it but finds no tape present, TapLoader's
    /// verdict on an empty .tap). A bad checksum inside a complete block is
    /// not a container error.
    ///
    /// Where jnext deliberately ACCEPTS MORE than libspectrum: the block IDs
    /// that library does not implement ("For now, don't handle anything
    /// else"). The TZX specification defines $16, $17, $18, $26, $27, $34 and
    /// $40 with a length formula each, and gives every later ID a length by
    /// its General Extension Rule; such a block is accepted when its length
    /// fields fit the file, and the tape player skips it. `unplayed`, when
    /// given, receives (block index, ID) of each block the player skips
    /// without playing its content.
    static bool validate(const std::vector<uint8_t>& data, std::string& error,
                         std::vector<std::pair<size_t, uint8_t>>* unplayed = nullptr);

    /// True if a file is loaded.
    bool is_loaded() const { return loaded_; }

    /// Eject the tape (free all data).
    void eject();

    /// Rewind to the beginning without ejecting.
    void rewind();

    /// Fast-load mode toggle.
    bool fast_load() const { return fast_load_; }
    void set_fast_load(bool enabled) { fast_load_ = enabled; }

    /// ROM trap address (same as TAP: LD-BYTES at 0x0556).
    static constexpr uint16_t LD_BYTES_ADDR = 0x0556;

    /// Handle LD-BYTES ROM trap for fast-load.
    /// Extracts the next standard-speed data block from the TZX,
    /// feeds it into memory, and returns true.
    /// Returns false if no suitable block is available.
    bool handle_ld_bytes_trap(Emulator& emu);

    // -----------------------------------------------------------------------
    // Real-time playback (EAR bit simulation via ZOT)
    // -----------------------------------------------------------------------

    /// Start playback from the beginning.
    void start_playback(uint64_t cpu_clocks);

    /// Stop playback.
    void stop_playback();

    /// True if the tape is currently playing.
    bool is_playing() const;

    /// Update EAR output. Call once per Z80 instruction.
    /// Returns the current EAR level (0 or 1).
    uint8_t update(uint64_t cpu_clocks);

    /// Get the tape filename (for UI display).
    const std::string& filename() const { return filename_; }

    /// True if all blocks have been consumed (for fast-load mode).
    bool at_end() const { return fast_load_offset_ >= static_cast<int>(file_data_.size()); }

private:
    /// Parse the next standard-speed data block for fast-load.
    /// Scans from fast_load_offset_, skipping non-data TZX blocks.
    /// Returns pointer to raw block data (flag + payload + checksum)
    /// and sets out_len to the total length.  Returns nullptr if none found.
    const uint8_t* next_data_block(int& out_len);

    std::vector<uint8_t> file_data_;    // owned copy of the file
    void* player_ = nullptr;            // ZOT TZXPlayer* (opaque, defined in tzx.h)
    bool loaded_ = false;
    bool fast_load_ = true;
    std::string filename_;

    // Fast-load scanning position (separate from ZOT's playback offset).
    int fast_load_offset_ = 0;
    bool is_tzx_ = false;  // true for TZX, false for TAP
};
