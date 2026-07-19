// Preferences "Apply" decision policy test (GitHub issue #40).
//
// No VHDL oracle: this is a *host UX* policy, not emulated hardware. Its oracle
// is the contract stated in issue #40 and restated in
// src/gui/preferences_apply_policy.h:
//
//   * "Apply" applies settings to the RUNNING machine; it must never restart
//     the machine as a side effect of an unrelated setting;
//   * the machine type is the ONE setting that cannot be applied live, and it
//     needs a restart ONLY when it actually differs from the running machine;
//   * when it does differ, the restart is explicit — the user is asked;
//   * if the user declines, the machine keeps running and the new type is left
//     for the next launch;
//   * the live-applicable settings are applied in EVERY outcome — declining a
//     machine-type restart must not discard the joypad change the user came to
//     make.
//
// Every row below derives from that contract, never from reading the
// implementation back.
//
// Run: ./build/test/preferences_apply_policy_test

#include "gui/preferences_apply_policy.h"

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

const char* type_name(MachineType t)
{
    switch (t) {
    case MachineType::ZXN_ISSUE2: return "ZXN_ISSUE2";
    case MachineType::ZX48K:      return "ZX48K";
    case MachineType::ZX128K:     return "ZX128K";
    case MachineType::ZX_PLUS3:   return "ZX_PLUS3";
    }
    return "?";
}

const char* outcome_name(PreferencesApplyOutcome o)
{
    switch (o) {
    case PreferencesApplyOutcome::ApplyLiveOnly:       return "ApplyLiveOnly";
    case PreferencesApplyOutcome::RebootThenApplyLive: return "RebootThenApplyLive";
    case PreferencesApplyOutcome::DeferMachineType:    return "DeferMachineType";
    }
    return "?";
}

const std::vector<MachineType> kAllTypes = {
    MachineType::ZXN_ISSUE2, MachineType::ZX48K,
    MachineType::ZX128K,     MachineType::ZX_PLUS3,
};

}  // namespace

int main()
{
    std::printf("preferences_apply_policy_test (Apply must not reboot, issue #40)\n");

    // --- PAP-01: an Apply that does not change the machine needs no restart --
    // THE defect. A user changes a joypad source and presses Apply; the machine
    // type field still holds the machine that is already running. Nothing about
    // that requires a power cycle, for ANY machine type.
    for (MachineType t : kAllTypes) {
        check("PAP-01", "same machine type requires no restart",
              !preferences_restart_required(t, t),
              std::string("running=requested=") + type_name(t));
    }

    // --- PAP-02: every genuine machine-type change does need a restart -------
    // The full off-diagonal of the matrix, one row per ordered pair: a 48K and
    // a Next are different machines and cannot be swapped under a running
    // program. Enumerated rather than sampled so no single pair can regress
    // unnoticed.
    for (MachineType from : kAllTypes) {
        for (MachineType to : kAllTypes) {
            if (from == to) continue;
            check("PAP-02", "differing machine type requires a restart",
                  preferences_restart_required(from, to),
                  std::string(type_name(from)) + " -> " + type_name(to));
        }
    }

    // --- PAP-03: no restart required => apply live, never reboot ------------
    // `confirmed` is meaningless here because the user is never asked; both
    // values must give the identical answer. Asserted separately so a policy
    // that leaked the unasked flag into the result is caught.
    check("PAP-03a", "no restart required, confirmed=false => ApplyLiveOnly",
          preferences_apply_outcome(false, false) ==
              PreferencesApplyOutcome::ApplyLiveOnly,
          outcome_name(preferences_apply_outcome(false, false)));
    check("PAP-03b", "no restart required, confirmed=true => ApplyLiveOnly",
          preferences_apply_outcome(false, true) ==
              PreferencesApplyOutcome::ApplyLiveOnly,
          outcome_name(preferences_apply_outcome(false, true)));

    // --- PAP-04: restart required + user accepted => reboot -----------------
    check("PAP-04", "restart required and confirmed => RebootThenApplyLive",
          preferences_apply_outcome(true, true) ==
              PreferencesApplyOutcome::RebootThenApplyLive,
          outcome_name(preferences_apply_outcome(true, true)));

    // --- PAP-05: restart required + user declined => defer, do NOT reboot ---
    // The other half of the issue's "prompt or defer": declining must leave the
    // running machine alone.
    check("PAP-05", "restart required and declined => DeferMachineType",
          preferences_apply_outcome(true, false) ==
              PreferencesApplyOutcome::DeferMachineType,
          outcome_name(preferences_apply_outcome(true, false)));

    // NOTE — PAP-06 and PAP-07 used to live here, asserting
    // preferences_reboots() and preferences_applies_live_settings(). Both
    // helpers ignored their argument or returned a constant and were called
    // from NO production code, so those six rows could not fail whatever
    // apply_preferences() did. The rules they claimed to pin ("only one
    // outcome reboots", "declining a restart still applies the joypad change")
    // are real and load-bearing, so they are now asserted against a live
    // MainWindow in test/gui/preferences_apply_test.cpp, where a mutation to
    // the control flow actually trips them.

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
