# 3.9.1 The backend API

Four headers in `src/debug/` are the whole interface a frontend sees:

| Header | Declares |
|---|---|
| `debugger.h` | `jnext::dbg::Debugger`, and the session types: `ClientKind`, `ClientInfo`, `Listener`, `Service`, `PumpBudget`, `ServiceHint`, `PausedInfo`, `StopPolicy`, `LoopDriver`, `LogLevel` |
| `events.h` | the event vocabulary: `EventKind` and its sub-kinds, `Event`, `EventFilter`, `Subscription`, `SubscriptionInfo`, `Condition`, `Handler`, `Action`, `Hit`, `ClientId`, `EventId` |
| `inspect.h` | the inspection value types: `MemSpace`, `SlotInfo`, `RegId`, `RunState`, `PauseReason`, `Time`, `MachineInfo`, `RewindRange`, `InputState`, `Layer`, `ClipLayer`, `PaletteId` and the rest |
| `result.h` | `Result`, the closed set of refusals, and `Expected<T>` |

They are a frozen interface. A verb is added only by owner decision, and each
declaration says which capability id of `doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md`
§4 it serves. `doc/design/debug-subsystem/b0-cap-traceability.md` maps each
capability id to its declarations, though not completely: `rgb333_to_argb()`,
for one, is missing from it.

## Conventions

**Every verb that can refuse returns a `Result`.** The set is closed, and every
value is pinned by `debug_types_check.cpp`:

| Value | `Result` | Meaning |
|---|---|---|
| 0 | `Ok` | done |
| 1 | `RefusedRunning` | needs a paused machine |
| 2 | `RefusedPaused` | needs a running machine |
| 3 | `RefusedCorrupt` | an unacknowledged corruption incident blocks it (CTL-11) |
| 4 | `RefusedRzx` | an RZX is recording or playing |
| 5 | `RefusedUnavailable` | benign: the thing is not there right now (empty rewind buffer, trace off, no loop driver, an unknown bookmark) |
| 6 | `RefusedReadOnly` | a ROM target |
| 7 | `InvalidPage` | a page number that names no page |
| 8 | `NotAtFrameBoundary` | a frame-boundary-only verb, mid-frame, asked not to advance |
| 9 | `NoFrame` | a deferred capture that never got its frame |
| 10 | `Unsupported` | there is no such thing, for example a verb called from inside an event delivery that may not be |

`result_name()` gives each one stable lowercase spelling for logs and protocol
error strings, and `ok(r)` is the named test for success.

**A verb that refuses and also yields data returns `Expected<T>`**: a `status`
and a `value` in one object, testable with `if (auto r = ...)`. On a refusal the
value is value-initialised, with one exception: `peek` and `poke` report how
many bytes did transfer even when they refuse.

**A query that cannot refuse returns its value directly.** The class is listed
in full in `debugger.h`'s "DIRECT-VALUE QUERIES" banner, with per-group counts
so the list can be audited. Moving a query onto that list is a claim that it
cannot fail. `pump()` is in neither class: it is the loop owner's service call.

**Every transition and every mutation is attributed.** A verb that pauses,
resumes, steps, resets, loads or writes takes `ClientId by` as its first
argument, and the backend broadcasts transitions with it. Reads take none. The
backend itself acts as `CLIENT_NONE`.

**No ownership token, no arbitration.** One machine, any number of clients, and
the last verb wins. The only thing owned is a subscription, which only the
client that created it may change or remove.

**Memory is addressed by `MemSpace`, and the index is part of the space.**

| `MemSpace` | Addresses |
|---|---|
| `MemSpace::cpu()` | the live 64 KB CPU map, every overlay included (DivMMC, Multiface, Layer 2, the NR 0x8C alternate ROM) |
| `MemSpace::page(p)` | the physical 8 KB page `p`, numbered as NR 0x50-0x57 numbers it (0..223), regardless of any overlay. 0xFE and 0xFF, the ROM sentinels, answer `InvalidPage` |
| `MemSpace::rom(i)` | the 16 KB ROM image `i` (0..3), read-only |

