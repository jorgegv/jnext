#pragma once
#include <cstdint>
#include <vector>

class Ram {
public:
    explicit Ram(size_t size_bytes = 1792 * 1024);
    uint8_t read(uint32_t addr) const;
    void write(uint32_t addr, uint8_t val);
    uint8_t* page_ptr(uint16_t page);       // pointer to start of 8K page
    const uint8_t* page_ptr(uint16_t page) const;
    void reset();
    size_t size() const { return data_.size(); }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    std::vector<uint8_t> data_;
};
