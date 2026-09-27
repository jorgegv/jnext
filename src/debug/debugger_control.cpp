// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.1 CAP-CTL, control.
//
// Work package B1 of epic #276. Every verb here is the corresponding body of
// `src/debugger/debugger_manager.cpp` with the Qt removed: the same
// `DebugState` calls in the same order, the same GH #207 / #221 / #223
// behaviour, the same two target computations for end-of-frame and
// end-of-scanline. What the Qt version did AROUND them — the four panel
// `set_paused()` calls, the `emit paused()`, `update_actions()` — is a
// frontend's reaction to a transition, and becomes SES-02's `Paused` push in B3.
//
// TWO THINGS EVERY EXECUTE-FAMILY VERB DOES, and neither is optional:
//
//   * `impl_->execute_gate()`, the CTL-11 corruption gate. In Qt this is
//     `confirm_resume_if_corrupt()`, which ASKS; here it REFUSES with
//     `RefusedCorrupt`, and §4.1 puts the asking in the Qt adapter. A remote
//     client or a script gets the refusal, which is the behaviour §4.1 asks for.
//   * `impl_->arm(...)`, which records what the next stop will mean. See
//     `Debugger::state()` for why the backend has to remember it.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

#include "debug/disasm.h"
#include "debug/rewind_buffer.h"
#include "video/renderer.h"   // Renderer::FB_HEIGHT — the last visible row

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// The two gates
// ---------------------------------------------------------------------------

Result Debugger::Impl::execute_gate() const {
    // Task 60b/60e: a failed rewind or state load leaves a partially restored
    // machine. Executing against it is what the guard exists to stop.
    const bool corrupt = !emu.last_state_error().empty();
    if (guard.needs_confirmation(corrupt, emu.state_error_generation()))
        return Result::RefusedCorrupt;
    return Result::Ok;
}

