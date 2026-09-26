#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <zlib.h>

#include "core/emulator_config.h"   // MachineType, parse_machine_type

/// A single frame of RZX input recording.
struct RzxFrame {
    uint16_t instruction_count;
    std::vector<uint8_t> in_values;
};

/// Complete RZX recording: creator info, embedded snapshot, and frame data.
struct RzxRecording {
    std::string creator;
    uint16_t    creator_major = 0;
    uint16_t    creator_minor = 0;
    /// The creator block's custom data (the bytes after its 29-byte fixed
    /// part), which the format leaves to the creator. jnext keeps the machine
    /// a recording was made on there — see rzx::set_recorded_machine().
    std::vector<uint8_t> creator_data;
    std::vector<uint8_t> snapshot_data;
    std::string snapshot_ext;  // "sna", "szx", "z80"
    std::vector<RzxFrame> frames;
    uint32_t initial_tstates = 0;
    uint32_t flags = 0;
    /// Snapshot blocks after the first. The format lets a recording continue
    /// from a new snapshot (FUSE writes one for an inserted snapshot); jnext
    /// plays one snapshot and its input, so parse() stops at the second
    /// snapshot and counts it here instead of mixing the two sessions.
    uint32_t later_snapshots = 0;
};

// ---------------------------------------------------------------------------
// RZX file format constants
// ---------------------------------------------------------------------------

namespace rzx {

static constexpr uint8_t SIGNATURE[] = {'R', 'Z', 'X', '!'};
static constexpr uint8_t BLOCK_CREATOR    = 0x10;
static constexpr uint8_t BLOCK_SNAPSHOT   = 0x30;
static constexpr uint8_t BLOCK_INPUT_REC  = 0x80;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

inline void write_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

inline void write_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

/// Decompress a zlib-compressed buffer.  Returns empty vector on failure.
inline std::vector<uint8_t> zlib_decompress(const uint8_t* src, size_t src_len,
                                            size_t expected_len = 0) {
    // Initial output buffer size — use hint or 4x input.
    size_t out_size = expected_len > 0 ? expected_len : src_len * 4;
    std::vector<uint8_t> out(out_size);

    z_stream zs{};
    zs.next_in  = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(src_len);

    if (inflateInit(&zs) != Z_OK) return {};

    std::vector<uint8_t> result;
    int ret;
    do {
        zs.next_out  = out.data();
        zs.avail_out = static_cast<uInt>(out.size());
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return {};
        }
        size_t have = out.size() - zs.avail_out;
        result.insert(result.end(), out.data(), out.data() + have);
    } while (ret != Z_STREAM_END);

    inflateEnd(&zs);
    return result;
}

/// Compress a buffer with zlib.  Returns empty vector on failure.
inline std::vector<uint8_t> zlib_compress(const uint8_t* src, size_t src_len) {
    uLong bound = compressBound(static_cast<uLong>(src_len));
    std::vector<uint8_t> out(bound);
    uLong out_len = bound;
    if (compress2(out.data(), &out_len, src, static_cast<uLong>(src_len), Z_DEFAULT_COMPRESSION) != Z_OK)
        return {};
    out.resize(out_len);
    return out;
}

// ---------------------------------------------------------------------------
// Parse RZX file
// ---------------------------------------------------------------------------

