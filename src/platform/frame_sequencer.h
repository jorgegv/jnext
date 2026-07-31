#pragma once

#include <cstdint>

#include "audio_pacing.h"
#include "frame_deadline.h"
#include "present_cadence.h"
#include "render_policy.h"
#include "tick_stats.h"

/// Frontend frame-tick sequencer (Task 91).
///
/// WHY THIS EXISTS — the pacing LOGIC was tested; the WIRING was not.
///
/// Issue #9 produced four pure, well-tested policy headers (tick_stats.h,
/// frame_deadline.h, audio_pacing.h, render_policy.h, present_cadence.h). What
/// no test could reach was the code that CONNECTS them: QtApp::on_frame_tick()
/// needs a live Emulator + SDL audio device + MainWindow to run at all, so the
/// order of the steps and the lifetime of the state threaded through them were
/// covered by nothing.
///
/// That hole was not theoretical. At v0.98.47 a deliberate wiring mutation —
/// constructing a FRESH audio_pacing::BandState on every tick instead of
/// persisting it across ticks — survived the entire automated gate (5000+ unit
/// rows, the FUSE suite, the full screenshot regression, all green) and was
/// caught only by watching a live 30-second cadence log. The policy headers
/// were all still correct; the wiring around them was not, and nothing could
/// see it.
///
/// THE FIX. Everything that is ORDER or PERSISTENT STATE moves here, into a
/// frontend-agnostic sequencer that a unit test can drive over thousands of
/// simulated ticks with a fake clock and a fake audio device. The frontend
/// keeps only the parts that are irreducibly about ITS toolkit (Qt timers, SDL
/// audio, the emulator widget) and hands them over as an `Effects` object.
///
/// WHAT IS OWNED HERE (the state whose LIFETIME is the bug surface):
///   * the frame-deadline schedule           — must advance, never be re-anchored
///   * the audio pacer's smoothed band state — must persist across ticks
///   * the tick-stats accumulators           — must accumulate across a window
///   * the present-cadence counters          — ditto
///   * last-tick / last-render timestamps    — the interval and throttle bases
/// Every one of those is a "fresh instance per tick" waiting to happen, which
/// is exactly the class of defect that got through before.
///
/// EFFECTS CONCEPT. `Effects` is a template parameter, not a virtual interface
/// or std::function set, so the release build inlines straight through it and
/// pays nothing for the seam. An Effects type provides:
///
///   int64_t now_us()              host monotonic clock, microseconds
///   int64_t period_us()           effective frame period (refresh / speed)
///   double  speed_multiplier()    1.0 = normal
///   bool    audio_present()       an audio device exists
///   int     queued_ms()           device queue depth, in ms.
///                                 PRECONDITION: audio_present() is true.
///                                 The real Qt adapter dereferences its
///                                 SdlAudio pointer unconditionally, so
///                                 calling this without the guard is a NULL
///                                 DEREFERENCE, not a "< 0 means no device"
///                                 answer. Every call site below is guarded;
///                                 keep it that way. (A negative RETURN is a
///                                 separate case: a device that exists but
///                                 cannot report its depth.)
///   bool    paused()              debugger has paused the machine
///   bool    fastload_active()     tape fastload / phantom typist running
///   bool    screenshot_due()      a --delayed-screenshot fires this tick
///   bool    pre_frames()          per-tick frontend work that runs BEFORE the
///                                 frame loops; false = abandon this tick
///                                 (cold boot reconstructed the emulator)
///   void    run_frame(bool composite)   set the render hint, run one frame
///   void    reset_render_hint()   restore "always composite" after the loops
///   void    push_audio(bool hold_on_underrun)
///   void    present(bool carries_new_content)
///   void    post_frames(int frames_rendered)  screenshot / exit / debugger
///   void    set_timer_interval(int ms)        point the repeating timer here
///
/// SCOPE. This sequences the QtApp frame tick. The SDL frontend
/// (src/platform/sdl_app.cpp) deliberately does NOT share it — see the note at
/// the bottom of this file.
namespace frame_sequencer {

/// Per-tick burst ceiling while a fast tape load is in flight. The GUI thread
/// must still service repaints between bursts, so the sprint is bounded.
inline constexpr int FASTLOAD_BURST_LIMIT = 200;

/// Speed > 1x compositor throttle: present at most this often (ms).
inline constexpr int64_t RENDER_INTERVAL_MS = 20;  // ~50 Hz presentation

/// What one tick did. Returned for the frontend's own bookkeeping and for
/// tests to assert against; the sequencer has already applied every effect.
struct TickResult {
    /// run_frame() calls made this tick (pacing loop + fastload burst).
    int  frames_rendered = 0;
    /// Frames the audio pacer asked for (before the fastload burst). -1 when
    /// the pacer did not run (paused, or not audio-paced).
    int  paced_frames = -1;
    /// Did this tick push the framebuffer to the display?
    bool presented = false;
    /// Did the frames of this tick run with the compositor hint enabled?
    bool render_this_tick = false;
    /// Timer interval scheduled at the end of the tick; -1 when the tick was
    /// abandoned by pre_frames() (cold boot rebases the schedule itself).
    int  next_interval_ms = -1;
    /// pre_frames() abandoned the tick.
    bool abandoned = false;
    /// The tick ended by re-anchoring the deadline at now instead of advancing
    /// it: the WhenSlowPrefer::Video catch-up (issue #35).
    bool next_tick_asap = false;
};

class Sequencer {
public:
    /// Degradation policy (issue #35). Set from the CLI/saved preference at
    /// startup and live from Preferences; there is nothing to re-anchor or
    /// reset, so it takes effect on the next tick.
    void set_when_slow_prefer(audio_pacing::WhenSlowPrefer prefer)
    {
        prefer_ = prefer;
    }
    audio_pacing::WhenSlowPrefer when_slow_prefer() const { return prefer_; }

