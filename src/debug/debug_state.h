#pragma once

#include "debug/breakpoints.h"
#include <bitset>
#include <cstdint>
#include <functional>

namespace jnext { namespace dbg {
class EventTable;
struct LatchEntry;
} }

enum class StepMode { NONE, INTO, OVER, OUT, RUN_TO_CYCLE, STEP_BACK, RUN_BACK_TO_CYCLE };

/// Manages the debugger's execution control state: pause/resume, step modes,
/// and breakpoint checking.  Pure C++ — no GUI dependency.
class DebugState {
public:
    /// GH #278 WP4c — THE MAGIC-BREAKPOINT HOLD: the one arm contributor a guest
    /// instruction raises, and it holds exactly one thing — the pause of the
    /// magic stop that raised it.
    ///
    /// The magic hook (`Emulator::set_magic_breakpoint()`) runs inside an
    /// instruction on a machine that may be UNARMED (headless, SDL, a closed
    /// Qt window), and the hot loop honours a pause only inside its armed()
    /// block: without an arm the machine would run on to the end of the frame
    /// past the ED FF. So the hook arms the machine by this bit and pauses it,
    /// and the next resume — unpause_(), every transition out of paused —
    /// releases it. What arms the machine after that is exactly what armed it
    /// before the opcode: nothing, a client, the flag.
    ///
    /// It replaced `active()`, the Qt window's bit, which the hook used to set
    /// and nothing but the window's close ever cleared: one magic hit with no
    /// window (a remote client's run() of the magic stop, say) left the machine
    /// armed — and the step machinery and raster walk on — for the rest of the
    /// session. It is an armed() contributor ONLY: it does not switch on the
    /// step machinery (attached()) or the raster walk (raster_live()), which
    /// belong to whoever is driving the machine, and nobody is while it holds.
    void hold_for_magic_stop() { magic_hold_ = true; refresh_gates_(); }
    bool magic_hold() const { return magic_hold_; }

    /// GH #219 — `--persistent-breakpoints`: keep breakpoints live for the
    /// whole run, not just while the debugger window is open. Set once from
    /// EmulatorConfig at init(); the UI never touches it.
    bool persistent_breakpoints() const { return persistent_; }
    void set_persistent_breakpoints(bool p) { persistent_ = p; refresh_gates_(); }

    /// GH #276 B3 (SES-01/SES-05) — is at least one ARMING `jnext::dbg::Debugger`
    /// CLIENT attached (an observer, REQ-qt-32, does not count)? Published by
    /// `Debugger::Impl::clients_changed()`, its one writer.
    ///
    /// Each armed() contributor keeps its OWN bit and refresh_gates_() ORs them:
    /// a shared bit would let one owner's release clear another's arm, and this
    /// class does not know who set it. Every new contributor must be added to
    /// refresh_gates_() AND to SuspendScope, whose job is to disarm the machine
    /// whatever armed it.
    bool clients_attached() const { return clients_attached_; }
    void set_clients_attached(bool a) { clients_attached_ = a; refresh_gates_(); }

    /// GH #276 B3 (§4.1, SES-05) — THE TWO FLAGS THAT REPLACE active() AS A
    /// HOT-PATH GATE. §4.1: "`attached` (≥1 client) gates the step machinery;
    /// `live_raster` (per client, ORed) gates only the render hint and the raster
    /// walk." Both are precomputed by refresh_gates_(), exactly like armed_, so
    /// each reader pays one bool load — the same as the active() it replaces.
    ///
    /// attached() = clients_attached_. The STEP machinery (Step Out's
    /// per-instruction test, the STEP_BACK / RUN_BACK_TO_CYCLE step modes) reads
    /// it. Before B3 they read active(), so a machine driven only by a backend
    /// client — a DZRP session with the Qt window closed — never finished a
    /// Step Out (row SES-05-13).
    ///
    /// raster_live() = live_raster_. The render-every-frame hint and the
    /// per-instruction VideoTiming::advance() walk read it. The retired
    /// `active_` bit was a term of both until GH #278 WP4c; the Qt window is a
    /// client now, and switches both on through its attach and its live-raster
    /// request.
    bool attached() const { return attached_; }
    bool raster_live() const { return raster_live_; }

    /// SES-05 — the OR of every backend client's live-raster request, published
    /// by `Debugger::Impl::clients_changed()` (its one writer). NOT an armed_
    /// contributor: it gates only the raster walk and the render hint.
    bool live_raster() const { return live_raster_; }
    void set_live_raster(bool l) { live_raster_ = l; refresh_gates_(); }

