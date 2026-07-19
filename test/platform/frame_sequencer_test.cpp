// Frame-tick sequencer test (Task 91).
//
// WHAT THIS FILE EXISTS TO CATCH — wiring, not policy.
//
// The pure pacing policies (audio_pacing, frame_deadline, render_policy,
// tick_stats, present_cadence) each already have a suite. What had NO test was
// the code that connects them: the order of the steps, and the LIFETIME of the
// state threaded through them. At v0.98.47 a deliberate mutation — a fresh
// audio_pacing::BandState per tick instead of one persisted across ticks —
// survived 5000+ unit rows, the FUSE suite and the whole screenshot
// regression, and was caught only by watching a live cadence log.
//
// So this suite drives frame_sequencer::Sequencer over hundreds of simulated
// ticks with a fake monotonic clock and a fake audio device, and asserts the
// EMERGENT properties that the live observation checks: interventions per
// second, doubles, skips, presented frames, and long-run tick rate.
//
// ORACLE. Like audio_pacing_test / render_policy_test this has no VHDL oracle:
// it tests a host presentation policy. Its oracle is the stated contract in
//   * src/platform/frame_sequencer.h  (step order, owned state)
//   * src/platform/audio_pacing.h     (band, smoothing, emergencies)
//   * src/platform/frame_deadline.h   (exact long-run rate)
//   * src/platform/render_policy.h    (only the last frame composites)
//   * src/platform/present_cadence.h  (frame accounting)
// Every row derives from those contracts, never from reading the sequencer
// implementation back.
//
// Run: ./build/test/frame_sequencer_test

#include "platform/frame_sequencer.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string s_int(long long v) { return std::to_string(v); }
std::string s_dbl(double v)
{
    char b[64];
    std::snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

// --- machine periods (src/core/emulator_config.h machine_timing) -----------
// Next 50 Hz: 1824*311 cycles @ 28 MHz = 20259 us (49.36 Hz)
// Next 60 Hz: 1824*264 cycles @ 28 MHz = 17198 us (58.15 Hz)
constexpr int64_t PERIOD_50_US = 20259;
constexpr int64_t PERIOD_60_US = 17198;

/// A scriptable stand-in for QtApp: fake monotonic clock, fake audio device,
/// and a recording of every effect the sequencer asked for.
struct FakeFrontend {
    // --- fake monotonic clock ---
    int64_t clock_us = 1'000'000;
    int64_t run_frame_cost_us  = 0;   ///< simulated cost of one emulated frame
    int64_t pre_frames_cost_us = 0;   ///< simulated cost of the pre-frame work
    int64_t post_frames_cost_us = 0;

    // --- scripted inputs ---
    int64_t period            = PERIOD_50_US;
    double  speed             = 1.0;
    bool    has_audio         = true;
    bool    is_paused         = false;
    int     fastload_frames   = 0;    ///< frames of fastload left; run_frame decrements
    bool    shot_due          = false;
    bool    pre_frames_result = true;

    // --- fake audio device -------------------------------------------------
    // The device consumes in ~23.2 ms chunks (1024 frames at 44100 Hz), which
    // is the sawtooth audio_pacing.h's smoothing exists to reject. Emulated
    // frames add exactly one frame period of audio each, pushed once per tick.
    static constexpr double CHUNK_MS = 23.2;
    bool   audio_model  = false;      ///< false: queued_ms is a fixed script value
    double queue_ms     = 65.0;
    /// When non-empty, queued_ms() cycles these depths instead of reading the
    /// modelled device — lets a test impose an exact, known sawtooth.
    std::vector<int> scripted_queue;
    size_t           scripted_pos = 0;
    double consume_accum_ms = 0.0;
    int    pending_frames = 0;        ///< frames synthesised but not yet pushed

    // --- recordings --------------------------------------------------------
    std::vector<bool> composites;         ///< one entry per run_frame(), its hint
    std::vector<int>  intervals;          ///< timer intervals handed back
    std::vector<int>  queue_reads;        ///< every queued_ms() the sequencer made
    std::vector<bool> present_new_content;
    std::vector<bool> push_hold;
    std::vector<int>  post_frames_arg;
    int presents = 0, pushes = 0, pre_calls = 0, post_calls = 0, reset_hints = 0;

    void advance(int64_t us)
    {
        clock_us += us;
        if (!audio_model) return;
        consume_accum_ms += static_cast<double>(us) / 1000.0;
        while (consume_accum_ms >= CHUNK_MS) {
            queue_ms -= CHUNK_MS;
            consume_accum_ms -= CHUNK_MS;
        }
        if (queue_ms < 0.0) queue_ms = 0.0;
    }

    // --- the Effects concept ----------------------------------------------
    int64_t now_us() const { return clock_us; }
    int64_t period_us() const { return period; }
    double  speed_multiplier() const { return speed; }
    bool    audio_present() const { return has_audio; }
    bool    paused() const { return is_paused; }
    bool    fastload_active() const { return fastload_frames > 0; }
    bool    screenshot_due() const { return shot_due; }

    int queued_ms()
    {
        int q;
        if (!has_audio) {
            q = -1;
        } else if (!scripted_queue.empty()) {
            q = scripted_queue[scripted_pos % scripted_queue.size()];
            ++scripted_pos;
        } else {
            q = static_cast<int>(std::llround(queue_ms));
        }
        queue_reads.push_back(q);
        return q;
    }

    bool pre_frames()
    {
        ++pre_calls;
        advance(pre_frames_cost_us);
        return pre_frames_result;
    }

    void run_frame(bool composite)
    {
        composites.push_back(composite);
        advance(run_frame_cost_us);
        if (fastload_frames > 0) --fastload_frames;
        ++pending_frames;
    }

    void reset_render_hint() { ++reset_hints; }

    void push_audio(bool hold_on_underrun)
    {
        ++pushes;
        push_hold.push_back(hold_on_underrun);
        if (audio_model) {
            queue_ms += pending_frames * (static_cast<double>(period) / 1000.0);
        }
        pending_frames = 0;
    }

    void present(bool carries_new_content)
    {
        ++presents;
        present_new_content.push_back(carries_new_content);
    }

    void post_frames(int frames_rendered)
    {
        ++post_calls;
        post_frames_arg.push_back(frames_rendered);
        advance(post_frames_cost_us);
    }

    void set_timer_interval(int ms) { intervals.push_back(ms); }
};

/// Anchor the schedule and wait for the first deadline, exactly as the real
/// frontend does: rebase() returns the interval to the FIRST tick, and the
/// timer only fires when it elapses. Starting to tick at the anchor instant
/// instead would give the first tick a double-length interval.
void start(frame_sequencer::Sequencer& seq, FakeFrontend& fx)
{
    fx.advance(static_cast<int64_t>(seq.rebase(fx.clock_us, fx.period)) * 1000);
}

/// Drive `n` timer ticks: run the tick, then let the clock reach the moment
/// the repeating timer would next fire (exactly the interval the sequencer
/// asked for). This is the QTimer contract — setInterval() restarts the period
/// from the moment of the call, which the sequencer makes at end of tick.
void run_ticks(frame_sequencer::Sequencer& seq, FakeFrontend& fx, int n)
{
    for (int i = 0; i < n; i++) {
        const frame_sequencer::TickResult r = seq.tick(fx);
        fx.advance(static_cast<int64_t>(r.next_interval_ms > 0 ? r.next_interval_ms : 1) *
                   1000);
    }
}

}  // namespace