    /// (Re)anchor the frame schedule — timer start, cold boot, speed change.
    /// Returns the whole-ms interval to the first deadline.
    int rebase(int64_t now_us, int64_t period_us)
    {
        return deadline_.rebase(now_us, period_us);
    }

    /// Run one frame tick, in the one order that is correct.
    ///
    /// The step numbering below is load-bearing: several pairs are ordered for
    /// a reason and a transposition is a real defect that the unit suite pins
    /// (test/platform/frame_sequencer_test.cpp).
    template <typename Effects>
    TickResult tick(Effects& fx)
    {
        TickResult res;

        // (1) Tick-entry timestamp. Also the base for the handler-duration
        // sample taken at the very end, so it must be the FIRST thing read.
        const int64_t t_entry = fx.now_us();

        // (2) Count the tick, and the interval since the previous one. The
        // interval is judged late against the period in effect at THIS tick
        // (tick_stats::add_interval), which is why the period is re-read per
        // tick rather than cached: a runtime 50/60 Hz switch or a speed change
        // moves the threshold mid-window.
        tick_stats::add_tick(stats_);
        if (last_tick_us_ >= 0) {
            tick_stats::add_interval(stats_, t_entry - last_tick_us_,
                                     fx.period_us());
        }
        last_tick_us_ = t_entry;

        // (3) Audio queue depth AS THE PACER IS ABOUT TO SEE IT. This sample
        // must be taken BEFORE the frame loops: after them the loops' own
        // audio push has already refilled the device, so a post-loop reading
        // would describe the queue the pacer never saw and the diagnostic
        // would systematically overstate how full the device runs.
        //
        // Note this is a SEPARATE read from the pacer's own below. Both are
        // deliberate: the diagnostic samples the depth at tick entry, the
        // pacer samples it at the moment it decides. They are microseconds
        // apart in the real frontend but they are two distinct readings, and
        // collapsing them into one would change what the pacer acts on.
        if (fx.audio_present()) {
            const int q = fx.queued_ms();
            if (q >= 0) tick_stats::add_sample(stats_.queue_ms, q);
        }

        // (4) Frontend per-tick work that precedes emulation (SDL event drain,
        // pending cold-boot requests, inject/load countdowns, screenshot mask
        // arming). A false return means the emulator was reconstructed under
        // us: abandon the tick WITHOUT an emu-time sample and WITHOUT
        // rescheduling (cold boot re-anchors the deadline itself). The tick
        // still counts, and still carries its interval and handler samples.
        if (!fx.pre_frames()) {
            res.abandoned = true;
            tick_stats::add_sample(stats_.handler_us, fx.now_us() - t_entry);
            return res;
        }

        const bool screenshot_due = fx.screenshot_due();

        // (5) Compositor throttle above 1x. At --speed 400 the timer emulates
        // up to 200 frames/s while the display presents ~50-60, so most
        // composited frames are discarded unseen; throttle the render hint to
        // ~50 Hz wall-clock. A due screenshot forces the tick to render: the
        // captured frame must have gone through the compositor with the layer
        // mask armed by pre_frames().
        bool render_this_tick = true;
        if (fx.speed_multiplier() > 1.0 && !screenshot_due) {
            // Read the clock again rather than reusing the tick-entry stamp:
            // pre_frames() has run since, and the throttle compares against a
            // 20 ms window, so reusing t_entry would shift the decision's
            // phase by the length of that work.
            const int64_t now_ms = fx.now_us() / 1000;
            if (now_ms - last_render_ms_ < RENDER_INTERVAL_MS) {
                render_this_tick = false;
            } else {
                last_render_ms_ = now_ms;
            }
        }
        res.render_this_tick = render_this_tick;

        int     frames_rendered = 0;
        int64_t emu_us          = 0;
        bool    next_tick_asap  = false;

        if (!fx.paused()) {
            // (6) Pace emulation on the sound card, not the wall clock — only
            // at 1x with no fastload in flight. --speed and fastload
            // deliberately decouple emulated time from real time, so the
            // emulator's sample rate no longer matches the device's and no
            // pacing band can hold.
            const bool audio_paced = fx.audio_present() &&
                                     fx.speed_multiplier() == 1.0 &&
                                     !fx.fastload_active();

            int frames = 1;
            if (audio_paced) {
                // band_ is a MEMBER: the smoothed estimate is a filter over
                // the tick history and is meaningless if it restarts each
                // tick. Re-seeding it per tick makes the EMA equal the raw
                // reading, the chunk sawtooth then clips the band edges, and
                // the frontend stutters roughly once a second (the v0.98.47
                // regression; frame_sequencer_test FS-BAND-*).
                //
                // The degradation policy (issue #35) is applied to the band's
                // answer, never inside it: a catch-up under
                // WhenSlowPrefer::Video runs one frame here and pulls the next
                // tick in at step (15) instead of superseding a frame.
                const audio_pacing::TickPlan plan = audio_pacing::plan_for(
                    audio_pacing::frames_for_tick(band_, fx.queued_ms()), prefer_);
                frames         = plan.frames;
                next_tick_asap = plan.next_tick_asap;
                res.paced_frames = frames;
            }

            const int64_t emu_t0 = fx.now_us();
            for (int i = 0; i < frames; i++) {
                // Only the tick's LAST frame can reach the screen — they all
                // composite into one framebuffer and present() runs once,
                // after the loops — so the earlier ones skip the compositor.
                fx.run_frame(render_this_tick &&
                             render_policy::composite_frame_in_tick(
                                 i, frames, screenshot_due));
                ++frames_rendered;
            }
            emu_us += fx.now_us() - emu_t0;

            // (7) Attribute the pacer's own frame losses. They are EXPECTED,
            // and the cadence report must not lump them in with presents the
            // window system actually swallowed.
            //
            // The three counters are mutually exclusive by construction and
            // together they say which policy is running: a session with
            // `pull-ins` and no `doubles` is preferring video, and one with
            // doubles and no pull-ins is preferring audio. Without the third
            // counter a video-preferring session under load reports zero
            // interventions of any kind, which reads as "the pacer is idle"
            // when it is in fact intervening on every tick.
            if (next_tick_asap)   ++audio_pull_ins_;
            else if (frames >= 2) ++audio_doubles_;
            else if (frames == 0) ++audio_skips_;

            // (8) Fastload sprint: run extra frames inside this one tick so a
            // tape loads at FUSE-like speed instead of being capped at the
            // timer rate. Which burst frame is last is unknowable up front
            // (any frame can end the tape), so every loop frame runs
            // unrendered — it is frame `burst` of a tick with at least
            // burst+2 frames, the closing frame below following by
            // construction — and the closing frame composites.
            //
            // BOTH pause re-checks are load-bearing, and for a reason that is
            // easy to lose: a breakpoint can fire INSIDE the burst. An
            // ordinary breakpoint has debug_state_.active() set beforehand, so
            // run_frame() composites those frames regardless of the hint; but
            // --magic-breakpoint sets active only AT the hit, so pre-hit burst
            // frames skip compositing and the paused framebuffer can lag until
            // the first step/resume. Either way the burst MUST stop on the
            // tick the pause happens — continuing to sprint through a paused
            // machine would run hundreds of frames past the breakpoint the
            // user asked to stop at. The while-guard stops the loop; the
            // closing-frame guard stops the one extra frame after it.
            // (frame_sequencer_test FS-BURST-04/05 pin each guard separately.)
            int           burst    = 0;
            const int64_t burst_t0 = fx.now_us();
            while (fx.fastload_active() && burst < FASTLOAD_BURST_LIMIT - 1 &&
                   !fx.paused()) {
                fx.run_frame(render_this_tick &&
                             render_policy::composite_frame_in_tick(
                                 burst, burst + 2, screenshot_due));
                ++frames_rendered;
                ++burst;
            }
            if (burst > 0 && !fx.paused()) {
                fx.run_frame(render_this_tick &&
                             render_policy::composite_frame_in_tick(
                                 burst, burst + 1, screenshot_due));
                ++frames_rendered;
                ++burst;
            }
            emu_us += fx.now_us() - burst_t0;

            // (9) Feed the device — but not during fastload, where samples are
            // produced at many times real time. `audio_paced` doubles as the
            // underrun-hold gate: unpaced, the hold would fire every tick
            // instead of acting as a rescue.
            if (fx.audio_present() && !fx.fastload_active()) {
                fx.push_audio(audio_paced);
            }
        } else if (fx.audio_present()) {
            // Paused in the debugger: no samples are produced, so the device
            // would drain and SDL would zero-fill — a step to 0 and back, two
            // clicks per pause. Keep feeding it: the mixer is empty, so the
            // push only runs its underrun guard, which holds the last level.
            fx.push_audio(true);
        }

        // (10) One emulation-time sample per tick that got this far. A tick
        // that emulated nothing (paused, or an audio skip) samples ~0, which
        // is a genuine reading and not a missing one.
        tick_stats::add_sample(stats_.emu_us, emu_us);

        // (11) The render hint is per-frame within this tick; restore the
        // default so any run_frame() outside the tick (debugger actions) is
        // unaffected. The next tick re-decides.
        fx.reset_render_hint();

        // (12) Push the framebuffer. Skipped on throttled ticks: nothing was
        // re-composited, so re-uploading the identical stale frame is waste.
        if (render_this_tick) {
            fx.present(present_cadence::carries_new_content(frames_rendered));
            res.presented = true;
        }

        // (13) Accumulate the cadence window. Only the LAST frame of the tick
        // could possibly have been presented; the rest are accounted as
        // superseded (multi-frame tick) or unrendered (throttled tick) so that
        // `lost` means only "a fresh frame was never painted".
        cadence_.emulated += static_cast<uint64_t>(frames_rendered);
        cadence_.superseded +=
            present_cadence::superseded_in_tick(frames_rendered, render_this_tick);
        cadence_.unrendered +=
            present_cadence::unrendered_in_tick(frames_rendered, render_this_tick);

        // (14) Frontend work that follows the frames: the delayed screenshot
        // (which needs to know whether anything actually rendered), the
        // delayed exit, the debugger panel refresh.
        fx.post_frames(frames_rendered);

        // (15) Advance the deadline by exactly one period and point the timer
        // at it. At the END of the tick, because setInterval() on a live timer
        // restarts its period from the moment of the call — so this tick's own
        // work is subtracted from the wait. Advancing (rather than
        // re-anchoring) is what makes the long-run rate exact: rounding error
        // cannot accumulate when every interval is recomputed against the wall
        // clock. Re-anchoring here instead would reinstate the +1.1-1.3% fast
        // timer of issue #9 (frame_sequencer_test FS-RATE-*).
        //
        // A video-preference catch-up (issue #35) re-anchors at now instead:
        // the tick declined to run the pacer's second frame, so it asks for
        // the next tick immediately rather than leaving the deadline in the
        // past for later ticks to inherit (frame_deadline.h, resync_now).
        const int64_t t_end = fx.now_us();
        const int     ivl   = next_tick_asap
                                ? deadline_.resync_now(t_end)
                                : deadline_.next_interval_ms(t_end, fx.period_us());
        fx.set_timer_interval(ivl);
        res.next_interval_ms = ivl;
        res.next_tick_asap   = next_tick_asap;

        // (16) Handler duration — measured across the WHOLE tick, so a tick
        // abandoned at step (4) still contributes one.
        tick_stats::add_sample(stats_.handler_us, fx.now_us() - t_entry);

        res.frames_rendered = frames_rendered;
        return res;
    }

