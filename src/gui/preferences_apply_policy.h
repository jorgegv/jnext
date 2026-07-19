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
// This header is pure: no Qt, no emulator instance, no I/O, so the decision can
// be pinned down row by row in isolation. That is a convenience, NOT a
// substitute for testing the behaviour: MainWindow::apply_preferences() is
// itself under test in test/gui/preferences_apply_test.cpp, which builds a real
// MainWindow under the offscreen QPA platform (the esc_break_test.cpp pattern)
// and asserts on what actually happens to the running machine.

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

// NOTE — there were two more predicates here (`preferences_reboots`,
// `preferences_applies_live_settings`). Both ignored their argument, or
// returned a constant, and NEITHER was called from production code: they
// existed only so tests could assert them. That made their six test rows
// unfailable no matter what apply_preferences() did — a tautology wearing a
// function as a disguise, which is precisely what test/lint-assertions.sh
// exists to catch and which it misses because it greps for a literal `true`.
// The rules they claimed to pin ("only one outcome reboots", "live settings
// apply in every outcome") are real, so they are now asserted where they can
// actually fail: against a live MainWindow in preferences_apply_test.cpp.
