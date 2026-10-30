#include "debug/z88dk_source_map.h"

#include "debug/source_file.h"
#include "debug/source_map.h"
#include "debug/z88dk_space.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <exception>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using jnext::dbg::FetchSource;
using jnext::dbg::MemSpace;

// DeZog's MAX_C_LINE_SIZE (design 9.1).
constexpr uint32_t kMaxLineSize = 0x400;

SourceMapLoadResult failed(std::string error)
{
    SourceMapLoadResult result;
    result.error = std::move(error);
    return result;
}

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool is_line_record(std::string_view name)
{
    return starts_with(name, "__C_LINE_") || starts_with(name, "__ASM_LINE_");
}

uint32_t space_key(FetchSource s)
{
    return (static_cast<uint32_t>(s.kind) << 16) | s.index;
}

std::string hex(uint64_t value)
{
    char buf[24];
    std::snprintf(buf, sizeof buf, "%llX", static_cast<unsigned long long>(value));
    return buf;
}

// The three location forms of design 2.3: `file:line`,
// `file::func::level::block:line` (sccz80, 80cc) and sdcc's
// `file::x::<level><TAB>:line`, where the TAB marks a function it did not give.
struct Location {
    std::string file;
    std::string func;
    bool func_known = false;
    int line = 0;
};

std::optional<Location> parse_location(std::string_view text)
{
    const size_t colon = text.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string_view digits = text.substr(colon + 1);
    Location out;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(),
                                              out.line, 10);
    if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size()
        || out.line < 1)
        return std::nullopt;

    std::string_view prefix = text.substr(0, colon);
    bool tab = false;
    if (!prefix.empty() && prefix.back() == '\t') {
        tab = true;
        prefix.remove_suffix(1);
    }
    const size_t scope = prefix.find("::");
    if (scope == std::string_view::npos) {
        out.file = std::string(prefix);
    } else {
        out.file = std::string(prefix.substr(0, scope));
        const std::string_view rest = prefix.substr(scope + 2);
        const std::string_view func = rest.substr(0, rest.find("::"));
        out.func = std::string(func);
        out.func_known = !tab && !func.empty();
    }
    if (out.file.empty()) return std::nullopt;
    return out;
}

bool is_data_section(std::string_view section)
{
    std::string lower(section.substr(0, 4));
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return starts_with(lower, "data") || starts_with(lower, "bss") || starts_with(lower, "smc");
}

// A line of the map, as the first pass keeps it.
struct RawLine {
    int map_line = 0;
    bool malformed = false;       // named like a line record, but not one
    Z88dkMapLine parsed;
};

// A line record, placed.
struct Record {
    size_t order = 0;             // position among the placed records
    uint64_t value = 0;
    std::string module;
    std::string section;
    Location loc;
    FetchSource space;
    uint16_t lo = 0;
};

// A `; addr` symbol that is not a debug record: it bounds an extent (another
// module's code starts there) and marks a function entry.
struct Symbol {
    std::string name;
    std::string module;
    FetchSource space;
    uint16_t addr = 0;
    std::optional<Location> loc;
};

struct Bin {
    std::string group;
    uint64_t head = 0;
    std::vector<uint8_t> bytes;
};

} // namespace

