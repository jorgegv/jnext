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
`Execute`, `Mem` and `Port` subscriptions (a `lo..hi` range on the first two, a port `mask`/`value`), and the
addressless `IntAck` and `Nmi` ones (the Interrupt and NMI types), with `once = false`, and a condition when
the dialog gave one. A
breakpoint stops: action `Stop` and no handler. A *count-only* one is action `Continue`
plus a handler of the model that counts. READ, WRITE and READ_WRITE are one `Mem` subscription with an
access mask. An I/O address `00`-`FF` is a low-byte match (`port_mask 0x00FF`);
`0100` and up is exact. Because the observer never detaches and arms
nothing, a breakpoint survives closing the window, fires with the window shut
exactly when something else arms the machine, and costs a closed window
nothing.

`BreakpointModel::Type` and the codec's `BpSpec::Kind` are one numbering (a
`static_assert` pins it), and the codec owns the type-name table (`bp_kind_name()`)
and the port rule (`bp_port_mask_for()`): the model has no copy of either. Another
client's `IntAck` or `Nmi` Stop subscription is a typed but read-only row, with an empty
Address.

**Hit counts need no backend counter** and follow the script engine's own split
(`account_static_stops` / `run_rule`). A Stop breakpoint stays a static Stop with no
handler (ZRCP's `other_breakpoint_at` and the step loops depend on that) and is counted
in the model's `on_paused()` from `PausedInfo::matched`, for its own ids only. A count-only
one counts in its handler, which captures the model (its destructor detaches the client,
which removes the subscription: SES-01). `extras_` holds what the backend cannot report
(which own ids are count-only), and `publish_()` prunes it and the counts with the listing.
`sync()` emits `hits_changed()` once when a count moved and the panel rewrites only its
Hits cells: never `changed()`, which would repaint the gutter every frame. A count dies
with its subscription (Edit = a new id) and is not saved. The `(type, address, space)`
API keeps meaning the plain breakpoint, and the gutters read plain members only, so a
count-only breakpoint at an address is a separate row. `QBPX-15` pins the GUI's counts to
the engine's `RuleView::hits` for the same rule.

**A breakpoint's identity is its full spec** (`BpSpec`: kind, address, range end, space, port mask, condition,
count-only). `add(const BpSpec&)` is idempotent on it, and a plain breakpoint and a conditional one at one address are
two rows. The `(type, address, space)` API and the gutters mean the *plain* breakpoint (`plain_()`: one address, the
address rule's mask, no condition). `extras_` (by id, own subscriptions only) holds what the backend cannot report:
count-only, the condition text and the shared slot its predicate writes a run-time error to. `type_of()` types a
subscription iff the dialog can make that exact one: another client's range or masked port is typed (so its Type and
Address texts read as before) but stays read-only, and its condition or handler stays typeless. A condition is compiled in
the kind's payload scope (`bp_scope()`, the checker's own `scope_of`) with `compile_expr`; the same
`bp_condition_problem()` check the dialog and the codec use refuses a text that does not read back as itself. A
run-time failure marks `hits_changed()` so the panel refreshes the Condition cell's tooltip.

**Run to Next Interrupt** (Debug menu, the `run_to_interrupt` keymap action, unbound by
default) is not a backend verb. `DebuggerManager::on_run_to_interrupt()` arms a
*transient* `IntAck` Stop subscription and calls `run()`: the transient is hidden from the
panel, exempt from the master switch, and dropped by the backend at the next stop or an
explicit `pause()` (the DeZog "arm, then continue" contract in `Impl::run_verb`). A refused
`run()` unsubscribes it. Rows: `QRTI-*` in `debugger_verbs_test`, `QKM-RTI-01` in
`debugger_keymap_test`.

The model also converts its own rows to and from the breakpoint file codec
(`script/bp_script.h`, see [3.10.6](13-the-debugger-scripting-language.md)):
`script_specs()` returns them as `BpSpec` in list order, and `add_specs()` adds a
file's breakpoints to the current set, applying the file's On state to each and
reusing a source-line group already armed on the same places (`add_source_line()` is
not idempotent). A source-line row is saved by the text it was set with, which the
model keeps per group (`group_lines_`) and the codec checks against the source map's
resolution. The manager's `on_save_breakpoints_script()` / `on_load_breakpoints_script()`
behind the Breakpoints menu's *Save as Script...* / *Load from Script...* do the file
dialogs, the write (`QSaveFile`) and the warning boxes; the last path is
remembered for the session only. Rows: `QBPJ-*` in `debugger_verbs_test`.

