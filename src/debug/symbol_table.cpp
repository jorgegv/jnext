#include "symbol_table.h"

#include "debug/source_file.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <charconv>

int SymbolTable::load_z88dk_map(const std::string& path, std::string* why)
{
    if (std::string problem = source_file_problem(path); !problem.empty()) {
        if (why) *why = problem;
        return -1;
    }
    std::ifstream file(path);
    if (!file.is_open())
        return -1;

    clear();
    const int count = read_z88dk_map(file);
    loaded_file_ = path;
    return count;
}

int SymbolTable::read_z88dk_map(std::istream& file)
{
    int count = 0;
    std::string line;

    while (std::getline(file, line)) {
        // "; addr" lines are memory addresses: they name addresses (the
        // disassembler's immediates) and resolve by name. "; const" lines are
        // compile-time values — a size, or a section bound such as
        // `__data_crt_head` — so they must never name an address, but a script
        // or a remote client may still ask for one by name (#279).
        auto semi = line.find(';');
        if (semi == std::string::npos)
            continue;
        std::string metadata = line.substr(semi + 1);
        // Trim leading whitespace from metadata
        auto md_start = metadata.find_first_not_of(" \t");
        if (md_start == std::string::npos)
            continue;
        metadata = metadata.substr(md_start);
        // "; const" values (section bounds, sizes) are kept by NAME only:
        // `lookup_name()` resolves them, `lookup()` never sees them.
        const bool is_const = metadata.substr(0, 5) == "const";
        if (metadata.substr(0, 4) != "addr" && !is_const)
            continue;

        // Strip metadata for value parsing
        std::string def_part = line.substr(0, semi);

        // Find '=' separator
        auto eq = def_part.find('=');
        if (eq == std::string::npos)
            continue;

        // Extract symbol name (first non-whitespace token before '=')
        std::string name_part = def_part.substr(0, eq);
        // Trim trailing whitespace
        auto end = name_part.find_last_not_of(" \t\r\n");
        if (end == std::string::npos)
            continue;
        // Trim leading whitespace
        auto start = name_part.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;
        std::string name = name_part.substr(start, end - start + 1);
        if (name.empty())
            continue;
        // z88dk `-debug` debug records (GH #352): a per-line marker at the
        // address of the function label, and the C debug info. Reserved
        // prefixes (z80asm preproc.cpp / directives.c, sccz80 cdbfile.c); the
        // map is sorted by name, so a marker would otherwise win the address.
        if (name.compare(0, 9, "__C_LINE_") == 0 ||
            name.compare(0, 11, "__ASM_LINE_") == 0 ||
            name.compare(0, 11, "__CDBINFO__") == 0)
            continue;

        // Extract hex value after '=' — look for '$' prefix
        std::string val_part = def_part.substr(eq + 1);
        auto dollar = val_part.find('$');
        if (dollar == std::string::npos)
            continue;

        std::string hex_str;
        for (size_t i = dollar + 1; i < val_part.size(); ++i) {
            char c = val_part[i];
            if (std::isxdigit(static_cast<unsigned char>(c)))
                hex_str += c;
            else
                break;
        }

        if (hex_str.empty())
            continue;

        // Parse hex value — skip addresses that don't fit in 16 bits
        unsigned long addr_val = 0;
        try {
            addr_val = std::stoul(hex_str, nullptr, 16);
        } catch (...) {
            continue;
        }

        if (addr_val > 0xFFFF)
            continue;

        auto addr = static_cast<uint16_t>(addr_val);

        if (is_const) {
            add_value(name, addr);   // first occurrence wins
            continue;
        }

        add_label(name, addr, std::nullopt);   // first occurrence wins
        ++count;
    }

    return count;
}

int SymbolTable::load_simple_map(const std::string& path, std::string* why)
{
    if (std::string problem = source_file_problem(path); !problem.empty()) {
        if (why) *why = problem;
        return -1;
    }
    std::ifstream file(path);
    if (!file.is_open())
        return -1;

    clear();

    int count = 0;
    std::string line;

    while (std::getline(file, line)) {
        // Skip comment lines
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == ';')
            continue;

        // Find '=' separator
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        // Extract symbol name
        std::string name_part = line.substr(0, eq);
        auto end = name_part.find_last_not_of(" \t\r\n");
        if (end == std::string::npos) continue;
        auto start = name_part.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        std::string name = name_part.substr(start, end - start + 1);
        if (name.empty()) continue;

        // Extract hex value after '=' — look for '$' prefix
        std::string val_part = line.substr(eq + 1);
        auto dollar = val_part.find('$');
        if (dollar == std::string::npos) continue;

        std::string hex_str;
        for (size_t i = dollar + 1; i < val_part.size(); ++i) {
            char c = val_part[i];
            if (std::isxdigit(static_cast<unsigned char>(c)))
                hex_str += c;
            else
                break;
        }
        if (hex_str.empty()) continue;

        unsigned long addr_val = 0;
        try {
            addr_val = std::stoul(hex_str, nullptr, 16);
        } catch (...) {
            continue;
        }
        if (addr_val > 0xFFFF) continue;

        auto addr = static_cast<uint16_t>(addr_val);

        add_label(name, addr, std::nullopt);
        ++count;
    }

    loaded_file_ = path;
    return count;
}

