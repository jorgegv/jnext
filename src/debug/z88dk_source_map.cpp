#include "debug/z88dk_source_map.h"

#include "debug/source_file.h"
#include "debug/source_map.h"
#include "debug/z88dk_lis.h"
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

// `...:0`: sccz80 and 80cc write `C_LINE 0,"<file>::<func>::0::1"` on entering a file,
// which z80asm turns into a record of line 0. No source line is numbered 0.
bool is_line_zero(std::string_view location)
{
    const size_t colon = location.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= location.size()) return false;
    return location.find_first_not_of('0', colon + 1) == std::string_view::npos;
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
                                          const SymbolSpaceOf& space_of, Z88dkListings listings)
{
    if (std::string why = source_file_problem(path); !why.empty()) return failed(why);
    std::ifstream input(path);
    if (!input.is_open()) return failed("could not open z88dk map file");
    skip_utf8_bom(input);

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

    const auto page_head = [&](int n) { return z88dk_page_head(bounds, n); };

    // Pass 2: place every line record, in file order, so the first refusal is
    // the first bad line.
    std::vector<Record> records;
    for (const RawLine& raw : raws) {
        const std::string where = "line " + std::to_string(raw.map_line) + ": ";
        if (raw.malformed) return failed(where + "malformed line record");
        const Z88dkMapLine& m = raw.parsed;
        if (m.section.empty()) continue;   // no code (G2)
        if (is_line_zero(m.location)) continue;   // z80asm's file-entry marker, not a line (GH #348)
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
    const bool beside = listings == Z88dkListings::Beside;
    if (records.empty() && !beside) {
        SourceMapLoadResult none;
        none.count = 0;
        return none;
    }

    // The section binaries beside the map: the identity of the line ranges, and the
    // opcode check of a listing.
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
    // The binary that holds [value, value + length) of `space`: its group must put both
    // ends of the range in that space.
    const auto bin_for = [&](uint64_t value, uint32_t length, FetchSource space) -> const Bin* {
        for (const Bin& bin : bins) {
            if (value < bin.head || value + length > bin.head + bin.bytes.size()) continue;
            const auto group_space = [&](uint64_t v) {
                const Z88dkSpace g = z88dk_section_space(bin.group, static_cast<uint32_t>(v),
                                                         page_head);
                if (g.kind == Z88dkSpace::Kind::Paged) return g.space;
                if (g.kind == Z88dkSpace::Kind::BindAtLoad) return space_of(g.addr16);
                return MemSpace::floating();
            };
            if (space_key(group_space(value)) != space_key(space)
                || space_key(group_space(value + length - 1)) != space_key(space))
                continue;
            return &bin;
        }
        return nullptr;
    };

    // The listings (GH #348): each one a module's lines, or a note why not.
    std::vector<Z88dkListedRange> listed_ranges;
    std::vector<SourceAlias> listed_aliases;
    std::map<std::string, SourceText> listed_texts;
    std::set<std::string> listed_modules;
    std::vector<std::string> notes;
    int listings_used = 0;
    int checks = 0;
    int disagreements = 0;
    std::vector<std::string> examples;
    if (beside) {
        std::vector<Z88dkMapLine> line_records;
        for (const RawLine& raw : raws)
            if (!raw.malformed && !raw.parsed.section.empty() && !is_line_zero(raw.parsed.location))
                line_records.push_back(raw.parsed);
        Z88dkMapView view;
        view.symbols = &symbols;
        view.line_records = &line_records;
        view.bounds = &bounds;
        for (const Z88dkMapLine& s : symbols) if (!s.module.empty()) view.modules.insert(s.module);
        for (const Z88dkMapLine& s : line_records) if (!s.module.empty()) view.modules.insert(s.module);
        view.place = [&](const std::string& section, uint64_t value) {
            Z88dkPlacement out;
            const Z88dkSpace placed = z88dk_section_space(section, static_cast<uint32_t>(value),
                                                          page_head);
            out.lo = placed.addr16;
            switch (placed.kind) {
            case Z88dkSpace::Kind::Paged:
                out.space = placed.space;
                out.ok = true;
                break;
            case Z88dkSpace::Kind::BindAtLoad:
                out.space = space_of(placed.addr16);
                out.ok = out.space.kind == MemSpace::Kind::Page;
                if (!out.ok)
                    out.why = "address $" + hex(placed.addr16) + " of section '" + section
                        + "' is not in a RAM page";
                break;
            case Z88dkSpace::Kind::Unpaged:
            case Z88dkSpace::Kind::Refused:
                out.why = placed.why;
                break;
            }
            return out;
        };
        view.byte_at = [&](uint64_t value, FetchSource space) -> std::optional<uint8_t> {
            const Bin* bin = bin_for(value, 1, space);
            if (!bin) return std::nullopt;
            return bin->bytes[static_cast<size_t>(value - bin->head)];
        };

        // Candidates: the start-up code's `<stem>.lis`, then `<file>.lis` for every
        // source file the map's module symbols name (relative to the map).
        struct Candidate {
            fs::path path;
            std::string name;
            std::map<std::string, std::string> modules;   // declared module -> map module
            bool any = false;                // a listing of any module
        };
        std::vector<Candidate> candidates;
        std::map<std::string, size_t> seen;
        const auto add = [&](const std::string& rel, const std::string& module) {
            const fs::path p = fs::path(rel).is_absolute() ? fs::path(rel) : map_path.parent_path() / rel;
            const auto [it, added] = seen.emplace(p.lexically_normal().string(), candidates.size());
            if (added) candidates.push_back({p, rel, {}, false});
            if (module.empty()) {
                candidates[it->second].any = true;
                return;
            }
            // z80asm names a module after the path it was given (`game/util.c` is
            // `game_util_c` in the map), a listing after the file alone (`util_c`).
            std::string declared = fs::path(rel).stem().string();   // `util.c.lis` -> `util.c`
            declared = fs::path(declared).filename().string();
            for (char& c : declared)
                if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
            candidates[it->second].modules.emplace(module, module);
            candidates[it->second].modules.emplace(declared, module);
        };
        add(stem + ".lis", "");
        std::map<std::string, std::set<std::string>> named;   // file -> modules
        const auto name_files = [&](const std::vector<Z88dkMapLine>& lines) {
            for (const Z88dkMapLine& m : lines) {
                if (m.section.empty() || m.module.empty()) continue;
                if (const auto loc = parse_location(m.location)) named[loc->file].insert(m.module);
            }
        };
        name_files(symbols);
        name_files(line_records);
        for (const auto& [file, modules] : named)
            for (const std::string& module : modules) add(file + ".lis", module);

        for (const Candidate& c : candidates) {
            std::error_code ec;
            if (!fs::is_regular_file(c.path, ec)) continue;
            if (std::string why = source_file_problem(c.path.string()); !why.empty()) {
                notes.push_back(c.name + ": " + why);
                continue;
            }
            std::string why;
            const auto parsed = parse_z88dk_listing(c.path.string(), why);
            if (!parsed) {
                notes.push_back(c.name + ": " + why);
                continue;
            }
            Z88dkListingResult r = place_z88dk_listing(
                *parsed, c.name, c.any ? std::map<std::string, std::string>{} : c.modules, view);
            if (r.used && listed_modules.count(r.module)) {
                notes.push_back(c.name + ": module '" + r.module + "' already has a listing; ignored");
                continue;
            }
            for (std::string& n : r.notes) notes.push_back(std::move(n));
            if (!r.used) continue;
            listed_modules.insert(r.module);
            ++listings_used;
            checks += r.checks;
            disagreements += r.disagreements;
            for (std::string& e : r.disagree_examples)
                if (examples.size() < 3) examples.push_back(std::move(e));
            const size_t base = listed_ranges.size();
            for (Z88dkListedRange& lr : r.ranges) listed_ranges.push_back(std::move(lr));
            for (SourceAlias& a : r.aliases) {
                a.range += base;
                listed_aliases.push_back(std::move(a));
            }
            for (auto& [file, lines] : r.texts)
                listed_texts.emplace(file, SourceText{c.path.string(), std::move(lines)});
        }
        if (disagreements > 0) {
            std::string text = map_path.filename().string() + ": the listings disagree with the map at "
                + std::to_string(disagreements) + " of " + std::to_string(checks)
                + " line records (first: ";
            for (size_t i = 0; i < examples.size(); ++i) text += (i ? "; " : "") + examples[i];
            notes.push_back(text + "); the listings were used");
        }
    }

    // A module with a listing takes its lines from it.
    if (!listed_modules.empty()) {
        records.erase(std::remove_if(records.begin(), records.end(),
                                     [&](const Record& r) { return listed_modules.count(r.module) != 0; }),
                      records.end());
        for (size_t i = 0; i < records.size(); ++i) records[i].order = i;
    }
    if (records.empty() && listed_ranges.empty()) {
        SourceMapLoadResult none;
        none.count = 0;
        none.listing_notes = std::move(notes);
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

    // The listings' ranges follow the map's, in discovery order.
    std::vector<std::pair<uint64_t, const std::string*>> meta;   // (value, section) per range
    for (const size_t i : owners) meta.emplace_back(records[i].value, &records[i].section);
    {
        const size_t base = ranges.size();
        for (const Z88dkListedRange& lr : listed_ranges) {
            ranges.push_back(lr.range);
            meta.emplace_back(lr.value, &lr.section);
        }
        for (SourceAlias& a : listed_aliases) {
            a.range += base;
            aliases.push_back(std::move(a));
        }
    }

    // Identity: the section binaries beside the map.
    std::optional<SourceProgramIdentity> identity;
    int unchecked = 0;
    if (!bins.empty()) {
        SourceProgramIdentity id;
        for (size_t k = 0; k < ranges.size(); ++k) {
            if (is_data_section(*meta[k].second)) continue;
            const uint32_t length = ranges[k].hi_excl - ranges[k].lo;
            if (const Bin* bin = bin_for(meta[k].first, length, ranges[k].space)) {
                const auto begin = bin->bytes.begin()
                    + static_cast<std::ptrdiff_t>(meta[k].first - bin->head);
                id.bytes.push_back({ranges[k].space, ranges[k].lo,
                                    std::vector<uint8_t>(begin, begin + length)});
            } else {
                ++unchecked;
            }
        }
        if (!id.bytes.empty()) identity = std::move(id);
    }

    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    const std::string loaded = ec ? path : absolute.lexically_normal().string();
    SourceMapLoadResult result;
    result.count = static_cast<int>(ranges.size());
    result.unchecked = unchecked;
    result.listings = listings_used;
    result.listing_checks = checks;
    result.listing_disagreements = disagreements;
    result.listing_notes = std::move(notes);
    map.replace(std::move(ranges), std::move(aliases), std::move(identity), loaded, "z88dk",
                std::move(listed_texts));
    return result;
}
