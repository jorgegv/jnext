#include "remote/zrcp/zrcp_format.h"

#include <cstdio>

namespace jnext {
namespace remote {
namespace zrcp {

std::string flags_string(std::uint8_t f) {
    static const char kNames[] = "SZ5H3PNC";
    std::string out(8, '-');
    for (int i = 0; i < 8; ++i)
        if (f & (0x80 >> i)) out[static_cast<std::size_t>(i)] = kNames[i];
    return out;
}

std::uint16_t mmu_value(const jnext::dbg::SlotInfo& slot, int index) {
    if (!slot.is_rom) return slot.effective_page;
    // A ROM slot's `effective_page` is the un-shifted ROM page (0..7 on the
    // Next: image k is pages 2k and 2k+1), so the image is the page >> 1.
    const unsigned rom_bank = static_cast<unsigned>(slot.effective_page) >> 1;
    return static_cast<std::uint16_t>(0x8000u | ((rom_bank & 1u) << 1) |
                                      (static_cast<unsigned>(index) & 1u));
}

/// 16 K segment `seg` (0..3) of a legacy machine as ZEsarUX's mem128.c records
/// it: ROM image or RAM bank, from the segment's first 8 K slot.
static std::uint16_t legacy_segment(const std::array<jnext::dbg::SlotInfo, 8>& slots, int seg) {
    const auto& s = slots[static_cast<std::size_t>(seg * 2)];
    const unsigned v = static_cast<unsigned>(s.effective_page) >> 1;
    return static_cast<std::uint16_t>(s.is_rom ? 0x8000u + v : v);
}

std::string mmu_field_legacy(const std::array<jnext::dbg::SlotInfo, 8>& slots) {
    char buf[8];
    std::string out;
    for (int seg = 0; seg < 4; ++seg) {
        std::snprintf(buf, sizeof(buf), "%04x", legacy_segment(slots, seg));
        out += buf;
    }
    return out + "0000000000000000";
}

std::uint16_t mapped_page(const std::array<jnext::dbg::SlotInfo, 8>& slots, int i,
                          MachineType type) {
    if (i < 0 || i > 7) return 0;
    if (type == MachineType::ZXN_ISSUE2) return mmu_value(slots[static_cast<std::size_t>(i)], i);
    return i < 4 ? legacy_segment(slots, i) : 0;
}

std::string register_line(const Z80Registers& r,
                          const std::array<jnext::dbg::SlotInfo, 8>& slots, MachineType type) {
    // ZEsarUX's print_registers() format string (debug.c), field for field.
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "PC=%04x SP=%04x AF=%04x BC=%04x HL=%04x DE=%04x IX=%04x IY=%04x "
                  "AF'=%04x BC'=%04x HL'=%04x DE'=%04x I=%02x R=%02x  "
                  "F=%s F'=%s MEMPTR=%04x IM%u IFF%c%c VPS: 0 MMU=",
                  r.PC, r.SP, r.AF, r.BC, r.HL, r.DE, r.IX, r.IY, r.AF2, r.BC2, r.HL2, r.DE2,
                  r.I, r.R, flags_string(static_cast<std::uint8_t>(r.AF & 0xFF)).c_str(),
                  flags_string(static_cast<std::uint8_t>(r.AF2 & 0xFF)).c_str(), r.MEMPTR,
                  static_cast<unsigned>(r.IM), r.IFF1 ? '1' : '-', r.IFF2 ? '2' : '-');
    std::string out = buf;
    if (type != MachineType::ZXN_ISSUE2) return out + mmu_field_legacy(slots);
    for (int i = 0; i < 8; ++i) {
        std::snprintf(buf, sizeof(buf), "%04x", mmu_value(slots[static_cast<std::size_t>(i)], i));
        out += buf;
    }
    return out;
}

std::string strip_dollar(const std::string& mnemonic) {
    std::string out;
    out.reserve(mnemonic.size());
    for (char c : mnemonic)
        if (c != '$') out.push_back(c);
    return out;
}

std::string disasm_line(std::uint16_t addr, const std::string& mnemonic) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "  %04X ", addr);
    return buf + strip_dollar(mnemonic);
}