`MemSpace` is also the answer to *what supplied this byte*: the read arbiter
(`Mmu::route()`) names its source with the same type, so it carries nine more
kinds that only the arbiter produces: `boot_rom()`, `multiface_rom()`,
`multiface_ram()`, `divmmc_rom()`, `divmmc_ram(bank)`, `alt_rom(i)`,
`layer2(page)`, `config(bank)` and `floating()` (nothing answers). `peek` and
`poke` of one of them answer `Unsupported` and write nothing, until a consumer
adds the access with its own rows. The type lives in `debug/mem_space.h`
(standard headers only, so the core's `memory/mmu.h` can return it), and
`FetchSource` is its name in the role of an opcode fetch's source.

A client never composes a space from a slot's page number. On the Next a ROM
slot's `effective_page` is an un-shifted SRAM page index, outside `page()`'s
numbering. So `mmu_slots()` returns each slot's complete backing store as
`SlotInfo::space` plus `SlotInfo::space_offset`. `rom_select()` names the image
legacy paging selects, even while RAM is paged at 0x0000.

`space` is the slot's OWN source. `SlotInfo::now` is what a read of the slot
reaches right now, overlays included (DivMMC, Multiface, Layer 2, alt ROM,
config mode, boot ROM): `Mmu::route(slot << 13).space`, the live data route of
the slot's first byte. One value per slot is complete, because every arm of the
VHDL arbiter switches on `cpu_a(15:13)` boundaries or coarser. It is the DATA
view: the instruction at the PC may come from elsewhere, which is what
`source_page(pc)` answers.

The spelling of a space for a human or a file (the tags `P0E`, `ROM1`, `DIVRAM3`..., the physical
text and the `AddressStyle`) is `debug/long_address.h`. The tags are a stable format; they are described
in [7.1](../07-source-level-debugging/01-which-memory.md), with the slot that holds a page >= `0xE0`
and reads nothing.

## The capability families

Each family keeps its design id prefix, which is how the headers, the tests and
the design documents refer to it.

### Control — CAP-CTL

| Verb | What it does |
|---|---|
| `pause(by)` | stop at the next instruction boundary. Idempotent; a second pause re-attributes the stop to its caller |
| `run(by)` | resume. Still `Ok` on a running machine. Subject to the corruption gate |
| `step_into(by)` | one instruction, **synchronously**, frame-loop aware: a step at a `HALT` runs the halt out |
| `step_over(by)`, `step_out(by)` | **asynchronous**: the stop arrives later as a `Paused` push |
| `run_to(by, addr)`, `run_to_source_line(by, "file:line")`, `run_to_cycle(by, c)`, `run_to_frame(by, f)`, `run_to_end_of_frame(by)`, `run_to_end_of_scanline(by)` | resume until a target. `run_to_source_line` arms a transient `Execute` group at every start of the line (each tied to its memory, `subscribe_source_line`); the first one reached stops the machine and the stop reads `RunTo` with that address. It is refused, the machine untouched, for a line with no code. A run-to issued while the machine RUNS first drops the caller's own transient targets (a second Run to Here replaces the first), never another client's |
| `step_back(by, n)`, `rewind_to_frame(by, f)` | rewind, synchronously. A benign refusal is `RefusedRzx` or `RefusedUnavailable` (no ring or an empty one, a frame outside it or not recorded, the trace off or empty, or a step back that would replay across a debugger change), and logs its reason |
| `resume_blocked_by_corruption()`, `acknowledge_corruption(gen)` | the CTL-11 gate after a failed restore |
| `reset(by, Soft \| Hard)` | `Soft` is `Emulator::soft_reset()`. `Hard` is the reconstruct contract of [3.9.3](09-3-sessions-the-pump-and-reconstruct.md) |
| `load(by, path)` | through the loop owner's `LoopDriver::load`; same contract when the load rebuilds the machine |
| `state()` | `RunState`: paused, step mode, `pause_reason`, cycle, frame, PC |
| `magic_breakpoint()`, `set_magic_breakpoint(on)` | the magic opcode switch |
| `armed()`, `persistent_breakpoints()`, `set_persistent_breakpoints(on)` | whether the hot loop consults the backend at all |

`step_over()` asks `is_call_like()` about the instruction at PC (`CALL`,
`CALL cc`, `RST`, `DJNZ`). If it is, it arms a transient `Execute`
subscription at the next instruction and resumes. Otherwise it is a
`step_into()`.

### Inspection and mutation — CAP-INS

Reads never perturb the machine. Writes are first-class, logged by the backend,
and follow the contract of [3.9.2](09-2-event-delivery-and-mutation.md).

| Area | Reads | Writes |
|---|---|---|
| registers | `registers()` | `set_register(by, RegId, v)`, one register at a time. Setting `PC` clears `halted` |
| memory | `peek(space, addr, n, buf)`, `memory_reader()` | `poke(by, space, addr, n, buf)` |
| paging | `mmu_slots()`, `paging_ports()`, `rom_select()` | `set_mmu_slot(by, slot, page)`, through the NR 0x50+slot write handler |
| NextREG | `nextreg_peek(reg)`, `nextreg_selected()` | `nextreg_write(by, reg, v)`, which runs the register's own handler |
| ports | — | `port_in(by, port)`, `port_out(by, port, v)`; both dispatch like the guest's `IN`/`OUT` and are perturbing by nature |
| time and raster | `time()`, `raster()`, `machine()` | — |
| sprites | `sprites()`, `sprite_attr_raw(i)`, `pattern_ram()`, `sprite_palette_rgb333(bank, i)`, `sprite_clip()` | `set_sprite_attr_raw(...)`, `write_pattern_ram(...)` |
| Copper | `copper()` | — |
| audio | `ay_registers(chip)`, `turbosound_enabled()`, `ay_mode()`, `stereo_mode()`, `audio_mute_mask()` | `set_audio_mute_mask(by, m)` (host-side, never in a snapshot or an RZX) |
| video | `framebuffer()`, `render_layer(layer, vc, dst, stride)`, `palette(id)`, `active_ula_palette_bank()`, `ula_screen_regs()`, `clip_window(layer)` | `set_palette(...)`, `set_border(by, colour)` |
| disassembly | `disassemble(addr, n, symbols)`, `instruction_length(addr)`, `is_call_like(addr)` | — |
| call stack and trace | `call_stack()`, `trace_entries()`, `trace_export(path)` | `set_call_stack_enabled`, `set_trace_enabled`, `trace_clear`, `trace_resize` |
| coverage | `coverage()`, `coverage_enabled()`; INS-21: `mem_coverage_enabled()`, `mem_coverage(space, addr, n, out)`, `mem_coverage_fetch(addr)`, `mem_coverage_spaces()`, `executed_source_lines(file)` | `coverage_enable`, `coverage_clear`; INS-21: `mem_coverage_enable`, `mem_coverage_clear` |
| input | `input_state()` | see input injection below |
| the event model | `subscriptions(include_transient)`, `events_fired_since(seq)` | see [3.9.2](09-2-event-delivery-and-mutation.md) |

Three of these are worth understanding before using them.

**`peek(MemSpace::cpu())` is `Mmu::peek()`.** `Mmu::read()` is the guest's
read: it captures the byte into the +3 floating-bus latch on a contended access
and raises the data-breakpoint latch on a READ watchpoint. `Mmu::peek()` wraps
`read()` under `DebugState::InspectionScope` and puts the floating-bus byte
back. It wraps rather than copies, so the overlay arbitration (boot ROM,
Multiface, DivMMC, Layer 2, alternate ROM, config mode) cannot drift between
the two. The trace's word at SP reads the same way.

**The frame number is a tag, not the raw counter.** `Emulator::frame_num()` is
post-incremented at the start of each frame, so during frame K it reads K+1.
`time().frame` and `state().frame` report `frame_num() - 1`. That is the tag
the rewind slot for the frame carries, and the frame `--delayed-keypress-frames N`
lands on. A rewind lands on a frame start the ring has already counted;
`RewindRange::at_restored_frame_start` says when the machine sits on one, and
`run_to_frame()` takes its base from it.

**`raster()` and `time()` of a paused machine are snapshots taken at the
query.** While the machine runs they report the live beam from the clock.

`render_layer()` (`src/debug/debugger_render.cpp`) draws one of the eight
`Layer` views of the paused frame: composite, ULA primary and shadow, Layer 2
active and shadow, sprites, tilemap, and the NR 0x4A background. It fills rows
0..vc of a `RENDER_WIDTH`-wide buffer with `0x00000000`, so alpha 0 means
transparent. It then replays every per-scanline change log the way
`Renderer::render_frame` does, so a raster split shows as on screen. The render
is state-preserving: the replay walks each log to its end, and the port 0x303B
sprite status bits are saved and restored. `debugger_backend_test` INS-14-08
compares the whole serialised machine state around each view.

### Events — CAP-EVT

| Verb | What it does |
|---|---|
| `subscribe(by, Subscription)` | register a subscription owned by `by`; returns its `EventId` |
| `unsubscribe(by, id)`, `set_enabled(by, id, on)` | owner only |
| `subscribe_source_line(by, "file:line", like)` | a source breakpoint: resolves the line in the live source map and subscribes one `Execute` per start, each a copy of `like` with `lo = hi = start.address` and `space = start.space`, all under one new `GroupId`. All or nothing; refused with no map, no code at the line, or a `like` that is not `Execute` |
| `unsubscribe_group(by, group)`, `set_group_enabled(by, group, on)` | every member of a source breakpoint; owner only. `SubscriptionInfo::group` says which group a listed subscription belongs to (`GROUP_NONE` for a plain one) |
| `master_enabled()`, `set_master_enabled(on)` | suspend every non-transient subscription without touching its own flag |
| `client_enabled(cid)`, `set_client_enabled(cid, on)` | the per-client switch |
| `probe_execute(pc)` | the ids of every live `Execute` subscription that would fire at `pc`, with its condition evaluated. A pure query, for step loops |
| `raise_host_event(by, name)` | raise a `Host` event, `script1`..`script8` or any name up to `MAX_HOST_EVENT_NAME` |

[3.9.2](09-2-event-delivery-and-mutation.md) describes the subscription model
and the delivery contract.

### Input injection and capture — CAP-IN, CAP-CAP

**Pulses append.** `press_key(by, name | MatrixKey, hold_frames)` queues a
pulse on `Keyboard`'s auto-type queue. `Keyboard::queue_auto_type()` appends
for every producer: the phantom typist, the tape `LOAD ""` sites,
`--delayed-keypress` and the backend. An append behind an entry in flight
leaves that entry's counters alone, so a held key does not restart its hold.
`Keyboard::MAX_AUTO_TYPE_KEYS` (16) caps the union of what every producer
queued. A pulse that does not fit is refused with `RefusedUnavailable` and the
count that was queued.

**Every injection lands at the frame edge.** A pulse or a level set
(`set_key`, `set_extended_key`) issued during frame N — from a handler, from a
remote command in a pump, from a CLI countdown — is applied at the end of frame
N and is visible to the guest from frame N+1. The level sets are queued on
`Keyboard` and applied first thing in the edge's `tick_auto_type()`, so a
handler cannot change what the rest of its own frame reads. `end_of_frame()`
delivers the `Frame` event before that tick, so an `on frame N` handler's pulse
is pressed by frame N's own tick. `set_joystick` and `press_nmi` are immediate.

**Screenshots wait for the next rendered frame.** `screenshot(by, path,
layer_mask, format)` only queues. `pump()` writes every capture whose frame has
been rendered since it was queued (`Emulator::rendered_frames()` moved), before
any command of that pump can change the machine. While a capture waits, its
layer mask is armed on the renderer and a force-render bit makes sure the next
frame is rendered whatever the frontend's render-skip hint says. A paused
machine renders nothing, so the capture is held, with one warning.
`flush_captures(by)` is the exit bound. It answers `NoFrame` if any of `by`'s
captures is still pending (they are dropped), `RefusedUnavailable` if one failed
to write since the last call, and `Ok` otherwise. A capture survives its
requester's detach and every machine rebuild. The PNG and `.SCR` writers live in
`src/core/screenshot.*`, below the platform layer.

`ula_screen_dump()` returns the ULA screen as a `.SCR` image. Layer 2, tilemap
and pattern RAM come out of `peek(MemSpace::page(...))`.

### State and rewind — CAP-ST

**Saves share one rule.** `save_state_bytes`, `bookmark_save` and
`save_snapshot` reach a frame boundary through one helper. At a boundary they
save. Mid-frame, `SaveStateMode::RefuseMidFrame` answers `NotAtFrameBoundary`,
and `AdvanceToBoundary` runs the frame out under `SuspendScope` and logs a
`MUTATE clock … by <client>` line, because emulated time moved under every other
client. `save_snapshot` always advances, as `--delayed-snapshot` does, and
writes the file by extension through `save_snapshot_file()` in `src/core/`.

A window inside the frame edge is not a boundary. Between the `Frame` delivery
and the end of `tick_auto_type()` the machine is not the state `run_frame()`
hands back, so a save from a `Frame` handler is refused, or answers
`NotAtFrameBoundary`.

**Bookmarks** are named, in memory and per client: `bookmark_save`,
`bookmark_restore`, `bookmarks(cid)`. A client holds at most 8; a ninth new name
is refused and re-saving a name replaces it. They are freed at detach and
survive a hard reconstruct. A restore into a machine of another type or
snapshot width is refused before `load_state` runs, so nothing is latched.
DZRP's `READ_STATE`/`WRITE_STATE` and ZRCP's `snapshot-save`/`snapshot-load`
are bookmarks.

`load_state_bytes` restores in-process bytes. A failure latches corruption,
which the CTL-11 gate then holds every resume on. The rewind controls are
`rewind_enabled`, `set_rewind_enabled`, `rewind_range()`, `resize_rewind_buffer`
and `rewind_blocked()`, which returns the `Result` a rewind would refuse with,
for greying a control before it is clicked. It knows only the refusals that do
not depend on the target: an RZX, no ring, an empty ring. The rest — no trace
entry, a frame outside the ring or not recorded, a replay that would cross a
debugger change ([3.9.2](09-2-event-delivery-and-mutation.md)) — are known only
when the verb runs.

**A refused rewind says why.** On every `RefusedRzx` or `RefusedUnavailable`,
`step_back` and `rewind_to_frame` log one `Warn` line to every listener:

```
REWIND REFUSED: Step Back refused: the instruction trace is off - turn on Debug > Trace > Enable Trace and run forward. See the user guide: Debugger > Functions > Backward execution (rewind)
```

The verb is `Step Back` or `Rewind to frame N`, and the reason is read from the
machine after the refusal (`rewind_refusal_reason()` in
`debugger_control.cpp`). The prefix is what the Qt debugger matches to put the
reason in its status bar. `RefusedCorrupt` logs no reason line: the corruption
incident is reported on its own.

### Symbols — CAP-SYM

There is one symbol table, and the backend owns it; the Qt panels, the DSL's `@name`, the GDB
server's `monitor sym` and `--map` all read it, so a symbol loaded by one client shows in every other.

| Verb | Does |
|---|---|
| `load_map(path, format, why)` | loads a symbol file into the table: `MapFormat::Z88dk`, `Simple` (`SYMBOL = $ADDR`), `NextBuild` (`Memory.txt`), `SjasmplusEqu` (`--sym` / `--exp`), or `Z88dkOrSjasmplus` (`--map`: the first line decides, one read, a pipe works). Returns the symbol count, or `RefusedUnavailable` (value 0) with the table and its owner untouched when refused; `why` says why a file was refused; for a file that cannot be opened it stays empty, except `NextBuild`, which says "could not open Memory.txt file" ([7.3](../07-source-level-debugging/03-loaders.md)) |
| `clear_symbols()` | forgets them all |
| `lookup(addr)` | the name of `addr` in the memory mapped there now (`source_page(addr)`); a label is paged or unpaged |
| `lookup_name(name)` | the address of a name; a name-only value (a size, a section bound) resolves here and never names an address |
| `symbols()` | the table itself, for `disasm_text::apply_symbols()` and for `SymbolTable::resolve()`, which turns an address field's text into an address (hex first, else a symbol; `@name` is always the symbol) |

How the table is keyed, bound and merged, and what each format puts in it, is
[7.4](../07-source-level-debugging/04-formats.md) ("The symbol table the formats fill"); the z88dk
placement rules and C names are [7.4.3](../07-source-level-debugging/04-3-z88dk-map.md); the three
other loaders are 7.4.1, 7.4.2 and 7.4.5.

### User labels and comments — CAP-ANN

A second layer beside the toolchain's symbols: the user's labels and comments (GH #387), keyed by memory and
address. `symbols()`, `lookup()` and `lookup_name()` return the composition of the two layers, the user's first,
so no consumer merges them. The store has no lifetime: loads, resets, state loads and rewinds leave it alone, and
it is not in `.jns`.

| Verb | Does |
|---|---|
| `set_user_label(space, addr, name, why)` | names `addr` in `space` (a rename when the key has a label); `Unsupported` with `why` for the Cpu space, a memory the file cannot name, a bad name, a name in use, or a name the loaded symbols give another address or memory |
| `remove_user_label(space, addr)` | removes the user's label; `RefusedUnavailable` when there is none (the toolchain's is untouched) |
| `set_user_comment(space, addr, text, why)` | sets the comment; empty text removes it; CRLF and CR become LF; `Unsupported` for text that is not UTF-8 |
| `annotations()`, `annotations_revision()` | the store, and a counter that rises with each successful change |
| `load_annotations(path, why)`, `save_annotations(path, why)` | the `.jdp` file: a load replaces the whole store or leaves it as it was (`Unsupported`, `line N: ...`; `RefusedUnavailable` for a file that cannot be read); a save writes `PATH.part` and renames it |
| `replace_annotations(a)` | replaces the whole store (what an import builds offline) |
| `toolchain_symbols()` | the loaded layer alone (the Load MAP messages count this) |

The verbs touch no machine state (allowed while running) and each success bumps `state_revision()`. The file,
the composed reader, where it is recomposed and why the attachment cache holds the toolchain layer only are
[7.4.6](../07-source-level-debugging/04-6-user-annotations.md).

### Source maps — CAP-SRC

The backend also owns one `SourceMap` (`src/debug/source_map.*`), keyed by the `FetchSource` that
supplies the code, filled by a format adapter and replaced only by a complete, successful load.

| Verb | Does |
|---|---|
| `load_source_map(path, accept_identity_mismatch, verify_at_first_execution, dot_only)` | the hand load: an sjasmplus SLD or a z88dk `-debug` map, told by content. Refused with the store untouched on a parse error, a mismatch (unless accepted), or (with `dot_only`) a map that is not a dot command's. Returns a `SourceMapLoad` (`count`, `labels`, `listings`, `identity`, `identity_note`, `listing_note`, `pending`, `not_dot`, `error`) |
| `clear_source_map()` | forgets the user's map |
| `source_map()` | the store: `lookup()`, `lookup_all()`, `resolve("file:line")`, `file_ranges()`, `embedded_text()`; empty while the user's map is stale |
| `source_map_status()` | None, Attached or Stale, with the file, the reason, and the `pending` and `dot` flags |
| `source_location(addr)`, `source_location()` | the primary position of `addr` (or of the PC) as it is mapped NOW: a pointer into the store, valid until the next load or clear, so copy what you keep |
| `source_page(addr)` | what an instruction fetch at `addr` would be supplied from now (`Emulator::predict_fetch()`); the identity every lookup uses |
| `effective_page(addr)` | the physical 8K page the MMU maps behind a logical address |
| `load_program_sidecars(path)` | attaches `<stem>.Memory.txt`, `<stem>.sld` and `<stem>.map` of a program just loaded; returns a `SidecarLoad` (`symbols`, `sources`, `symbols_unreadable`, `sources_pending`, `problem`) |
| `subscribe_source_line(by, "file:line", like)` | a source breakpoint: a group of `Execute` subscriptions, one per start of the line, each with the `space` it was compiled for (CAP-EVT, above) |
| `run_to_source_line(by, "file:line")` | resume until any start of the line is reached (every start is armed; the first one reached stops the machine) (CAP-CTL, above) |
| `source_step(by, kind)` | steps by statement: `Into`, `Over`, `Out`, `Back`, `ReverseContinue`; limits `SOURCE_STEP_LIMIT` and `SOURCE_STEP_FRAME_LIMIT` |
| `executed_source_lines(file)` | the lines of a file any of whose ranges the guest executed (the coverage section, below) |

Where the mechanism lives, in [chapter 7](../07-source-level-debugging/index.md):

| Subject | Page |
|---|---|
| What `source_page()` predicts, what the machine records (`Mmu::last_m1()`, `TraceEntry::fetch_space()`, `CallFrame`), `EventFilter::space` and the legacy `page` | [7.1](../07-source-level-debugging/01-which-memory.md) |
| Ranges, the primary, `lookup_all()`, `resolve()`, aliases, embedded texts, the program identity and `verify_program()` | [7.2](../07-source-level-debugging/02-the-source-map.md) |
| The adapter contract, content dispatch, `--source-map`, the sidecar search and precedence, ownership, `SidecarLoad::problem` | [7.3](../07-source-level-debugging/03-loaders.md) |
| Each file format and its producer | [7.4](../07-source-level-debugging/04-formats.md) |
| `subscribe_source_line()`, `run_to_source_line()`, `source_step()`, Back and Reverse Continue, the call stack's frame rule | [7.5](../07-source-level-debugging/05-breakpoints-steps-history.md) |
| The load generation, resets, snapshots, rewind, tapes, dot commands | [7.6](../07-source-level-debugging/06-lifetime.md) |
| Which frontend reads what, the Source tab | [7.7](../07-source-level-debugging/07-frontends.md) |

### Coverage and the trace

INS-20 coverage is recorded in `step_one_instruction()`, the one body that
`run_frame()`, the debugger's Step and `execute_single_instruction()` share,
and only for a slot that fetched the opcode at PC. An NMI or INT acknowledge
does not run it. Switched off it costs one pointer test per instruction.

**INS-21, memory coverage by execute, read and write** (GH #388, CAP-INS-21) is
the physical-memory counterpart, with a separate switch: INS-20 holds 16-bit
logical PCs (ZRCP `cpu-code-coverage` returns them, and the logical address a page
ran at cannot be rebuilt from a physical byte), INS-21 holds one byte of flags
(`dbg::COVERAGE_EXEC | COVERAGE_READ | COVERAGE_WRITE`) per byte of every
`MemSpace`, in lazily allocated 8 KB chunks (`dbg::MemCoverage`,
`debug/mem_coverage.h`; a 16 KB store, `Rom`, `AltRom` or `Config`, is two chunks).
`Mmu` writes it from the one arbiter (see [3.2](02-memory.md)): X is the first
opcode byte of an executed instruction (`read_m1()`; a prefix's inner M1 is no
mark), R every other read including the DMA's, W a write that landed, each at the
space and offset the route names and only for guest accesses (inside
`GuestExecutionScope`, not `InspectionScope`: exactly what a watchpoint can see, so
`peek`/`poke`, the trace's captures and loaders mark nothing). The set and its
switch are backend state, published to `DebugState::mem_coverage_sink()` by
`Impl::publish_mem_coverage()` and retired by `~Debugger()` like INS-20's, and
re-published at the one re-application, so a rewind, reset, state load or cold boot
keeps the bits; a rewind's replay sets the same bits again, so nothing is counted
twice. Off clears; `mem_coverage_clear()` empties and recording goes on.
`mem_coverage(space, addr, n, out)` addresses like `peek()` (`Cpu` through the live
data route, any other space by offset in its store);
`mem_coverage_fetch(addr)` is the byte an M1 at `addr` would fetch now
(`source_page(addr)` plus `space_offset()`), which is what the Disassembly paints;
`mem_coverage_spaces()` lists the marked spaces sorted by (kind, index), for an
exporter to walk. `executed_source_lines(file)` is the Source tab's: the lines of a
file any of whose ranges (`SourceMap::file_ranges()`) holds an X byte. The
front-ends add `--coverage` (`EmulatorConfig::coverage`, applied by the three loop
owners through `apply_startup_coverage()`) and the Qt window's **Debug ▸ Coverage**.
There is no DZRP, ZRCP, GDB or DSL verb.

Each trace entry carries the registers (including I, R, IM, IFF1 and IFF2), the
word at SP read with `peek()`, the eight MMU pages and `rom_slots`, a bit per
slot that held ROM, and the identity of the memory its instruction was fetched
from (`fetch_kind`, `fetch_index`). `trace_export()` writes all of them; the ROM
mask is the `ROM=` column after the pages and the identity the `FETCH=<tag>`
column after it (`space_tag()` of `fetch_space()`, never the GUI's address style).

### Session — CAP-SES

`attach`, `detach`, `set_listener`, `add_service`, `remove_service`, `pump`,
`stop_policy`, `set_stop_policy`, `set_live_raster`, `live_raster`, `attached`,
`log`, `set_loop_driver`, `on_cold_boot_begin` and `on_cold_boot_done`.
[3.9.3](09-3-sessions-the-pump-and-reconstruct.md) describes them.

## Behind the facade

`Debugger` holds one `unique_ptr<Impl>` and nothing else, so adding state never
edits a header five frontends compile against. `Impl` is defined in the internal
`src/debug/debugger_impl.h`, which is also where `core/emulator.h` is included.
No path through `run_frame()` calls a `Debugger` method: the hook sites read the
internals directly, through the pointers the constructor publishes into
`DebugState`.

| File | Holds |
|---|---|
| `debugger.cpp` | construction and destruction, the `MUTATE` log, symbols, `state()`, the save and rewind-buffer verbs |
| `debugger_control.cpp` | CAP-CTL: pause, run, the steps, the run-to family, rewind, soft reset |
| `debugger_inspect.cpp` | CAP-INS reads and writes, the trace and coverage verbs |
| `debugger_input.cpp` | input injection |
| `debugger_events.cpp` | CAP-EVT, the pre-instruction `Execute` gate and the boundary drain |
| `debugger_session.cpp` | clients, listeners, services, `pump()`, the stop policy |
| `debugger_reconstruct.cpp` | the loop driver, `reset(Hard)`, `load()`, the cold-boot notifications and the reconstruct contract |
| `debugger_capture.cpp` | screenshots, bookmarks, `save_snapshot` |
| `debugger_source.cpp` | CAP-SRC: the source map, program sidecars, `source_step()` |
| `debugger_render.cpp` | `render_layer()` |
| `event_table.*` | `EventTable`: subscriptions, cheap filters, the latch ring, the delivery history |
| `inspect.cpp`, `result.cpp` | the free functions of `inspect.h` (`key_name_to_matrix()`, `rrrgggbb_to_argb()`, `rgb333_to_argb()`) and `result_name()` |

`debugger_backend_test` is the backend's suite. It is headless and Qt-free, so
it runs in both test configurations. Its row families follow the capability
ids: `CTL-*`, `INS-*`, `EVT-*`, `SES-*`, `CAP-*`, `ST-*`, `HOST-*`, `LIFE-*`.
Each control verb has a wiring row (arm it through the facade, run, assert the
machine stopped where the verb promises) and a control row, where the same
program runs straight past.
