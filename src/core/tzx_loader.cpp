#include "core/tzx_loader.h"
#include "core/emulator.h"
#include "core/log.h"

extern "C" {
#include "tzx.h"
}

#include <cstring>
#include <fstream>

// Cast opaque player_ pointer to ZOT's TZXPlayer*.
#define P() (static_cast<TZXPlayer*>(player_))

// Helper: read little-endian uint16_t
static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Helper: read little-endian 24-bit value
static uint32_t read_u24(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16);
}

TzxLoader::TzxLoader() {
    player_ = new TZXPlayer;
    std::memset(P(), 0, sizeof(TZXPlayer));
}

TzxLoader::~TzxLoader() {
    delete P();
}

TzxLoader::TzxLoader(TzxLoader&& other) noexcept
    : file_data_(std::move(other.file_data_))
    , player_(other.player_)
    , loaded_(other.loaded_)
    , fast_load_(other.fast_load_)
    , filename_(std::move(other.filename_))
    , fast_load_offset_(other.fast_load_offset_)
    , is_tzx_(other.is_tzx_)
{
    other.player_ = new TZXPlayer;
    std::memset(static_cast<TZXPlayer*>(other.player_), 0, sizeof(TZXPlayer));
    other.loaded_ = false;

    // ZOT player holds a pointer into file_data_; update it.
    if (loaded_ && !file_data_.empty()) {
        P()->data = file_data_.data();
        P()->len = static_cast<int>(file_data_.size());
    }
}

