# 7.10 C frames and values

[7.9](09-c-variables.md) holds the records, the storage and the scopes. This page is what
sits on them: the **C frames** at a stop (`Debugger::c_frames()`), and the rule that keeps the
call-stack tracker running for them. Reading a value (the formatting layer, the type lookup, the C
paths, the facade that returns rows) is the next stage of the epic and is not described here yet.

## The frame base

Every compiler keeps a frame base `fp`, the address of the saved frame pointer. Write `E` for the
SP at the function's entry instruction (the address of the return address): then `fp = E - 2`, for
all three compilers, and the `B` records of a variable are offsets from `fp`.

| Compiler | How `fp` is kept | Evidence |
|---|---|---|
| sccz80 (2.4 and master) | `call l_debug_push_frame` at the entry stores `E - 2` in `__debug_framepointer`; the caller's `fp` is at `[fp]` | `z88dk/libsrc/l/sccz80/8-8080/l_debug_push_frame.asm`; the `vars.c` listing of `master/sccz80-cvars` |
| zsdcc | `push ix; ld ix,0; add ix,sp` (`DD E5 DD 21 00 00 DD 39`), so IX = `E - 2`; `[IX]` is the caller's IX | the `vars.c` listing of `master/sdcc-cvars` |
| 80cc | per function: some use IX, some `l_debug_push_frame` | `master/80cc-cvars` (`sum` and `main` IX, `other` push-frame) and `master/80cc-fc` |
| banked | `banked_call` pops its own return address and `call l_jphl`s into the function, so `E` holds the return address into `banked_call` and the arguments sit above it | `z88dk/libsrc/target/zxn/far/zxn_banked_call.asm` |
| the epilogue | after `call l_debug_pop_frame` the variable already holds the CALLER's `fp`; after `pop ix` IX is the caller's. SP is `E` only right after that instruction: a `__z88dk_callee` routine pops its arguments between it and the `ret` (fc listings of all three compilers), so from the next instruction on the base is not known without the tracker | the same listings |

The helpers (`__debug_framepointer`, `l_debug_push_frame`, `l_debug_pop_frame`) are joined by
public name in `CFrameHelpers` (`struct CFrameHelpers`, `variable_store.h:99`), by the adapter
(`helpers.framepointer`, `z88dk_source_map.cpp:533`), never from the symbol table: a
hand-loaded map attaches no symbols. They are unresolved, with a reason, in a zsdcc map.

## Which function a PC is in

A function's **extent** is a list of pieces (`CPiece`, `variable_store.h:56`). `fill_extents()`
(`z88dk_source_map.cpp:327`) bounds the extent above by the lowest map value, in the same section,
of another function's entry, a symbol or line record of another module, or the section's
`__<section>_tail`; then it splits `[entry, bound)` at the 8 KB logical boundaries and places each
piece with the rule the symbols use. A function that crosses a slot lives in two spaces (a NEX code
group is placed slot by slot: page 4 for `$8000`-`$9FFF`, page 5 for `$A000`-`$BFFF`). With no bound,
`pieces` is empty and `extent_why` says so: the function then contains no PC.

`VariableStore::locate()` (`variable_store.cpp:50`) answers "which function, and how far into it" from a
`SourceAddress`: the piece with the same space whose range holds the address. Nothing else is
used, neither the nearest symbol nor a jump target. The scope tables follow the same rule
(`fill_scopes()`, `z88dk_source_map.cpp:376`): a record is filed under the function of its module and
section whose entry **value** is the greatest at or below the record's, and each `CScope` carries its
`offset` from the entry, so tables compare across pieces. Comparing spaces would lose the tail of a
function that crosses a slot (`cross.c`, in the `*-cross` fixtures).

## The frames

`Debugger::c_frames()` (`debugger_cframes.cpp:30`) returns the frames newest first. Frame 0 is where
the CPU is; the `CFrame` (`struct CFrame`, `debugger.h:1228`) has its function (an index into
`variables()->functions()`, -1 for assembly), the `pc`, the source line and the base, with how it was found:

- **Tracked.** With the tracker's frames (oldest first, `n` of them), frame `k` was entered by
  `frames[n - 1 - k]`, whose `sp_at_call` is `E`, so `fp = sp_at_call - 2`. The PC of an older frame is
  the CALL that is in progress (`caller_pc` in `caller_space`), which is inside the call, never the return
  address. The function is the one that contains that PC, **never** the `target_pc`: the banked case's
  target is `l_jphl`. An INT or NMI frame fits the same rule.
- **Prologue** (frame 0, no tracked frame): the entry bytes decide. A call to `l_debug_push_frame`: at the entry
  `fp = SP - 2`, right after `call l_debug_pop_frame` the same, otherwise `peek16(__debug_framepointer)`.
  The IX form: at the entry `fp = SP - 2`, after the `push ix` and after the `ld ix,0` `fp = SP`, right after
  `pop ix` `fp = SP - 2`, otherwise IX. "Right after" is tested from the bytes before the PC, so a long function costs nothing. Between the restore of
  the caller's base and the `ret` (found by walking the function's code from its entry, `in_epilogue()`; a `ret` closes
  the window), any other PC is `Unknown` with a note. The code is `Unknown` too when it cannot be read (`Epi::Unsure`, a defensive state that no fixture reaches and no row covers); a walk that steps over the PC without landing on it is taken as outside the window.
- **Chain** (older frames): `ret = peek16(fp + 2)`, the next PC is `ret - 1`, and the next `fp` is `peek16(fp)`
  only when the next function keeps its frame in the same style, and that style is a push-frame or an IX frame (a function with neither saves no frame pointer). A change of style ends the walk, the frame
  listed `Unknown` with a note (80cc mixes both styles); a next PC in no C function is listed as assembly and stops
  the walk; so does a next `fp` that is not a stack address above this one.
- **Unknown**: none of the above applies, `note` says why.

Each frame has a `floor`, the lowest stack address that holds its data: `SP` for frame 0, the inner call's
`sp_at_call + 2` for an older frame. A `B` slot at `fp + offset`, `offset < 0`, is allocated only when it is
at or above the floor. "Outer frames are always allocated" is wrong: a stop inside `l_debug_push_frame`, or in
any assembly routine a function calls before it allocates its locals, leaves that function's locals unallocated.

## Who keeps the tracker running

The tracker (`CallStack`, [7.5](05-breakpoints-steps-history.md)) costs per instruction, so it is not
always on. Its state is **derived** (`Impl::sync_call_stack()`, `debugger_session.cpp:128`), the one
writer, from two things:

1. **A client's own request.** `Debugger::set_call_stack_enabled(by, on)` (`debugger_inspect.cpp:906`) sets
   the request on that client's row. The tracker is on while any live client asked. One client switching off
   no longer takes another's tracking away (the Qt debugger window closing used to empty a ZRCP session's
   extended stack), and a detach drops the request with the client.
