#pragma once

#include <cstdint>
#include <string>
#include <tuple>
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


    /// Handle the LD-BREAK (0x056B) ROM trap for fast-load (tape_trap.h).
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

    // -----------------------------------------------------------------------
    // Automatic play/stop (GH #395)
    // -----------------------------------------------------------------------

    /// The host's policy for the next update(): hold at the next gap boundary
    /// (a block's non-zero pause has elapsed); stop blocks hold instead of
    /// ending the tape (auto_play); a 0x2A block stops only on a 48K machine.
    void set_auto_policy(bool hold_at_gap, bool auto_play, bool is_48k);

    /// True when the deck is stopped at a gap boundary or a stop block and
    /// resume() continues it at the next block.
    bool is_held() const;

    /// Continue a held tape at the next block, from its first pulse.
    bool resume(uint64_t cpu_clocks);

    /// Update EAR output. Call once per Z80 instruction.
    /// Returns the current EAR level (0 or 1).
    uint8_t update(uint64_t cpu_clocks);

    /// Get the tape filename (for UI display).
    const std::string& filename() const { return filename_; }

    /// The absolute UTF-8 host path the tape was loaded from (GH #401);
    /// empty when nothing is loaded. filename() stays the display name.
    const std::string& path() const { return path_; }

    /// Start real-time playback at the fast-load cursor instead of the start of
    /// the tape: the blocks a trapped load already served are not played again
    /// (GH #402; an RZX recording switches a fast tape to real time "from
    /// where it is").
    /// With `held` the deck waits at that block instead (auto play resumes it
    /// when the program polls the tape).
    void start_playback_from_fast_position(uint64_t cpu_clocks, bool held = false);

    /// Convert a real-time deck to fast loading without cutting a block (GH
    /// #402): a deck at a block boundary (not playing: held, stopped, ended)
    /// turns fast at once and returns true, its fast cursor on the next block;
    /// a deck mid-block keeps playing and turns fast, stopped, at the next gap
    /// boundary. Not serialised: re-derived on restore.
    bool handover_to_fast_at_gap();
    bool handover_pending() const { return handover_pending_; }

    /// A deck stopped at a stop block (playing 0, held 0, tape not at its end)
    /// becomes held there, the state auto play would have left it in (GH #402).
    void hold_if_stopped();

    /// SHA-256 of the exact bytes load() read (GH #402); empty when nothing is
    /// loaded.
    const std::string& sha256() const { return sha256_; }

    /// The deck's position, for a `.jns` (GH #402): the fast-load cursor and
    /// the whole ZOT player. File pointers are byte offsets into the file (-1
    /// for none); `edge_in` is the next edge's distance from `now`, the
    /// monotonic T-state clock, so a restore onto a re-based clock lands on the
    /// same T-state. Host policy (hold_request, stop_holds, is_48k) is not
    /// part of it: the emulator sets it before every update.
    /// `phase`: idle pilot sync1 sync2 data pause tone pulses direct pause2.
    struct DeckState {
        int32_t     fast_offset = 0;
        int32_t     offset = 0;
        bool        playing = false;
        bool        held = false;
        uint32_t    level = 0;
        int64_t     edge_in = 0;
        std::string phase = "idle";
        uint32_t    pilot_pulse = 0, sync1 = 0, sync2 = 0, zero_pulse = 0, one_pulse = 0;
        int32_t     pilot_left = 0;
        int32_t     block_data = -1;
        int32_t     data_len = 0, data_pos = 0, bit_pos = 0, pulse_half = 0, used_bits_last = 0;
        uint32_t    pause_tstates = 0;
        uint32_t    tone_pulse = 0;
        int32_t     tone_left = 0;
        int32_t     pulse_lengths = -1;
        int32_t     pulse_count = 0, pulse_idx = 0;
        uint32_t    sample_tstates = 0;
        uint32_t    direct_level = 0;
        int32_t     loop_start = 0, loop_count = 0;
        uint32_t    gap_done = 0;
        bool operator==(const DeckState& o) const {
            return std::tie(fast_offset, offset, playing, held, level, edge_in, phase,
                            pilot_pulse, sync1, sync2, zero_pulse, one_pulse, pilot_left,
                            block_data, data_len, data_pos, bit_pos, pulse_half,
                            used_bits_last, pause_tstates, tone_pulse, tone_left,
                            pulse_lengths, pulse_count, pulse_idx, sample_tstates,
                            direct_level, loop_start, loop_count, gap_done) ==
                   std::tie(o.fast_offset, o.offset, o.playing, o.held, o.level, o.edge_in,
                            o.phase, o.pilot_pulse, o.sync1, o.sync2, o.zero_pulse,
                            o.one_pulse, o.pilot_left, o.block_data, o.data_len,
                            o.data_pos, o.bit_pos, o.pulse_half, o.used_bits_last,
                            o.pause_tstates, o.tone_pulse, o.tone_left, o.pulse_lengths,
                            o.pulse_count, o.pulse_idx, o.sample_tstates, o.direct_level,
                            o.loop_start, o.loop_count, o.gap_done);
        }
    };
    DeckState deck_state(uint64_t now) const;

    /// Put the deck back where `d` says. False, with the reason in `why` and
    /// the loader untouched, when `d` is not a position this file can be in
    /// (nothing loaded, an offset or pointer outside the file, an index past
    /// its block, an unknown phase...): every value the player reads is
    /// checked against the loaded bytes first.
    bool restore_deck(const DeckState& d, uint64_t now, std::string& why);

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
    std::string path_;
    std::string sha256_;

    // Fast-load scanning position (separate from ZOT's playback offset).
    int fast_load_offset_ = 0;
    bool handover_pending_ = false;
    bool is_tzx_ = false;  // true for TZX, false for TAP
};
