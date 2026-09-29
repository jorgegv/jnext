#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — the one facade every debugger frontend holds.
//
// Realises §4 of doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md in full (§4.1
// CAP-CTL, §4.2 + §4.2a CAP-INS, §4.3 CAP-EVT, §4.4 CAP-TIME, §4.5 CAP-IN /
// CAP-CAP, §4.6 CAP-ST, §4.7 CAP-SYM, §4.8 CAP-SES) for work package B0
// (§10.1). Every declaration carries the CAP id it serves; the full map is
// doc/design/debug-subsystem/b0-cap-traceability.md.
//
// B0 IS THE FROZEN INTERFACE AND NOTHING ELSE. There are no bodies behind these
// verbs; B1..B5 put them there. A frontend that needs a signature this file
// lacks files a finding against B0 rather than declaring its own.
//
// ── FIVE RULES THIS FILE IS THE STATEMENT OF ────────────────────────────────
//
//  1. NO `Emulator*` BELOW A FRONTEND. `Emulator` is forward-declared; nothing
//     in the four published headers reaches `core/emulator.h`, `src/platform/`,
//     Qt, SDL, `memory/mmu.h`, `video/renderer.h`, `video/palette.h`,
//     `video/timing.h`, `debug/debug_state.h` or `debug/breakpoints.h`. That is
//     the epic, and it is GATED: `test/lint-debug-headers.sh` compiles a
//     one-line translation unit per published header and asserts the forbidden
//     set is absent from its `-M` dependency list (`-M`, not `-MM`: `-MM` omits
//     system headers, which would make the SDL arm dead — the lint's own header
//     explains it at length). It is row 5 of the regression preflight
//     (`lint-debug-headers`).
//
//     §9's `grep -l 'core/emulator.h' src/debugger/*.cpp` is a DIFFERENT gate,
//     for a different package: it greps the Qt panels' sources, and says
//     nothing about these headers. Citing it here was the mistake the B0 review
//     caught — adding `#include "core/emulator.h"` to `inspect.h` compiled
//     clean and nothing in the tree noticed.
//
//  2. EVERY VERB THAT CAN REFUSE SAYS SO. `Result`, or `Expected<T>` when it
//     also yields data. `result.h`'s banner states the rule and the one class
//     of exception — a pure query that cannot refuse — and the
//     "DIRECT-VALUE QUERIES" banner below enumerates that class in full.
//
//  3. EVERY TRANSITION AND EVERY MUTATION IS ATTRIBUTED. §4.1 requires each
//     transition to be broadcast with the originating client id, and §4.2a
//     requires every mutation to be logged `MUTATE <what> <old> -> <new> by
//     <client>`. So a verb that pauses, resumes, steps, resets, loads or writes
//     takes `ClientId by` FIRST. Reads do not. The backend itself passes
//     `CLIENT_NONE`.
//
//  4. NO OWNERSHIP TOKEN, NO ARBITRATION QUEUE. One machine, N clients, "last
//     verb wins" (§4.1). Subscriptions are owned by their creating client and
//     editable only by their owner; everything else any client may do.
//
//  5. NO REENTRANCY. `pump()` is never called from inside `run_frame()`, and a
//     `Handler` (which DOES run inside a delivery, at a boundary, with the
//     machine stopped) may not issue a control verb (§5).
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "debug/events.h"
#include "debug/inspect.h"
#include "debug/result.h"
#include "debug/symbol_table.h"

class Emulator;

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// Session value types — §4.8 CAP-SES
// ---------------------------------------------------------------------------

/// Which frontend a client is (SES-01's `ClientInfo::kind`).
///
/// FIVE consumer enumerators, not six: §1.1 names six consumers, but the sixth
/// — the record/replay recorder — is `#20` folded into the DSL as its WP6
/// (§10.1's `R` row), so it attaches as `Script` rather than as a kind of its
/// own. Plus `Test`, for `debugger_backend_test`'s fake clients: six
/// enumerators total.
enum class ClientKind : uint8_t {
    /// The Qt GUI adapter — attached while its debugger window is open (an
    /// attach arms the machine; qt-frontend.md §4.1). SES-01 needs no "last
    /// client" condition: its release rule is per client.
    Gui = 0,
    /// The DZRP server (DeZog, ZX Basic Studio, `tools/cspect_dzrp`).
    Dzrp,
    /// The ZRCP server (DeZog's `zesarux` remote, telnet).
    Zrcp,
    /// The GDB remote-serial-protocol server (`z88dk-gdb`).
    GdbRsp,
    /// The scripting DSL engine, and the #20 recorder it carries.
    Script,
    /// A test harness.
    Test,
};

/// Who is attaching (SES-01). `name` is what a log line and a client list show
/// — "DeZog 3.7.4", "Qt GUI", a script's file name.
struct ClientInfo {
    std::string name;
    ClientKind  kind = ClientKind::Test;
};

/// Severity of an SES-06 message. Mirrors the spdlog levels the project already
/// uses, so a frontend can route straight to its own logger.
enum class LogLevel : uint8_t { Trace = 0, Debug, Info, Warn, Error };

/// What a `Stop` action does, in this frontend (SES-04). Set by the LOOP OWNER,
/// never by an adapter.
enum class StopPolicy : uint8_t {
    /// Pause and notify. Qt.
    Pause = 0,
    /// Log the event and request exit with a non-zero code. SDL and
    /// `--headless`: the SDL frontend has no pause path at all, and a headless
    /// run is a CI verdict. Overridden to `Pause` + notify while a remote
    /// client is connected, which is the only way a `--dzrp-port` headless
    /// session can be stepped.
    ExitNonZero,
};