namespace {

std::string_view trim_view(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// A whole field of hex digits, at most 16 bits.
std::optional<uint16_t> parse_hex16(std::string_view text)
{
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        }))
        return std::nullopt;
    unsigned int value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (ec != std::errc{} || end != text.data() + text.size() || value > 0xFFFF)
        return std::nullopt;
    return static_cast<uint16_t>(value);
}

// One sjasmplus `--sym` / `--exp` line (GH #390): `NAME: EQU 0x0000ABCD`, the
// exact text of its WriteLabelEquValue() (sjasm/sjio.cpp), plus the CR its
// Windows build adds (the files are opened in text mode). This is the ONE
// definition of an EQU line: the loader and the first-line check that picks it
// in load_z88dk_or_sjasmplus() both use it, so they cannot disagree about what
// a line is. The value keeps all 32 bits (sjasmplus writes a negative one as
// 0xFFFFFFFF); the loader decides which values fit an address. sjasmplus has
// written the keyword two ways: `EQU`, by `--exp` always and by `--sym` from
// v1.18.0 (both through WriteLabelEquValue), and `equ` by the `--sym` writer of
// v1.17.0 and every release before it (CLabelTable::DumpSymbols). Those two,
// exactly, and no other case or spacing.
struct EquLine {
    std::string name;
    uint32_t    value;
};

std::optional<EquLine> parse_equ_line(std::string_view line)
{
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    constexpr std::string_view kSeparators[] = {": EQU 0x", ": equ 0x"};
    for (const std::string_view separator : kSeparators) {
        const auto sep = line.find(separator);
        if (sep == 0 || sep == std::string_view::npos)
            continue;
        const std::string_view name = line.substr(0, sep);
        if (name.find_first_of(" \t:") != std::string_view::npos)
            continue;
        // The value is the whole rest of the line: hex digits only, at least one.
        const std::string_view hex = line.substr(sep + separator.size());
        uint32_t value = 0;
        const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
        if (ec != std::errc{} || end != hex.data() + hex.size())
            continue;
        return EquLine{std::string(name), value};
    }
    return std::nullopt;
}

}  // namespace

int SymbolTable::load_sjasmplus_equ(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return -1;

    clear();
    const int count = read_sjasmplus_equ(file);
    loaded_file_ = path;
    return count;
}

int SymbolTable::read_sjasmplus_equ(std::istream& file)
{
    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        const auto equ = parse_equ_line(line);
        if (!equ || equ->value > 0xFFFF)
            continue;
        const auto addr = static_cast<uint16_t>(equ->value);

        // First occurrence wins, as in the MAP loaders: an address keeps its
        // first name, a name its first address, each independently.
        add_label(equ->name, addr, std::nullopt);
        ++count;
    }

    return count;
}

int SymbolTable::load_z88dk_or_sjasmplus(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return -1;

    // ONE read of the file, kept in memory: the first line picks the loader,
    // which then reads every line, that one included. A second open would find
    // a pipe (`--map <(cmd)`) already drained by the first.
    std::ostringstream all;
    all << file.rdbuf();
    const std::string content = all.str();
    const bool sjasmplus =
        parse_equ_line(std::string_view(content).substr(0, content.find('\n'))).has_value();
    std::istringstream text(content);

    clear();
    const int count = sjasmplus ? read_sjasmplus_equ(text) : read_z88dk_map(text);
    loaded_file_ = path;
    return count;
}

int SymbolTable::load_nextbuild_memory(const std::string& path, std::string* why)
{
    if (std::string problem = source_file_problem(path); !problem.empty()) {
        if (why) *why = problem;
        return -1;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        if (why) *why = "could not open Memory.txt file";
        return -1;
    }

    clear();

    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const auto addr = parse_hex16(trim_view(std::string_view(line).substr(0, colon)));
        const std::string raw(trim_view(std::string_view(line).substr(colon + 1)));
        if (!addr || raw.empty())
            continue;

        // `._Main` -> `Main`: the label without its `.`, and a Boriel user
        // name without the one `_` the compiler prefixes. `.core.__LABEL` and
        // other double-underscore runtime names keep their underscores.
        std::string display = raw;
        if (display.front() == '.') display.erase(display.begin());
        if (display.size() > 1 && display[0] == '_' && display[1] != '_')
            display.erase(display.begin());
        if (display.empty())
            continue;

        // First occurrence wins, as in the MAP loaders.
        add_label(display, *addr, std::nullopt);
        for (const std::string& name : {raw, raw.front() == '.' ? raw.substr(1) : raw})
            add_name(name, *addr, std::nullopt);
        ++count;
    }

    loaded_file_ = path;
    return count;
}

