#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the syntax tree of a `.jds` script (dsl-frontend.md §2.1).
//
// The parser builds it, `check_script()` validates and BINDS it (every
// upper-case name is resolved to what it reads in the scope it appears in),
// and the evaluator walks it. Expression nodes are shared (`ExprPtr`) because
// a compiled predicate keeps the tree it evaluates alive after the parse
// result is gone.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "script/diagnostic.h"
#include "script/lexer.h"

namespace jnext {
namespace script {

// ---------------------------------------------------------------------------
// Built-in names
// ---------------------------------------------------------------------------

/// What an upper-case name reads, after binding (§2.3). The LIVE names read
/// the machine through the backend's inspection surface; the `P_` names read
/// the delivered event's payload. `PC`, `HC_ULA` and `CVC` exist in both
/// forms: which one a use binds to depends on the scope (see `bind` in
/// check.cpp).
enum class Builtin : uint8_t {
    None,  ///< not bound yet (or not a built-in)

    // registers — 8-bit
    A, B, C, D, E, H, L, F, I, R,
    // registers — 16-bit
    AF, BC, DE, HL, IX, IY, SP, PC, AF2, BC2, DE2, HL2,
    // flags, from F
    CF, ZF, SF, PF, HF, NF,
    // interrupt state
    IFF1, IFF2, IM, HALTED,
    // time
    FRAME, CYCLE, TFRAME,
    // the VHDL raster counters (raster_state.h)
    RAW_HC, RAW_VC, HC_ULA, VC_ULA, CVC, PHC,
    // host / machine
    AUDIO_MUTE, MACHINE,

