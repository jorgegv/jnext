#pragma once
#include <cstdint>
#include <array>
#include <functional>

// ZX Spectrum Next NextREG register file.
// Accessed via ports 0x243B (select) and 0x253B (data).
class NextReg {
public:
    NextReg();
    void reset();

    void    select(uint8_t reg);          // write to port 0x243B
    uint8_t read_selected() const;        // read from port 0x253B
    void    write_selected(uint8_t val);  // write to port 0x253B

    uint8_t read(uint8_t reg) const;
    void    write(uint8_t reg, uint8_t val);

    // Install a handler called when a specific register is written
    void set_write_handler(uint8_t reg, std::function<void(uint8_t)> fn);

private:
    std::array<uint8_t, 256> regs_{};
    uint8_t selected_ = 0;
    std::array<std::function<void(uint8_t)>, 256> write_handlers_{};
};