std::optional<SymbolNameKey> SymbolTable::resolve_key(std::string_view text) const
{
    text = trim_view(text);
    if (text.empty())
        return std::nullopt;
    if (text.front() == '@')
        return find_name(std::string(trim_view(text.substr(1))));
    std::optional<uint16_t> number;
    if (text.front() == '$')
        number = parse_hex16(text.substr(1));
    else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        number = parse_hex16(text.substr(2));
    else if (const auto n = parse_hex16(text))
        number = n;
    else
        return find_name(std::string(text));
    // A '$' or '0x' prefix that is not a number is not a name either.
    if (!number)
        return std::nullopt;
    return SymbolNameKey{*number, std::nullopt};
}

std::optional<uint16_t> SymbolTable::resolve(std::string_view text) const
{
    const auto key = resolve_key(text);
    if (!key)
        return std::nullopt;
    return key->addr;
}

void SymbolTable::add_name(const std::string& name, uint16_t addr,
                           std::optional<jnext::dbg::FetchSource> space)
{
    name_to_key_.emplace(name, SymbolNameKey{addr, space});
}

bool SymbolTable::add_label(const std::string& name, uint16_t addr,
                            std::optional<jnext::dbg::FetchSource> space)
{
    add_name(name, addr, space);
    const auto range = labels_.equal_range(addr);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second.space == space)
            return false;
    labels_.emplace(addr, SymbolLabel{name, space});
    return true;
}

void SymbolTable::merge(const SymbolTable& other)
{
    for (const auto& [addr, label] : other.labels_) {
        bool taken = false;
        const auto range = labels_.equal_range(addr);
        for (auto it = range.first; it != range.second; ++it)
            if (it->second.space == label.space)
                taken = true;
        if (!taken)
            labels_.emplace(addr, label);
    }
    name_to_key_.insert(other.name_to_key_.begin(), other.name_to_key_.end());
    const_by_name_.insert(other.const_by_name_.begin(), other.const_by_name_.end());
    if (loaded_file_.empty())
        loaded_file_ = other.loaded_file_;
}

void SymbolTable::add_value(const std::string& name, uint16_t value)
{
    const_by_name_.emplace(name, value);
}

std::optional<std::string> SymbolTable::lookup(jnext::dbg::FetchSource space,
                                               uint16_t addr) const
{
    const auto range = labels_.equal_range(addr);
    const SymbolLabel* unpaged = nullptr;
    for (auto it = range.first; it != range.second; ++it) {
        if (!it->second.space)
            unpaged = &it->second;
        else if (space.kind != jnext::dbg::MemSpace::Kind::Cpu && *it->second.space == space)
            return it->second.name;
    }
    if (unpaged)
        return unpaged->name;
    return std::nullopt;
}

std::optional<std::pair<uint16_t, std::string>>
SymbolTable::nearest(uint16_t addr, const SymbolSpaceOf& space_of) const
{
    auto it = labels_.upper_bound(addr);
    while (it != labels_.begin()) {
        --it;
        const SymbolLabel& label = it->second;
        if (!label.space || (space_of && *label.space == space_of(it->first))) {
            // The name `lookup()` gives that address, so the two cannot disagree.
            const auto name = lookup(space_of ? space_of(it->first) : jnext::dbg::MemSpace::cpu(),
                                     it->first);
            return std::make_pair(it->first, name ? *name : label.name);
        }
    }
    return std::nullopt;
}

std::optional<SymbolNameKey> SymbolTable::find_name(const std::string& name) const
{
    auto it = name_to_key_.find(name);
    if (it != name_to_key_.end())
        return it->second;
    auto c = const_by_name_.find(name);
    if (c != const_by_name_.end())
        return SymbolNameKey{c->second, std::nullopt};
    return std::nullopt;
}

std::optional<uint16_t> SymbolTable::lookup_name(const std::string& name) const
{
    const auto key = find_name(name);
    if (!key)
        return std::nullopt;
    return key->addr;
}

void SymbolTable::clear()
{
    labels_.clear();
    name_to_key_.clear();
    const_by_name_.clear();
    loaded_file_.clear();
}