TzxLoader& TzxLoader::operator=(TzxLoader&& other) noexcept {
    if (this != &other) {
        file_data_ = std::move(other.file_data_);
        std::swap(player_, other.player_);
        loaded_ = other.loaded_;
        fast_load_ = other.fast_load_;
        filename_ = std::move(other.filename_);
        fast_load_offset_ = other.fast_load_offset_;
        is_tzx_ = other.is_tzx_;

        other.loaded_ = false;

        // Update ZOT's data pointer to our owned buffer.
        if (loaded_ && !file_data_.empty()) {
            P()->data = file_data_.data();
            P()->len = static_cast<int>(file_data_.size());
        }
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Container validation — the oracle is libspectrum 1.5.0's internal_tzx_read()
// (tzx_read.c), the TZX reader FUSE loads tapes with. Each case below is that
// function's per-block reader reduced to its length rules; the comments name
// the reader. ZOT's own tzx_load() checks nothing: it took any file of two
// bytes or more, and one without the signature as TAP data, so a random or
// truncated .tzx "loaded".
// ---------------------------------------------------------------------------

bool TzxLoader::validate(const std::vector<uint8_t>& data, std::string& error)
{
    char msg[160];
    const size_t size = data.size();

    // internal_tzx_read(): the 8-byte signature plus the 2 version bytes.
    if (size < 10) {
        std::snprintf(msg, sizeof(msg),
                      "%zu bytes, too short for the 10-byte TZX header", size);
        error = msg;
        return false;
    }
    if (std::memcmp(data.data(), "ZXTape!\x1A", 8) != 0) {
        error = "no \"ZXTape!\" signature";
        return false;
    }

    size_t pos = 10;      // libspectrum skips the version bytes unread
    size_t blocks = 0;    // blocks libspectrum would append to the tape

    // Bytes left at `at` (0 once past the end).
    auto left = [&](size_t at) -> size_t { return at < size ? size - at : 0; };
    auto le = [&](size_t at, int n) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<uint32_t>(data[at + i]) << (8 * i);
        return v;
    };
    // tzx_read_string(): a 1-byte length (present — every caller checks it
    // first) and that many bytes.
    auto skip_string = [&](size_t& p) -> bool {
        const size_t n = data[p];
        if (left(p + 1) < n) return false;
        p += 1 + n;
        return true;
    };

    while (pos < size) {
        const size_t start = pos;
        const uint8_t id = data[pos++];
        bool ok = true;

        switch (id) {
        case 0x10:  // tzx_read_rom_block: pause(2) + 2-byte length + data
            ok = left(pos) >= 4 && left(pos + 4) >= le(pos + 2, 2);
            if (ok) pos += 4 + le(pos + 2, 2);
            break;
        case 0x11:  // tzx_read_turbo_block: 15 bytes of timing + 3-byte length
            ok = left(pos) >= 18 && left(pos + 18) >= le(pos + 15, 3);
            if (ok) pos += 18 + le(pos + 15, 3);
            break;
        case 0x12:  // tzx_read_pure_tone
            ok = left(pos) >= 4;
            pos += 4;
            break;
        case 0x13:  // tzx_read_pulses_block: count + 2 bytes per pulse
            ok = left(pos) >= 1 && left(pos + 1) >= 2u * data[pos];
            if (ok) pos += 1 + 2u * data[pos];
            break;
        case 0x14:  // tzx_read_pure_data: 7 bytes of timing + 3-byte length
            ok = left(pos) >= 10 && left(pos + 10) >= le(pos + 7, 3);
            if (ok) pos += 10 + le(pos + 7, 3);
            break;
        case 0x15:  // tzx_read_raw_data: 5 bytes + 3-byte length
            ok = left(pos) >= 8 && left(pos + 8) >= le(pos + 5, 3);
            if (ok) pos += 8 + le(pos + 5, 3);
            break;
        case 0x19: {  // tzx_read_generalised_data, check for check.
            // Its arithmetic is kept as written, 32-bit unsigned `length`
            // and all: `length -= ptr2 - *ptr` ADDS each symbol table's size
            // back rather than subtracting it, and the data table's size
            // error is ignored (the table is then simply not skipped). Both
            // only loosen the intermediate checks; the final one — the parts
            // must end exactly where the block's declared length ends —
            // decides. Anything that would read past the end of the file
            // (libspectrum does not guard every such read) is refused.
            if (left(pos) < 4) { ok = false; break; }
            uint32_t length = le(pos, 4);
            pos += 4;
            const size_t blockend = pos + length;
            if (length < 14 || left(pos) < length) { ok = false; break; }
            const uint32_t totp = le(pos + 2, 4);
            const uint32_t npp  = data[pos + 6];
            const uint32_t asp  = (data[pos + 7] == 0 && totp) ? 256u : data[pos + 7];
            const uint32_t totd = le(pos + 8, 4);
            const uint32_t npd  = data[pos + 12];
            const uint32_t asd  = (data[pos + 13] == 0 && totd) ? 256u : data[pos + 13];
            pos += 14;
            length -= 14;
            // Pilot symbol table (libspectrum_tape_block_read_symbol_table).
            if (totp) {
                const uint32_t table = (2 * npp + 1) * asp;
                if (length < table) { ok = false; break; }
                pos += table;
                length += table;                         // the quirk, as written
            }
            // Pilot stream: 3 bytes per symbol.
            if (length < 3u * totp) { ok = false; break; }
            pos += 3u * static_cast<size_t>(totp);
            length -= 3u * totp;
            if (pos > size) { ok = false; break; }
            // Data symbol table: size error ignored, table then not skipped.
            if (totd) {
                const uint32_t table = (2 * npd + 1) * asd;
                if (length >= table) {
                    pos += table;
                    length += table;
                }
            }
            if (pos > size) { ok = false; break; }
            // Data stream: ceil(log2(symbols in table)) bits per symbol.
            // (32-bit, like libspectrum's `( bits_per_symbol * symbol_count
            // + 7 ) / 8`; its floating-point ceil(log()) gives the exact
            // integer for every table size 1..256.)
            uint32_t bits = 0;
            while (totd && (1u << bits) < asd) ++bits;
            const uint32_t data_count = (bits * totd + 7u) / 8u;
            if (left(pos) < data_count) { ok = false; break; }
            pos += data_count;
            ok = pos == blockend;   // "sanity check failed"
            break;
        }
        case 0x20:  // tzx_read_pause
        case 0x23:  // tzx_read_jump
        case 0x24:  // tzx_read_loop_start
            ok = left(pos) >= 2;
            pos += 2;
            break;
        case 0x21:  // tzx_read_group_start: a string
        case 0x30:  // tzx_read_comment: a string
            ok = left(pos) >= 1 && skip_string(pos);
            break;
        case 0x22:  // group end: tzx_read_empty_block
        case 0x25:  // loop end: tzx_read_empty_block
            break;
        case 0x28: {  // tzx_read_select: 2-byte length (checked, then unused)
                      // + count + per entry an offset and a string
            ok = left(pos) >= 3 && left(pos + 2) >= le(pos, 2);
            if (!ok) break;
            const size_t count = data[pos + 2];
            pos += 3;
            for (size_t i = 0; ok && i < count; ++i) {
                ok = left(pos) >= 3;
                if (!ok) break;
                pos += 2;
                ok = skip_string(pos);
            }
            break;
        }
        case 0x2A:  // tzx_read_stop
            ok = left(pos) >= 4;
            pos += 4;
            break;
        case 0x2B:  // tzx_read_set_signal_level: 4-byte length (unused) + level
            ok = left(pos) >= 5;
            pos += 5;
            break;
        case 0x31:  // tzx_read_message: time + a string
            ok = left(pos) >= 2;
            if (!ok) break;
            pos += 1;
            ok = skip_string(pos);
            break;
        case 0x32: {  // tzx_read_archive_info: 2-byte length (unused) + count
                      // + per entry an ID and a string
            ok = left(pos) >= 3;
            if (!ok) break;
            const size_t count = data[pos + 2];
            pos += 3;
            for (size_t i = 0; ok && i < count; ++i) {
                ok = left(pos) >= 2;
                if (!ok) break;
                pos += 1;
                ok = skip_string(pos);
            }
            break;
        }
        case 0x33:  // tzx_read_hardware: count + 3 bytes per entry
            ok = left(pos) >= 1 && left(pos + 1) >= 3u * data[pos];
            if (ok) pos += 1 + 3u * data[pos];
            break;
        case 0x35:  // tzx_read_custom: 16-byte name + 4-byte length + data
            ok = left(pos) >= 20 && left(pos + 20) >= le(pos + 16, 4);
            if (ok) pos += 20 + static_cast<size_t>(le(pos + 16, 4));
            break;
        case 0x5A:  // tzx_read_concat: the rest of a second header; appends no block
            ok = left(pos) >= 9;
            pos += 9;
            break;
        default:
            // "For now, don't handle anything else" — libspectrum refuses
            // every other ID, including the TZX spec's own 0x16-0x18, 0x26,
            // 0x27, 0x34 and 0x40, and any ID a later spec version adds.
            std::snprintf(msg, sizeof(msg),
                          "block %zu at offset %zu has ID $%02X, which is not a "
                          "supported TZX block type", blocks, start, id);
            error = msg;
            return false;
        }

        if (!ok) {
            std::snprintf(msg, sizeof(msg),
                          "block %zu (ID $%02X) at offset %zu runs past the end of "
                          "the file or is inconsistent", blocks, id, start);
            error = msg;
            return false;
        }
        if (id != 0x5A) ++blocks;
    }

    // libspectrum reads a header-only file but finds no tape in it
    // (libspectrum_tape_present() == 0).
    if (blocks == 0) {
        error = "no blocks after the header";
        return false;
    }
    return true;
}

bool TzxLoader::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("TZX: cannot open '{}'", path);
        return false;
    }

    const auto file_size = f.tellg();
    std::vector<uint8_t> bytes(file_size > 0 ? static_cast<size_t>(file_size) : 0);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) {
        Log::emulator()->error("TZX: cannot read '{}'", path);
        return false;
    }

    // Refuse before touching any member, so a failed load leaves this loader
    // (and whatever tape it held) exactly as it was.
    std::string why;
    if (!validate(bytes, why)) {
        Log::emulator()->error("TZX: '{}' is not a valid TZX file: {}", path, why);
        return false;
    }
    file_data_ = std::move(bytes);

    // Load into ZOT player. validate() has seen the signature, so ZOT takes
    // the TZX branch; its TAP branch is unreachable from here.
    if (tzx_load(P(), file_data_.data(), static_cast<int>(file_data_.size())) != 0) {
        Log::emulator()->error("TZX: failed to parse '{}'", path);
        file_data_.clear();
        loaded_ = false;
        return false;
    }

    is_tzx_ = !P()->is_tap;
    loaded_ = true;
    fast_load_offset_ = is_tzx_ ? 10 : 0;  // skip TZX header

    // Extract filename for UI.
    auto slash = path.rfind('/');
    filename_ = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    Log::emulator()->info("TZX: loaded '{}' — {} format, {} bytes",
                           filename_, is_tzx_ ? "TZX" : "TAP",
                           file_data_.size());
    return true;
}