/// How long the loop owner lends its thread to socket I/O in one `pump()`
/// (SES-03).
///
/// HOST SERVICE PARAMETERS, NOT EMULATION SEMANTICS. Nothing in the emulated
/// timeline depends on any of these; changing them changes latency and CPU use
/// and nothing else. `PumpBudget{}` — all zero — is the "never block" form the
/// Qt and SDL loops pass every tick while running.
struct PumpBudget {
    /// Block up to this long waiting for the FIRST complete command. 0 =
    /// poll and return. A headless loop paused with a remote attached passes
    /// ≤50 ms here, which is what turns its busy spin into a `poll()`.
    int max_wait_ms = 0;
    /// While paused, after answering a command, wait up to this long for the
    /// next one and answer it too (~2 ms). A DeZog ZRCP step is ~15 sequential
    /// round trips; at one per tick that is 300 ms.
    int drain_ms = 0;
    /// Hard ceiling on the whole drain (~10 ms).
    int budget_ms = 0;
};

/// What `pump()` tells the loop owner (SES-03).
///
/// ADVISORY. The loop owner MAY shorten its cadence while `paused &&
/// remote_attached`; whether that is needed is measured against real DeZog
/// before it is adopted (§11).
struct ServiceHint {
    bool remote_attached = false;
    bool paused          = false;
};

/// What the backend pushed to a listener when the machine stopped (SES-02).
struct PausedInfo {
    /// The client whose verb or subscription caused it.
    ClientId    by = CLIENT_NONE;
    PauseReason reason;
    uint64_t    cycle = 0;
    uint16_t    pc    = 0;
    /// EVERY subscription that matched at this boundary, transient ones
    /// included. "Temp beats user" is adapter policy over this list.
    std::vector<Hit> matched;
};

/// The session push interface (SES-02).
///
/// SYNCHRONOUS, ON THE EMULATION THREAD, from inside `pump()` or `run_frame()`.
/// An implementation must return promptly and do no UI work — the Qt listener
/// records and acts on its own tick.
///
/// Pure virtual throughout, deliberately: B0 has no bodies, and a silently
/// ignored notification is the failure mode a default empty override invites.
/// Adding a notification later is meant to break every implementer, and
/// `debug_types_check.cpp` gates that per METHOD rather than per class: it
/// declares seven stubs, each overriding six of the seven and omitting a
/// different one, and asserts each is still abstract. `is_abstract<Listener>`
/// alone would not have — giving any single method a default empty body keeps
/// the class abstract and that assert passing.
class Listener {
public:
    virtual ~Listener() = default;

    /// §4.8 `Paused{by, reason, cycle, pc, matched[]}`.
    virtual void on_paused(const PausedInfo& info)                = 0;
    /// §4.8 `Resumed{by}`.
    virtual void on_resumed(ClientId by)                          = 0;
    /// §4.8 `Reset{kind}`. A `Reset{Hard}` is how an adapter whose client is
    /// blocked in a `run` completes that reply — adapter policy, not a pause.
    virtual void on_reset(ResetKind kind)                         = 0;
    /// §4.8 `FrameEnded{frame}`.
    virtual void on_frame_ended(uint32_t frame)                   = 0;
    /// §4.8 `SubscriptionsChanged{kinds}` — the successor of
    /// `BreakpointSet`'s observer contract.
    virtual void on_subscriptions_changed(EventKindMask kinds)    = 0;
    /// §4.8 `ExitRequested{code}` — what a `Stop` becomes under
    /// `StopPolicy::ExitNonZero`.
    virtual void on_exit_requested(int code)                      = 0;
    /// §4.8 `Log{level, text}` — SES-06's sink, including the §4.2a `MUTATE`
    /// lines the backend emits for every client's writes.
    virtual void on_log(LogLevel level, const std::string& text)  = 0;
};

/// What one `Service::service_once()` call achieved.
enum class ServiceStep : uint8_t {
    /// Nothing complete was available within the wait — stop draining.
    Idle = 0,
    /// One complete command was executed and its reply written; there may be
    /// more.
    Serviced,
};

/// A socket adapter `pump()` drives (SES-03, package T).
///
/// The backend owns the DRAIN POLICY (how many commands, for how long); a
/// service owns its wire. It executes each command synchronously against the
/// `Debugger` on the emulation thread — there is no queue the backend inspects,
/// which is what keeps protocol content out of `src/debug/`.
///
/// §4.8/§9 name the type but not its members; this is the minimum shape the
/// §9 pump rows describe ("a queued command chain is drained in one `pump`
/// while paused, `pump(0)` while running services exactly one").
class Service {
public:
    virtual ~Service() = default;

    /// Accept connections, read what is available, and execute AT MOST ONE
    /// complete command, writing its reply. Blocks at most `wait_ms`.
    virtual ServiceStep service_once(int wait_ms) = 0;

    /// Write out queued notifications (a `Paused` turned into a packet). Called
    /// once per `pump()`, after the drain, so a stop in this tick's frames is
    /// notified in this tick.
    virtual void flush_notifications() = 0;

    /// Does this service currently have a connected peer? Feeds
    /// `ServiceHint::remote_attached` and the SES-04 stop-policy override.
    virtual bool peer_connected() const = 0;
};

/// The frontend sequences the backend cannot reach (SES-07).
///
/// Both live in `src/platform/`, the layer ABOVE the backend, so the loop owner
/// registers them as closures rather than the backend including them. Neither
/// takes an `EmulatorConfig`: `emulator_frontend_cold_boot()` derives its config
/// from the frontend's own startup config plus live host-side settings, none of
/// which the backend has or should have — so the loop owner closes over it.
struct LoopDriver {
    /// Run the frontend's full cold boot — destroy and placement-new the
    /// `Emulator`, re-run `init()`, re-bind and re-enumerate the host adapters
    /// (`emulator_frontend_cold_boot()` + `ColdBootHooks`). Returns false if the
    /// boot failed. Called SYNCHRONOUSLY inside `pump()`, in the same
    /// post-frames slot, so later commands in the same drain see the new
    /// machine.
    std::function<bool()> cold_boot;

    /// Apply a load file by extension — `emulator_apply_load()`'s dispatch,
    /// the single source of truth for the format table. Returns the loader's
    /// success flag.
    std::function<bool(const std::string& path)> load;
};