The Breakpoints panel, the disassembly gutter and context menu, and the window's
Breakpoints menu all edit this one model. The panel lists every client's
subscriptions; another client's are read-only and named by client id. The
master switch is the backend's (`set_master_enabled()`).

**The emulator window's Pause client** is a third Qt client, owned by
`MainWindow` rather than the adapter, so it exists in a Qt build with no
debugger too. It is a non-arming observer, attached lazily on the first press of
Machine > Pause (Alt+U or the Pause key) and detached with the window. With the debugger window
shut, the action pauses and resumes through this client; with it open, the action
calls the adapter's `on_pause()` / `on_run()`, the same verbs as the forwarded F9
and F5 keys. In a build with no debugger it is also the only way, from the GUI, to resume a
pause made by a magic breakpoint or a script.

## The pause state is pulled

A pause the window did not cause still opens it, with one exception: a pause
whose `by` is the emulator window's Pause client
(`set_emulator_window_pause_client()`; matched on the client id, so another
Gui or observer client still opens it) is exactly what that control exists to
make without the window. `check_breakpoint_hit()` reads
`state()` once per tick, after the pump, and brings the window to it in both
directions. A pause it has not shown gets the pause-edge sequence, opening the
window first if it is closed: a breakpoint, a magic breakpoint, a persistent
breakpoint, another client's pause. A resume it did not cause gets the running
sequence. What the window last showed is the one piece of state the adapter
keeps for this, and it is presentation state, not a copy of the machine's.
`apply_pause_state(bool)` is the one place both sequences live.
`MainWindow::sync_pause_state()` pulls the same `state()` once per tick, from
`QtApp::TickEffects::post_frames()`, for the Pause checkmark and the status
bar's Paused cell, with or without the debugger built in.

The listener records and returns: a listener runs inside `pump()` and does no window-wide UI
work. A breakpoint change another client made arrives as
`SubscriptionsChanged`; the model records it and the tick publishes it. `Reset` records a
restart of the CPU panel's T-state count and draws that one label.

## Panels

`DebuggerWindow::create_panels()` creates eighteen:

| Panel | Reads | Writes |
|---|---|---|
| CPU Registers | `registers()`, `paging_ports()`, `ula_screen_regs()`, `time()` (`tstates_total`), `time_epoch()` | `set_register()`, one register per call: an overlay `QLineEdit` over the value labels, a click on a flag (`reg_edit.h`); a click or the menu on an address-like value asks the window to show it (Disassembly, Memory, Source) |
| MMU | `mmu_slots()`, `paging_ports()` | — |
| Disassembly | `disasm_one()` over `memory_reader()`, with symbols, the PC line's hint (`disasm_hint::pc_hint()` over `registers()`, `memory_reader()` and `operand_reader()`), where the PC goes (`disasm_hint::pc_transfer()`, the destination marker), Follow (`disasm_hint::follow_target()`) and Back/Forward (`NavHistory`, `nav_history.h`), columns Mem, Addr, BP column (the breakpoint gutter), [Label], mnemonic, opcode bytes, and a selection you can copy as assembly | breakpoints, through the model |
| Memory | `peek(MemSpace::cpu())`, or a slot's or NR page's backing store; the whole view for Find, the block for Save | `poke`, one per chunk (`memtools::split_block`) |
| Stack | the words at and above SP, through `peek` | — |
| Call Stack | `call_stack()`, a shadow stack: a CALL, RST, INT or NMI opens a frame; a RET that lands on the newest CALL's return address (or the newest INT or NMI frame's interrupted PC) ends that frame, any other taken RET ends every frame whose return slot is below SP, and an esxDOS call the shim services ends its RST frame; a JP (HL)/(IX)/(IY) onto the newest CALL's return point, or taken with SP above its slot when the routine has stored no copy of its return address above SP (a callee prologue's PUSH or EX (SP)), ends that frame (sccz80's `EX (SP),HL` / `POP r; JP (r)` helpers and `l_case`; a `__z88dk_callee` body's internal jumps keep it); a routine that drops its return address and leaves by a direct JP keeps its frame until a later RET ends it by SP | — |
| Watches | byte, word or long at user addresses, through `peek`; an Expression row (`script::eval_expr()` at every refresh, paused or running; the dialog checks it with `compile_expr()` in the `None` scope); a C variable row (`read_c_value()`, `c_frames()`, `c_variables_status()`), read only while paused | — |
| Breakpoints | every client's subscriptions, through `BreakpointModel` | the GUI's own |
| Video | `raster()` in all four counter domains plus the ULA fetch phase, and `render_layer()` for each of the eight views | — |
| Sprites | `sprites()` | — |
| Graphics | `pattern_ram()`, `palette()`, `nextreg_peek()` (NR 0x43, 0x4B, 0x4C, 0x14, 0x6B, 0x6F), `peek(MemSpace::page())` of pages 0A/0B/0E, `active_ula_palette_bank()`, `palette_priority()` (the Palettes `Pri` column), `rgb333_to_argb()`; the decoders in `src/debug/gfx_decode.*` | — |
| Copper | `copper()` | — |
| NextREG | `nextreg_peek()` for the whole register file | `nextreg_write()` |
| Audio | `ay_registers()`, `turbosound_enabled()`, `ay_mode()`, `stereo_mode()` | `set_audio_mute_mask()` |
| Script | the `ScriptHost`'s loaded scripts, their rules and hits, the verdict line and the script log | load, reload, unload, record |
| Source | `source_location()` (a pointer into the store) for the PC, and the source file it names | the source steps and loads, through `DebuggerManager` |
| Symbols | `symbols().symbols()` (the toolchain's labels and the user's), `shown_name()`, `source_page()`, `source_map().lookup()`, `state_revision()` | — |
| Variables | `c_frames()`, `c_locals()`, `c_globals()`, `c_children()`, `c_variables_status()`, `call_stack()`, `mmu_slots()`: the C variables of a call frame and the globals, as a tree, a value editable in place through `write_c_value()` ([7.7](../07-source-level-debugging/07-frontends.md)) | — |

