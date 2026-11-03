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
| the epilogue | after `call l_debug_pop_frame` the variable already holds the CALLER's `fp`, SP = `E` at the `ret`; after `pop ix` IX is the caller's | the same listings |

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
the CPU is; the `CFrame` (`struct CFrame`, `debugger.h:1226`) has its function (an index into
`variables()->functions()`, -1 for assembly), the `pc`, the source line and the base, with how it was found:

- **Tracked.** With the tracker's frames (oldest first, `n` of them), frame `k` was entered by
  `frames[n - 1 - k]`, whose `sp_at_call` is `E`, so `fp = sp_at_call - 2`. The PC of an older frame is
  the CALL that is in progress (`caller_pc` in `caller_space`), which is inside the call, never the return
  address. The function is the one that contains that PC, **never** the `target_pc`: the banked case's
  target is `l_jphl`. An INT or NMI frame fits the same rule.
- **Prologue** (frame 0, no tracked frame): the entry bytes decide. A call to `l_debug_push_frame`: at the entry
  `fp = SP - 2`, right after `call l_debug_pop_frame` the same, otherwise `peek16(__debug_framepointer)`.
  The IX form: at the entry `fp = SP - 2`, after the `push ix` and after the `add` `fp = SP`, at a `ret` after
  `pop ix` `fp = SP - 2`, otherwise IX.
- **Chain** (older frames): `ret = peek16(fp + 2)`, the next PC is `ret - 1`, and the next `fp` is `peek16(fp)`
  only when the next function keeps its frame in the same style. A change of style ends the walk, the frame
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
`CallStack::set_enabled()` empties the tracker only on a change. `call_stack_enabled()` returns the effective state.

## Fixtures and rows

`Z88CDB-41` to `-46` in `z88dk_map_test` (the extents against a golden that `derive-scopes.py --extents`
derives from the raw map lines, the `*-cross` fixtures, the helpers, the listing evidence for the calling
convention, the no-record facts, and the unchanged scope tables on the 12 maps of `Z88CDB-40`), and
`SRCDBG-CVAL-01` to `-17` in `source_debug_test` (the tracker policy, and the frames of each compiler on a
running fixture: tracked, the fallback, the prologue and epilogue windows, a mid-run attach, banked, and a
function that crosses a slot).
