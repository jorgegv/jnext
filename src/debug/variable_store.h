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
// std headers, cdb_record.h and source_map.h (SourceAddress) only.
// ---------------------------------------------------------------------------

#include "debug/cdb_record.h"
#include "debug/source_map.h"

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
};

struct CFunction {
    CdbRecord record;          ///< the `F:` record
    std::string text;          ///< the unescaped record
    std::string name;
    std::string module;        ///< the module of the entry symbol (the CDB line's when there is none)
    CStorage entry;
    std::vector<CScope> scopes;   ///< sorted by (space, address)
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

class VariableStore {
public:
    /// Sorts everything canonically; `variables[i].function` indexes the vector as
    /// given and is remapped to the sorted one.
    VariableStore(std::vector<CFunction> functions, std::vector<CVariable> variables,
                  std::vector<CRecord> types, std::vector<CRecord> modules,
                  std::vector<std::string> refused);

    const std::vector<CFunction>& functions() const { return functions_; }
    const std::vector<CVariable>& variables() const { return variables_; }
    const std::vector<CRecord>& types() const { return types_; }
    const std::vector<CRecord>& modules() const { return modules_; }
    /// "<escaped symbol>: <reason>" of every record the decoder refused.
    const std::vector<std::string>& refused() const { return refused_; }

private:
    std::vector<CFunction> functions_;
    std::vector<CVariable> variables_;
    std::vector<CRecord> types_;
    std::vector<CRecord> modules_;
    std::vector<std::string> refused_;
};

}  // namespace dbg
}  // namespace jnext
