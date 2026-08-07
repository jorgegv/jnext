// Audio-clock pacing test (GitHub issue #7 / Task 23).
//
// Unlike the rest of the audio suite this file has no VHDL oracle: it tests
// the *host* audio pipeline, not emulated hardware. The behaviour under test
// is src/platform/audio_pacing.h, which decides how many emulator frames to
// run per timer tick so that the emulator's audio clock tracks the sound
// card's clock instead of drifting behind it.
//
// The bug it guards against (issue #7 "constant noise", Task 23 "clicking
// over beeper music" — one and the same defect):
//
//   The frontends ran exactly one emulator frame per 20 ms wall-clock tick,
//   i.e. 50.00 frames per real second. A 48K frame is 69888 T-states at
//   3.5 MHz = 1/50.08 s, and the mixer synthesises 44100 samples per
//   *emulated* second. So the emulator fed the device 50 x (44100/50.08) =
//   44030 samples per real second while the sound card consumed 44100 — a
//   permanent ~70 sample/s deficit. The device queue drained to empty, SDL
//   padded the buffer with zeros, and every shortfall stepped the signal to
//   0 and back: an audible click, a few times a second, until quit.
//
// AP-03 is the discriminative regression test: it runs the closed loop with
// the real rate numbers and asserts the queue never empties. AP-04 proves the
// test has teeth by running the same loop with the old fixed-1-frame policy
// and asserting that it *does* empty.
//
// AP-13..AP-16 (Task 63, landed with the frame-deadline scheduler): the band
// thresholds now act on a smoothed queue estimate (audio_pacing::BandState —
// EMA + action feed-forward + chunk clamp). With the timer's +1.1% bias gone
// the queue is a zero-drift random walk, and the device's ~23.2 ms
// consumption chunks (1024 frames @ 44100 Hz) put a sawtooth on every
// queued_ms() reading; the old stateless band let that sawtooth clip both
// edges (~2.5 visible one-frame jumps/holds per second). The sawtooth cannot
// reach the thresholds through the EMA; genuine drift or a disturbance still
// crosses them within ~1 s and is corrected one-sidedly. These rows model
// exactly that spec. (A trigger/re-arm hysteresis was prototyped first and
// rejected — see the WHY block in audio_pacing.h.)
//
// Run: ./build/test/audio_pacing_test

#include "platform/audio_pacing.h"
#include "audio/mixer.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

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

std::string fmt_d(const char* label, double v)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s=%.2f", label, v);
    return std::string(buf);
}

// --- Closed-loop model of the frontend tick ---------------------------------
//
// One tick = 20 ms of wall clock. The sound card consumes 44100 samples per
// real second; the emulator produces SAMPLES_PER_FRAME per emulated frame.
// Returns the minimum queue depth (in ms) observed over `ticks`.
//
// `paced` selects the policy: true = audio_pacing::frames_for_tick (the fix),
// false = the old "always exactly one frame per tick" behaviour.
constexpr double TICK_MS = 20.0;
constexpr double SAMPLES_PER_TICK = Mixer::SAMPLE_RATE * TICK_MS / 1000.0;  // 882

// The defect is TWO-SIDED, because the machines' frame rates straddle the 20 ms
// tick (src/core/emulator_config.h machine_timing()):
//
//   48K       224 T x 312 lines = 69888 T @3.5 MHz = 50.08 Hz -> 19.968 ms/frame
//             frame SHORTER than the tick -> UNDER-production -> the device queue
//             starves -> SDL pads with ZEROS -> clicks (Task 23 / issue #7).
//
//   128K/+3/  228 T x 311 lines = 70908 T @3.5 MHz = 49.36 Hz -> 20.259 ms/frame
//   Next      frame LONGER than the tick -> OVER-production -> the queue overfills
//             -> the old code drained the mixer ring and DISCARDED the chunk,
//             dropping audio outright (a skip rather than a hole).
//
// Same root cause, opposite sign. Both must converge under pacing.
constexpr double FRAME_HZ_48K = 3'500'000.0 / (224.0 * 312.0);   // 50.08 Hz
constexpr double FRAME_HZ_128K = 3'500'000.0 / (228.0 * 311.0);  // 49.36 Hz (also Next)

constexpr double samples_per_frame(double frame_hz)
{
    return Mixer::SAMPLE_RATE / frame_hz;
}

// min_ms / max_ms track only depths the loop actually settles on — the initial
// depth is excluded, or a run that deliberately STARTS at the ceiling would
// trivially report max == ceiling and say nothing about whether it converges.
struct LoopRun {
    double min_ms;   // 0 => the device ran dry (SDL injected zeros: clicks)
    double max_ms;   // >= QUEUE_MAX_MS => the old drain-and-discard path re-arms
};