    /// Are breakpoints and watchpoints LIVE? The hot-path gate.
    ///
    /// A single cached bool, so the default configuration executes exactly the
    /// same load-and-branch the old active() gate did — the flag costs nothing
    /// to the user who does not pass it. With --persistent-breakpoints it is
    /// true for the whole run: that is what "persistent" means, and the
    /// per-instruction breakpoint check is the price of it.
    bool armed() const { return armed_; }

    /// Are WATCHPOINTS live — i.e. would a memory or port access made RIGHT
    /// NOW be allowed to raise the data-breakpoint latch? The hot-path gate
    /// for the eight Mmu sites and PortDispatch::check_io_watchpoint_.
    ///
    /// armed() AND the access being the EMULATED MACHINE's, rather than the
    /// debugger's own. A watchpoint exists to catch the guest
    /// program touching an address; the panel that DISPLAYS that address is an
    /// observer and must be invisible to it. Before this gate existed, the
    /// Watches / Memory / Stack / Disassembly panels all read guest memory
    /// through the same Mmu::read() the CPU uses, so a READ watchpoint on any
    /// address a panel happened to show was latched by the panel's own
    /// refresh — ~4 times a second while running — and the next Run or Step
    /// stopped one instruction later, at an address the watchpoint had
    /// nothing to do with.
    ///
    /// It is a single cached bool, recomputed only when something a human did
    /// changed it, so the eight Mmu sites execute exactly the load-and-branch
    /// they executed when they read armed() directly.
    bool watchpoints_live() const { return wp_live_; }

    /// Is the emulated machine currently executing? See GuestExecutionScope.
    /// Public for the one assertion that enforces the invariant, and for
    /// tests.
    bool guest_access() const { return guest_access_; }

    /// RAII — "the code in this block IS the emulated machine".
    ///
    /// Taken by the three functions that drive the machine: Emulator::
    /// run_frame(), execute_single_instruction() and step_frame_slot(). Every
    /// guest memory access and every guest port access happens inside one of
    /// them, so watchpoints are live exactly there and NOWHERE ELSE.
    ///
    /// That is what makes this safe by DEFAULT rather than by discipline: a
    /// panel, a tool, a snapshot saver — anything that reads guest memory
    /// from outside the emulator's own execution — cannot fire a watchpoint,
    /// whatever entry point it calls and whether or not its author knew this
    /// problem existed. There is no call site to remember, because the gate
    /// does not depend on the caller at all.
    ///
    /// It nests (run_frame -> step_frame_slot is a real nesting under
    /// rewind replay) and restores the previous value, so it is safe on any
    /// path out, including the data-breakpoint early return.
    class GuestExecutionScope {
    public:
        explicit GuestExecutionScope(DebugState& ds)
            : ds_(ds), prev_(ds.guest_access_) { ds.set_guest_access_(true); }
        ~GuestExecutionScope() { ds_.set_guest_access_(prev_); }
        GuestExecutionScope(const GuestExecutionScope&) = delete;
        GuestExecutionScope& operator=(const GuestExecutionScope&) = delete;
    private:
        DebugState& ds_;
        bool prev_;
    };

    /// RAII — "the code in this block is the DEBUGGER looking, not the guest".
    ///
    /// The counterpart, for the handful of debugger reads that necessarily
    /// happen INSIDE GuestExecutionScope because they are interleaved with
    /// execution: the trace log's 4-byte opcode capture and the call-stack
    /// tracker's 3-byte opcode peek, both of which read bytes past a short
    /// instruction that the CPU never fetches. Both are switched on by the
    /// debugger itself, so without this they fired spurious READ watchpoints
    /// on data bytes for exactly the user who had the debugger open.
    class InspectionScope {
    public:
        explicit InspectionScope(DebugState& ds)
            : ds_(ds), prev_(ds.guest_access_) { ds.set_guest_access_(false); }
        ~InspectionScope() { ds_.set_guest_access_(prev_); }
        InspectionScope(const InspectionScope&) = delete;
        InspectionScope& operator=(const InspectionScope&) = delete;
    private:
        DebugState& ds_;
        bool prev_;
    };

    bool paused() const { return paused_; }
    void pause();
    void resume();