void TzxLoader::eject() {
    tzx_stop(P());
    file_data_.clear();
    loaded_ = false;
    fast_load_offset_ = 0;
    filename_.clear();
    Log::emulator()->info("TZX: tape ejected");
}

void TzxLoader::rewind() {
    fast_load_offset_ = is_tzx_ ? 10 : 0;
    // If playing, restart from beginning.
    if (is_playing()) {
        tzx_play(P(), P()->edge_clock);
    }
}

void TzxLoader::start_playback(uint64_t cpu_clocks) {
    if (!loaded_) return;
    tzx_play(P(), cpu_clocks);
    Log::emulator()->debug("TZX: playback started");
}

void TzxLoader::stop_playback() {
    tzx_stop(P());
}

bool TzxLoader::is_playing() const {
    return loaded_ && tzx_is_playing(static_cast<TZXPlayer*>(player_));
}

uint8_t TzxLoader::update(uint64_t cpu_clocks) {
    if (!loaded_) return 0;
    return tzx_update(P(), cpu_clocks);
}

// ---------------------------------------------------------------------------
// Fast-load: scan TZX/TAP for standard data blocks
// ---------------------------------------------------------------------------

const uint8_t* TzxLoader::next_data_block(int& out_len) {
    const uint8_t* data = file_data_.data();
    int len = static_cast<int>(file_data_.size());

    if (!is_tzx_) {
        // TAP format: [2-byte LE length][data bytes]
        if (fast_load_offset_ + 2 > len) return nullptr;
        uint16_t block_len = read_u16(data + fast_load_offset_);
        if (block_len == 0 || fast_load_offset_ + 2 + block_len > len)
            return nullptr;
        const uint8_t* result = data + fast_load_offset_ + 2;
        out_len = block_len;
        fast_load_offset_ += 2 + block_len;
        return result;
    }

    // TZX format: scan blocks, return data from 0x10 (standard speed) blocks.
    // Skip all non-data blocks.
    while (fast_load_offset_ < len) {
        uint8_t id = data[fast_load_offset_];
        const uint8_t* b = data + fast_load_offset_ + 1;
        int remaining = len - fast_load_offset_ - 1;

        switch (id) {
        case 0x10: {
            // Standard speed data block: [2-byte pause][2-byte len][data...]
            if (remaining < 4) return nullptr;
            uint16_t data_len = read_u16(b + 2);
            if (remaining < 4 + data_len) return nullptr;
            fast_load_offset_ += 1 + 4 + data_len;
            out_len = data_len;
            return b + 4;
        }
        case 0x11: {
            // Turbo speed data: 18-byte header + data.
            // We can fast-load these too — the data format is the same.
            if (remaining < 18) return nullptr;
            uint32_t data_len = read_u24(b + 15);
            if (remaining < static_cast<int>(18 + data_len)) return nullptr;
            fast_load_offset_ += 1 + 18 + static_cast<int>(data_len);
            out_len = static_cast<int>(data_len);
            return b + 18;
        }
        // Skip all non-data blocks (same logic as ZOT's parser).
        case 0x12: fast_load_offset_ += 1 + 4; break;
        case 0x13: {
            if (remaining < 1) return nullptr;
            fast_load_offset_ += 1 + 1 + b[0] * 2;
            break;
        }
        case 0x14: {
            if (remaining < 10) return nullptr;
            uint32_t dl = read_u24(b + 7);
            fast_load_offset_ += 1 + 10 + static_cast<int>(dl);
            break;
        }
        case 0x15: {
            if (remaining < 8) return nullptr;
            uint32_t dl = read_u24(b + 5);
            fast_load_offset_ += 1 + 8 + static_cast<int>(dl);
            break;
        }
        case 0x20: fast_load_offset_ += 1 + 2; break;
        case 0x21: {
            if (remaining < 1) return nullptr;
            fast_load_offset_ += 1 + 1 + b[0];
            break;
        }
        case 0x22: fast_load_offset_ += 1; break;
        case 0x24: fast_load_offset_ += 1 + 2; break;
        case 0x25: fast_load_offset_ += 1; break;
        case 0x2A: fast_load_offset_ += 1 + 4; break;
        case 0x2B: fast_load_offset_ += 1 + 5; break;
        case 0x30: {
            if (remaining < 1) return nullptr;
            fast_load_offset_ += 1 + 1 + b[0];
            break;
        }
        case 0x32: {
            if (remaining < 2) return nullptr;
            fast_load_offset_ += 1 + 2 + read_u16(b);
            break;
        }
        // Unknown blocks: try common size patterns.
        default: {
            int body_size = -1;
            switch (id) {
                case 0x23: body_size = 2; break;
                case 0x26: if (remaining >= 2) body_size = 2 + read_u16(b) * 2; break;
                case 0x27: body_size = 0; break;
                case 0x28: if (remaining >= 2) body_size = 2 + read_u16(b); break;
                case 0x31: if (remaining >= 2) body_size = 2 + b[1]; break;
                case 0x33: if (remaining >= 1) body_size = 1 + b[0] * 3; break;
                case 0x35: if (remaining >= 20) {
                    body_size = 20 + static_cast<int>(
                        static_cast<uint32_t>(b[16]) |
                        (static_cast<uint32_t>(b[17]) << 8) |
                        (static_cast<uint32_t>(b[18]) << 16) |
                        (static_cast<uint32_t>(b[19]) << 24));
                    break;
                }
                case 0x5A: body_size = 9; break;
                default: break;
            }
            if (body_size >= 0 && 1 + body_size <= len - fast_load_offset_) {
                fast_load_offset_ += 1 + body_size;
            } else {
                Log::emulator()->warn("TZX fast-load: unknown block 0x{:02X} at offset {}, stopping",
                                       id, fast_load_offset_);
                fast_load_offset_ = len;  // mark as exhausted
                return nullptr;
            }
            break;
        }
        }
    }
    return nullptr;
}

