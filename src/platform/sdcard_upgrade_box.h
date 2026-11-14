#pragma once

#include "core/sdcard_provisioner.h"

#include <string>

namespace sdcard {

/// The SDL frontends' UpgradeFn: an SDL message box with three buttons, "Download and
/// replace", "Keep my card" and "Keep my card, do not ask again". Closing the box or
/// pressing Escape is Keep. SDL documents SDL_ShowMessageBox as usable before
/// SDL_Init; if no box can be shown (no display, a driver without message boxes)
/// the answer is Keep and the reason is logged, so a session never replaces a card
/// the user was not asked about.
UpgradeChoice sdl_offer_upgrade(const std::string& from, const std::string& to,
                                const std::string& message);

/// The button ids of that box and the choice each means, once, for the box and its test.
UpgradeChoice upgrade_choice_for_button(int button_id);
constexpr int kUpgradeButtonReplace = 1, kUpgradeButtonKeep = 2, kUpgradeButtonNever = 3;

}  // namespace sdcard