    /// GH #27 S6 — suspend the debugger for a NON-debugging action that has
    /// to run the machine: the frame-boundary advance a snapshot needs when
    /// the user hit Save while paused mid-frame (design §10.2 P7, §15.2).
    ///
    /// It is deliberately NOT `resume()` then `pause()`. `resume()` clears
    /// one-shot breakpoints and `pause()` clears the step mode, so that pair
    /// would quietly destroy a pending "Run to Here" and whatever step the
    /// user was in — a save must leave the debugging session exactly as it
    /// found it. This scope saves and restores the four execution-control
    /// fields and the GH #221 step-off arm, and touches nothing else:
    /// one-shot breakpoints, the breakpoint set and the data-breakpoint
    /// latch all survive untouched.
    class SuspendScope {
    public:
        explicit SuspendScope(DebugState& ds)
            : ds_(ds), paused_(ds.paused_), magic_hold_(ds.magic_hold_),
              clients_(ds.clients_attached_), live_raster_(ds.live_raster_),
              persistent_(ds.persistent_), replay_(ds.replay_armed_),
              step_(ds.step_mode_), step_off_(ds.step_off_pending_) {
            ds_.paused_     = false;
            // GH #278 WP4c — the magic hold, which replaced active_ here.
            ds_.magic_hold_ = false;
            // GH #276 B3 — EVERY armed_ contributor, not just the two that
            // existed when this scope was written. The comment above promises
            // "disarms breakpoints"; leaving clients_attached_ standing would
            // make that promise false for any machine a backend client is
            // attached to, which is every machine once a frontend holds a
            // `Debugger`. A new contributor to refresh_gates_() belongs here in
            // the same commit.
            ds_.clients_attached_ = false;
            // And the raster-walk contributor: the scope has always switched
            // the walk off, and a client's live_raster must not switch it back
            // on under it.
            ds_.live_raster_ = false;
            ds_.persistent_ = false;
            // GH #278 WP3 — and the replay contributor (ReplayArmScope below).
            ds_.replay_armed_ = false;
            ds_.step_mode_  = StepMode::NONE;
            ds_.refresh_gates_();          // disarms breakpoints
        }
        ~SuspendScope() {
            ds_.paused_     = paused_;
            ds_.magic_hold_ = magic_hold_;
            ds_.clients_attached_ = clients_;
            ds_.live_raster_ = live_raster_;
            ds_.persistent_ = persistent_;
            ds_.replay_armed_ = replay_;
            ds_.step_mode_  = step_;
            ds_.refresh_gates_();
            ds_.step_off_pending_ = step_off_;   // after refresh_gates_
        }
        SuspendScope(const SuspendScope&) = delete;
        SuspendScope& operator=(const SuspendScope&) = delete;

    private:
        DebugState& ds_;
        bool        paused_;
        bool        magic_hold_;
        bool        clients_;
        bool        live_raster_;
        bool        persistent_;
        bool        replay_;
        StepMode    step_;
        bool        step_off_;
    };

    /// GH #278 WP3 (B3 obligation 3) — arm the machine for the length of a
    /// rewind's REPLAY, and no longer.
    ///
    /// `Emulator::rewind_to_cycle()` fast-forwards from a snapshot to its target
    /// with `run_to_cycle()`, and the stop at the target is tested inside
    /// `run_frame()`'s armed() block: an unarmed replay would never stop. The
    /// rewind paths used to arm it with `set_active(true)` — the Qt window's
    /// bit — and never cleared it, so one remote client's step-back left the
    /// machine armed, and the raster walk and render hint on, for the rest of
    /// the session, whoever detached. This scope is its own contributor to
    /// armed() and nothing else (not attached(), not raster_live()), set for
    /// the replay loop and restored after it: what the machine is armed by
    /// once the rewind returns is exactly what it was armed by before.
    class ReplayArmScope {
    public:
        explicit ReplayArmScope(DebugState& ds) : ds_(ds), prev_(ds.replay_armed_) {
            ds_.replay_armed_ = true;
            ds_.refresh_gates_();
        }
        ~ReplayArmScope() {
            ds_.replay_armed_ = prev_;
            ds_.refresh_gates_();
        }
        ReplayArmScope(const ReplayArmScope&) = delete;
        ReplayArmScope& operator=(const ReplayArmScope&) = delete;

    private:
        DebugState& ds_;
        bool        prev_;
    };

    // ── CTL-13 — evidence that survives the stop (GH #276 B2) ───────────
    //
    // pause() clears the step mode, and the hot loop consumes data_bp_hit_ in
    // the SAME BREATH as the pause (`pause(); set_data_bp_hit(false);`), so by
    // the time `Debugger::state()` can look, WHY the machine stopped is already
    // gone. These two latches are that evidence, and they are the whole reason
    // `PauseReason::Watch` and `PauseReason::Magic` can be reported at all.
    //
    // Both are cleared by unpause_(), i.e. on every paused -> running edge,
    // which is the one place every resume-family transition goes through.
    void note_watch_stop(uint16_t addr, bool is_write) {
        watch_stop_ = true;
        watch_stop_addr_ = addr;
        watch_stop_is_write_ = is_write;
    }
    bool     watch_stop() const { return watch_stop_; }
    uint16_t watch_stop_addr() const { return watch_stop_addr_; }
    bool     watch_stop_is_write() const { return watch_stop_is_write_; }

