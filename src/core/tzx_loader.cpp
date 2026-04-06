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

bool TzxLoader::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("TZX: cannot open '{}'", path);
        return false;
    }

    auto file_size = f.tellg();
    if (file_size < 2) {
        Log::emulator()->error("TZX: file too small '{}'", path);
        return false;
    }

    f.seekg(0);
    file_data_.resize(static_cast<size_t>(file_size));
    f.read(reinterpret_cast<char*>(file_data_.data()), file_size);

    // Load into ZOT player (auto-detects TZX vs TAP).
    if (tzx_load(P(), file_data_.data(), static_cast<int>(file_data_.size())) != 0) {
        Log::emulator()->error("TZX: failed to parse '{}'", path);
        file_data_.clear();
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
