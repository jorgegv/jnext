#include "ram.h"
#include <cstring>

Ram::Ram(size_t size_bytes) : data_(size_bytes, 0) {}

uint8_t Ram::read(uint32_t addr) const {
    if (addr >= data_.size()) return 0xFF;
    return data_[addr];
}

void Ram::write(uint32_t addr, uint8_t val) {
    if (addr < data_.size()) data_[addr] = val;
}

uint8_t* Ram::page_ptr(uint16_t page) {
    uint32_t offset = static_cast<uint32_t>(page) * 0x2000;
    if (offset >= data_.size()) return nullptr;
    return data_.data() + offset;
}

const uint8_t* Ram::page_ptr(uint16_t page) const {
    uint32_t offset = static_cast<uint32_t>(page) * 0x2000;
    if (offset >= data_.size()) return nullptr;
    return data_.data() + offset;
}

void Ram::reset() { std::fill(data_.begin(), data_.end(), 0); }