    void note_magic_stop(uint16_t pc) { magic_stop_ = true; magic_stop_pc_ = pc; }
    bool     magic_stop() const { return magic_stop_; }
    uint16_t magic_stop_pc() const { return magic_stop_pc_; }

    /// Drop both latches. `unpause_()` does this on every resume; a
    /// `load_state` while PAUSED never unpauses and must do it explicitly, or
    /// CTL-13 explains the restored machine's stop with the replaced machine's
    /// evidence (`Emulator::debug_after_state_restore_()`).
    void clear_stop_evidence() { watch_stop_ = false; magic_stop_ = false; }

    /// GH #276 B2 — how many times the machine has left `paused`. Bumped by
    /// `unpause_()`, i.e. by EVERY transition out of paused, including a raw
    /// `DebugState::resume()` from the Qt frontend that never reaches a backend
    /// verb.
    ///
    /// `Debugger::Impl` stamps it on the event-stop latch and `state()` ignores a
    /// latch whose generation has moved. `Impl::arm()` alone was not enough — it
    /// is called by the backend's control verbs and the Qt panels still drive this
    /// class directly until package Q, so a Qt-driven Run left the latch standing
    /// and it explained the NEXT, unrelated stop. Same argument that put
    /// `watch_stop_` / `magic_stop_` in `unpause_()`, reached without giving this
    /// class a pointer back into the backend.
    uint64_t resume_generation() const { return resume_gen_; }

    /// Drop the "the ring holds something" flag after the ring itself was
    /// emptied from outside a drain (the same restore path).
    void clear_ring_flag() {
        ring_nonempty_ = false;
        recompute_boundary_work_();
    }

    // Step modes.
    void step_into();
    void step_over(uint16_t next_pc);

    /// GH #276 B2 — the TRANSIENT-SUBSCRIPTION forms of step_over / run_to.
    ///
    /// step_over()/run_to() above additionally set `BreakpointSet`'s single
    /// one-shot. These do not: the backend arms a transient `Execute`
    /// subscription instead, which §4.3 makes unlimited in number, exempt from
    /// the master switch and auto-removed at the next stop — the single one-shot
    /// is none of those, and DeZog needs two temporary breakpoints per
    /// `CMD_CONTINUE`. The one-shot forms stay because the Qt frontend still
    /// drives this class directly until package Q retires that path.
    void step_over_subscribed();
    void run_to_subscribed();
    void step_out(uint16_t current_sp);
    void run_to(uint16_t addr);
    void run_to_cycle(uint64_t target_cycle);
    void step_back(int n = 1);
    void run_back_to_cycle(uint64_t target_cycle);
    uint64_t target_cycle() const { return target_cycle_; }
    int step_back_count() const { return step_back_count_; }

    /// Called before each CPU instruction in the hot loop.
    /// Returns true if execution should break (pause).
    bool should_break(uint16_t pc) const;

    /// GH #221 — the one-instruction step-off, consumed by the hot loop's
    /// breakpoint gate immediately before should_break().
    ///
    /// Returns true (and disarms) on the FIRST breakpoint test of a resumed
    /// run, telling the gate to skip that one test. Every transition out of
    /// paused arms it — resume(), step_into/over/out(), run_to(),
    /// run_to_cycle(), step_back(), run_back_to_cycle() — because all of them
    /// leave PC exactly where the user is looking at it, and the gate runs
    /// BEFORE the instruction at that PC. Without the skip, F5 at a breakpoint
    /// cleared paused_, the gate re-matched the unchanged PC and the machine
    /// re-paused having made no progress: #221.
    ///
    /// It suppresses the RESUMED-FROM address and no other, and it does so
    /// without storing one — but that rests on a PRECONDITION, stated here
    /// because it is not self-evident and was wrong in the first cut of this
    /// fix: **the arm is only ever raised on a real paused -> running edge**
    /// (see unpause_()). Given that, no instruction can execute between the
    /// arm and its consumption — the machine was stopped — so the address the
    /// gate is looking at when it consumes the arm IS the address the resume
    /// was issued at. Drop the precondition and the property goes with it: a
    /// resume issued while the machine is already running arms against a PC
    /// that keeps moving, and the arm lands on some later, unrelated
    /// breakpoint test and swallows it.
    ///
    /// It is also exactly one instruction wide, so a breakpoint at the NEXT
    /// address — or at this one, on the next pass round a loop — still fires.
    bool consume_step_off();

