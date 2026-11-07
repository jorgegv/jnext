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

// A block name by appmake's rule (GH #478; appmake.c mb_enumerate_banks(), zxn.c):
// the first bank space, in zxn's order BANK, DIV, PAGE, whose name is found in the
// block (its first occurrence, `strstr`) followed by `_<n>` (`sscanf("_%d")`);
// whatever follows the number is part of the name. A BANK block is the lower or
// upper 8K of the bank when `_L` / `_H` follows the number (`"BANK_%*d_%c"`).
SectionName classify_block(std::string_view block)
{
    struct Space {
        std::string_view id;
        Family family;
    };
    for (const Space& space : {Space{"BANK", Family::Bank}, Space{"DIV", Family::Div},
                               Space{"PAGE", Family::Page}}) {
        const size_t at = block.find(space.id);
        if (at == std::string_view::npos) continue;
        std::string_view rest = block.substr(at + space.id.size());
        if (rest.empty() || rest[0] != '_') continue;
        rest.remove_prefix(1);
        size_t digits = 0;
        int value = 0;
        while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '9') {
            value = std::min(value * 10 + (rest[digits] - '0'), 99999);   // past every range
            ++digits;
        }
        if (digits == 0) continue;
        SectionName out;
        out.family = space.family;
        out.number = value;
        if (space.family == Family::Bank && rest.size() > digits + 1 && rest[digits] == '_'
            && (rest[digits + 1] == 'L' || rest[digits + 1] == 'H'))
            out.half = rest[digits + 1];
        return out;
    }
    return {};
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

Z88dkSpace paged_in(jnext::dbg::FetchSource space, uint16_t addr16)
{
    Z88dkSpace out;
    out.kind = Z88dkSpace::Kind::Paged;
    out.space = space;
    out.addr16 = addr16;
    return out;
}

Z88dkSpace refused_dotn(const std::string& what)
{
    Z88dkSpace out;
    out.kind = Z88dkSpace::Kind::Refused;
    out.why = what + ": a dotn command's main bank and PAGE / DIV sections are "
              "pages NextZXOS allocates when it runs (__z_page_table); a dotn map cannot place "
              "them (build it as a dotx to debug it)";
    return out;
}

// The head a PAGE section runs on from, and its name for a message.
struct PageHead {
    std::optional<uint16_t> value;
    std::string name;   // `__PAGE_<n>_head` or `__<block>_head`
};

// Every rule after the name is classified. `what` names the section in a message
// ("section 'x'", or "section 'x' (block 'y')" when its block placed it).
Z88dkSpace place(const SectionName& name, const std::string& what, uint32_t value,
                 const std::function<PageHead()>& page_head, const Z88dkDot* dot)
{
    const uint16_t addr16 = static_cast<uint16_t>(value & 0xFFFF);
    if (name.family == Family::Other) {
        if (value > 0xFFFF) {
            Z88dkSpace out;
            out.kind = Z88dkSpace::Kind::Refused;
            char hex[16];
            std::snprintf(hex, sizeof hex, "%X", static_cast<unsigned>(value));
            out.why = what + " names no bank or page, so the 24-bit address $" + hex
                    + " cannot be placed";
            return out;
        }
        if (dot) {
            // Design 9.4 rule 5: NextZXOS runs the command's $2000-$3FFF from a DivMMC bank.
            if (addr16 >= 0x2000 && addr16 < 0x4000)
                return paged_in(MemSpace::divmmc_ram(kNextZxosDotBank), addr16);
            if (dot->variant == Z88dkDot::Variant::Dotn)
                return refused_dotn(what);
        }
        Z88dkSpace out;
        out.kind = Z88dkSpace::Kind::BindAtLoad;
        out.addr16 = addr16;
        return out;
    }
    if (dot && dot->variant == Z88dkDot::Variant::Dotn
        && (name.family == Family::Page || name.family == Family::Div))
        return refused_dotn(what);

    const int limit = name.family == Family::Page ? kMaxPage
                    : name.family == Family::Bank ? kMaxBank
                                                  : kMaxDiv;
    if (name.number > limit) {
        const std::string why = what + " is out of range";
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
        return unpaged(addr16, what + ": a DIV_n bank is the one the program, or "
                               "a dotn command's allocator, chooses when it runs");
    case Family::Bank: {
        const int page = name.half == 'L' ? 2 * name.number
                       : name.half == 'H' ? 2 * name.number + 1
                                          : 2 * name.number + ((addr16 >> 13) & 1);
        return paged(page, addr16);
    }
    case Family::Page: {
        const PageHead head = page_head();
        if (!head.value)
            return unpaged(addr16, what + " has no " + head.name + " in the map");
        if (addr16 < *head.value)
            return unpaged(addr16, "address is below " + head.name + " of " + what);
        const int page = name.number + (((*head.value & 0x1FFF) + (addr16 - *head.value)) >> 13);
        if (page > kMaxPage)
            return unpaged(addr16, what + " runs past page 223");
        return paged(page, addr16);
    }
    case Family::Other:
        break;
    }
    return unpaged(addr16, what);
}

} // namespace

