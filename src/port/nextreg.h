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
    uint8_t read_selected();              // read from port 0x253B
    void    write_selected(uint8_t val);  // write to port 0x253B

    uint8_t read(uint8_t reg);
    void    write(uint8_t reg, uint8_t val);

    // Install a handler called when a specific register is written
    void set_write_handler(uint8_t reg, std::function<void(uint8_t)> fn);

    // Install a handler called when a specific register is read.
    // The handler returns the dynamic value; if no handler is set, the cached
    // value from regs_[] is returned.
    void set_read_handler(uint8_t reg, std::function<uint8_t()> fn);

    // Direct access to cached register value (bypasses read handler)
    uint8_t cached(uint8_t reg) const { return regs_[reg]; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    std::array<uint8_t, 256> regs_{};
    uint8_t selected_ = 0x24;  // VHDL zxnext.vhd:4594-4596 reset default
    std::array<std::function<void(uint8_t)>, 256> write_handlers_{};
    std::array<std::function<uint8_t()>, 256> read_handlers_{};
};