    /// Step Out predicate — called after EVERY instruction while
    /// StepMode::OUT is armed (GH #203).
    ///
    /// Returns true when the instruction that just completed was the return
    /// that left the subroutine Step Out was armed in, so the caller must
    /// re-pause. The caller supplies:
    ///   @param sp_before  SP immediately before the instruction executed
    ///   @param sp_after   SP immediately after it executed
    ///   @param opcode     first opcode byte, read at the PRE-execution PC
    ///   @param opcode2    second opcode byte (only read for the 0xED prefix)
    ///
    /// Three conditions, all required:
    ///   1. the stack actually shrank by one return address (sp_after ==
    ///      sp_before + 2) — this is what distinguishes a TAKEN `RET cc` from
    ///      an untaken one, and what makes the predicate inert on the steps
    ///      that execute no CPU instruction at all (DMA burst, boot hold,
    ///      parked CPU);
    ///   2. the opcode was a return form (see the .cpp) — this is what stops
    ///      a bare `POP rr` from ending the step;
    ///   3. the pop unwound the stack PAST the arming point (sp_after >
    ///      step_out_sp_, strictly). A nested call's own RET lands exactly ON
    ///      it and must NOT end the step; likewise an interrupt taken while
    ///      stepping out returns to the same routine, not out of it.
    bool check_step_out(uint16_t sp_before, uint16_t sp_after,
                        uint8_t opcode, uint8_t opcode2) const;

    /// The memory-free half of the test above: conditions 1 and 3 only.
    ///
    /// check_step_out() calls it, so there is one definition of the SP rules.
    /// It is public because the caller needs it FIRST, as a gate on whether
    /// fetching the opcode bytes is safe at all: an accepted NMI/INT executes
    /// no instruction and fetches nothing, so reading the opcode at PC in that
    /// slot is a read the CPU never makes — enough to fire a READ watchpoint
    /// and end the step in the wrong place. Every such slot moves SP the wrong
    /// way, so a false here means the read must not be taken.
    bool step_out_sp_qualifies(uint16_t sp_before, uint16_t sp_after) const;

    BreakpointSet& breakpoints() { return breakpoints_; }
    const BreakpointSet& breakpoints() const { return breakpoints_; }

    // ── GH #276 B2 — CAP-EVT: the event table, the gates, the latch ──────
    //
    // DebugState is the ONE object `Mmu`, `PortDispatch`, `NextReg`, `Copper`,
    // `Dma` and `Emulator` already hold a pointer to, which is why the event
    // machinery is reached through it rather than through `Debugger` — nothing
    // below a frontend may see an `Emulator*` OR a `Debugger*` (§4 rule 1).
    //
    // Every member here is NULL / false / inert until a `Debugger` is
    // constructed, so a build with no debugger attached executes exactly what
    // it executed before this existed.

    /// Install (or, with nullptr, retire) the subscription table. The
    /// `Debugger` does this in its constructor and clears it in its destructor:
    /// a dangling table pointer reachable from the MMU would be a use-after-free
    /// on the hot path.
    void set_event_table(jnext::dbg::EventTable* t) { events_ = t; }
    jnext::dbg::EventTable* event_table() const { return events_; }

    /// Install the two hooks the hot loop calls. `drain` is the boundary drain
    /// (returns true iff a `Stop` action fired); `gate` is the pre-instruction
    /// `Execute` gate (same convention). Both live in `Debugger`, because a
    /// `Handler` takes a `Debugger&`.
    void set_event_hooks(std::function<bool()> drain,
                         std::function<bool(uint16_t)> gate) {
        event_drain_ = std::move(drain);
        execute_gate_ = std::move(gate);
    }

    /// GH #276 — "the machine this backend was describing has been REPLACED".
    ///
    /// THE REASON THIS EXISTS RATHER THAN A CALL IN EACH VERB. CTL-13's stop
    /// evidence lives in THREE places: `DebugState`'s own `watch_stop_` /
    /// `magic_stop_`, `EventTable::hits_`, and `Debugger::Impl`'s
    /// `event_stop_latched` / `event_stop` / `armed_reason`. `Emulator` can reach
    /// the first two and CANNOT reach the third — nothing below a frontend may
    /// see a `Debugger*` (§4 rule 1) — so the reconciliation stopped one object
    /// graph short, and `Debugger::load_state_bytes()` (which, unlike
    /// `step_back()` and `rewind_to_frame()`, calls no control verb and therefore
    /// never reaches `Impl::arm()`) left `state()` reporting a `Watch` on a write
    /// that had not happened on the restored machine.
    ///
    /// Fixing the one verb would have been the FOURTH instance of this branch's
    /// recurring hazard — sibling operations where all but one do the thing. So
    /// the notification is issued by the ONE place a machine is replaced
    /// (`Emulator::debug_after_machine_transition_()`), and every present and
    /// future verb that lands a new machine inherits it: `load_state_bytes`,
    /// `step_back`, `rewind_to_frame`, `run_back_to_cycle`, a warm start, B4's
    /// bookmark restore and B3's cold-boot reconstruct all route through
    /// `Emulator::load_state()` or `init()`.
    void set_machine_replaced_hook(std::function<void()> fn) {
        machine_replaced_ = std::move(fn);
    }
    void notify_machine_replaced() {
        if (machine_replaced_) machine_replaced_();
    }

