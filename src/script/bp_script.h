#pragma once

// ---------------------------------------------------------------------------
// jnext::script - the Breakpoints panel's breakpoints as a `.jds` file.
//
// A Qt-free codec: it knows how one GUI breakpoint is SPELLED as a script rule
// and which breakpoints have no faithful spelling (they are refused, never
// written approximately), and how a file is READ back into breakpoints. The Qt
// BreakpointModel only converts between its rows and `BpSpec`.
//
// A breakpoint is `LABEL: on EVENT FILTER do stop end` (`do end` for one that
// only counts its hits, GH #372); the language's grammar of record is
// doc/design/debug-subsystem/dsl-frontend.md section 2.1.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "debug/debugger.h"
#include "debug/mem_space.h"
#include "debug/source_map.h"
#include "script/diagnostic.h"
#include "script/expr_compiler.h"

namespace jnext {
namespace script {

/// One GUI breakpoint. `Kind` is BreakpointModel::Type, value for value.
struct BpSpec {
    enum Kind : uint8_t { Execute = 0, Read, Write, ReadWrite, IoRead, IoWrite, Interrupt, Nmi };
    Kind            kind  = Execute;
    uint16_t        addr  = 0;                      ///< address, or port (<= 0xFF: low-byte decode); 0 for Interrupt / Nmi
    /// Execute / Read / Write / Read-Write only, and never with a space or a source line: the end of an
    /// address range `addr..hi`. nullopt (or == addr) = one address.
    std::optional<uint16_t> hi;
    /// IO Read / Write only: the port decode mask (`addr` is then the value matched: `(port & mask) == addr`).
    /// nullopt (or == bp_port_mask_for(addr)) = the address rule.
    std::optional<uint16_t> mask;
    /// A `.jds` expression, one line, in the payload scope of the kind (`bp_scope`); "" = none. Stored trimmed.
    std::string     condition;
    dbg::MemSpace   space = dbg::MemSpace::cpu();   ///< Execute only
    std::string     file_line;                      ///< Execute only; non-empty = a source line
    std::vector<::SourceAddress> locations;      ///< a source line's members, in resolve() order
    bool            enabled = true;
    bool            count_only = false;             ///< counts its hits and never stops (`do end`)
};
/// The payload scope a breakpoint kind's condition compiles in: the DSL checker's own table (`scope_of`),
/// not a second one.
PayloadScope bp_scope(BpSpec::Kind k);
/// "" when `text` is a condition the codec can write and read back unchanged for `kind`; else why not. One
/// check for the dialog, the saver and the decoder. Refused: blank text, a line break, anything
/// `compile_expr` refuses in that scope (script-only forms such as `changed()`, a payload name the kind
/// lacks, an unknown `@symbol`), and any text that does not come back as itself from the parser (a `;`
/// comment would swallow ` do stop end`).
std::string bp_condition_problem(BpSpec::Kind kind, const std::string& text, const dbg::Debugger& dbg);
/// The effective range end and port mask (the optional fields with their defaults applied).
uint16_t bp_effective_hi(const BpSpec& s);
uint16_t bp_effective_mask(const BpSpec& s);
/// THE type-name table: the Type column's text for each kind, "?" past the last.
const char* bp_kind_name(BpSpec::Kind k);
/// THE port decode rule (GH #222): a port of 00-FF is a LOW-BYTE match, 0100 and
/// up that exact 16-bit port, so the exact port 0x00xx cannot be named.
uint16_t bp_port_mask_for(uint16_t addr);
bool operator==(const BpSpec& a, const BpSpec& b);
/// The same places, whatever order they are listed in.
bool same_locations(std::vector<::SourceAddress> a, std::vector<::SourceAddress> b);

struct BpScript {
    std::string              text;
    std::vector<std::string> refused;   ///< one line per breakpoint left out, with the reason
};

/// The file: a header comment, then one rule per spellable spec in input order,
/// labelled bp1, bp2, ... (numbered over the WRITTEN rules), and a
/// `; not saved: ...` comment in place of each refused spec.
BpScript bp_script_text(const std::vector<BpSpec>& specs, const dbg::Debugger& dbg);

struct BpRead {
    std::vector<BpSpec>     specs;
    std::vector<Diagnostic> errors;
    bool ok() const { return errors.empty(); }
};

/// Read a breakpoint file. All or nothing: any error leaves `specs` empty.
BpRead bp_specs_from_script(const std::string& text, const dbg::Debugger& dbg);

}  // namespace script
}  // namespace jnext
