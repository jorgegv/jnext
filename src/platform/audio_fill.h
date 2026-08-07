#pragma once

#include <cstdint>

/// Device-boundary audio fill/hold policy (GitHub issue #208).
///
/// WHY THIS EXISTS — the tick-side underrun pad can never be sufficient.
///
/// SdlAudio's push side already pads the queue up to QUEUE_FLOOR_MS with a
/// last-level hold (audio_pacing::underrun_pad_samples). But that pad runs
/// only when a frontend tick runs, and issue #208 is precisely the case where
/// ticks stop arriving on time: a host CPU-throttled below real time, a Qt
/// timer silently degraded to 15.6 ms SetTimer granularity, a Win32 modal
/// move/size loop blocking the GUI thread for the whole duration of a window
/// drag. Measured on a throttled host, inter-push gaps exceed the 30 ms floor
/// and the device plays content jnext never chose — SDL's queue model injects
/// ZEROS for the shortfall, and with any nonzero programme content those zeros
/// are hard steps to 0 and back: the click-train / motor noise of issue #7.
///
/// THE FIX moves the last line of defense to the DEVICE BOUNDARY: the SDL
/// audio callback, which runs on SDL's high-priority audio thread at exactly
/// the moment the device needs samples, regardless of what the GUI thread is
/// doing. For any shortfall the callback repeats the LAST REAL SAMPLE PAIR it
/// delivered — a DC hold has no discontinuity, so a starved host stutters
/// instead of clicking. The device can now never play content jnext did not
/// choose.
///
/// This header is the PURE policy for that callback: the real/fill split, the
/// ring buffer the producer and consumer share, the hold-level tracking, and
/// the diagnostic counters. SdlAudio owns the locking (SDL_LockAudioDevice on
/// the producer side; SDL itself holds the device lock around the callback)
/// and calls into these functions; everything here is unit-testable with no
/// SDL at all (audio_fill_test).
namespace audio_fill {

/// Ring capacity in stereo pairs: 250 ms at 44100 Hz.
///
/// WHY 250 ms — bounded above by construction, not by tuning. The push side
/// refuses to push past audio_pacing::QUEUE_MAX_MS (120 ms), and ONE push
/// after that check can add at most the mixer's own ring (4096 pairs ~ 93 ms).
/// 120 + 93 = 213 ms, plus one ~23 ms device chunk of slack, still fits — so
/// a correctly-guarded producer can never overflow this ring, and ring_push's
/// truncation path is a can't-happen diagnostic, not a working mode.
/// (sdl_audio.cpp static_asserts this constant against Mixer::SAMPLE_RATE and
/// the mixer ring size so the relation cannot silently rot.)
inline constexpr int RING_CAPACITY_PAIRS = 11025;  // 44100 * 250 / 1000

/// The producer/consumer sample ring: stereo int16 pairs, fixed storage, no
/// allocation. POD on purpose — the audio thread reads it inside a device
/// callback, so construction/destruction must be trivial.
struct Ring {
    int16_t data[RING_CAPACITY_PAIRS * 2] = {};
    int head = 0;  ///< index (in pairs) of the first unread pair
    int size = 0;  ///< pairs currently queued
};

/// Append up to `pairs` stereo pairs; returns how many were ACCEPTED. A short
/// return means the ring was full — see RING_CAPACITY_PAIRS for why a guarded
/// producer never hits that; the caller treats it as a loud diagnostic.
/// Truncating (rather than overwriting the oldest) is deliberate: overwriting
/// would move `head` under the consumer's feet and splice a discontinuity into
/// samples already promised to the device.
inline int ring_push(Ring& r, const int16_t* pcm, int pairs)
{
    if (pairs <= 0) return 0;
    const int free_pairs = RING_CAPACITY_PAIRS - r.size;
    const int n = pairs < free_pairs ? pairs : free_pairs;
    for (int i = 0; i < n; i++) {
        const int at = (r.head + r.size + i) % RING_CAPACITY_PAIRS;
        r.data[at * 2]     = pcm[i * 2];
        r.data[at * 2 + 1] = pcm[i * 2 + 1];
    }
    r.size += n;
    return n;
}

/// Pop up to `pairs` stereo pairs into `out`; returns how many were delivered.
inline int ring_pop(Ring& r, int16_t* out, int pairs)
{
    const int n = pairs < r.size ? (pairs > 0 ? pairs : 0) : r.size;
    for (int i = 0; i < n; i++) {
        const int at = (r.head + i) % RING_CAPACITY_PAIRS;
        out[i * 2]     = r.data[at * 2];
        out[i * 2 + 1] = r.data[at * 2 + 1];
    }
    r.head = (r.head + n) % RING_CAPACITY_PAIRS;
    r.size -= n;
    return n;
}

/// Consumer-side state: the hold level and the diagnostic counters. Owned by
/// the audio thread (SdlAudio drains the counters under the device lock).
struct State {
    /// The hold level = the last REAL pair delivered to the device. Starts at
    /// exact 0/0: silence rests at exact zero since GH #116, so a callback
    /// that fires before the first push fills with genuine silence — which is
    /// also what the device played before jnext existed. Updated ONLY from
    /// real samples; fill output never feeds back into it.
    int16_t last_l = 0;
    int16_t last_r = 0;