2. **The source map.** With no request, tracking is on while the live source map has C variable records and a
   live non-observer client is attached (the Qt emulator window's permanent observer does not count).

`sync_call_stack()` reads the client list and the live source map directly (never `recheck()` or the
replay-guarded `clients_attached()`), so it cannot recurse, and it runs wherever an input changes: a request,
an attach or detach, `recheck()`, a source map loaded or cleared, the program sidecars, a reconstruct.
`CallStack::set_enabled()` empties the tracker only on a change, and the backend writes it only when the derived state changes (or, after a reconstruct, whenever it is on): a write made outside the backend, directly on the `Emulator` (CTL-12-55, QEN-01), is not undone by an attach or an in-place landing that changes nothing derived. `call_stack_enabled()` returns the effective state.

## Formatting a value (the pure layer)

`src/debug/c_value.{h,cpp}` turns a type chain and bytes into text and knows no machine: a `CReader` callback
supplies bytes (the CPU view for the stack and for pointer targets; the caller picks), so every rule is pinned with
synthetic memory in `c_value_test` (`CVALF-*`). Reading a value on a running machine through the page-aware
identity, and the facade that returns rows, are the section after this one.

**Formatting is locale-independent**: every number goes through `std::to_chars`, never a printf-family call or an
ostream (QApplication runs `setlocale(LC_ALL, "")`, and on a Spanish host `%g` prints `1,5`; `CVALF-09`).

| Type (decoder) | Text |
|---|---|
| `SC` | `65 'A'` for 0x20..0x7E, else the number; signed or unsigned by `:S` / `:U` |
| `SI`, `SL`, bare `:S`/`:U` | little-endian, `{size}` bytes (1, 2, 4, 8), decimal |
| `SF` `{6}` | z88dk's 48-bit float: five mantissa bytes (sign in bit 7 of the fifth, implied leading 1) then an exponent biased by `$80`; 12 significant digits (`c_format_float48()`, `c_value.cpp:411`). Oracle: the bytes z88dk compiled in `*-floats` |
| `SF` `{4}` | IEEE 754 binary32, shortest text that reads back. The SIZE decides, not the noun |
| `S6` | IEEE 754 binary16 through float |
| `SQ` | `<fixed-point $xxxx: layout not decoded>` |
| pointer | `$8D21`, `$0000 (NULL)`, ` <name>` of a variable or function mapped there, and for a `char *` its string (at most 32 bytes, escaped, `...` when cut) |
| array | the count is the `DA<n>`, never the byte size (a `DA4x,SI` is 8 bytes and 4 elements); at most 16 elements, then `...`; a char array reads up to its NUL when what precedes it is text, else (`uint8_t btable[4]`, the CDB cannot tell it from a `char` array) it is a list of numbers |
| struct | `{x = 3, y = 4, tag = 5}`, nested, from the `T:` record |
| zsdcc bitfield `SB<s>$<w>` | `(value >> s) & mask` |
| sccz80 / 80cc overlapping members | the storage unit's value with `bitfield layout unknown (z88dk U2)`: in a struct whose members are not all at offset 0, a member sharing bytes with another (a union is not one) |

`c_find_type()` (`c_value.cpp:498`) finds a struct's `T:` record: the records of the variable's own scope first (zsdcc's
copy per module), then those of its module, then a tag that is unique across all records (sccz80 and 80cc: one per
header). Different definitions at the deciding step are ambiguous, never first-wins. `c_type_text()`
(`c_value.cpp:491`) prints `char`, `unsigned int`, `struct node *`, `int [2][3]`, `int (*)()`; the CDB has no
union marker, so a union is `struct word`, and no typedef name survives. A zsdcc padding member (`__00000000`) is
not shown.

**A struct's size** is a declared object's when there is one: an `S:` record, or a struct member, whose chain is
exactly `ST<tag>` (`{2}STflags` for `g_flags`). With none, it is the members' extent, provided no member shares bytes
with another (a union is fine). sccz80 and 80cc write a bitfield as an int that sticks out of the struct (z88dk U2):
`struct flags` is 2 bytes but its member `hi` is an int at offset 1, so for such a struct the members' extent
overstates the size and is not used: with no declared object it is refused as `size unknown (z88dk U2)`. Every view of
such a member (the whole struct, a member path, a pointer, an element `p[1]`, a member of a nested struct) clamps its
read to the struct that holds it and zero-fills the overhang, so `hi` is 200 for the bytes `8D C8` whichever way it is
reached, and no read goes past that struct.

`c_parse_path()` (`c_value.cpp:581`) parses the C paths: `['*'] primary {'.' name | '->' name | '[' index ']'}`,
the primary a name, `fn::name`, `file.c::name` or `file.c::fn::name` (the file names which `fn` when two modules have a function of that name), the index decimal, `0x<hex>` or `$<hex>` with no sign. As in C the
postfix operators bind tighter than the `*`: `*x[1]` is `*(x[1])` and `*head->next` is `*(head->next)`.
`c_eval_path()` (`c_value.cpp:682`) walks the steps over a typed location (an array index is checked against its
count, a pointer index scales by the element size, the size of a pointed-to array is its whole size, a null pointer is
refused), then applies the `*`, and `c_read_loc()` reads and formats it. Every refusal says why.

`c_visible_locals()` (`c_value.cpp:781`) lists which locals show at a PC: the parameters (ordered by stack position),
the locals, the static locals. A parameter is a record of level 0 (sccz80, 80cc), at a stack offset >= 4, or (zsdcc) in
a block below the body's first block (the entry scope's), where its register and fastcall parameters sit. Parameters
and level-1 locals are always visible; a local of level >= 2 is visible iff its block is the open block or one of its
ancestors. A block's level is its locals' own; a block that holds no local (the `else` of an `if` that declared one)
takes the level of the first scope entry, in offset order, that names it (sccz80 numbers blocks in pre-order, so a
block just opened is the last opened one). The open block is the greatest known block at the scope's level at or below
the scope's block, which turns sccz80's "last opened block" into the block that is open and keeps a closed sibling's
locals hidden; its ancestors are found over the same set. With no block data (80cc) or an unknown scope, every block
local is shown, marked.
`c_read_register_local()` (`c_value.cpp:862`) reads an `R` local of frame 0 (`[c,b]` is C | B << 8, with a note
that it may be stale), refuses `[]` as optimised out and an outer frame as not available.
`c_param_location_unknown()` (`c_value.cpp:902`) marks a listed `__z88dk_fastcall` parameter recorded at an offset
>= 0 (z88dk U1).