    // event payload (§2.3 second table)
    P_ADDR, P_VALUE, P_PREV, P_PAGE, P_PORT, P_REG, P_SOURCE, P_KEY, P_REASON,
    P_CPC, P_WAIT_V, P_WAIT_H, P_SRC, P_DST, P_LEN, P_DMA_MODE, P_IO_SRC, P_IO_DST,
    P_PC,      ///< `PC` inside an event rule: the pre-execution PC of the cause
    P_HC_ULA,  ///< `HC_ULA` inside a `copper` rule: the Copper step's position
    P_CVC,     ///< `CVC` inside a `copper` rule: the Copper step's line
};

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

enum class ExprKind : uint8_t {
    Int,        ///< literal, `true`/`false`, `CPU`/`DMA`/`COPPER` (`value`)
    Str,        ///< string literal (`pieces`)
    Name,       ///< upper-case built-in or payload name (`text`, bound `builtin`)
    Var,        ///< a user `var` (`text`)
    Symbol,     ///< `@name` (`text`; `value` once resolved)
    Unary,      ///< `op a`
    Binary,     ///< `a op b`
    Mem,        ///< `mem[a]`
    Mem16,      ///< `mem16[a]`
    NextReg,    ///< `nextreg[a]`
    Mmu,        ///< `mmu[a]`
    Page,       ///< `page[a]`
    Stack,      ///< `stack[a]`
    Phys,       ///< `phys[a, b]` — a = 8K page, b = offset
    SnapField,  ///< `text.field` / `text.MMU[a]` (§2.5)
    Changed,    ///< `changed(text, group)`
    Depth,      ///< `depth(text)`
};

enum class Op : uint8_t {
    // unary
    Neg, BitNot, Not,
    // binary
    Or, And, Eq, Ne, Lt, Gt, Le, Ge, BitOr, BitXor, BitAnd, Shl, Shr, Add, Sub, Mul, Div, Mod,
};

/// `changed(NAME, group)` groups (§2.5).
enum class SnapGroup : uint8_t { Regs, Mmu, Iff1, Stack0 };

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

/// A piece of a string literal: text, or an embedded expression with a format.
struct StringPart {
    std::string text;         ///< literal text (when `expr` is null)
    ExprPtr     expr;         ///< the `${…}` expression, parsed
    Fmt         fmt = Fmt::None;
};

struct StringLit {
    SourcePos               pos;
    std::vector<StringPart> parts;
};

struct Expr {
    ExprKind    kind = ExprKind::Int;
    SourcePos   pos;
    int32_t     value = 0;        ///< Int; Symbol once resolved
    bool        resolved = false; ///< Symbol: `value` is valid
    std::string text;             ///< Name / Var / Symbol / snapshot name
    std::string field;            ///< SnapField: the field name (`A`, `MMU`, …)
    Op          op = Op::Neg;     ///< Unary / Binary
    SnapGroup   group = SnapGroup::Regs;
    Builtin     builtin = Builtin::None;  ///< Name, after binding
    ExprPtr     a, b;             ///< operands / index / phys page+offset
    StringLit   str;              ///< Str
};

// ---------------------------------------------------------------------------
// Events (§2.1 `event`)
// ---------------------------------------------------------------------------

enum class EventType : uint8_t {
    Execute, Read, Write, IoRead, IoWrite, NextReg, Frame, Scanline, Cycle,
    Interrupt, Nmi, Reset, HostKey, Stop, Copper, Dma,
};

enum class CopperSub : uint8_t { Move, Wait, Halt };
enum class DmaSub : uint8_t { Start, Byte, End };

/// One `on …` event with its filter, as written. The expressions are NOT
/// evaluated here: the filter bounds are resolved when the rule is registered.
///
/// Which fields a type uses:
///   Execute/Read/Write, Dma Byte:  addr_spec — `lo`[`..hi`] [`page` page_lo],
///                                  or the page-only form `page` page_lo[`..page_hi`]
///                                  (`page_only`, `lo` null). Dma Byte may have none.
///   IoRead/IoWrite:                `lo`[`..hi`], or `mask` + `value`
///   NextReg, Copper Move:          reg_spec — `lo`[`..hi`] (optional on Copper Move)
///   Frame (optional), Scanline, Cycle:  `lo`
///   HostKey:                       `hostkey`
///   Copper:                        `copper`, and `at` `at_lo`[`..at_hi`]
///   Dma:                           `dma`
struct EventSpec {
    EventType type = EventType::Execute;
    SourcePos pos;
    ExprPtr   lo, hi;
    ExprPtr   page_lo, page_hi;
    bool      page_only = false;
    ExprPtr   mask, value;      ///< port_spec's `mask … value …` form
    int       hostkey = 0;
    CopperSub copper = CopperSub::Move;
    DmaSub    dma = DmaSub::Start;
    ExprPtr   at_lo, at_hi;
};

// ---------------------------------------------------------------------------
// Actions (§2.1 `action`)
// ---------------------------------------------------------------------------

enum class ActionKind : uint8_t {
    Log, Stop, Assert, Exit, DumpRegs, DumpMmu, DumpMem, Snap, Unsnap, DumpDiff,
    Enable, Disable, Screenshot, SaveSnapshot, CompareScr, Press, Release, Joystick,
    Set, Out, If,
};

struct Action {
    ActionKind kind = ActionKind::Log;
    SourcePos  pos;
    /// log: indent (optional); assert/if: condition; exit: code; dump_mem: address;
    /// press: `for` frames (optional); joystick: bits; set: the value;
    /// out: port.
    ExprPtr e1;
    /// dump_mem: length; out: value.
    ExprPtr e2;
    /// set: the lvalue — a Var, a Name (register, flag, IFF1, IFF2, IM,
    /// AUDIO_MUTE), or a Mem / Mem16 / Phys / NextReg node.
    ExprPtr target;
    /// log/assert message, stop reason (optional), screenshot / save_snapshot /
    /// compare_scr file, press / release key.
    std::optional<StringLit> s1;
    /// compare_scr message.
    std::optional<StringLit> s2;
    /// snap / unsnap / dump_diff / enable / disable: the name.
    std::string name;
    SourcePos   name_pos;
    /// joystick: the port index (1 or 2).
    int joystick = 0;
    /// if: the branches.
    std::vector<Action> then_body;
    std::vector<Action> else_body;
    bool has_else = false;
};

// ---------------------------------------------------------------------------
// The script
// ---------------------------------------------------------------------------

struct VarDecl {
    std::string name;
    SourcePos   pos;
    ExprPtr     init;
};

struct Rule {
    SourcePos   pos;          ///< of `on` (or of the label, when there is one)
    std::string label;        ///< empty = unlabelled
    SourcePos   label_pos;
    bool        disabled = false;
    EventSpec   event;
    bool        once = false;
    ExprPtr     when;         ///< null = no `when`
    std::vector<Action> body;
};

struct Script {
    std::vector<VarDecl> vars;
    std::vector<Rule>    rules;
};

}  // namespace script
}  // namespace jnext
