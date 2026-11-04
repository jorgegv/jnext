#include "debug/z88dk_source_map.h"

#include "debug/cdb_record.h"
#include "debug/source_file.h"
#include "debug/source_map.h"
#include "debug/variable_store.h"
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
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
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
    bool tab = false;           // the TAB that marks a function sdcc did not give
    bool scope_given = false;   // `<func>::<level>::<block>` was there (GH #433)
    int level_raw = 0;          // sccz80, 80cc: the level; zsdcc: level * 10000 + sub
    int block = 0;
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
        out.tab = tab;
        // `level::block` after the function (GH #433): both numeric, else not given.
        const size_t after = rest.find("::");
        if (after != std::string_view::npos) {
            const std::string_view tail = rest.substr(after + 2);
            const size_t mid = tail.find("::");
            if (mid != std::string_view::npos) {
                const std::string_view lv = tail.substr(0, mid);
                const std::string_view bl = tail.substr(mid + 2);
                int level = 0, block = 0;
                const auto [e1, r1] = std::from_chars(lv.data(), lv.data() + lv.size(), level, 10);
                const auto [e2, r2] = std::from_chars(bl.data(), bl.data() + bl.size(), block, 10);
                if (!lv.empty() && !bl.empty() && r1 == std::errc{} && r2 == std::errc{}
                    && e1 == lv.data() + lv.size() && e2 == bl.data() + bl.size()) {
                    out.scope_given = true;
                    out.level_raw = level;
                    out.block = block;
                }
            }
        }
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


// ── The C variable store (GH #433) ──────────────────────────────────────────

using jnext::dbg::CdbKind;
using jnext::dbg::CdbRecord;
using jnext::dbg::CdbScope;
using jnext::dbg::CFunction;
using jnext::dbg::CRecord;
using jnext::dbg::CScope;
using jnext::dbg::CStorage;
using jnext::dbg::CVariable;

// A `__CDBINFO__` map line: the escaped record and the module column.
struct CdbLine {
    std::string name;
    std::string module;
};

// A map symbol placed in its space; the one rule `symbols_by_space` and the store share.
struct SymPlace {
    bool ok = false;
    bool bound = false;         // placed by rule 4 (bound to the RAM page mapped there at load)
    FetchSource space;
    uint16_t addr16 = 0;
    std::string why;            // !ok
};
using PlaceSymbol = std::function<SymPlace(const Z88dkMapLine&)>;

struct SymbolIndex {
    std::map<std::pair<std::string, std::string>, std::vector<const Z88dkMapLine*>> in_module;
    std::map<std::string, std::vector<const Z88dkMapLine*>> pub;
};

// The symbol a record joins to, or why not. Two or more matches are ambiguous, never first-wins.
struct Join {
    const Z88dkMapLine* sym = nullptr;
    std::string why;
};

Join pick(const std::vector<const Z88dkMapLine*>* found, const std::string& absent,
          const std::string& several)
{
    Join j;
    if (!found || found->empty()) j.why = absent;
    else if (found->size() > 1) j.why = several;
    else j.sym = found->front();
    return j;
}

// `S:F` and every local: the symbol of that name in the record's own module.
Join join_in_module(const SymbolIndex& idx, const std::string& module, const std::string& symbol)
{
    const auto it = idx.in_module.find({module, symbol});
    return pick(it == idx.in_module.end() ? nullptr : &it->second,
                "no symbol '" + symbol + "' in module '" + module + "'",
                "symbol '" + symbol + "' is defined more than once in module '" + module + "'");
}

// `S:G` and `F:G`: the PUBLIC symbol, whatever its module. The CDB line's module is the
// first emitter of the record (an `extern int g;` in another unit), not the definer.
Join join_public(const SymbolIndex& idx, const std::string& symbol)
{
    const auto it = idx.pub.find(symbol);
    return pick(it == idx.pub.end() ? nullptr : &it->second,
                "no public symbol '" + symbol + "'",
                "public symbol '" + symbol + "' is defined more than once");
}