std::string hexdump(std::uint16_t addr, const std::uint8_t* bytes, std::size_t n) {
    // remote_hexdump() (remote.c): the address as ZEsarUX's memory-zone printer
    // spells a 64 KB zone (six columns, four digits), then the bytes, a short
    // last line padded to the ASCII column, and the printable ASCII.
    std::string out;
    char        buf[16];
    for (std::size_t off = 0; off < n; off += 16) {
        std::snprintf(buf, sizeof(buf), "  %04XH ", static_cast<unsigned>((addr + off) & 0xFFFF));
        out += buf;
        const std::size_t len = n - off < 16 ? n - off : 16;
        for (std::size_t i = 0; i < len; ++i) {
            std::snprintf(buf, sizeof(buf), "%02X ", bytes[off + i]);
            out += buf;
        }
        for (std::size_t i = len; i < 16; ++i) out += "   ";
        out += " |";
        for (std::size_t i = 0; i < len; ++i) {
            const unsigned char c = bytes[off + i];
            out.push_back(c < 32 || c > 126 ? '.' : static_cast<char>(c));
        }
        out += "|\n";
    }
    return out;
}

std::uint32_t crc32_ieee(const std::uint8_t* bytes, std::size_t n) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= bytes[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

std::string memory_pages(const std::array<jnext::dbg::SlotInfo, 8>& slots, bool verbose,
                         MachineType type) {
    std::string out;
    char        buf[160];
    if (type != MachineType::ZXN_ISSUE2) {
        // ZEsarUX's 48K default is two fixed segments; 128K / +3 are four 16 K
        // ones named from mem128.c's map (debug.c, the RO%X / RA%X branch).
        struct Seg { std::string shortname, longname; unsigned start, end; };
        std::vector<Seg> segs;
        if (type == MachineType::ZX48K) {
            segs.push_back({"ROM", "System ROM", 0x0000, 0x3FFF});
            segs.push_back({"RAM", "System RAM", 0x4000, 0xFFFF});
        } else {
            for (int seg = 0; seg < 4; ++seg) {
                const std::uint16_t v = legacy_segment(slots, seg);
                char sn[16], ln[32];
                std::snprintf(sn, sizeof(sn), (v & 0x8000) ? "RO%X" : "RA%X", v & 0x7FFFu);
                std::snprintf(ln, sizeof(ln), (v & 0x8000) ? "ROM %X" : "RAM %X", v & 0x7FFFu);
                segs.push_back({sn, ln, seg * 0x4000u, seg * 0x4000u + 0x3FFF});
            }
        }
        for (std::size_t i = 0; i < segs.size(); ++i) {
            if (!verbose) {
                out += segs[i].shortname + " ";
                continue;
            }
            std::snprintf(buf, sizeof(buf),
                          "Segment %d\nLong name: %s\nShort name: %s\nStart: %XH\nEnd: %XH\n\n",
                          static_cast<int>(i) + 1, segs[i].longname.c_str(),
                          segs[i].shortname.c_str(), segs[i].start, segs[i].end);
            out += buf;
        }
        return out;
    }
    for (int i = 0; i < 8; ++i) {
        const auto& s = slots[static_cast<std::size_t>(i)];
        const std::string shortname =
            s.is_rom ? std::string("RO") : "A" + std::to_string(s.effective_page);
        if (!verbose) {
            out += shortname + " ";
            continue;
        }
        const std::string longname =
            s.is_rom ? std::string("ROM") : "RAM " + std::to_string(s.effective_page);
        std::snprintf(buf, sizeof(buf),
                      "Segment %d\nLong name: %s\nShort name: %s\nStart: %XH\nEnd: %XH\n\n",
                      i + 1, longname.c_str(), shortname.c_str(), i * 0x2000,
                      i * 0x2000 + 0x1FFF);
        out += buf;
    }
    return out;
}

std::string machine_name(MachineType type) {
    switch (type) {
        case MachineType::ZXN_ISSUE2: return "ZX Spectrum Next";
        case MachineType::ZX48K:      return "ZX Spectrum 48k";
        case MachineType::ZX128K:     return "ZX Spectrum 128k";
        case MachineType::ZX_PLUS3:   return "ZX Spectrum +3";
    }
    return "ZX Spectrum Next";
}

bool parse_number(const std::string& tok, std::uint32_t& out) {
    if (tok.empty()) return false;
    const char last = tok.back();
    const bool hex  = last == 'H' || last == 'h';
    const std::size_t digits = hex ? tok.size() - 1 : tok.size();
    if (digits == 0) return false;
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < digits; ++i) {
        const char c = tok[i];
        unsigned   d;
        if (c >= '0' && c <= '9')
            d = static_cast<unsigned>(c - '0');
        else if (hex && c >= 'a' && c <= 'f')
            d = static_cast<unsigned>(c - 'a' + 10);
        else if (hex && c >= 'A' && c <= 'F')
            d = static_cast<unsigned>(c - 'A' + 10);
        else
            return false;
        v = v * (hex ? 16u : 10u) + d;
        if (v > 0xFFFFFFFFull) return false;
    }
    out = static_cast<std::uint32_t>(v);
    return true;
}

std::vector<std::string> split_args(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}  // namespace zrcp
}  // namespace remote
}  // namespace jnext