// ---------------------------------------------------------------------------
// The facade
// ---------------------------------------------------------------------------

/// The one type a debugger frontend holds (§4 preamble).
///
/// Constructed with an `Emulator&` and living as long as the process: a CTL-12
/// `Hard` reset reconstructs the machine IN PLACE, and the `Debugger` survives
/// it — every client's subscriptions, switches, attach / `live_raster` state,
/// trace / call-stack / coverage enables and the symbol table are kept OUTSIDE
/// `Emulator` precisely so the reconstruct cannot lose them (CTL-12, CTL-15).
// ---------------------------------------------------------------------------
// DIRECT-VALUE QUERIES — the complete exception to "every verb returns a
// `Result`" (`result.h`'s banner). These 51 read live machine state that always
// exists, so they have no refusal case by construction and return their value
// directly. THE LIST IS THE AUDIT: a query not on it must return `Result` or
// `Expected<T>`, and moving one onto it is a claim that it cannot fail. The
// per-bucket counts are there so the total can be checked a line at a time —
// the first cut of this banner said "32" and listed 50, which is exactly the
// failure the list exists to prevent.
//
//   control/session (13): state, magic_breakpoint, armed,
//                     persistent_breakpoints, master_enabled, client_enabled,
//                     probe_execute, attached, live_raster, stop_policy,
//                     at_frame_boundary, rewind_enabled, rewind_range
//   registers/memory (5): registers, mmu_slots, paging_ports, nextreg_peek,
//                     nextreg_selected
//   video/audio (16): raster, machine, time, sprites, pattern_ram,
//                     sprite_clip, copper, framebuffer, palette,
//                     active_ula_palette_bank, ula_screen_regs, clip_window,
//                     turbosound_enabled, ay_mode, stereo_mode,
//                     audio_mute_mask
//   disasm/trace (9): disassemble, instruction_length, is_call_like,
//                     memory_reader, call_stack, call_stack_enabled,
//                     trace_enabled, coverage_enabled, coverage
//   model/symbols (8): input_state, subscriptions, events_fired_since, symbols,
//                     lookup, lookup_name, ula_screen_dump, bookmarks
//
// Four of those need a word, because "cannot refuse" is doing work:
// `coverage()` returns an all-zero bit set while coverage is off (hence
// `coverage_enabled()` beside it), `bookmarks(cid)` returns an empty list for a
// client id that never saved one, `disassemble()` returns an empty vector for a
// range it cannot read, and `lookup`/`lookup_name` return `std::optional` —
// absence is their answer, not a refusal.
//
// `std::optional` also carries CTL-11's `resume_blocked_by_corruption()` and
// ST-03's `rewind_blocked()`: both ask "would this be refused?", so an empty
// optional is the affirmative answer and a `Result` inside it is the reason.
// They are queries, but about refusal rather than about state, so they are not
// on the list above.
//
// ONE VERB IS IN NEITHER BUCKET, and saying so is the point of a partition:
// SES-03's `pump()` returns a `ServiceHint` directly and is NOT a query at all.
// It drains client commands, may run `LoopDriver::cold_boot` synchronously
// (CTL-12 `Hard`) and fires listener callbacks — it is the loop owner's service
// call, its `ServiceHint` is advisory rather than an answer, and the refusals of
// whatever it drives are reported through those verbs' own `Result`s. So the
// taxonomy is three buckets: refusing verbs (`Result` / `Expected<T>`),
// direct-value queries (above), and `pump()`.
// ---------------------------------------------------------------------------

class Debugger {
public:
    explicit Debugger(Emulator& emu);
    ~Debugger();

    Debugger(const Debugger&)            = delete;
    Debugger& operator=(const Debugger&) = delete;
    Debugger(Debugger&&)                 = delete;
    Debugger& operator=(Debugger&&)      = delete;

    // =======================================================================
    // §4.1 — CAP-CTL, control
    // =======================================================================

    /// CTL-01 — pause at the next instruction boundary. Idempotent. Called from
    /// `pump()`, the machine is already at a boundary.
    Result pause(ClientId by);

    /// CTL-02 — resume. A no-op that still returns `Ok` if already running
    /// (GH #223). The GH #221 step-off arm is the BACKEND's: an adapter never
    /// inserts its own step-off breakpoint. Subject to CTL-11.
    Result run(ClientId by);

    /// CTL-03 — execute exactly one instruction, SYNCHRONOUSLY: frame-loop
    /// aware, and it runs a HALT out rather than stalling on it (GH #207).
    Result step_into(ClientId by);

    /// CTL-04 — over a CALL / RST / DJNZ: a transient `Execute` at the next PC
    /// plus `run()`; anything else behaves as CTL-03. ASYNCHRONOUS — the stop
    /// arrives as a `Paused` push.
    Result step_over(ClientId by);

    /// CTL-05 — run until the return to the current stack depth (GH #203).
    /// ASYNCHRONOUS.
    Result step_out(ClientId by);

    /// CTL-06 — transient `Execute[addr,addr]` plus `run()`. "Run to Here", and
    /// RSP's `i<len>` is `run_to(pc + len)`.
    Result run_to(ClientId by, uint16_t addr);

    /// CTL-07 / TIME-03 — run until the master cycle counter reaches
    /// `master_cycle`. The primitive under CTL-08.
    Result run_to_cycle(ClientId by, uint64_t master_cycle);

    /// TIME-03 — run until frame `frame` begins. Forward only; the backward
    /// verb is CTL-10.
    Result run_to_frame(ClientId by, uint32_t frame);

    /// CTL-08 — run to the end of the current frame.
    Result run_to_end_of_frame(ClientId by);

    /// CTL-08 — run to the end of the current scanline.
    Result run_to_end_of_scanline(ClientId by);