CStorage storage_of(const Join& j, const PlaceSymbol& place)
{
    CStorage st;
    if (!j.sym) {
        st.why = j.why;
        return st;
    }
    st.symbol = j.sym->name;
    const SymPlace p = place(*j.sym);
    if (!p.ok) {
        st.why = p.why;
    } else if (p.bound && p.space.kind != MemSpace::Kind::Page) {
        st.why = "address $" + hex(p.addr16) + " of section '" + j.sym->section
            + "' is not in a RAM page";
    } else {
        st.resolved = true;
        st.at = {p.space, p.addr16};
    }
    return st;
}

// The storage symbol of an E-space variable (the writers: sccz80 declparse.c:1012, 80cc
// declparse.c:1099 name a static local `st_<fn>_<x>`; SDCC allocLocal() SDCCmem.c:876 names
// one `<fn>_<x>_<level * 10000 + sub>_<block>`).
Join join_variable(const SymbolIndex& idx, const CdbRecord& r, const std::string& module)
{
    switch (r.scope) {
    case CdbScope::Global:
        return join_public(idx, "_" + r.var.name);
    case CdbScope::Local:
        return join_in_module(idx, module,
            "_" + r.function + "_" + r.var.name + "_"
            + std::to_string(r.var.level * 10000 + r.var.sub) + "_" + std::to_string(r.var.block));
    default:
        return join_in_module(idx, module, "_" + r.var.name);
    }
}

// The function a local belongs to: the one record of that name in the local's own module.
int function_of(const std::vector<CFunction>& fns, const std::string& module, const std::string& name)
{
    int found = -1;
    for (size_t i = 0; i < fns.size(); ++i) {
        if (fns[i].name != name || fns[i].module != module) continue;
        if (found >= 0) return -1;   // two: not attributed
        found = static_cast<int>(i);
    }
    return found;
}

// sccz80 / 80cc static local `st_<fn>_<x>` (level > 0): the one function of the module whose
// name makes `st_<fn>_` a prefix. Several (`a` and `a_b`): not attributed.
int static_local_function(const std::vector<CFunction>& fns, const std::string& module,
                          const std::string& name, std::string* rest)
{
    int found = -1;
    for (size_t i = 0; i < fns.size(); ++i) {
        const std::string prefix = "st_" + fns[i].name + "_";
        if (fns[i].module != module || name.size() <= prefix.size() || !starts_with(name, prefix))
            continue;
        if (found >= 0) return -1;
        found = static_cast<int>(i);
        *rest = name.substr(prefix.size());
    }
    return found;
}

// A function's entry symbol as the map gives it: the VALUE and SECTION the extents and the
// scope tables compare (GH #434 F-A: never the space, which a function crossing an 8 KB slot
// changes half way).
struct FnEntry {
    const Z88dkMapLine* sym = nullptr;
};

// The extent of every function whose entry resolved (GH #434, plan 2.1.1): from the entry to the
// lowest map value above it, in the same section, of the entry of another function, an `addr`
// symbol or line record of ANOTHER module, or the section's `__<section>_tail`; split at the
// 8 KB logical boundaries, each piece placed by the same rule the symbols use.
void fill_extents(std::vector<CFunction>& fns, const std::vector<FnEntry>& entries,
                  const std::vector<Z88dkMapLine>& symbols, const std::vector<Record>& scope_records,
                  const std::map<std::string, uint64_t>& bounds, const PlaceSymbol& place)
{
    for (size_t i = 0; i < fns.size(); ++i) {
        CFunction& f = fns[i];
        if (!f.entry.resolved || !entries[i].sym) {
            f.extent_why = "its entry symbol is not resolved";
            continue;
        }
        const Z88dkMapLine& e = *entries[i].sym;
        uint64_t bound = UINT64_MAX;
        const auto consider = [&](uint64_t v) {
            if (v > e.value && v < bound) bound = v;
        };
        for (size_t j = 0; j < fns.size(); ++j)
            if (j != i && fns[j].entry.resolved && entries[j].sym && entries[j].sym->section == e.section)
                consider(entries[j].sym->value);
        for (const Z88dkMapLine& s : symbols)
            if (s.section == e.section && s.module != e.module) consider(s.value);
        for (const Record& r : scope_records)
            if (r.section == e.section && r.module != e.module) consider(r.value);
        if (const auto tail = bounds.find("__" + e.section + "_tail"); tail != bounds.end())
            consider(tail->second);
        if (bound == UINT64_MAX) {
            f.extent_why = "nothing bounds it above: no other function, no symbol or line of another "
                           "module and no __" + e.section + "_tail";
            continue;
        }
        Z88dkMapLine probe = e;
        for (uint64_t at = e.value; at < bound;) {
            probe.value = at;
            const SymPlace p = place(probe);
            if (!p.ok || (p.bound && p.space.kind != MemSpace::Kind::Page)) {
                f.extent_why = "address $" + hex(at) + " of section '" + e.section
                    + "' cannot be placed" + (p.why.empty() ? std::string() : ": " + p.why);
                break;
            }
            const uint32_t room = 0x2000u - (p.addr16 & 0x1FFFu);
            const uint32_t chunk = static_cast<uint32_t>(std::min<uint64_t>(bound - at, room));
            f.pieces.push_back({{p.space, p.addr16}, p.addr16 + chunk, static_cast<uint32_t>(at - e.value)});
            at += chunk;
        }
    }
}

