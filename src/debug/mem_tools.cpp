#include "debug/mem_tools.h"


namespace jnext {
namespace dbg {
namespace memtools {

namespace {

void set_why(std::string* why, const std::string& s) {
    if (why) *why = s;
}

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::vector<std::string> tokens(std::string_view t) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && is_space(t[i])) ++i;
        size_t j = i;
        while (j < t.size() && !is_space(t[j])) ++j;
        if (j > i) out.emplace_back(t.substr(i, j - i));
        i = j;
    }
    return out;
}

// Hex bytes into (bytes, mask); '?' only when allow_q. Bad characters are refused before the
// digit count, so "$CD" and "0xCD" read as not-hex rather than odd.
bool parse_bytes(std::string_view text, bool allow_q, std::vector<uint8_t>* bytes,
                 std::vector<uint8_t>* mask, std::string* why) {
    for (const std::string& tok : tokens(text)) {
        for (char c : tok) {
            if (nibble(c) < 0 && !(allow_q && c == '?')) {
                set_why(why, "not a hex byte: '" + tok + "'");
                return false;
            }
        }
        if (tok.size() % 2) {
            set_why(why, "odd number of hex digits in '" + tok + "'");
            return false;
        }
        for (size_t i = 0; i < tok.size(); i += 2) {
            uint8_t b = 0, m = 0;
            for (int k = 0; k < 2; ++k) {
                const char c = tok[i + k];
                b = static_cast<uint8_t>(b << 4);
                m = static_cast<uint8_t>(m << 4);
                if (c != '?') {
                    b = static_cast<uint8_t>(b | nibble(c));
                    m = static_cast<uint8_t>(m | 0xF);
                }
            }
            bytes->push_back(b);
            mask->push_back(m);
        }
    }
    if (bytes->empty()) {
        set_why(why, "no bytes given");
        return false;
    }
    return true;
}

}  // namespace

std::optional<Pattern> parse_hex_pattern(std::string_view text, std::string_view mask_text,
                                         std::string* why) {
    Pattern p;
    if (!parse_bytes(text, true, &p.bytes, &p.mask, why)) return std::nullopt;
    if (!tokens(mask_text).empty()) {
        std::vector<uint8_t> mb, mm;
        if (!parse_bytes(mask_text, false, &mb, &mm, why)) return std::nullopt;
        if (mb.size() != p.bytes.size()) {
            set_why(why, "the mask has " + std::to_string(mb.size()) + " bytes and the pattern " +
                             std::to_string(p.bytes.size()));
            return std::nullopt;
        }
        for (size_t i = 0; i < mb.size(); ++i) p.mask[i] = static_cast<uint8_t>(p.mask[i] & mb[i]);
    }
    bool any = false;
    for (uint8_t m : p.mask) any = any || m != 0;
    if (!any) {
        set_why(why, "a pattern of only wildcards matches everywhere");
        return std::nullopt;
    }
    return p;
}

std::optional<Pattern> text_pattern(std::string_view text, bool match_case, std::string* why) {
    if (text.empty()) {
        set_why(why, "no text given");
        return std::nullopt;
    }
    Pattern p;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7E) {
            set_why(why, "only printable ASCII text can be searched for");
            return std::nullopt;
        }
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!match_case && letter) {
            p.bytes.push_back(static_cast<uint8_t>(c & 0xDF));
            p.mask.push_back(0xDF);
        } else {
            p.bytes.push_back(c);
            p.mask.push_back(0xFF);
        }
    }
    return p;
}

std::optional<std::vector<uint8_t>> parse_fill(std::string_view text, std::string* why) {
    std::vector<uint8_t> b, m;
    if (!parse_bytes(text, false, &b, &m, why)) return std::nullopt;
    return b;
}

std::vector<uint8_t> fill_buffer(const std::vector<uint8_t>& value, size_t len) {
    std::vector<uint8_t> out(len);
    if (value.empty()) return out;
    for (size_t i = 0; i < len; ++i) out[i] = value[i % value.size()];
    return out;
}

