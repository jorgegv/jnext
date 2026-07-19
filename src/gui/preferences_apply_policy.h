#pragma once

// Issue #40 — what pressing "Apply" in the Preferences dialog is allowed to do.
//
// "Apply" means *apply these settings to what is running*. Every setting in the
// dialog can be pushed into a live machine EXCEPT the machine type: a 48K and a
// Next are different machines, and switching between them is a power cycle, not
// a setting. The old code re-applied the machine type unconditionally on every
// Apply, so changing a joypad source rebooted the machine and threw away
// whatever the user was running.
//
// The rule, therefore:
//   - the machine type needs a restart ONLY when it actually differs from the
//     machine currently running;
//   - when it does differ, the restart is never silent — the user is asked;
//   - if the user declines, the machine keeps running and the new type is left
//     persisted for the next launch (the dialog has already saved it), which is
//     the "defer to next launch" half of the two options the issue allows;
//   - everything else applies live and silently, always.
//
// This header is pure: no Qt, no emulator instance, no I/O. MainWindow itself
// is unreachable by the unit tests (Task 91), so the decision it makes lives
// here where it can be pinned down row by row.

#include "memory/contention.h"   // MachineType

/// Does applying `requested` to a machine currently running `running` require a
/// cold boot? The machine type is the ONLY preference that cannot be applied to
/// a running machine.
constexpr bool preferences_restart_required(MachineType running,
                                            MachineType requested) {
    return running != requested;
}

/// What an Apply should actually do.
enum class PreferencesApplyOutcome {
    /// No restart needed: push every live-applicable setting and nothing else.
    ApplyLiveOnly,
    /// The machine type changed and the user accepted the restart: cold boot
    /// into the new machine, then apply the live settings to it.
    RebootThenApplyLive,
    /// The machine type changed and the user declined: keep the machine
    /// running, apply the live settings, and leave the new type for next
    /// launch (it is already persisted).
    DeferMachineType,
};

/// Resolve the Apply outcome. `confirmed` is only meaningful when a restart is
/// required; when none is, the user is never asked, so its value cannot change
/// the result.
constexpr PreferencesApplyOutcome preferences_apply_outcome(bool restart_required,
                                                            bool confirmed) {
    if (!restart_required) return PreferencesApplyOutcome::ApplyLiveOnly;
    return confirmed ? PreferencesApplyOutcome::RebootThenApplyLive
                     : PreferencesApplyOutcome::DeferMachineType;
}

/// Live settings are applied in every outcome — an Apply always applies what it
/// can. Stated as its own predicate so the "declining the restart must not also
/// throw away the joypad change" rule is pinned, not implied.
constexpr bool preferences_applies_live_settings(PreferencesApplyOutcome) {
    return true;
}

/// Only one outcome may touch the emulator's machine type.
constexpr bool preferences_reboots(PreferencesApplyOutcome outcome) {
    return outcome == PreferencesApplyOutcome::RebootThenApplyLive;
}
