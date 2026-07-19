#pragma once

/// Audio-clock pacing policy (GitHub issue #7 / Task 23).
///
/// Audio is synthesised on the *emulated* clock: the mixer emits exactly
/// 44100 samples per emulated second. The frontends, however, run the
/// emulator from a 20 ms wall-clock timer — 50.00 frames per real second.
/// A 48K machine's frame is 69888 T-states at 3.5 MHz = 50.08 Hz, so 50
/// frames of emulation are only 0.9984 s of emulated time: we synthesise
/// ~44030 samples per real second while the sound card consumes 44100.
///
/// That ~70 sample/s deficit is permanent. It drains the audio device queue
/// to empty within ~20 s, after which SDL pads the device buffer with ZEROS
/// on every shortfall — a hard step from the current signal level to 0 and
/// back, several times a second, for the rest of the session. On a host with
/// little CPU headroom the emulator falls further behind still and the holes
/// become continuous. That is the "constant noise" of issue #7 and the
/// clicking over beeper music of Task 23: one bug, two volumes.
///
/// The cure is to stop pacing emulation on the wall clock and pace it on the
/// sound card's clock instead: run an extra frame when the device queue runs
/// low, skip one when it runs high. Emulation then tracks the audio device
/// exactly and the queue never empties.
namespace audio_pacing {

/// Target band for the audio device queue.
/// Below LOW we are falling behind the sound card; above HIGH we are ahead.
inline constexpr int QUEUE_LOW_MS = 40;
inline constexpr int QUEUE_HIGH_MS = 90;

/// Band smoothing (Task 63, landed with the deadline scheduler in
/// frame_deadline.h).
///
/// WHY a smoothed reading, and why this design:
/// the audio device consumes in ~23.2 ms chunks (1024 frames at 44100 Hz),
/// so every queued_ms() reading carries a ~23 ms sawtooth around the true
/// queue mean. The original stateless band intervened whenever a single
/// reading clipped an edge; with the old +1.1% timer bias gone (the
/// deadline scheduler paces the timer exactly) the queue mean is a
/// zero-drift random walk, and whenever it wanders within sawtooth reach of
/// an edge the teeth clip it tick after tick — measured ~2.5
/// interventions/s, each a visible one-frame jump or hold. The band
/// thresholds therefore now act on an ESTIMATE OF THE MEAN (EMA over the
/// readings), which the sawtooth cannot reach: steady state is silent, and
/// only a genuine sustained drift or disturbance moves the estimate across
/// a threshold — promptly and one-sidedly.
///
/// A trigger/re-arm hysteresis (episodes intervening until the queue is
/// back at mid-band) was prototyped first and REJECTED on model evidence:
/// episode termination sees instantaneous readings, which at the moment of
/// crossing are sawtooth extremes (drain landings sit right after a chunk
/// removal, catch-up landings right after a double), so every episode lands
/// the queue MEAN up to a whole chunk away from the re-arm point. In a
/// 50 ms band with 23 ms chunks that parks the mean within sawtooth reach
/// of the OPPOSITE edge — the model measured ~0.36/s wrong-side bounces
/// under 1% drift, interventions the design exists to remove.
///
/// Two auxiliary mechanisms make the EMA a sound controller (both are
/// load-bearing, see audio_pacing_test AP-13/AP-15/AP-16):
///   * ACTION FEED-FORWARD: an intervention changes the real queue by one
///     frame (~17-20 ms) immediately; the estimate is advanced by
///     INTERVENTION_MS at the moment of the decision, otherwise the EMA
///     lag would re-trigger on the next few readings and over-correct.
///   * ENVELOPE EMERGENCIES on the RAW reading: the smoothed estimate is
///     deliberately slow, so it alone would let a sustained drift push the
///     raw queue past the hard envelope before it reacts (modelled: max
///     ~131 ms > QUEUE_MAX_MS where SdlAudio drops pushes, min ~17 ms <
///     QUEUE_FLOOR_MS where the hold-pad becomes routine). Two raw-reading
///     guards therefore act IMMEDIATELY, outside the smoothing:
///       - reading >= QUEUE_MAX_MS - INTERVENTION_MS: skip now — one more
///         frame could push the queue past QUEUE_MAX_MS and get the push
///         dropped (a hole). INTERVENTION_MS covers the longest machine
///         frame (20.26 ms at 49.36 Hz), making the ceiling structural.
///       - reading < EMERGENCY_LOW_MS: catch up now — the queue is at the
///         band's low edge for real (sawtooth or not, the device really is
///         that low) and the next dip could reach QUEUE_FLOOR_MS.
///     Neither guard is reachable from a mid-band mean through the ~23 ms
///     sawtooth alone, so steady state stays silent (AP-14).
///
/// SMOOTH_SHIFT: EMA weight 1/32 per tick = time constant ~32 ticks
/// (~0.55 s at the 58 Hz Next-60 tick). Slow enough to flatten the ~23 ms
/// sawtooth to ~2 ms of residual ripple, fast enough that genuine drift
/// (~10 ms/s at 1%) crosses a threshold within ~1 s of the true mean.
inline constexpr int SMOOTH_SHIFT = 5;  // EMA weight = 2^-5 = 1/32
/// Queue change one intervention causes: one emulated frame, rounded UP to
/// the longest machine frame (20.26 ms at 49.36 Hz) so the high emergency
/// guard below is a hard ceiling on every machine; the up-to ~4 ms
/// feed-forward mismatch at Next-60 decays through the EMA.
inline constexpr int INTERVENTION_MS = 21;
/// Low emergency threshold: one ms below the band edge. Readings under it
/// mean the device REALLY is at the bottom of the band (whatever the
/// sawtooth phase), so a catch-up frame runs immediately instead of
/// waiting ~a second for the estimate. Calibrated in audio_pacing_test's
/// chunked model: at 39 a jittered double-chunk dip cannot pierce
/// QUEUE_FLOOR_MS; at 38 it can (AP-15f).
inline constexpr int EMERGENCY_LOW_MS = 39;

/// Never let the device queue fall below this. If the host is too slow to
/// emulate in real time, no amount of pacing can conjure the missing samples,
/// so SdlAudio tops the queue up by holding the last sample level instead of
/// letting SDL inject zeros (a DC hold has no discontinuity, so it stutters
/// rather than clicks).
///
/// This MUST exceed the frontends' ~20 ms tick period: the guard can only run
/// when a tick runs, so a floor below one tick cannot bridge even a single late
/// tick — the device would drain to empty before the next push and SDL would
/// inject zeros anyway. It must also stay below QUEUE_LOW_MS so that pacing,
/// not padding, does the work on a host that can keep up (padding inserts
/// samples the emulator never produced, so it must remain a rescue, not a
/// routine).
inline constexpr int QUEUE_FLOOR_MS = 30;

/// Hard ceiling. Beyond this the device is so far ahead (host stall, debugger
/// resume, fastload burst) that we stop feeding it and let it drain.
inline constexpr int QUEUE_MAX_MS = 120;

/// Band-smoothing state. Each frontend owns ONE instance next to its audio
/// device (QtApp and SdlApp each have their own — never a global) and passes
/// it to every frames_for_tick() call for that device.
struct BandState {
    /// Smoothed queue-depth estimate in ms. Negative = unseeded: the next
    /// reading seeds it directly (so the very first decision, and the first
    /// after the device goes away, sees the raw depth — no cold-start lag).
    double smoothed_ms = -1.0;
};

/// Emulator frames to run this tick, given the current device queue depth in
/// milliseconds (or a negative value when audio is not running, in which case
/// the caller free-runs at its timer rate).
///
/// Trigger points are unchanged (QUEUE_LOW_MS / QUEUE_HIGH_MS) but act on
/// the smoothed estimate (see the WHY above): the chunk sawtooth on raw
/// readings can no longer clip the band edges, while a genuine drift or
/// disturbance still crosses them promptly and one-sidedly.
inline constexpr int frames_for_tick(BandState& st, int queued_ms)
{
    if (queued_ms < 0) {          // no audio: wall-clock paced. The estimate
        st.smoothed_ms = -1.0;    // dies with the device; a later device
        return 1;                 // re-seeds from its first reading.
    }
    if (st.smoothed_ms < 0.0) {
        st.smoothed_ms = queued_ms;  // seed: first reading for this device
    }
    st.smoothed_ms += (queued_ms - st.smoothed_ms) / (1 << SMOOTH_SHIFT);

    // Envelope emergencies act on the RAW reading (see the WHY above).
    if (queued_ms >= QUEUE_MAX_MS - INTERVENTION_MS) {
        st.smoothed_ms -= INTERVENTION_MS;   // feed the skip's effect forward
        return 0;
    }
    if (queued_ms < EMERGENCY_LOW_MS) {
        st.smoothed_ms += INTERVENTION_MS;   // feed the double's effect forward
        return 2;
    }

    // Cadence control acts on the smoothed estimate.
    if (st.smoothed_ms >= QUEUE_HIGH_MS) {   // ahead of the card: let it drain
        st.smoothed_ms -= INTERVENTION_MS;
        return 0;
    }
    if (st.smoothed_ms < QUEUE_LOW_MS) {     // behind the card: catch up
        st.smoothed_ms += INTERVENTION_MS;
        return 2;
    }
    return 1;
}

/// Stereo sample pairs that `ms` of playback occupies at `sample_rate`.
inline constexpr int ms_to_samples(int ms, int sample_rate)
{
    return sample_rate * ms / 1000;
}

/// Stereo sample pairs the underrun guard must append to lift a device queue of
/// `queued_samples` back to QUEUE_FLOOR_MS — zero when it is already at or above
/// the floor, which is the case on any host that keeps up (pacing holds the
/// queue in [QUEUE_LOW_MS, QUEUE_HIGH_MS), well above the floor). The caller
/// appends this many copies of the last sample it emitted; see SdlAudio.
inline constexpr int underrun_pad_samples(int queued_samples, int sample_rate)
{
    const int floor_samples = ms_to_samples(QUEUE_FLOOR_MS, sample_rate);
    return queued_samples < floor_samples ? floor_samples - queued_samples : 0;
}

}  // namespace audio_pacing
