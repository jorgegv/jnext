#pragma once

#include "core/sdcard_provisioner.h"

#include <string>

namespace sdcard {

/// The SDL frontends' UpgradeFn: an SDL message box with three buttons, "Upgrade (keep my
/// files)", "Not now" and "Keep my card (don't ask again)". Closing the box or
/// pressing Escape is Keep. SDL documents SDL_ShowMessageBox as usable before
/// SDL_Init; if no box can be shown (no display, a driver without message boxes)
/// the answer is Keep and the reason is logged, so a session never replaces a card
/// the user was not asked about.
UpgradeChoice sdl_offer_upgrade(const std::string& from, const std::string& to,
                                const std::string& message, bool files_carry_over = true);

/// The button ids of that box and the choice each means, once, for the box and its test.
UpgradeChoice upgrade_choice_for_button(int button_id);
constexpr int kUpgradeButtonReplace = 1, kUpgradeButtonKeep = 2, kUpgradeButtonNever = 3;
/// The labels, in both frontends. "Upgrade" keeps the user's files; closing the box or Escape is
/// "Not now".
constexpr const char* kUpgradeLabelReplace = "Upgrade (keep my files)";
/// Instead of the above when the files cannot be carried over (no original image to compare with).
constexpr const char* kUpgradeLabelFresh   = "Fresh start (keep my card as a backup)";
constexpr const char* kUpgradeLabelNever   = "Keep my card (don't ask again)";
constexpr const char* kUpgradeLabelKeep    = "Not now";

/// The report box of a finished upgrade, SDL: the first 20 lines and where the full list is.
std::string report_box_text(const std::string& report, const std::string& report_path);
void sdl_show_report(const std::string& report);
/// A one-button notice box (SDL): the card in use is newer than this jnext.
void sdl_show_notice(const std::string& message);

}  // namespace sdcard