    /// Drain the tick-stats window (one status tick). `paint_us` is owned by
    /// the display widget, so the caller folds it in before summarising.
    tick_stats::Counters& stats() { return stats_; }
    const tick_stats::Counters& stats() const { return stats_; }
    void reset_stats() { stats_ = {}; }

    /// Drain the present-cadence window. `presented` is owned by the display
    /// widget (only it knows what actually reached the screen), so the caller
    /// fills that field before summarising.
    present_cadence::Counters& cadence() { return cadence_; }
    const present_cadence::Counters& cadence() const { return cadence_; }
    void reset_cadence() { cadence_ = {}; }

    /// Audio-pacer interventions since the last drain: ticks that ran 2+
    /// frames (catch-up) and ticks that ran none (device ahead). In steady
    /// state on a healthy host BOTH are zero — that is the property the
    /// v0.98.47 regression broke and the live cadence log exposed.
    uint64_t audio_doubles() const { return audio_doubles_; }
    uint64_t audio_skips() const { return audio_skips_; }
    /// Catch-ups answered by pulling the next tick in rather than by running a
    /// second frame (WhenSlowPrefer::Video). Always zero under the default
    /// audio preference.
    uint64_t audio_pull_ins() const { return audio_pull_ins_; }
    void reset_audio_counters()
    {
        audio_doubles_ = 0;
        audio_skips_ = 0;
        audio_pull_ins_ = 0;
    }