    /// THE MEMORY-WATCH GATE the eight `Mmu` sites read, in place of
    /// has_any_watchpoints(). One byte load, a shift and a test (§6.1); it
    /// covers legacy watchpoints AND `Mem` subscriptions, pre-ORed by
    /// `BreakpointSet` (see there for why the bytes live in that class).
    bool rd_watch_armed(uint16_t addr) const {
        return breakpoints_.rd_watch_slot_armed(addr);
    }
    bool wr_watch_armed(uint16_t addr) const {
        return breakpoints_.wr_watch_slot_armed(addr);
    }
    /// The port twin. Ports have no slots, so it is one bool.
    bool port_watch_armed() const { return breakpoints_.port_watch_armed(); }

    /// The two mask bytes themselves, so a row can assert "nothing at all is
    /// armed" rather than probing eight addresses.
    uint8_t rd_watch_mask() const { return breakpoints_.watch_slot_mask_rd(); }
    uint8_t wr_watch_mask() const { return breakpoints_.watch_slot_mask_wr(); }

    /// Is there anything for the boundary drain to do? A single cached bool:
    /// the ring holds something, or a `Cycle` subscription is armed (the one
    /// kind whose condition is a boundary comparison rather than a latch).
    bool events_pending() const { return event_boundary_work_; }

    /// Is a live `Execute` subscription armed? The gate on the pre-instruction
    /// hook, so the ordinary breakpoint path pays one bool test for it.
    bool execute_events_armed() const { return execute_armed_; }

    /// The same, for `NextReg::write`'s hook. Without it every guest NextREG
    /// write ran a linear scan of all subscriptions.
    bool nextreg_events_armed() const { return nextreg_armed_; }

    /// Run the pre-instruction `Execute` gate for `pc`. True iff it stopped.
    bool run_execute_gate(uint16_t pc) {
        return execute_gate_ ? execute_gate_(pc) : false;
    }

    /// Run the boundary drain. True iff a `Stop` action fired.
    bool drain_events() {
        ring_nonempty_ = false;
        recompute_boundary_work_();
        return event_drain_ ? event_drain_() : false;
    }

    /// Latch a site entry. Out of line: it stamps the common
    /// {cycle, frame, pc, vc, hc} through `stamp_common_` and appends to the
    /// ring, and neither belongs in a header the MMU includes.
    void latch_event(jnext::dbg::LatchEntry& e);

    /// The same, for a site whose event happened at a cycle the LIVE CLOCK no
    /// longer holds.
    ///
    /// `on_scanline` is the case: it is called from the post-instruction device
    /// cluster, so `clock_.get()` is already past the line boundary by the whole
    /// instruction that crossed it (measured +32 / +64 master cycles at 3.5 MHz).
    /// `events.h` promises "latched at the line with its exact cycle", and B2
    /// stamped the live clock and threw the boundary away one line from where the
    /// caller handed it in. The stamper derives `vc`/`hc` from whichever cycle it
    /// is given, so the whole header stays self-consistent.
    void latch_event_at(jnext::dbg::LatchEntry& e, uint64_t cycle);

    /// Install the site-context stamper — the one thing only the `Emulator`
    /// knows ({cycle, frame, pc, vc, hc}). Called ONLY from inside
    /// `latch_event`, i.e. only when a filter has already matched.
    ///
    /// `at` is the cycle to stamp from, or null for "the live clock". A POINTER
    /// rather than a sentinel value: cycle 0 is a real cycle (before the first
    /// tick), and a sentinel that is also a legal value is how an off-by-one
    /// hides.
    void set_latch_stamper(std::function<void(jnext::dbg::LatchEntry&,
                                             const uint64_t* at)> fn) {
        stamp_common_ = std::move(fn);
    }