// The scope tables (plan 2.5): the C_LINE records of the map's code sections, one owner per
// address (the highest line), each filed under the function of its module and section with the
// greatest entry VALUE at or below it (GH #434 F-A), with its offset from that entry.
void fill_scopes(std::vector<CFunction>& fns, const std::vector<FnEntry>& entries,
                 const std::vector<Record>& records)
{
    std::map<std::pair<uint32_t, uint16_t>, std::vector<const Record*>> by_address;
    for (const Record& r : records) by_address[{space_key(r.space), r.lo}].push_back(&r);

    const auto scope_key = [](const Record& r) {
        return std::make_tuple(r.module, r.loc.func, r.loc.tab, r.loc.scope_given,
                               r.loc.level_raw, r.loc.block);
    };
    for (const auto& [where, members] : by_address) {
        int top = 0;
        for (const Record* r : members) top = std::max(top, r->loc.line);
        std::vector<const Record*> owners;
        for (const Record* r : members)
            if (r->loc.line == top) owners.push_back(r);
        std::sort(owners.begin(), owners.end(),
                  [&](const Record* a, const Record* b) { return scope_key(*a) < scope_key(*b); });
        const Record& owner = *owners.front();
        const bool agree = std::all_of(owners.begin(), owners.end(), [&](const Record* r) {
            return scope_key(*r) == scope_key(owner);
        });

        CFunction* fn = nullptr;
        uint64_t fn_entry = 0;
        for (size_t i = 0; i < fns.size(); ++i) {
            CFunction& f = fns[i];
            if (!f.entry.resolved || !entries[i].sym || f.module != owner.module
                || entries[i].sym->section != owner.section || entries[i].sym->value > owner.value)
                continue;
            const uint64_t v = entries[i].sym->value;
            if (!fn || v > fn_entry || (v == fn_entry && f.text < fn->text)) {
                fn = &f;
                fn_entry = v;
            }
        }
        if (!fn) continue;

        CScope cs;
        cs.at = {owner.space, owner.lo};
        cs.line = top;
        cs.offset = static_cast<uint32_t>(owner.value - fn_entry);
        if (agree && owner.loc.scope_given) {
            // zcc's peephole rule writes zsdcc's C_LINE as `<file>::x::<level * 10000 + sub>::<block>`
            // (lib/sdcc/sdcc_opt.1:313): the function is the literal `x`. A TAB marks it, or the
            // function at the address is not itself named `x`.
            const bool zsdcc = owner.loc.func == "x" && (owner.loc.tab || fn->name != "x");
            if (zsdcc) {
                cs.known = true;
                cs.level = owner.loc.level_raw / 10000;
                cs.sub = owner.loc.level_raw % 10000;
                cs.block = owner.loc.block;
            } else if (owner.loc.func == fn->name) {
                cs.known = true;
                cs.level = owner.loc.level_raw;
                cs.block = owner.loc.block;
            }
        }
        fn->scopes.push_back(cs);
    }
    for (CFunction& f : fns)
        std::sort(f.scopes.begin(), f.scopes.end(),
                  [](const CScope& a, const CScope& b) { return a.offset < b.offset; });
}