void Z88dkBlocks::add(const Z88dkMapLine& line)
{
    if (!line.is_const || line.section.empty()) return;
    const std::string& n = line.name;
    if (n.size() <= 7 || n.compare(0, 2, "__") != 0 || n.compare(n.size() - 5, 5, "_head") != 0)
        return;
    const std::string section = n.substr(2, n.size() - 7);
    block_.emplace(section, line.section);
    head_.emplace(section, line.value);
}

const std::string* Z88dkBlocks::block_of(std::string_view section) const
{
    const auto it = block_.find(section);
    return it == block_.end() ? nullptr : &it->second;
}

std::optional<uint16_t> Z88dkBlocks::head(std::string_view block) const
{
    const auto it = head_.find(block);
    if (it == head_.end() || it->second > 0xFFFFFF) return std::nullopt;
    return static_cast<uint16_t>(it->second & 0xFFFF);   // design 9.4 rule 1: bits 16+ not read
}

std::optional<Z88dkDot> z88dk_dot_command(const std::map<std::string, uint64_t>& consts,
                                          std::string* why)
{
    if (why) why->clear();
    const auto offset = consts.find("startupoffset");
    if (offset == consts.end()) return std::nullopt;
    Z88dkDot dot;
    switch (offset->second) {
    case 0x100: dot.variant = Z88dkDot::Variant::Dot; break;
    case 0x200: dot.variant = Z88dkDot::Variant::Dotx; break;
    case 0x300: dot.variant = Z88dkDot::Variant::Dotn; break;
    default: return std::nullopt;
    }
    const auto head = consts.find("__CODE_head");
    if (head == consts.end() || head->second != 0x2000) {
        if (why) *why = "a dot command's CODE must start at $2000";
        return std::nullopt;
    }
    return dot;
}

Z88dkSpace z88dk_section_space(
    std::string_view section, uint32_t value,
    const std::function<std::optional<uint16_t>(int)>& page_head,
    const Z88dkDot* dot)
{
    return z88dk_section_space(section, value, page_head, dot, nullptr);
}

Z88dkSpace z88dk_section_space(
    std::string_view section, uint32_t value,
    const std::function<std::optional<uint16_t>(int)>& page_head,
    const Z88dkDot* dot, const Z88dkBlocks* blocks)
{
    const std::string what = "section '" + std::string(section) + "'";
    if (const std::string* block = blocks ? blocks->block_of(section) : nullptr) {
        const SectionName name = classify_block(*block);
        if (name.family != Family::Other) {
            const auto block_head = [&] {
                return PageHead{blocks->head(*block), "__" + *block + "_head"};
            };
            return place(name, what + " (block '" + *block + "')", value, block_head, dot);
        }
    }
    const SectionName name = classify(section);
    const auto section_head = [&] {
        return PageHead{page_head ? page_head(name.number) : std::nullopt,
                        "__PAGE_" + std::to_string(name.number) + "_head"};
    };
    return place(name, what, value, section_head, dot);
}

std::optional<uint16_t> z88dk_page_head(const std::map<std::string, uint64_t>& consts, int n)
{
    const auto it = consts.find("__PAGE_" + std::to_string(n) + "_head");
    if (it == consts.end() || it->second > 0xFFFFFF) return std::nullopt;
    // Design 9.4 rule 1: in a numbered section the head's bits above 16 are
    // not read (newlib puts the page there for __banked; GH #345).
    return static_cast<uint16_t>(it->second & 0xFFFF);
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
    out.is_public = trim(field[1]) == "public";
    out.module = std::string(trim(field[3]));
    out.section = std::string(trim(field[4]));
    // Leading blanks go; the trailing ones too, but a TAB inside stays.
    out.location = std::string(trim(field[5], " \r\n"));
    return out;
}
