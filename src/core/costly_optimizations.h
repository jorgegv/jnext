#pragma once

// GH #496 - the registry of COSTLY optimisations: host-CPU optimisations whose fixed cost, paid by every
// program once the optimisation is compiled in, is above the line a free optimisation may have (+0.2 % of
// per-frame host instructions on the non-polling `make bench` workloads, on any shipped compiler; see the
// developer guide, "Adding a costly optimisation"). A costly one is OFF unless the user names it:
// `--enable-costly-optimizations NAME[,NAME...]`.
//
// ONE TABLE drives everything user-facing: the names the flag accepts, the error for an unknown name, the
// `--help` list, the man page check (every row has an item carrying its summary / cost / benefits lines
// verbatim, and every item names a row) and the startup log line. A row is added with: a CostlyOptId, a
// table row, one arm of the `switch` in Emulator::apply_costly_optimizations() (no `default`, so a row
// without an arm does not compile), its code, and its rows.
//
// The table is EMPTY today. The first optimisation of this kind, the raster-poll batch, measured free on the
// bench workloads and ships always on; the flag, its man page items and the user guide page land with the
// first optimisation that is really costly. What is here is the part that does not depend on that: the table
// type and the parser, which takes its table as an argument so that the rows that pin it do not need a member.
//
// Qt-free: main.cpp, the SDL-only and headless builds and the tests include it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace jnext {

enum class CostlyOptId : uint8_t {
    Count   // a switch with no default must name every id above this line
};

struct CostlyOpt {
    CostlyOptId id;       // the switch it drives (see Emulator::apply_costly_optimizations)
    const char* name;     // stable, lower-case [a-z0-9-]+; the flag's word; never renamed once shipped
    const char* summary;  // one line: what it speeds up; --help prints it, the man page item starts with it verbatim
    const char* cost;     // one line: the measured fixed cost and who pays it once named
    const char* benefits; // one line: which programs gain, and where it does nothing
};

inline constexpr std::array<CostlyOpt, 0> COSTLY_OPTS{};

// "a,b" -> mask of (1u << id). Blanks around a name are trimmed and duplicates are allowed. An empty value, an
// empty element or an unknown name is an error: `err` names it and lists every valid name of the table.
inline bool parse_costly_optimizations(const char* list, uint32_t& mask, std::string& err,
                                       const CostlyOpt* table, std::size_t n) {
    mask = 0;
    err.clear();
    auto valid_names = [&]() {
        std::string v;
        for (std::size_t i = 0; i < n; ++i) v += std::string(i ? ", " : "") + table[i].name;
        return v.empty() ? std::string("(none)") : v;
    };
    if (!list || !*list) {
        err = "--enable-costly-optimizations needs at least one name; valid names: " + valid_names();
        return false;
    }
    const char* p = list;
    for (;;) {
        const char* end = std::strchr(p, ',');
        std::string name(p, end ? static_cast<std::size_t>(end - p) : std::strlen(p));
        const std::size_t b = name.find_first_not_of(" \t");
        const std::size_t e = name.find_last_not_of(" \t");
        name = b == std::string::npos ? std::string() : name.substr(b, e - b + 1);
        if (name.empty()) {
            err = "--enable-costly-optimizations: empty name in '" + std::string(list) + "'; valid names: " + valid_names();
            return false;
        }
        bool found = false;
        for (std::size_t i = 0; i < n && !found; ++i) {
            if (name == table[i].name) {
                mask |= 1u << static_cast<unsigned>(table[i].id);
                found = true;
            }
        }
        if (!found) {
            err = "--enable-costly-optimizations: unknown optimisation '" + name + "'; valid names: " + valid_names();
            return false;
        }
        if (!end) return true;
        p = end + 1;
    }
}

inline bool parse_costly_optimizations(const char* list, uint32_t& mask, std::string& err) {
    return parse_costly_optimizations(list, mask, err, COSTLY_OPTS.data(), COSTLY_OPTS.size());
}

}  // namespace jnext
