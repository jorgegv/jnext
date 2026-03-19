#pragma once
#include <cstdint>
#include <string>
#include <array>

class Rom {
public:
    Rom();
    bool load(int slot, const std::string& path);  // slot 0-3, each 16K
    uint8_t read(uint32_t addr) const;
    uint8_t* page_ptr(uint16_t page);              // 8K page within ROM space
    const uint8_t* page_ptr(uint16_t page) const;
private:
    static constexpr size_t ROM_SIZE = 64 * 1024;  // 4 x 16K
    std::array<uint8_t, ROM_SIZE> data_;
    bool loaded_[4] = {};
};