LoopRun simulate(int ticks, bool paced, double start_ms, double frame_hz = FRAME_HZ_48K)
{
    const double per_frame = samples_per_frame(frame_hz);
    double queued = Mixer::SAMPLE_RATE * start_ms / 1000.0;  // samples in the device queue
    LoopRun r{1e9, 0.0};
    audio_pacing::BandState band;  // per-device pacer state, held across ticks

    for (int t = 0; t < ticks; t++) {
        const int queued_ms = static_cast<int>(queued * 1000.0 / Mixer::SAMPLE_RATE);
        const int frames = paced ? audio_pacing::frames_for_tick(band, queued_ms) : 1;

        queued += frames * per_frame;           // emulator produces
        queued -= SAMPLES_PER_TICK;             // sound card consumes
        if (queued < 0.0) queued = 0.0;         // empty: SDL pads with zeros == click

        const double ms = queued * 1000.0 / Mixer::SAMPLE_RATE;
        if (ms < r.min_ms) r.min_ms = ms;
        if (ms > r.max_ms) r.max_ms = ms;
    }
    return r;
}

// --- Model of SdlAudio::push_from_mixer against a real device queue ---------
//
// Mirrors the runtime ordering in sdl_audio.cpp: each tick the device has
// drained TICK_MS of audio (injecting ZEROS — the click — if it ran out
// mid-tick), then we push whatever the emulator produced, then the underrun
// guard tops the queue back up to `floor_ms` by holding the last level.
//
// `produced_per_tick` is what the host actually managed to synthesise: on a
// healthy host that is frames_for_tick() x SAMPLES_PER_FRAME; on a starved host
// (danboid's) it is less, and can be 0 while the host stalls.
//
// Returns {zero_injections, pad_events}: the first is audible clicks, the
// second is how often the guard had to invent samples.
struct DeviceRun {
    int zero_injections;
    int pad_events;
};

DeviceRun simulate_device(int ticks, int floor_ms, bool starved)
{
    double queued = 0.0;
    DeviceRun r{0, 0};
    audio_pacing::BandState band;  // per-device pacer state, held across ticks

    for (int t = 0; t < ticks; t++) {
        // 1. the device consumed a tick's worth; if it ran dry, SDL injected zeros
        queued -= SAMPLES_PER_TICK;
        if (queued < 0.0) {
            queued = 0.0;
            if (t > 0) r.zero_injections++;   // t==0: queue legitimately starts empty
        }

        // 2. the emulator's samples for this tick
        const int queued_ms = static_cast<int>(queued * 1000.0 / Mixer::SAMPLE_RATE);
        const double produced =
            starved ? 0.0
                    : audio_pacing::frames_for_tick(band, queued_ms) *
                          samples_per_frame(FRAME_HZ_48K);
        queued += produced;

        // 3. the underrun guard (parameterised on floor_ms so the test can show
        //    what the shipped 15 ms floor did)
        const double floor_samples = Mixer::SAMPLE_RATE * floor_ms / 1000.0;
        if (queued < floor_samples) {
            queued = floor_samples;
            r.pad_events++;
        }
    }
    return r;
}

// --- Task 63 model: deadline-paced ticks vs chunked device consumption ------
//
// The frame-deadline scheduler (src/platform/frame_deadline.h) delivers ticks
// at the EXACT emulated frame period — 17.197 ms at Next-60 — so production
// carries zero drift by construction. The device, however, consumes in whole
// hardware buffers: 1024 frames at 44100 Hz = ~23.22 ms removed at once. A
// queued_ms() reading taken at tick entry therefore carries a ~23 ms sawtooth
// around the true mean. This model reproduces exactly that geometry:
//
//   * ticks at t = k * tick_ms (default TICK_N60_MS); each runs
//     frames_for_tick() on the reading, then pushes frames * tick_ms *
//     (1 + drift) of audio (drift models a mis-paced production clock;
//     0 = the deadline pacer);
//   * the device removes one CHUNK_N60_MS chunk whenever model time passes
//     the chunk's due time; `jit_seed` != 0 jitters each chunk's delivery
//     by a deterministic per-chunk +/-8 ms hash (host scheduling jitter —
//     it is what makes compressed double-chunk dips and stretched
//     zero-chunk peaks appear in the readings, as on the real pipeline);
//   * `walk_seed` != 0 adds a +/-0.75 ms zero-mean LCG step to the queue
//     each tick — the "zero-drift random walk" measured on the real
//     pipeline (pipewire rate wobble); deterministic per seed.
//
// Interventions (doubles + skips) and queue extremes are reported both in
// total and after `settle_ticks`, so a test can allow a settling transient
// and then assert steady-state behaviour.
constexpr double TICK_N60_MS = 17.197;                       // Next-60 deadline tick
constexpr double TICK_128K_MS = 20.259;                      // 49.36 Hz machines
constexpr double CHUNK_N60_MS = 1024.0 * 1000.0 / 44100.0;   // 23.22 device chunk

/// The pre-Task-63 stateless policy, encoded from its spec (trigger on any
/// reading outside [LOW, HIGH), no memory). Comparator for the teeth rows.
int stateless_frames_for_tick(int queued_ms)
{
    if (queued_ms < 0) return 1;
    if (queued_ms >= audio_pacing::QUEUE_HIGH_MS) return 0;
    if (queued_ms < audio_pacing::QUEUE_LOW_MS) return 2;
    return 1;
}