    /// Edge detector for fill_events: was the PREVIOUS callback short?
    /// Survives take_stats() — see there for why.
    bool in_fill = false;

    /// Diagnostic counters, drained per status window by take_stats().
    uint64_t real_pairs  = 0;  ///< pairs delivered from the ring
    uint64_t fill_pairs  = 0;  ///< pairs manufactured by the hold
    uint64_t fill_events = 0;  ///< no-fill -> fill callback transitions
};

/// How a device request splits between real samples and hold fill.
struct Split {
    int real = 0;
    int fill = 0;
};

/// Pure split: of `requested` pairs, how many can be real (ring supply) and
/// how many must be hold fill. Negative inputs clamp to zero (defensive — a
/// device never requests negative bytes, and an empty ring is size 0).
inline constexpr Split split_request(int requested, int available)
{
    Split s;
    if (requested <= 0) return s;
    const int avail = available > 0 ? available : 0;
    s.real = requested < avail ? requested : avail;
    s.fill = requested - s.real;
    return s;
}

/// The device callback's entire policy, in order:
///   1. pop what the ring has (the real samples, delivered first — the device
///      must play everything the emulator produced before any fill);
///   2. advance the hold level to the LAST real pair delivered (the fill must
///      continue from where the real signal stopped, not from where it stood
///      a callback ago);
///   3. write the shortfall as copies of the hold level (DC hold — never
///      zeros: zeros spliced into a live waveform are the #7 click train);
///   4. account counters, and count a fill EVENT on the no-fill -> fill edge
///      (a 3-second starvation is one event over many callbacks, not
///      hundreds).
/// A zero-pair request delivers nothing and changes nothing — it carries no
/// edge information, so it must not re-arm or clear the event detector.
inline Split fill_request(Ring& ring, State& st, int16_t* out, int requested_pairs)
{
    if (requested_pairs <= 0) return Split{};

    const Split s = split_request(requested_pairs, ring.size);

    const int real = ring_pop(ring, out, s.real);
    // ring_pop cannot deliver short of s.real (the split was computed from
    // ring.size), but derive from what actually happened, not from intent.
    if (real > 0) {
        st.last_l = out[(real - 1) * 2];
        st.last_r = out[(real - 1) * 2 + 1];
    }
    const int fill = requested_pairs - real;
    for (int i = 0; i < fill; i++) {
        out[(real + i) * 2]     = st.last_l;
        out[(real + i) * 2 + 1] = st.last_r;
    }

    st.real_pairs += static_cast<uint64_t>(real);
    st.fill_pairs += static_cast<uint64_t>(fill);
    if (fill > 0) {
        if (!st.in_fill) ++st.fill_events;
        st.in_fill = true;
    } else {
        st.in_fill = false;
    }
    return Split{real, fill};
}

/// One drained diagnostics window (what SdlAudio::take_fill_stats returns).
struct StatsWindow {
    uint64_t real_pairs  = 0;
    uint64_t fill_pairs  = 0;
    uint64_t fill_events = 0;
};

/// Drain the counters for one reporting window and zero them. Deliberately
/// does NOT touch `in_fill` or the hold level: `in_fill` is the event edge
/// detector, and resetting it at a window boundary would re-count one ongoing
/// starvation as a fresh event every window — the report would then scale
/// with the observer's cadence instead of with what happened. The hold level
/// is playback state, not a statistic.
inline StatsWindow take_stats(State& st)
{
    const StatsWindow w{st.real_pairs, st.fill_pairs, st.fill_events};
    st.real_pairs  = 0;
    st.fill_pairs  = 0;
    st.fill_events = 0;
    return w;
}

}  // namespace audio_fill
