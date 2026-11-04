#pragma once

// ---------------------------------------------------------------------------
// The C variable store (GH #433, E353-03 of epic #353; plan sections 2.1-2.5).
//
// The decoded `__CDBINFO__` records of a z88dk `-debug` map (cdb_record.h), joined
// to the map's symbols by the z88dk adapter (load_z88dk_source_map()): where each
// E-space variable and each function entry lives, which function a local belongs
// to, and each function's scope table. It is IMMUTABLE once built and is part of
// the SourceMap it was read from (`SourceMap::variables()`): attached, dropped,
// made stale and restored with it, by the same rules. This header knows no z88dk
// map type; the joins are the adapter's.
//
// Canonical order: every vector is sorted by the record text, so the store is the
// same whatever order the map lists its lines in.
// GH #434 (E353-04, plan 2.1): a function is also its EXTENT (`pieces`, in map values, split at
// the 8 KB logical boundaries and placed one by one: a function that crosses a slot lives in two
// spaces), the frame helpers of the sccz80 / 80cc debug frames, and the calling-convention
// evidence of its listing. A scope entry is filed under the function whose entry VALUE is the
// greatest at or below it in the same module and section, never by comparing spaces.
// std headers, cdb_record.h and source_map.h (SourceAddress) only.
// ---------------------------------------------------------------------------

#include "debug/cdb_record.h"
#include "debug/source_map.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jnext {
namespace dbg {

/// An E-space variable or a function entry, joined to a map symbol.
struct CStorage {
    bool resolved = false;
    SourceAddress at;          ///< resolved: the page-aware address (#360)
    std::string symbol;        ///< the map symbol it was joined to (_s_dup, _st_sum_calls, ...)
    std::string why;           ///< !resolved: why (no symbol, two symbols, not in a RAM page...)
};

/// One entry of a function's scope table: the scope the compiler gave the code
/// that starts at `at`, in the CDB records' terms (zsdcc's level decomposed).
struct CScope {
    SourceAddress at;
    int line = 0;              ///< the owning C_LINE record's line
    bool known = false;        ///< false: no scope given, or it names another function
    int level = 0;
    int sub = 0;
    int block = 0;
    uint32_t offset = 0;       ///< the record's map value minus its function's entry value
};

/// One run of a function's code that lies in ONE space and ONE 8 KB logical slot.
struct CPiece {
    SourceAddress at;          ///< first byte
    uint32_t hi_excl = 0;      ///< exclusive end, an address of that space
    uint32_t offset = 0;       ///< bytes from the function's entry to `at`
};

/// What the listings beside the map say of a function (GH #434, OD-14).
enum class CConv : uint8_t {
    Unlisted,          ///< no used listing names it
    Listed,            ///< a used listing names it
    ListedFastcall,    ///< ... and flags it `__z88dk_fastcall`
};

/// Which z88dk compiler wrote a function's records (GH #438, plan 1.2). Unknown: a store that did not come
/// from a map (a synthetic one), where no compiler-specific rule applies.
enum class CCompiler : uint8_t { Unknown, Sccz80, Zsdcc, Cc80 };

struct CFunction {
    CdbRecord record;          ///< the `F:` record
    std::string text;          ///< the unescaped record
    std::string name;
    std::string module;        ///< the module of the entry symbol (the CDB line's when there is none)
    CStorage entry;
    std::vector<CScope> scopes;   ///< sorted by offset
    std::vector<CPiece> pieces;   ///< the extent, in order; empty: unknown (`extent_why`)
    std::string extent_why;       ///< pieces empty: why; else the reason the extent stops short, or empty
    CConv conv = CConv::Unlisted;
    CCompiler compiler = CCompiler::Unknown;   ///< set by the adapter from the map (see build_variable_store())
};

struct CVariable {
    CdbRecord record;          ///< an `S:` record, `hidden` included
    std::string text;
    std::string module;        ///< the CDB line's module
    std::string c_name;        ///< the C name (`calls` for sccz80's `st_sum_calls`)
    int function = -1;         ///< index into functions(): parameters, locals, static locals
    CStorage storage;          ///< space E only
};

/// A `T:` or `M:` record, with the module of its CDB line.
struct CRecord {
    CdbRecord record;
    std::string text;
    std::string module;
};

/// The debug-frame helpers sccz80 and 80cc call, joined by public name (GH #434, plan 2.1.2);
/// unresolved, with a reason, in a zsdcc map.
struct CFrameHelpers {
    CStorage framepointer;   ///< `__debug_framepointer`
    CStorage push_frame;     ///< `l_debug_push_frame`
    CStorage pop_frame;      ///< `l_debug_pop_frame`
};

/// What the adapter knows of a map without variables (GH #434, OD-13).
struct CNoRecordFacts {
    bool c_line_records = false;   ///< the map has `__C_LINE_` records that carry a scope
    bool zsdcc_form = false;       ///< ... and one of them is zsdcc's `x` form
};

/// Where a PC is in a function.
struct CLocation {
    int function = -1;        ///< index into functions()
    uint32_t offset = 0;      ///< bytes from the function's entry
};

class VariableStore {
public:
    /// Sorts everything canonically; `variables[i].function` indexes the vector as
    /// given and is remapped to the sorted one.
    VariableStore(std::vector<CFunction> functions, std::vector<CVariable> variables,
                  std::vector<CRecord> types, std::vector<CRecord> modules,
                  std::vector<std::string> refused, CFrameHelpers helpers = {},
                  CNoRecordFacts facts = {}, std::string float_library = {});

    const std::vector<CFunction>& functions() const { return functions_; }
    const std::vector<CVariable>& variables() const { return variables_; }
    const std::vector<CRecord>& types() const { return types_; }
    const std::vector<CRecord>& modules() const { return modules_; }
    /// "<escaped symbol>: <reason>" of every record the decoder refused.
    const std::vector<std::string>& refused() const { return refused_; }
    const CFrameHelpers& helpers() const { return helpers_; }
    const CNoRecordFacts& no_record_facts() const { return facts_; }
    /// The float library the map names (`math32`, `mbf32`, ...: the first non-zero `__code_fp_<lib>_size`),
    /// empty when it names none (GH #438, z88dk#3236: the records do not say the float format).
    const std::string& float_library() const { return float_library_; }

    /// The function whose extent holds `at` (the space and the address both), and the offset
    /// into it; nothing else is used: neither the nearest symbol nor a jump target. nullopt: in
    /// no function (assembly, the CRT, a library routine), or a function with an unknown extent.
    std::optional<CLocation> locate(const SourceAddress& at) const;

    /// The scope entry in force at `offset` of `function`: the one with the greatest offset at or
    /// below it. Null when the function has none at or below (before its first entry).
    const CScope* scope_at(int function, uint32_t offset) const;

private:
    std::vector<CFunction> functions_;
    std::vector<CVariable> variables_;
    std::vector<CRecord> types_;
    std::vector<CRecord> modules_;
    std::vector<std::string> refused_;
    CFrameHelpers helpers_;
    CNoRecordFacts facts_;
    std::string float_library_;
};

}  // namespace dbg
}  // namespace jnext
