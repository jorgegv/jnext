#pragma once

#include <cstdint>

// Task 60e — pure, GUI-free decision policy for "may the machine resume /
// step right now?" when it may be in a corrupt, partially-restored state
// after a failed rewind/step-back (Task 60b).
//
// The Qt layer (DebuggerManager) owns the modal dialog; this class owns the
// *policy* so it can be unit-tested without a display. It is deliberately a
// tiny value type with no dependency on Emulator or Qt.
//
// Corruption is identified by two Emulator observables:
//   * corrupt    — Emulator::last_state_error() is non-empty.
//   * generation — Emulator::state_error_generation(), a monotonic counter
//                  bumped every time a *fresh* corruption is latched. It lets
//                  the guard distinguish "the incident the user already
//                  acknowledged" from "a brand-new corruption that must be
//                  re-confirmed", without the guard ever having to be told
//                  about resets: once the machine is clean again `corrupt`
//                  is false and the guard short-circuits to allow, and any
//                  new failure advances the generation past the acked one.
class ResumeGuard {
public:
    // True  → the caller MUST obtain explicit user confirmation before
    //         resuming/stepping (machine is corrupt and this specific
    //         corruption incident has not been acknowledged yet).
    // False → safe to proceed (machine is clean, or the user already
    //         acknowledged this exact incident).
    bool needs_confirmation(bool corrupt, uint64_t generation) const {
        if (!corrupt) return false;
        return !(acked_ && acked_generation_ == generation);
    }

    // Record that the user acknowledged the corruption incident identified
    // by `generation`. Subsequent needs_confirmation() calls for the SAME
    // generation return false; a later, higher generation re-prompts.
    void record_ack(uint64_t generation) {
        acked_            = true;
        acked_generation_ = generation;
    }

private:
    bool     acked_            = false;
    uint64_t acked_generation_ = 0;
};
