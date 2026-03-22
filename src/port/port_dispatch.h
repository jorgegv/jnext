#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "cpu/z80_cpu.h"

struct PortHandler {
    uint16_t mask;
    uint16_t value;
    std::function<uint8_t(uint16_t port)>            read;
    std::function<void(uint16_t port, uint8_t val)>  write;
};

// ZX Spectrum I/O port dispatcher using address-line mask/value matching.
// ZX ports are decoded by address line masking, not full 16-bit compare.
class PortDispatch : public IoInterface {
public:
    PortDispatch();

    /// Remove all registered handlers (used before re-registering on reset).
    void clear_handlers() { handlers_.clear(); }

    void register_handler(uint16_t mask, uint16_t value,
                          std::function<uint8_t(uint16_t)>           rd,
                          std::function<void(uint16_t, uint8_t)>     wr);

    uint8_t read(uint16_t port) const;
    void    write(uint16_t port, uint8_t val);

    // IoInterface implementation
    uint8_t in(uint16_t port) override;
    void    out(uint16_t port, uint8_t val) override;

private:
    std::vector<PortHandler> handlers_;
};