// Which compiler wrote each function's records (GH #438, plan 1.2), from the map alone: the `M:` module records of the
// compilers differ. 80cc writes `M:<file>` into the module with the file name as `C_LINE` carries it (`M:./m.c` beside
// `./m.c::main::...`); zsdcc writes `M:<module>` against `C_LINE` file `<module>.c`, so it never matches; sccz80 writes
// no `M:` into the map at all. The name z80asm gives the module is NOT used: it prefixes `X` to a path that starts
// with `.`, `/` or a digit. A zsdcc `x`+TAB C_LINE record rules 80cc out.
void fill_compilers(std::vector<CFunction>& fns, const std::vector<CRecord>& modules,
                    const std::vector<Record>& scope_records)
{
    std::map<std::string, std::set<std::string>> m_names, line_files;
    std::set<std::string> zsdcc_form;
    for (const CRecord& m : modules) m_names[m.module].insert(m.record.var.name);
    for (const Record& r : scope_records) {
        line_files[r.module].insert(r.loc.file);
        if (r.loc.scope_given && r.loc.func == "x" && r.loc.tab) zsdcc_form.insert(r.module);
    }
    for (CFunction& f : fns) {
        const auto m = m_names.find(f.module);
        if (m == m_names.end()) {
            f.compiler = jnext::dbg::CCompiler::Sccz80;
            continue;
        }
        bool match = false;
        for (const std::string& file : line_files[f.module])
            if (m->second.count(file)) match = true;
        f.compiler = match && !zsdcc_form.count(f.module) ? jnext::dbg::CCompiler::Cc80 : jnext::dbg::CCompiler::Zsdcc;
    }
}

// (module, function) of every function a used listing names, and whether it is `__z88dk_fastcall`.
using ListedFunctions = std::map<std::pair<std::string, std::string>, bool>;