    /// CTL-09 / ST-04 — step back `n` instructions. SYNCHRONOUS.
    /// `RefusedRzx` (an RZX is recording or playing), `RefusedUnavailable`
    /// (empty buffer, or a target inside a span a debugger mutation touched —
    /// §4.2a) and `RefusedCorrupt` are distinguished.
    Result step_back(ClientId by, uint32_t n);

    /// CTL-10 / ST-04 — rewind to the start of frame `frame`. SYNCHRONOUS, same
    /// refusal set as CTL-09.
    Result rewind_to_frame(ClientId by, uint32_t frame);

    /// CTL-11 — is a resume currently blocked by an unacknowledged corruption?
    /// The `ResumeGuard` policy; the modal stays in Qt, and an unacknowledged
    /// remote gets `RefusedCorrupt` from `run()` / the step verbs.
    std::optional<CorruptionIncident> resume_blocked_by_corruption() const;

    /// CTL-11 — acknowledge the incident identified by `generation`. A later,
    /// fresh incident re-blocks.
    Result acknowledge_corruption(uint64_t generation);

    /// CTL-12 — `Soft` is `Emulator::soft_reset()`, synchronous. `Hard` is the
    /// COLD-BOOT RECONSTRUCT CONTRACT: run the registered `LoopDriver::cold_boot`
    /// synchronously inside `pump()`, re-bind and re-apply every client's
    /// subscriptions / switches / attach / `live_raster` / trace / call-stack /
    /// coverage enables and the symbol table, re-apply the pause if the CALLER
    /// was paused (at PC 0x0000 of `nextboot.rom`), NEVER pause a running
    /// machine (there is no `Reset` pause reason), and deliver `Reset{Hard}` to
    /// every listener before returning. No driver → `RefusedUnavailable`.
    Result reset(ClientId by, ResetKind kind);

    /// CTL-13 — what the machine is doing right now: paused, step mode, why it
    /// stopped, and where. THE most-polled query in the API — a panel refreshes
    /// on it after a `Paused` push, a protocol server turns it into a stop
    /// reply, and a script reads `REASON` from it.
    RunState state() const;

    /// CTL-14 — is the magic breakpoint armed?
    bool magic_breakpoint() const;

    /// CTL-14 — arm or disarm it.
    Result set_magic_breakpoint(bool enabled);

    /// CTL-15 — load a program, routed through `LoopDriver::load`. A load that
    /// reconstructs the machine honours the same contract as CTL-12 `Hard`. No
    /// driver → `RefusedUnavailable`. A paused caller stays paused, at the new
    /// PC.
    Result load(ClientId by, const std::string& path);

    /// §4.1 — is the step machinery live? The hot loop's gate: when it is
    /// false the backend is inert and costs what today's `DebugState::armed()`
    /// costs.
    ///
    /// **`attached || persistent_breakpoints`** — §5's formula, and the whole
    /// formula. The magic breakpoint is NOT a third term, though §4.1 once read
    /// that way: it does not need one, because the magic hook lives on
    /// `Z80Cpu::on_magic_breakpoint` rather than behind the armed gate, so it
    /// fires on an unarmed machine and SETS `active_` when it does. So
    /// `--magic-breakpoint` alone leaves `armed()` false until the opcode
    /// executes, which is the behaviour wanted and the reason the gate can stay
    /// two-termed (settled, Revision 6).
    bool armed() const;

    /// §4.1 — the `--persistent-breakpoints` half of `armed()` (GH #219): keep
    /// subscriptions live with no client attached.
    Result set_persistent_breakpoints(bool enabled);
    bool persistent_breakpoints() const;

    // =======================================================================
    // §4.2 / §4.2a — CAP-INS, inspection and mutation
    //
    // OBSERVATION IS SIDE-EFFECT FREE BY CONTRACT. A read never advances a
    // latch, never fires a watch and never perturbs the machine — F1 (a
    // `peek(Cpu)` sweep in +3 mode leaves `p3_floating_bus_dat_` unchanged) is
    // a backend test row, not an aspiration. INS-05 `port_in` is the one
    // exception and says so.
    //
    // MUTATION IS FIRST-CLASS AND LANDS AT A DELIVERY POINT. From a frontend
    // command the machine is paused at a boundary. From an `Execute` handler the
    // write is seen by the instruction at PC. From every other kind the raising
    // instruction has already completed and the write lands before the next
    // one. Nothing a handler writes can reach the raising instruction or the
    // device cluster that already ran for it. Every mutation is logged
    // `MUTATE <what> <old> -> <new> by <client>` BY THE BACKEND, fires no event,
    // and is refused under RZX.
    // =======================================================================

    /// INS-01 — the whole register file, by value.
    Z80Registers registers() const;

    /// INS-01 / §4.2a — write ONE register. No "set all". For an 8-bit `RegId`
    /// the low byte of `value` is used. `RegId::PC` additionally clears
    /// `halted`.
    Result set_register(ClientId by, RegId reg, uint16_t value);

    /// INS-02 — read `n` bytes from `space` at `addr` into `buf`. Returns the
    /// count read; a short or refused read still reports what it managed.
    /// `InvalidPage` for a `Page` sentinel or an out-of-range page.
    Expected<size_t> peek(MemSpace space, uint32_t addr, size_t n, uint8_t* buf) const;

    /// INS-02 / §4.2a — write `n` bytes. `Cpu` goes through `Mmu::write`
    /// outside any `GuestExecutionScope`: through the LIVE map, OVERLAYS
    /// HONOURED, ROM ignored, per-scanline change logs and the attribute mux
    /// updated, no latch, no event. `Page` addresses the physical page
    /// REGARDLESS of any overlay. `Rom` is `RefusedReadOnly`, as is a ROM-class
    /// page under `Page`.
    Expected<size_t> poke(ClientId by, MemSpace space, uint32_t addr, size_t n,
                          const uint8_t* buf);

    /// INS-03 — the eight slots, each carrying the COMPLETE address of its
    /// backing store (`space` + `space_offset`), so no client composes a
    /// `MemSpace` itself.
    std::array<SlotInfo, 8> mmu_slots() const;

