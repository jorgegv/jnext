#include "remote/gdb/target_desc.h"

#include "remote/gdb/rsp_codec.h"

namespace jnext {
namespace remote {
namespace gdb {

std::uint16_t reg_value(int n, const Z80Registers& r, std::uint64_t tstates) {
    switch (n) {
        case 0:  return r.AF;
        case 1:  return r.BC;
        case 2:  return r.DE;
        case 3:  return r.HL;
        case 4:  return r.AF2;
        case 5:  return r.BC2;
        case 6:  return r.DE2;
        case 7:  return r.HL2;
        case 8:  return r.IX;
        case 9:  return r.IY;
        case 10: return r.SP;
        case 11: return r.PC;
        // The low and high halves of a 64-bit count: it wraps every 2^32
        // T-states (~20 minutes at 3.5 MHz), and the client's profiler only
        // takes differences (design §3.1).
        case REG_CLOCKL: return static_cast<std::uint16_t>(tstates & 0xFFFF);
        case REG_CLOCKH: return static_cast<std::uint16_t>((tstates >> 16) & 0xFFFF);
        default: return 0;
    }
}

std::string pack_register(std::uint16_t value) {
    const std::uint8_t le[2] = {static_cast<std::uint8_t>(value & 0xFF),
                                static_cast<std::uint8_t>(value >> 8)};
    return to_hex(le, 2);
}

std::string pack_registers(const Z80Registers& r, std::uint64_t tstates) {
    std::string out;
    out.reserve(REG_COUNT * 4);
    for (int n = 0; n < REG_COUNT; ++n) out += pack_register(reg_value(n, r, tstates));
    return out;
}

bool unpack_register(const std::string& hex, std::uint16_t& out) {
    std::string bytes;
    if (hex.size() != 4 || !from_hex(hex, bytes)) return false;
    out = static_cast<std::uint16_t>(static_cast<std::uint8_t>(bytes[0]) |
                                     (static_cast<std::uint8_t>(bytes[1]) << 8));
    return true;
}

bool unpack_registers(const std::string& hex, std::array<std::uint16_t, REG_COUNT>& out) {
    if (hex.size() != static_cast<std::size_t>(REG_COUNT) * 4) return false;
    for (int n = 0; n < REG_COUNT; ++n)
        if (!unpack_register(hex.substr(static_cast<std::size_t>(n) * 4, 4),
                             out[static_cast<std::size_t>(n)]))
            return false;
    return true;
}

}  // namespace gdb
}  // namespace remote
}  // namespace jnext
