#pragma once

// ---------------------------------------------------------------------------
// The z88dk CDB record decoder (GH #432, E353-02 of epic #353; plan section 2.2).
//
// A z88dk `-debug` build puts its C debug information in the map as symbols
// named `__CDBINFO__<escaped record>` (sccz80: src/sccz80/cdbfile.c; zsdcc: SDCC's
// cdbFile.c; 80cc: sccz80's writer). This file turns ONE such record into a
// `CdbRecord`: pure, no store, no map reader, no machine. What the records mean
// for a running program (frames, values, storage lookup) is the layers above.
//
// The compilers differ in spelling and the decoder keeps the difference visible
// instead of normalising it away: sccz80/80cc scope a local `vars.c.sum`, zsdcc
// `vars.sum`; a struct is scoped by its declaring header (sccz80) or its module
// (zsdcc); an array is `DA3x` or `DA3d`.
//
// Every refusal carries a reason; a record is never silently dropped.
// std headers only.
// ---------------------------------------------------------------------------

#include <string>
#include <string_view>
#include <vector>

namespace jnext {
namespace dbg {

/// The map symbol prefix of a CDB record.
inline constexpr std::string_view kCdbPrefix = "__CDBINFO__";

/// The record kind: its first letter (`F:` `M:` `S:` `T:`).
enum class CdbKind { Function, Module, Variable, Type };

/// Where the name lives: `G` global, `F<file>` file scope, `L<file>.<function>`
/// a function's locals and parameters, `S` a struct member (inside a `T:`).
/// A module record has no scope.
enum class CdbScope { None, Global, File, Local, Member };

/// One element of a type chain, outermost first.
enum class CdbTypeKind {
    Char, Int, Long, Float, Fixed, Float16, Void,  // SC SI SL SF SQ S6 SV/DV
    Struct,                                        // ST<tag>:S (also a union)
    Bitfield,                                      // SB<start>$<width>:U
    Array,                                         // DA<n>x (sccz80) / DA<n>d (zsdcc)
    Pointer,                                       // DG DC DX DD DI DP
    Function                                       // DF: a function returning the rest
};

struct CdbTypeElem {
    CdbTypeKind kind = CdbTypeKind::Int;
    bool is_unsigned = false;  // `:U`; for the Struct / Void / Function kinds always as written
    int count = 0;             // Array: the element count of THIS dimension (-1: incomplete)
    char pointer_class = 0;    // Pointer: G C X D I P
    std::string tag;           // Struct: the tag without the `:S`
    int bit_start = 0;         // Bitfield
    int bit_width = 0;         // Bitfield
};

/// `({size}chain)`. `size` is the byte size of the whole object as the compiler
/// wrote it (-1 for an incomplete array, 0 for an enumerator).
struct CdbType {
    int size = 0;
    std::vector<CdbTypeElem> chain;
};

/// A named thing with a type and a place: a variable, a function or a struct
/// member (`name$level_sub$block(type),space,...`). Also a module (`name` only).
struct CdbVar {
    std::string name;
    int level = 0;  // block nesting level; zsdcc 0 for a compiler temporary
    int sub = 0;
    int block = 0;
    CdbType type;
    // Address space: E data symbol, B stack, R registers, C code, D literal,
    // Z struct member / earlier prototype. 0 for a module.
    char space = 0;
    // The numbers after the space letter, as written: `B,1,-2` is {1,-2}.
    std::vector<int> nums;
    // R: the registers, low byte first (`[c,b]` is BC); `[]` is EMPTY and means
    // optimised out. Meaningful only when space == 'R'.
    std::vector<std::string> regs;

    /// B: the offset from the frame base (`fp`), after the leading 1.
    int stack_offset() const { return nums.size() >= 2 ? nums[1] : 0; }
    /// R with an empty list: the compiler kept the variable nowhere.
    bool optimised_out() const { return space == 'R' && regs.empty(); }
};

/// A `T:` member: its byte offset in the aggregate (0 for every member of a union).
struct CdbMember {
    int offset = 0;
    CdbVar var;
};

/// Why a variable record is not a user variable; views hide everything but None
/// (owner decision OD-6 of #353).
enum class CdbHidden {
    None,
    Temporary,        // zsdcc `sloc<n>`, `__str_<n>`, `__<digits>`
    Enumerator,       // a record with an empty type: no value, no enum type
    IncompleteArray,  // sccz80 / 80cc `({-1}DA-1x,...)`, the twin of a sized array
    Function          // a function-typed `S:` record (zsdcc writes functions as both)
};

struct CdbRecord {
    CdbKind kind = CdbKind::Variable;
    CdbScope scope = CdbScope::None;
    // File / Local: the file or module (`vars.c`, `vars`, `game/util.c`); Type: the
    // declaring header or module.
    std::string scope_name;
    // Local: the function, split off at the LAST '.' of the scope text (a function
    // name has no dots, a path does).
    std::string function;
    CdbVar var;                      // name, type, place (Module: name only; Type: the tag)
    std::vector<CdbMember> members;  // Type only
    CdbHidden hidden = CdbHidden::None;  // Variable only
};

/// `error` is empty exactly when `record` is meaningful.
struct CdbDecoded {
    CdbRecord record;
    std::string error;
    bool ok() const { return error.empty(); }
};

struct CdbUnescaped {
    std::string text;
    std::string error;
    bool ok() const { return error.empty(); }
};

/// z80asm's encoding of a record into a symbol name: a letter stands for itself,
/// every other byte is `_` and two lower-case hex digits (digits and `_` too).
/// Refused: any other unescaped byte, a bad or truncated `_hh`, an escaped NUL.
CdbUnescaped cdb_unescape(std::string_view escaped);

/// Decode an UNESCAPED record (`S:G$g_int$0_0$0({2}SI:S),E,0,0`).
CdbDecoded cdb_decode(std::string_view record);

/// Decode a map symbol name: the `__CDBINFO__` prefix, then `cdb_unescape`, then
/// `cdb_decode`.
CdbDecoded cdb_decode_symbol(std::string_view symbol_name);

}  // namespace dbg
}  // namespace jnext
