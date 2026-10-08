# 3.10.2 The Qt debugger

The Qt debugger is the debugger window with its panels, menus and toolbar,
in **`src/debugger/`** (target `jnext_debugger`, built when `ENABLE_DEBUGGER=ON`
in a Qt build). It reaches the machine only through the backend:
`DebuggerManager` and `DebuggerWindow` are constructed on the `Debugger` alone,
and `debug_qt_free_test` fails the run if any file there includes a core-layer
header or names `Emulator` ([3.9](09-debug-and-the-debugger.md)). For what the
window looks like and how to drive it, see chapter 6 of the user guide,
*The debugger*.

## The adapter: two clients

`DebuggerManager` holds the loop owner's `Debugger`, handed over by
`MainWindow::set_debugger()`. It is two clients of it.

**The window's client** is attached exactly while the debugger window is open.
Opening the window attaches it, requests the live raster and turns call-stack
tracking on. Closing it resumes a paused machine — whoever paused it — and
detaches. Every verb the window issues is attributed to this client. It is not
attached for the life of the process, because an attach arms the machine, and a
window that armed breakpoints while closed would make `--persistent-breakpoints`
the default (`debugger_persistent_bp_test` PBPUI-03 pins the opposite).

**The breakpoints' owner** is a non-arming observer client, attached by
`BreakpointModel` (`src/debugger/breakpoint_model.*`) for the manager's
lifetime. The GUI's breakpoints are its subscriptions: single-address
`Execute`, `Mem` and `Port` subscriptions with no condition, `once = false` and
action `Stop`. READ, WRITE and READ_WRITE are one `Mem` subscription with an
access mask. An I/O address `00`-`FF` is a low-byte match (`port_mask 0x00FF`);
`0100` and up is exact (GH #222). Because the observer never detaches and arms
nothing, a breakpoint survives closing the window, fires with the window shut
exactly when something else arms the machine, and costs a closed window
nothing.

The Breakpoints panel, the disassembly gutter and context menu, and the window's
Breakpoints menu all edit this one model. The panel lists every client's
subscriptions; another client's are read-only and named by client id. The
master switch is the backend's (`set_master_enabled()`).

## The pause state is pulled

A pause the window did not cause still opens it. `check_breakpoint_hit()` reads
`state()` once per tick, after the pump, and brings the window to it in both
directions. A pause it has not shown gets the pause-edge sequence, opening the
window first if it is closed: a breakpoint, a magic breakpoint, a persistent
breakpoint, another client's pause. A resume it did not cause gets the running
sequence. What the window last showed is the one piece of state the adapter
keeps for this, and it is presentation state, not a copy of the machine's.
`apply_pause_state(bool)` is the one place both sequences live.

The listener records and returns: a listener runs inside `pump()` and does no UI
work. A breakpoint change another client made arrives as
`SubscriptionsChanged`; the model records it and the tick publishes it.

## Panels

`DebuggerWindow::create_panels()` creates fifteen:

| Panel | Reads | Writes |
|---|---|---|
| CPU Registers | `registers()`, `paging_ports()`, `ula_screen_regs()` | — |
| MMU | `mmu_slots()`, `paging_ports()` | — |
| Disassembly | `disasm_one()` over `memory_reader()`, with symbols, a breakpoint gutter and a selection you can copy as assembly | breakpoints, through the model |
| Memory | `peek(MemSpace::cpu())`, or a slot's or NR page's backing store | `poke` |
| Stack | the words at and above SP, through `peek` | — |
| Call Stack | `call_stack()`, a shadow stack built from SP deltas plus a frame per accepted INT or NMI | — |
| Watches | byte, word or long at user addresses, through `peek` | — |
| Breakpoints | every client's subscriptions, through `BreakpointModel` | the GUI's own |
| Video | `raster()` in all four counter domains plus the ULA fetch phase, and `render_layer()` for each of the eight views | — |
| Sprites | `sprites()` | — |
| Copper | `copper()` | — |
| NextREG | `nextreg_peek()` for the whole register file | `nextreg_write()` |
| Audio | `ay_registers()`, `turbosound_enabled()`, `ay_mode()`, `stereo_mode()` | `set_audio_mute_mask()` |
| Script | the `ScriptHost`'s loaded scripts, their rules and hits, the verdict line and the script log | load, reload, unload, record |
| Source | `source_location()` for the PC, and the source file it names | the source steps and loads, through `DebuggerManager` |

Four panels (CPU, Disassembly, Stack, Call Stack) update only while paused:
reading the register file every frame while the machine runs produces a blur,
at real cost. Everything else refreshes on `DebuggerManager`'s timer, except
the two views of the breakpoints.

**The breakpoint views follow the model.** The Breakpoints list and the
disassembly gutter follow `BreakpointModel::changed(kinds)`, so a breakpoint
appears the instant it is set. `kinds` is the set of event kinds whose *listed*
state changed, found by diffing the backend's `subscriptions()` listing against
the last one published: the list acts on any kind, the gutter only on
`Execute`. A transient subscription (Step Over, Run to Here) is not listed and
notifies nobody; a master-switch flip notifies every kind. Every model mutator
publishes before it returns, so a new call site cannot forget to repaint. The
backend's `SubscriptionsChanged` push carries the *live* kinds, not the changed
ones, which is why the model diffs rather than trusting it.

**Writes are mutations.** A NextREG edit is `nextreg_write()`, so the register's
own handler runs exactly as for `NEXTREG nn,n`. A Memory hex edit in the CPU
view is `poke(MemSpace::cpu())`, which writes through the live map. Both are
attributed to the window's client — to no client while the window is closed;
`DebuggerManager::set_panels_client()` keeps the three writing panels told —
and each produces a `MUTATE` line. Both are refused while an RZX records or
plays: the NextREG cell shows the register's value again at the next refresh,
and the Memory byte does not change.

**A refused rewind says why in the status bar.** `DebuggerManager` is also a
`Listener` of the window's client, installed at attach and removed before
detach, and only its `on_log` does anything. It records a line that starts with
`REWIND REFUSED: ` ([3.9.1](09-1-the-backend-api.md)), minus that prefix and
the backend's ` [client N]` tag, and does no UI work in the push.
`on_step_back()` and `on_rewind_to_frame()` serve Step Back, Frame Back and the
slider. Each first clears the recorded line and any refusal still on show
(`DebuggerWindow::clear_rewind_refusal()`), because that refusal described the
previous verb. Each then calls the verb, and on a benign refusal hands the
recorded line to `DebuggerWindow::show_rewind_refusal()`.

That puts it in the debugger window's status bar for `kRewindRefusalMs`
(10 s), with no modal. Meanwhile `update_rewind_ui()` leaves the status bar
alone, instead of writing its rewind status line there on every tick.
`RefusedCorrupt` is reported differently: a "Rewind Failed" modal and a line in
the main window's status bar. `debugger_verbs_test`'s `QRF-*` rows pin each
reason's text, the hold and the clear.

**The Memory panel's slot view reads the physical backing store.** For a slot it
uses the `space` and `space_offset` that `mmu_slots()` reports — `Page{nr_page}`
for a RAM slot, `Rom{...}` for a ROM slot, whose `poke` is refused — at
`space_offset + (addr & 0x1FFF)`. It never composes a space from
`effective_page` and `is_rom`. So an overlay over the slot (DivMMC, Multiface,
Layer 2) does not show in it, and its "Page..." item reads any NR page, mapped or
not.

**The Video panel draws through the backend.** Each layer view is
`render_layer()` ([3.9.1](09-1-the-backend-api.md)). The panel keeps the
presentation: the `QImage`, the dark "not yet rendered" rows, the checkerboard
under every alpha-0 cell, the scaling, the red raster line and the titles. Its
raster block shows `hc`/`vc`, `hc_ula`/`vc_ula`, `cvc` and `phc` side by side,
each labelled with the VHDL signal it mirrors; `cvc` is labelled as what NR
0x1E/0x1F report. The derivation lives in `src/debug/raster_state.*` and takes
the live `VideoTiming` by reference, so there is no second table of raster
constants in `src/debugger/`.

## The key bindings are data

No shortcut in the debugger window is written at its call site. The inventory is
a table in `src/debug/debug_keymap.cpp` — each action with a config-file id, a
label and a compiled-in default — and `DebuggerWindow::apply_keymap()` is the
one place that pushes a `jnext::dbgkeys::Keymap` onto the `QAction`s. It also
rewrites every toolbar caption and tooltip that quotes a key, so `F5: Continue`
is generated from the binding rather than typed next to it.

**The model is Qt-free and lives in `jnext_debug`**, not in `src/debugger/`,
because `src/gui/` has to read and write the same table with
`ENABLE_DEBUGGER=OFF`: `AppConfig` must round-trip `[debugger_keys]` in a
debugger-less build, or the Preferences dialog would wipe a user's bindings. And
not in `src/gui/`, because `ENABLE_QT_UI=OFF` with `ENABLE_DEBUGGER=ON` is a
real build combination. The Qt conversions sit in the header-only
`src/qt/debug_keymap_qt.h`.

**The vocabulary is bounded.** `Key` is an enum of function keys, letters,
digits and named keys, not a mirror of `Qt::Key`. That lets `parse_combo()`
refuse a value by name, makes `render_combo()` canonical, and makes the grammar
testable without a widget. `validate_combo()` refuses the combinations that
would break something else: anything without Ctrl, Alt or Meta that is not
`F1`-`F12` (the Memory panel types hex with bare keys), `Alt`+letter (the menu
bar's), `Alt`+`1`..`8` (the script host keys), and `Ctrl+C` / `Ctrl+A`.

**A chord both windows bind is a warning, not a conflict.** `Qt::WindowShortcut`
matches against the active window, and each window keeps its own binding, so a
chord the emulator window binds (`harvest_host_chords()` reads them off the real
`QAction`s) is accepted with a warning. Two actions in *one* window's map are a
conflict: Qt dispatches identical sequences round-robin and breaks both. The
Preferences tab refuses such a capture. `build_keymap()` resolves a hand-edited
file deterministically — explicit beats default, the earlier action beats the
later, the loser is left unbound — and every refusal becomes a `LoadIssue` that
`MainWindow` logs at error level and the tab lists.

`MainWindow` keeps the map in effect in `debug_keys_`. Both its forwarding of
the five execution keys (`run`, `pause`, `step_into`, `step_over`, `step_out`)
and the push into `DebuggerWindow::set_keymap()` read that member, so the two
windows cannot disagree.

## Symbols

The Map menu loads into the backend's one table with `load_map()`, and the
Disassembly, Call Stack and Breakpoints panels read `symbols()`. A breakpoint set
on a symbol keeps its name in the list, and a symbol any other client loads
shows in the panels too. `src/debug/symbol_table.*` reads z88dk maps
(`load_z88dk_map()`), plain `SYMBOL = $ADDR` lists (`load_simple_map()`) and
NextBuild `Memory.txt` files (`load_nextbuild_memory()`); the address fields of
the breakpoint and watch dialogs go through `SymbolTable::resolve()`.

## Source

The Source tab (`source_panel.*`) shows the PC's line from the backend's source
map and carries the source-step buttons; it holds no state of its own beyond
the loaded file and its modification time. Its buttons are not menu items and
have no key binding: the menu tree and the bindings are pinned by
`debugger_accel_test` and `debugger_keymap_test`. `BreakpointModel` lists an
Execute subscription with a page qualifier as the GUI's own row (`Row::page`);
the Breakpoints panel creates one from a `file:line`, and the gutter draws it
only while its page is mapped (`pc_marked()`). The Call Stack's last column
looks up each frame's caller on its recorded page.

## Copying out of a custom-painted panel

The Disassembly panel paints itself, so there is no Qt text widget to inherit a
selection from. The selection is an **address range** — an anchor and a cursor —
not a pair of line indices, because the painted line list is rebuilt by every
scroll and refresh while an address survives all of them. The copied text comes
from `src/debug/disasm_text.*`, Qt-free, which re-disassembles the selected
range from live memory rather than reusing the painted lines, so a selection
copies in full after its lines have scrolled away. The same file holds
`apply_symbols()`, which the painter calls too, so the copied and the painted
text cannot drift. `debugger_disasm_copy_test` drives the real panel with real
events and reads the real clipboard.

## Tests

The Qt suites are gated on a Qt + debugger build and run in `make unit-test`:
`debugger_verbs_test` (the verbs, the pause edge and the rewind, trace and MAP
controls), `debugger_panels_test` (each panel's displayed text), `debugger_video_panel_test`, `debugger_audio_panel_test`,
`debugger_inspect_watchpoint_test`, `debugger_persistent_bp_test`,
`debugger_keymap_test`, `debugger_menu_test`, `debugger_disasm_copy_test`,
`debugger_quit_gate_test`, and the window-geometry suites. The regression rows
`qt-host-probe-func` and `qt-host-order-func` exercise `QtApp`'s hosting.