    /// Stall resyncs performed by the deadline schedule (diagnostics/tests).
    uint64_t deadline_resyncs() const { return deadline_.resyncs(); }

    /// Smoothed audio-queue estimate, for tests and diagnostics. Negative
    /// means unseeded (no device seen yet).
    double band_estimate_ms() const { return band_.smoothed_ms; }

private:
    // --- state whose LIFETIME is the whole point of this class ---
    frame_deadline::Scheduler deadline_;
    audio_pacing::BandState   band_;
    tick_stats::Counters      stats_;
    present_cadence::Counters cadence_;

    /// Tick-entry time of the previous tick; -1 = no previous tick, so the
    /// first tick of a session contributes no interval sample (there is no
    /// interval to measure).
    int64_t  last_tick_us_  = -1;
    /// Wall-clock ms of the last presented frame, for the >1x throttle.
    int64_t  last_render_ms_ = 0;
    uint64_t audio_doubles_  = 0;
    uint64_t audio_skips_    = 0;
    uint64_t audio_pull_ins_ = 0;

    /// Issue #35 — what a catch-up costs. Audio (drop a frame) is the default
    /// and the behaviour that existed before the option.
    audio_pacing::WhenSlowPrefer prefer_ = audio_pacing::WhenSlowPrefer::Audio;
};

/// WHY THE SDL FRONTEND DOES NOT SHARE THIS SEQUENCER.
///
/// src/platform/sdl_app.cpp has a superficially similar tick, and folding it in
/// here was evaluated and rejected. Its loop is a DIFFERENT machine:
///
///   * it paces with a blocking SDL_Delay to a whole-ms period, not a QTimer
///     against a fractional absolute deadline — it has no deadline schedule at
///     all, and giving it one would change its rate;
///   * it has no speed multiplier, so no >1x compositor throttle;
///   * it emits no tick-stats and no cadence report (both are GUI diagnostics
///     driven by the Qt status timer, which SDL does not have);
///   * fastload is handled by SKIPPING THE SLEEP rather than by an in-tick
///     burst, so it runs exactly one frame per iteration;
///   * it runs its frame loop BEFORE checking the cold-boot requests, where Qt
///     checks them first.
///
/// WHO ACTUALLY RUNS THE SDL LOOP (GH #132). An earlier version of this
/// comment said the SDL frontend's "only consumer is the headless regression
/// suite". That is wrong twice over, and wrong in the direction that invites a
/// careless edit — it reads as "nothing real depends on this".
///
///   * `--headless` does NOT build an SdlApp. It builds a HeadlessApp
///     (src/main.cpp:913), a separate class with its own loop; SdlApp is
///     instantiated only when ENABLE_QT_UI=OFF (src/main.cpp:944-951), which
///     the suite's binary ($JNEXT resolves to build/gui-release) is not. The
///     regression rows that do run a windowed frontend — grep the functional
///     scripts for QT_QPA_PLATFORM / xvfb-run rather than trusting a list here
///     — therefore exercise QtApp, not this.
///   * NOTHING CURRENTLY SHIPPED runs it either. Every published artifact is
///     built ENABLE_QT_UI=ON: the three Windows zips (release.yml:227-232 —
///     "Final published lineup: x64-Qt6, x64-Qt5, i686-Qt5"), the rpm
///     (packaging/rpm/jnext.spec:40), the deb (packaging/debian/rules:7), the
///     Flatpak (io.github.zxjogv.jnext.yml:42) and the macOS bundle
///     (CMakeLists.txt:372 gates the whole bundle path on ENABLE_QT_UI).
///     Do not read `-legacy` as "the SDL one": those zips come from
///     package-win-qt5 / package-win32-qt5, which configure
///     -DENABLE_QT_UI=ON -DJNEXT_FORCE_QT5=ON (Makefile:237,299). The name
///     means Qt5, i.e. the Win7/8 AUDIENCE, not the toolchain (Makefile:849).
///   * The only ENABLE_QT_UI=OFF package legs are package-win-sdl /
///     package-win32-sdl (Makefile:216,274), named `-sdl`. They are
///     repo-internal validation only — built by `make package-test` in ci.yml,
///     never built or published by release.yml (owner decision 2026-07-26).
///
/// So this loop is currently UNSHIPPED, not unimportant, and the distinction
/// matters in one direction only: a future published SDL leg would silently
/// inherit whatever is wrong with it, and until GH #122 it had no regression
/// coverage of ANY kind to notice with. It now has exactly one row,
/// sdl-keypress-func, which builds build/sdl-release and drives it under Xvfb.
/// Treat everything else about its timing as unproven and do not change it
/// casually.
///
/// Sharing would therefore mean either changing SDL's timing — unproven, so a
/// change here cannot be shown to be safe, and Task 91 is a
/// behaviour-preserving refactor — or parameterising the sequencer until every
/// step is optional, which reintroduces exactly the untested branching the
/// class exists to remove. The two policy pieces the frontends genuinely share
/// (audio_pacing::frames_for_tick, render_policy::composite_frame_in_tick) are
/// already shared, already pure, and already unit-tested; SDL consumes them
/// directly. Revisit only if the SDL frontend ever grows the same diagnostics.

}  // namespace frame_sequencer
