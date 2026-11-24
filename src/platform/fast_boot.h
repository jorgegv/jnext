#pragma once

// ---------------------------------------------------------------------------
// Fast boot: whether a launch runs its first emulated seconds at maximum speed
// (GH #499).
//
// A plain Next start spends about 11.7 emulated seconds in the firmware splash
// and the NextZXOS start-up before anything is interactive. The core opens a
// window of emulated frames in which Emulator::fastload_active() is true (the
// frontends then run uncapped with the sound card unfed); main.cpp arms it
// once, through decide() below. Free of Qt, SDL and the emulator so the table
// is unit-testable. No hardware counterpart: it is host pacing.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <vector>

#include "memory/contention.h"   // MachineType
#include "platform/render_policy.h"

namespace fast_boot {

inline constexpr int DEFAULT_SECONDS = 15;    // the welcome screen completes at frame 577 (11.7 s)
inline constexpr int MAX_SECONDS = 60;
inline constexpr int FRAMES_PER_SECOND = 50;  // seconds are counted as 50 frames each

/// GH #544 - the generation a hidden (black) frame is uploaded under: distinct from
/// render_policy::NOTHING_UPLOADED / EmulatorWidget::NO_GENERATION (~0) and from any
/// real framebuffer generation (a counter from 0), so the black frame is uploaded once and held.
inline constexpr uint64_t HIDDEN_GENERATION = ~uint64_t{0} - 1;
static_assert(HIDDEN_GENERATION != render_policy::NOTHING_UPLOADED);

/// What the window is handed.
struct Shown { const uint32_t* pixels; uint64_t generation; };

/// The emulator's frame, or (hidden) a black one of the same size.
inline Shown shown_frame(bool hidden, const uint32_t* fb, int w, int h, uint64_t generation)
{
    if (!hidden) return { fb, generation };
    static std::vector<uint32_t> black;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (black.size() < n) black.assign(n, 0xFF000000u);
    return { black.data(), HIDDEN_GENERATION };
}

/// What the launch looks like, resolved by main.cpp.
struct Launch {
    int  seconds = 0;           ///< the resolved setting, 0 = off
    bool headless = false;
    bool next_machine = false;  ///< the resolved machine type is the Next
    bool program = false;       ///< --load (any format), --inject, --rzx-play: a program runs at start
    bool scheduled = false;     ///< --delayed-* and --script: work scheduled on the emulated timeline
    bool recording = false;     ///< --record, --wav-record, --wav-record-stems, --rzx-record, --dac-trace, --record-script
};

/// Only the Next boots a firmware worth skipping (the other machines are
/// interactive within 1-3 s, and muting them would hide the 48K's first seconds).
inline constexpr bool is_next(MachineType t) { return t == MachineType::ZXN_ISSUE2; }

/// The first matching reason, in this order, wins.
enum class Why { Armed, Off, Headless, NotNext, Program, Scheduled, Recording };

struct Decision {
    uint32_t frames;
    Why      why;
};

inline constexpr Decision decide(const Launch& l)
{
    if (l.seconds <= 0)    return { 0, Why::Off };
    if (l.headless)        return { 0, Why::Headless };
    if (!l.next_machine)   return { 0, Why::NotNext };
    if (l.program)         return { 0, Why::Program };
    if (l.scheduled)       return { 0, Why::Scheduled };
    if (l.recording)       return { 0, Why::Recording };
    const int s = l.seconds > MAX_SECONDS ? MAX_SECONDS : l.seconds;
    return { static_cast<uint32_t>(s * FRAMES_PER_SECOND), Why::Armed };
}

inline constexpr const char* why_text(Why w)
{
    switch (w) {
    case Why::Armed:     return "armed";
    case Why::Off:       return "set to 0";
    case Why::Headless:  return "--headless";
    case Why::NotNext:   return "not the Next machine";
    case Why::Program:   return "a program is loaded at start";
    case Why::Scheduled: return "the command line schedules work on the emulated timeline";
    case Why::Recording: return "a recording is running";
    }
    return "";
}

} // namespace fast_boot