    /// §6.1 — the MMU remapped a slot: re-evaluate any physical-page filter and
    /// re-publish the masks if they moved.
    void on_slot_remapped(int slot, uint16_t page);

    /// Re-read the `EventTable`'s cached state into the hot-path gates: the
    /// slot masks, the port flag, the `Execute` arm and the `Cycle` arm. Called
    /// by the `Debugger` on every subscription change — never from the hot path.
    void refresh_event_gates();

    // ── GH #276 B4 — INS-20, PC coverage ─────────────────────────────────
    //
    // The executed-PC bit set the hot path writes into, or null while coverage
    // is off. The set itself is the BACKEND's (`Debugger::Impl::coverage`, the
    // `CoverageBits` a client reads by reference); this is only where the hot
    // path finds it — the same arrangement as `events_` above, and for the same
    // reason: nothing below a frontend may see a `Debugger*`.
    //
    // A POINTER RATHER THAN A FLAG PLUS A POINTER: the one per-instruction test
    // is "is there a sink", so switched off it costs one load and one branch
    // and nothing else (§4.2 INS-20, "zero cost when off").
    //
    // PUBLISHED LIKE THE THREE EVENT HOOKS, and retired and re-published with
    // them: `~Debugger()` nulls it (the set dies with `Impl`), and a cold boot's
    // brand-new `DebugState` starts null, so the backend re-publishes it in the
    // one re-application.
    std::bitset<65536>* coverage_sink() const { return coverage_; }
    void set_coverage_sink(std::bitset<65536>* s) { coverage_ = s; }

    // ── GH #276 B4 — CAP-01, a screenshot is waiting for a rendered frame ─
    //
    // Set by the backend while a deferred capture is queued; ORed into
    // `Emulator::end_of_frame()`'s render decision, so the next frame IS
    // rendered even where a frontend's hint would have skipped it (Qt at speed
    // above 1x). The capture is "deferred to the next rendered frame"; this is
    // what guarantees there is one. Read once per frame, never per instruction.
    bool capture_render() const { return capture_render_; }
    void set_capture_render(bool on) { capture_render_ = on; }

    StepMode step_mode() const { return step_mode_; }

    /// Set by MMU when a data breakpoint (read/write) is hit.
    /// Checked after each instruction in the hot loop.
    bool data_bp_hit() const { return data_bp_hit_; }
    void set_data_bp_hit(bool h) { data_bp_hit_ = h; }
    uint16_t data_bp_addr() const { return data_bp_addr_; }
    void set_data_bp_addr(uint16_t a) { data_bp_addr_ = a; }

private:
    /// The ONE writer of guest_access_ — private, so the flag can only be
    /// moved by the two RAII scopes above and can never be left stuck by an
    /// early return, an exception or a forgotten reset.
    void set_guest_access_(bool g) { guest_access_ = g; refresh_gates_(); }

    /// Recompute EVERY cached hot-path gate — armed_, wp_live_ and (GH #276 B3)
    /// attached_ and raster_live_ — from the six inputs that feed them
    /// (clients_attached_, live_raster_, persistent_, guest_access_, GH #278
    /// WP3's rewind replay_armed_ and GH #278 WP4c's magic_hold_, which
    /// replaced the retired active_).
    /// Named for the gates rather than for armed_ alone, which is what it used
    /// to maintain: a name that mentions only part of what a function maintains
    /// is how the next person misses the rest.
    ///
    /// Called only from the setters above and SuspendScope — i.e. only when a
    /// human opened the debugger, a backend client attached, detached or
    /// changed its live-raster request, --persistent-breakpoints was passed, or
    /// the machine entered or left execution (twice per frame, or twice per
    /// debugger Step). Never from the hot path.
    void refresh_gates_() {
        armed_ = clients_attached_ || persistent_ || replay_armed_ || magic_hold_;
        attached_    = clients_attached_;
        raster_live_ = live_raster_;
        wp_live_ = armed_ && guest_access_;
        // Disarming breakpoints drops any pending step-off with them. The gate
        // that consumes it does not run while !armed(), so PC moves on freely
        // and a surviving arm would suppress an unrelated test the moment
        // breakpoints came back (GH #221).
        if (!armed_) step_off_pending_ = false;
    }

