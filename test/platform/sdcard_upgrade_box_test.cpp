// The SDL frontends' upgrade offer: which choice each button of the message box means.
//
// No VHDL oracle: a host dialog. The oracle is the contract in
// src/platform/sdcard_upgrade_box.h and the owner's decision for the offer of a newer
// NextZXOS distribution (2026-11): "Download and replace" replaces the card, "Keep my card"
// keeps it, a third button keeps it and asks no more, and closing the box or pressing
// Escape can never replace anything. The box itself cannot be shown in a unit test (it needs
// a display); the mapping is the part that decides what happens to the user's files.
#include "platform/sdcard_upgrade_box.h"

#include <cstdio>
#include <string>

#include "../row_id.h"

namespace {
int g_pass = 0, g_fail = 0;
void check(const char* id, const char* desc, bool ok) {
    report_row_id(id);
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  FAIL %s: %s\n", id, desc); }
}
}  // namespace

int main() {
    using sdcard::UpgradeChoice;
    check("UPGBOX-01", "the 'Download and replace' button is the only one that means Replace",
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonReplace) == UpgradeChoice::Replace &&
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonKeep) != UpgradeChoice::Replace &&
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonNever) != UpgradeChoice::Replace);
    check("UPGBOX-02", "'Keep my card' keeps, and the third button keeps and asks no more",
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonKeep) == UpgradeChoice::Keep &&
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonNever) == UpgradeChoice::KeepDontAskAgain);
    check("UPGBOX-03", "closing the box, Escape or any unknown button id keeps the card",
          sdcard::upgrade_choice_for_button(0) == UpgradeChoice::Keep &&
          sdcard::upgrade_choice_for_button(-1) == UpgradeChoice::Keep &&
          sdcard::upgrade_choice_for_button(99) == UpgradeChoice::Keep);
    check("UPGBOX-04", "zenity on Linux with no session bus is the one case where the box would hang",
          sdcard::box_would_hang(true, true, false));
    check("UPGBOX-05", "a session bus, no zenity (SDL's own X11 box) or another OS never means a hang",
          !sdcard::box_would_hang(true, true, true) && !sdcard::box_would_hang(true, false, false) &&
          !sdcard::box_would_hang(false, true, false));
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
