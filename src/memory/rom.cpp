#include "rom.h"
#include "core/log.h"
#include <fstream>
#include <cstring>

Rom::Rom() { data_.fill(0xFF); }

bool Rom::load(int slot, const std::string& path) {
    if (slot < 0 || slot > 3) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        Log::memory()->warn("ROM slot {}: failed to open '{}'", slot, path);
        return false;
    }
    f.read(reinterpret_cast<char*>(data_.data() + slot * 0x4000), 0x4000);
    loaded_[slot] = f.gcount() == 0x4000;
    if (loaded_[slot]) {
        Log::memory()->info("ROM slot {}: loaded '{}' (16384 bytes)", slot, path);
    } else {
        Log::memory()->warn("ROM slot {}: '{}' short read ({} bytes)", slot, path, f.gcount());
    }
    return loaded_[slot];
}

uint8_t Rom::read(uint32_t addr) const {
    if (addr >= ROM_SIZE) return 0xFF;
    return data_[addr];
}

uint8_t* Rom::page_ptr(uint16_t page) {
    uint32_t offset = static_cast<uint32_t>(page) * 0x2000;
    if (offset >= ROM_SIZE) return nullptr;
    return data_.data() + offset;
}

const uint8_t* Rom::page_ptr(uint16_t page) const {
    uint32_t offset = static_cast<uint32_t>(page) * 0x2000;
    if (offset >= ROM_SIZE) return nullptr;
    return data_.data() + offset;
}
