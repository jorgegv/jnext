#pragma once

// ---------------------------------------------------------------------------
// jnext::script - the Breakpoints panel's breakpoints as a `.jds` file.
//
// A Qt-free codec: it knows how one GUI breakpoint is SPELLED as a script rule
// and which breakpoints have no faithful spelling (they are refused, never
// written approximately), and how a file is READ back into breakpoints. The Qt
// BreakpointModel only converts between its rows and `BpSpec`.
//
// A breakpoint is `LABEL: on EVENT FILTER do stop end`; the language's
// grammar of record is doc/design/debug-subsystem/dsl-frontend.md section 2.1.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "debug/debugger.h"
#include "debug/mem_space.h"
#include "debug/source_map.h"
#include "script/diagnostic.h"

namespace jnext {
namespace script {

/// One GUI breakpoint. `Kind` is BreakpointModel::Type, value for value.
struct BpSpec {
    enum Kind : uint8_t { Execute = 0, Read, Write, ReadWrite, IoRead, IoWrite };
    Kind            kind  = Execute;
    uint16_t        addr  = 0;                      ///< address, or port (<= 0xFF: low-byte decode)
    dbg::MemSpace   space = dbg::MemSpace::cpu();   ///< Execute only
    std::string     file_line;                      ///< Execute only; non-empty = a source line
    std::vector<::SourceAddress> locations;      ///< a source line's members, in resolve() order
    bool            enabled = true;
};
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