std::optional<uint32_t> parse_length(std::string_view text, std::string* why) {
    size_t a = 0, b = text.size();
    while (a < b && is_space(text[a])) ++a;
    while (b > a && is_space(text[b - 1])) --b;
    const std::string orig(text.substr(a, b - a));
    std::string_view t = std::string_view(orig);
    if (!t.empty() && t[0] == '$') t.remove_prefix(1);
    else if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.remove_prefix(2);
    bool ok = !t.empty() && t.size() <= 8;
    uint32_t v = 0;
    for (char c : t) {
        const int n = nibble(c);
        if (n < 0) { ok = false; break; }
        v = (v << 4) | static_cast<uint32_t>(n);
    }
    if (!ok) {
        set_why(why, "not a hex length: '" + orig + "'");
        return std::nullopt;
    }
    return v;
}

std::optional<Hit> find(const uint8_t* data, size_t size, const Pattern& p, size_t from) {
    const size_t n = p.bytes.size();
    if (n == 0 || n > size || p.mask.size() != n) return std::nullopt;
    const size_t last = size - n;   // the last start that fits
    auto match = [&](size_t i) {
        for (size_t k = 0; k < n; ++k)
            if ((data[i + k] & p.mask[k]) != (p.bytes[k] & p.mask[k])) return false;
        return true;
    };
    for (size_t i = from; i <= last; ++i)
        if (match(i)) return Hit{i, false};
    for (size_t i = 0; i < from && i <= last; ++i)
        if (match(i)) return Hit{i, true};
    return std::nullopt;
}

uint32_t block_room(MemSpace s, uint32_t addr) {
    switch (s.kind) {
        case MemSpace::Kind::Cpu:
            return addr >= 0x10000 ? 0 : 0x10000 - addr;
        case MemSpace::Kind::Page: {
            if (s.index >= 0xE0 || addr >= 0x2000) return 0;
            return (0xE0u - s.index) * 0x2000u - addr;
        }
        case MemSpace::Kind::Rom:
            return addr >= 0x4000 ? 0 : 0x4000 - addr;
        default:
            return 0;
    }
}

std::optional<std::vector<Chunk>> split_block(MemSpace s, uint32_t addr, uint32_t len,
                                              std::string* why) {
    if (len == 0) {
        set_why(why, "the length is 0");
        return std::nullopt;
    }
    std::vector<Chunk> out;
    switch (s.kind) {
        case MemSpace::Kind::Cpu:
            if (addr >= 0x10000) { set_why(why, "the start is past $FFFF"); return std::nullopt; }
            if (static_cast<uint64_t>(addr) + len > 0x10000) {
                set_why(why, "the block runs past $FFFF");
                return std::nullopt;
            }
            out.push_back({s, addr, len});
            return out;
        case MemSpace::Kind::Page: {
            if (s.index >= 0xE0) { set_why(why, "the slot is paged to no memory"); return std::nullopt; }
            if (addr >= 0x2000) { set_why(why, "the start is outside the 8K page"); return std::nullopt; }
            if (len > block_room(s, addr)) { set_why(why, "the block runs past page DF"); return std::nullopt; }
            uint32_t page = s.index, off = addr, left = len;
            while (left) {
                const uint32_t n = left < 0x2000 - off ? left : 0x2000 - off;
                out.push_back({MemSpace::page(static_cast<uint16_t>(page)), off, n});
                left -= n;
                ++page;
                off = 0;
            }
            return out;
        }
        case MemSpace::Kind::Rom:
            if (addr >= 0x4000) { set_why(why, "the start is outside the 16K ROM"); return std::nullopt; }
            if (len > 0x4000 - addr) {
                set_why(why, "the block runs past the end of ROM" + std::to_string(s.index));
                return std::nullopt;
            }
            out.push_back({s, addr, len});
            return out;
        default:
            set_why(why, "this memory cannot be addressed as a block");
            return std::nullopt;
    }
}

}  // namespace memtools
}  // namespace dbg
}  // namespace jnext