bool TzxLoader::handle_ld_bytes_trap(Emulator& emu) {
    auto regs = emu.cpu().get_registers();

    uint8_t expected_flag = static_cast<uint8_t>(regs.AF >> 8);
    bool is_load = (regs.AF & 0x01) != 0;
    uint16_t dest = regs.IX;
    uint16_t length = regs.DE;

    Log::emulator()->debug("TZX trap: flag={:#04x} {} IX={:#06x} DE={:#06x}",
                            expected_flag, is_load ? "LOAD" : "VERIFY", dest, length);

    // Get next data block.
    int block_len = 0;
    const uint8_t* block_data = next_data_block(block_len);
    if (!block_data || block_len < 1) {
        Log::emulator()->warn("TZX: no more data blocks on tape");
        regs.AF &= ~0x0001;  // clear carry (error)
        uint16_t ret_lo = emu.mmu().read(regs.SP);
        uint16_t ret_hi = emu.mmu().read(regs.SP + 1);
        regs.SP += 2;
        regs.PC = static_cast<uint16_t>(ret_lo | (ret_hi << 8));
        emu.cpu().set_registers(regs);
        return false;
    }

    // block_data[0] is the flag byte.
    uint8_t block_flag = block_data[0];
    if (block_flag != expected_flag) {
        Log::emulator()->debug("TZX: flag mismatch (expected {:#04x}, got {:#04x})",
                                expected_flag, block_flag);
        regs.AF &= ~0x0001;
        uint16_t ret_lo = emu.mmu().read(regs.SP);
        uint16_t ret_hi = emu.mmu().read(regs.SP + 1);
        regs.SP += 2;
        regs.PC = static_cast<uint16_t>(ret_lo | (ret_hi << 8));
        emu.cpu().set_registers(regs);
        return true;
    }

    if (is_load) {
        // Payload is block_data[1..block_len-2], checksum is block_data[block_len-1].
        int payload_len = block_len - 2;  // exclude flag and checksum
        if (payload_len < 0) payload_len = 0;
        size_t copy_len = std::min(static_cast<size_t>(length),
                                    static_cast<size_t>(payload_len));
        for (size_t i = 0; i < copy_len; ++i) {
            emu.mmu().write(static_cast<uint16_t>(dest + i), block_data[1 + i]);
        }
        Log::emulator()->debug("TZX: loaded {} bytes to {:#06x}", copy_len, dest);
        regs.IX = static_cast<uint16_t>(dest + copy_len);
        regs.DE = 0;
    } else {
        Log::emulator()->debug("TZX: verify mode — auto-passing");
        regs.IX = static_cast<uint16_t>(dest + length);
        regs.DE = 0;
    }

    regs.AF |= 0x0001;  // set carry (success)

    // Simulate RET: pop return address.
    uint16_t ret_lo = emu.mmu().read(regs.SP);
    uint16_t ret_hi = emu.mmu().read(regs.SP + 1);
    regs.SP += 2;
    regs.PC = static_cast<uint16_t>(ret_lo | (ret_hi << 8));

    emu.cpu().set_registers(regs);
    return true;
}
