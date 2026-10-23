#include "debug/sld_loader.h"

#include "debug/source_file.h"
#include "debug/source_map.h"
#include "debug/symbol_table.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string_view> split(std::string_view text, char delimiter)
{
    std::vector<std::string_view> fields;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(delimiter, start);
        fields.push_back(text.substr(start, end - start));
        if (end == std::string_view::npos) return fields;
        start = end + 1;
    }
}

template<typename T>
std::optional<T> decimal(std::string_view text)
{
    T value{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, 10);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

bool parse_position(std::string_view text, int& line, int& column)
{
    const auto fields = split(text, ':');
    if (fields.empty() || fields.size() > 3) return false;
    const auto parsed_line = decimal<int>(fields[0]);
    if (!parsed_line || *parsed_line < 1) return false;
    line = *parsed_line;
    column = 0;
    for (size_t i = 1; i < fields.size(); ++i) {
        const auto value = decimal<int>(fields[i]);
        if (!value || *value < 1) return false;
        if (i == 1) column = *value;
    }
    return true;
}

bool parse_device(std::string_view text, int& page_count, std::string& error)
{
    constexpr std::string_view SIZE = "pages.size:";
    constexpr std::string_view COUNT = ",pages.count:";
    constexpr std::string_view SLOTS = ",slots.count:";
    constexpr std::string_view ADDRESSES = ",slots.adr:";

    if (text.substr(0, SIZE.size()) != SIZE) {
        error = "malformed SLD device model";
        return false;
    }
    const size_t count_pos = text.find(COUNT, SIZE.size());
    const size_t slots_pos = text.find(SLOTS, count_pos == std::string_view::npos
                                               ? 0 : count_pos + COUNT.size());
    const size_t addresses_pos = text.find(
        ADDRESSES, slots_pos == std::string_view::npos ? 0 : slots_pos + SLOTS.size());
    if (count_pos == std::string_view::npos || slots_pos == std::string_view::npos
        || addresses_pos == std::string_view::npos) {
        error = "malformed SLD device model";
        return false;
    }

    const auto page_size = decimal<int>(text.substr(
        SIZE.size(), count_pos - SIZE.size()));
    const auto pages = decimal<int>(text.substr(
        count_pos + COUNT.size(), slots_pos - count_pos - COUNT.size()));
    const auto slot_count = decimal<int>(text.substr(
        slots_pos + SLOTS.size(), addresses_pos - slots_pos - SLOTS.size()));
    if (!page_size || *page_size != 8192) {
        error = "SLD device pages must be 8192 bytes";
        return false;
    }
    // The Next's 224 8K RAM pages: a page above them cannot be RAM, and 0xFF
    // is NOT_RAM_PAGE (debug/ram_page.h).
    if (!pages || *pages < 1 || *pages > 224 || !slot_count || *slot_count < 1) {
        error = "invalid SLD device dimensions";
        return false;
    }

    std::vector<int> slots;
    for (const auto field : split(text.substr(addresses_pos + ADDRESSES.size()), ',')) {
        const auto address = decimal<int>(field);
        if (!address || *address < 0 || *address > 0xFFFF) {
            error = "invalid SLD device slot address";
            return false;
        }
        slots.push_back(*address);
    }
    if (static_cast<int>(slots.size()) != *slot_count
        || !std::is_sorted(slots.begin(), slots.end())
        || std::adjacent_find(slots.begin(), slots.end()) != slots.end()) {
        error = "invalid SLD device slots";
        return false;
    }
    for (size_t i = 0; i < slots.size(); ++i) {
        const int address = slots[i];
        if (address + *page_size > 0x10000) {
            error = "SLD device slot exceeds logical memory";
            return false;
        }
        if (i > 0 && slots[i - 1] + *page_size > address) {
            error = "SLD device slots overlap";
            return false;
        }
    }
    page_count = *pages;
    return true;
}

bool valid_sha256(const std::string& value)
{
    return value.size() == 64
        && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return std::isdigit(c) || (c >= 'a' && c <= 'f');
           });
}

SourceMapLoadResult failed(std::string error)
{
    return {-1, std::move(error)};
}

