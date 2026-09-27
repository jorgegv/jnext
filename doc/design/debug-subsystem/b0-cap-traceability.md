# B0 — CAP id → declaration traceability

> Work package **B0** of epic [#276](https://github.com/jorgegv/jnext/issues/276):
> the four public headers `src/debug/{debugger,events,inspect,result}.h`, as
> [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md) §10.1
> defines them — signatures only, no bodies behind them.
>
> **This file is how B0 is checked.** Every CAP id of §4 appears below with the
> exact declaration that serves it. A CAP id with no declaration is an
> incomplete B0; a declaration serving no CAP id is invention. The four headers
> also carry the CAP id in a comment on each declaration, so the mapping is
> visible from either end.
>
> Unqualified names live in `namespace jnext::dbg`. `debugger.h` means the
> member of `jnext::dbg::Debugger`. Section numbers are §-numbers of the
> architecture document.

---

## §4.1 — CAP-CTL, control

| CAP | Declaration | File |
|---|---|---|
| CTL-01 | `Result pause(ClientId by)` | debugger.h |
| CTL-02 | `Result run(ClientId by)` | debugger.h |
| CTL-03 | `Result step_into(ClientId by)` | debugger.h |
| CTL-04 | `Result step_over(ClientId by)` | debugger.h |
| CTL-05 | `Result step_out(ClientId by)` | debugger.h |
| CTL-06 | `Result run_to(ClientId by, uint16_t addr)` | debugger.h |
| CTL-07 | `Result run_to_cycle(ClientId by, uint64_t master_cycle)` | debugger.h |
| CTL-08 | `Result run_to_end_of_frame(ClientId by)`, `Result run_to_end_of_scanline(ClientId by)` | debugger.h |
| CTL-09 | `Result step_back(ClientId by, uint32_t n)` | debugger.h |
| CTL-10 | `Result rewind_to_frame(ClientId by, uint32_t frame)` | debugger.h |
| CTL-11 | `std::optional<CorruptionIncident> resume_blocked_by_corruption() const`, `Result acknowledge_corruption(uint64_t generation)`; `struct CorruptionIncident{subsystem, generation}` | debugger.h, inspect.h |
| CTL-12 | `Result reset(ClientId by, ResetKind kind)`; `enum class ResetKind{Soft, Hard, Any}` (`Any` is filter-only; `reset(Any)` is refused); the driver is SES-07's `LoopDriver::cold_boot` | debugger.h, events.h |
| CTL-13 | `RunState state() const`; `struct RunState{paused, step_mode, pause_reason, cycle, frame, pc}`, `enum class StepMode`, `struct PauseReason` with `Kind{None, User, Breakpoint, Watch, Step, RunTo, Magic, Corrupt, Script}` | debugger.h, inspect.h |
| CTL-14 | `bool magic_breakpoint() const`, `Result set_magic_breakpoint(bool)` | debugger.h |
| CTL-15 | `Result load(ClientId by, const std::string& path)`; the dispatch is SES-07's `LoopDriver::load` | debugger.h |
| §4.1 `armed` | `bool armed() const`, `Result set_persistent_breakpoints(bool)`, `bool persistent_breakpoints() const` | debugger.h |
| §4.1 `attached` / `live_raster` | SES-05 below | debugger.h |
| §4.1 client id on every transition | `ClientId by` is the first parameter of every transition verb; `using ClientId`, `CLIENT_NONE` | debugger.h, events.h |

## §4.2 — CAP-INS, inspection

| CAP | Declaration | File |
|---|---|---|
| INS-01 | `Z80Registers registers() const`, `Result set_register(ClientId by, RegId reg, uint16_t value)`; `enum class RegId` (12 pairs, 20 eight-bit halves, I/R/IFF1/IFF2/IM — `REG_ID_COUNT == 37`) | debugger.h, inspect.h |
| INS-02 | `Expected<size_t> peek(MemSpace, uint32_t addr, size_t n, uint8_t* buf) const`, `Expected<size_t> poke(ClientId by, MemSpace, uint32_t addr, size_t n, const uint8_t* buf)`; `struct MemSpace` with `Kind{Cpu, Page, Rom}` + `index`, the factories `cpu()`/`page(p)`/`rom(i)`, `PAGE_SENTINEL_ROM_LO/HI` | debugger.h, inspect.h |
| INS-03 | `std::array<SlotInfo, 8> mmu_slots() const`, `Result set_mmu_slot(ClientId by, int slot, uint8_t page)`, `PagingPorts paging_ports() const`; `struct SlotInfo{nr_page, effective_page, is_rom, space, space_offset}`, `struct PagingPorts` | debugger.h, inspect.h |
| INS-04 | `uint8_t nextreg_peek(uint8_t reg) const`, `Result nextreg_write(ClientId by, uint8_t reg, uint8_t value)`, `uint8_t nextreg_selected() const`; the `Debugger` source value is `EventSource::Debugger` | debugger.h, events.h |
| INS-05 | `Expected<uint8_t> port_in(ClientId by, uint16_t port)`, `Result port_out(ClientId by, uint16_t port, uint8_t value)` | debugger.h |
| INS-06 | `RasterState raster() const` (`RasterState` reused from `debug/raster_state.h`) | debugger.h |
| INS-07 | `Time time() const`; `struct Time{master_cycle, tstates_total, frame, cycle_in_frame, vc_raw, hc_raw}` | debugger.h, inspect.h |
| INS-08 | `std::vector<SpriteInfo> sprites() const`, `Expected<std::array<uint8_t,5>> sprite_attr_raw(uint8_t) const`, `Result set_sprite_attr_raw(...)`, `ConstBytes pattern_ram() const`, `Result write_pattern_ram(...)`, `Expected<uint16_t> sprite_palette_rgb333(int bank, uint8_t index) const`, `ClipWindow sprite_clip() const`; `using SpriteInfo = SpriteEngine::SpriteInfo`, `SPRITE_COUNT`, `SPRITE_ATTR_BYTES`, `PATTERN_RAM_BYTES`, `struct ClipWindow` | debugger.h, inspect.h |
| INS-09 | `CopperState copper() const`; `struct CopperState{pc, running, mode, program}` | debugger.h, inspect.h |
| INS-10 | `Expected<std::array<uint8_t,16>> ay_registers(int chip) const`, `bool turbosound_enabled() const`, `AyChipMode ay_mode() const`, `StereoMode stereo_mode() const`, `uint8_t audio_mute_mask() const`, `Result set_audio_mute_mask(ClientId by, uint8_t)`; `AY_REGISTER_COUNT`, `AY_CHIP_COUNT`; the mask bits are `AudioMute::*` (`audio/audio_mute.h`), reused | debugger.h, inspect.h |
| INS-11 | `std::vector<DisasmLine> disassemble(uint16_t addr, size_t n, const SymbolTable*) const`, `int instruction_length(uint16_t) const`, `bool is_call_like(uint16_t) const`, `DisasmReadFn memory_reader() const` (drives the already-published `disasm_text::*` with no `Emulator*`) | debugger.h |
| INS-12 | `const std::vector<CallFrame>& call_stack() const`, `bool call_stack_enabled() const`, `Result set_call_stack_enabled(bool)` (`CallFrame` reused from `debug/call_stack.h`) | debugger.h |
| INS-13 | `bool trace_enabled() const`, `Result set_trace_enabled(bool)`, `Result trace_clear()`, `Result trace_resize(size_t)`, `Expected<std::vector<TraceEntry>> trace_entries() const`, `Result trace_export(const std::string&) const` (`TraceEntry` reused from `debug/trace.h`; B4 extends the struct) | debugger.h |
| INS-14 | `ConstU32s framebuffer() const`, `Result render_layer(Layer, int vc, uint32_t* dst, size_t stride_pixels) const`; `enum class Layer` (8 views), `LAYER_COUNT`, `RENDER_WIDTH` | debugger.h, inspect.h |
| INS-15 | `std::vector<uint16_t> palette(PaletteId) const`, `Result set_palette(ClientId by, PaletteId, uint8_t index, uint16_t rgb333)`, `uint8_t active_ula_palette_bank() const`, `UlaScreenRegs ula_screen_regs() const`, `ClipWindow clip_window(ClipLayer) const`, `uint32_t rrrgggbb_to_argb(uint8_t)`; `enum class PaletteId` (8 banks + `UlaActive`), `enum class ClipLayer` + `CLIP_LAYER_COUNT`, `struct UlaScreenRegs` | debugger.h, inspect.h |
| INS-16 | `InputState input_state() const`; `struct InputState{matrix[8], ext_keys, joy_left12, joy_right12, port_1f, port_37}` | debugger.h, inspect.h |
| INS-17 | `std::vector<SubscriptionInfo> subscriptions(bool include_transient) const`, `std::vector<Event> events_fired_since(uint64_t seq) const`; `struct SubscriptionInfo`, `Event::seq`, `Event::overflowed`/`dropped` | debugger.h, events.h |
| INS-18 | `Result set_border(ClientId by, uint8_t colour)` | debugger.h |
| INS-19 | `MachineInfo machine() const`; `struct MachineInfo{type, cpu_divisor, tstates_per_line, tstates_per_frame, master_cycles_per_line, master_cycles_per_frame, lines, fps, hc_max, vc_max, max_hblank, max_vblank, display_origin_hc, display_origin_vc, vblank_top}` — both clock domains, under `MachineTiming`'s own field names | debugger.h, inspect.h |
| INS-20 | `Result coverage_enable(bool)`, `bool coverage_enabled() const`, `Result coverage_clear()`, `const CoverageBits& coverage() const`; `using CoverageBits = std::bitset<65536>` | debugger.h, inspect.h |

## §4.2a — mutation (the write half of CAP-INS; "no new CAP id")

Every write §4.2a enumerates, and the declaration that is it. All of them take
`ClientId by`, which is what makes the `MUTATE <what> <old> -> <new> by
<client>` log line (SES-06) possible.

| §4.2a write | Declaration |
|---|---|
| INS-01 `set_register` (incl. `PC` clearing `halted`) | `Debugger::set_register` |
| INS-02 `poke(Cpu \| Page, …)` | `Debugger::poke` |
| INS-03 `set_mmu_slot` | `Debugger::set_mmu_slot` |
| INS-04 `nextreg_write` | `Debugger::nextreg_write` |
| INS-05 `port_out` | `Debugger::port_out` |
| INS-08 `set_sprite_attr_raw`, `write_pattern_ram` | `Debugger::set_sprite_attr_raw`, `Debugger::write_pattern_ram` |
| INS-10 `set_audio_mute_mask` | `Debugger::set_audio_mute_mask` |
| INS-15 `set_palette` | `Debugger::set_palette` |
| INS-18 `set_border` | `Debugger::set_border` |
| "where a write lands" — `Page{}` bypasses overlays, `Cpu` honours them | `MemSpace::Kind` doc comments |
| "when it lands" — `Execute` handler is pre-instruction, every other kind post | `EventKind::Execute` doc comment, `Handler` doc comment |
| refused under RZX | `Result::RefusedRzx` |
| ROM-class target refused | `Result::RefusedReadOnly` |
| rewind refused into a mutated span | `Result::RefusedUnavailable`, `Debugger::step_back` doc comment |
| every mutation logged by the backend | `Debugger::log`, `CLIENT_NONE` |

## §4.3 — CAP-EVT, events, breakpoints, conditions

### Kinds and payloads

| CAP-EVT kind | Declaration |
|---|---|
| `Execute` | `EventKind::Execute`; filter `EventFilter::{lo, hi, page}` |
| `Mem` (+ `access ⊆ {Read, Write}`) | `EventKind::Mem`; `EventFilter::{lo, hi, pages, page}`, `Subscription::access`; payload `Event::{addr, phys_page, value, prev, pc, source, access}` |
| `Port` | `EventKind::Port`; `EventFilter::{port_mask, port_value}`; payload `Event::{port, value, pc, source, access}` |
| `NextRegWrite` | `EventKind::NextRegWrite`; `EventFilter::{regs, source}`; payload `Event::{reg, value, prev, source, pc, cycle, hc, vc}` |
| `Frame` | `EventKind::Frame`; `EventFilter::frame`, `FRAME_EVERY`; payload `Event::frame` |
| `Scanline` | `EventKind::Scanline`; `EventFilter::scanline`; payload `Event::{frame, vc, cycle}` |
| `Cycle` | `EventKind::Cycle`; `EventFilter::cycle`; payload `Event::cycle` |
| `Reset` | `EventKind::Reset`; `EventFilter::reset_kind` incl. `ResetKind::Any` for either kind (owner decision F8); payload `Event::reset_kind`, never `Any` |
| `IntAck` | `EventKind::IntAck`; payload `Event::{int_vector, int_mode}` |
| `Nmi` | `EventKind::Nmi`; payload `Event::nmi_source`, `enum class NmiButton` |
| `Magic` | `EventKind::Magic`; payload `Event::pc` |
| `Host` | `EventKind::Host`; `EventFilter::host_name`, `Event::host_name`, `MAX_HOST_EVENT_NAME`; raised by `Debugger::raise_host_event` |
| `Copper{Move, Wait, Halt}` | `EventKind::Copper`, `enum class CopperEventKind`; `EventFilter::{lo, hi, regs, copper_kind}`; payload `Event::{reg, value, copper_pc, wait_vpos, wait_hpos_threshold, hc_ula, cvc}` |
| `Dma{Start, Byte, End}` | `EventKind::Dma`, `enum class DmaEventKind`; `EventFilter::{lo, hi, dma_kind}`; payload `Event::{dma_src, dma_dst, dma_length, dma_bytes, dma_direction, dma_mode, dma_is_io_src, dma_is_io_dst, value, cycle}` |
| `source ∈ {Cpu, Copper, Dma, Any}` (+ `Debugger`, filter-only `Any`) | `enum class EventSource` |
| the common `{cycle, frame, vc, hc, pc, id, owner}` | `Event`'s first block |

### Subscription model, conditions, actions, delivery

| CAP-EVT feature | Declaration |
|---|---|
| condition = predicate callback compiled by the subscriber | `using Condition = std::function<bool(const Event&, const Debugger&)>` |
| the rule body a delivery runs, whose verdict overrides the static action | `using Handler = std::function<Action(const Event&, Debugger&)>` |
| `Subscription = {kind, filter, access, condition?, once, transient, action, enabled, owner}` | `struct Subscription` (+ `handler`, see the judgement calls in the B0 report) |
| `subscribe → EventId` | `Expected<EventId> Debugger::subscribe(ClientId, const Subscription&)`, `using EventId`, `EVENT_NONE` |
| `unsubscribe` | `Result Debugger::unsubscribe(ClientId, EventId)` |
| `set_enabled(id)` | `Result Debugger::set_enabled(ClientId, EventId, bool)` |
| the master switch (GH #225) | `bool Debugger::master_enabled() const`, `Result Debugger::set_master_enabled(bool)` |
| the per-client switch | `bool Debugger::client_enabled(ClientId) const`, `Result Debugger::set_client_enabled(ClientId, bool)` |
| `probe_execute(pc)` | `bool Debugger::probe_execute(uint16_t) const` |
| `once` | `Subscription::once` |
| `transient` (unlimited, master-exempt, hidden, auto-removed) | `Subscription::transient`, `Debugger::subscriptions(bool include_transient)` |
| owner / edit-only-by-owner | `Subscription` owner is the `by` of `subscribe`; `SubscriptionInfo::owner` |
| `SubscriptionsChanged{kinds}` | `Listener::on_subscriptions_changed(EventKindMask)`, `using EventKindMask`, `kind_bit()` |
| actions `Stop` / `Log` / `Continue` | `enum class Action` |
| `Paused` carrying `matched: vector<Hit{event_id, addr, access, value}>` | `struct Hit`, `PausedInfo::matched` |
| delivery at the instruction boundary, latched at the site | `Event::cycle` doc comment, `events.h` banner rule 2 |
| latch-ring overflow is delivered and marked | `Event::overflowed`, `Event::dropped`, `events.h` banner rule 3 |
| the model listed incl. disabled, with live vs own flag | `struct SubscriptionInfo` (`enabled` vs `live`) |

## §4.4 — CAP-TIME

| CAP | Declaration |
|---|---|
| TIME-01 | `Time Debugger::time()`, `MachineInfo Debugger::machine()` (= INS-07, INS-19) |
| TIME-02 | `EventKind::{Frame, Scanline, Cycle}` + `EventFilter::{frame, scanline, cycle}`; a handler runs AT the instant (`Handler`) |
| TIME-03 | `Result Debugger::run_to_cycle(ClientId, uint64_t)`, `Result Debugger::run_to_frame(ClientId, uint32_t)` |

## §4.5 — CAP-IN, input injection

| CAP | Declaration |
|---|---|
| IN-01 | `Expected<size_t> press_key(ClientId, const std::string& name, int hold_frames)`, `Expected<size_t> press_key(ClientId, const MatrixKey&, int hold_frames)`; `bool key_name_to_matrix(const std::string&, MatrixKey&)` (moved out of `headless_app.cpp`), `struct MatrixKey` |
| IN-02 | `Result set_key(ClientId, int row, int col, bool pressed)`, `Result set_extended_key(ClientId, int id, bool pressed)` |
| IN-03 | `Result set_joystick(ClientId, JoystickSide, uint16_t bits12)`; `enum class JoystickSide` |
| IN-04 | `Result press_nmi(ClientId, NmiButton)`; `enum class NmiButton{Mf, Drive}` |

## §4.5 — CAP-CAP, capture

| CAP | Declaration |
|---|---|
| CAP-01 | `Result screenshot(ClientId, const std::string& path, uint8_t layer_mask, ScreenshotFormat)`; `enum class ScreenshotFormat{Png, Scr}`, `LAYER_MASK_*`, `Result::NoFrame` |
| CAP-02 | `std::vector<uint8_t> ula_screen_dump() const`; the rest is `peek(MemSpace::page(p))` (INS-02) |
| CAP-03 | `Result bookmark_save(ClientId, const std::string& name, SaveStateMode)`, `Result bookmark_restore(ClientId, const std::string& name)`, `std::vector<std::string> bookmarks(ClientId) const` |
| CAP-04 | `Result save_snapshot(ClientId, const std::string& path)` |

## §4.6 — CAP-ST, state bookmarks and reverse execution

| CAP | Declaration |
|---|---|
| ST-01 | `bool at_frame_boundary() const`, `Expected<std::vector<uint8_t>> save_state_bytes(ClientId by, SaveStateMode)` — the `by` attributes the `AdvanceToBoundary` advance (owner decision; B0 left it open); `enum class SaveStateMode{AdvanceToBoundary, RefuseMidFrame}`, `Result::NotAtFrameBoundary` |
| ST-02 | `Result load_state_bytes(ClientId, const uint8_t* data, size_t n)`; failure latches corruption → CTL-11 |
| ST-03 | `bool rewind_enabled() const`, `Result set_rewind_enabled(bool)`, `RewindRange rewind_range() const`, `std::optional<Result> rewind_blocked() const`, `Result resize_rewind_buffer(size_t frames)`; `struct RewindRange` |
| ST-04 | `Result step_back(ClientId, uint32_t)`, `Result rewind_to_frame(ClientId, uint32_t)` (= CTL-09, CTL-10) |

## §4.7 — CAP-SYM, symbols

| CAP-SYM verb | Declaration |
|---|---|
| `load_map(path, Z88dk\|Simple)` | `Expected<int> load_map(const std::string&, MapFormat)`; `enum class MapFormat{Z88dk, Simple}` |
| `clear` | `Result clear_symbols()` |
| `lookup(addr)` | `std::optional<std::string> lookup(uint16_t) const` |
| `lookup_name` | `std::optional<uint16_t> lookup_name(const std::string&) const` |
| `symbols()` | `const SymbolTable& symbols() const` (`SymbolTable` reused from `debug/symbol_table.h`) |

## §4.8 — CAP-SES, session

| CAP | Declaration |
|---|---|
| SES-01 | `Expected<ClientId> attach(const ClientInfo&)`, `Result detach(ClientId)`; `struct ClientInfo{name, kind}`, `enum class ClientKind` |
| SES-02 | `Result set_listener(ClientId, Listener*)`; `class Listener` with `on_paused/on_resumed/on_reset/on_frame_ended/on_subscriptions_changed/on_exit_requested/on_log`; `struct PausedInfo{by, reason, cycle, pc, matched}` |
| SES-03 | `ServiceHint pump(const PumpBudget&)`, `Result add_service(Service&)`, `Result remove_service(Service&)`; `struct PumpBudget{max_wait_ms, drain_ms, budget_ms}`, `struct ServiceHint{remote_attached, paused}`, `class Service`, `enum class ServiceStep` |
| SES-04 | `StopPolicy stop_policy() const`, `Result set_stop_policy(StopPolicy)`; `enum class StopPolicy{Pause, ExitNonZero}`; `Listener::on_exit_requested(int code)` is the `ExitNonZero` half |
| SES-05 | `Result set_live_raster(ClientId, bool)`, `bool live_raster() const`, `bool attached() const` |
| SES-06 | `Result log(ClientId, LogLevel, const std::string&)`; `enum class LogLevel`, `Listener::on_log` |
| SES-07 | `Result set_loop_driver(const LoopDriver&)`, `Result on_cold_boot_done()`; `struct LoopDriver{cold_boot, load}` |

---

## Nothing is deliberately omitted

Every CAP id of §4 has a row above. Two entries in §4 are deliberately served
by something other than a new declaration, and say so in their row:

* **§4.2a mutation** states explicitly that it adds "no new CAP id" — it is a
  contract over the nine INS writes, and its own table above maps each clause to
  the declaration or doc comment that carries it.
* **ST-04** and **TIME-01/TIME-03** are the same verbs as CTL-09/CTL-10 and
  INS-07/INS-19/CTL-07; the rows point at them rather than duplicating a
  signature.

## How the contract is gated

Two gates, both added after B0's first review found the headers asserting
properties nothing checked:

| Gate | Where | What it proves |
|---|---|---|
| `src/debug/debug_types_check.cpp` | compiled in all four configurations by `src/debug/`'s `CONFIGURE_DEPENDS` glob | the headers compile; every mirrored enum still matches what it mirrors; every `Result` value is where the order says; the four enum counts catch an APPEND; each of `Listener`'s seven methods is individually pure; `MachineInfo`'s two clock domains keep `MachineTiming`'s names, types and 8x relation |
| `test/lint-debug-headers.sh` | row 5 of the regression preflight; `make harness-selftest` HS-57a/b prove it stays wired and that its verdict reddens the row | no published header reaches `core/emulator.h`, `src/platform/`, Qt, SDL, `memory/mmu.h`, `video/renderer.h`, `video/palette.h`, `video/timing.h`, `debug/debug_state.h` or `debug/breakpoints.h` — transitively, via `-M` |

## What is declared but is NOT a §4 CAP row

**Five** kinds of thing, each present because some §4 row is unusable without it
— listed here so a reviewer can check the count rather than hunt:

1. **Shared scalars and sentinels**: `ClientId`, `EventId`, `CLIENT_NONE`,
   `EVENT_NONE`, `PAGE_ANY`, `FRAME_EVERY`. (The four enum COUNTS are item 4,
   not here — listing them twice in a list whose stated purpose is to be
   countable would defeat it.)
2. **The `{ptr, size}` views** `ConstBytes` / `ConstU16s` / `ConstU32s` — the
   §4-preamble-mandated replacement for `std::span` in C++17, used by INS-08,
   INS-09 and INS-14.
3. **`Result` plumbing**: `ok()`, `result_name()`, `Expected<T>`, `make_ok()`,
   `make_refused()` — the §4-preamble "every verb returns a `Result`" rule made
   usable.
4. **Four trailing `Count` sentinels** — `EventKind::Count`, `Layer::Count`,
   `RegId::Count`, `ClipLayer::Count` — and the counts derived from them
   (`EVENT_KIND_COUNT`, `LAYER_COUNT`, `REG_ID_COUNT`, `CLIP_LAYER_COUNT`). Not
   members of any CAP set: each is the only C++ mechanism that makes APPENDING to
   its enum a build failure. Deriving a count from the last real enumerator
   (`Dma + 1`) is blind to an append, which the B0 review proved.
5. **Two accessors a §4 promise needs**: `Debugger::memory_reader()` (INS-11's
   "`disasm_text::*`" is unreachable from a frontend without a `DisasmReadFn`)
   and `Debugger::coverage_enabled()` (INS-20's `coverage()` is all-zero when
   off, and a caller must be able to tell that from "nothing ran").