Result Debugger::Impl::rewind_refusal() const {
    // Order matters and is §4.1 CTL-09's: RZX first, because it is a refusal
    // about the RECORDING rather than about the buffer, and it must not be
    // reported as "nothing to rewind to".
    if (emu.rzx_recorder().is_recording() || emu.rzx_player().is_playing())
        return Result::RefusedRzx;
    const RewindBuffer* rb = emu.rewind_buffer();
    if (!rb || rb->empty()) return Result::RefusedUnavailable;
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// CTL-01 / CTL-02 — pause, run
// ---------------------------------------------------------------------------

Result Debugger::pause(ClientId by) {
    // Idempotent (CTL-01): DebugState::pause() is, and re-pausing an already
    // paused machine re-attributes the stop to the caller, which is what "last
    // verb wins" means.
    //
    // GH #276 B2 — AN EXPLICIT PAUSE IS A STOP, so the transient subscriptions
    // go (§4.3). That is what makes "pause, then Run, and the machine runs past
    // the Run-to-Here target" behave as it does today, where `resume()`'s
    // `clear_oneshot()` did it one transition later.
    impl_->ds().pause();
    impl_->apply_stop();
    impl_->arm(PauseReason::Kind::User, by);
    return Result::Ok;
}

Result Debugger::run(ClientId by) {
    // GH #223 — Run on an already-running machine is a no-op that still returns
    // Ok, and it must return BEFORE the corruption gate: DebugState::resume()
    // clears the one-shot breakpoint, so reaching it here would silently throw
    // away a pending Run to Here or step-over target. Ordered exactly as
    // DebuggerManager::on_run() orders it, and for the same reason.
    //
    // GH #276 B2 — and it must return before the TRANSIENT DROP below for the
    // same reason, now that Step Over and Run to Here arm a transient
    // subscription instead of that one-shot. CTL-02-04/05 pin the property:
    // a redundant `run()` must not clear a pending target.
    if (!impl_->ds().paused()) return Result::Ok;

    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    // NOTE — `run()` does NOT drop the transient subscriptions, and that is a
    // DEPARTURE from `DebugState::resume()`'s `clear_oneshot()` rather than an
    // oversight. §4.3 gives DeZog TWO temporary breakpoints per `CMD_CONTINUE`,
    // which the adapter arms and then continues: a `run()` that cleared them
    // would clear what the same operation had just asked for, and the protocol
    // could not work at all. The rule §4.3 states is "auto-removed at the next
    // STOP", and that is where they go — `Impl::apply_stop()` for a stop the
    // backend caused, and `Debugger::pause()` for an explicit one, which is the
    // transition `resume()`'s clear was really standing in for.
    impl_->ds().resume();
    // A stop after a free run is explained by the machine, not by this verb.
    impl_->arm(PauseReason::Kind::None, by);
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// CTL-03 / CTL-04 / CTL-05 — the three steps
// ---------------------------------------------------------------------------

Result Debugger::step_into(ClientId by) {
    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    if (!impl_->ds().paused()) impl_->ds().pause();

    // GH #207 — debugger_step(), not the raw execute_single_instruction()
    // primitive: while the debugger holds the machine nothing else calls
    // run_frame(), so the Step has to turn frames over too, and a Step at a HALT
    // has to run the halt out (the CPU leaves it only on an accepted interrupt).
    impl_->emu.debugger_step();
    impl_->ds().pause();

    impl_->arm(PauseReason::Kind::Step, by);
    return Result::Ok;
}

Result Debugger::step_over(ClientId by) {
    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    if (!impl_->ds().paused()) impl_->ds().pause();

    const uint16_t pc = impl_->emu.cpu().get_registers().PC;
    if (!::is_call_like(pc, memory_reader())) {
        // CTL-04: anything that is not CALL-like behaves as CTL-03, which
        // includes arming `Step` — so this is the whole of the else branch.
        return step_into(by);
    }

    const int len = ::instruction_length(pc, memory_reader());
    const uint16_t next_pc = static_cast<uint16_t>(pc + len);
    // GH #276 B2 — a REAL transient `Execute` subscription, replacing
    // `DebugState::step_over`'s single one-shot. §4.3 makes transient
    // subscriptions unlimited in number, exempt from the master switch and
    // auto-removed at the next stop; the one-shot was one, exempt, and cleared
    // by `resume()`. Unlimited is what DeZog's two temporary breakpoints per
    // `CMD_CONTINUE` need, and what makes a Step Over inside a Run to Here
    // possible at all.
    //
    // `step_over_subscribed()` is `step_over()` minus the one-shot: the step
    // MODE is unchanged, so `state().step_mode` still reports `Over` and every
    // CTL-04 row that asserts it still holds.
    Subscription tgt;
    tgt.kind      = EventKind::Execute;
    tgt.filter.lo = next_pc;
    tgt.filter.hi = next_pc;
    tgt.transient = true;
    tgt.action    = Action::Stop;
    (void)subscribe(by, tgt);
    impl_->ds().step_over_subscribed();
    // ASYNCHRONOUS — the stop arrives later, and it is a step completing rather
    // than a run reaching a target, so the reason is Step (CTL-13's "a step verb
    // completed"), not RunTo.
    impl_->arm(PauseReason::Kind::Step, by);
    return Result::Ok;
}

Result Debugger::step_out(ClientId by) {
    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    if (!impl_->ds().paused()) impl_->ds().pause();

    impl_->ds().step_out(impl_->emu.cpu().get_registers().SP);
    impl_->arm(PauseReason::Kind::Step, by);
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// CTL-06 / CTL-07 / CTL-08 — the run-to family
// ---------------------------------------------------------------------------

Result Debugger::run_to(ClientId by, uint16_t addr) {
    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    if (!impl_->ds().paused()) impl_->ds().pause();
    // GH #276 B2 — the same transient `Execute`, for the same reasons as
    // step_over() above. CTL-06 is "Run to Here", and RSP's `i<len>` is
    // `run_to(pc + len)`.
    Subscription tgt;
    tgt.kind      = EventKind::Execute;
    tgt.filter.lo = addr;
    tgt.filter.hi = addr;
    tgt.transient = true;
    tgt.action    = Action::Stop;
    (void)subscribe(by, tgt);
    impl_->ds().run_to_subscribed();
    impl_->arm_target(PauseReason::Kind::RunTo, by, addr);
    return Result::Ok;
}

Result Debugger::run_to_cycle(ClientId by, uint64_t master_cycle) {
    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    if (!impl_->ds().paused()) impl_->ds().pause();
    impl_->ds().run_to_cycle(master_cycle);
    // No target ADDRESS — the target is a cycle, and `state()` reports RunTo for
    // an armed run-to with no address rather than trying to match a PC.
    impl_->arm(PauseReason::Kind::RunTo, by);
    return Result::Ok;
}

Result Debugger::run_to_frame(ClientId by, uint32_t frame) {
    // TIME-03, forward only. The frame's start cycle is derivable without the
    // rewind buffer: frames are a fixed number of master cycles apart, and one
    // frame's start is known (`current_frame_cycle()`).
    //
    // WHICH frame `current_frame_cycle()` names is the whole difficulty, and two
    // cuts of this verb got it wrong. At a FRAME BOUNDARY `run_frame()` has
    // already advanced `frame_cycle_` to the next frame's start
    // (`frame_cycle_ = frame_end`, emulator.cpp) while `frame_num_` still counts
    // the frame that finished, so the two disagree by exactly one frame — and a
    // paused debugger is at a boundary almost always.
    //
    // THE BASE IS DERIVED FROM THE RAW COUNTER, NOT FROM THE TAG, and that is
    // the whole of the second fix. `frame_tag()` clamps (`raw > 0 ? raw - 1 : 0`),
    // so `tag == 0` means BOTH "frame 0 has finished" and "nothing has run yet";
    // a base of `tag + 1` at a boundary is right for the first and wrong for the
    // second, and `run_to_frame(3)` on a never-run machine landed in frame 2
    // (rows TIME-03-04/05). The raw counter has no such ambiguity: it is K+1
    // during frame K and 0 before anything runs, so
    //
    //     mid-frame K:   raw = K+1, frame_cycle_ = start of K   -> base = raw-1
    //     at a boundary: raw = K+1, frame_cycle_ = start of K+1 -> base = raw
    //     never run:     raw = 0,   frame_cycle_ = start of 0   -> base = raw
    //
    // is exact in all three. `raw - 1` cannot underflow: a frame in progress
    // means at least one `begin_new_frame()` has run.
    const uint32_t now = frame_tag(impl_->emu);
    if (frame <= now) return Result::RefusedUnavailable;   // forward only

    // ONE residual, stated rather than hidden: on a never-run machine the
    // refusal above reads the clamped tag, so `run_to_frame(0)` is refused even
    // though frame 0 has not begun. That is the safe side of the clamp's
    // ambiguity — a refusal is an answer a caller can act on, where a stop that
    // ran nothing would look like the frame had begun.
    const uint32_t raw  = impl_->emu.frame_num();
    const uint32_t base = impl_->emu.frame_in_progress() ? raw - 1u : raw;
    const MachineTiming& t = impl_->emu.timing();
    const uint64_t target = impl_->emu.current_frame_cycle()
                          + static_cast<uint64_t>(frame - base) * t.master_cycles_per_frame;
    return run_to_cycle(by, target);
}

Result Debugger::run_to_end_of_frame(ClientId by) {
    // Moved verbatim from DebuggerManager::on_run_to_eof(), comment included:
    //
    // Target the midpoint of the last visible scanline, so that when the CPU
    // stops every framebuffer row has been rendered and the video panel shows a
    // complete picture. If we are already past that point in the current frame
    // (e.g. stepping from blanking), target the same scanline in the next frame.
    //
    // The last visible row is framebuffer row FB_HEIGHT-1, and since G164v2
    // (Task 13) `fb_row = raw_vc - vblank_top()` — so the RAW VC we must run to
    // is FB_HEIGHT-1 + vblank_top() (287 on the NEXT family, 303 on Pentagon,
    // 263 on the 60 Hz overrides), not the bare FB_HEIGHT-1 = 255 this used to
    // target. Stopping at raw VC 255 left the bottom 32 framebuffer rows
    // undrawn, so "Run to EOF" never actually reached the end of the frame.
    if (!impl_->ds().paused()) return Result::RefusedRunning;

    const uint64_t frame_start = impl_->emu.current_frame_cycle();
    const MachineTiming& t = impl_->emu.timing();
    const uint64_t last_vis_vc =
        static_cast<uint64_t>(Renderer::FB_HEIGHT - 1
                              + impl_->emu.video_timing().vblank_top());
    const uint64_t last_vis_mid = last_vis_vc * t.master_cycles_per_line
                                  + t.master_cycles_per_line / 2;
    uint64_t target = frame_start + last_vis_mid;
    if (impl_->emu.clock().get() >= target)
        target += t.master_cycles_per_frame;

    return run_to_cycle(by, target);
}

Result Debugger::run_to_end_of_scanline(ClientId by) {
    // Moved verbatim from DebuggerManager::on_run_to_eosl(), comment included:
    //
    // Calculate the cycle at the end of the current scanline, then round up to
    // the next scanline boundary. Past the last visible row -> jump straight to
    // the next frame start.
    //
    // `next_line_vc` is a RAW VC; the visible framebuffer occupies raw VC
    // [vblank_top(), vblank_top()+FB_HEIGHT). Comparing the raw VC directly
    // against FB_HEIGHT (as this used to) misclassified raw VC 256..287 — the
    // bottom border, framebuffer rows 224..255 — as blanking, so "Run to EOSL"
    // skipped to the next frame instead of the next scanline for the last 32
    // visible rows. G164v2 / Task 13.
    if (!impl_->ds().paused()) return Result::RefusedRunning;

    const uint64_t frame_start = impl_->emu.current_frame_cycle();
    const uint64_t elapsed     = impl_->emu.clock().get() - frame_start;
    const MachineTiming& t = impl_->emu.timing();
    const uint64_t next_line_start =
        ((elapsed / t.master_cycles_per_line) + 1) * t.master_cycles_per_line;
    const uint64_t next_line_vc = next_line_start / t.master_cycles_per_line;
    const int next_fb_row = static_cast<int>(next_line_vc)
                          - impl_->emu.video_timing().vblank_top();
    const uint64_t target = (next_fb_row >= Renderer::FB_HEIGHT)
                          ? frame_start + t.master_cycles_per_frame
                          : frame_start + next_line_start;

    return run_to_cycle(by, target);
}

// ---------------------------------------------------------------------------
// CTL-09 / CTL-10 — reverse execution
//
// SYNCHRONOUS, and the three refusals are distinguished (§4.1): `RefusedRzx`,
// `RefusedUnavailable` (nothing to rewind to, or a frame outside the ring) and
// `RefusedCorrupt` (the restore itself failed and left the machine torn — which
// is the case DebuggerManager surfaces with warn_state_corrupt()).
// ---------------------------------------------------------------------------

Result Debugger::step_back(ClientId by, uint32_t n) {
    const Result refusal = impl_->rewind_refusal();
    if (refusal != Result::Ok) return refusal;

    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    if (!impl_->emu.step_back(static_cast<int>(n == 0 ? 1 : n)))
        return Result::RefusedCorrupt;

    impl_->arm(PauseReason::Kind::Step, by);
    return Result::Ok;
}

Result Debugger::rewind_to_frame(ClientId by, uint32_t frame) {
    const Result refusal = impl_->rewind_refusal();
    if (refusal != Result::Ok) return refusal;

    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    // A frame outside the ring is BENIGN (§4 `RefusedUnavailable`), not a
    // corruption: Emulator::rewind_to_frame() range-checks it and returns false
    // without attempting a restore, which is the same false a torn restore
    // returns — so the range is checked HERE, where the two can still be told
    // apart.
    const RewindBuffer* rb = impl_->emu.rewind_buffer();
    if (frame < rb->oldest_frame_num() || frame > rb->newest_frame_num())
        return Result::RefusedUnavailable;

    if (!impl_->emu.rewind_to_frame(frame)) return Result::RefusedCorrupt;

    impl_->arm(PauseReason::Kind::Step, by);
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// CTL-11 — the corruption gate
// ---------------------------------------------------------------------------

std::optional<CorruptionIncident> Debugger::resume_blocked_by_corruption() const {
    const bool corrupt = !impl_->emu.last_state_error().empty();
    const uint64_t gen = impl_->emu.state_error_generation();
    if (!impl_->guard.needs_confirmation(corrupt, gen)) return std::nullopt;
    CorruptionIncident inc;
    inc.subsystem  = impl_->emu.last_state_error();
    inc.generation = gen;
    return inc;
}

Result Debugger::acknowledge_corruption(uint64_t generation) {
    // Acking a generation that is not the live one is not an error — it is a
    // stale acknowledgment, and the guard's own rule (a later generation
    // re-blocks) already makes it harmless. Refused all the same, so a frontend
    // that acks the wrong incident finds out.
    if (generation != impl_->emu.state_error_generation())
        return Result::RefusedUnavailable;
    impl_->guard.record_ack(generation);
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// CTL-12 / CTL-15 — reset and load
//
// `Soft` is `Emulator::soft_reset()` and lands here. `Hard` and `load()` need
// the loop owner's registered driver (SES-07) — the cold-boot reconstruct
// contract destroys and placement-news the `Emulator`, which only the layer
// ABOVE the backend can do — so without one they refuse with
// `RefusedUnavailable`, exactly as §4.1 specifies. B3 registers the driver.
// ---------------------------------------------------------------------------

Result Debugger::reset(ClientId by, ResetKind kind) {
    // A SWITCH WITH NO `default`, and that is the point rather than a style
    // choice. `ResetKind` has no trailing `Count` sentinel and nothing else in
    // the tree switches over it, so before this there was NOTHING that would
    // notice a fourth enumerator being appended — the three `static_assert`s in
    // `debug_types_check.cpp` pin the values 0/1/2 and say nothing about a
    // value 3. `src/debug/CMakeLists.txt` compiles this library with
    // `-Werror=switch` (the same mechanism issue #43 uses to couple
    // `cli::OptId` to its parser), so an appended kind is a COMPILE ERROR here.
    //
    // `ResetKind::Any` is a FILTER value (F8): it exists so one `on reset` rule
    // compiles to one subscription. "Reset the machine, either way" is not a
    // reset, so the verb refuses it rather than picking one.
    switch (kind) {
        case ResetKind::Any:  return Result::Unsupported;
        case ResetKind::Hard: return Result::RefusedUnavailable;   // no driver (B3)
        case ResetKind::Soft: break;
    }

    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    impl_->emu.soft_reset();
    impl_->log_mutate_range(by, "machine", "soft reset");
    // A soft reset does not pause a running machine and does not resume a paused
    // one; whatever the caller's state was, it survives. So the armed reason
    // stays as it was — this verb is not a stop.
    return Result::Ok;
}

Result Debugger::load(ClientId, const std::string&) {
    // CTL-15 routes through `LoopDriver::load` (SES-07), which B3 registers.
    return Result::RefusedUnavailable;
}

// ---------------------------------------------------------------------------
// CTL-14 — the magic breakpoint
// ---------------------------------------------------------------------------

bool Debugger::magic_breakpoint() const {
    // `Emulator::set_magic_breakpoint()` records the arm in `config_` alongside
    // installing the CPU hook (GH #239), so the config IS the state — there is no
    // second flag to read and none to add.
    return impl_->emu.config().magic_breakpoint;
}

Result Debugger::set_magic_breakpoint(bool enabled) {
    impl_->emu.set_magic_breakpoint(enabled);
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// §4.1 — the armed gate
//
// `armed()` is §5's formula `attached || persistent_breakpoints`, and
// `DebugState::armed()` IS that formula over today's two flags
// (`active_ || persistent_`). It is read rather than recomputed here precisely
// so there is one gate: the hot loop consults `DebugState::armed()` on every
// instruction, and a second copy of the formula in the backend could disagree
// with the one the machine actually obeys.
// ---------------------------------------------------------------------------

bool Debugger::armed() const { return impl_->ds().armed(); }

Result Debugger::set_persistent_breakpoints(bool enabled) {
    impl_->ds().set_persistent_breakpoints(enabled);
    return Result::Ok;
}

bool Debugger::persistent_breakpoints() const {
    return impl_->ds().persistent_breakpoints();
}

}  // namespace dbg
}  // namespace jnext