bool read_line(std::istream& input, std::string& line)
{
    if (!std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

} // namespace

namespace {

struct PendingLabel {
    std::string name;
    uint16_t address;
    std::optional<uint16_t> page;   // nullopt: unpaged (SLD page -1)
};

// The label half of one `L` record (design 10.1): `module,main,local[,traits]`
// named `module.main.local` as sjasmplus --sym spells it. Returns false when the
// record names nothing (a module marker, a bad number, a value that does not fit
// 16 bits or a page the device does not have); a `+struct_def` / `+sizeof` value
// goes to `values` (name only: an offset or a size names no address).
bool parse_label(const std::vector<std::string_view>& fields, int page_count,
                 std::vector<PendingLabel>& labels,
                 std::vector<std::pair<std::string, uint16_t>>& values)
{
    const auto data = split(fields[7], ',');
    if (data.size() < 3 || data[1].empty()) return false;
    std::string name;
    if (!data[0].empty()) name = std::string(data[0]) + ".";
    name += data[1];
    if (!data[2].empty()) name += "." + std::string(data[2]);

    const auto page = decimal<int>(fields[4]);
    const auto value = decimal<int>(fields[5]);
    if (!page || !value || *value < 0 || *value > 0xFFFF) return false;
    for (size_t i = 3; i < data.size(); ++i) {
        if (data[i] == "+struct_def" || data[i] == "+sizeof") {
            values.emplace_back(std::move(name), static_cast<uint16_t>(*value));
            return false;
        }
    }
    if (*page == -1) {
        labels.push_back({std::move(name), static_cast<uint16_t>(*value), std::nullopt});
        return true;
    }
    if (*page < 0 || *page >= page_count || *page > 0xFF) return false;
    labels.push_back({std::move(name), static_cast<uint16_t>(*value),
                      static_cast<uint16_t>(*page)});
    return true;
}

} // namespace

SourceMapLoadResult load_sld(SourceMap& map, const std::string& path, SymbolTable* labels)
{
    if (std::string why = source_file_problem(path); !why.empty()) return failed(why);
    std::ifstream input(path);
    if (!input.is_open()) return failed("could not open SLD file");

    std::string line;
    if (!read_line(input, line) || line != "|SLD.data.version|1")
        return failed("missing SLD.data.version 1 header");

    std::optional<std::string> program_name;
    std::optional<std::string> program_sha256;
    std::optional<uint32_t> program_org;
    std::optional<uint32_t> program_size;
    std::vector<SourceRange> ranges;
    int trace_count = 0;
    int device_count = 0;
    int page_count = 0;
    int line_number = 1;
    bool has_identity_metadata = false;
    // The source half's first refusal. Without a label table it ends the load
    // at once; with one the scan goes on, because the labels are a transaction
    // of their own.
    std::string source_error;
    std::vector<PendingLabel> pending;
    std::vector<std::pair<std::string, uint16_t>> values;
    int label_count = 0;

    while (read_line(input, line)) {
        ++line_number;
        if (line.compare(0, 2, "||") == 0) {
            const size_t colon = line.find(':', 2);
            if (colon == std::string::npos) continue;
            const std::string key = line.substr(2, colon - 2);
            const std::string value = line.substr(colon + 1);
            if (key == "program.name") {
                has_identity_metadata = true;
                program_name = value;
            } else if (key == "program.sha256") {
                has_identity_metadata = true;
                program_sha256 = value;
            } else if (key == "program.org") {
                has_identity_metadata = true;
                program_org = decimal<uint32_t>(value);
            } else if (key == "program.size") {
                has_identity_metadata = true;
                program_size = decimal<uint32_t>(value);
            }
            continue;
        }

        const auto fields = split(line, '|');
        if (fields.size() != 8) {
            if (!labels) return failed("malformed SLD record at line " + std::to_string(line_number));
            if (source_error.empty())
                source_error = "malformed SLD record at line " + std::to_string(line_number);
            continue;
        }
        if (fields[6] == "Z") {
            const auto page = decimal<int>(fields[4]);
            const auto value = decimal<int>(fields[5]);
            if (!page || !value || *page != -1 || *value != -1)
                return failed("invalid SLD device record at line "
                              + std::to_string(line_number));
            std::string error;
            if (!parse_device(fields[7], page_count, error)) return failed(error);
            if (++device_count != 1) return failed("SLD contains multiple device records");
            continue;
        }
        if (fields[6] == "L") {
            if (labels && parse_label(fields, device_count == 1 ? page_count : 0, pending, values))
                ++label_count;
            continue;
        }
        if (fields[6] != "T") continue;
        if (device_count != 1) {
            if (!labels) return failed("SLD trace appears before device metadata");
            if (source_error.empty()) source_error = "SLD trace appears before device metadata";
            continue;
        }
        if (fields[0].empty() || !fields[7].empty()) {
            if (!labels) return failed("invalid SLD trace at line " + std::to_string(line_number));
            if (source_error.empty())
                source_error = "invalid SLD trace at line " + std::to_string(line_number);
            continue;
        }

        int source_line = 0;
        int source_column = 0;
        const auto page = decimal<int>(fields[4]);
        const auto address = decimal<int>(fields[5]);
        if (!parse_position(fields[1], source_line, source_column)
            || !page || *page < 0 || *page >= page_count || *page > 0xFF
            || !address || *address < 0 || *address > 0xFFFF) {
            if (!labels) return failed("invalid SLD trace at line " + std::to_string(line_number));
            if (source_error.empty())
                source_error = "invalid SLD trace at line " + std::to_string(line_number);
            continue;
        }
        ranges.push_back({jnext::dbg::MemSpace::page(static_cast<uint16_t>(*page)),
                          static_cast<uint16_t>(*address), 0,
                          {std::string(fields[0]), source_line, source_column}});
        ++trace_count;
    }

    if (device_count != 1) return failed("SLD must contain one device record");
    if (trace_count == 0 && source_error.empty()) {
        if (!labels) return failed("SLD contains no source traces");
        source_error = "SLD contains no source traces";
    }

    const int identity_fields = static_cast<int>(program_name.has_value())
        + static_cast<int>(program_sha256.has_value())
        + static_cast<int>(program_org.has_value())
        + static_cast<int>(program_size.has_value());
    std::optional<SourceProgramIdentity> identity;
    if (has_identity_metadata) {
        if (program_sha256 && !valid_sha256(*program_sha256))
            return failed("invalid SHA-256 in SLD program identity");
        if (identity_fields != 4 || program_name->empty() || *program_org > 0xFFFF
            || *program_size > 0x10000 || *program_org + *program_size > 0x10000) {
            return failed("invalid or incomplete SLD program identity");
        }
        identity = SourceProgramIdentity{*program_sha256,
                                         static_cast<uint16_t>(*program_org),
                                         *program_size};
    }

    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    const std::string loaded = ec ? path : absolute.lexically_normal().string();
    if (labels) {
        labels->clear();
        for (const PendingLabel& l : pending)
            labels->add_label(l.name, l.address,
                              l.page ? std::optional<jnext::dbg::FetchSource>(
                                           jnext::dbg::MemSpace::page(*l.page))
                                     : std::nullopt);
        for (const auto& [name, value] : values)
            labels->add_value(name, value);
        labels->set_loaded_file(loaded);
    }
    if (!source_error.empty()) {
        SourceMapLoadResult result = failed(source_error);
        result.labels = label_count;
        return result;
    }

    // Absolute, so a source file named relative to the map can be found from
    // its directory whatever the working directory is later.
    // A record covers its instruction's bytes up to the next distinct start in
    // its page, never past the 8K page end (design 9.1; an SLD names 8K pages).
    // Records sharing a start share the extent.
    std::map<uint16_t, std::set<uint16_t>> starts;
    for (const SourceRange& r : ranges) starts[r.space.index].insert(r.lo);
    for (SourceRange& r : ranges) {
        const auto& page_starts = starts[r.space.index];
        const uint32_t page_end = (static_cast<uint32_t>(r.lo) & 0xE000u) + 0x2000u;
        const auto next = page_starts.upper_bound(r.lo);
        r.hi_excl = next == page_starts.end() ? page_end
                                              : std::min<uint32_t>(*next, page_end);
    }
    map.replace(std::move(ranges), {}, std::move(identity),
                loaded);
    SourceMapLoadResult result{trace_count, {}};
    if (labels) result.labels = label_count;
    return result;
}

SourceMapLoadResult load_sld_sidecar(SourceMap& map,
                                     const std::string& program_path,
                                     SymbolTable* labels)
{
    namespace fs = std::filesystem;
    const fs::path program(program_path);
    const fs::path stem = program.parent_path() / program.stem();
    for (const auto& suffix : {std::string(".sld"), std::string(".sld.txt")}) {
        const fs::path candidate(stem.string() + suffix);
        // Only a regular file is the sidecar (one that cannot be read is
        // refused with a reason): a FIFO would block the load on open. The
        // error_code overload: a broken or looping link is "not a sidecar",
        // never an exception.
        std::error_code ec;
        const auto st = fs::status(candidate, ec);
        if (!ec && fs::is_regular_file(st)) {
            SourceMapLoadResult result = load_sld(map, candidate.string(), labels);
            result.file = candidate.string();
            return result;
        }
    }
    return failed("no adjacent SLD sidecar");
}