/// Deterministic per-chunk delivery jitter in [-8, +8] ms.
double chunk_jitter(uint32_t k, uint32_t seed)
{
    uint32_t h = (k + 1) * 2654435761u ^ seed;
    h ^= h >> 13;
    h *= 2246822519u;
    h ^= h >> 16;
    return (static_cast<double>(h % 2001u) - 1000.0) / 1000.0 * 8.0;
}

struct ChunkRun {
    int doubles = 0, skips = 0;              // whole run
    int doubles_after = 0, skips_after = 0;  // after settle_ticks
    int last_intervention_tick = -1;         // last tick that doubled/skipped
    double min_ms = 1e9, max_ms = 0.0;       // queue extremes, whole run
};

ChunkRun simulate_chunked(int ticks, double start_ms, double drift, bool smoothed,
                          uint32_t walk_seed, uint32_t jit_seed, int settle_ticks,
                          double tick_ms = TICK_N60_MS)
{
    double queued = start_ms;
    uint32_t next_chunk = 0;  // index of the next chunk to consume
    uint32_t rng = walk_seed;
    audio_pacing::BandState band;
    ChunkRun r;

    for (int t = 0; t < ticks; t++) {
        const double now = t * tick_ms;
        // device consumed every chunk whose (jittered) due time has passed
        while (next_chunk * CHUNK_N60_MS + CHUNK_N60_MS +
                   (jit_seed ? chunk_jitter(next_chunk, jit_seed) : 0.0) <= now) {
            queued -= CHUNK_N60_MS;
            if (queued < 0.0) queued = 0.0;
            next_chunk++;
        }
        if (walk_seed != 0) {
            rng = rng * 1664525u + 1013904223u;
            queued += (static_cast<int>((rng >> 8) % 1501u) - 750) / 1000.0;
            if (queued < 0.0) queued = 0.0;
        }
        if (queued < r.min_ms) r.min_ms = queued;

        const int reading = static_cast<int>(queued);
        const int frames = smoothed
                               ? audio_pacing::frames_for_tick(band, reading)
                               : stateless_frames_for_tick(reading);
        if (frames != 1) {
            r.last_intervention_tick = t;
            if (frames == 2) { r.doubles++; if (t >= settle_ticks) r.doubles_after++; }
            if (frames == 0) { r.skips++;   if (t >= settle_ticks) r.skips_after++; }
        }
        queued += frames * tick_ms * (1.0 + drift);
        if (queued > r.max_ms) r.max_ms = queued;
    }
    return r;
}

}  // namespace

