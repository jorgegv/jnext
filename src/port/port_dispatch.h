#pragma once
#include <cstdint>
#include <functional>
#include <vector>

struct PortHandler {
    uint16_t mask;
    uint16_t value;
    std::function<uint8_t(uint16_t port)>            read;
    std::function<void(uint16_t port, uint8_t val)>  write;
};

// ZX Spectrum I/O port dispatcher using address-line mask/value matching.
// ZX ports are decoded by address line masking, not full 16-bit compare.
class PortDispatch {
public:
    PortDispatch();

    void register_handler(uint16_t mask, uint16_t value,
                          std::function<uint8_t(uint16_t)>           rd,
                          std::function<void(uint16_t, uint8_t)>     wr);

    uint8_t read(uint16_t port) const;
    void    write(uint16_t port, uint8_t val);

private:
    std::vector<PortHandler> handlers_;
};