    /// INS-03 / §4.2a — point slot `slot` (0..7) at NR page `page`.
    Result set_mmu_slot(ClientId by, int slot, uint8_t page);

    /// INS-03 — the legacy paging ports as last written.
    PagingPorts paging_ports() const;

    /// INS-04 — read a NextREG through its READ path, never a destructive one.
    uint8_t nextreg_peek(uint8_t reg) const;

    /// INS-04 / §4.2a — write a NextREG, running the register's own write
    /// handler synchronously, side effects included. Source `Debugger`, so it
    /// fires no `NextRegWrite` event on itself.
    Result nextreg_write(ClientId by, uint8_t reg, uint8_t value);

    /// INS-04 — the register port 0x243B last selected.
    uint8_t nextreg_selected() const;

    /// INS-05 — dispatch a port READ like a guest `IN`. PERTURBING BY NATURE
    /// (it advances device state), which is why it takes a `by` and why the DSL
    /// declines it.
    Expected<uint8_t> port_in(ClientId by, uint16_t port);

    /// INS-05 / §4.2a — dispatch a port write like a guest `OUT`.
    Result port_out(ClientId by, uint16_t port, uint8_t value);

    /// INS-06 — where the beam is and what the ULA is fetching, computed from
    /// the live `VideoTiming` (GH #22).
    RasterState raster() const;

    /// INS-07 / TIME-01 — the deterministic clock.
    Time time() const;

    /// INS-08 — the 128 decoded sprites.
    std::vector<SpriteInfo> sprites() const;

    /// INS-08 — one sprite's raw 5-byte attribute set, exactly as the engine
    /// holds it.
    Expected<std::array<uint8_t, SPRITE_ATTR_BYTES>> sprite_attr_raw(uint8_t idx) const;

    /// INS-08 / §4.2a — write one sprite's raw attributes through the engine's
    /// setter, not as port traffic. `n` must be `SPRITE_ATTR_BYTES`.
    Result set_sprite_attr_raw(ClientId by, uint8_t idx, const uint8_t* bytes, size_t n);

    /// INS-08 — the 16 KB pattern RAM, as a view.
    ConstBytes pattern_ram() const;

    /// INS-08 / §4.2a — write pattern RAM through the engine's setter.
    Result write_pattern_ram(ClientId by, uint16_t addr, const uint8_t* src, size_t n);

    /// INS-08 — one sprite-palette entry, RGB333.
    Expected<uint16_t> sprite_palette_rgb333(int bank, uint8_t index) const;

    /// INS-08 — the sprite clip window, from live layer state.
    ClipWindow sprite_clip() const;

    /// INS-09 — the Copper's PC, run state, mode and instruction RAM.
    CopperState copper() const;

    /// INS-10 — one chip's 16 registers.
    Expected<std::array<uint8_t, AY_REGISTER_COUNT>> ay_registers(int chip) const;

    /// INS-10 — live signals, not cached NextREG shadows.
    bool       turbosound_enabled() const;
    AyChipMode ay_mode() const;
    StereoMode stereo_mode() const;

    /// INS-10 — the host-side mute mask (`AudioMute::*`). Never in a snapshot or
    /// an RZX.
    uint8_t audio_mute_mask() const;

    /// INS-10 / §4.2a — set it.
    Result set_audio_mute_mask(ClientId by, uint8_t mask);

    /// INS-11 — disassemble `n` instructions from `addr` over `peek(Cpu)`.
    /// `symbols` may be null; pass `&symbols()` for the backend's own table.
    std::vector<DisasmLine> disassemble(uint16_t addr, size_t n,
                                        const SymbolTable* symbols) const;

    /// INS-11 — instruction length at `addr`, without a full disassembly.
    int instruction_length(uint16_t addr) const;

    /// INS-11 — is the instruction at `addr` CALL-like? What CTL-04 asks.
    bool is_call_like(uint16_t addr) const;

    /// INS-11 — a reader over `peek(Cpu)`, so `disasm_text::collect_range()` and
    /// `disasm_text::apply_symbols()` (already published, already Qt-free) can be
    /// driven with no `Emulator*`. Without this the INS-11 promise of
    /// "`disasm_text::*`" is unreachable from a frontend.
    DisasmReadFn memory_reader() const;

    /// INS-12 — the shadow call stack, most recent first.
    const std::vector<CallFrame>& call_stack() const;

    /// INS-12 — tracking costs per instruction; off by default.
    bool   call_stack_enabled() const;
    Result set_call_stack_enabled(bool enabled);

    /// INS-13 — the trace log.
    bool   trace_enabled() const;
    Result set_trace_enabled(bool enabled);
    Result trace_clear();
    Result trace_resize(size_t entries);

    /// INS-13 — the recorded entries, oldest first. `RefusedUnavailable` when
    /// the trace is off. A copy, not a view: the underlying buffer is circular,
    /// so there is no contiguous range to hand out.
    Expected<std::vector<TraceEntry>> trace_entries() const;

    /// INS-13 — write the trace to a file.
    Result trace_export(const std::string& path) const;

    /// INS-14 — the composited framebuffer, as a view. `RENDER_WIDTH` pixels
    /// per row.
    ConstU32s framebuffer() const;

    /// INS-14 — draw one layer's rows 0..vc into `dst` over a 0x00000000 fill
    /// (alpha 0 = transparent). `stride_pixels` is the destination's row
    /// stride; `RENDER_WIDTH` pixels are written per row. The Qt-free successor
    /// of `video_panel.cpp`'s `render_to_image` + `replay_*`.
    Result render_layer(Layer layer, int vc, uint32_t* dst, size_t stride_pixels) const;

    /// INS-15 — one palette bank's entries, RGB333. `PaletteId::UlaActive`
    /// resolves to whichever ULA bank NR 0x43 has selected.
    std::vector<uint16_t> palette(PaletteId id) const;