    /// The ONE way out of paused. Every resume-family transition goes through
    /// it so the GH #221 step-off arm cannot be forgotten by a path added
    /// later — the defect was that F5 resumed onto its own breakpoint, and
    /// Step Over / Step Out / Run to Here resumed onto it in exactly the same
    /// way.
    ///
    /// It arms ONLY on a real paused -> running edge, and that guard is the
    /// whole of consume_step_off()'s correctness, not a tidiness check. A
    /// resume issued while the machine is ALREADY RUNNING has no "address the
    /// user is standing on" to protect: PC is wherever the free run has got
    /// to, and by the time the gate consumes the arm it has moved on again. An
    /// unconditional arm there would swallow the next legitimate hit, and the
    /// UI reaches this state by ordinary use — the debugger window's
    /// "F5: Continue" button is not enable-gated, and the main window's global
    /// F5 handler fires whenever the debugger is enabled, whatever the machine
    /// is doing. Guarding here rather than at those call sites covers the ones
    /// that exist and the ones that do not yet.
    void unpause_() {
        if (paused_) step_off_pending_ = true;
        paused_ = false;
        // GH #276 B2 — the stop evidence describes the stop the machine is
        // LEAVING. Cleared here rather than in resume(), because resume() is
        // only one of seven transitions out of paused and this is the one place
        // all seven pass through — the same argument the GH #221 arm rests on.
        clear_stop_evidence();
        ++resume_gen_;
        // GH #278 WP4c — a magic stop's hold ends with its pause. Released
        // AFTER the step-off arm above, so a machine the release leaves
        // unarmed drops that arm too (refresh_gates_()): the gate that would
        // consume it does not run on an unarmed machine.
        if (magic_hold_) {
            magic_hold_ = false;
            refresh_gates_();
        }
    }

    // GH #278 WP4c — the magic stop's hold (hold_for_magic_stop()), in the slot
    // the retired active_ occupied. Read only by refresh_gates_().
    bool magic_hold_ = false;
    // GH #276 B3 — the third armed_ contributor. APPENDED next to its siblings
    // rather than at the end of the class: all three are read only by
    // refresh_gates_(), never by the hot path (which reads armed_).
    bool clients_attached_ = false;
    // GH #278 WP3 — the fourth armed_ contributor, held only for a rewind's
    // replay (ReplayArmScope). Read only by refresh_gates_().
    bool replay_armed_ = false;
    bool persistent_ = false;
    bool armed_ = false;
    // Kept adjacent to armed_ deliberately: the eight Mmu watchpoint sites
    // read wp_live_ on every memory access, and it is the one member of this
    // class the hot path touches.
    //
    // guest_access_ defaults FALSE, which is the whole design: watchpoints are
    // OFF until the emulator itself declares that it is executing. Everything
    // else — every panel, every tool, every saver — is an observer by
    // construction rather than by remembering to say so.
    bool guest_access_ = false;
    bool wp_live_ = false;
    // GH #276 B3 — the two gates that replace active() in the hot path, and the
    // one input only they read. Precomputed by refresh_gates_(). After wp_live_
    // rather than between it and armed_, which the comment above keeps adjacent.
    bool attached_    = false;
    bool raster_live_ = false;
    bool live_raster_ = false;
    bool paused_ = false;
    bool step_off_pending_ = false;
    bool data_bp_hit_ = false;
    uint16_t data_bp_addr_ = 0;
    // GH #276 B2 — see note_watch_stop() / note_magic_stop().
    uint64_t resume_gen_ = 0;
    bool     watch_stop_ = false;
    bool     watch_stop_is_write_ = false;
    bool     magic_stop_ = false;
    uint16_t watch_stop_addr_ = 0;
    uint16_t magic_stop_pc_   = 0;

    // ── GH #276 B2 — appended, never interleaved ────────────────────────
    void recompute_boundary_work_() {
        event_boundary_work_ = ring_nonempty_ || cycle_armed_;
    }

    /// The one body behind `latch_event` and `latch_event_at`.
    void latch_event_at_(jnext::dbg::LatchEntry& e, const uint64_t* at);

    jnext::dbg::EventTable* events_ = nullptr;
    std::function<bool()> event_drain_;
    std::function<bool(uint16_t)> execute_gate_;
    std::function<void(jnext::dbg::LatchEntry&, const uint64_t*)> stamp_common_;
    std::function<void()> machine_replaced_;
    bool event_boundary_work_ = false;
    bool ring_nonempty_       = false;
    bool cycle_armed_         = false;
    bool execute_armed_       = false;
    bool nextreg_armed_       = false;
    // GH #276 B4 — INS-20; see coverage_sink().
    std::bitset<65536>* coverage_ = nullptr;
    // GH #276 B4 — CAP-01; see capture_render().
    bool capture_render_ = false;
    StepMode step_mode_ = StepMode::NONE;
    uint16_t step_out_sp_ = 0;
    uint64_t target_cycle_ = 0;
    int      step_back_count_ = 1;
    BreakpointSet breakpoints_;
};
