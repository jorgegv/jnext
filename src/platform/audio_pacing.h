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

/// Never let the device queue fall below this. If the host is too slow to
/// emulate in real time, no amount of pacing can conjure the missing samples,
/// so SdlAudio tops the queue up by holding the last sample level instead of
/// letting SDL inject zeros (a DC hold has no discontinuity, so it stutters
/// rather than clicks).
inline constexpr int QUEUE_FLOOR_MS = 15;

/// Hard ceiling. Beyond this the device is so far ahead (host stall, debugger
/// resume, fastload burst) that we stop feeding it and let it drain.
inline constexpr int QUEUE_MAX_MS = 120;

/// Emulator frames to run this tick, given the current device queue depth in
/// milliseconds (or a negative value when audio is not running, in which case
/// the caller free-runs at its timer rate).
inline constexpr int frames_for_tick(int queued_ms)
{
    if (queued_ms < 0) return 1;                 // no audio: wall-clock paced
    if (queued_ms >= QUEUE_HIGH_MS) return 0;    // ahead of the card: let it drain
    if (queued_ms < QUEUE_LOW_MS) return 2;      // behind the card: catch up
    return 1;
}

}  // namespace audio_pacing