SourceMapLoadResult load_z88dk_source_map(SourceMap& map, const std::string& path,
                                          const SymbolSpaceOf& space_of)
{
    if (std::string why = source_file_problem(path); !why.empty()) return failed(why);
    std::ifstream input(path);
    if (!input.is_open()) return failed("could not open z88dk map file");

    // Pass 1: the whole file. A section's head may come after its records (the
    // map is sorted by name), so nothing is placed before it has all been read.
    std::vector<RawLine> raws;
    std::vector<Z88dkMapLine> symbols;
    std::map<std::string, uint64_t> bounds;   // const __<group>_head / _tail, first wins
    std::string text;
    int map_line = 0;
    while (std::getline(input, text)) {
        ++map_line;
        const size_t first = text.find_first_not_of(" \t");
        const std::string_view name_start = first == std::string::npos
            ? std::string_view{} : std::string_view(text).substr(first);
        const auto parsed = parse_z88dk_map_line(text);
        if (is_line_record(name_start)) {
            RawLine raw;
            raw.map_line = map_line;
            if (!parsed) raw.malformed = true;
            else if (parsed->is_addr) raw.parsed = *parsed;
            else continue;
            raws.push_back(std::move(raw));
            continue;
        }
        if (!parsed) continue;
        if (parsed->is_const) {
            if (starts_with(parsed->name, "__")
                && (parsed->name.size() > 7)
                && (parsed->name.compare(parsed->name.size() - 5, 5, "_head") == 0
                    || parsed->name.compare(parsed->name.size() - 5, 5, "_tail") == 0))
                bounds.emplace(parsed->name, parsed->value);
        } else if (parsed->is_addr && !starts_with(parsed->name, "__CDBINFO__")) {
            symbols.push_back(*parsed);
        }
    }

    const auto page_head = [&](int n) -> std::optional<uint16_t> {
        const auto it = bounds.find("__PAGE_" + std::to_string(n) + "_head");
        if (it == bounds.end() || it->second > 0xFFFF) return std::nullopt;
        return static_cast<uint16_t>(it->second);
    };

    // Pass 2: place every line record, in file order, so the first refusal is
    // the first bad line.
    std::vector<Record> records;
    for (const RawLine& raw : raws) {
        const std::string where = "line " + std::to_string(raw.map_line) + ": ";
        if (raw.malformed) return failed(where + "malformed line record");
        const Z88dkMapLine& m = raw.parsed;
        if (m.section.empty()) continue;   // no code (G2)
        if (m.value > 0xFFFFFF)
            return failed(where + "address $" + hex(m.value) + " does not fit 24 bits");
        const auto loc = parse_location(m.location);
        if (!loc) return failed(where + "invalid location '" + m.location + "'");
        const Z88dkSpace placed = z88dk_section_space(m.section, static_cast<uint32_t>(m.value),
                                                      page_head);
        Record r;
        r.value = m.value;
        r.module = m.module;
        r.section = m.section;
        r.loc = *loc;
        r.lo = placed.addr16;
        switch (placed.kind) {
        case Z88dkSpace::Kind::Paged:
            r.space = placed.space;
            break;
        case Z88dkSpace::Kind::BindAtLoad:
            r.space = space_of(placed.addr16);
            if (r.space.kind != MemSpace::Kind::Page)
                return failed(where + "address $" + hex(placed.addr16) + " of section '"
                              + m.section + "' is not in a RAM page");
            break;
        case Z88dkSpace::Kind::Unpaged:
        case Z88dkSpace::Kind::Refused:
            return failed(where + placed.why);
        }
        r.order = records.size();
        records.push_back(std::move(r));
    }
    if (records.empty()) {
        SourceMapLoadResult none;
        none.count = 0;
        return none;
    }

    // The symbols that bound an extent or mark a function entry, in their space.
    std::map<uint32_t, std::vector<Symbol>> symbols_by_space;
    for (const Z88dkMapLine& s : symbols) {
        if (s.section.empty() || s.value > 0xFFFFFF) continue;
        const Z88dkSpace placed = z88dk_section_space(s.section, static_cast<uint32_t>(s.value),
                                                      page_head);
        Symbol out;
        out.name = s.name;
        out.module = s.module;
        out.addr = placed.addr16;
        out.loc = parse_location(s.location);
        if (placed.kind == Z88dkSpace::Kind::Paged) out.space = placed.space;
        else if (placed.kind == Z88dkSpace::Kind::BindAtLoad) out.space = space_of(placed.addr16);
        else continue;
        symbols_by_space[space_key(out.space)].push_back(std::move(out));
    }
    for (auto& [key, list] : symbols_by_space)
        std::stable_sort(list.begin(), list.end(),
                         [](const Symbol& a, const Symbol& b) { return a.addr < b.addr; });

    std::map<uint32_t, std::set<uint16_t>> starts;
    for (const Record& r : records) starts[space_key(r.space)].insert(r.lo);

    // Several lines at one address: the highest line owns the range.
    std::map<std::pair<uint32_t, uint16_t>, std::vector<size_t>> groups;
    for (const Record& r : records) groups[{space_key(r.space), r.lo}].push_back(r.order);

    const auto later_function_entry = [&](const Record& lower) {
        const auto it = symbols_by_space.find(space_key(lower.space));
        if (it == symbols_by_space.end()) return false;
        return std::any_of(it->second.begin(), it->second.end(), [&](const Symbol& s) {
            return s.addr == lower.lo && s.module == lower.module && s.name.size() > 0
                && s.name[0] == '_' && s.loc && s.loc->file == lower.loc.file
                && s.loc->line > lower.loc.line;
        });
    };

    std::vector<size_t> owners;                       // indices into records
    std::vector<std::pair<size_t, size_t>> alias_of;  // (lower record, owner record)
    for (const auto& [key, members] : groups) {
        size_t owner = members.front();
        for (const size_t i : members)
            if (records[i].loc.line >= records[owner].loc.line) owner = i;
        owners.push_back(owner);
        std::set<std::pair<std::string, int>> seen{{records[owner].loc.file,
                                                    records[owner].loc.line}};
        for (const size_t i : members) {
            const Record& lower = records[i];
            if (!seen.insert({lower.loc.file, lower.loc.line}).second) continue;   // duplicate
            const Record& top = records[owner];
            const bool same_function = lower.loc.func_known && top.loc.func_known
                ? lower.loc.func == top.loc.func
                : !later_function_entry(lower);
            if (lower.module == top.module && same_function) alias_of.emplace_back(i, owner);
        }
    }
    std::sort(owners.begin(), owners.end());

    const auto extent = [&](const Record& r) {
        uint32_t hi = std::min<uint32_t>(r.lo + kMaxLineSize, (r.lo & 0xE000u) + 0x2000u);
        const auto& s = starts[space_key(r.space)];
        if (const auto next = s.upper_bound(r.lo); next != s.end()) hi = std::min<uint32_t>(hi, *next);
        if (const auto it = symbols_by_space.find(space_key(r.space)); it != symbols_by_space.end()) {
            const auto& list = it->second;
            auto sym = std::upper_bound(list.begin(), list.end(), r.lo,
                                        [](uint16_t a, const Symbol& b) { return a < b.addr; });
            for (; sym != list.end() && sym->addr < hi; ++sym)
                if (sym->module != r.module) { hi = sym->addr; break; }
        }
        if (const auto tail = bounds.find("__" + r.section + "_tail");
            tail != bounds.end() && tail->second > r.value
            && tail->second - r.value < 0x10000)
            hi = std::min<uint32_t>(hi, r.lo + static_cast<uint32_t>(tail->second - r.value));
        return hi;
    };

    std::vector<SourceRange> ranges;
    std::map<size_t, size_t> range_of;   // record -> index in ranges
    for (const size_t i : owners) {
        const Record& r = records[i];
        range_of[i] = ranges.size();
        ranges.push_back({r.space, r.lo, extent(r), {r.loc.file, r.loc.line, 0}});
    }
    std::vector<SourceAlias> aliases;
    for (const auto& [lower, owner] : alias_of)
        aliases.push_back({records[lower].loc.file, records[lower].loc.line, range_of[owner]});

    // Identity: the section binaries beside the map.
    std::vector<Bin> bins;
    const fs::path map_path(path);
    const std::string stem = map_path.stem().string();
    for (const auto& [name, head] : bounds) {
        if (!starts_with(name, "__") || name.compare(name.size() - 5, 5, "_head") != 0) continue;
        const std::string group = name.substr(2, name.size() - 7);
        // z88dk's groups are upper case (CODE, PAGE_20, BANK_10_H): a lower-case head
        // (__code_compiler_head) is a section, and on a case-insensitive file system
        // would name the same file as its group.
        if (group.empty() || std::any_of(group.begin(), group.end(),
                                         [](unsigned char c) { return std::islower(c) != 0; }))
            continue;
        const fs::path bin_path = map_path.parent_path() / (stem + "_" + group + ".bin");
        std::error_code ec;
        if (!fs::exists(bin_path, ec)) continue;
        if (std::string why = source_file_problem(bin_path.string()); !why.empty())
            return failed("section binary '" + bin_path.string() + "': " + why);
        std::ifstream in(bin_path, std::ios::binary);
        if (!in.is_open()) return failed("could not open section binary '" + bin_path.string() + "'");
        Bin bin;
        bin.group = group;
        bin.head = head;
        // libstdc++ throws std::ios_base::failure from here when the read itself fails
        // (an I/O error on a regular file). No exception leaves the adapter: the half is
        // refused with a reason, like any other unreadable file.
        try {
            bin.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        } catch (const std::exception&) {
            return failed("could not read section binary '" + bin_path.string() + "'");
        }
        bins.push_back(std::move(bin));
    }
    std::optional<SourceProgramIdentity> identity;
    int unchecked = 0;
    if (!bins.empty()) {
        SourceProgramIdentity id;
        for (size_t k = 0; k < owners.size(); ++k) {
            const Record& r = records[owners[k]];
            if (is_data_section(r.section)) continue;
            const uint32_t length = ranges[k].hi_excl - ranges[k].lo;
            bool covered = false;
            for (const Bin& bin : bins) {
                if (r.value < bin.head || r.value + length > bin.head + bin.bytes.size()) continue;
                // The binary's group must put both ends of the range in the range's space.
                const auto group_space = [&](uint64_t value) {
                    const Z88dkSpace g = z88dk_section_space(bin.group,
                                                             static_cast<uint32_t>(value), page_head);
                    if (g.kind == Z88dkSpace::Kind::Paged) return g.space;
                    if (g.kind == Z88dkSpace::Kind::BindAtLoad) return space_of(g.addr16);
                    return MemSpace::floating();
                };
                if (space_key(group_space(r.value)) != space_key(r.space)
                    || space_key(group_space(r.value + length - 1)) != space_key(r.space))
                    continue;
                const auto begin = bin.bytes.begin() + static_cast<std::ptrdiff_t>(r.value - bin.head);
                id.bytes.push_back({r.space, r.lo,
                                    std::vector<uint8_t>(begin, begin + length)});
                covered = true;
                break;
            }
            if (!covered) ++unchecked;
        }
        if (!id.bytes.empty()) identity = std::move(id);
    }

    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    const std::string loaded = ec ? path : absolute.lexically_normal().string();
    SourceMapLoadResult result;
    result.count = static_cast<int>(ranges.size());
    result.unchecked = unchecked;
    map.replace(std::move(ranges), std::move(aliases), std::move(identity), loaded, "z88dk");
    return result;
}
