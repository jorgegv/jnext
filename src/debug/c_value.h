#pragma once

// ---------------------------------------------------------------------------
// The C value layer (GH #434, E353-04 of epic #353, stage 2; plan sections 2.5-2.7).
//
// PURE: std headers, cdb_record.h and variable_store.h. No machine, no map reader.
// A `CReader` supplies bytes, so every rule is pinned with synthetic memory; what a
// reader reads (the CPU view for the stack and for pointer targets, a page for a
// global) is the caller's choice (stage 3).
//
// What is here: the formatting of a value from its type chain (integers, chars,
// z88dk's 48-bit float, IEEE 754 binary32 and binary16, pointers, arrays, structs,
// bitfields), the type text, the struct type lookup, the C path grammar and its
// evaluation over a typed location, which locals are visible at a scope, the
// register locals of frame 0 and the sccz80 fastcall marker.
//
// NO number is formatted with a printf-family call or an ostream: QApplication runs
// setlocale(LC_ALL, ""), and on a Spanish host `%g` prints "1,5". Every number goes
// through std::to_chars, which is locale-independent.
// ---------------------------------------------------------------------------

#include "debug/cdb_record.h"
#include "debug/variable_store.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jnext {
namespace dbg {

/// Bytes at an address of whatever view the caller bound. False: unreadable.
using CReader = std::function<bool(uint32_t addr, size_t n, uint8_t* out)>;

// -- numbers -----------------------------------------------------------------

/// z88dk's 48-bit float (`{6}SF`): five little-endian mantissa bytes, the sign in bit 7 of the
/// fifth, an implied leading 1, then the biased exponent byte; value = 0.1mmm...(binary) x 2^(e-0x80);
/// e = 0 is zero. Printed with 12 significant digits (the mantissa has 40 bits).
std::string c_format_float48(const uint8_t bytes[6]);
/// IEEE 754 binary32 (`{4}SF`, zsdcc): the shortest text that reads back; `inf`, `-inf`, `nan`, `-0`.
std::string c_format_float32(uint32_t bits);
/// IEEE 754 binary16 (`S6`), through float.
std::string c_format_float16(uint16_t bits);

// -- types -------------------------------------------------------------------

/// `char`, `unsigned char`, `int`, `long`, `long long` ({8}SL), `float`, `void`, `struct <tag>` (the CDB has no
/// union marker), `T *`, `T [n]`, `T (*)()`. `from` skips that many outer chain elements.
std::string c_type_text(const CdbType& type, size_t from = 0);

struct CTypeLookup {
    const CRecord* record = nullptr;
    std::string why;   ///< record null: why ("struct point: 2 definitions, none in this module")
};
/// The `T:` record of `tag`: first those whose scope name is `scope_name` (zsdcc's own copy), then those
/// of `module`, then a tag that is unique across all `T:` records (sccz80 and 80cc: one per header). Several
/// DIFFERENT definitions at the deciding step are ambiguous, never first-wins.
CTypeLookup c_find_type(const VariableStore& store, const std::string& tag, const std::string& scope_name,
                        const std::string& module);

/// What a type lookup and a deref need to know about the variable a type belongs to.
struct CFormatCtx {
    const VariableStore* store = nullptr;
    std::string scope_name;   ///< the variable record's scope text (`vars`, `vars.c`)
    std::string module;       ///< the variable's CDB module
    CReader read;             ///< the CPU view: pointer targets, strings, and a location that is no longer inside its root object
    /// How the ROOT object's own bytes are read (a global lives in a page, not in the CPU view); unset: `read`. Used while a
    /// location is `CLoc::rooted`, that is until a pointer is followed.
    CReader read_root;
    /// ` <name>` of a resolved variable or function at that address, mapped now; empty when none.
    std::function<std::string(uint16_t)> name_at;
};

/// The byte size of the object at the head of `chain` (-1: unknown: float, void, function).
int c_leaf_size(const CFormatCtx& ctx, const std::vector<CdbTypeElem>& chain, size_t from = 0);

// -- formatting --------------------------------------------------------------

struct CFormatted {
    bool ok = false;
    std::string text;
    std::string why;                 ///< !ok
    std::vector<std::string> notes;  ///< e.g. "bitfield layout unknown (z88dk U2)"
};

/// Format an object of `type` whose `avail` bytes start at `bytes`. Arrays show at most 16 elements,
/// strings at most 32 bytes. Aggregates and pointers are read through `ctx`. A function or void is refused.
CFormatted c_format_value(const CFormatCtx& ctx, const CdbType& type, const uint8_t* bytes, size_t avail);

// -- paths -------------------------------------------------------------------

struct CPathStep {
    enum class Kind : uint8_t { Dot, Arrow, Index };
    Kind kind = Kind::Dot;
    std::string name;     ///< Dot, Arrow
    uint32_t index = 0;   ///< Index
};

/// `path := ['*'] primary { '.' name | '->' name | '[' index ']' }`, `primary := name | fn '::' name | file '::' name |
/// file '::' fn '::' name | '#' index` (`file` contains a '.'), `index := decimal | 0x<hex> | $<hex>` (no sign). Whitespace is allowed between tokens.
struct CPath {
    bool ok = false;
    std::string why;
    bool deref = false;
    enum class Primary : uint8_t { Name, Function, File, Handle };
    Primary primary = Primary::Name;
    std::string qualifier;   ///< the function or file of `fn::x` / `file.c::x`
    std::string file;        ///< `file.c::fn::x`: the file that names which `fn` (two modules can each have a static function of that name)
    std::string name;
    size_t handle = 0;       ///< Handle: the index into VariableStore::variables() (`#12`, then the usual steps): the debugger's own rows name a variable this way when its C name would reach another one (a shadowed local, two same-named block statics)
    std::vector<CPathStep> steps;
};
/// `allow_handle`: also accept the debugger's internal row handle (`#12`, an index into VariableStore::variables()), which only the Variables panel's own rows use, through the `*_row` entry points of the
/// facade; every typed surface (watches, scripts, ZRCP, GDB) parses with the default and so refuses a leading `#`.
CPath c_parse_path(std::string_view text, bool allow_handle = false);

/// A typed place (a struct's size is a declared object's, never its members' extent: "size unknown (z88dk U2)" when there is none): `type.chain` is the chain of THIS object (the outer elements already peeled), `type.size` its size.
struct CLoc {
    CdbType type;
    uint32_t addr = 0;
    std::string note;   ///< e.g. a member of a bitfield-like struct (z88dk U2)
    uint32_t end = 0;   ///< exclusive end of the enclosing object (0: none); a read is clamped to it and zero-extended
    std::optional<uint16_t> ptr_value;   ///< a pointer whose VALUE is given (a register local): followed without reading memory for it
    bool rooted = false;   ///< `addr` is in the root object's own view (`CFormatCtx::read_root`); a followed pointer clears it
};
struct CEval {
    bool ok = false;
    std::string why;
    CLoc loc;
};
/// Walk `deref` and `steps` from `root`: `.name` on a struct, `->name` on a pointer to one, `[i]` on an array
/// (checked against its count) or a pointer (scaled by the element size), `*` on a pointer. Pointers are read
/// through `ctx.read`.
CEval c_eval_path(const CFormatCtx& ctx, const CLoc& root, bool deref, const std::vector<CPathStep>& steps);
/// The integer a Char, Int, Long, Scalar, Bitfield or Pointer holds (the sign from `:S`/`:U`; a pointer's address), from
/// `type`'s own bytes; nullopt for anything else or a short buffer. 8-byte values are the 64-bit pattern.
std::optional<int64_t> c_integer_value(const CdbType& type, const uint8_t* bytes, size_t avail);
/// The value of a 48-bit, binary32 or binary16 float; nullopt for anything else.
std::optional<double> c_float_value(const CdbType& type, const uint8_t* bytes, size_t avail);

/// E353-06's int32 atom (`cvar()` in a script, `CVAR()` over ZRCP; GH #436): the value of the object at `loc` as the DSL's
/// 32-bit pattern, or why it is not an integer scalar. `integer` is the object's value sign- or zero-extended from its
/// size, as the reader gave it (nullopt when it gave none). A Char, Int, Long, Scalar, Bitfield or Pointer of at most 4
/// bytes is accepted (an unsigned 32-bit value keeps its bit pattern, as the DSL's literals do, and a pointer is its
/// address); a larger integer, a float, an aggregate, void, a function and a location carrying a note (a member of an
/// overlapping struct, z88dk U2: the value is not trustworthy) are refused with the reason. Only `loc.type` and
/// `loc.note` are read, so a register-held value (no address) passes a `CLoc` with just its type.
struct CInt32 {
    bool ok = false;
    int32_t value = 0;
    std::string why;   ///< !ok
};
CInt32 c_int32_scalar(const CLoc& loc, std::optional<int64_t> integer);

/// GH #437: what a write stores. Text is what a person typed (the Qt panel, ZRCP, GDB); Int32 is a script's 32-bit value.
struct CNewValue {
    enum class From : uint8_t { Text, Int32 };
    From from = From::Text;
    std::string text;     ///< From::Text
    int32_t int32 = 0;    ///< From::Int32
};
struct CEncoded {
    bool ok = false;
    std::string why;             ///< !ok: "'-1' is out of range for unsigned int (0..65535)", "float is not an integer", ...
    std::vector<uint8_t> bytes;  ///< the object's new bytes (a bitfield: its whole storage unit)
};
/// The bytes `type` (its head and size) stores for `v`, never a truncated or wrapped value: a value out of the type's range is
/// refused. Text: surrounding blanks trimmed, then an optional sign and decimal / `0x` / `$` hex digits, or a `'c'` literal, or
/// (float types) a decimal float read with the classic locale, all of it (`nan` and `inf` are refused). Int32 (a script): the type's range, except a 4-byte
/// integer, which takes the 32-bit pattern (the mirror of c_int32_scalar); a float and an 8-byte integer are refused. A 48-bit
/// float is rounded to nearest (z88dk does). `current` / `avail` are the object's present bytes: only a bitfield reads them,
/// to keep its unit's other bits. No printf, no ostream: locale independent.
CEncoded c_encode_value(const CdbType& type, const CNewValue& v, const uint8_t* current, size_t avail);

/// Read `loc.type.size` bytes at `loc.addr` and format them, with `loc.note` among the notes.
CFormatted c_read_loc(const CFormatCtx& ctx, const CLoc& loc, std::vector<uint8_t>* bytes = nullptr);

// -- children (GH #435) ------------------------------------------------------

/// One child of an aggregate or a pointer: what a tree view lists under it.
struct CChild {
    std::string label;    ///< "x", "[3]", "*" (the dereference of a pointer to a non-struct)
    std::string suffix;   ///< appended to the parent's path: ".x", "->x", "[3]", "[0]"; `parent + suffix` parses back to this place
    CLoc loc;             ///< the child's typed place
};
struct CChildren {
    bool ok = false;
    std::string why;      ///< !ok: "null pointer", the type lookup's reason, "an array of unknown length", ...
    size_t total = 0;     ///< how many children exist (a struct's visible named members, an array's COUNT; 0 = a leaf)
    std::vector<CChild> items;   ///< children [first, first + max) of them
};
/// The children of the object at `loc`: a struct's members in record order (hidden and unnamed ones left out, since no path
/// can name them), an array's elements `first`..`first + max` (`total` is the element count, never the byte size), a pointer
/// to a struct the pointee's members (`->`), a pointer to anything else its one pointee (`[0]`). A scalar, `void *` and a
/// function pointer have none (`ok`, `total` 0); a NULL or unreadable pointer is refused with the dereference's own reason.
CChildren c_children(const CFormatCtx& ctx, const CLoc& loc, size_t first, size_t max);

// -- scope, registers, fastcall ----------------------------------------------

enum class CVarKind : uint8_t { Param, Local, StaticLocal };
struct CLocalRef {
    const CVariable* var = nullptr;
    CVarKind kind = CVarKind::Local;
    bool scope_unknown = false;   ///< shown because the block at the PC is not known (80cc, or an unknown entry)
    /// GH #438: the other visible stack variables whose bytes overlap this one's, comma separated ("y", "i, p"); empty: none. Exactly
    /// one of them holds the slot's value, and nothing says which.
    std::string shares;
};
/// GH #438 S4b: the variable a name means among the visible locals `rows` (c_visible_locals()): the innermost declaration wins, the greatest level and then the greatest block,
/// as in gdb. `others` are the other candidates of that name (shadowed). `certain` is false when some candidate is flagged `scope unknown`: the innermost may not be in scope
/// at all, so a read is noted and a write refused. A parameter and a block local of one name pick the local.
struct CPicked {
    const CLocalRef* chosen = nullptr;
    std::vector<const CLocalRef*> others;
    bool certain = true;
};
CPicked c_pick_local(const std::vector<CLocalRef>& rows, const std::string& name);
/// The note of a read whose pick is not certain, and the row notes of the candidates a pick did not choose.
std::string c_ambiguous_note(const CPicked& p);
inline constexpr const char* kCShadowedNote = "shadowed";
inline constexpr const char* kCMayBeShadowedNote = "may be shadowed";
/// How many trail records of `function` name a block that the replay's stack does not hold at the record's level (GH #438): the replay then
/// keeps its stack. 0 on every map whose block numbers are exact (z88dk#3227 and later), more on sccz80 before it.
int c_trail_disagreements(const VariableStore& store, int function);
/// The note and the refusal text of a stack-slot overlap: `shares_note("y")` is `shares its stack slot with y: only one of them holds its value`.
std::string c_shares_note(const std::string& others);
/// The parameters (ordered by stack position: the records carry no declaration order), then the locals visible at
/// `scope` (the scope table's entry at the PC, null when none), then the static locals, of function `function`.
/// Hidden records (CdbHidden) are left out. Parameters and level-1 locals are always visible; a local of level >= 2
/// is visible iff its block is the open block or one of its ancestors; with no block data (every known scope at
/// level 0 while some local has level >= 2) or an unknown scope, every block local is shown, marked.
/// GH #438: for a function of sccz80 (`CCompiler::Sccz80`) with a trail, which block is open is read from the trail: every C_LINE record
/// up to the PC's, replayed as a stack of open blocks (so a `for` header, which raises the level without opening a block in every sccz80
/// before z88dk#3254, and a `for` that does open one, are both right). Other compilers keep the block rule above. Every stack variable
/// that shares bytes with another visible one is marked (`CLocalRef::shares`).
std::vector<CLocalRef> c_visible_locals(const VariableStore& store, int function, const CScope* scope);

/// The CPU registers of frame 0.
struct CRegs {
    uint16_t af = 0, bc = 0, de = 0, hl = 0, ix = 0, iy = 0;
};
struct CRegRead {
    bool ok = false;
    std::vector<uint8_t> bytes;   ///< low byte first: `[c,b]` is C then B
    std::string note;             ///< "in BC; may be stale outside its live range"
    std::string why;              ///< !ok: "optimised out", "not available (register, outer frame)", an unknown name
};
/// An `R` record's value: names a b c d e h l ixl ixh iyl iyh; `[]` is optimised out; `regs` null (an outer frame)
/// is not available.
CRegRead c_read_register_local(const CdbVar& var, const CRegs* regs);

/// The refusal text of z88dk U1 (`eval_var` returns it for a parameter of a function `c_fastcall_records_nominal()` holds for).
inline constexpr const char* kCU1Text = "location unknown (z88dk U1: sccz80 records a fastcall parameter at its nominal slot)";

/// GH #438, z88dk U1, per FUNCTION (a per-record test cannot see that sccz80 after z88dk#3229 records one register
/// parameter below the frame and still the others at their nominal slots): true iff `function` is fastcall and none of its
/// parameters (level 0, space B) is recorded at a negative offset, i.e. the records are the pre-#3229 ones, every parameter at
/// its nominal slot, where none of them is. Fastcall is `CConv::ListedFastcall`, or `prologue_fastcall` (the caller read the
/// entry bytes `call l_debug_push_frame; push hl` and no listing names the function). False for a function with no such parameter.
bool c_fastcall_records_nominal(const VariableStore& store, int function, bool prologue_fastcall = false);

}  // namespace dbg
}  // namespace jnext