## Reading values: the facade

`Debugger` has six CAP-SRC readers (`debugger.h`), all `const`, all running `recheck()` first (through `variables()` and
`c_frames()`) and bumping nothing themselves, so a panel redrawing on `state_revision()` does not redraw on a read:

| Reader | Gives |
|---|---|
| `c_variables_status()` (`debugger_cvalues.cpp:398`) | `{available, why}` with a printable reason: `no source map is attached`; `an SLD source map carries no C variables`; `the map has no C debug records` (lines from listings only); `zsdcc wrote no C variable records for this build` (`zcc -O0` keeps the `C_LINE` records and writes no `__CDBINFO__`, fixture `master/sdcc-cvars-O0`); `the map has line records but no __CDBINFO__ records`; `all N C variable records were refused: <reason>`; a stale map |
| `c_locals(frame)` (`debugger_cvalues.cpp:434`) | the parameters (by stack position), the locals visible at the frame's PC and its static locals, each a `CValueRow`; refused, with a reason, for a frame past the end, an assembly frame or a frame whose base is not known |
| `c_globals()` (`debugger_cvalues.cpp:466`) | every global, file static and static local, ordered by file then name, with a `path` no local can shadow: `vars.c::s_dup`, `sum::calls` (`a.c::init::done` when two modules have a function `init`) |
| `read_c_value(frame, path)` (`debugger_cvalues.cpp:496`) | the object a C path names in that frame: `ok` or `why`, the type text, the value text, `notes`, the typed location `loc`, `where`, `integer` (Char, Int, Long, Scalar, Bitfield and Pointer: the address) and `real` (the floats) |
| `c_children(frame, path, first, max)` (`debugger_cvalues.cpp:C_CHILDREN`) | what a tree lists under an object: a struct's members, an array's elements `first`..`first + max` (`total` is the element COUNT), a pointer's pointee (a struct's members as `p->x`, anything else as `p[0]`, named `*`); `void *`, a function pointer and a scalar have none (`ok`, `total` 0). Each row's `path` is the parent's plus a suffix and round-trips through `read_c_value`; a row is read, placed and noted exactly as `read_c_value` would, so a banked global's elements carry its page. `!ok` gives `read_c_value`'s own reason, or `null pointer` / the unreadable address |

