#include "debug/z88dk_space.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>

namespace {

using jnext::dbg::MemSpace;

constexpr int kMaxPage = 223;   // NR 0x50-0x57 numbers
constexpr int kMaxBank = 111;   // 16K banks
constexpr int kMaxDiv = 15;

enum class Family { Other, Page, Bank, Div };

struct SectionName {
    Family family = Family::Other;
    int number = 0;
    char half = 0;   // 'L' / 'H' for a `_L` / `_H` bank section
};

// A section number: 1 to 3 plain digits.
std::optional<int> number(std::string_view text)
{
    if (text.empty() || text.size() > 3) return std::nullopt;
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// The names of z88dk's newlib and classic memory maps (design 9.4).
SectionName classify(std::string_view section)
{
    SectionName out;
    std::string_view rest = section;
    bool has_kind = false;
    bool aligned_kind = false;   // RODATA / DATA: `<KIND>_<n>_align_<m>` exists
    for (const std::string_view kind : {std::string_view("CODE_"), std::string_view("RODATA_"),
                                        std::string_view("DATA_"), std::string_view("BSS_")}) {
        if (starts_with(rest, kind)) {
            aligned_kind = kind == "RODATA_" || kind == "DATA_";
            rest.remove_prefix(kind.size());
            has_kind = true;
            break;
        }
    }

    if (starts_with(rest, "PAGE_")) {
        if (const auto n = number(rest.substr(5))) {
            out.family = Family::Page;
            out.number = *n;
        }
        return out;
    }
    if (starts_with(rest, "DIV_")) {
        if (const auto n = number(rest.substr(4))) {
            out.family = Family::Div;
            out.number = *n;
        }
        return out;
    }
    if (!has_kind) {
        if (!starts_with(rest, "BANK_")) return out;
        rest.remove_prefix(5);
    }
    // `<n>`, `<n>_L`, `<n>_H`, and for RODATA / DATA `<n>_align_<m>`.
    const size_t underscore = rest.find('_');
    const auto n = number(rest.substr(0, underscore));
    if (!n) return out;
    if (underscore == std::string_view::npos) {
        out.family = Family::Bank;
        out.number = *n;
    } else {
        const std::string_view suffix = rest.substr(underscore);
        if (suffix == "_L" || suffix == "_H") {
            out.family = Family::Bank;
            out.number = *n;
            out.half = suffix[1];
        } else if (aligned_kind && starts_with(suffix, "_align_") && number(suffix.substr(7))) {
            out.family = Family::Bank;
            out.number = *n;
        }
    }
    return out;
}

Z88dkSpace unpaged(uint16_t addr16, std::string why)
{
    Z88dkSpace out;
    out.kind = Z88dkSpace::Kind::Unpaged;
    out.addr16 = addr16;
    out.why = std::move(why);
    return out;
}

Z88dkSpace paged(int page, uint16_t addr16)
{
    Z88dkSpace out;
    out.kind = Z88dkSpace::Kind::Paged;
    out.space = MemSpace::page(static_cast<uint16_t>(page));
    out.addr16 = addr16;
    return out;
}

} // namespace

Z88dkSpace z88dk_section_space(
    std::string_view section, uint32_t value,
    const std::function<std::optional<uint16_t>(int)>& page_head)
{
    const uint16_t addr16 = static_cast<uint16_t>(value & 0xFFFF);
    const SectionName name = classify(section);
    const std::string text(section);

    if (name.family == Family::Other) {
        if (value > 0xFFFF) {
            Z88dkSpace out;
            out.kind = Z88dkSpace::Kind::Refused;
            char hex[16];
            std::snprintf(hex, sizeof hex, "%X", static_cast<unsigned>(value));
            out.why = "section '" + text + "' names no bank or page, so the 24-bit address $"
                    + hex + " cannot be placed";
            return out;
        }
        Z88dkSpace out;
        out.kind = Z88dkSpace::Kind::BindAtLoad;
        out.addr16 = addr16;
        return out;
    }

    const int limit = name.family == Family::Page ? kMaxPage
                    : name.family == Family::Bank ? kMaxBank
                                                  : kMaxDiv;
    if (name.number > limit) {
        const std::string why = "section '" + text + "' is out of range";
        if (value > 0xFFFF) {
            Z88dkSpace out;
            out.kind = Z88dkSpace::Kind::Refused;
            out.why = why;
            return out;
        }
        return unpaged(addr16, why);
    }

    switch (name.family) {
    case Family::Div:
        return unpaged(addr16, "DivMMC sections are not supported yet (section '" + text + "')");
    case Family::Bank: {
        const int page = name.half == 'L' ? 2 * name.number
                       : name.half == 'H' ? 2 * name.number + 1
                                          : 2 * name.number + ((addr16 >> 13) & 1);
        return paged(page, addr16);
    }
    case Family::Page: {
        const auto head = page_head ? page_head(name.number) : std::nullopt;
        if (!head)
            return unpaged(addr16, "section '" + text + "' has no __PAGE_"
                                   + std::to_string(name.number) + "_head in the map");
        if (addr16 < *head)
            return unpaged(addr16, "address is below __PAGE_" + std::to_string(name.number)
                                   + "_head of section '" + text + "'");
        const int page = name.number + (((*head & 0x1FFF) + (addr16 - *head)) >> 13);
        if (page > kMaxPage)
            return unpaged(addr16, "section '" + text + "' runs past page 223");
        return paged(page, addr16);
    }
    case Family::Other:
        break;
    }
    return unpaged(addr16, "section '" + text + "'");
}

namespace {

std::string_view trim(std::string_view text, std::string_view chars = " \t\r\n")
{
    const size_t first = text.find_first_not_of(chars);
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(chars) - first + 1);
}

} // namespace

std::optional<Z88dkMapLine> parse_z88dk_map_line(std::string_view line)
{
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) return std::nullopt;
    const size_t semi = line.find(';', eq);
    if (semi == std::string_view::npos) return std::nullopt;

    Z88dkMapLine out;
    out.name = std::string(trim(line.substr(0, eq)));
    if (out.name.empty()) return std::nullopt;

    const std::string_view value = trim(line.substr(eq + 1, semi - eq - 1));
    if (value.size() < 2 || value[0] != '$') return std::nullopt;
    const std::string_view digits = value.substr(1);
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(),
                                              out.value, 16);
    if (error != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;

    // type, scope, def, module, section, location: the location is the rest,
    // after the fifth comma (it can hold commas, and sdcc's holds a TAB).
    std::string_view rest = line.substr(semi + 1);
    std::string_view field[6];
    for (int i = 0; i < 5; ++i) {
        const size_t comma = rest.find(',');
        if (comma == std::string_view::npos) {
            field[i] = rest;
            rest = {};
            break;
        }
        field[i] = rest.substr(0, comma);
        rest.remove_prefix(comma + 1);
        if (i == 4) field[5] = rest;
    }
    const std::string_view type = trim(field[0]);
    out.is_addr = type == "addr";
    out.is_const = type == "const";
    out.module = std::string(trim(field[3]));
    out.section = std::string(trim(field[4]));
    // Leading blanks go; the trailing ones too, but a TAB inside stays.
    out.location = std::string(trim(field[5], " \r\n"));
    return out;
}