int main()
{
    std::printf("Audio pacing (issue #7 / Task 23) — host audio-clock tracking\n");
    std::printf("====================================================\n\n");

    // --- AP-01: the policy band (triggers, evaluated from the armed state) ---
    // Task 63: frames_for_tick takes the caller's BandState; the TRIGGER
    // points are unchanged, so each row uses a fresh (armed) state.
    {
        audio_pacing::BandState st;
        check("AP-01a", "queue below LOW asks for a catch-up frame",
              audio_pacing::frames_for_tick(st, audio_pacing::QUEUE_LOW_MS - 1) == 2);
    }
    {
        audio_pacing::BandState s1, s2;
        check("AP-01b", "queue inside the band runs exactly one frame",
              audio_pacing::frames_for_tick(s1, audio_pacing::QUEUE_LOW_MS) == 1 &&
              audio_pacing::frames_for_tick(s2, audio_pacing::QUEUE_HIGH_MS - 1) == 1);
    }
    {
        audio_pacing::BandState st;
        check("AP-01c", "queue at/above HIGH skips the frame and lets the device drain",
              audio_pacing::frames_for_tick(st, audio_pacing::QUEUE_HIGH_MS) == 0);
    }
    {
        audio_pacing::BandState st;
        check("AP-01d", "audio off (negative depth) free-runs at the timer rate",
              audio_pacing::frames_for_tick(st, -1) == 1);
    }

    // --- AP-02: the thresholds are ordered ----------------------------------
    check("AP-02", "FLOOR < LOW < HIGH < MAX",
          audio_pacing::QUEUE_FLOOR_MS < audio_pacing::QUEUE_LOW_MS &&
          audio_pacing::QUEUE_LOW_MS   < audio_pacing::QUEUE_HIGH_MS &&
          audio_pacing::QUEUE_HIGH_MS  < audio_pacing::QUEUE_MAX_MS);

    // --- AP-02b: the underrun floor must outlast one frontend tick ----------
    // The hold-guard only runs when a tick runs. A floor shorter than the tick
    // period cannot bridge a single late tick: the device drains to empty
    // before the next push and SDL injects zeros regardless. (Observed with a
    // 15 ms floor against the 20 ms tick — clicks survived on a loaded host.)
    check("AP-02b", "underrun floor exceeds the 20 ms frontend tick period",
          audio_pacing::QUEUE_FLOOR_MS > static_cast<int>(TICK_MS),
          fmt_d("floor_ms", audio_pacing::QUEUE_FLOOR_MS));

    // --- AP-03: 48K — the closed loop never starves the device ----------------
    // 20000 ticks = 400 s of emulation, far beyond the ~20 s in which the
    // unpaced deficit emptied the queue in the field.
    {
        const LoopRun r = simulate(20000, /*paced=*/true, audio_pacing::QUEUE_LOW_MS);
        check("AP-03", "48K paced: device queue never empties over 400 s",
              r.min_ms > 0.0, fmt_d("min_queue_ms", r.min_ms));
        // It should not merely stay off zero — it should stay in a healthy band.
        check("AP-03b", "48K paced: queue stays at or above the underrun floor",
              r.min_ms >= audio_pacing::QUEUE_FLOOR_MS, fmt_d("min_queue_ms", r.min_ms));
    }

    // --- AP-04: ...and the old policy demonstrably did ------------------------
    // Guards the guard: if this ever passes, the simulation has stopped
    // modelling the defect and AP-03 proves nothing.
    {
        const LoopRun r = simulate(20000, /*paced=*/false, audio_pacing::QUEUE_LOW_MS);
        check("AP-04", "48K unpaced (old policy): device queue drains to empty",
              r.min_ms == 0.0, fmt_d("min_queue_ms", r.min_ms));
    }

    // --- AP-05: a full queue is pulled back into the band --------------------
    {
        const LoopRun r = simulate(20000, /*paced=*/true, audio_pacing::QUEUE_MAX_MS);
        check("AP-05", "48K paced: an over-full queue converges without emptying",
              r.min_ms >= audio_pacing::QUEUE_FLOOR_MS, fmt_d("min_queue_ms", r.min_ms));
    }

    // --- AP-10: 128K/+3/Next — the OVER-production side of the same bug -------
    // A 49.36 Hz machine's frame (20.26 ms) is LONGER than the 20 ms tick, so an
    // unpaced frontend feeds the device faster than it drains. The queue climbs
    // until it crosses QUEUE_MAX_MS, where the old code drained the mixer ring
    // and then discarded what it had drained — a hole in the stream, i.e. a
    // click, just from the other direction. Pacing must hold the queue below
    // that ceiling instead (frames_for_tick returns 0 above QUEUE_HIGH_MS).
    {
        const LoopRun r =
            simulate(20000, /*paced=*/true, audio_pacing::QUEUE_LOW_MS, FRAME_HZ_128K);
        check("AP-10a", "128K/Next paced: queue never reaches the discard ceiling",
              r.max_ms < audio_pacing::QUEUE_MAX_MS, fmt_d("max_queue_ms", r.max_ms));
        check("AP-10b", "128K/Next paced: queue never empties either",
              r.min_ms >= audio_pacing::QUEUE_FLOOR_MS, fmt_d("min_queue_ms", r.min_ms));
    }

    // --- AP-11: ...and the old policy demonstrably overflowed -----------------
    // The teeth for AP-10: unpaced at 49.36 Hz the queue ratchets past the
    // ceiling and the old drain-and-discard fires.
    {
        const LoopRun r =
            simulate(20000, /*paced=*/false, audio_pacing::QUEUE_LOW_MS, FRAME_HZ_128K);
        check("AP-11", "128K/Next unpaced (old policy): queue overruns the ceiling",
              r.max_ms >= audio_pacing::QUEUE_MAX_MS, fmt_d("max_queue_ms", r.max_ms));
    }

    // --- AP-12: both machines settle inside the band, from either extreme -----
    {
        bool ok = true;
        double worst_lo = 1e9, worst_hi = 0.0;
        for (double hz : {FRAME_HZ_48K, FRAME_HZ_128K}) {
            for (double start : {0.0, static_cast<double>(audio_pacing::QUEUE_MAX_MS)}) {
                const LoopRun r = simulate(20000, /*paced=*/true, start, hz);
                if (r.max_ms >= audio_pacing::QUEUE_MAX_MS) ok = false;
                // Ignore the transient from a start of 0 for the low-water check:
                // the queue legitimately begins empty and climbs into the band.
                const LoopRun s = simulate(20000, /*paced=*/true, audio_pacing::QUEUE_LOW_MS, hz);
                if (s.min_ms < audio_pacing::QUEUE_FLOOR_MS) ok = false;
                worst_lo = std::min(worst_lo, s.min_ms);
                worst_hi = std::max(worst_hi, r.max_ms);
            }
        }
        check("AP-12", "48K and 128K/Next both converge inside the queue band",
              ok, fmt_d("worst_min_ms", worst_lo) + " " + fmt_d("worst_max_ms", worst_hi));
    }

    // --- AP-06: the underrun guard keeps a STARVED host off zero -------------
    // The host produces nothing at all (worst case). Ticks still run, so the
    // guard still runs. With a floor longer than one tick the device is always
    // handed enough audio to survive to the next push: no zeros, hence no
    // clicks — the host stutters on held samples instead.
    {
        const DeviceRun r = simulate_device(2000, audio_pacing::QUEUE_FLOOR_MS, /*starved=*/true);
        check("AP-06", "starved host: underrun guard prevents every zero-injection",
              r.zero_injections == 0,
              fmt_d("zero_injections", r.zero_injections));
    }

    // --- AP-07: ...and a floor shorter than one tick does NOT ----------------
    // This is the bug that shipped in the first cut of this fix: a 15 ms floor
    // against a 20 ms tick cannot bridge a single tick, so the device drains to
    // empty before the next push and SDL injects zeros anyway. It was caught by
    // ear on a loaded host, not by a test. This is that test.
    {
        const DeviceRun r = simulate_device(2000, /*floor_ms=*/15, /*starved=*/true);
        check("AP-07", "a sub-tick floor (15ms < 20ms tick) still injects zeros",
              r.zero_injections > 0,
              fmt_d("zero_injections", r.zero_injections));
    }

    // --- AP-08: on a healthy host the guard never fires -----------------------
    // Padding inserts samples the emulator never produced. Pacing must do the
    // work; padding is a rescue, not a routine (see audio_pacing.h).
    {
        const DeviceRun r = simulate_device(20000, audio_pacing::QUEUE_FLOOR_MS, /*starved=*/false);
        check("AP-08a", "healthy host: no zero-injections over 400 s",
              r.zero_injections == 0, fmt_d("zero_injections", r.zero_injections));
        check("AP-08b", "healthy host: underrun padding never fires",
              r.pad_events == 0, fmt_d("pad_events", r.pad_events));
    }

    // --- AP-09: the pad arithmetic ------------------------------------------
    {
        const int sr = Mixer::SAMPLE_RATE;
        const int floor_samples = audio_pacing::ms_to_samples(audio_pacing::QUEUE_FLOOR_MS, sr);
        check("AP-09a", "an empty queue is padded to exactly the floor",
              audio_pacing::underrun_pad_samples(0, sr) == floor_samples);
        check("AP-09b", "a queue at the floor is not padded",
              audio_pacing::underrun_pad_samples(floor_samples, sr) == 0);
        check("AP-09c", "a queue above the floor is not padded",
              audio_pacing::underrun_pad_samples(floor_samples + 1, sr) == 0 &&
              audio_pacing::underrun_pad_samples(sr, sr) == 0);
        check("AP-09d", "a half-empty queue is topped up to the floor, not overfilled",
              audio_pacing::underrun_pad_samples(floor_samples / 2, sr) ==
                  floor_samples - floor_samples / 2);
    }

    // ========================================================================
    // Task 63 — smoothed band reading + envelope emergencies (see the WHY
    // block in audio_pacing.h and the header comment above).
    // ========================================================================
    const int MID_BAND_MS =
        (audio_pacing::QUEUE_LOW_MS + audio_pacing::QUEUE_HIGH_MS) / 2;  // 65

    // --- AP-13: the estimator and guards, boundary-exact ---------------------
    {
        // A full-amplitude chunk sawtooth around a mid-band mean must never
        // reach a threshold through the smoothing: that flattening is the
        // whole point of the estimator.
        audio_pacing::BandState st;
        bool quiet = true;
        for (int t = 0; t < 400; t++) {
            const int reading = MID_BAND_MS + ((t & 1) ? +12 : -12);  // ~23 pk-pk
            if (audio_pacing::frames_for_tick(st, reading) != 1) quiet = false;
        }
        check("AP-13a", "mid-band chunk sawtooth never reaches a threshold", quiet);
    }
    {
        // The first reading SEEDS the estimate exactly (no cold-start lag),
        // which is what makes the AP-01 boundary rows exact.
        audio_pacing::BandState st;
        (void)audio_pacing::frames_for_tick(st, MID_BAND_MS);
        check("AP-13b", "first reading seeds the estimate directly",
              st.smoothed_ms >= MID_BAND_MS - 1.0 && st.smoothed_ms <= MID_BAND_MS + 1.0,
              fmt_d("smoothed", st.smoothed_ms));
    }
    {
        // Readings held at EMERGENCY_LOW_MS (just above the raw guard, below
        // QUEUE_LOW_MS) can only trigger through the EMA. It must get there —
        // and not instantly, or the smoothing does nothing. This is the row
        // that dies if the EMA update is removed (the estimate would stay at
        // its mid-band seed forever and never intervene).
        audio_pacing::BandState st;
        (void)audio_pacing::frames_for_tick(st, MID_BAND_MS);  // seed mid-band
        int first_double = -1;
        for (int t = 1; t <= 200 && first_double < 0; t++) {
            if (audio_pacing::frames_for_tick(st, audio_pacing::EMERGENCY_LOW_MS) == 2)
                first_double = t;
        }
        check("AP-13c", "a low queue triggers through the EMA alone, in (2, 120] ticks",
              first_double > 2 && first_double <= 120, fmt_d("first_double", first_double));
    }
    {
        // Action feed-forward, catch-up side: the double's effect on the real
        // queue is credited to the estimate immediately, so the very next
        // (recovered) reading does not re-double.
        audio_pacing::BandState st;
        const bool trig = audio_pacing::frames_for_tick(st, 38) == 2;  // seeds + triggers
        const bool once = audio_pacing::frames_for_tick(st, 56) == 1;  // 38 + one frame
        check("AP-13d", "catch-up feed-forward prevents runaway re-doubling", trig && once);
    }
    {
        // Action feed-forward, drain side.
        audio_pacing::BandState st;
        const bool trig = audio_pacing::frames_for_tick(st, 92) == 0;
        const bool once = audio_pacing::frames_for_tick(st, 74) == 1;  // 92 - one frame
        check("AP-13e", "drain feed-forward prevents runaway re-skipping", trig && once);
    }
    {
        // The low emergency guard fires on the RAW reading at exactly
        // EMERGENCY_LOW_MS, regardless of a mid-band estimate.
        audio_pacing::BandState s1, s2;
        (void)audio_pacing::frames_for_tick(s1, MID_BAND_MS);
        (void)audio_pacing::frames_for_tick(s2, MID_BAND_MS);
        check("AP-13f", "low emergency guard boundary-exact on the raw reading",
              audio_pacing::frames_for_tick(s1, audio_pacing::EMERGENCY_LOW_MS - 1) == 2 &&
              audio_pacing::frames_for_tick(s2, audio_pacing::EMERGENCY_LOW_MS) == 1);
    }
    {
        // The high emergency guard fires on the RAW reading at exactly
        // QUEUE_MAX_MS - INTERVENTION_MS, regardless of a mid-band estimate.
        const int G = audio_pacing::QUEUE_MAX_MS - audio_pacing::INTERVENTION_MS;
        audio_pacing::BandState s1, s2;
        (void)audio_pacing::frames_for_tick(s1, MID_BAND_MS);
        (void)audio_pacing::frames_for_tick(s2, MID_BAND_MS);
        check("AP-13g", "high emergency guard boundary-exact on the raw reading",
              audio_pacing::frames_for_tick(s1, G) == 0 &&
              audio_pacing::frames_for_tick(s2, G - 1) == 1);
    }
    {
        // Losing the device kills the estimate: a later device re-seeds from
        // its own first reading (here: a genuinely low one acts instantly).
        audio_pacing::BandState st;
        (void)audio_pacing::frames_for_tick(st, MID_BAND_MS);
        const bool freerun = audio_pacing::frames_for_tick(st, -1) == 1;
        check("AP-13h", "no-audio reading free-runs and resets the estimate",
              freerun && st.smoothed_ms < 0.0 &&
              audio_pacing::frames_for_tick(st, audio_pacing::QUEUE_LOW_MS - 1) == 2);
    }

    // --- AP-14: zero drift + chunk sawtooth => steady state is silent --------
    // 60 s at Next-60 = 3489 ticks; 2 s settling window.
    {
        const int ticks = static_cast<int>(60000.0 / TICK_N60_MS);
        const int settle = static_cast<int>(2000.0 / TICK_N60_MS);
        const ChunkRun r = simulate_chunked(ticks, MID_BAND_MS, /*drift=*/0.0,
                                            /*smoothed=*/true, /*walk_seed=*/0,
                                            /*jit_seed=*/0, settle);
        check("AP-14a", "zero drift, chunked device: no interventions after settling",
              r.doubles_after + r.skips_after == 0,
              fmt_d("doubles", r.doubles) + " " + fmt_d("skips", r.skips));
    }
    {
        // The measured mechanism (Task 63): zero MEAN drift, a slow random
        // walk, and host scheduling jitter on the chunk deliveries. The
        // estimator only crosses a threshold when the walked mean does — the
        // acceptance bound is the issue-#9 target of < 0.2/s, on all three
        // pinned seed pairs.
        const int ticks = static_cast<int>(60000.0 / TICK_N60_MS);
        const int settle = static_cast<int>(2000.0 / TICK_N60_MS);
        const uint32_t seeds[3][2] = {
            {0xC0FFEEu, 0x51C817u}, {0x1234567u, 0xAB12u}, {0xDEAD01u, 0x9997u}};
        bool ok = true;
        double worst = 0.0;
        for (const auto& sp : seeds) {
            const ChunkRun r = simulate_chunked(ticks, MID_BAND_MS, 0.0, true,
                                                sp[0], sp[1], settle);
            const double per_s = (r.doubles_after + r.skips_after) / 58.0;
            worst = std::max(worst, per_s);
            if (per_s >= 0.2) ok = false;
        }
        check("AP-14b", "random-walk queue: smoothing holds interventions under 0.2/s",
              ok, fmt_d("worst_per_s", worst));
    }
    {
        // Teeth: the SAME walks under the old stateless band, whose
        // raw-reading triggers clip on the jittered sawtooth — if this row
        // ever passes, the model has stopped being adversarial and AP-14b
        // proves nothing.
        const int ticks = static_cast<int>(60000.0 / TICK_N60_MS);
        const int settle = static_cast<int>(2000.0 / TICK_N60_MS);
        const uint32_t seeds[3][2] = {
            {0xC0FFEEu, 0x51C817u}, {0x1234567u, 0xAB12u}, {0xDEAD01u, 0x9997u}};
        bool ok = true;
        double best = 1e9;
        for (const auto& sp : seeds) {
            const ChunkRun r = simulate_chunked(ticks, MID_BAND_MS, 0.0,
                                                /*smoothed=*/false, sp[0], sp[1], settle);
            const double per_s = (r.doubles_after + r.skips_after) / 58.0;
            best = std::min(best, per_s);
            if (per_s < 0.2) ok = false;
        }
        check("AP-14c", "the stateless band exceeds 0.2/s on the same walks (teeth)",
              ok, fmt_d("best_per_s", best));
    }

    // --- AP-15: genuine sustained drift is corrected promptly, one-sided -----
    // +1% production drift grows the queue ~10 ms/s; each skipped frame
    // forgoes one frame (~17-20 ms), so the theoretical steady rate is
    // ~0.5-0.6 skips/s. Runs cover three chunk-jitter seeds at Next-60 plus
    // the longest machine frame (49.36 Hz), the worst case for the
    // QUEUE_MAX_MS ceiling.
    {
        const uint32_t jits[3] = {0x51C817u, 0xAB12u, 0x9997u};
        bool onesided = true, rate_ok = true, bounds_ok = true;
        double worst_rate_lo = 1e9, worst_rate_hi = 0.0, worst_min = 1e9, worst_max = 0.0;
        for (uint32_t js : jits) {
            for (double tick : {TICK_N60_MS, TICK_128K_MS}) {
                const int ticks = static_cast<int>(60000.0 / tick);
                const int settle = static_cast<int>(4000.0 / tick);
                const ChunkRun r = simulate_chunked(ticks, MID_BAND_MS, +0.01, true,
                                                    0, js, settle, tick);
                const double rate = r.skips_after / 56.0;
                if (r.doubles_after != 0 || r.skips_after == 0) onesided = false;
                if (rate < 0.3 || rate > 0.9) rate_ok = false;
                if (r.min_ms < audio_pacing::QUEUE_FLOOR_MS ||
                    r.max_ms >= audio_pacing::QUEUE_MAX_MS) bounds_ok = false;
                worst_rate_lo = std::min(worst_rate_lo, rate);
                worst_rate_hi = std::max(worst_rate_hi, rate);
                worst_min = std::min(worst_min, r.min_ms);
                worst_max = std::max(worst_max, r.max_ms);
            }
        }
        check("AP-15a", "+1% drift: interventions are skips only", onesided);
        check("AP-15b", "+1% drift: skip rate tracks the drift rate",
              rate_ok, fmt_d("lo", worst_rate_lo) + " " + fmt_d("hi", worst_rate_hi));
        check("AP-15c", "+1% drift: queue stays inside [FLOOR, MAX]",
              bounds_ok, fmt_d("min", worst_min) + " " + fmt_d("max", worst_max));
    }
    {
        const uint32_t jits[3] = {0x51C817u, 0xAB12u, 0x9997u};
        bool onesided = true, rate_ok = true, bounds_ok = true;
        double worst_rate_lo = 1e9, worst_rate_hi = 0.0, worst_min = 1e9, worst_max = 0.0;
        for (uint32_t js : jits) {
            for (double tick : {TICK_N60_MS, TICK_128K_MS}) {
                const int ticks = static_cast<int>(60000.0 / tick);
                const int settle = static_cast<int>(4000.0 / tick);
                const ChunkRun r = simulate_chunked(ticks, MID_BAND_MS, -0.01, true,
                                                    0, js, settle, tick);
                const double rate = r.doubles_after / 56.0;
                if (r.skips_after != 0 || r.doubles_after == 0) onesided = false;
                if (rate < 0.3 || rate > 0.9) rate_ok = false;
                if (r.min_ms < audio_pacing::QUEUE_FLOOR_MS ||
                    r.max_ms >= audio_pacing::QUEUE_MAX_MS) bounds_ok = false;
                worst_rate_lo = std::min(worst_rate_lo, rate);
                worst_rate_hi = std::max(worst_rate_hi, rate);
                worst_min = std::min(worst_min, r.min_ms);
                worst_max = std::max(worst_max, r.max_ms);
            }
        }
        check("AP-15d", "-1% drift: interventions are catch-up doubles only", onesided);
        check("AP-15e", "-1% drift: double rate tracks the drift rate",
              rate_ok, fmt_d("lo", worst_rate_lo) + " " + fmt_d("hi", worst_rate_hi));
        check("AP-15f", "-1% drift: queue stays inside [FLOOR, MAX]",
              bounds_ok, fmt_d("min", worst_min) + " " + fmt_d("max", worst_max));
    }

    // --- AP-16: step disturbances recover promptly, then stay silent ---------
    // 30 s runs, zero drift, deterministic chunks. Prompt = the last
    // intervention happens within 60 ticks (~1 s; the raw emergency guards
    // act on the very first tick, the estimator mops up). No oscillation =
    // the correction never changes sign, and once the queue is back in the
    // band the rest of the run is intervention-free.
    {
        const int ticks = static_cast<int>(30000.0 / TICK_N60_MS);
        const ChunkRun r = simulate_chunked(ticks, /*start_ms=*/20.0, 0.0, true,
                                            0, 0, /*settle=*/0);
        check("AP-16a", "queue stepped to 20 ms: recovery within ~1 s",
              r.doubles > 0 && r.last_intervention_tick < 60,
              fmt_d("last_tick", r.last_intervention_tick));
        check("AP-16b", "queue stepped to 20 ms: one-sided, then silent",
              r.skips == 0 && r.last_intervention_tick < 60,
              fmt_d("doubles", r.doubles) + " " + fmt_d("skips", r.skips));
    }
    {
        const int ticks = static_cast<int>(30000.0 / TICK_N60_MS);
        const ChunkRun r = simulate_chunked(ticks, /*start_ms=*/110.0, 0.0, true,
                                            0, 0, /*settle=*/0);
        check("AP-16c", "queue stepped to 110 ms: recovery within ~1 s",
              r.skips > 0 && r.last_intervention_tick < 60,
              fmt_d("last_tick", r.last_intervention_tick));
        check("AP-16d", "queue stepped to 110 ms: one-sided, then silent",
              r.doubles == 0 && r.last_intervention_tick < 60,
              fmt_d("doubles", r.doubles) + " " + fmt_d("skips", r.skips));
    }

    // --- AP-17: the degradation policy (issue #35) --------------------------
    // plan_for() maps the band's answer to what the tick actually does. Only
    // the catch-up is policy-sensitive, because it is the only outcome that
    // costs a frame: a tick presents once, after its frames, so the first
    // frame of a double is overwritten before any paint can serve it.
    //
    // The band itself is NOT parameterised by the policy, and AP-17g is the
    // row that says so: whatever the user prefers, the closed loop modelled by
    // AP-13..AP-16 above must move identically.
    {
        using audio_pacing::WhenSlowPrefer;
        constexpr auto A = WhenSlowPrefer::Audio;
        constexpr auto V = WhenSlowPrefer::Video;

        check("AP-17a", "audio: a catch-up runs two frames inside this tick",
              audio_pacing::plan_for(2, A).frames == 2 &&
                  !audio_pacing::plan_for(2, A).next_tick_asap);
        check("AP-17b", "video: a catch-up runs ONE frame and pulls the next tick in",
              audio_pacing::plan_for(2, V).frames == 1 &&
                  audio_pacing::plan_for(2, V).next_tick_asap);
        check("AP-17c", "an ordinary tick passes through unchanged under both",
              audio_pacing::plan_for(1, A).frames == 1 &&
                  !audio_pacing::plan_for(1, A).next_tick_asap &&
                  audio_pacing::plan_for(1, V).frames == 1 &&
                  !audio_pacing::plan_for(1, V).next_tick_asap);
        // The skip DELAYS a frame, it does not drop one — and declining it
        // would let the queue pass QUEUE_MAX_MS, where SdlAudio drops the push
        // and the hole is an audible click. So video keeps it too.
        check("AP-17d", "both policies keep the 0-frame skip",
              audio_pacing::plan_for(0, A).frames == 0 &&
                  !audio_pacing::plan_for(0, A).next_tick_asap &&
                  audio_pacing::plan_for(0, V).frames == 0 &&
                  !audio_pacing::plan_for(0, V).next_tick_asap);
        // frames_for_tick() never returns more than 2 today, but the policy is
        // stated over "a tick that would supersede a frame", not over the
        // literal value 2: a wider burst must not slip past as frames the
        // video preference never shows.
        check("AP-17e", "video: any multi-frame tick is reduced to one",
              audio_pacing::plan_for(5, V).frames == 1 &&
                  audio_pacing::plan_for(5, V).next_tick_asap);

        // Discriminative end to end: the same starving reading, the same band,
        // two policies, two different plans.
        audio_pacing::BandState band_a{}, band_v{};
        const int starving = audio_pacing::EMERGENCY_LOW_MS - 1;
        const audio_pacing::TickPlan pa =
            audio_pacing::plan_for(audio_pacing::frames_for_tick(band_a, starving), A);
        const audio_pacing::TickPlan pv =
            audio_pacing::plan_for(audio_pacing::frames_for_tick(band_v, starving), V);
        check("AP-17f", "a starving queue: audio supersedes a frame, video does not",
              pa.frames == 2 && !pa.next_tick_asap &&
                  pv.frames == 1 && pv.next_tick_asap);
        check("AP-17g", "the band's estimate moves identically under both policies",
              band_a.smoothed_ms == band_v.smoothed_ms,
              fmt_d("audio", band_a.smoothed_ms) + " " + fmt_d("video", band_v.smoothed_ms));
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