std::shared_ptr<const jnext::dbg::VariableStore> build_variable_store(
    const std::vector<CdbLine>& cdb_lines, const std::vector<Z88dkMapLine>& symbols,
    const std::vector<Record>& scope_records, const PlaceSymbol& place,
    const std::map<std::string, uint64_t>& bounds, const ListedFunctions& listed,
    const std::string& float_library)
{
    SymbolIndex idx;
    for (const Z88dkMapLine& s : symbols) {
        idx.in_module[{s.module, s.name}].push_back(&s);
        if (s.is_public) idx.pub[s.name].push_back(&s);
    }

    struct Item {
        CdbRecord record;
        std::string text;
        std::string module;
    };
    std::vector<Item> functions, variables;
    std::vector<CRecord> types, modules;
    std::vector<std::string> refused;
    for (const CdbLine& line : cdb_lines) {
        const auto un = jnext::dbg::cdb_unescape(line.name.substr(jnext::dbg::kCdbPrefix.size()));
        if (!un.ok()) {
            refused.push_back(line.name + ": " + un.error);
            continue;
        }
        auto decoded = jnext::dbg::cdb_decode(un.text);
        if (!decoded.ok()) {
            refused.push_back(line.name + ": " + decoded.error);
            continue;
        }
        switch (decoded.record.kind) {
        case CdbKind::Function:
            functions.push_back({std::move(decoded.record), un.text, line.module});
            break;
        case CdbKind::Variable:
            variables.push_back({std::move(decoded.record), un.text, line.module});
            break;
        case CdbKind::Type:
            types.push_back({std::move(decoded.record), un.text, line.module});
            break;
        case CdbKind::Module:
            modules.push_back({std::move(decoded.record), un.text, line.module});
            break;
        }
    }

    std::vector<CFunction> fns;
    std::vector<FnEntry> entries;
    for (Item& it : functions) {
        CFunction f;
        f.record = std::move(it.record);
        f.text = std::move(it.text);
        f.name = f.record.var.name;
        Join j;
        if (f.record.scope == CdbScope::Global) j = join_public(idx, "_" + f.name);
        else if (f.record.scope == CdbScope::File) j = join_in_module(idx, it.module, "_" + f.name);
        else j.why = "a function record without a global or file scope";
        f.entry = storage_of(j, place);
        f.module = j.sym ? j.sym->module : it.module;
        if (const auto l = listed.find({f.module, f.name}); l != listed.end())
            f.conv = l->second ? jnext::dbg::CConv::ListedFastcall : jnext::dbg::CConv::Listed;
        entries.push_back({j.sym});
        fns.push_back(std::move(f));
    }

    std::vector<CVariable> vars;
    for (Item& it : variables) {
        CVariable v;
        v.record = std::move(it.record);
        v.text = std::move(it.text);
        v.module = it.module;
        v.c_name = v.record.var.name;
        const CdbRecord& r = v.record;
        if (r.scope == CdbScope::Local) {
            v.function = function_of(fns, v.module, r.function);
        } else if (r.scope == CdbScope::File && r.var.level > 0 && starts_with(r.var.name, "st_")) {
            std::string rest;
            v.function = static_local_function(fns, v.module, r.var.name, &rest);
            if (v.function >= 0) v.c_name = rest;
        }
        if (r.var.space == 'E' && r.hidden != jnext::dbg::CdbHidden::EmptyType)
            v.storage = storage_of(join_variable(idx, r, v.module), place);
        vars.push_back(std::move(v));
    }

    fill_extents(fns, entries, symbols, scope_records, bounds, place);
    fill_scopes(fns, entries, scope_records);
    fill_compilers(fns, modules, scope_records);

    // An `{-1}` array record is the twin of a sized one (an array sized by its initialiser: sccz80 and 80cc write the
    // unsized record first). With no sized twin of the same scope, name and function it is the variable's only record: it
    // is listed, and refused with its reason (GH #438, plan 2.6). The decoder still classes every `{-1}` array as hidden.
    std::vector<CVariable*> unsized_alone;   // decided first, applied after: unhiding one must not make it the twin of the next
    for (CVariable& v : vars) {
        if (v.record.hidden != jnext::dbg::CdbHidden::IncompleteArray) continue;
        const bool twin = std::any_of(vars.begin(), vars.end(), [&](const CVariable& o) {
            return &o != &v && o.record.hidden == jnext::dbg::CdbHidden::None && o.record.scope == v.record.scope &&
                   o.record.scope_name == v.record.scope_name && o.record.function == v.record.function &&
                   o.record.var.name == v.record.var.name && o.module == v.module;
        });
        if (!twin) unsized_alone.push_back(&v);
    }
    for (CVariable* v : unsized_alone) v->record.hidden = jnext::dbg::CdbHidden::None;

    jnext::dbg::CFrameHelpers helpers;
    helpers.framepointer = storage_of(join_public(idx, "__debug_framepointer"), place);
    helpers.push_frame = storage_of(join_public(idx, "l_debug_push_frame"), place);
    helpers.pop_frame = storage_of(join_public(idx, "l_debug_pop_frame"), place);

    // OD-13: what a map without variables looks like, for the facade's status.
    jnext::dbg::CNoRecordFacts facts;
    for (const Record& r : scope_records) {
        if (!r.loc.scope_given) continue;
        facts.c_line_records = true;
        if (r.loc.func == "x" && r.loc.tab) facts.zsdcc_form = true;
    }
    return std::make_shared<const jnext::dbg::VariableStore>(std::move(fns), std::move(vars),
                                                           std::move(types), std::move(modules),
                                                           std::move(refused), std::move(helpers), facts,
                                                           float_library);
}

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
    std::vector<CdbLine> cdb_lines;           // the __CDBINFO__ records, for the variable store
    std::map<std::string, uint64_t> bounds;   // const __<group>_head / _tail, first wins
    std::string float_library;                // the first non-zero const __code_fp_<lib>_size
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
        if (parsed->is_const && starts_with(parsed->name, jnext::dbg::kCdbPrefix)) {
            cdb_lines.push_back({parsed->name, parsed->module});
            continue;
        }
        if (parsed->is_const) {
            // the float library the program links: `__code_fp_<lib>_size` is non-zero for it alone (GH #438, z88dk#3236)
            if (float_library.empty() && starts_with(parsed->name, "__code_fp_") && parsed->value != 0
                && parsed->name.size() > 15 && parsed->name.compare(parsed->name.size() - 5, 5, "_size") == 0)
                float_library = parsed->name.substr(10, parsed->name.size() - 15);
            if (parsed->name == "startupoffset"   // a dot command's (design 9.4 rule 5)
                || (starts_with(parsed->name, "__")
                    && (parsed->name.size() > 7)
                    && (parsed->name.compare(parsed->name.size() - 5, 5, "_head") == 0
                        || parsed->name.compare(parsed->name.size() - 5, 5, "_tail") == 0)))
                bounds.emplace(parsed->name, parsed->value);
        } else if (parsed->is_addr && !starts_with(parsed->name, "__CDBINFO__")) {
            symbols.push_back(*parsed);
        }
    }

    const auto page_head = [&](int n) { return z88dk_page_head(bounds, n); };

    // A dot command (GH #351): the DivMMC part of the map is placed by rule 5.
    std::string dot_why;
    const std::optional<Z88dkDot> dot_command = z88dk_dot_command(bounds, &dot_why);
    if (!dot_why.empty()) return failed(dot_why);
    const Z88dkDot* dot = dot_command ? &*dot_command : nullptr;

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
                                                      page_head, dot);
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
    // The C_LINE scope of every code record, before a listing replaces its module's lines:
    // a listing keeps only the file of a line (GH #433).
    std::vector<Record> scope_records;
    for (const Record& r : records)
        if (!is_data_section(r.section)) scope_records.push_back(r);

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
    // ends of the range in that space. With `more` given, every binary is scanned and
    // *more says whether a second one covers it too (overlay sections at one ORG).
    const auto bin_for = [&](uint64_t value, uint32_t length, FetchSource space,
                             bool* more = nullptr) -> const Bin* {
        const Bin* found = nullptr;
        for (const Bin& bin : bins) {
            if (value < bin.head || value + length > bin.head + bin.bytes.size()) continue;
            const auto group_space = [&](uint64_t v) {
                const Z88dkSpace g = z88dk_section_space(bin.group, static_cast<uint32_t>(v),
                                                         page_head, dot);
                if (g.kind == Z88dkSpace::Kind::Paged) return g.space;
                if (g.kind == Z88dkSpace::Kind::BindAtLoad) return space_of(g.addr16);
                return MemSpace::floating();
            };
            if (space_key(group_space(value)) != space_key(space)
                || space_key(group_space(value + length - 1)) != space_key(space))
                continue;
            if (!more) return &bin;
            if (found) {
                *more = true;
                return found;
            }
            found = &bin;
        }
        return found;
    };

    // The listings (GH #348): each one a module's lines, or a note why not.
    std::vector<Z88dkListedRange> listed_ranges;
    std::vector<SourceAlias> listed_aliases;
    std::map<std::string, SourceText> listed_texts;
    std::set<std::string> listed_modules;
    std::vector<std::string> notes;
    ListedFunctions listed_functions;
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
                                                          page_head, dot);
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
            // A byte two section binaries both claim is unknown, never one of them.
            bool more = false;
            const Bin* bin = bin_for(value, 1, space, &more);
            if (!bin || more) return std::nullopt;
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
            for (const Z88dkListedFunction& lf : z88dk_listing_functions(*parsed))
                listed_functions[{r.module, lf.name}] = lf.fastcall;
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

    // The one rule that puts a map symbol in its space: the symbols below and the variable store.
    const PlaceSymbol place_symbol = [&](const Z88dkMapLine& s) {
        SymPlace out;
        if (s.section.empty()) {
            out.why = "symbol '" + s.name + "' is in no section";
            return out;
        }
        if (s.value > 0xFFFFFF) {
            out.why = "address $" + hex(s.value) + " does not fit 24 bits";
            return out;
        }
        const Z88dkSpace placed = z88dk_section_space(s.section, static_cast<uint32_t>(s.value),
                                                      page_head, dot);
        out.addr16 = placed.addr16;
        if (placed.kind == Z88dkSpace::Kind::Paged) {
            out.space = placed.space;
            out.ok = true;
        } else if (placed.kind == Z88dkSpace::Kind::BindAtLoad) {
            out.space = space_of(placed.addr16);
            out.ok = true;
            out.bound = true;
        } else {
            out.why = placed.why;
        }
        return out;
    };

    // The symbols that bound an extent or mark a function entry, in their space.
    std::map<uint32_t, std::vector<Symbol>> symbols_by_space;
    for (const Z88dkMapLine& s : symbols) {
        const SymPlace placed = place_symbol(s);
        if (!placed.ok) continue;
        Symbol out;
        out.name = s.name;
        out.module = s.module;
        out.addr = placed.addr16;
        out.loc = parse_location(s.location);
        out.space = placed.space;
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
                std::move(listed_texts), dot != nullptr,
                build_variable_store(cdb_lines, symbols, scope_records, place_symbol, bounds,
                                     listed_functions, float_library));
    return result;
}