    /// INS-15 / §4.2a — write one palette entry.
    Result set_palette(ClientId by, PaletteId id, uint8_t index, uint16_t rgb333);

    /// INS-15 — 0 or 1: which ULA palette bank is active.
    uint8_t active_ula_palette_bank() const;

    /// INS-15 — the ULA's screen-selection state.
    UlaScreenRegs ula_screen_regs() const;

    /// INS-15 — a clip window from LIVE layer state, not from the rotating
    /// NR 0x18-0x1C write shadows.
    ClipWindow clip_window(ClipLayer layer) const;

    /// INS-16 — the whole input surface in one read, for the #20 recorder.
    InputState input_state() const;

    /// INS-17 — the subscription model, disabled entries included, with owners.
    /// `include_transient` false hides the transient ones, as a user list must.
    std::vector<SubscriptionInfo> subscriptions(bool include_transient) const;

    /// INS-17 — every event delivered since sequence `seq` (see `Event::seq`),
    /// for a client that polls rather than handles.
    std::vector<Event> events_fired_since(uint64_t seq) const;

    /// INS-18 / §4.2a — set the border colour directly (`Ula::set_border`).
    /// DZRP sends this on every load.
    Result set_border(ClientId by, uint8_t colour);

    /// INS-19 / TIME-01 — the per-machine constants, from the live timing.
    MachineInfo machine() const;

    /// INS-20 — PC coverage. One bit-set per instruction inside the
    /// attached-gated branch; zero cost when off.
    Result coverage_enable(bool enabled);
    bool   coverage_enabled() const;
    Result coverage_clear();

    /// INS-20 — the executed-PC bit set. All-zero while coverage is off. By
    /// const reference: it is 8 KB.
    const CoverageBits& coverage() const;

    // =======================================================================
    // §4.3 — CAP-EVT, events, breakpoints, conditions
    // =======================================================================

    /// CAP-EVT — register a subscription owned by `by`. Today's PC breakpoints
    /// and watchpoints are single-address `Execute` / `Mem` subscriptions owned
    /// by the Qt client.
    Expected<EventId> subscribe(ClientId by, const Subscription& sub);

    /// CAP-EVT — remove one. Only its owner may; another client gets
    /// `RefusedUnavailable`.
    Result unsubscribe(ClientId by, EventId id);

    /// CAP-EVT — set a subscription's own enable flag. Owner only.
    Result set_enabled(ClientId by, EventId id, bool enabled);

    /// CAP-EVT — the master switch (GH #225). Suspends every subscription
    /// without deleting one and without touching any per-subscription flag;
    /// `transient` subscriptions are EXEMPT, so Step Over and Run to Here keep
    /// working while the user has everything suspended.
    bool   master_enabled() const;
    Result set_master_enabled(bool enabled);

    /// CAP-EVT — the per-client switch. Live = master ∧ client ∧ own flag,
    /// rebuilt on change, never evaluated per instruction.
    bool   client_enabled(ClientId cid) const;
    Result set_client_enabled(ClientId cid, bool enabled);

    /// CAP-EVT — "would an `Execute` subscription match here?" A PURE query for
    /// step loops: the GH #221 step-off arm skips exactly the address it landed
    /// on, and needs to know whether one is there without arming anything.
    bool probe_execute(uint16_t pc) const;

    /// CAP-EVT `Host` — raise a named host event: a bound host key,
    /// `script1`..`script8`. `Unsupported` for a name longer than
    /// `MAX_HOST_EVENT_NAME` — never silently truncated.
    Result raise_host_event(ClientId by, const std::string& name);

    // =======================================================================
    // §4.5 — CAP-IN input injection, CAP-CAP capture
    // =======================================================================

    /// IN-01 — press a named key for `hold_frames` frames, as a PULSE with
    /// APPEND semantics: queued behind whatever is already in flight (the
    /// 4-frame released gap between entries stays), so a pulse issued while one
    /// is held is never stranded down and two pulses due in one frame both
    /// happen. Honours `Keyboard::MAX_AUTO_TYPE_KEYS` with the same loud
    /// truncation: on overflow, `RefusedUnavailable` AND the count that was
    /// queued. Never resets the in-flight entry's frame counters.
    Expected<size_t> press_key(ClientId by, const std::string& name, int hold_frames);

    /// IN-01 — the same, by matrix position (single or compound).
    Expected<size_t> press_key(ClientId by, const MatrixKey& key, int hold_frames);

    /// IN-02 — LEVEL, not a pulse: for replay of recorded state. The DSL's bare
    /// `press` / `release`; only `press … for n` is IN-01.
    Result set_key(ClientId by, int row, int col, bool pressed);

    // INJECTION ORDERING, a CAP-IN contract over both verbs above: every pulse
    // append and every level set issued during frame N — from a `Frame` handler,
    // from a remote command in that tick's `pump`, or from a `--delayed-*`
    // countdown — is applied in `end_of_frame` BEFORE the auto-type state machine
    // ticks. So a pulse issued at the edge of frame N is pressed at that edge and
    // visible to the guest from frame N+1: the same frame
    // `--delayed-keypress-frames N` lands on today. The other order shifts every
    // existing `--delayed-keypress-frames` regression row by one frame.

    /// IN-02 — one of the 16 extended keys. `id` is the `Keyboard::ExtKey`
    /// numbering (0..15, aligned 1:1 with the NR 0xB0 / 0xB1 readback bits).
    ///
    /// A bare `int` DELIBERATELY, not `Keyboard::ExtKey`: taking the enum would
    /// put `input/keyboard.h` in this header's include graph, and that header
    /// reaches `<SDL3/SDL.h>` — which would hand every frontend, and every
    /// remote server, an SDL dependency to satisfy the type of one parameter.
    /// Out-of-range ids are refused, not ignored.
    Result set_extended_key(ClientId by, int id, bool pressed);

    /// IN-03 — a connector's 12-bit button state.
    Result set_joystick(ClientId by, JoystickSide side, uint16_t bits12);

