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
// Run: ./build/test/audio_pacing_test

#include "platform/audio_pacing.h"
#include "audio/mixer.h"

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
constexpr double FRAME_HZ_48K = 3'500'000.0 / 69888.0;              // 50.08 Hz
constexpr double SAMPLES_PER_FRAME = Mixer::SAMPLE_RATE / FRAME_HZ_48K;  // 880.6
constexpr double SAMPLES_PER_TICK = Mixer::SAMPLE_RATE * TICK_MS / 1000.0;  // 882

double simulate(int ticks, bool paced, double start_ms)
{
    double queued = Mixer::SAMPLE_RATE * start_ms / 1000.0;  // samples in the device queue
    double min_ms = start_ms;

    for (int t = 0; t < ticks; t++) {
        const int queued_ms = static_cast<int>(queued * 1000.0 / Mixer::SAMPLE_RATE);
        const int frames = paced ? audio_pacing::frames_for_tick(queued_ms) : 1;

        queued += frames * SAMPLES_PER_FRAME;   // emulator produces
        queued -= SAMPLES_PER_TICK;             // sound card consumes
        if (queued < 0.0) queued = 0.0;         // empty: SDL pads with zeros == click

        const double ms = queued * 1000.0 / Mixer::SAMPLE_RATE;
        if (ms < min_ms) min_ms = ms;
    }
    return min_ms;
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
            starved ? 0.0 : audio_pacing::frames_for_tick(queued_ms) * SAMPLES_PER_FRAME;
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

}  // namespace

int main()
{
    std::printf("Audio pacing (issue #7 / Task 23) — host audio-clock tracking\n");
    std::printf("====================================================\n\n");

    // --- AP-01: the policy band ---------------------------------------------
    check("AP-01a", "queue below LOW asks for a catch-up frame",
          audio_pacing::frames_for_tick(audio_pacing::QUEUE_LOW_MS - 1) == 2);
    check("AP-01b", "queue inside the band runs exactly one frame",
          audio_pacing::frames_for_tick(audio_pacing::QUEUE_LOW_MS) == 1 &&
          audio_pacing::frames_for_tick(audio_pacing::QUEUE_HIGH_MS - 1) == 1);
    check("AP-01c", "queue at/above HIGH skips the frame and lets the device drain",
          audio_pacing::frames_for_tick(audio_pacing::QUEUE_HIGH_MS) == 0);
    check("AP-01d", "audio off (negative depth) free-runs at the timer rate",
          audio_pacing::frames_for_tick(-1) == 1);

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

    // --- AP-03: the closed loop never starves the device ---------------------
    // 20000 ticks = 400 s of emulation, far beyond the ~20 s in which the
    // unpaced deficit emptied the queue in the field.
    {
        const double min_ms = simulate(20000, /*paced=*/true, audio_pacing::QUEUE_LOW_MS);
        check("AP-03", "paced: device queue never empties over 400 s",
              min_ms > 0.0, fmt_d("min_queue_ms", min_ms));
        // It should not merely stay off zero — it should stay in a healthy band.
        check("AP-03b", "paced: queue stays at or above the underrun floor",
              min_ms >= audio_pacing::QUEUE_FLOOR_MS, fmt_d("min_queue_ms", min_ms));
    }

    // --- AP-04: ...and the old policy demonstrably did ------------------------
    // Guards the guard: if this ever passes, the simulation has stopped
    // modelling the defect and AP-03 proves nothing.
    {
        const double min_ms = simulate(20000, /*paced=*/false, audio_pacing::QUEUE_LOW_MS);
        check("AP-04", "unpaced (old policy): device queue drains to empty",
              min_ms == 0.0, fmt_d("min_queue_ms", min_ms));
    }

    // --- AP-05: a full queue is pulled back into the band --------------------
    {
        const double min_ms = simulate(20000, /*paced=*/true, audio_pacing::QUEUE_MAX_MS);
        check("AP-05", "paced: an over-full queue converges without emptying",
              min_ms >= audio_pacing::QUEUE_FLOOR_MS, fmt_d("min_queue_ms", min_ms));
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

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