The children come from the pure `c_children()` (`c_value.cpp`: `CChild`, `CChildren`): a struct's visible named members in record order
(an unnamed C11 member has no path and is left out), an array's elements scaled by the element size, one pointee for a pointer.

A `CValueRow` carries `name`, `path` (which round-trips through `read_c_value`), `file`, `type`, `kind` (Param, Local,
StaticLocal, FileStatic, Global), `where`, `ok` with `value` or `why`, `aggregate` and `notes`. A failed read has an empty
`value`, never `0`. `where` tells a page address (`Page`: the space and the 16-bit logical address, also
`DivMmcRam`) from a CPU address (`Cpu`: a stack slot, what a pointer points at) and from registers (`Register`: `BC`).

Where an object is read from (`eval_var()`, `debugger_cvalues.cpp:140`):

- **A global, file static or static local** is read from the page its map symbol was placed in (`CStorage::at`; the reader is
  `space_reader()`, `debugger_cvalues.cpp:104`), whatever the MMU maps there now, and an element or member of it too. A note says
  `page 20 is not mapped at $C000 now` when it is not mapped. A value that would cross the end of its 8 KB page is refused (`the value crosses a page boundary`, with no value: a failed read is never `0`).
- **A stack variable** is at the frame's base `fp` plus its record's offset, through the CPU view; a negative offset below the frame's
  floor is `not allocated`.
- **A register local** is frame 0's register, with the note that it may be stale; in an older frame it is `not available (register,
  outer frame)`; `R []` is `optimised out`. A pointer held in a register can be followed (`*p`, `p->x`, `p[i]` start at the pointed-to
  object, read through the CPU view, `CLoc::ptr_value`); a member or an element of a register-held aggregate has no address and is refused.
- **What a pointer points at** is read through the CPU view (C semantics), which is why a location carries `rooted`: true while it is
  inside its root object, cleared when a pointer is followed (`CFormatCtx::read_root`).
- A listed `__z88dk_fastcall` parameter recorded at an offset >= 0 is `location unknown (z88dk U1...)`.

`find_var()` (`debugger_cvalues.cpp:303`) resolves a name in frame `k`: the frame's visible locals, then its function's static
locals, the file's statics, the globals. A local that exists but is hidden is `out of scope at <file>:<line>`. `fn::name` names a static
local from any frame, and a local of `fn` only from a frame of `fn`; `file.c::name` names that file's statics and globals.

## Fixtures and rows

`Z88CDB-41` to `-50` in `z88dk_map_test` (the extents against a golden that `derive-scopes.py --extents`
derives from the raw map lines, the `*-cross` fixtures, the helpers, the listing evidence for the calling
convention, the no-record facts, and the unchanged scope tables on the 12 maps of `Z88CDB-40`), and
`CVALF-01` to `-41` in `c_value_test` (with `-27a` and `-28a` to `-28d`), and `SRCDBG-CVAL-01` to `-58` in `source_debug_test` (the tracker policy, the frames of each compiler on a running fixture:
tracked, the fallback, the prologue and epilogue windows, a mid-run attach, banked, a function that crosses a slot; and the
values: the first call of sum on each compiler, the globals, the loop, paged globals with their note, the fastcall marker, C paths and
their refusals, bitfields, the status texts, the lifetime, a rewind, a dot command's global, order and hiding, and the facade's
contract, an unknown frame base, a stale map, an ambiguous function, page crossing and the label of an unmapped page, file-qualified static locals, a pointer in a register). The plan's row for an identity-only space (a value whose storage is in `BootRom`, `Layer2` and the like) is not built, and cannot be: `storage_of()` refuses any bound placement that is not a `Page` and `z88dk_section_space()` produces only `Page` and `DivMmcRam`, so no z88dk map can place a variable in such a space; the neighbouring contract, a failed read is never `0`, is reachable and rowed (a 2-byte global on the last byte of its page). A struct's size by module is `CVALF-37`, the integer and real a value holds `CVALF-38`, rooted reads `CVALF-39`.