int main()
{
    std::printf("frame_sequencer_test (frame-tick wiring, Task 91)\n");

    // =====================================================================
    // FS-TICK — every tick is counted, and the interval series is complete
    // =====================================================================
    // Contract (tick_stats.h): `ticks` counts timer callbacks; an interval
    // needs two ticks, so N ticks yield N-1 interval samples. The accumulators
    // span a REPORTING WINDOW (many ticks), not one tick — resetting them per
    // tick is the defect this row exists to catch.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        start(seq, fx);
        run_ticks(seq, fx, 100);
        check("FS-TICK-01", "100 ticks accumulate 100 tick counts in one window",
              seq.stats().ticks == 100, "ticks=" + s_int(seq.stats().ticks));
        check("FS-TICK-02", "100 ticks yield 99 interval samples (first has no predecessor)",
              seq.stats().interval_us.count == 99,
              "count=" + s_int(seq.stats().interval_us.count));
        check("FS-TICK-03", "every tick contributes one handler-duration sample",
              seq.stats().handler_us.count == 100,
              "count=" + s_int(seq.stats().handler_us.count));
        check("FS-TICK-04", "every non-abandoned tick contributes one emu-time sample",
              seq.stats().emu_us.count == 100,
              "count=" + s_int(seq.stats().emu_us.count));
    }

    // The last-tick timestamp must OUTLIVE a window drain: only the process's
    // very first tick may lack an interval sample. A drain that also cleared
    // the timestamp would silently drop one interval per second forever.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        start(seq, fx);
        run_ticks(seq, fx, 10);
        seq.reset_stats();          // status tick drains the window
        run_ticks(seq, fx, 10);
        check("FS-TICK-05", "after a window drain, all 10 ticks still carry intervals",
              seq.stats().interval_us.count == 10,
              "count=" + s_int(seq.stats().interval_us.count));
    }

    // =====================================================================
    // FS-RATE — the deadline is ADVANCED, never re-anchored
    // =====================================================================
    // Contract (frame_deadline.h): the long-run tick rate must equal the EXACT
    // fractional frame period. Re-anchoring at `now` each tick, or handing the
    // timer a fixed round(period), reinstates the +1.1-1.3% fast timer of
    // issue #9. Both 50 Hz and 60 Hz are checked: 20259 us and 17198 us round
    // to 20 and 17 ms, so a rounding bug shows as 49.36->50.00 and
    // 58.15->58.82 respectively.
    {
        for (int mode = 0; mode < 2; mode++) {
            const int64_t period = mode ? PERIOD_60_US : PERIOD_50_US;
            const char*   name   = mode ? "60Hz" : "50Hz";
            const char*   id_a   = mode ? "FS-RATE-02a" : "FS-RATE-01a";
            const char*   id_b   = mode ? "FS-RATE-02b" : "FS-RATE-01b";

            frame_sequencer::Sequencer seq;
            FakeFrontend fx;
            fx.has_audio = false;
            fx.period    = period;
            start(seq, fx);
            const int64_t t0 = fx.clock_us;
            const int     N  = 2000;
            run_ticks(seq, fx, N);
            const double elapsed_us   = static_cast<double>(fx.clock_us - t0);
            const double mean_tick_us = elapsed_us / N;
            const double achieved_hz  = 1'000'000.0 / mean_tick_us;
            const double exact_hz     = 1'000'000.0 / static_cast<double>(period);
            // Tolerance 0.05%: far tighter than the 1.1-1.3% error being
            // guarded against, loose enough for whole-ms interval quantisation
            // over 2000 ticks.
            check(id_a,
                  (std::string("long-run rate matches the exact period (") + name + ")").c_str(),
                  std::fabs(achieved_hz - exact_hz) / exact_hz < 0.0005,
                  "achieved=" + s_dbl(achieved_hz) + "Hz exact=" + s_dbl(exact_hz) + "Hz");
            // A fractional period CANNOT be served by one repeated whole-ms
            // interval: the series must alternate. If every interval is equal
            // the schedule is not fractional at all.
            bool has_two_values = false;
            for (size_t i = 1; i < fx.intervals.size(); i++)
                if (fx.intervals[i] != fx.intervals[0]) { has_two_values = true; break; }
            check(id_b,
                  (std::string("interval series alternates rather than repeating (") + name +
                   ")").c_str(),
                  has_two_values,
                  "first=" + s_int(fx.intervals.empty() ? -1 : fx.intervals[0]));
        }
    }

    // The deadline subtracts THIS tick's own work from the wait: a tick that
    // burns most of its period must be followed by a correspondingly shorter
    // interval, otherwise the rate slips by the handler duration every tick.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        fx.run_frame_cost_us = 12000;     // 12 ms of emulation in a 20.259 ms period
        start(seq, fx);
        const int64_t t0 = fx.clock_us;
        const int     N  = 500;
        run_ticks(seq, fx, N);
        const double mean_tick_us = static_cast<double>(fx.clock_us - t0) / N;
        check("FS-RATE-03",
              "a tick that burns 12ms still yields the full period tick-to-tick",
              std::fabs(mean_tick_us - static_cast<double>(PERIOD_50_US)) < 60.0,
              "mean=" + s_dbl(mean_tick_us) + "us period=" + s_int(PERIOD_50_US));
    }

    // Stall policy: a host stall longer than STALL_PERIODS resyncs instead of
    // machine-gunning 1 ms callbacks to walk off the backlog.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        start(seq, fx);
        run_ticks(seq, fx, 5);
        const uint64_t before = seq.deadline_resyncs();
        fx.advance(500'000);              // 500 ms stall (~25 periods)
        run_ticks(seq, fx, 1);
        check("FS-RATE-04", "a 500ms stall resyncs the schedule rather than bursting",
              seq.deadline_resyncs() == before + 1,
              "resyncs=" + s_int(static_cast<long long>(seq.deadline_resyncs())));
    }

    // =====================================================================
    // FS-BAND — the audio pacer's smoothed state PERSISTS across ticks
    // =====================================================================
    // THE v0.98.47 REGRESSION ROW.
    //
    // Contract (audio_pacing.h): the band thresholds act on an ESTIMATE OF THE
    // MEAN queue depth, not on the raw reading, precisely so that the device's
    // ~23 ms chunk sawtooth cannot clip a band edge. A queue whose MEAN sits
    // safely inside the band but whose teeth cross the upper edge must
    // therefore produce ZERO interventions. If the smoothing state is rebuilt
    // each tick the estimate degenerates to the raw reading, the teeth clip
    // the edge, and the frontend stutters — the measured symptom of the
    // regression (~1 intervention/s against a correct 0.000/s).
    //
    // The row asserts its OWN STIMULUS first: a test in which the sawtooth
    // never actually reaches the edge would pass vacuously whatever the code
    // does, so the raw readings are required to cross QUEUE_HIGH_MS.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = true;
        // Calibrated so the sawtooth MEAN settles inside the band while its
        // upper teeth cross QUEUE_HIGH_MS but stay under the raw emergency
        // threshold. Rows (a) and (b) below assert that this stimulus was
        // actually achieved, so the row cannot pass vacuously.
        fx.queue_ms    = 89.0;
        start(seq, fx);

        // Warm-up, then measure STEADY STATE — which is what the live cadence
        // observation reads, and what the contract is about. Start-up
        // transients (the band seeding itself from a first reading that may
        // legitimately sit outside the band) are not the property under test.
        run_ticks(seq, fx, 100);
        seq.reset_audio_counters();
        fx.queue_reads.clear();
        fx.composites.clear();
        run_ticks(seq, fx, 600);   // ~12 s of wall-clock at 49.36 Hz

        int raw_above_high = 0, raw_at_emergency = 0, raw_min = 9999, raw_max = -1;
        for (int q : fx.queue_reads) {
            if (q < 0) continue;
            if (q >= audio_pacing::QUEUE_HIGH_MS) ++raw_above_high;
            if (q >= audio_pacing::QUEUE_MAX_MS - audio_pacing::INTERVENTION_MS)
                ++raw_at_emergency;
            if (q < raw_min) raw_min = q;
            if (q > raw_max) raw_max = q;
        }
        // (a) the stimulus is real: teeth DO cross the upper band edge...
        check("FS-BAND-01a",
              "stimulus: raw queue readings cross QUEUE_HIGH_MS (sawtooth reaches the edge)",
              raw_above_high > 20,
              "crossings=" + s_int(raw_above_high) + " raw=[" + s_int(raw_min) + "," +
                  s_int(raw_max) + "]");
        // (b) ...but stay below the hard envelope, so the RAW emergency guard
        // (which is meant to fire, by design) is not what is being measured.
        check("FS-BAND-01b",
              "stimulus: raw readings stay below the raw emergency threshold",
              raw_at_emergency == 0, "emergencies=" + s_int(raw_at_emergency));
        // (c) the property: the smoothed estimate rejects the sawtooth, so the
        // steady state is SILENT. This is the assertion the mutation breaks.
        check("FS-BAND-01c",
              "smoothed band: zero skips in steady state despite teeth over the edge",
              seq.audio_skips() == 0,
              "skips=" + s_int(static_cast<long long>(seq.audio_skips())));
        check("FS-BAND-01d",
              "smoothed band: zero doubles in steady state",
              seq.audio_doubles() == 0,
              "doubles=" + s_int(static_cast<long long>(seq.audio_doubles())));
        // (e) one frame per tick throughout — the visible consequence of (c).
        check("FS-BAND-01e", "steady state runs exactly one frame per tick",
              fx.composites.size() == 600,
              "frames=" + s_int(static_cast<long long>(fx.composites.size())));
        // (f) the estimate really is a smoothed mean, not the raw reading:
        // its ripple must be a fraction of the sawtooth it is rejecting.
        check("FS-BAND-01f",
              "the retained estimate sits inside the band, away from the edge",
              seq.band_estimate_ms() > audio_pacing::QUEUE_LOW_MS &&
                  seq.band_estimate_ms() < audio_pacing::QUEUE_HIGH_MS,
              "estimate=" + s_dbl(seq.band_estimate_ms()));
    }

    // The estimate must SURVIVE across ticks in the ordinary sense too: after
    // many ticks at a steady depth it has converged to that depth. A per-tick
    // reconstruction also happens to converge (it equals the raw reading), so
    // this row is deliberately NOT the regression row above — it pins the
    // seeding contract instead: an unseeded band takes its first reading raw.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        start(seq, fx);
        run_ticks(seq, fx, 1);
        check("FS-BAND-02", "first reading for a device seeds the estimate directly",
              std::fabs(seq.band_estimate_ms() - 65.0) < 0.001,
              "estimate=" + s_dbl(seq.band_estimate_ms()));
    }

    // The estimate must be a SMOOTHED MEAN, not an echo of the latest reading.
    // This is the persistence property stated in isolation: rebuild the band
    // each tick and the EMA degenerates to the raw value, which is exactly the
    // v0.98.47 defect. Readings alternate between two depths that both sit
    // safely inside the band, so no intervention perturbs the measurement and
    // the ONLY thing under test is what the estimate retained.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        // Two readings per full cycle; the sequencer reads the depth twice per
        // tick (diagnostic + pacer), so a 4-entry script gives each tick one
        // low and one high reading and keeps the cycle aligned.
        fx.scripted_queue = {50, 50, 80, 80};
        start(seq, fx);
        run_ticks(seq, fx, 400);
        const int last_raw = fx.queue_reads.back();
        check("FS-BAND-05a",
              "the retained estimate is a smoothed mean, not the latest reading",
              seq.band_estimate_ms() >= 0.0 &&
                  std::fabs(seq.band_estimate_ms() - static_cast<double>(last_raw)) > 5.0,
              "estimate=" + s_dbl(seq.band_estimate_ms()) + " last_raw=" + s_int(last_raw));
        check("FS-BAND-05b",
              "the retained estimate lies strictly between the two alternating depths",
              seq.band_estimate_ms() > 50.0 && seq.band_estimate_ms() < 80.0,
              "estimate=" + s_dbl(seq.band_estimate_ms()));
        check("FS-BAND-05c",
              "alternating depths inside the band provoke no intervention",
              seq.audio_doubles() == 0 && seq.audio_skips() == 0,
              "doubles=" + s_int(static_cast<long long>(seq.audio_doubles())) +
                  " skips=" + s_int(static_cast<long long>(seq.audio_skips())));
    }

    // FS-BAND-06 — THE REGRESSION'S SYMPTOM, MEASURED DIRECTLY.
    //
    // FS-BAND-05 pins what the estimate RETAINS; this row pins what the user
    // FELT. Its scripted teeth deliberately CROSS the upper band edge, so a
    // degenerate (per-tick-rebuilt) estimate does not merely look wrong — it
    // produces real interventions, one frozen frame each. That is the live
    // measurement which caught v0.98.47: 0.000 interventions/s when correct,
    // ~1/s when broken.
    //
    // The readings are scripted, so the stimulus is bit-identical for correct
    // and broken code and only the OUTPUT can differ — a mutation cannot dodge
    // this row by shifting the queue trajectory the way it can with the
    // self-consistent device model of FS-BAND-01. Both framings are kept.
    //
    // Each depth appears TWICE because the sequencer reads the device twice
    // per tick (diagnostic sample + pacer decision, see FS-QUEUE-01), so a
    // doubled script gives every tick one consistent depth.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        // Pacer sees 76, 82, 88, 93, 90, 84, 78 -> mean 84.4, peak 93:
        // mean safely inside the band, teeth over QUEUE_HIGH_MS (90), all
        // below the raw emergency threshold (QUEUE_MAX - INTERVENTION = 99).
        fx.scripted_queue = {76, 76, 82, 82, 88, 88, 93, 93, 90, 90, 84, 84, 78, 78};
        start(seq, fx);
        run_ticks(seq, fx, 40);          // let the estimate converge
        seq.reset_audio_counters();
        fx.composites.clear();
        fx.queue_reads.clear();
        run_ticks(seq, fx, 350);         // ~7 s of measurement

        int crossings = 0, emergencies = 0;
        for (int q : fx.queue_reads) {
            if (q >= audio_pacing::QUEUE_HIGH_MS) ++crossings;
            if (q >= audio_pacing::QUEUE_MAX_MS - audio_pacing::INTERVENTION_MS)
                ++emergencies;
        }
        check("FS-BAND-06a",
              "stimulus: the scripted teeth cross QUEUE_HIGH_MS every cycle",
              crossings > 100, "crossings=" + s_int(crossings));
        check("FS-BAND-06b",
              "stimulus: the scripted teeth stay below the raw emergency threshold",
              emergencies == 0, "emergencies=" + s_int(emergencies));
        check("FS-BAND-06c",
              "band interventions in steady state are ZERO (the v0.98.47 symptom)",
              seq.audio_skips() == 0 && seq.audio_doubles() == 0,
              "skips=" + s_int(static_cast<long long>(seq.audio_skips())) +
                  " doubles=" + s_int(static_cast<long long>(seq.audio_doubles())));
        check("FS-BAND-06d",
              "every tick therefore runs exactly one frame (no frozen frames)",
              fx.composites.size() == 350,
              "frames=" + s_int(static_cast<long long>(fx.composites.size())));
    }

    // A sustained genuine drift (not sawtooth) MUST still be corrected — the
    // smoothing rejects noise, not signal. Queue held below the low edge:
    // the pacer must run catch-up doubles.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 20.0;         // genuinely starved, well under the band
        start(seq, fx);
        run_ticks(seq, fx, 40);
        check("FS-BAND-03", "a genuinely starved device still triggers catch-up doubles",
              seq.audio_doubles() > 0,
              "doubles=" + s_int(static_cast<long long>(seq.audio_doubles())));
        check("FS-BAND-03b", "a starved device never triggers a skip",
              seq.audio_skips() == 0,
              "skips=" + s_int(static_cast<long long>(seq.audio_skips())));
    }
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 110.0;        // genuinely ahead, over the band
        start(seq, fx);
        run_ticks(seq, fx, 40);
        check("FS-BAND-04", "a device running ahead still triggers skips",
              seq.audio_skips() > 0,
              "skips=" + s_int(static_cast<long long>(seq.audio_skips())));
        check("FS-BAND-04b", "a device running ahead never triggers a double",
              seq.audio_doubles() == 0,
              "doubles=" + s_int(static_cast<long long>(seq.audio_doubles())));
    }

    // =====================================================================
    // FS-PACE — the pacer's answer is what actually RUNS
    // =====================================================================
    // Contract (frame_sequencer.h step 6): the frame loop runs exactly the
    // number of frames frames_for_tick() returned. A wiring that ignored the
    // result and always ran one frame would leave every pure-policy test green
    // while the pacing did nothing at all.
    {
        // Starved device -> the pacer asks for 2, so 2 frames must run.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 20.0;
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        check("FS-PACE-01a", "starved device: the pacer asks for 2 frames",
              r.paced_frames == 2, "paced=" + s_int(r.paced_frames));
        check("FS-PACE-01b", "starved device: exactly 2 run_frame() calls are made",
              fx.composites.size() == 2,
              "calls=" + s_int(static_cast<long long>(fx.composites.size())));
        check("FS-PACE-01c", "the frames run equal the frames the pacer asked for",
              r.frames_rendered == r.paced_frames,
              "run=" + s_int(r.frames_rendered) + " asked=" + s_int(r.paced_frames));
    }
    {
        // Device far ahead -> the pacer asks for 0, so NO frame may run.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 110.0;
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        check("FS-PACE-02a", "device ahead: the pacer asks for 0 frames",
              r.paced_frames == 0, "paced=" + s_int(r.paced_frames));
        check("FS-PACE-02b", "device ahead: no run_frame() call is made",
              fx.composites.empty(),
              "calls=" + s_int(static_cast<long long>(fx.composites.size())));
    }
    {
        // Mid-band -> one frame.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        check("FS-PACE-03", "mid-band device: exactly one frame runs",
              r.paced_frames == 1 && fx.composites.size() == 1,
              "paced=" + s_int(r.paced_frames));
    }

    // The pacer must NOT run when pacing cannot hold: no audio device, speed
    // != 1x, or a fastload burst in flight (frame_sequencer.h step 6). In each
    // case the tick free-runs at one frame.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;          // --silent
        fx.audio_model = false;
        fx.queue_ms  = 20.0;           // would demand a double IF consulted
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        check("FS-PACE-04a", "no audio device: the pacer is not consulted",
              r.paced_frames == -1, "paced=" + s_int(r.paced_frames));
        check("FS-PACE-04b", "no audio device: the tick free-runs at one frame",
              fx.composites.size() == 1,
              "frames=" + s_int(static_cast<long long>(fx.composites.size())));
        check("FS-PACE-04c", "no audio device: no queue-depth sample is recorded",
              seq.stats().queue_ms.count == 0,
              "count=" + s_int(seq.stats().queue_ms.count));
        check("FS-PACE-04d", "no audio device: nothing is pushed to it",
              fx.pushes == 0, "pushes=" + s_int(fx.pushes));
    }
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 20.0;         // would demand a double IF consulted
        fx.speed       = 2.0;          // --speed 200 decouples emulated time
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        check("FS-PACE-05", "speed != 1x: the pacer is not consulted, one frame runs",
              r.paced_frames == -1 && fx.composites.size() == 1,
              "paced=" + s_int(r.paced_frames));
    }
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model     = false;
        fx.queue_ms        = 20.0;
        fx.fastload_frames = 5;        // tape in flight
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        check("FS-PACE-06", "fastload in flight: the pacer is not consulted",
              r.paced_frames == -1, "paced=" + s_int(r.paced_frames));
    }

    // =====================================================================
    // FS-QUEUE — the queue depth is sampled BEFORE the frames, not after
    // =====================================================================
    // Contract (frame_sequencer.h step 3): the diagnostic samples the depth as
    // THE PACER IS ABOUT TO SEE IT. After the frame loops the tick's own audio
    // push has already refilled the device, so a post-loop sample would
    // describe a queue the pacer never saw and would systematically overstate
    // how full the device runs — the `queue` column of the `ticks:` line would
    // then be unable to diagnose starvation, which is its entire purpose.
    //
    // The fake device makes the two readings unmistakably different: the push
    // adds a full frame period (20.259 ms) per frame.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = true;
        fx.queue_ms    = 65.0;
        start(seq, fx);
        const int pre_depth = static_cast<int>(std::llround(fx.queue_ms));
        seq.tick(fx);
        const int post_depth = static_cast<int>(std::llround(fx.queue_ms));
        check("FS-QUEUE-01a",
              "stimulus: the queue depth genuinely differs before vs after the frames",
              pre_depth != post_depth,
              "pre=" + s_int(pre_depth) + " post=" + s_int(post_depth));
        check("FS-QUEUE-01b", "the recorded sample is the PRE-frame depth",
              seq.stats().queue_ms.count == 1 && seq.stats().queue_ms.min == pre_depth,
              "recorded=" + s_int(seq.stats().queue_ms.min) + " pre=" + s_int(pre_depth) +
                  " post=" + s_int(post_depth));
    }

    // =====================================================================
    // FS-RENDER — only the tick's last frame composites
    // =====================================================================
    // Contract (render_policy.h): a tick presents ONCE, after its loops, so
    // every frame but the last is overwritten before any paint can serve it.
    // Wiring the hint the wrong way round is invisible to render_policy_test
    // (the policy function is still correct) but doubles the compositor work
    // on catch-up ticks and, inverted, blanks the presented frame.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 20.0;         // forces a 2-frame catch-up tick
        start(seq, fx);
        seq.tick(fx);
        check("FS-RENDER-01a", "2-frame tick: the first frame does NOT composite",
              fx.composites.size() == 2 && fx.composites[0] == false,
              "frames=" + s_int(static_cast<long long>(fx.composites.size())));
        check("FS-RENDER-01b", "2-frame tick: the LAST frame composites",
              fx.composites.size() == 2 && fx.composites[1] == true);
    }
    {
        // A due screenshot forces every frame through the compositor: the
        // captured frame must have gone through it with the layer mask armed.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 20.0;
        fx.shot_due    = true;
        start(seq, fx);
        seq.tick(fx);
        bool all_composited = !fx.composites.empty();
        for (bool c : fx.composites) if (!c) all_composited = false;
        check("FS-RENDER-02", "screenshot due: every frame of the tick composites",
              all_composited && fx.composites.size() == 2,
              "frames=" + s_int(static_cast<long long>(fx.composites.size())));
    }
    {
        // The ordinary single-frame tick composites its one frame.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        start(seq, fx);
        seq.tick(fx);
        check("FS-RENDER-03", "1-frame tick composites its only frame",
              fx.composites.size() == 1 && fx.composites[0] == true);
    }
    {
        // The per-frame hint must be taken down after the loops so that
        // run_frame() callers outside the tick (debugger step) are unaffected.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        start(seq, fx);
        seq.tick(fx);
        check("FS-RENDER-04", "the render hint is restored once per tick",
              fx.reset_hints == 1, "resets=" + s_int(fx.reset_hints));
    }

    // =====================================================================
    // FS-THROTTLE — the >1x compositor throttle
    // =====================================================================
    // Contract (frame_sequencer.h step 5): above 1x the timer emulates far
    // more frames than the display can present, so the render hint is
    // throttled to ~50 Hz wall-clock. At or below 1x every tick renders.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        fx.speed     = 4.0;
        fx.period    = PERIOD_50_US / 4;   // --speed 400 -> ~5 ms ticks
        start(seq, fx);
        run_ticks(seq, fx, 100);
        check("FS-THROTTLE-01a", "speed 4x: some ticks are throttled (not presented)",
              fx.presents < 100, "presents=" + s_int(fx.presents));
        check("FS-THROTTLE-01b", "speed 4x: presentation is throttled to roughly 50 Hz",
              fx.presents > 0 && fx.presents <= 40,
              "presents=" + s_int(fx.presents) + " of 100 ticks");
        // A throttled tick still EMULATES — the machine must not slow down.
        check("FS-THROTTLE-01c", "speed 4x: every tick still emulates a frame",
              fx.composites.size() == 100,
              "frames=" + s_int(static_cast<long long>(fx.composites.size())));
        // ...and those frames must not composite: that is the saving.
        int composited = 0;
        for (bool c : fx.composites) if (c) ++composited;
        check("FS-THROTTLE-01d", "speed 4x: throttled frames skip the compositor",
              composited == fx.presents,
              "composited=" + s_int(composited) + " presents=" + s_int(fx.presents));
    }
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        fx.speed     = 1.0;
        start(seq, fx);
        run_ticks(seq, fx, 50);
        check("FS-THROTTLE-02", "speed 1x: every tick presents (no throttle)",
              fx.presents == 50, "presents=" + s_int(fx.presents));
    }
    {
        // A due screenshot defeats the throttle: the capture must be a frame
        // that went through the compositor this tick.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        fx.speed     = 4.0;
        fx.period    = PERIOD_50_US / 4;
        start(seq, fx);
        run_ticks(seq, fx, 3);           // get into the throttled window
        const int before = fx.presents;
        fx.shot_due = true;
        seq.tick(fx);
        check("FS-THROTTLE-03a", "screenshot due at 4x: the tick presents anyway",
              fx.presents == before + 1,
              "before=" + s_int(before) + " after=" + s_int(fx.presents));
        check("FS-THROTTLE-03b", "screenshot due at 4x: the frame composites",
              !fx.composites.empty() && fx.composites.back() == true);
    }

    // =====================================================================
    // FS-ABANDON — a cold boot abandons the tick cleanly
    // =====================================================================
    // Contract (frame_sequencer.h step 4): pre_frames() returning false means
    // the emulator was reconstructed under us. No frame may run against the
    // new machine this tick, nothing may be presented, and the deadline must
    // NOT be advanced (cold_boot re-anchors it itself — advancing as well
    // would double-count one period). The tick is still counted and still
    // carries interval + handler samples: it really did happen.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        start(seq, fx);
        run_ticks(seq, fx, 3);
        const int frames_before = static_cast<int>(fx.composites.size());
        const int presents_before = fx.presents;
        const int posts_before  = fx.post_calls;
        const size_t ivl_before = fx.intervals.size();

        fx.pre_frames_result = false;
        const frame_sequencer::TickResult r = seq.tick(fx);

        check("FS-ABANDON-01a", "abandoned tick is reported as abandoned",
              r.abandoned);
        check("FS-ABANDON-01b", "abandoned tick runs no frame",
              static_cast<int>(fx.composites.size()) == frames_before);
        check("FS-ABANDON-01c", "abandoned tick presents nothing",
              fx.presents == presents_before);
        check("FS-ABANDON-01d", "abandoned tick runs no post-frame work",
              fx.post_calls == posts_before);
        check("FS-ABANDON-01e", "abandoned tick does not touch the timer",
              fx.intervals.size() == ivl_before,
              "intervals=" + s_int(static_cast<long long>(fx.intervals.size())));
        check("FS-ABANDON-01f", "abandoned tick is still counted",
              seq.stats().ticks == 4, "ticks=" + s_int(seq.stats().ticks));
        check("FS-ABANDON-01g", "abandoned tick still carries a handler sample",
              seq.stats().handler_us.count == 4,
              "count=" + s_int(seq.stats().handler_us.count));
        check("FS-ABANDON-01h", "abandoned tick carries NO emu-time sample",
              seq.stats().emu_us.count == 3,
              "count=" + s_int(seq.stats().emu_us.count));
    }

    // =====================================================================
    // FS-PAUSE — the debugger-paused tick
    // =====================================================================
    // Contract (frame_sequencer.h step 9 else-branch): a paused machine
    // produces no samples, so the device would drain and SDL would zero-fill —
    // two clicks per pause. The device must keep being fed so its underrun
    // guard holds the last level instead.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        fx.is_paused   = true;
        start(seq, fx);
        seq.tick(fx);
        check("FS-PAUSE-01a", "paused: no frame runs",
              fx.composites.empty(),
              "frames=" + s_int(static_cast<long long>(fx.composites.size())));
        check("FS-PAUSE-01b", "paused: the audio device is still fed",
              fx.pushes == 1, "pushes=" + s_int(fx.pushes));
        check("FS-PAUSE-01c", "paused: the push requests the underrun hold",
              fx.push_hold.size() == 1 && fx.push_hold[0] == true);
        check("FS-PAUSE-01d", "paused: the pacer is not consulted",
              seq.audio_doubles() == 0 && seq.audio_skips() == 0);
        check("FS-PAUSE-01e", "paused: an emu-time sample of ~0 is still recorded",
              seq.stats().emu_us.count == 1,
              "count=" + s_int(seq.stats().emu_us.count));
        check("FS-PAUSE-01f", "paused: the present reports NO new content",
              fx.present_new_content.size() == 1 &&
                  fx.present_new_content[0] == false);
        check("FS-PAUSE-01g", "paused: the deadline still advances (timer keeps ticking)",
              fx.intervals.size() == 1);
    }
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        fx.is_paused = true;
        start(seq, fx);
        seq.tick(fx);
        check("FS-PAUSE-02", "paused with no audio device: nothing is pushed",
              fx.pushes == 0, "pushes=" + s_int(fx.pushes));
    }

    // =====================================================================
    // FS-AUDIO — when and how the device is fed
    // =====================================================================
    // Contract (frame_sequencer.h step 9): the push happens AFTER the frames
    // (they are what produced the samples), is skipped entirely during
    // fastload (samples generated at many times real time), and carries the
    // underrun-hold flag only when the tick was genuinely audio-paced.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        start(seq, fx);
        seq.tick(fx);
        check("FS-AUDIO-01a", "audio-paced tick pushes exactly once",
              fx.pushes == 1, "pushes=" + s_int(fx.pushes));
        check("FS-AUDIO-01b", "audio-paced tick enables the underrun hold",
              fx.push_hold.size() == 1 && fx.push_hold[0] == true);
    }
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        fx.speed       = 2.0;          // unpaced: the hold must NOT be routine
        start(seq, fx);
        seq.tick(fx);
        check("FS-AUDIO-02", "unpaced tick (speed 2x) pushes without the underrun hold",
              fx.pushes == 1 && fx.push_hold.size() == 1 && fx.push_hold[0] == false);
    }
    {
        // Tape still in flight at the end of the tick: no push. Feeding the
        // device samples generated at many times real time would either
        // back-pressure the emulator or produce stuttering chunks.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model     = false;
        fx.queue_ms        = 65.0;
        fx.fastload_frames = 100000;   // outlasts the tick
        start(seq, fx);
        seq.tick(fx);
        check("FS-AUDIO-03", "a tick with fastload still active does not push",
              fx.pushes == 0, "pushes=" + s_int(fx.pushes));
    }
    {
        // ...and the moment the tape ENDS inside a tick, normal audio resumes
        // on that same tick.
        //
        // NOTE — DOCUMENTS PRE-EXISTING BEHAVIOUR, NOT A REQUIREMENT. The
        // push gate is evaluated AFTER the burst, so it sees fastload already
        // finished and pushes the burst's worth of samples. Whether that is
        // ideal is a separate question (those samples were generated far
        // faster than real time); the row exists to prove Task 91 did not move
        // the gate, not to bless its placement.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model     = false;
        fx.queue_ms        = 65.0;
        fx.fastload_frames = 3;        // ends during this tick
        start(seq, fx);
        seq.tick(fx);
        check("FS-AUDIO-04", "the tick on which the tape ends resumes pushing audio",
              fx.pushes == 1, "pushes=" + s_int(fx.pushes));
    }

    // =====================================================================
    // FS-BURST — the fastload sprint
    // =====================================================================
    // Contract (frame_sequencer.h step 8): while a tape is loading, extra
    // frames run inside the single tick so the load is not capped at the timer
    // rate. The sprint is bounded so the GUI thread still services repaints,
    // and the tick's closing frame always composites.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model     = false;
        fx.queue_ms        = 65.0;
        fx.fastload_frames = 20;       // tape ends after 20 frames
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        // 1 pacing frame + 19 burst frames exhaust the tape, then 1 closing.
        check("FS-BURST-01a", "a 20-frame tape load completes inside one tick",
              r.frames_rendered == 21,
              "frames=" + s_int(r.frames_rendered));
        check("FS-BURST-01b", "only the tick's closing frame composites",
              fx.composites.size() == 21 && fx.composites.back() == true);
        // The 19 INTERIOR burst frames are the waste this policy removes;
        // none of them may composite.
        //
        // NOTE — DOCUMENTS PRE-EXISTING BEHAVIOUR, NOT A REQUIREMENT. A
        // fastload tick composites TWO frames, not one: the pacing loop's
        // single frame is the last frame of its own loop and cannot know a
        // burst will follow it, so it composites, and then the closing frame
        // composites too. That one redundant compositor pass per fastload
        // tick predates Task 91 and is deliberately left alone here (Task 91
        // is a behaviour-preserving refactor). The row asserts only the
        // INTERIOR frames, which is the actual policy; do not read the
        // two-frame total as a specification.
        int interior_composited = 0;
        for (size_t i = 1; i + 1 < fx.composites.size(); i++)
            if (fx.composites[i]) ++interior_composited;
        check("FS-BURST-01c", "no interior burst frame composites",
              interior_composited == 0,
              "interior_composited=" + s_int(interior_composited));
    }
    {
        // The burst is BOUNDED: an endless tape must not hang the GUI thread.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model     = false;
        fx.queue_ms        = 65.0;
        fx.fastload_frames = 100000;   // never ends within a tick
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        // NOTE — DOCUMENTS PRE-EXISTING BEHAVIOUR, NOT A REQUIREMENT. The cap
        // bounds the BURST (199 loop frames + 1 closing frame); the tick's own
        // paced frame precedes the burst and is NOT counted against the limit,
        // so a capped tick runs limit + 1 = 201 frames. That off-by-one in the
        // ceiling predates Task 91; the row pins it so the refactor is proven
        // not to have shifted it, not because 201 is a designed number.
        check("FS-BURST-02", "an endless fastload is capped at the burst limit",
              r.frames_rendered == frame_sequencer::FASTLOAD_BURST_LIMIT + 1,
              "frames=" + s_int(r.frames_rendered) + " limit=" +
                  s_int(frame_sequencer::FASTLOAD_BURST_LIMIT));
    }
    {
        // No fastload -> no burst at all.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        start(seq, fx);
        const frame_sequencer::TickResult r = seq.tick(fx);
        check("FS-BURST-03", "no fastload: the tick runs only its paced frames",
              r.frames_rendered == 1, "frames=" + s_int(r.frames_rendered));
    }

    // =====================================================================
    // FS-CADENCE — frame accounting over a window
    // =====================================================================
    // Contract (present_cadence.h): `emulated` counts every run_frame();
    // frames that could never reach the screen are attributed as `superseded`
    // (earlier frames of a multi-frame tick) or `unrendered` (frames of a
    // throttled tick), so that `lost` means only "a fresh frame was never
    // painted". Mis-wiring these buckets makes the report blame the window
    // system for losses the frontend caused deliberately.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        start(seq, fx);
        run_ticks(seq, fx, 50);
        check("FS-CADENCE-01a", "1 frame per tick: emulated counts every frame",
              seq.cadence().emulated == 50,
              "emulated=" + s_int(static_cast<long long>(seq.cadence().emulated)));
        check("FS-CADENCE-01b", "1 frame per tick: nothing is superseded",
              seq.cadence().superseded == 0,
              "superseded=" + s_int(static_cast<long long>(seq.cadence().superseded)));
        check("FS-CADENCE-01c", "1 frame per tick: nothing is unrendered",
              seq.cadence().unrendered == 0,
              "unrendered=" + s_int(static_cast<long long>(seq.cadence().unrendered)));
    }
    {
        // A catch-up double: 2 emulated, 1 of them superseded.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 20.0;
        start(seq, fx);
        seq.tick(fx);
        check("FS-CADENCE-02a", "a 2-frame tick counts 2 emulated frames",
              seq.cadence().emulated == 2,
              "emulated=" + s_int(static_cast<long long>(seq.cadence().emulated)));
        check("FS-CADENCE-02b", "a 2-frame tick attributes 1 frame as superseded",
              seq.cadence().superseded == 1,
              "superseded=" + s_int(static_cast<long long>(seq.cadence().superseded)));
    }
    {
        // A throttled tick: its frames are `unrendered`, never `superseded`.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        fx.speed     = 4.0;
        fx.period    = PERIOD_50_US / 4;
        start(seq, fx);
        run_ticks(seq, fx, 100);
        check("FS-CADENCE-03a", "throttled ticks attribute their frames as unrendered",
              seq.cadence().unrendered > 0,
              "unrendered=" + s_int(static_cast<long long>(seq.cadence().unrendered)));
        check("FS-CADENCE-03b", "emulated + attribution stays consistent over the window",
              seq.cadence().emulated == 100,
              "emulated=" + s_int(static_cast<long long>(seq.cadence().emulated)));
    }
    {
        // The cadence window drains independently of the stats window.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        start(seq, fx);
        run_ticks(seq, fx, 10);
        seq.reset_cadence();
        run_ticks(seq, fx, 5);
        check("FS-CADENCE-04", "a cadence drain resets only the cadence window",
              seq.cadence().emulated == 5 && seq.stats().ticks == 15,
              "emulated=" + s_int(static_cast<long long>(seq.cadence().emulated)) +
                  " ticks=" + s_int(seq.stats().ticks));
    }

    // =====================================================================
    // FS-PRESENT — what reaches the display, and what it is labelled
    // =====================================================================
    // Contract (present_cadence.h carries_new_content): a push that carries no
    // newly composited frame must say so, or a paused frontend re-presenting a
    // static screen inflates `presented` and the report then claims lost == 0
    // while frames really were lost.
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        start(seq, fx);
        seq.tick(fx);
        check("FS-PRESENT-01", "a tick that rendered a frame presents new content",
              fx.present_new_content.size() == 1 && fx.present_new_content[0] == true);
    }
    {
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 110.0;        // pacer skips: 0 frames run
        start(seq, fx);
        seq.tick(fx);
        check("FS-PRESENT-02a", "an audio-skip tick still presents (the frame is unchanged)",
              fx.presents == 1, "presents=" + s_int(fx.presents));
        check("FS-PRESENT-02b", "an audio-skip tick presents NO new content",
              fx.present_new_content.size() == 1 &&
                  fx.present_new_content[0] == false);
    }
    {
        // post_frames() must be told how many frames actually rendered: the
        // delayed screenshot uses it to decide whether the framebuffer holds a
        // frame composited with the requested layer mask.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 20.0;         // 2-frame tick
        start(seq, fx);
        seq.tick(fx);
        check("FS-PRESENT-03", "post-frame work is told the true frame count",
              fx.post_frames_arg.size() == 1 && fx.post_frames_arg[0] == 2,
              "arg=" + s_int(fx.post_frames_arg.empty() ? -1 : fx.post_frames_arg[0]));
    }

    // =====================================================================
    // FS-ORDER — the step order itself
    // =====================================================================
    // Several orderings are load-bearing and a transposition is invisible to
    // every policy suite. These rows pin them one at a time.
    {
        // pre_frames() must precede the frames: it arms the screenshot layer
        // mask that those very frames must be composited with.
        struct OrderFrontend : FakeFrontend {
            std::vector<std::string> log;
            bool pre_frames() { log.push_back("pre"); return FakeFrontend::pre_frames(); }
            void run_frame(bool c) { log.push_back("frame"); FakeFrontend::run_frame(c); }
            void push_audio(bool h) { log.push_back("push"); FakeFrontend::push_audio(h); }
            void present(bool n) { log.push_back("present"); FakeFrontend::present(n); }
            void post_frames(int f) { log.push_back("post"); FakeFrontend::post_frames(f); }
            void set_timer_interval(int m) { log.push_back("timer"); FakeFrontend::set_timer_interval(m); }
        };
        frame_sequencer::Sequencer seq;
        OrderFrontend fx;
        fx.audio_model = false;
        fx.queue_ms    = 65.0;
        start(seq, fx);
        seq.tick(fx);
        const std::vector<std::string> want = {"pre", "frame", "push", "present",
                                               "post", "timer"};
        check("FS-ORDER-01", "the tick's effects run in the contracted order",
              fx.log == want,
              "got=" + [&] {
                  std::string s;
                  for (auto& e : fx.log) { s += e; s += " "; }
                  return s;
              }());
    }
    {
        // The timer is rescheduled LAST, so this tick's own work is subtracted
        // from the wait. If it were rescheduled before the frames, a tick
        // costing most of its period would over-run every deadline.
        frame_sequencer::Sequencer seq;
        FakeFrontend fx;
        fx.has_audio = false;
        fx.run_frame_cost_us = 15000;   // 15 ms of a 20.259 ms period
        start(seq, fx);
        run_ticks(seq, fx, 6);          // reach steady state on the grid
        check("FS-ORDER-02",
              "the interval is computed after the work, so it is shorter than the period",
              !fx.intervals.empty() && fx.intervals.back() <= 6,
              "interval=" + s_int(fx.intervals.empty() ? -1 : fx.intervals.back()) + "ms");
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
