#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "debug/disasm.h"

class SymbolTable;

/// GH #21 — turning what the disassembly panel PAINTS into text you can paste.
///
/// Pure C++, no Qt: the panel is custom-painted, so the copy path cannot be
/// inherited from a text widget and has to be built against the line model.
/// Keeping it here rather than in the panel is what makes it assertable
/// without a display, and — more importantly — what stops the painter and the
/// clipboard from disagreeing about symbol substitution: both call
/// apply_symbols() below, there is no second copy of that rule.
namespace disasm_text {

/// What a copy carries.
enum class CopyFormat {
    /// Mnemonic and operands only — no address, no opcode bytes, no gutter.
    /// Indented four spaces so it pastes into a source file and assembles:
    /// assemblers that take a bare column-1 word as a label definition would
    /// otherwise read `LD HL,$4000` as a label named LD.
    AsmOnly,
    /// Address, opcode bytes and mnemonic, laid out in the panel's own
    /// columns — for bug reports and annotated traces.
    WithAddresses,
};

/// Indent placed in front of every AsmOnly line. See CopyFormat::AsmOnly.
constexpr const char* ASM_INDENT = "    ";

/// One line as the panel shows it, with MAP symbols already substituted.
struct CopyLine {
    uint16_t    addr       = 0;
    uint8_t     bytes[4]   = {};
    int         byte_count = 0;
    std::string mnemonic;   ///< post-substitution, exactly as painted
};

/// The first `$XXXX` (exactly four hex digits) in `mnemonic`, or 0 if there is
/// none. 0 is ambiguous with a genuine `$0000`; callers disambiguate by also
/// looking for the literal "$0000", which is the rule the panel already used.
uint16_t extract_immediate16(const char* mnemonic);

/// `mnemonic` with its 16-bit immediate replaced by the MAP-file symbol for
/// that address, if `symbols` holds one. Returns the mnemonic unchanged when
/// `symbols` is null, when there is no 16-bit immediate, or when the address
/// is not in the table. This IS the painter's rule — see the namespace note.
std::string apply_symbols(const char* mnemonic, const SymbolTable* symbols);

/// Disassemble from `start` through `end`, both inclusive, reading live memory
/// through `read_fn`.
///
/// The range is addresses, not line indices, because the panel's line vector
/// is rebuilt from scratch on every scroll, refresh and re-centre — an index
/// into it survives none of those, an address survives all of them. The walk
/// stops at the first instruction that starts past `end`, at an address-space
/// wrap, or when `end` is unreachable from `start` (which memory rewritten
/// between the drag and the copy can cause); it never runs unbounded.
std::vector<CopyLine> collect_range(uint16_t start, uint16_t end,
                                    const DisasmReadFn& read_fn,
                                    const SymbolTable* symbols);

/// Render `lines` as clipboard text. Every line, including the last, ends in
/// '\n'; an empty input renders as an empty string, never as a bare newline.
std::string format_lines(const std::vector<CopyLine>& lines, CopyFormat fmt);

} // namespace disasm_text