Five panels (CPU, Disassembly, Stack, Call Stack, Variables) update only while paused:
reading the register file every frame while the machine runs produces a blur,
at real cost. Everything else refreshes on `DebuggerManager`'s timer, except
the two views of the breakpoints.

**The CPU panel's T-state counter.** Where a count starts is a frontend choice, as with ZRCP's partial
counter (`get-tstates-partial` keeps its own base over `time().tstates_total`): the start point and the
Accumulate policy live in `jnext::TstateCounter` (`src/debugger/tstate_counter.h`, pure, no Qt), fed by
`CpuPanel::refresh()` on every paused redraw, so any stop counts, whoever caused it, and a redraw that moves no
time changes nothing. One mechanism detects a discontinuity: `Debugger::time_epoch()`, a token the Emulator
re-takes (from a process-wide counter, so a cold boot's new machine never repeats one) wherever time moves
without executing: `load_state()` (Step Back, a rewind in either direction, another client's
`load_state_bytes()`, a `.jns` restore, which runs through it), and a hard `init()` (a cold boot, a hard reset, every in-place
snapshot or program load, whoever or whatever started it). The counter restarts at the first stop that sees a
new epoch. A soft reset leaves the epoch alone: the total runs on through one. Two restarts are pushed because
the epoch cannot say where they start: `Reset` (a cold boot while running: counted from the reset, not from the
next stop) and opening the window (nothing is observed while it is closed), both through
`CpuPanel::restart_tstate_count()`. Rows: `TSC-01`..`TSC-11` (`tstate_counter_test`), `INS-07-04`..`INS-07-10`
(each epoch bump site, a remote client's included), `QPN-CPU-05`, `QTS-01`..`QTS-13`.

**Editing the CPU panel.** The value widgets stay `QLabel`s in their layouts (the rows that read the panel find
them there). An edit opens one `QLineEdit`, in no layout, over the label (a double-click, or the context menu's
Edit Value...); Enter commits, Escape and focus-out cancel, and `set_paused(false)` cancels without committing:
a value typed against one stop must not be written into the state after it. What a field is, how its text is
read (`SymbolTable::resolve()` for the 16-bit ones, the rule every address box uses), the flag toggle and the
refusal text are in `jnext::regedit` (`src/debugger/reg_edit.h`, pure, no Qt; `reg_edit_test`). A commit calls
`Debugger::set_register(client_, ...)`, one register per call (IFF is two), and only for a changed value: the
backend logs one `MUTATE` line and bumps `state_revision()`, so the next paused tick redraws every panel from the
register file, with no refresh path of the panel's own. The client is the window's (`set_panels_client()`). A
parse error or a refusal (RZX recording or playback) is the panel's `message()` signal, which the window shows with
`show_rewind_refusal()`: the one status message `update_rewind_ui()` does not rewrite. Rows: `REGED-01`..`08`,
`QREG-01`..`11`, `QREGW-01`..`04`.

**Navigation (Follow, Back, Forward).** `disasm_hint::follow_target(r, at, code, data)` (`src/debug/disasm_hint.h`, Qt-free,
beside `pc_hint()` and sharing its DD/FD prefix walk) says what Follow does on the instruction at `at`: a code address
(JP, CALL, JR, DJNZ and RST targets, a conditional's whether or not it holds; `JP (HL)/(IX)/(IY)`; RET, RETI and RETN
through `data`), a data address (the one memory operand `pc_hint()` reports) or nothing (`JP (C)` needs a port read, which no
debugger read does). The PC line reads its operand through `operand_reader()`, any other line through a `peek(Cpu)` reader (one helper, `follow_reader.h`, for the Disassembly and the Source tab).
History is `jnext::NavHistory<Loc>` (`src/debugger/nav_history.h`, pure; depth 100): the Disassembly's `Loc` is its top
line and caret, so Back is exact; the Source tab's is the address of the cursor line's first instruction mapped now. What
records is one rule: a jump the user (or another panel on their behalf) made through `show_address()`, the Address box or
Go to PC; the automatic re-centre on a stop, scrolling and the keys do not. The window's Follow, Back and Forward are
bindable actions (`follow`, `go_back`, `go_forward`; F12, Alt+Left, Alt+Right) installed by `apply_keymap()`, listed in the Go menu
(with Symbols, which raises the Symbols tab), acting on the Source tab when its text has the focus. A memory operand goes through
`show_in_memory_requested()` to `MemoryPanel::show_address()`, which leaves a slot or page view first. Rows: `FOLW-01`..`09`,
`NAVH-01`..`07`, `QNAV-*`, `QSRCN-*`, `QNAVW-*`.

**The Symbols tab** (`symbols_panel.cpp`) lists `symbols().symbols()` and rebuilds its rows only when `state_revision()`
moved; the greying of a paged label whose page is not what `source_page()` reports at its address is redone on every
refresh, because a page can be mapped with no revision change. It never jumps to an unmapped label (Show in Disassembly, Memory and Source are all disabled for one): the Disassembly and the
Source tab show the CPU's map. Rows: `QSYML-*`.

**Refresh cadence.** Running, `DebuggerManager::refresh_panels()` (`debugger_manager.cpp:1083`)
redraws the window on every `REFRESH_INTERVAL`-th tick (12, `REFRESH_INTERVAL`, `debugger_manager.h:393`: ~4 Hz). Paused, it
redraws only when a *stamp* changed since the last redraw (`paused_stamp()`, `debugger_manager.cpp:1072`; `PausedStamp`,
`debugger_manager.h:262`), and calls `update_actions()` on every tick, since their inputs (an RZX started from
the main window, the rewind buffer freed) have no verb. A paused machine with nothing happening used to redraw
everything, the Video tab's composited frame included, fifty times a second. The stamp is:

- the whole CPU register file, compared field by field (a direct core write behind the backend, as the tests and
  host actions make);
- the master clock, `time().master_cycle` (steps, run-to, rewinds);
- the backend's `state_revision()`, a counter that `Impl::bump_revision()` advances (31 call sites in
  `src/debug/`) for every MUTATE line of any client (`log_mutate()`, `log_mutate_range()`), a cold boot
  (`on_cold_boot_done()`), a landing `recheck()` applied, and the metadata verbs: `load_map()` and
  `clear_symbols()`, `load_source_map()`, `clear_source_map()` and `load_program_sidecars()`, every
  subscription verb (`subscribe`, `subscribe_source_line`, the group, enable and master switches), the
  trace, call-stack, coverage and rewind settings, attach and detach, and the magic and persistent
  breakpoints. A map or SLD load needs no special call: it bumps the revision, so the tick redraws;
- the host-input epoch (`input_epoch_`): `DebuggerManager::eventFilter()` (`debugger_manager.cpp:160`), installed on `qApp`
  while the debugger is enabled, counts key press and release, shortcut, mouse-button press, release and
  double-click, and wheel events anywhere in the application. It is the generic catch-all for host-side
  changes made on user input (the CPU-speed menu writing NR 0x07). Mouse *movement* is not counted: hover
  and Kempston mouse capture must not redraw a paused window;
- a flag, `source_file_changed`: whether the loaded source file's modification time differs from the one
  loaded (`SourcePanel::file_changed_on_disk()`, `source_panel.cpp:575`), so an edit on disk still shows.

`refresh_window()` (`debugger_manager.cpp:1063`) takes the stamp after the redraw, because a panel may itself touch the backend and
that must not read as a change at the next tick. A change of the Source tab's own view that moves nothing in the
machine (a colour scheme, `follow_pc()`) repaints the tab itself. `take_panel_refresh_count()` counts the
redraws; the `ticks:` line reports it as `panel-refreshes=`. The emulator window's half of the same issue,
the framebuffer generation and the upload gate that stops a paused machine re-presenting an unchanged frame,
is in [2.3](../02-architecture/03-a-frame-end-to-end.md).

**The window's opening size.** A window opening at its default size grows to the size where
nothing scrolls, if the screen has room (`DebuggerWindow::grow_default_size_to_natural()`,
`debugger_window.cpp:530`, queued from the first `showEvent()`); a size restored from the config file is left
alone. The size is *measured*: the scroll ranges are the amount of panel area out of view, and the left column
of tabs is widened by what its pages' `sizeHint()` lack, using the share of a widening the splitter really gave
it on the previous pass (`fit_last_add_`, `fit_last_lacking_`; bounded by `kMaxInitialFitPasses` = 4
(`kMaxInitialFitPasses`, `debugger_window.h:212`) and `kMaxWidthFitPasses` = 8 (`kMaxWidthFitPasses`, `debugger_window.h:239`)). The frame, not the client area, must fit the screen
(`jnext::clamp_window_size_to_screen`). A window manager can answer the fit's requests late and out of order,
so the fit is bounded in time as well: a resize to a size the fit asked for or started a pass from
(`fit_sizes_`, `fit_known_size_()`) is the manager's answer, not the user's, and the request is asked again,
at most `kMaxFitEchoes` = 6 times: the answer queues a pass, and the pass asks again and counts it, once
(`resizeEvent()`, `debugger_window.cpp:511-522`); any other size is the user's and ends the fit
(`end_fit_()`, `debugger_window.cpp:501`). The fit also ends by itself `kFitGraceMs` = 1000 ms after its last pass started (every
pass restarts that wait, `kFitGraceMs`, `debugger_window.cpp:549-554`), after which every size is the user's and the remembered sizes are
dropped; the grace does not end the fit while a pass is still queued (`fit_passes_queued_`, `queue_fit_pass_()`), so a stalled event loop cannot
cut the fit short between a pass and the one it queued. Rows: `GR-06` (a stale answer inside the grace window is asked again), `GR-07` (after it every size is the user's), `GR-08` (an earlier request landing while a pass is queued), `GR-09` (a user resize while the fit runs stands), `GR-10` (each late answer counts once: the 7th stands) and `GR-11` (a loop stalled past the grace window does not end the fit), `GR-12` (the bound holds on the pass's own path), `GR-13` and `GR-14` (the grace waits for the pass a re-ask, or a late answer, queued), in `debugger_window_grow_test`; `GR-09` to `GR-14` use a window no window manager sees.

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
under every alpha-0 cell, the scaling, the red raster line and the titles.
Paused at a frame boundary (`Debugger::at_frame_boundary()`, raster row < 0) it
renders all 256 rows with no raster line: `begin_new_frame()` has not yet
re-baselined the per-scanline change logs, so they still hold the frame that just
ended. Paused in the top vblank of a frame in progress, it shows the placeholder.
Its raster block shows `hc`/`vc`, `hc_ula`/`vc_ula`, `cvc` and `phc` side by side,
each labelled with the VHDL signal it mirrors; `cvc` is labelled as what NR
0x1E/0x1F report. The derivation lives in `src/debug/raster_state.*` and takes
the live `VideoTiming` by reference, so there is no second table of raster
constants in `src/debugger/`.

**Which memory an address is in.** Every view names the memory behind an address through
`debug/long_address.h`, by way of `src/debugger/memory_text.h`; `DebuggerWindow` holds one
`AddressStyle` (Debug > Physical Addresses) and hands it to each panel and to the `BreakpointModel`.
Which space each view prints, and the columns that were added, are described in
[7.7](../07-source-level-debugging/07-frontends.md).

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
bar's), `Alt`+`1`..`8` (the script host keys), `Ctrl+C` / `Ctrl+A`, and, where
the host modifier is Command, Command+`L` (Reload Last File).

**A chord both windows bind is a warning, not a conflict.** `Qt::WindowShortcut`
matches against the active window, and each window keeps its own binding, so a
chord the emulator window binds (`harvest_host_chords()` reads them off the real
`QAction`s) is accepted with a warning. Two actions in *one* window's map are a
conflict: Qt dispatches identical sequences round-robin and breaks both. The
Preferences tab refuses such a capture. `build_keymap()` resolves a hand-edited
file deterministically — explicit beats default, the earlier action beats the
later, the loser is left unbound — and every refusal becomes a `LoadIssue` that
`MainWindow` logs at error level and the tab lists.

**File > Reload Last File is not a keymap action.** The debugger window's File menu holds its
OWN `QAction`, carrying the host chord (`host_shortcuts::sequence()`, Alt+L; Command+L on
macOS). `DebuggerManager::set_reload_action()` hands the manager the emulator window's
`reload_last_action_`, and `DebuggerWindow::set_reload_source()` connects the two: triggering
forwards to that action, and `QAction::changed` mirrors its enabled state. So there is one
handler (the File > Open route, with the Recent Files bookkeeping) and one enabled state.
Two designs were rejected. One shared `QAction` would be invisible to the guards, which walk
`findChildren()` of the debugger window, and `jnext_debugger` cannot name `MainWindow`.
`Debugger::load()` is an in-place load with no Recent Files bookkeeping, and it drops the
`SidecarLoad`, so a refused map would only be logged.

`MainWindow` keeps the map in effect in `debug_keys_`. Both its forwarding of
the five execution keys (`run`, `pause`, `step_into`, `step_over`, `step_out`)
and the push into `DebuggerWindow::set_keymap()` read that member, so the two
windows cannot disagree. The five source-step actions are keymap actions too
(`source_step_into` and the rest), but they are debugger-window only: the
forwarding stays at the five instruction-level keys.

## Symbols

The Map menu also imports and exports skoolkit control files and Ghidra symbol files: see [7.4.7](../07-source-level-debugging/04-7-skoolkit-and-ghidra-exports.md).

The Map menu loads into the backend's one table with `load_map()`, and the Disassembly, Call Stack and
Breakpoints panels read `symbols()`. A breakpoint set on a symbol keeps its name in the list, and a symbol
any other client loads shows in the panels too. What each loader reads is
[7.4](../07-source-level-debugging/04-formats.md); the Qt side is only the naming:

- The panels name an address in the memory that supplies it now (`Debugger::source_page()`), through
  `disasm_text::symbol_namer()` for the disassembly; a Breakpoints row uses its own page when it has one,
  a Call Stack frame the page its target was entered in, and a breakpoint dialog keeps the page of a name
  assembled for one (`resolve_key()`). The address fields of the breakpoint and watch dialogs go through
  `SymbolTable::resolve()`.
- A z88dk C symbol is shown by its C name (`lookup()`, design 10.6). The Disassembly copy asks
  `symbol_namer(..., true)` for the assembler name so the paste still assembles, the watch dialog labels an
  auto-filled watch with `SymbolTable::shown_name()`, and the Source tab's word menu offers the shown name
  too (rows `QCN-01` to `-05`, `DCN-01`/`02`, `QMAP-08`).
- The three Map > Load MAP File items (Z88DK, Simple, sjasmplus) are three handlers in `DebuggerManager`,
  each calling `load_map()` and then `refresh_panels()`. The Breakpoints Symbol column and the Call Stack's
  target names are computed in the panels' own `refresh()`, so a load that skipped it left the previous names
  on a paused machine (rows `QMAP-05` and `QMAP-06`). A file refused rather than unreadable has the
  backend's reason appended to the failure box (`on_load_map_z88dk()`, `on_load_map_simple()`, rows `QMAP-07`).

## Labels and comments of your own

The Disassembly and the Source tab share one menu builder, `add_annotation_actions()`
(`src/debugger/annotation_menu.cpp:77`), for Add / Rename Label…, Remove Label and Add / Edit Comment…;
`DisasmPanel` draws a **Label** column and the first line of a comment, and **Map ▸ Open / Save / Save As
Annotations** and the quit question about unsaved changes are `DebuggerManager`'s. The mechanism and its rows
are [7.4.6](../07-source-level-debugging/04-6-user-annotations.md), section 10.

## Source

The Source tab (`source_panel.*`) shows the PC's line from the backend's source map and carries the
source-step buttons; it holds no state of its own beyond the loaded file, its modification time, where the
view was last put, and the sidecar reason `report_sidecars()` gave it. Its refresh, the file search path, the
highlighter and colour schemes, the gutter and breakpoint rows, the context menu, the selection sync with the
Disassembly and the Debug > Source actions are described in
[7.7](../07-source-level-debugging/07-frontends.md), with the mechanism behind each. The menu tree and the
bindings are pinned by `debugger_accel_test` and `debugger_keymap_test`.

## Memory coverage

**Debug ▸ Coverage** (`Enable Coverage`, `Clear Coverage`) drives INS-21
(`mem_coverage_enable/clear`, [3.9.1](09-1-the-backend-api.md)); the check mirrors
the backend on every `refresh_panels()` and at construction, so a window built over
a backend that `--coverage` already switched on shows it ticked. All colours come
from `coverage_colors.h` (X green, R blue, W orange-red, the Source tint a pale
green), each through `palette_tint`; the Source tab alone takes its tint from the scheme
(`SchemeColours::coverage_line`: Light is `coverage_colors::exec_line()` of a light palette, Dark and
Borland their own dark greens, so every class colour reads on it, QSRC-195 clause 5, QSRC-200). The Disassembly paints a 3 px bar right of the
gutter separator on a line where `mem_coverage_fetch(addr)` has X, after the PC and
selection fills; the Memory panel asks `mem_coverage()` for each row through the
same `locate()` as the bytes and underlines each covered byte in thirds, X | R | W,
with a legend label in its top bar; the Source tab adds a full-width
`ExtraSelection`, in the scheme's tint, for each line in `executed_source_lines(file)` before the
current-line selection, so that one wins on its own line. Nothing is drawn while
coverage is off.

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

## The PC-line hint

The hint at the end of the PC line (`[taken]`, `[($9005)=$3C]`) is computed by
`src/debug/disasm_hint.*`, Qt-free, so any frontend can use it and its suite runs
in every configuration. It is a small decoder of its own, next to `disasm.cpp`,
and `disasm_hint_test` pins the two together over the whole opcode space. The
rule for what counts as a memory operand is the operand the mnemonic names in
parentheses; ports, block transfers, `RRD`/`RLD`, `PUSH`/`POP` and the Z80N
opcodes get none. The instruction bytes are read through `memory_reader()` (the
bytes the CPU will fetch) and the operand through `Debugger::operand_reader()`,
the memory the instruction's own data cycles see. They differ at a DivMMC or
Multiface entry point, where the M1 has already switched the overlay
(`divmmc.vhd` loads `automap_hold` in the M1 and copies it to `automap_held` as
soon as MREQ goes high), and equal `peek(Cpu)` everywhere else. The panel
computes the hint in `disassemble_from()` and paints it only while paused, at
the comment's column; the user comment moves right of it.

Where a taken jump, call or return goes is `disasm_hint::pc_transfer()`, in the same
files: it is built from `pc_hint()` (a conditional is taken or not) and `follow_target()`
(every unconditional target), so no decoder is added. `Transfer::dir` is the unsigned
address compare, because the panel is a linear $0000-$FFFF listing (a `JR` that wraps
from $FFF0 to $0012 points up). `hidden` is set for `RET`, `RETI`, `RETN` and `JP (rr)`,
whose target the line does not write; `RET cc` is not hidden, its hint names the target.
`disassemble_from()` calls it through `follow_data_reader()` at the PC and marks the
entry whose start address is the target (an exact start, not "covers"), whether or not
the PC line is in view. The arrow and the `[<- PC]` marker are positioned in
`paintEvent()` but drawn after the comment, so the items before them keep their order;
`line_transfer()` and `line_destination()` are the accessors, and `line_hint()` is
unchanged.

## The Graphics tab

The Graphics tab (`GraphicsPanel`, `src/debugger/graphics_panel.*`) has three sub-tabs: **Patterns** (the
64 8-bit or 128 4-bit sprite patterns of pattern RAM), **Tiles** (the tilemap's tile definitions) and
**Palettes** (all eight banks, with `(active)` on the one each layer reads). It reads through the facade only
and draws only while its tab is the current one (`render_count()` is the test seam). Format and source follow the
live registers: the tile format, count and base come from NR 0x6B and NR 0x6F, and the transparency from NR 0x4B,
NR 0x4C and NR 0x14; only the palette bank and offset are the user's. A pattern or tile is drawn untransformed. The
tile bytes are read from the BRAM the tilemap fetches (pages 0A/0B for bank 5, 0E for bank 7), never from the
CPU map.

The Qt-free half is `src/debug/gfx_decode.*` (`jnext::dbg::gfx`, a sibling of `mem_tools.h`, not one of the frozen
facade headers): `decode_pattern()` and `decode_tile()` return, per pixel, the palette index the layer looks up and
whether the layer draws it, following `sprites.vhd` and `tilemap.vhd`, and `tile_byte_addr()` gives the tile's
byte address (the 6-bit base add that wraps inside the bank, and the 8 KB wrap of bank 7). The panel applies the
colour (`palette()` through `rgb333_to_argb()`) itself. `GfxAtlas` is the one widget both atlases use: its
`image()` is the unscaled picture, with alpha 0 where a pixel is transparent, painted over the Video panel's
checkerboard.

## The Memory panel's tools

Find, Save Block, Load Block and Fill Block live in the Memory panel's context menu
(`MemoryPanel::fill_context_menu()`, reached by `contextMenuEvent()`: right-click, the Menu key,
Shift+F10). The Qt-free half is `src/debug/mem_tools.*` (`jnext::dbg::memtools`): the pattern grammar
and matcher (hex pairs with `?` nibble wildcards and an optional mask, or ASCII text reduced to the same
bytes-and-mask form), the fill value, the hex length, and `split_block()`, which turns a block in a
space into the chunks the backend's verbs can take. It does no file I/O, so its suite runs in every
configuration; the panel does the `QFile` work.

A block is in the selected view's space (`locate()`): `Cpu` in the CPU view, the slot's `SlotInfo::space`
or the picked `Page{n}` in the others. A `Cpu` block is one chunk and never wraps; a `Page` block is one
chunk per 8K page, running on through the following pages to page `DF`; a ROM space is one chunk inside its
16K image. The panel checks the start against the 8K view before `locate()`, which masks the offset to 13
bits. Every write is then ONE `Debugger::poke()` per chunk, never a byte loop: a block lands as one MUTATE
line, one RZX check and one rewind mark per chunk, and a `Cpu` poke reports how many bytes landed (a
partial ROM drop is `RefusedReadOnly` with the count), which the message reports as `L of M`.

The tools answer through `MemoryPanel::message()`, which the window connects to `show_rewind_refusal()`
(the status bar, held), and a find hit with a CPU address through `show_in_disassembly_requested()` to
the Disassembly. The dialogs are stack `QDialog`s parented to the panel and gone when they return:
nothing may stay a child of the panel, because callers and rows find the Addr box and the view selector
with `findChild<>()`.

## Tests

The Qt suites are gated on a Qt + debugger build and run in `make unit-test`:
`debugger_verbs_test` (the verbs, the pause edge and the rewind, trace and MAP
controls), `debugger_panels_test` (each panel's displayed text), `debugger_video_panel_test`, `debugger_audio_panel_test`,
`debugger_inspect_watchpoint_test`, `debugger_persistent_bp_test`,
`debugger_keymap_test`, `debugger_menu_test`, `debugger_disasm_copy_test`,
`debugger_quit_gate_test`, `debugger_variables_panel_test` (the Variables tab, `QVAR-*`, editing `QVARW-*`), and the window-geometry suites. The regression rows
`qt-host-probe-func` and `qt-host-order-func` exercise `QtApp`'s hosting. The hint's decoder suite, `disasm_hint_test` (`DHINT-*`), is not Qt-gated; the panel's rows are `QDHINT-*` in `debugger_panels_test`, and `operand_reader()`'s are `OPRD-*` in `source_debug_test`. The Memory panel tools' helper suite, `mem_tools_test` (`MEMT-*`), is not Qt-gated either; the panel's rows are `QMPT-*` in `debugger_panels_test` and the window's `QMPW-*` in `debugger_verbs_test`. The Graphics decoders' suite, `gfx_decode_test` (`GFXD-*`, VHDL as oracle), is not Qt-gated; the tab's rows are `QGFX-*` in `debugger_panels_test`.