inline bool parse(const std::string& path, RzxRecording& rec) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;

    auto file_size = static_cast<size_t>(f.tellg());
    if (file_size < 10) return false;  // header too small

    std::vector<uint8_t> data(file_size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(file_size));

    // Check signature "RZX!"
    if (std::memcmp(data.data(), SIGNATURE, 4) != 0) return false;

    // Header: signature(4) + version_major(1) + version_minor(1) + flags(4)
    // uint8_t ver_major = data[4];
    // uint8_t ver_minor = data[5];
    rec.flags = read_u32(data.data() + 6);

    size_t pos = 10;  // after header

    while (pos + 5 <= file_size) {
        uint8_t block_id = data[pos];
        uint32_t block_len = read_u32(data.data() + pos + 1);
        if (block_len < 5 || pos + block_len > file_size) break;

        // A second snapshot starts a new session: its machine state and the
        // input after it do not belong to the first. Taking its snapshot and
        // appending its frames to the first session's (what this loop used
        // to do) replays neither.
        if (block_id == BLOCK_SNAPSHOT && (!rec.snapshot_data.empty() || !rec.frames.empty())) {
            for (size_t p = pos; p + 5 <= file_size;) {
                const uint32_t len = read_u32(data.data() + p + 1);
                if (len < 5 || p + len > file_size) break;
                if (data[p] == BLOCK_SNAPSHOT) ++rec.later_snapshots;
                p += len;
            }
            break;
        }

        if (block_id == BLOCK_CREATOR) {
            // Creator block: id(1) + len(4) + creator_string(20) + major(2) + minor(2)
            if (block_len >= 29) {
                char creator[21] = {};
                std::memcpy(creator, data.data() + pos + 5, 20);
                rec.creator = creator;
                rec.creator_major = read_u16(data.data() + pos + 25);
                rec.creator_minor = read_u16(data.data() + pos + 27);
                rec.creator_data.assign(data.data() + pos + 29, data.data() + pos + block_len);
            }
        } else if (block_id == BLOCK_SNAPSHOT) {
            // Snapshot block: id(1) + len(4) + flags(4) + ext(4) + uncompressed_len(4)
            if (block_len >= 17) {
                uint32_t snap_flags = read_u32(data.data() + pos + 5);
                char ext[5] = {};
                std::memcpy(ext, data.data() + pos + 9, 4);
                rec.snapshot_ext = ext;
                // Lowercase the extension
                for (auto& c : rec.snapshot_ext)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                // Trim trailing spaces/nulls
                while (!rec.snapshot_ext.empty() &&
                       (rec.snapshot_ext.back() == ' ' || rec.snapshot_ext.back() == '\0'))
                    rec.snapshot_ext.pop_back();

                uint32_t uncompressed_len = read_u32(data.data() + pos + 13);
                size_t snap_data_offset = pos + 17;
                size_t snap_data_len = block_len - 17;

                if (snap_flags & 0x02) {
                    // Compressed
                    rec.snapshot_data = zlib_decompress(
                        data.data() + snap_data_offset, snap_data_len, uncompressed_len);
                } else {
                    rec.snapshot_data.assign(
                        data.data() + snap_data_offset,
                        data.data() + snap_data_offset + snap_data_len);
                }
            }
        } else if (block_id == BLOCK_INPUT_REC) {
            // Input recording block:
            // id(1) + len(4) + num_frames(4) + reserved(1) + initial_tstates(4) + flags(4)
            if (block_len >= 18) {
                uint32_t num_frames = read_u32(data.data() + pos + 5);
                // reserved byte at pos + 9
                rec.initial_tstates = read_u32(data.data() + pos + 10);
                uint32_t input_flags = read_u32(data.data() + pos + 14);

                size_t frame_data_offset = pos + 18;
                size_t frame_data_len = block_len - 18;

                const uint8_t* frame_ptr;
                std::vector<uint8_t> decompressed;
                size_t frame_total_len;

                if (input_flags & 0x02) {
                    // Compressed
                    decompressed = zlib_decompress(
                        data.data() + frame_data_offset, frame_data_len);
                    frame_ptr = decompressed.data();
                    frame_total_len = decompressed.size();
                } else {
                    frame_ptr = data.data() + frame_data_offset;
                    frame_total_len = frame_data_len;
                }

                // Parse individual frames
                size_t fp = 0;
                for (uint32_t i = 0; i < num_frames && fp + 4 <= frame_total_len; ++i) {
                    RzxFrame frame;
                    frame.instruction_count = read_u16(frame_ptr + fp);
                    uint16_t in_count = read_u16(frame_ptr + fp + 2);
                    fp += 4;

                    if (in_count == 0xFFFF) {
                        // Repeat frame: use previous frame's IN values
                        if (!rec.frames.empty()) {
                            frame.in_values = rec.frames.back().in_values;
                        }
                    } else {
                        if (fp + in_count > frame_total_len) break;
                        frame.in_values.assign(frame_ptr + fp, frame_ptr + fp + in_count);
                        fp += in_count;
                    }

                    rec.frames.push_back(std::move(frame));
                }
            }
        }

        pos += block_len;
    }

    return true;
}

/// The embedded-snapshot types jnext can load (Emulator::load_snapshot_from_memory).
inline bool snapshot_type_supported(const std::string& ext) {
    return ext == "sna" || ext == "szx" || ext == "z80";
}

