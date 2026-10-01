#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

/// Tape SAVE to TZX or WAV (GH #89; design in doc/design/TAPE-SAVE-PLAN.md).
///
/// Records the machine's tape-out signal as timed edges, plus the blocks the
/// SA-BYTES ROM trap hands over, as one time-ordered stream, and writes it as
/// TZX (decoded into blocks 0x10 / 0x11 / 0x13 / 0x20) or as WAV (the raw
/// signal, PCM 8-bit mono 44100 Hz).
///
/// Times are emulated master-clock cycles (28 MHz, Clock::get()), never wall
/// clock. TZX durations are 3.5 MHz T-states = master cycles / 8.
///
/// The encoders and the segment decoder are static so unit rows test them
/// byte-exact without an emulator.
class TapeRecorder {
public:
    enum class Format { None, Tzx, Wav };

    static constexpr uint64_t MASTER_HZ       = 28000000;
    static constexpr uint32_t MASTER_PER_T    = 8;        ///< 28 MHz / 3.5 MHz
    static constexpr uint32_t MASTER_PER_MS   = 28000;
    /// An interval longer than this ends a segment: no data pulse is this long,
    /// and it is the longest pulse block 0x13 can hold.
    static constexpr uint32_t SEGMENT_GAP_T   = 65535;
    static constexpr uint16_t MAX_PAUSE_MS    = 65535;    ///< TZX pause field maximum
    static constexpr uint64_t MAX_GAP_MASTER  = uint64_t(MAX_PAUSE_MS) * MASTER_PER_MS;
    /// The clock went backwards (rewind, reset, snapshot): counted as this gap.
    static constexpr uint64_t BACKWARDS_GAP_MASTER = MASTER_HZ;    // 1 s
    /// A segment this long is decoded and written without waiting for silence,
    /// so a saver that never pauses cannot grow memory without bound.
    static constexpr size_t   MAX_SEGMENT_EDGES = size_t(1) << 20;
    static constexpr uint32_t MIN_PILOT_PULSES  = 256;

    static constexpr uint32_t WAV_RATE = 44100;
    static constexpr uint8_t  WAV_LOW  = 0x40;
    static constexpr uint8_t  WAV_HIGH = 0xC0;
    static constexpr size_t   WAV_HEADER_SIZE = 44;

    /// ROM (and TZX block 0x10) timings, in T-states.
    static constexpr uint16_t STD_PILOT = 2168;
    static constexpr uint16_t STD_SYNC1 = 667;
    static constexpr uint16_t STD_SYNC2 = 735;
    static constexpr uint16_t STD_ZERO  = 855;
    static constexpr uint16_t STD_ONE   = 1710;
    static constexpr uint16_t STD_PILOT_HEADER = 8063;    ///< flag byte < 0x80
    static constexpr uint16_t STD_PILOT_DATA   = 3223;    ///< flag byte >= 0x80

    /// `.tzx` -> Tzx, `.wav` -> Wav (case-insensitive); anything else -> None
    /// (the TAP saver's business).
    static Format format_for_path(const std::string& path);

    // ── Encoders (TZX 1.20) ──────────────────────────────────────────────
    struct TurboParams {
        uint16_t pilot = 0, sync1 = 0, sync2 = 0, zero = 0, one = 0;
        uint16_t pilot_count = 0;
        uint8_t  used_bits = 8;
    };
    static std::vector<uint8_t> tzx_header();
    static std::vector<uint8_t> tzx_block_10(uint16_t pause_ms, const std::vector<uint8_t>& data);
    static std::vector<uint8_t> tzx_block_11(const TurboParams& p, uint16_t pause_ms,
                                             const std::vector<uint8_t>& data);
    static std::vector<uint8_t> tzx_block_13(const std::vector<uint16_t>& pulses);
    static std::vector<uint8_t> tzx_block_20(uint16_t pause_ms);
    /// Canonical 44-byte header: PCM, 1 channel, WAV_RATE, 8 bits.
    static std::vector<uint8_t> wav_header(uint32_t data_bytes);

    /// The pulse train (T-states) of a standard ROM block: pilot, two sync
    /// pulses, two pulses per bit MSB first. `data` is flag + payload + checksum.
    static std::vector<uint16_t> standard_pulses(const std::vector<uint8_t>& data);

