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
    check("UPGBOX-01", "the 'Upgrade (keep my files)' button is the only one that means Replace",
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonReplace) == UpgradeChoice::Replace &&
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonKeep) != UpgradeChoice::Replace &&
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonNever) != UpgradeChoice::Replace);
    check("UPGBOX-02", "'Not now' keeps, and 'Keep my card (don't ask again)' keeps and asks no more",
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonKeep) == UpgradeChoice::Keep &&
          sdcard::upgrade_choice_for_button(sdcard::kUpgradeButtonNever) == UpgradeChoice::KeepDontAskAgain);
    check("UPGBOX-03", "closing the box, Escape or any unknown button id keeps the card",
          sdcard::upgrade_choice_for_button(0) == UpgradeChoice::Keep &&
          sdcard::upgrade_choice_for_button(-1) == UpgradeChoice::Keep &&
          sdcard::upgrade_choice_for_button(99) == UpgradeChoice::Keep);
    check("UPGBOX-04", "the three button labels are 'Upgrade (keep my files)', 'Keep my card (don't ask again)' and 'Not now'",
          std::string(sdcard::kUpgradeLabelReplace) == "Upgrade (keep my files)" &&
          std::string(sdcard::kUpgradeLabelNever) == "Keep my card (don't ask again)" &&
          std::string(sdcard::kUpgradeLabelKeep) == "Not now");
    check("UPGBOX-06", "with no original image to carry files over from, the affirmative button reads 'Fresh start (keep my card as a backup)'",
          std::string(sdcard::kUpgradeLabelFresh) == "Fresh start (keep my card as a backup)" &&
          std::string(sdcard::kUpgradeLabelFresh) != std::string(sdcard::kUpgradeLabelReplace));
    {
        std::string rep;
        for (int i = 1; i <= 25; ++i) rep += "line " + std::to_string(i) + "\n";
        const std::string t = sdcard::report_box_text(rep, "/x/previous/upgrade-report.txt");
        const std::string shortrep = "a\nb\n";
        check("UPGBOX-05", "the SDL report box shows the first 20 lines and where the full list is; a short report is shown whole",
              t.find("line 20\n") != std::string::npos && t.find("line 21") == std::string::npos &&
              t.find("the full list is in /x/previous/upgrade-report.txt") != std::string::npos &&
              sdcard::report_box_text(shortrep, "/x") == shortrep);
    }
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