/// Whether `path` is an RZX recording jnext can play: it parses, and its
/// embedded snapshot, if any, is of a supported type. `why` says what is
/// wrong otherwise. The GUI asks this BEFORE it cold-boots the machine to
/// play a file, so an unplayable one is refused with the machine untouched.
inline bool playable(const std::string& path, std::string& why) {
    RzxRecording rec;
    if (!parse(path, rec)) {
        why = "it is not an RZX recording, or it cannot be read";
        return false;
    }
    if (!rec.snapshot_data.empty() && !snapshot_type_supported(rec.snapshot_ext)) {
        why = "its embedded snapshot is of an unsupported type ('" + rec.snapshot_ext +
              "'; supported: sna, szx, z80)";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The machine a recording was made on
// ---------------------------------------------------------------------------

/// jnext's creator ID, and the key in its creator-block custom data that names
/// the machine a recording was made on ("machine=48k" — a --machine value).
static constexpr const char JNEXT_CREATOR[] = "JNEXT";
static constexpr const char MACHINE_KEY[]   = "machine=";

/// Name `t` as --machine spells it (parse_machine_type() reads it back).
inline const char* machine_marker_name(MachineType t) {
    switch (t) {
        case MachineType::ZX48K:      return "48k";
        case MachineType::ZX128K:     return "128k";
        case MachineType::ZX_PLUS3:   return "plus3";
        case MachineType::ZXN_ISSUE2: return "next";
    }
    return "next";
}

/// Record in `rec` that it is made on machine `t` (the creator's custom data).
inline void set_recorded_machine(RzxRecording& rec, MachineType t) {
    const std::string s = std::string(MACHINE_KEY) + machine_marker_name(t);
    rec.creator_data.assign(s.begin(), s.end());
}

/// The machine an embedded snapshot says it holds, from its own header: an SNA
/// by its size (48K, or 128K when longer), an SZX by its machine ID, a .z80 by
/// its version and hardware mode. False when it names none jnext emulates
/// (Pentagon, Timex...) or cannot be read.
inline bool snapshot_machine(const std::vector<uint8_t>& snap, const std::string& ext,
                             MachineType& out) {
    const size_t n = snap.size();
    if (ext == "sna") {
        if (n < 49179) return false;
        out = n > 49179 ? MachineType::ZX128K : MachineType::ZX48K;
        return true;
    }
    if (ext == "szx") {
        if (n < 8 || std::memcmp(snap.data(), "ZXST", 4) != 0) return false;
        switch (snap[6]) {   // ZXSTMID_*
            case 0: case 1: case 15: out = MachineType::ZX48K;    return true;  // 16K 48K NTSC48K
            case 2: case 3: case 16: out = MachineType::ZX128K;   return true;  // 128K +2 128Ke
            case 4: case 5: case 6:  out = MachineType::ZX_PLUS3; return true;  // +2A +3 +3e
            default:                 return false;
        }
    }
    if (ext == "z80") {
        if (n < 30) return false;
        if (read_u16(snap.data() + 6) != 0) { out = MachineType::ZX48K; return true; }  // v1
        if (n < 35) return false;
        const bool    v2 = read_u16(snap.data() + 30) == 23;
        const uint8_t hw = snap[34];
        if (hw <= 2 || (hw == 3 && !v2))           { out = MachineType::ZX48K;    return true; }
        if (hw == 3 || hw == 4 || (hw <= 6 && !v2) || hw == 12)
                                                   { out = MachineType::ZX128K;   return true; }
        if (hw == 7 || hw == 8 || hw == 13)        { out = MachineType::ZX_PLUS3; return true; }
        return false;
    }
    return false;
}

/// The machine `rec` was made on. A jnext recording names it in its creator
/// data (set_recorded_machine()); any other recording is judged by its
/// embedded snapshot (snapshot_machine()). A jnext recording from before the
/// marker existed (up to v1.0.0) with an SNA snapshot names NO machine: jnext
/// embedded that 48K SNA on the 48K and on the Next alike. False when no
/// machine can be told — the recording then plays on the configured machine.
inline bool recorded_machine(const RzxRecording& rec, MachineType& out) {
    if (rec.creator == JNEXT_CREATOR) {
        const std::string data(rec.creator_data.begin(), rec.creator_data.end());
        if (data.rfind(MACHINE_KEY, 0) == 0)
            return parse_machine_type(data.substr(sizeof(MACHINE_KEY) - 1), out);
        if (rec.snapshot_ext == "sna") return false;
    }
    return snapshot_machine(rec.snapshot_data, rec.snapshot_ext, out);
}

// ---------------------------------------------------------------------------
// Write RZX file
// ---------------------------------------------------------------------------

inline bool write(const std::string& path, const RzxRecording& rec) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    // --- File header (10 bytes) ---
    uint8_t header[10] = {};
    std::memcpy(header, SIGNATURE, 4);
    header[4] = 0;  // version major
    header[5] = 13; // version minor
    write_u32(header + 6, rec.flags);
    f.write(reinterpret_cast<const char*>(header), 10);

    // --- Creator block (29 bytes + the creator's custom data) ---
    {
        std::vector<uint8_t> blk(29 + rec.creator_data.size());
        blk[0] = BLOCK_CREATOR;
        write_u32(blk.data() + 1, static_cast<uint32_t>(blk.size()));
        // Creator string (20 bytes, padded with zeros)
        size_t len = std::min(rec.creator.size(), size_t(20));
        std::memcpy(blk.data() + 5, rec.creator.c_str(), len);
        write_u16(blk.data() + 25, rec.creator_major);
        write_u16(blk.data() + 27, rec.creator_minor);
        if (!rec.creator_data.empty())
            std::memcpy(blk.data() + 29, rec.creator_data.data(), rec.creator_data.size());
        f.write(reinterpret_cast<const char*>(blk.data()), static_cast<std::streamsize>(blk.size()));
    }

    // --- Snapshot block (if present) ---
    if (!rec.snapshot_data.empty()) {
        auto compressed = zlib_compress(rec.snapshot_data.data(), rec.snapshot_data.size());
        bool use_compressed = !compressed.empty() && compressed.size() < rec.snapshot_data.size();
        const auto& snap_bytes = use_compressed ? compressed : rec.snapshot_data;

        uint32_t block_size = 17 + static_cast<uint32_t>(snap_bytes.size());
        std::vector<uint8_t> blk(block_size);
        blk[0] = BLOCK_SNAPSHOT;
        write_u32(blk.data() + 1, block_size);
        // Flags: bit 1 = compressed
        uint32_t snap_flags = use_compressed ? 0x02 : 0x00;
        write_u32(blk.data() + 5, snap_flags);
        // Extension (4 bytes)
        std::string ext = rec.snapshot_ext;
        ext.resize(4, '\0');
        std::memcpy(blk.data() + 9, ext.c_str(), 4);
        // Uncompressed length
        write_u32(blk.data() + 13, static_cast<uint32_t>(rec.snapshot_data.size()));
        std::memcpy(blk.data() + 17, snap_bytes.data(), snap_bytes.size());
        f.write(reinterpret_cast<const char*>(blk.data()), static_cast<std::streamsize>(block_size));
    }

    // --- Input recording block ---
    if (!rec.frames.empty()) {
        // Build uncompressed frame data
        std::vector<uint8_t> frame_data;
        for (const auto& fr : rec.frames) {
            uint8_t hdr[4];
            write_u16(hdr, fr.instruction_count);
            write_u16(hdr + 2, static_cast<uint16_t>(fr.in_values.size()));
            frame_data.insert(frame_data.end(), hdr, hdr + 4);
            frame_data.insert(frame_data.end(), fr.in_values.begin(), fr.in_values.end());
        }

        auto compressed = zlib_compress(frame_data.data(), frame_data.size());
        bool use_compressed = !compressed.empty() && compressed.size() < frame_data.size();
        const auto& out_data = use_compressed ? compressed : frame_data;

        uint32_t block_size = 18 + static_cast<uint32_t>(out_data.size());
        std::vector<uint8_t> blk(block_size);
        blk[0] = BLOCK_INPUT_REC;
        write_u32(blk.data() + 1, block_size);
        write_u32(blk.data() + 5, static_cast<uint32_t>(rec.frames.size()));
        blk[9] = 0;  // reserved
        write_u32(blk.data() + 10, rec.initial_tstates);
        uint32_t input_flags = use_compressed ? 0x02 : 0x00;
        write_u32(blk.data() + 14, input_flags);
        std::memcpy(blk.data() + 18, out_data.data(), out_data.size());
        f.write(reinterpret_cast<const char*>(blk.data()), static_cast<std::streamsize>(block_size));
    }

    // Close before judging: the stream buffers, so for a small recording
    // every byte can still be in memory here and a full disk only shows up
    // when the buffer is flushed. good() before close() reported success
    // for a file that never reached the disk.
    f.close();
    return !f.fail();
}

}  // namespace rzx