    /// Decode one segment. `edges` are master-cycle times, ascending, gaps no
    /// longer than SEGMENT_GAP_T; `next_event` is the time of whatever follows
    /// the segment (it sets the last block's pause). Returns the TZX bytes.
    /// `tail_pause_at` (if given) receives the offset, in those bytes, of the
    /// pause WORD that depends on `next_event` (it is measured from the last
    /// edge), and `tail_is_rest` whether it is a 0x20 written as "the gap less
    /// the 1 ms closing pulse" rather than a 0x10/0x11 pause.
    static std::vector<uint8_t> decode_segment(const std::vector<uint64_t>& edges,
                                               uint64_t next_event,
                                               size_t* tail_pause_at = nullptr,
                                               bool* tail_is_rest = nullptr);

    /// The pause WORD for a gap (master cycles): a block's (ms, capped), or a
    /// 0x20 after a closing 1 ms pulse (ms - 1, at least 1, capped).
    static uint16_t pause_ms(uint64_t gap, bool rest);

    // ── Recorder ─────────────────────────────────────────────────────────
    TapeRecorder() = default;
    ~TapeRecorder() = default;
    TapeRecorder(const TapeRecorder&) = delete;
    TapeRecorder& operator=(const TapeRecorder&) = delete;

    /// Arm on `path` (format from its extension). A missing or empty file
    /// starts a new tape; an existing one is appended to if it is a TZX, or a
    /// WAV with exactly the header this class writes. Returns false and stays
    /// inactive otherwise; `error` says why.
    bool open(const std::string& path, std::string& error);

    bool active() const { return format_ != Format::None; }
    Format format() const { return format_; }
    const std::string& path() const { return path_; }

    /// The tape-out level at master time `now`. The first call sets the
    /// baseline; each later change of level is an edge.
    void sample(bool level, uint64_t now);

    /// Take `level` as the current level without recording an edge.
    void set_level(bool level) { have_level_ = true; level_ = level; }

    /// A block the SA-BYTES trap took at `now` (flag + payload + checksum).
    bool rom_block(const std::vector<uint8_t>& data, uint64_t now);

    /// Once per frame while saving. A TZX segment silent for longer than
    /// SEGMENT_GAP_T is decoded and written now, its last pause provisional
    /// (patched in place when the next event comes); a WAV gets its RIFF/data
    /// sizes refreshed. Then the file is flushed, so a run that is killed
    /// leaves a valid file holding everything up to the last silence.
    void poll(uint64_t now);

    /// Write everything pending (its pause/hold measured up to `now`), patch
    /// the WAV header, and disarm. Safe when inactive.
    void close(uint64_t now);

    uint64_t edges_recorded() const { return edges_; }
    size_t   rom_blocks() const { return rom_blocks_; }

private:
    void edge(uint64_t now);
    /// Time since the last event, with the backwards and cap rules applied.
    uint64_t gap_since_last(uint64_t now) const;
    /// Decode and write the open segment, whose next event is at `next`;
    /// `final` = that time is real (else the tail pause stays patchable).
    void flush_tzx(uint64_t next, bool final);
    /// Settle the provisional pause WORD with the real next event at `now`.
    void patch_pending(uint64_t now);
    void write_bytes(const std::vector<uint8_t>& b);
    void wav_advance(uint64_t tape_pos);
    void wav_toggle();
    void wav_patch_header();

    Format        format_ = Format::None;
    std::string   path_;
    std::fstream  file_;
    bool          ok_ = true;

    bool          have_level_ = false;
    bool          level_ = false;           ///< last sampled tape-out level
    bool          have_event_ = false;
    uint64_t      last_event_ = 0;          ///< master time of the last edge / ROM block
    uint64_t      edges_ = 0;
    size_t        rom_blocks_ = 0;

    // TZX: the open segment, and the pause WORD already on disk whose value
    // waits for the next event (a trapped block's, or a segment's written by
    // poll()): its file offset, the time it is measured from, its kind.
    std::vector<uint64_t> seg_;
    bool                  have_pending_ = false;
    uint64_t              pending_at_ = 0;
    uint64_t              pending_from_ = 0;
    bool                  pending_rest_ = false;

    // WAV: tape position (master cycles) and the samples written so far.
    uint64_t      wav_pos_ = 0;
    uint64_t      wav_samples_ = 0;
    uint64_t      wav_patched_ = 0;         ///< data size the header says
    bool          wav_level_ = false;
};