    /// IN-04 — the GH #209 NMI-button seam (F9 Multiface / F10 DivMMC).
    Result press_nmi(ClientId by, NmiButton button);

    /// CAP-01 — capture the screen. DEFERRED TO THE NEXT RENDERED FRAME for
    /// every frontend, never the stale framebuffer. `NoFrame` when an exit bound
    /// cuts the deferral off. `layer_mask` is the `LAYER_MASK_*` bits; a
    /// `Scr` capture accepts only `LAYER_MASK_ALL`.
    Result screenshot(ClientId by, const std::string& path, uint8_t layer_mask,
                      ScreenshotFormat format);

    /// CAP-01 — THE EXIT BOUND for `by`'s deferred captures: what `screenshot()`
    /// cannot answer, because it returns before the capture is taken. `NoFrame`
    /// if any capture `by` queued is still pending — those are DROPPED (logged,
    /// never taken later); otherwise `RefusedUnavailable` if any of `by`'s
    /// captures since the previous call failed to write; otherwise `Ok`. Per
    /// client: another client's captures, pending or failed, are untouched.
    /// Safe from inside an event delivery — it executes, rewinds and replaces
    /// nothing.
    ///
    /// ADDED BY B4 (owner decision 2026-09-28, B4 report O1) — the second change
    /// to this frozen header after B0: `NoFrame` ("the exit bound cut the
    /// deferral off") had no carrier, so a loop owner could not turn a capture
    /// that failed, or never came, into the non-zero exit its `--delayed-*`
    /// flags promise.
    Result flush_captures(ClientId by);

    /// CAP-02 — the ULA layer's screen memory as a `.SCR` image
    /// (`Ula::screen_dump()`). Layer 2, tilemap and pattern RAM come out of
    /// `peek(MemSpace::page(...))` instead.
    std::vector<uint8_t> ula_screen_dump() const;

    /// CAP-03 — save a named in-memory bookmark for `by`. A full snapshot at
    /// the rewind slot size (the machine's RAM plus subsystem state), allocated
    /// on first use, bounded at 8 per client (`RefusedUnavailable` beyond).
    Result bookmark_save(ClientId by, const std::string& name, SaveStateMode mode);

    /// CAP-03 — restore one of `by`'s own bookmarks. A bookmark survives a
    /// CTL-12 `Hard` reconstruct (the bytes are backend-owned and tagged with
    /// machine type and width); a restore into a machine whose type or width
    /// differs is refused `RefusedUnavailable` BEFORE `load_state` runs, never
    /// left to a sentinel check.
    Result bookmark_restore(ClientId by, const std::string& name);

    /// CAP-03 — a client's own bookmark names. They die with its `detach`.
    std::vector<std::string> bookmarks(ClientId cid) const;

    /// CAP-04 — write a snapshot file at the next frame boundary (the
    /// `--delayed-snapshot` path).
    Result save_snapshot(ClientId by, const std::string& path);

    // =======================================================================
    // §4.6 — CAP-ST, state bookmarks and reverse execution
    // =======================================================================

    /// ST-01 — is the machine at a frame boundary right now?
    bool at_frame_boundary() const;

    /// ST-01 — serialise the machine. FRAME-BOUNDARY ONLY:
    /// `AdvanceToBoundary` runs the #27 S6 `SuspendScope` advance,
    /// `RefuseMidFrame` returns `NotAtFrameBoundary`.
    ///
    /// TAKES A `ClientId by` — SETTLED (owner decision; B0 left it open and the
    /// answer is that the advance must be attributed).
    ///
    /// A save is a read, so rule 3 of this file's banner would not ask for
    /// attribution on its own. `AdvanceToBoundary` is what does: it ADVANCES the
    /// machine, so emulated time moves, observably to every other attached
    /// client, and every other state change in this API carries a `by` and emits
    /// the SES-06 `MUTATE … by <client>` line. Without it a CI transcript can
    /// show time jumping with nothing recording who caused it.
    ///
    /// `bookmark_save`'s `by` is NOT the parallel argument — it needs one
    /// anyway, because bookmarks are per client and die with a detach. The point
    /// here is attribution of the ADVANCE, which is why the `by` is meaningful
    /// only in `AdvanceToBoundary` mode: `RefuseMidFrame` advances nothing and
    /// has nothing to attribute. The SES-06 line for the advance is B4's, with
    /// the rest of the CAP-ST work.
    Expected<std::vector<uint8_t>> save_state_bytes(ClientId by, SaveStateMode mode);

    /// ST-02 — restore from bytes, IN-PROCESS ONLY and unversioned (the disk
    /// format is JNS, #27). A failure LATCHES corruption, which CTL-11 then
    /// gates resumption on.
    Result load_state_bytes(ClientId by, const uint8_t* data, size_t n);

    /// ST-03 — the rewind buffer.
    bool   rewind_enabled() const;
    Result set_rewind_enabled(bool enabled);
    RewindRange rewind_range() const;

    /// ST-03 — why a rewind would be refused right now, for PRE-CLICK greying:
    /// the same `Result` the verb would return, or empty if it would succeed.
    std::optional<Result> rewind_blocked() const;

    /// ST-03 — resize the ring, in frames.
    Result resize_rewind_buffer(size_t frames);

    // =======================================================================
    // §4.7 — CAP-SYM, symbols
    // =======================================================================

    /// CAP-SYM — load a MAP file into the ONE table the panels, the DSL's
    /// `@name`, the servers' lookups and `--map` all read. Returns the number of
    /// symbols loaded.
    Expected<int> load_map(const std::string& path, MapFormat format);

    /// CAP-SYM — forget them all.
    Result clear_symbols();

    /// CAP-SYM — name for an address, address for a name.
    std::optional<std::string> lookup(uint16_t addr) const;
    std::optional<uint16_t>    lookup_name(const std::string& name) const;

    /// CAP-SYM — the table itself, for `disasm_text::apply_symbols()` and for a
    /// frontend that lists symbols.
    const SymbolTable& symbols() const;

