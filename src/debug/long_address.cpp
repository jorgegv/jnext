#include "debug/long_address.h"

#include <cctype>
#include <cstdio>

namespace jnext {
namespace dbg {

namespace {

using Kind = MemSpace::Kind;

std::string fmt(const char* f, unsigned v) {
    char b[24];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}

std::string sram(uint32_t a) { return fmt("$%06X", a); }

// [A-Z0-9]+ digits of `d` as a number, `n` digits exactly.
std::optional<unsigned> hex_digits(std::string_view d, size_t n) {
    if (d.size() != n) return std::nullopt;
    unsigned v = 0;
    for (char c : d) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
        v = v * 16 + (std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : c - 'A' + 10);
    }
    return v;
}

// 1 or 2 decimal digits, no leading zero, value <= max.
std::optional<unsigned> dec_digits(std::string_view d, unsigned max) {
    if (d.empty() || d.size() > 2 || (d.size() == 2 && d[0] == '0')) return std::nullopt;
    unsigned v = 0;
    for (char c : d) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
        v = v * 10 + (c - '0');
    }
    if (v > max) return std::nullopt;
    return v;
}

}  // namespace

std::string space_tag(MemSpace s) {
    switch (s.kind) {
        case Kind::Cpu:          return "CPU";
        case Kind::Page:         return fmt("P%02X", s.index);
        case Kind::Rom:          return fmt("ROM%u", s.index);
        case Kind::BootRom:      return "BOOT";
        case Kind::MultifaceRom: return "MFROM";
        case Kind::MultifaceRam: return "MFRAM";
        case Kind::DivMmcRom:    return "DIVROM";
        case Kind::DivMmcRam:    return fmt("DIVRAM%u", s.index);
        case Kind::AltRom:       return fmt("ALTROM%u", s.index);
        case Kind::Layer2:       return fmt("L2P%02X", s.index);
        case Kind::Config:       return fmt("CFG%02X", s.index);
        case Kind::Floating:     return "FLOAT";
    }
    return "CPU";
}

std::optional<MemSpace> parse_space_tag(std::string_view tag) {
    std::string t(tag);
    for (char& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::string_view v = t;
    auto starts = [&](std::string_view p) { return v.substr(0, p.size()) == p; };
    auto rest = [&](std::string_view p) { return v.substr(p.size()); };

    if (v == "CPU")    return MemSpace::cpu();
    if (v == "BOOT")   return MemSpace::boot_rom();
    if (v == "MFROM")  return MemSpace::multiface_rom();
    if (v == "MFRAM")  return MemSpace::multiface_ram();
    if (v == "DIVROM") return MemSpace::divmmc_rom();
    if (v == "FLOAT")  return MemSpace::floating();
    // Longest prefixes first: DIVRAM / ALTROM / ROM / L2P / CFG before P.
    if (starts("DIVRAM")) {
        auto n = dec_digits(rest("DIVRAM"), 15);
        if (n) return MemSpace::divmmc_ram(static_cast<uint16_t>(*n));
        return std::nullopt;
    }
    if (starts("ALTROM")) {
        auto n = dec_digits(rest("ALTROM"), 1);
        if (n && rest("ALTROM").size() == 1) return MemSpace::alt_rom(static_cast<uint16_t>(*n));
        return std::nullopt;
    }
    if (starts("ROM")) {
        auto n = dec_digits(rest("ROM"), 3);
        if (n && rest("ROM").size() == 1) return MemSpace::rom(static_cast<uint16_t>(*n));
        return std::nullopt;
    }
    if (starts("L2P")) {
        auto n = hex_digits(rest("L2P"), 2);
        if (n && *n < 0xE0) return MemSpace::layer2(static_cast<uint16_t>(*n));
        return std::nullopt;
    }
    if (starts("CFG")) {
        auto n = hex_digits(rest("CFG"), 2);
        if (n) return MemSpace::config(static_cast<uint16_t>(*n));
        return std::nullopt;
    }
    if (starts("P")) {
        auto n = hex_digits(rest("P"), 2);
        if (n && *n < 0xE0) return MemSpace::page(static_cast<uint16_t>(*n));
        return std::nullopt;
    }
    return std::nullopt;
}

std::string physical_text(MemSpace s, uint16_t addr) {
    const uint32_t off = space_offset(s, addr);
    switch (s.kind) {
        case Kind::Cpu:      return "";
        case Kind::Floating: return "FLOAT";
        case Kind::BootRom:  return fmt("BOOT+$%04X", off);
        case Kind::Page:
            // The two block-RAM banks are not in the SRAM space: bank 5 (pages
            // 0x0A/0x0B, a 16 KB BRAM) and the low half of bank 7 (page 0x0E)
            // (zxnext.vhd:2961-2962, :6558-6578, :6667-6670). Page 0x0F is SRAM.
            if (s.index == 0x0A || s.index == 0x0B)
                return fmt("BRAM5+$%04X", ((s.index & 1u) << 13) | off);
            if (s.index == 0x0E) return fmt("BRAM7+$%04X", off);
            return sram(((s.index + 0x20u) << 13) | off);       // :2964
        case Kind::Layer2:      return sram(((s.index + 0x20u) << 13) | off);   // :2971, no BRAM
        case Kind::Rom:         return sram((s.index << 14) | off);             // :3052-3057
        case Kind::AltRom:      return sram(0x018000u + s.index * 0x4000u + off);   // :2924-2925
        case Kind::Config:      return sram((s.index << 14) | off);             // :3044-3050
        case Kind::MultifaceRom: return sram(0x014000u | off);                  // :2923
        case Kind::MultifaceRam: return sram(0x016000u | off);
        case Kind::DivMmcRom:   return sram(0x010000u | off);                   // divmmc.vhd:94
        case Kind::DivMmcRam:   return sram(0x020000u + s.index * 0x2000u + off);   // :95-96
    }
    return "";
}

std::string space_text(MemSpace s, uint16_t addr, AddressStyle st, bool next_layout) {
    if (s.kind == MemSpace::Kind::Cpu) return "";
    if (st == AddressStyle::Physical && next_layout) return physical_text(s, addr);
    return space_tag(s);
}

std::string long_address(uint16_t addr, MemSpace s, AddressStyle st, bool next_layout) {
    std::string out = fmt("$%04X", addr);
    const std::string t = space_text(s, addr, st, next_layout);
    if (!t.empty()) out += " " + t;
    return out;
}

}  // namespace dbg
}  // namespace jnext
