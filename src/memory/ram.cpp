#include "ram.h"
#include "core/saveable.h"
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

void Ram::save_state(StateWriter& w) const
{
    w.write_u64(static_cast<uint64_t>(data_.size()));
    w.write_bytes(data_.data(), data_.size());
}

void Ram::load_state(StateReader& r)
{
    uint64_t sz = r.read_u64();
    r.read_bytes(data_.data(), static_cast<size_t>(sz));
}