    // =======================================================================
    // §4.8 — CAP-SES, session
    // =======================================================================

    /// SES-01 — attach a client. Every other verb's `ClientId` comes from here.
    Expected<ClientId> attach(const ClientInfo& info);

    /// SES-01 — detach. Removes this client's subscriptions and its bookmarks,
    /// and — IFF the machine is paused BY THIS CLIENT (its `pause()`, or a
    /// `Stop` on one of its subscriptions) — resumes it. A pause by another
    /// client SURVIVES. There is no "last client" condition; the rule exists so
    /// a crashed DeZog cannot leave the machine hung.
    ///
    /// AN UNOWNED PAUSE IS NEVER RESUMED BY A DETACH: `PauseReason::Magic` and
    /// `PauseReason::Corrupt` carry `by == CLIENT_NONE` because neither is any
    /// client's verb, so no client's departure may clear them (owner decision,
    /// Revision 6 — see `PauseReason`).
    Result detach(ClientId cid);

    /// SES-02 — install (or, with nullptr, clear) a client's push listener.
    /// A pointer rather than the reference §4.8 writes, because a client that
    /// wants to stop receiving pushes without detaching has no other way to say
    /// so.
    Result set_listener(ClientId cid, Listener* listener);

    /// SES-03 — register a socket adapter for `pump()` to drive.
    Result add_service(Service& service);

    /// SES-03 — unregister one. Idempotent.
    Result remove_service(Service& service);

    /// SES-03 — the loop owner's once-per-tick service call, made AFTER the
    /// tick's frame batch (where `check_breakpoint_hit()` sits today), so a stop
    /// in this tick's frames is notified in this tick. While paused it drains
    /// per `PumpBudget`; `PumpBudget{}` while running never blocks.
    ///
    /// NEVER called from inside `run_frame()`, and never from inside an event
    /// delivery (asserted).
    ServiceHint pump(const PumpBudget& budget);

    /// SES-04 — what a `Stop` action does here. The LOOP OWNER sets it; an
    /// adapter never does.
    StopPolicy stop_policy() const;
    Result     set_stop_policy(StopPolicy policy);

    /// SES-05 — this client's live-raster request. ORed across clients; gates
    /// only the render hint and the raster walk, never the step machinery.
    Result set_live_raster(ClientId cid, bool enabled);

    /// SES-05 — the ORed value.
    bool live_raster() const;

    /// SES-05 — is any client attached? The gate on the step machinery, and the
    /// `attached` half of `armed()`.
    bool attached() const;

    /// SES-06 — the backend's message sink. Reaches every listener as
    /// `on_log()`. `CLIENT_NONE` is the backend itself — which is who emits the
    /// §4.2a `MUTATE` lines.
    Result log(ClientId by, LogLevel level, const std::string& text);

    /// SES-07 — register the loop owner's cold-boot and load sequences, both of
    /// which live above the backend. Without them CTL-12 `Hard` and CTL-15
    /// return `RefusedUnavailable`.
    Result set_loop_driver(const LoopDriver& driver);

    /// SES-07 — the loop owner reports that a DEFERRED (guest NR 0x02) cold
    /// boot is ABOUT TO destroy the machine. Call it immediately before the
    /// destroy, and `on_cold_boot_done()` after the rebuild: the backend captures
    /// here the pause in force and whose it is — exactly what `reset(Hard)`
    /// captures before its driver — so CTL-12 rule 3 ("paused stays paused")
    /// holds on the guest path too. Needs no driver; never refuses. A second call
    /// before `done` replaces the first; a `reset(Hard)` or `load()` in between
    /// discards it.
    ///
    /// ADDED BY B3 (owner decision 2026-09-28) — the one change to this frozen
    /// header after B0: without it the guest path could not honour rule 3,
    /// because by `on_cold_boot_done()` the paused machine is already gone.
    Result on_cold_boot_begin();

    /// SES-07 — the loop owner reports that a DEFERRED (guest NR 0x02) cold
    /// boot has completed, so the reconstruct contract's re-application runs for
    /// it too. After `on_cold_boot_begin()` it re-applies that capture; without
    /// one it re-applies the REBUILT machine's own pause state, unowned.
    Result on_cold_boot_done();

private:
    // ── The ONE thing B1 added to this frozen header ────────────────────────
    //
    // (B3 added one public declaration to this header: `on_cold_boot_begin()`
    // above, by owner decision on 2026-09-28 — recorded at its declaration, in
    // `doc/design/debug-subsystem/b0-cap-traceability.md` (SES-07) and in
    // `backend.md` CAP-SES-07. B4 added one more, `flush_captures()`, by owner
    // decision on the same day — recorded at its declaration, in
    // `b0-cap-traceability.md` (CAP-01) and in `backend.md` CAP-CAP-01.)
    //
    // B0 declared the constructor, an out-of-line destructor and deleted
    // copy/move, and no storage at all — the shape a pImpl is prepared for. B1
    // adds it, in two lines, and that is the whole of the state: the
    // `Emulator&`, the symbol table, the stop policy, and everything B2..B5
    // bring (the event table, the latch ring, the client list, the bookmarks,
    // the coverage bit set) live in `Impl`, defined in the INTERNAL header
    // `src/debug/debugger_impl.h`.
    //
    // Why pImpl and not members here: this header is FROZEN and five frontends
    // compile against it (§10.1). Every later sub-package would otherwise have
    // to edit it to add its own state, and each such edit is a chance to change
    // something a frontend depends on. With the state behind `Impl`, B2..B5
    // touch nothing a frontend can see. It also keeps rule 1 of this file's
    // banner structural rather than careful: `Impl` is where `core/emulator.h`
    // is included, and it is not reachable from here.
    //
    // NOT a hot-path cost: no path through `run_frame()` calls a `Debugger`
    // method. B2's hooks read the internals directly, not through this
    // indirection.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dbg
}  // namespace jnext
