#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <array>

class Rom {
public:
    Rom();
    void reset() { alt_rom_config_ = 0; }
    bool load(int slot, const std::string& path);  // slot 0-3, each 16K

    /// Load 16 KB into ROM slot (0-3) directly from a byte buffer. Used
    /// by the Wave 0.3 SD-ROM path: extract_sd_rom() returns a vector of
    /// bytes which we slice into 16 KB banks (e.g. 32 KB 128.rom -> banks
    /// 0+1, 64 KB plus3.rom -> banks 0..3). `data`/`size` describe the
    /// 16 KB slice; pass `data + offset` from the caller. A short buffer
    /// (size < 16 KB) is treated as a load failure and returns false
    /// (mirrors the file-load short-read path in Rom::load).
    bool load_bytes(int slot, const uint8_t* data, size_t size);

    uint8_t read(uint32_t addr) const;
    uint8_t* page_ptr(uint16_t page);              // 8K page within ROM space
    const uint8_t* page_ptr(uint16_t page) const;

    /// Configure alternate ROM from NextREG 0x8C.
    /// bit 7 = enable alt rom, bit 6 = write-only overlay,
    /// bits 5:4 = lock ROM1/ROM0.
    void set_alt_rom_config(uint8_t val) { alt_rom_config_ = val; }
    uint8_t alt_rom_config() const { return alt_rom_config_; }
    bool alt_rom_enabled() const { return (alt_rom_config_ & 0x80) != 0; }

private:
    uint8_t alt_rom_config_ = 0;
    static constexpr size_t ROM_SIZE = 64 * 1024;  // 4 x 16K
    std::array<uint8_t, ROM_SIZE> data_;
    bool loaded_[4] = {};
};
